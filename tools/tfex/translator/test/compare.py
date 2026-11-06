"""compare.py <set dir>: <set>/gen/* must equal <set>/oracle/* byte for byte (TF.EX's own JSON); every field the hand-written
models write (<set>/hand/*: the message's lobby) must equal TF.EX's value for it."""
import json, os, sys

d = sys.argv[1]
bad = 0
def first_diff(a, b):
    i = next((i for i in range(min(len(a), len(b))) if a[i] != b[i]), min(len(a), len(b)))
    return i, a[max(0, i - 60):i + 60], b[max(0, i - 60):i + 60]

oracle = sorted(os.listdir(os.path.join(d, "oracle")))
same = 0
for f in oracle:
    o = open(os.path.join(d, "oracle", f), "rb").read()
    p = os.path.join(d, "gen", f)
    if not os.path.exists(p):
        print(f"  {f}: no generated output"); bad += 1; continue
    g = open(p, "rb").read()
    if g == o:
        same += 1; continue
    bad += 1
    i, oa, ga = first_diff(o, g)
    print(f"  {f}: differs at byte {i}\n    TF.EX: {oa!r}\n    gen:   {ga!r}")

def walk(h, t, path, out):
    if isinstance(h, dict):
        if not isinstance(t, dict): out.append((path, h, t)); return
        for k, v in h.items():
            if k not in t: out.append((path + "." + k, v, "<missing>")); continue
            walk(v, t[k], path + "." + k, out)
    elif isinstance(h, list):
        if not isinstance(t, list) or len(t) != len(h): out.append((path, h, t)); return
        for i, (a, b) in enumerate(zip(h, t)): walk(a, b, f"{path}[{i}]", out)
    elif h != t:
        out.append((path, h, t))

hand_ok = hand_n = 0
for f in sorted(os.listdir(os.path.join(d, "hand"))):
    if not os.path.exists(os.path.join(d, "oracle", f)): continue
    hand_n += 1
    h = json.load(open(os.path.join(d, "hand", f), encoding="utf-8"))
    o = json.load(open(os.path.join(d, "oracle", f), encoding="utf-8"))
    lob = next(iter(o.values())).get("lobby")
    diffs = []
    walk(h, lob, "lobby", diffs)
    if diffs:
        print(f"  {f}: hand-written models differ from TF.EX on {len(diffs)} fields: " +
              "; ".join(f"{p}: hand {a!r} TF.EX {b!r}" for p, a, b in diffs[:4]))
    else:
        hand_ok += 1
print(f"{os.path.basename(d)}: generated = TF.EX on {same}/{len(oracle)} messages; hand-written models = TF.EX on {hand_ok}/{hand_n} lobbies")
sys.exit(1 if bad else 0)
