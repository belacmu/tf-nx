#!/usr/bin/env python3
"""Summarizes a console visit: hw-check.py <folder holding hw-log.txt (and trial-log.txt)>
(the SD card's xplay/ folder, copied off or mounted). Prints what each VIABILITY-TESTS hardware item
shows and whether it passes. Read-only."""
import re, statistics, sys
from pathlib import Path

d = Path(sys.argv[1] if len(sys.argv) > 1 else ".")
here = Path(__file__).resolve().parent.parent
log = (d / "hw-log.txt").read_text(errors="replace").splitlines() if (d / "hw-log.txt").exists() else []
if not log:
    sys.exit(f"no hw-log.txt in {d}")
log = [l.rstrip() for l in log if l.strip()]
def grep(p): return [l for l in log if re.search(p, l)]

EXPECT = {"sin": "07301656689ca354", "cos": "5ca7e6ef1f70778b", "atan2": "c85d1691abc73032", "pow": "d3d1af53b719d844", "musl-pow": "2ca2e37827dfbaea"}
print("== build:", *(grep(r"probe injected")[:1] or ["(no 'injected' line: the log starts mid-session or the probe didn't run)"]))

print("\n== .NET Random, gameplay RNG and DeterministicMath (boot self-tests)")
for l in grep(r"\[rnd\]|\[dm\]"): print("  ", l)

print("\n== 3. FPCR and math (expected from Ryujinx and the browser: FPCR=0; sin/cos/atan2 equal the browser's)")
for l in grep(r"FPCR"): print("  ", l)
for l in grep(r"mathtest"):
    got = dict(re.findall(r"(sin|cos|atan2|pow|musl-pow) ([0-9a-f]{16})", l))
    for k, v in EXPECT.items():
        print(f"   {k:9s} {got.get(k, '?')}  {'ok' if got.get(k) == v else 'DIFFERS (expected ' + v + ')'}")

print("\n== 3. scripted Trials run, 700 frames")
t = d / "trial-log.txt"
if t.exists():
    mine = [re.sub(r"^\[xplay\] T ", "", l).strip() for l in t.read_text().splitlines() if l.strip()]
    for name in ("result-switch-700.txt", "result-browser-700.txt"):
        ref = [l.strip() for l in (here / "test-scripts/results" / name).read_text().splitlines() if l.strip()]
        bad = [i for i, (a, b) in enumerate(zip(mine, ref)) if a != b]
        print(f"   vs {name}: {len(mine)} frames, " + ("IDENTICAL" if not bad and len(mine) == len(ref) else f"{len(bad)} differ, first at frame {bad[0] if bad else '(length)'}: {mine[bad[0]] if bad else ''} vs {ref[bad[0]] if bad else ''}"))
else:
    print("   no trial-log.txt (run A with the script, in Trials 'Sacred Ground I', for ~15 s)")
for l in grep(r"scripted run"): print("  ", l)

print("\n== network stages")
for l in log:
    if re.search(r"^(nifm|socket::|7a:|7b:|7c:|8:|network probe|net:)", l) or "[hw]" in l and re.search(r"(7a|7b|7c|8):", l): print("  ", l)

print("\n== 1. update cost (TFGame.Update, no drawing), in-level frames")
blocks = []
for l in grep(r"timing, in-level frames"):
    m = re.search(r"update ms p50 ([\d.]+) p90 ([\d.]+) p99 ([\d.]+) max ([\d.]+).*frame period ms p50 ([\d.]+) p90 ([\d.]+) p99 ([\d.]+) max ([\d.]+)", l)
    if m: blocks.append(tuple(float(x) for x in m.groups()))
if blocks:
    col = lambda i: [b[i] for b in blocks]
    print(f"   {len(blocks)} blocks of 600 frames. update ms: median of p50s {statistics.median(col(0)):.2f}, worst p90 {max(col(1)):.2f}, worst p99 {max(col(2)):.2f}, worst max {max(col(3)):.2f}")
    print(f"   frame period ms: median of p50s {statistics.median(col(4)):.2f} (16.7 = 60 fps), worst p99 {max(col(6)):.2f}, worst max {max(col(7)):.2f}")
else:
    print("   no timing blocks (needs 600 in-level frames: ten seconds in a level)")

print("\n== anything odd")
for l in grep(r"REFUSED|FAIL|NOT written|missing|exception|skipped"): print("  ", l)
