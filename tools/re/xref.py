#!/usr/bin/env python3
"""Find ADRP(+ADD) PC-relative references to a target address in NSO .text.
Usage: xref.py <flat> <target_hex> [text_end_hex]
Scans for `ADRP xN, page` followed (within a small window) by
`ADD xN, xN, #lo12` that resolves to target. Prints the ADRP site (a code
offset inside the referencing function) so it can be walked back to a prologue.
"""
import sys, struct
from capstone import Cs, CS_ARCH_ARM64, CS_MODE_LITTLE_ENDIAN

flat = sys.argv[1]
target = int(sys.argv[2], 16)
text_end = int(sys.argv[3], 16) if len(sys.argv) > 3 else 0x28626c0
img = open(flat, "rb").read()
text = img[:text_end]

md = Cs(CS_ARCH_ARM64, CS_MODE_LITTLE_ENDIAN)
md.detail = False

# Fast pre-pass: decode only ADRP instructions by their encoding, then check a
# short window of following ADD/LDR with matching dest reg. Full capstone over
# 42MB is slow, so hand-decode ADRP and use capstone only to confirm the ADD.
def adrp_target(insn, pc):
    # ADRP: bits: op=1, immlo[30:29], 10000, immhi[23:5], Rd[4:0]
    immlo = (insn >> 29) & 3
    immhi = (insn >> 5) & 0x7FFFF
    imm = (immhi << 2) | immlo
    if imm & (1 << 20):
        imm -= (1 << 21)
    return ((pc & ~0xFFF) + (imm << 12)) & 0xFFFFFFFFFFFF, insn & 0x1F

hits = []
n = len(text)
tgt_page = target & ~0xFFF
tgt_lo = target & 0xFFF
for off in range(0, n - 4, 4):
    insn = struct.unpack_from("<I", text, off)[0]
    if (insn & 0x9F000000) != 0x90000000:   # ADRP opcode mask
        continue
    page, rd = adrp_target(insn, off)
    if page != tgt_page:
        continue
    # look ahead up to 8 instrs for ADD Rd,Rd,#lo12 or LDR using Rd
    for j in range(1, 9):
        o2 = off + j * 4
        if o2 + 4 > n:
            break
        i2 = struct.unpack_from("<I", text, o2)[0]
        # ADD (immediate) 64-bit: sf=1 op=0 S=0 100010 sh imm12 Rn Rd
        if (i2 & 0xFF800000) == 0x91000000:
            rn = (i2 >> 5) & 0x1F
            rd2 = i2 & 0x1F
            imm12 = (i2 >> 10) & 0xFFF
            sh = (i2 >> 22) & 1
            if rn == rd and imm12 == tgt_lo and sh == 0:
                hits.append((off, o2, rd))
                break
        # LDR (immediate, unsigned) using base=rd → target in low12*scale
        if (i2 & 0x3B000000) == 0x39000000:
            rn = (i2 >> 5) & 0x1F
            if rn == rd:
                # can't easily confirm scaled imm==lo12 without size; record as candidate
                imm = (i2 >> 10) & 0xFFF
                size = (i2 >> 30) & 3
                if (imm << size) == tgt_lo:
                    hits.append((off, o2, rd))
                    break
print(f"target 0x{target:x}: {len(hits)} ADRP+ADD/LDR ref site(s)")
for adrp_off, add_off, rd in hits[:40]:
    print(f"  ref at .text+0x{adrp_off:x} (ADRP x{rd}) / add@0x{add_off:x}")
