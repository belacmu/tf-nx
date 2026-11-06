#!/usr/bin/env python3
"""draw-sites.py <hw log> [frame_from] [frame_to]: the console's gameplay draws ("[vs] draw N frame F site +X", dumped at the first
DESYNC of a lobby match, or at vsstop; "[rnd] gdraw N frame F site +X" with hw.txt rnggametrace) with each call site named by
tools/re's callmap (the TowerFall method whose body contains it), to line up with the page's "[xplay2rng] draw … ggrs_frame=…" lines.
A site in code the callmap doesn't name (MonoGame, the runtime) shows the nearest named body before it, marked "?"."""
import bisect, os, pickle, re, sys

here = os.path.dirname(os.path.abspath(__file__))
idx = os.path.join(here, "re", "callmap.idx")
if not os.path.exists(idx):   # (a cache: only the main checkout has it; tools/re/callmap.py builds it)
    import subprocess
    main = subprocess.run(["git", "-C", here, "worktree", "list", "--porcelain"], capture_output=True, text=True).stdout.split("\n")[0][len("worktree "):]
    idx = os.path.join(main, "tools", "re", "callmap.idx")
names = pickle.load(open(idx, "rb"))
starts = sorted(names)
log, lo, hi = sys.argv[1], int(sys.argv[2]) if len(sys.argv) > 2 else -10**9, int(sys.argv[3]) if len(sys.argv) > 3 else 10**9
pat = re.compile(r"(?:\[vs\] draw|\[rnd\] gdraw) (\d+) frame (-?\d+) site \+(0x[0-9a-f]+)")
for line in open(log, "rb").read().replace(b"\0", b"").decode("utf-8", "replace").splitlines():
    m = pat.search(line)
    if not m: continue
    n, f, site = int(m.group(1)), int(m.group(2)), int(m.group(3), 16)
    if not lo <= f <= hi: continue
    i = bisect.bisect_right(starts, site) - 1
    name = f"{names[starts[i]]}+{site - starts[i]:#x}" if i >= 0 else "?"
    if i >= 0 and site - starts[i] > 0x4000: name += " ?"
    print(f"draw {n} frame {f} site +{site:#x} {name}")
