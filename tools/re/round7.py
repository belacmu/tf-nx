#!/usr/bin/env python3
"""round7.py: float constants the Switch build rounded differently from the PC (a few ULPs off).

BRUTE wrote the game's float literals as 7-digit decimal text, so a constant that needs 8-9 digits is one ULP off on the Switch.
The PC side is FortRise's game (parity.py, tools/re/pc-il.sh): FortRise itself sets two such constants to the Switch's form
(Spikeball.Update's 1f / 180f = 0x3bb60b60, Arrow.get_SeekMaxAngle), so those are no gaps; against the vanilla exe they were, and
"fixing" them desynced spike ball towers (2026-10-28). parity.py compares constants to 4 decimals and cannot see this.

For every method on both sides: a PC ldc.r4 value p that the Switch body lacks, with a Switch float constant within ULPS units in
the last place of it (the same literal, rounded differently: the Switch's 0x3bb60b60 is "0.005555555" truncated, not %.7g). Each hit is a pcgameplay.cpp kGaps candidate (a rodata word read by one site, or an immediate).

  round7.py                 every TowerFall / Monocle class
  round7.py TowerFall.Spikeball [...]"""
import struct, sys, collections
import parity, classmeta as cm

f32 = lambda x: struct.unpack("<f", struct.pack("<f", x))[0]
ULPS = 4   # how close (in units in the last place) a Switch constant must be to count as the PC's, rounded
bits = lambda x: struct.unpack("<I", struct.pack("<f", x))[0]

def hits(typename, il, sites):
    sw, _, _ = parity.switch_methods(typename, sites)
    out = []
    pcg = parity.group(il.get(typename, {}))
    swg = {k: [b for bl in v for b in bl] for k, v in parity.group(sw).items()}
    for n in sorted(set(pcg) & set(swg)):
        pc = set(f32(f) for d in pcg[n] for f in d["f"] if f == f and 0 < abs(f) < 3.4e38)
        swf = set()
        for b in swg[n]:
            w = []; swf |= set(f32(f) for f in parity.consts(b, w)[0] if f == f and abs(f) < 3.4e38); swf |= set(f32(f) for f in w if f == f and abs(f) < 3.4e38)
        swb = {}
        for q in swf: swb.setdefault(bits(abs(q)), q)
        for p in sorted(pc):
            bp = bits(abs(p))
            if bp in swb or bp < 0x00800000: continue   # the same constant (or a denormal)
            near = [swb[bp + d] for d in range(-ULPS, ULPS + 1) if d and bp + d in swb]
            if near: out.append((n, p, near[0], [hex(b) for b in swg[n]]))
    return out

if __name__ == "__main__":
    il = parity.parse_il(); sites = cm.index()
    names = [a for a in sys.argv[1:] if not a.startswith("-")] or sorted(
        n for n in il if n.split(".")[0] in ("TowerFall", "Monocle") and "+" not in n and "<" not in n)
    total = 0
    for t in names:
        try: hs = hits(t, il, sites)
        except Exception as e: print(f"{t}: skipped ({e})"); continue
        for n, p, q, bodies in hs:
            total += 1
            print(f"{t}.{n}: PC {p!r} ({bits(p):08x})  Switch {q!r} ({bits(q):08x})  bodies {bodies}")
    print(f"round7: {total} constant(s) rounded differently on the Switch")
