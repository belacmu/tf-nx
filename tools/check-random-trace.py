#!/usr/bin/env python3
"""Replays the Switch probe's "[rnd] trace" lines (the first calls on each System.Random the game made)
against .NET's seeded algorithm and reports any call whose result differs.
  tools/check-random-trace.py <Ryujinx log>      (read with grep -a; the log counts as binary)
Also self-checks the Python port against hw-probe/tests/dnrandom-ref.txt (from the real .NET runtime)."""
import re, struct, sys
from collections import defaultdict
from pathlib import Path

M = 0x7FFFFFFF
def i32(x): return ((x + 0x80000000) & 0xFFFFFFFF) - 0x80000000

class Knuth:
    def __init__(self, seed):
        sub = M if seed == -0x80000000 else abs(seed)
        mj = i32(161803398 - sub)
        a = [0] * 56
        a[55] = mj
        mk, ii = 1, 0
        for _ in range(1, 55):
            ii += 21
            if ii >= 55: ii -= 55
            a[ii] = mk
            mk = i32(mj - mk)
            if mk < 0: mk = i32(mk + M)
            mj = a[ii]
        for _ in range(1, 5):
            for i in range(1, 56):
                n = i + 30
                if n >= 55: n -= 55
                a[i] = i32(a[i] - a[1 + n])
                if a[i] < 0: a[i] = i32(a[i] + M)
        self.a, self.i, self.j = a, 0, 21
    def internal(self):
        self.i = self.i + 1 if self.i + 1 < 56 else 1
        self.j = self.j + 1 if self.j + 1 < 56 else 1
        r = i32(self.a[self.i] - self.a[self.j])
        if r == M: r -= 1
        if r < 0: r = i32(r + M)
        self.a[self.i] = r
        return r
    def sample(self): return self.internal() * (1.0 / M)
    def large(self):
        r = self.internal()
        if self.internal() % 2 == 0: r = -r
        return (r + (M - 1)) / (2 * M - 1)
    def next(self): return self.internal()
    def next_max(self, mx): return int(self.sample() * mx)
    def next_minmax(self, lo, hi):
        rng = hi - lo
        if rng <= M: return int(self.sample() * rng) + lo
        return int(self.large() * rng) + lo
    def next_bytes(self, n):
        return [self.internal() & 0xFF for _ in range(n)]

# --- self-check against the real runtime's values
ref = Path(__file__).resolve().parent.parent / "hw-probe/tests/dnrandom-ref.txt"
seed = None; bad = 0; checked = 0
for line in ref.read_text().splitlines():
    if line.startswith("seed "):
        seed = int(line.split()[1]); k = Knuth(seed); continue
    name, _, rest = line.partition(":"); vals = rest.split()
    if name == "next": got = [k.next() for _ in vals]; want = [int(v) for v in vals]
    elif name == "max100": got = [k.next_max(100) for _ in vals]; want = [int(v) for v in vals]
    elif name == "range5_10": got = [k.next_minmax(5, 10) for _ in vals]; want = [int(v) for v in vals]
    elif name == "range-50_50": got = [k.next_minmax(-50, 50) for _ in vals]; want = [int(v) for v in vals]
    elif name == "large": got = [k.next_minmax(-0x80000000, M) for _ in vals]; want = [int(v) for v in vals]
    elif name == "max1": got = [k.next_max(1) for _ in vals]; want = [int(v) for v in vals]
    elif name == "max0": got = [k.next_max(0) for _ in vals]; want = [int(v) for v in vals]
    elif name == "eq3_3": got = [k.next_minmax(3, 3)]; want = [int(vals[0])]
    elif name == "double": got = [struct.unpack("<Q", struct.pack("<d", k.sample()))[0] for _ in vals]; want = [int(v, 16) for v in vals]
    elif name == "bytes": got = k.next_bytes(len(vals)); want = [int(v) for v in vals]
    elif name == "after2000":
        for _ in range(1999): k.next()
        got = [k.next()]; want = [int(vals[0])]
    else: continue
    checked += 1
    if got != want: bad += 1
print(f"python port vs real .NET reference: {checked - bad}/{checked} groups match")
if bad: sys.exit("the Python port is wrong; fix it before trusting trace results")
if len(sys.argv) < 2: sys.exit(0)

# --- replay the trace
text = Path(sys.argv[1]).read_bytes().decode("utf-8", "replace")
inst = defaultdict(list)
for m in re.finditer(r"\[rnd\] trace #(\d+) seed (-?\d+) (\w+) (-?\d+) (-?\d+) -> (-?\d+)", text):
    serial, seed, kind, a, b, res = int(m[1]), int(m[2]), m[3], int(m[4]), int(m[5]), int(m[6])
    inst[(serial, seed)].append((kind, a, b, res))
ok = fail = 0
for (serial, seed), calls in sorted(inst.items()):
    k = Knuth(seed)
    for kind, a, b, res in calls:
        if kind == "Next": want = k.next()
        elif kind == "NextMax": want = k.next_max(a)
        elif kind == "NextMinMax": want = k.next_minmax(a, b)
        elif kind == "Sample": want = struct.unpack("<Q", struct.pack("<d", k.sample()))[0]
        elif kind == "NextBytes": want = k.next_bytes(a)[0]
        else: continue
        if kind == "Sample": want = want - (1 << 64) if want >= 1 << 63 else want
        if want == res: ok += 1
        else:
            fail += 1
            if fail <= 10: print(f"MISMATCH #{serial} seed {seed} {kind}({a},{b}): switch {res}, .NET {want}")
print(f"{len(inst)} Random instances, {ok + fail} traced calls: {ok} match, {fail} differ")
by = defaultdict(int)
for calls in inst.values():
    for c in calls: by[c[0]] += 1
print("calls by kind:", dict(by))
