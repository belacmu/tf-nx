#!/usr/bin/env python3
"""Extract field offsets from BRUTE metadata field records.
Field record layout (from disasm): name ptr stored at [obj+0x80], the field's
byte offset stored at [obj+0x90] via `mov w8,#imm; str x8,[obj,#0x90]`.
Class disambiguation: a class's field builder emits records for many of that
class's fields together, so we find the builder REGION where a cluster of known
class fields co-occur, and read every field offset in that region.
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

def adrpv(insn, pc):
    immlo=(insn>>29)&3; immhi=(insn>>5)&0x7FFFF; imm=(immhi<<2)|immlo
    if imm&(1<<20): imm-=(1<<21)
    return ((pc&~0xFFF)+(imm<<12))&0xFFFFFFFFFFFF, insn&0x1F

def name_store_sites(tok):
    tp, tl = tok & ~0xFFF, tok & 0xFFF; hits=[]
    for off in range(0, TEXT_END-4, 4):
        insn = struct.unpack_from("<I", img, off)[0]
        if (insn & 0x9F000000)!=0x90000000: continue
        page, rd = adrpv(insn, off)
        if page!=tp: continue
        i2 = struct.unpack_from("<I", img, off+4)[0]
        if (i2 & 0xFF800000)==0x91000000 and ((i2>>5)&0x1F)==rd and (i2&0x1F)==rd and ((i2>>10)&0xFFF)==tl:
            hits.append(off)
    return hits

def offset_after(site, window=0x40):
    """find `mov w,#imm` feeding `str x,[x0,#0x90]` shortly after the name store."""
    ins = list(md.disasm(img[site:site+window], site))
    movs = {}   # reg -> imm from recent movz
    for insn in ins:
        m = insn.mnemonic; ops = insn.op_str
        if m == "mov" and ops.startswith("w") and "#" in ops:
            try:
                r = ops.split(",")[0].strip()
                v = int(ops.split("#")[1], 0)
                movs[r.replace("w","x")] = v; movs[r] = v
            except: pass
        if m == "str" and "[x0, #0x90]" in ops:
            reg = ops.split(",")[0].strip()
            return movs.get(reg, movs.get(reg.replace("x","w")))
    return None

PLAYER_FIELDS = ["PlayerIndex","Facing","Speed","State","Aiming","Allegiance",
                 "Position","Arrows","CharacterIndex","Scale","Flapping"]

if __name__ == "__main__":
    fields = sys.argv[1:] or PLAYER_FIELDS
    # gather all (field, site, offset) then cluster by region
    recs = []
    for f in fields:
        tok = token(f)
        if tok is None:
            print(f"{f}: no token"); continue
        for s in name_store_sites(tok):
            o = offset_after(s)
            if o is not None:
                recs.append((f, s, o))
    recs.sort(key=lambda r: r[1])
    # cluster: group sites within 0x3000 of each other, score by #distinct fields
    clusters = []
    for f, s, o in recs:
        if clusters and s - clusters[-1][-1][1] < 0x3000:
            clusters[-1].append((f, s, o))
        else:
            clusters.append([(f, s, o)])
    clusters.sort(key=lambda c: len(set(x[0] for x in c)), reverse=True)
    print("=== top field-record clusters (likely one class each) ===")
    for c in clusters[:4]:
        distinct = sorted(set(x[0] for x in c))
        print(f"\ncluster @0x{c[0][1]:x}..0x{c[-1][1]:x}  ({len(distinct)} distinct fields)")
        seen=set()
        for f, s, o in c:
            if f in seen: continue
            seen.add(f)
            print(f"    {f:16} = 0x{o:x} ({o})   @0x{s:x}")
