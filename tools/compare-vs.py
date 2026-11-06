#!/usr/bin/env python3
"""compare-vs.py <browser-log> <switch-log> : test 5, per-tick comparison. The switch log is sdmc:/xplay/vs-log.txt
(or the Ryujinx log lines).

Browser lines:  [xplay2] T <n> rng=<16 hex> p0 x y sx sy alive=<0|1> p1 ...   ([xplay2pre] for n < 0)
Switch lines:   [vs] <tick> rng=<16 hex> p0 x y sx sy alive=<0|1> p1 ...   (tick 1-based; tick 0 of the
                browser = the tick printed on "[vs] origin: tick <N>")
Prints the first tick where each column (rng, p0, p1) differs, and the first 5 differing ticks overall."""
import re, sys
bro, sw = {}, {}
for l in open(sys.argv[1], errors="replace"):
    m = re.search(r"\[xplay2(?:pre)?\] T (-?\d+) rng=(\w+) (p0 .*)", l)
    if m: bro.setdefault(int(m.group(1)), (m.group(2), m.group(3)))
origin = None
for l in open(sys.argv[2], errors="replace"):
    m = re.search(r"\[vs\] origin: tick (\d+)", l)
    if m: origin = int(m.group(1))
    m = re.search(r"\[vs\] (\d+) rng=(\w+) (p0 .*)", l)
    if m: sw[int(m.group(1))] = (m.group(2), m.group(3).split(" |")[0])   # the last line of a frame (a nested Level.Update) counts
if origin is None:
    sys.exit("no origin line in the switch log")
sw = {t - origin: v for t, v in sw.items()}
common = sorted(set(bro) & set(sw))
common = [t for t in common if t != min(sw)]   # the first tick: the Switch's players aren't registered yet (alive=0)
print(f"switch origin tick {origin}; browser ticks {min(bro)}..{max(bro)}; switch ticks {min(sw)}..{max(sw)}; compared {len(common)}")
cols = {"rng": lambda v: v[0], "p0": lambda v: v[1].split(" p1 ")[0], "p1": lambda v: "p1 " + v[1].split(" p1 ")[1]}
first = {}
bad = []
for t in common:
    d = [k for k, f in cols.items() if f(bro[t]) != f(sw[t])]
    for k in d: first.setdefault(k, t)
    if d: bad.append((t, d))
print("first divergence per column:", first or "none")
print(f"{len(bad)} differing ticks of {len(common)}")
for t, d in bad[:5]:
    print(f"T {t} differs in {d}\n  browser {bro[t][0]} {bro[t][1]}\n  switch  {sw[t][0]} {sw[t][1]}")
