#!/usr/bin/env python3
"""vtable.py Type [Type...]: a class's vtable slot by slot, each entry named by the class (or base) whose method body it is
(classmeta.records over the base chain, overloads with their PC signatures from sigs.py). The slot offsets are what a subclass
made at run time overrides (tfex/Game.hpp brute::Override). Slots with no named body: abstract methods (a throwing stub) or
interface thunks."""
import sys
import rtypes, relocs, classmeta as cm, sigs

def names_by_body(t):
    out = {}
    for c in rtypes.chain(t):
        try: recs = cm.records(c.name) or []
        except Exception: recs = []
        pc = {}
        try:
            for n, k, b, params, st, mis in sigs.paired(c.name):
                if b: pc[b] = f"{n}({params})"
        except Exception: pass
        for n, b, fl in recs:
            out.setdefault(b, f"{c.name.split('.')[-1]}.{pc.get(b, n)}")
    return out

def slots(t):
    R = relocs.relative(); ty = rtypes.table()[t]
    vt = ty.vtable; desc = ty.desc
    names = names_by_body(t)
    # abstract methods (no body: a null slot), in IL order, for the null slots in order
    abstract = []
    for c in reversed(rtypes.chain(t)):
        sigs.pc_methods(c.name)
        abstract += [f"{c.name.split('.')[-1]}.{n}({p}) abstract" for n, p in sigs.ABSTRACT.get(c.name, [])]
    out = []; off = 0
    while vt + off != desc and off < 0x800:
        tgt = R.get(vt + off)
        if tgt is None:   # not a pointer: the interface table ({hash | first slot << 32, 0}) desc[2] points at, or padding
            raw = int.from_bytes(rtypes._img()[vt + off:vt + off + 8], "little")
            out.append((off, 0, abstract.pop(0) + " (guess: the class's abstract methods in IL order)" if raw == 0 and abstract else "(raw %#x)" % raw)); off += 8; continue
        out.append((off, tgt, names.get(tgt, "?")))
        off += 8
    return out

if __name__ == "__main__":
    for t in sys.argv[1:]:
        print(f"== {t}")
        for off, tgt, n in slots(t): print(f"  {off:#06x}  +{tgt:#x}  {n}")
