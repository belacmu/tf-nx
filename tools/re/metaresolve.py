#!/usr/bin/env python3
"""Type-qualified metadata resolver: resolve (TypeName, MethodName) -> body.
Each class has a metadata-BUILDER function that references its type-name token
then stores its method records. So: xref the type token -> builder site;
within the builder span, find the method-name store -> next adrp+add = invoke
wrapper -> wrapper's first bl = concrete body.
"""
import struct, sys
from capstone import Cs, CS_ARCH_ARM64, CS_MODE_LITTLE_ENDIAN

img = open("main.flat", "rb").read()
TEXT_END = 0x28626c0
md = Cs(CS_ARCH_ARM64, CS_MODE_LITTLE_ENDIAN)

def token(name):
    u = name.encode("utf-16-le"); off = 0
    while True:
        i = img.find(u, off)
        if i < 0: return None
        if img[i+len(u):i+len(u)+2] == b"\x00\x00" and img[i-2:i] == b"\x00\x00":
            return i
        off = i + 1

def adrp_val(insn, pc):
    immlo=(insn>>29)&3; immhi=(insn>>5)&0x7FFFF; imm=(immhi<<2)|immlo
    if imm&(1<<20): imm-=(1<<21)
    return ((pc&~0xFFF)+(imm<<12))&0xFFFFFFFFFFFF, insn&0x1F

def store_sites(target, lo, hi):
    """offsets in [lo,hi) whose adrp+add computes `target`."""
    tp, tl = target & ~0xFFF, target & 0xFFF; hits=[]
    for off in range(lo, hi-4, 4):
        insn = struct.unpack_from("<I", img, off)[0]
        if (insn & 0x9F000000)!=0x90000000: continue
        page, rd = adrp_val(insn, off)
        if page!=tp: continue
        i2 = struct.unpack_from("<I", img, off+4)[0]
        if (i2 & 0xFF800000)==0x91000000 and ((i2>>5)&0x1F)==rd and (i2&0x1F)==rd and ((i2>>10)&0xFFF)==tl:
            hits.append(off)
    return hits

def next_adrp_add(after_off, limit=8):
    o = after_off
    for _ in range(limit*2):
        insn = struct.unpack_from("<I", img, o)[0]
        if (insn & 0x9F000000)==0x90000000:
            page, rd = adrp_val(insn, o)
            i2 = struct.unpack_from("<I", img, o+4)[0]
            if (i2 & 0xFF800000)==0x91000000 and ((i2>>5)&0x1F)==rd:
                return page + ((i2>>10)&0xFFF)
        o += 4
    return None

def classify_wrapper(func):
    """Return ('DIRECT', body) for a non-virtual method, or ('VIRTUAL', slot)
    when the wrapper is a vtable-dispatch thunk (ldr x8,[x0]; ldr x8,[x8,#8];
    ldr x8,[x8,#SLOT]; ...; br)."""
    if not func or func>=TEXT_END: return ("UNKNOWN", None)
    ins = list(md.disasm(img[func:func+0x30], func))
    # detect dispatch thunk: two chained loads off [x0] then a load at #SLOT
    for k in range(len(ins)-2):
        a,b,c = ins[k], ins[k+1], ins[k+2]
        if (a.mnemonic=="ldr" and "[x0]" in a.op_str and
            b.mnemonic=="ldr" and "#8]" in b.op_str and
            c.mnemonic=="ldr" and c.op_str.startswith("x")):
            # c like: ldr x8, [x8, #0x40]
            if "#" in c.op_str and "]" in c.op_str:
                try:
                    slot = int(c.op_str.split("#")[1].split("]")[0], 16)
                    return ("VIRTUAL", slot)
                except: pass
    # else non-virtual: the concrete body is a bl/tail-b target in the main code
    # region. Guarded wrappers also bl a tiny init thunk first, so collect all
    # branch targets and pick the last real body (>= 0x100000, i.e. not a stub).
    ins2 = list(md.disasm(img[func:func+0x80], func))
    bodies = []
    for insn in ins2:
        if insn.mnemonic == "b":                 # tail-call = body; ends function
            t = int(insn.op_str[1:], 16)
            if 0x100000 <= t < TEXT_END:
                bodies.append(t)
            break
        if insn.mnemonic == "bl":
            t = int(insn.op_str[1:], 16)
            if 0x100000 <= t < TEXT_END:          # skip tiny init/PLT thunks
                bodies.append(t)
        if insn.mnemonic == "ret":               # don't run into the next function
            break
    if not bodies:
        return ("UNKNOWN", None)
    if len(bodies) == 1:
        return ("DIRECT", bodies[0])
    # multiple candidates (guarded wrapper): the concrete body has real game
    # callers; internal thunks/helpers do not. Pick the max-caller target.
    best = max(bodies, key=lambda b: bl_callers(b))
    return ("DIRECT", best)

def bl_callers(target):
    if not target: return 0
    n=0
    for off in range(0, TEXT_END-4, 4):
        insn = struct.unpack_from("<I", img, off)[0]
        if (insn & 0xFC000000)==0x94000000:
            imm = insn & 0x03FFFFFF
            if imm & (1<<25): imm -= (1<<26)
            if off + imm*4 == target: n+=1
    return n

def type_resolve(typename, methodname, span=0x9000):
    ttok = token(typename)
    if ttok is None: return ("NO_TYPE_TOKEN", None, None)
    # builder site(s) referencing the type name
    tsites = store_sites(ttok, 0, TEXT_END)
    mtok = token(methodname)
    if mtok is None: return ("NO_METHOD_TOKEN", None, None)
    out=[]
    for ts in tsites:
        # bound this builder to the nearest method store after ts (first one only:
        # take the store closest to the type-name ref to avoid the next class)
        msites = store_sites(mtok, ts, ts+span)
        if msites:
            ms = min(msites)                 # nearest store to this builder
            wrapper = next_adrp_add(ms+8)
            kind, val = classify_wrapper(wrapper)
            callers = bl_callers(val) if kind=="DIRECT" else 0
            out.append((ts, ms, wrapper, kind, val, callers))
    return ("OK", ttok, out)

if __name__ == "__main__":
    # (Type, Method) hook set the TowerBall port needs.
    targets = [
        ("TowerFall.Player","Added"), ("TowerFall.Player","ShootArrow"),
        ("TowerFall.Player","CollectArrows"), ("TowerFall.Player","Die"),
        ("TowerFall.Player","Update"), ("TowerFall.Player","DuckingUpdate"),
        ("TowerFall.Player","HUDRender"),
        ("TowerFall.PlayerCorpse","Update"),
        ("TowerFall.TowerMapData","GetLevelSystem"),
        # RoundLogic (the mode's brain — all virtual overrides on PC)
        ("TowerFall.RoundLogic","OnUpdate"), ("TowerFall.RoundLogic","OnRoundStart"),
        ("TowerFall.RoundLogic","OnLevelLoadFinish"), ("TowerFall.RoundLogic","OnPlayerDeath"),
        ("TowerFall.VersusRoundLogic","OnUpdate"),
        # Arrow / ToyArrow (the ball reuses this)
        ("TowerFall.Arrow","Update"), ("TowerFall.Arrow","OnCollideH"),
        ("TowerFall.Arrow","OnCollideV"), ("TowerFall.ToyArrow","Update"),
        # Level plumbing (spawn entities / read XML markers)
        ("TowerFall.Level","GetXMLPositions"),
        ("TowerFall.Explosion","Spawn"),
    ]
    print(f"{'method':40} {'kind':8} {'addr/slot':12} {'callers'}")
    for typ, meth in targets:
        status, ttok, res = type_resolve(typ, meth)
        if status != "OK":
            print(f"{typ+'.'+meth:40} {status}"); continue
        if not res:
            print(f"{typ+'.'+meth:40} no-store"); continue
        for ts, ms, wrapper, kind, val, callers in res:
            vstr = (f"0x{val:x}" if kind=="DIRECT" else f"slot 0x{val:x}") if val is not None else "None"
            print(f"{typ+'.'+meth:40} {kind:8} {vstr:12} {callers}")
