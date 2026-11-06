#!/usr/bin/env python3
"""cmp.py LOG_A LOG_B [more logs...] : compares the [xplay2] T / R / U / A lines (A without resim=) of logs, tick by tick, on the ticks all have.
Lines may carry a 'page log: ' prefix. Prints, per log pair against the first, the number of compared ticks and the first differences."""
import re, sys
def load(path):
    T, R, U, A = {}, {}, {}, {}
    for l in open(path, errors="replace"):
        l = l.rstrip("\n")
        l = re.sub(r"^page log: ", "", l)
        m = re.match(r"\[xplay2(?:pre)?\] T (-?\d+) (.*)", l)
        if m: T[int(m[1])] = m[2]; continue
        m = re.match(r"\[xplay2\] R (-?\d+) (.*)", l)
        if m: R[int(m[1])] = m[2]; continue
        m = re.match(r"\[xplay2\] U (\d+) (.*)", l)
        if m: U[(int(m[1]), m[2])] = 1; continue
        m = re.match(r"\[xplay2\] A (\d+) (.*?) resim=\d+", l)
        if m: A[int(m[1])] = m[2]; continue
    return T, R, U, A
logs = [(p, load(p)) for p in sys.argv[1:]]
base_name, base = logs[0]
for name, cur in logs[1:]:
    print(f"== {name} vs {base_name}")
    for idx, kind in enumerate("TRUA"):
        a, b = base[idx], cur[idx]
        if kind == "U":
            lim = min(max((t for t, _ in a), default=-1), max((t for t, _ in b), default=-1))
            sa = {k for k in a if k[0] <= lim}; sb = {k for k in b if k[0] <= lim}
            print(f"U lines to tick {lim}: {len(sa)} vs {len(sb)}; only in base: {sorted(sa - sb)[:3]}; only in other: {sorted(sb - sa)[:3]}")
            continue
        ta, tb = max(a, default=-1), max(b, default=-1)
        lim = min(ta, tb)
        common = [k for k in sorted(set(a) & set(b)) if k <= lim]
        diff = [k for k in common if a[k] != b[k]]
        print(f"{kind}: base ticks {min(a, default=0)}..{ta}, other {min(b, default=0)}..{tb}; compared {len(common)}; identical {len(common) - len(diff)}; differing {len(diff)}")
        for k in diff[:3]:
            print(f"   first diffs at {k}:\n     base  {a[k][:230]}\n     other {b[k][:230]}")
        missing = [k for k in range(min(a, default=0), lim + 1) if (k in a) != (k in b)]
        if missing: print(f"   ticks present in only one log (<= {lim}): {len(missing)} e.g. {missing[:5]}")
