"""callmap.py: {Switch body offset: "Class::Method"} for every class in the PC IL (classmeta.records), cached in callmap.idx."""
import os, pickle
import classmeta as cm, parity
_here = os.path.dirname(os.path.abspath(__file__))
def body_names():
    cache = os.path.join(_here, "callmap.idx")
    if os.path.exists(cache) and os.path.getmtime(cache) > os.path.getmtime(os.path.join(_here, "classmeta.py")):
        with open(cache, "rb") as f: return pickle.load(f)
    il = parity.parse_il(); sites = cm.index(); out = {}
    for t in il:
        if "+" in t or "<" in t: continue
        for n, b, fl in cm.records(t, sites) or []:
            out.setdefault(b, f"{t}::{n}")
    with open(cache, "wb") as f: pickle.dump(out, f)
    return out
if __name__ == "__main__":
    m = body_names(); print(len(m), "bodies named")
