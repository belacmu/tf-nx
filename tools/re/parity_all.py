"""parity_all.py: parity.py over every TowerFall/Monocle class of the PC IL; writes the report to stdout."""
import sys, io, contextlib, parity, classmeta
il = parity.parse_il()
sites = classmeta.index()
names = sorted(n for n in il if n.split(".")[0] in ("TowerFall", "Monocle") and "+" not in n and "<" not in n)
print(f"{len(names)} classes", flush=True)
for n in names:
    buf = io.StringIO()
    try:
        with contextlib.redirect_stdout(buf): parity.report(n, il, sites)
    except Exception as e:
        buf.write(f"== {n}: ERROR {e}\n")
    out = buf.getvalue()
    if "DIFFERS" in out or "not found" in out or "ERROR" in out or "Switch-only [0" in out or "PC-only [0" in out or ("class level: PC-only []  Switch-only []" not in out):
        print(out, end="", flush=True)
