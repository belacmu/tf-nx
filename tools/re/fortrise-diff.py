#!/usr/bin/env python3
"""fortrise-diff.py: the game methods FortRise changes (constants or calls), vanilla TowerFall 1.3.3.1 against the FortRise-patched
game TF.EX players run (both IL listings from tools/re/pc-il.sh). Most changes are mod hooks that behave as vanilla with no mods
(orig_ calls); the ones that change a versus match's numbers are version gaps for the Switch (pcgameplay.cpp kGaps). 2026-10-28:
Spikeball.Update's 1f / 180f and Arrow.get_SeekMaxAngle, both FortRise's "same as v1.3.3.3" tweaks, i.e. the Switch's own values.

  fortrise-diff.py [class prefix ...]     (default: TowerFall. and Monocle.)"""
import collections, os, struct, sys
import parity
f32 = lambda x: struct.unpack("<f", struct.pack("<f", x))[0]
cache = os.path.dirname(os.path.realpath(parity.IL))   # (the main checkout's .cache: a worktree links only towerfall-pc.il)
prefixes = tuple(sys.argv[1:]) or ("TowerFall.", "Monocle.")
V = parity.parse_il(os.path.join(cache, "towerfall-pc-vanilla.il"))
F = parity.parse_il(os.path.join(cache, "towerfall-pc.il"))
n = 0
for cls in sorted(V):
    if not cls.startswith(prefixes): continue
    for m in sorted(V[cls]):
        if m not in F.get(cls, {}): continue
        a, b = V[cls][m], F[cls][m]
        fa, fb = collections.Counter(f32(x) for x in a["f"]), collections.Counter(f32(x) for x in b["f"])
        ia, ib = collections.Counter(a["i"]), collections.Counter(b["i"])
        ca, cb = collections.Counter(t for _, t in a["c"]), collections.Counter(t for _, t in b["c"])
        d = []
        if fa != fb: d.append(f"floats -{sorted((fa - fb).elements())} +{sorted((fb - fa).elements())}")
        if ia != ib: d.append(f"ints -{sorted((ia - ib).elements())[:6]} +{sorted((ib - ia).elements())[:6]}")
        if ca != cb: d.append(f"calls -{sorted((ca - cb).elements())[:4]} +{sorted((cb - ca).elements())[:4]}")
        if d: n += 1; print(f"{cls}::{m}: " + " | ".join(d))
print(f"fortrise-diff: {n} method(s) FortRise changes", file=sys.stderr)
