#!/usr/bin/env python3
"""Runtime-metadata symbol resolver (static proof-of-concept).
Recipe discovered from the BRUTE metadata:
  method NAME (UTF-16LE, standalone) -> class-metadata builder site that stores it
  -> the NEXT adrp+add pair = the method's invoke WRAPPER pointer
  -> the wrapper's first BL = the concrete method BODY (what gameplay calls).
"""
import struct, sys
from capstone import Cs, CS_ARCH_ARM64, CS_MODE_LITTLE_ENDIAN

img = open(sys.argv[1] if len(sys.argv) > 1 else "main.flat", "rb").read()
TEXT_END = 0x28626c0
md = Cs(CS_ARCH_ARM64, CS_MODE_LITTLE_ENDIAN)

def find_tokens(name):
    u = name.encode("utf-16-le"); off = 0; out = []
    while True:
        i = img.find(u, off)
        if i < 0: break
        before = img[i-2:i]; after = img[i+len(u):i+len(u)+2]
        if after == b"\x00\x00" and before == b"\x00\x00":   # standalone token
            out.append(i)
        off = i + 1
    return out

def adrp_val(insn, pc):
    immlo=(insn>>29)&3; immhi=(insn>>5)&0x7FFFF; imm=(immhi<<2)|immlo
    if imm&(1<<20): imm-=(1<<21)
    return ((pc&~0xFFF)+(imm<<12))&0xFFFFFFFFFFFF, insn&0x1F

def xref_sites(target):
    tp, tl = target & ~0xFFF, target & 0xFFF; hits=[]
    for off in range(0, TEXT_END-4, 4):
        insn = struct.unpack_from("<I", img, off)[0]
        if (insn & 0x9F000000)!=0x90000000: continue
        page, rd = adrp_val(insn, off)
        if page!=tp: continue
        i2 = struct.unpack_from("<I", img, off+4)[0]
        if (i2 & 0xFF800000)==0x91000000 and ((i2>>5)&0x1F)==rd and (i2&0x1F)==rd and ((i2>>10)&0xFFF)==tl:
            hits.append(off)
    return hits

def next_adrp_add(after_off, limit=6):
    """the next adrp+add pair at/after after_off -> resolved address"""
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

def first_bl_target(func):
    for insn in md.disasm(img[func:func+0x28], func):
        if insn.mnemonic == "bl":
            return int(insn.op_str[1:], 16)
    return None

def bl_callers(target):
    n=0
    for off in range(0, TEXT_END-4, 4):
        insn = struct.unpack_from("<I", img, off)[0]
        if (insn & 0xFC000000)==0x94000000:
            imm = insn & 0x03FFFFFF
            if imm & (1<<25): imm -= (1<<26)
            if off + imm*4 == target: n+=1
    return n

def resolve(name):
    toks = find_tokens(name)
    results = []
    for t in toks:
        sites = xref_sites(t)
        for s in sites:
            wrapper = next_adrp_add(s+8)          # skip the name's own adrp+add
            if wrapper is None or wrapper>=TEXT_END: continue
            body = first_bl_target(wrapper)
            results.append((wrapper, body))
    return results

if __name__ == "__main__":
    for name in sys.argv[2:] or ["ShootArrow","CollectArrows","DuckingUpdate","OnCollideV","GetLevelSystem"]:
        r = resolve(name)
        print(f"{name}:")
        for wrapper, body in r:
            fi = next(md.disasm(img[body:body+4], body)) if body and body<TEXT_END else None
            callers = bl_callers(body) if body else 0
            print(f"  wrapper 0x{wrapper:x} -> body 0x{body:x}  "
                  f"first='{fi.mnemonic} {fi.op_str}'  direct_callers={callers}" if body else f"  wrapper 0x{wrapper:x} -> (no bl)")
