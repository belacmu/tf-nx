#!/usr/bin/env python3
"""compare-arrows.py <browser-log-with-R-lines> <switch-log>: first ticks where the arrow lists differ.
Browser: [xplay2] R <T> arrows=<n> q0=.. q1=.. list=<x>:<y>:t<type>:s<state>;...   Switch: [vs] R <T> arrows=<n> list=<x>:<y>;..."""
import re, sys
def parse(path, pat, strip):
    d = {}
    for l in open(path, errors="replace"):
        m = re.search(pat, l)
        if m:
            items = [tuple(p.split(":")[:2]) for p in m.group(3).strip().split(";") if p]
            d.setdefault(int(m.group(1)), (int(m.group(2)), sorted(items), l.strip()))
    return d
b = parse(sys.argv[1], r"\[xplay2\] R (-?\d+) arrows=(\d+) q0=\S+ q1=\S+ list=(.*)", 0)
s = parse(sys.argv[2], r"\[vs\] R (-?\d+) (?:tags=\S+ )?arrows=(\d+) list=(.*)", 0)
origin = None
for l in open(sys.argv[2], errors='replace'):
    m = re.search(r'\[vs\] origin: tick (\d+)', l)
    if m: origin = int(m.group(1)); break
if origin is None: sys.exit('no origin line in the switch log')
s = {t - origin: v for t, v in s.items()}   # the Switch labels are Level.Update ticks; T = tick - origin
common = sorted(set(b) & set(s))
bad = [t for t in common if b[t][:2] != s[t][:2]]
print(f"compared {len(common)} ticks, {len(bad)} differ; first: {bad[:8]}")
for t in bad[:4]:
    print(f"T {t}\n  browser {b[t][2][:230]}\n  switch  {s[t][2][:230]}")
