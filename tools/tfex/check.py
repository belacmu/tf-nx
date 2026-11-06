"""Checks TF.EX's Harmony patch targets (targets.json from targets.py) against the Switch 1.4.1.6 metadata (tools/re/classmeta.py).
Usage: check.py <dir with targets.json> (TFEX_ALL=1: found pairs listed too, as "found"). PC source tree: $XPLAY_PC_SRC."""
import sys, json, os
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.getcwd())   # run from the RE folder that holds main.flat
import classmeta
S = sys.argv[1]
pc = os.environ.get("XPLAY_PC_SRC", os.path.expanduser("~/Code/Towerfall Switch Mods/switch-online/.cache/pc-src/full"))
ns = {}
for d in ("TowerFall", "Monocle", "TowerFall.Editor"):
    for f in os.listdir(os.path.join(pc, d)):
        if f.endswith('.cs'): ns.setdefault(f[:-3], d)
t = json.load(open(os.path.join(S, 'targets.json')))
pairs = sorted(set((a, b) for a, b, _ in t))
cache = {}; found = miss = notype = nonGame = 0; misses = []
for ty, m in pairs:
    if ty not in ns: nonGame += 1; misses.append(("nongame", ty, m)); continue
    full = ns[ty] + "." + ty
    if full not in cache:
        try: cache[full] = set(r[0] for r in classmeta.records(full))
        except Exception as e: cache[full] = None
    s = cache[full]
    if not s: notype += 1; misses.append(("notype", full, m)); continue
    if m in s:
        found += 1
        if os.environ.get('TFEX_ALL'): misses.append(("found", full, m))
    else: miss += 1; misses.append(("nomethod", full, m))
print("pairs", len(pairs), "found", found, "method-missing", miss, "type-missing", notype, "non-game-type", nonGame)
for x in misses: print(*x)
