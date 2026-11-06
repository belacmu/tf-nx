#!/usr/bin/env python3
"""sigs.py: a class's methods with their PC signatures (towerfall-pc.il) paired with the Switch bodies (classmeta.records).

Overloads are paired by declaration order: BRUTE emits a class's method records in the order of the IL (checked on Entity.Add,
Draw.Texture/TextCentered, the ArchivesButton getters). A name with a different overload count on the two sides is flagged.

  sigs.py Monocle.Draw [Texture TextCentered]   -> name  #k  body  (PC parameters)"""
import os, re, sys
import classmeta as cm

PC_IL = os.path.join(os.path.dirname(os.path.abspath(__file__)), "../../.cache/towerfall-pc.il")
if not os.path.exists(PC_IL):
    PC_IL = os.path.expanduser("~/Code/Towerfall Switch Mods/switch-online/.cache/towerfall-pc.il")

_classes = None
ABSTRACT = {}   # type -> [(name, params)] of its abstract methods, in IL order (filled by pc_methods)
def pc_methods(typename):
    """[(name, params, static)] of the class in IL order."""
    global _classes
    if _classes is None:
        _classes = {}
        stack, cur, buf, inm = [], None, "", False
        for line in open(PC_IL, encoding="utf-8", errors="replace"):
            s = line.strip()
            if s.startswith(".class "):
                n = s.split()[-1].strip("'")
                stack.append(stack[-1] + "+" + n if " nested " in s and stack else n)
                cur = stack[-1]; _classes.setdefault(cur, [])
                continue
            if s.startswith("} // end of class"):
                if stack: stack.pop()
                cur = stack[-1] if stack else None
                continue
            if cur is None: continue
            if s.startswith(".method"): inm, buf = True, s
            elif inm: buf += " " + s
            if inm and ")" in buf and ("cil managed" in buf or "runtime managed" in buf or buf.rstrip().endswith(")")):
                if "(" in buf:
                    head, rest = buf.split("(", 1)
                    name = head.split()[-1]
                    params = rest.rsplit(")", 1)[0]
                    params = re.sub(r"\s+", " ", params)
                    params = ", ".join(p.strip().split(" ")[-2] if len(p.strip().split(" ")) >= 2 else p.strip() for p in params.split(",") if p.strip())
                    _classes[cur].append((name, params, " static " in buf))
                    if " abstract " in buf: ABSTRACT.setdefault(cur, []).append((name, params))
                inm = False
    return _classes.get(typename, [])

def paired(typename):
    sw = cm.records(typename) or []
    pc = pc_methods(typename)
    out = []
    for name in dict.fromkeys(n for n, _, _ in pc):
        a = [p for p in pc if p[0] == name]
        b = [r for r in sw if r[0] == name]
        for k, p in enumerate(a):
            out.append((name, k, b[k][1] if k < len(b) else None, p[1], p[2], len(a) != len(b)))
    return out

if __name__ == "__main__":
    t, names = sys.argv[1], set(sys.argv[2:])
    for name, k, body, params, static, mismatch in paired(t):
        if names and name not in names: continue
        print(f"  {name:24} #{k}  {('+%#x' % body) if body else '-':>10}  {'static ' if static else ''}({params}){'   OVERLOAD COUNT DIFFERS' if mismatch else ''}")
