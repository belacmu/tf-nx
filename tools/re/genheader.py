#!/usr/bin/env python3
"""genheader.py: Game.hpp-style accessors for a list of game members, from rtypes.py (fields, statics, type records) and sigs.py
(method bodies, overloads by PC signature).

  genheader.py Monocle.Tween Monocle.Alarm.OnComplete "Monocle.Draw.Texture(Monocle.Subtexture,...)" ...
     a type name alone: its runtime-type facts (size, flags, descriptor, getter, cctor runner) and every field
     Type.Member: that field (offset, or the static's storage) or every overload of that method (body, PC parameters)
  genheader.py --tfex [--class Notification]: every member TF.EX's network DLLs use (tfexrefs.py), grouped per type

Output: C++ namespaces like tfex/Game.hpp's (constexpr offsets relative to the main module)."""
import re, sys, collections
import rtypes, sigs, tfexrefs

def cname(s): return re.sub(r"[^A-Za-z0-9_]", "_", s)

def type_block(t):
    ty = rtypes.table().get(t)
    if ty is None: return [f"/* {t}: not on the Switch */"]
    out = [f"/* {t}: size {ty.size:#x}, flags {ty.flags:#x}, base {ty.parent} */"]
    out.append(f"constexpr uintptr_t Desc = {ty.desc:#x}, Getter = {(ty.getter or 0):#x}, Cctor = {(ty.cctor if isinstance(ty.cctor, int) else 0):#x};")
    for owner, f, off, fty in rtypes.fields(t, inherited=False, statics=True):
        if off is None: continue
        static = off > 0x100000
        out.append(f"constexpr {'uintptr_t' if static else 'uint32_t'} {cname(f)} = {off:#x};   /* {fty}{' (static storage)' if static else ''} */")
    return out

def member_lines(t, m):
    off = rtypes.field(t, m)
    if off is None:
        for owner, f, o, fty in rtypes.fields(t, statics=True):
            if f == m: off = o
    if off is None:
        for c in rtypes.chain(t):
            o = tfexrefs.accessor_offset(c.name, m)
            if o is not None: off = o; break
    if off is not None: return [f"constexpr {'uintptr_t' if off > 0x100000 else 'uint32_t'} {cname(m)} = {off:#x};"]
    name, want = (m.split("(")[0], m[m.index("(") + 1:-1]) if "(" in m else (m, None)
    lines = []
    for c in rtypes.chain(t):
        rows = [r for r in sigs.paired(c.name) if r[0] == name]
        if not rows: continue
        for n, k, body, params, static, mis in rows:
            if want is not None and tfexrefs.normsig(params) != want: continue
            suffix = "" if len(rows) == 1 else f"_{k}"
            lines.append(f"constexpr uintptr_t {cname(name)}{suffix} = {('%#x' % body) if body else '0 /* no body */'};   /* {'static ' if static else ''}({params}){' (' + c.name + ')' if c.name != t else ''} */")
        break
    return lines or [f"/* {t}.{m}: not resolved */"]

def emit(groups):
    for t in sorted(groups):
        print(f"namespace {cname(t.split('.')[-1])} {{   /* {t} */")
        for m in groups[t] or [None]:
            for l in (type_block(t) if m is None else member_lines(t, m)): print("    " + l)
        print("}")

if __name__ == "__main__":
    a = sys.argv[1:]
    groups = collections.defaultdict(list)
    if a and a[0] == "--tfex":
        only = a[a.index("--class") + 1] if "--class" in a else None
        refs, dyn = tfexrefs.scan(tfexrefs.IL_DIR, only)
        for kind, t, m in sorted(refs): groups[t].append(m)
    else:
        for x in a:
            head = x.split("(")[0]
            if head in rtypes.table(): groups[head]
            else:
                t, m = head.rsplit(".", 1)
                groups[t].append(m + x[len(head):])
    emit(groups)
