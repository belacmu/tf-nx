#!/usr/bin/env python3
"""abstract-slots.py [Type...]: every abstract method of the game's types (PC IL, sigs.py) with the vtable slot bind.py derives for it
(abstract_slot: all concrete subclasses agree, the owner's entry null), or why it stays unresolved. Then binding.json's abstract
entries checked against it: a mismatch, or an entry without "proof" (an old guess), is listed and the exit code is 1. Also the vtable
sizes (vtable_bytes, binding.json "vtbytes" = bind::vb_*): a class whose size can't be derived or is smaller than its base's, and a
binding.json size that differs, are listed (exit code 1).
Without arguments: every type (a few minutes, needs tools/re/main.flat)."""
import json, os, sys

sys.argv, args = sys.argv[:1], sys.argv[1:]      # bind.py reads its own arguments at import
import bind                                      # noqa: E402  (chdirs to tools/re, sets up the imports)
from bind import rtypes, sigs, abstract_slot, vtable_bytes     # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))


def main():
    types = args or sorted(rtypes.table())
    derived = {}
    for t in types:
        try:
            sigs.pc_methods(t)
        except Exception:
            continue
        for mem, params in sigs.ABSTRACT.get(t, []):
            r = abstract_slot(t, mem, params)
            derived[(t, mem, params)] = r
            print(f"{t}.{mem}({params})\t" + (f"{r['slot']:#x}\t{r['proof']}" if "slot" in r else "-\t" + r["error"]))
    ok = sum("slot" in r for r in derived.values())
    print(f"\n{len(derived)} abstract methods: {ok} slots proven, {len(derived) - ok} unresolved")

    bad = 0
    binding = json.load(open(os.path.join(HERE, "binding.json")))
    for k, v in sorted(binding.items()):
        if not (k.startswith("v_") and v.get("abstract")):
            continue
        hits = [r for (t, mem, p), r in derived.items() if t == v.get("owner") and (k.endswith("_" + mem) or k[:-5].endswith("_" + mem))]
        if "proof" not in v:
            print(f"binding.json {k}: {v['slot']:#x} is an old guess (no proof): run bind.py to repair it"); bad += 1
        elif len(hits) == 1 and hits[0].get("slot") != v.get("slot"):
            print(f"binding.json {k}: {v['slot']:#x}, derived {hits[0].get('slot', hits[0].get('error'))}"); bad += 1
    print(f"binding.json: {bad} abstract entries to fix" if bad else "binding.json: every abstract entry matches")

    tab = rtypes.table()
    vbad = 0
    for t in types:
        vb = vtable_bytes(t) if t in tab else None
        if isinstance(vb, str):
            print(f"vtable {t}: {vb}"); vbad += 1
    bydesc = {ty.desc: name for name, ty in tab.items() if ty.desc}
    seen = set()
    for k, v in sorted(binding.items()):
        if "vtbytes" not in v or v.get("desc") not in bydesc or bydesc[v["desc"]] in seen:
            continue
        t = bydesc[v["desc"]]; seen.add(t)
        vb = vtable_bytes(t)
        if vb is None:
            continue   # not a class vtable (value types): not derived, not checked
        base = tab[t].parent
        bvb = (vtable_bytes(base) if base in tab else 0) or 0
        mark = "" if vb == v["vtbytes"] else f"   <- binding.json says {v['vtbytes']:#x}: run bind.py to repair it"
        print(f"vb {t}: {vb:#x} (base {base}: {bvb:#x}){mark}" if isinstance(vb, int) else f"vb {t}: {vb}")
        vbad += bool(mark) or not isinstance(vb, int)
    print(f"vtable sizes: {vbad} to fix" if vbad else "vtable sizes: every class derived, binding.json matches")
    return 1 if bad or vbad else 0


if __name__ == "__main__":
    sys.exit(main())
