#!/usr/bin/env python3
"""tfexrefs.py: which TowerFall/Monocle members TF.EX's compiled network assemblies use, and how many resolve on the Switch by name.

Input: the IL of TF.EX's own DLLs (ilspycmd -il of the release zip's TF.EX.{API,Common,Core,Domain,Patchs}.dll: the network part;
TF.State is replaced by the raw-memory rollback). Every ldfld/stfld/ldsfld/stsfld/ldflda/call/callvirt/newobj/ldftn whose member
is declared in [TowerFall] is one reference; DynamicData.For(x).Get/Set/Invoke("name") strings are counted apart (private members
by name; their declaring type is not in the IL).

Resolution on the Switch (1.4.1.6 main):
  field    rtypes.field(type, name) (the reflection field records, base classes included); for a static field the record gives
           its storage address. Otherwise, a property-style accessor of that name with a decodable body (`ldr [x0,#off]; ret`).
  method   classmeta.records(type) has a member of that name (overloads: resolved by name only, the count is reported).
  type     rtypes.table() has the type at all.

  tfexrefs.py [il dir] [-v] [--class Notification]   -> coverage numbers (+ every unresolved reference with -v)"""
import os, re, sys, collections
import rtypes, classmeta as cm
from capstone import Cs, CS_ARCH_ARM64, CS_MODE_LITTLE_ENDIAN

IL_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "../../.cache/tfex-0.19.2/il")
if not os.path.isdir(IL_DIR):   # a worktree: the main checkout's cache (ilspycmd -il of .cache/tfex-0.19.2/zip/DShad.TF.EX/TF.EX.*.dll)
    IL_DIR = os.path.expanduser("~/Code/Towerfall Switch Mods/switch-online/.cache/tfex-0.19.2/il")
NETWORK = ("TF.EX.API", "TF.EX.Common", "TF.EX.Core", "TF.EX.Domain", "TF.EX.Patchs")
OPS = ("ldfld", "stfld", "ldsfld", "stsfld", "ldflda", "ldsflda", "call", "callvirt", "newobj", "ldftn", "ldvirtftn")
REF = re.compile(r"\[TowerFall\]((?:Monocle|TowerFall)[A-Za-z0-9_.]*(?:/[A-Za-z0-9_`]+)*)(?:<[^>]*>)?::([A-Za-z0-9_.<>|`]+)")
_md = Cs(CS_ARCH_ARM64, CS_MODE_LITTLE_ENDIAN)

def normsig(params):
    """'class [TowerFall]Monocle.Subtexture, valuetype [FNA]X.Vector2' -> 'Monocle.Subtexture,X.Vector2' (sigs.py's form too)."""
    out = []
    for p in [x.strip() for x in re.split(r",(?![^<]*>)", params) if x.strip()]:
        p = re.sub(r"^(class|valuetype)\s+", "", p)
        p = re.sub(r"\[[A-Za-z][^\]]*\]", "", p).replace("/", "+").split(" ")[0]
        out.append(p)
    return ",".join(out)

def scan(il_dir, only_class=None):
    refs = collections.Counter()      # (kind, type, member) -> count
    dyn = collections.Counter()
    for asm in NETWORK:
        p = os.path.join(il_dir, asm + ".il")
        if not os.path.exists(p): continue
        stack, pending_str = [], None
        for line in open(p, encoding="utf-8", errors="replace"):
            s = line.strip()
            if s.startswith(".class "):
                n = s.split()[-1].strip("'")
                stack.append(stack[-1] + "+" + n if " nested " in s and stack else n)
            elif s.startswith("} // end of class") and stack:
                stack.pop()
            cls = stack[-1] if stack else None   # nested classes (lambdas' display classes) count for their outer class
            if only_class and (cls is None or not cls.split("+")[0].split(".")[-1] == only_class): continue
            m = re.match(r"IL_[0-9a-f]+: (\S+)\s+(.*)", s)
            if not m: continue
            op, arg = m.groups()
            if op == "ldstr": pending_str = arg.strip('"'); continue
            if "DynamicData::" in arg and any(k in arg for k in ("::Get", "::Set", "::Invoke")) and pending_str:
                dyn[pending_str] += 1
            if op in ("call", "callvirt", "ldftn", "ldvirtftn") and "DynamicData" not in arg: pending_str = None
            if op not in OPS: continue
            r = REF.search(arg)
            if not r: continue
            t, mem = r.group(1).replace("/", "+"), r.group(2)
            kind = "field" if "fld" in op else "method"
            if kind == "method":
                tail = arg[r.end():]
                params = tail[1:tail.rfind(")")] if tail.startswith("(") else ""
                mem = mem + "(" + normsig(params) + ")"   # one member per overload
            refs[(kind, t, mem)] += 1
    return refs, dyn

_rec = {}
def records(typename):
    """classmeta.records, cached; [] where its builder simulation fails (a few non-game types)."""
    if typename not in _rec:
        try: _rec[typename] = cm.records(typename) or []
        except Exception: _rec[typename] = []
    return _rec[typename]

def accessor_offset(typename, prop):
    """The field offset behind get_<prop> when its body is `ldr{b,h,sb,sh,} w/x/s, [x0, #off]; ret`."""
    for n, body, fl in records(typename):
        if n != "get_" + prop: continue
        ins = list(_md.disasm(rtypes._img()[body:body + 8], body))
        if len(ins) == 2 and ins[0].mnemonic.startswith("ldr") and "[x0" in ins[0].op_str and ins[1].mnemonic == "ret":
            m = re.search(r"#(0x[0-9a-f]+|\d+)", ins[0].op_str)
            return int(m.group(1), 0) if m else 0
    return None

def resolve(refs):
    tab = rtypes.table()
    out = {}
    for (kind, t, mem) in refs:
        if t not in tab:
            out[(kind, t, mem)] = ("no type", None); continue
        if kind == "field":
            off = rtypes.field(t, mem)
            if off is None:
                for owner, f, o, ty in rtypes.fields(t, statics=True):
                    if f == mem: off = o
            if off is not None: out[(kind, t, mem)] = ("field record", off); continue
            # an auto-property's backing field is not in the records; nor are private fields
            for c in rtypes.chain(t):
                o = accessor_offset(c.name, mem)
                if o is not None: out[(kind, t, mem)] = ("accessor", o); break
            else: out[(kind, t, mem)] = ("unresolved", None)
        else:
            name, want = mem.split("(")[0], mem[mem.index("(") + 1:-1] if "(" in mem else None
            name = name.split("<")[0] if not name.startswith("<") else name
            found = None
            for c in rtypes.chain(t):
                rec = [b for n, b, fl in records(c.name) if n == name]
                if rec: found = (c.name, rec); break
            if found and len(found[1]) > 1:   # overloads: by the IL parameter list (sigs.py pairs PC signatures with Switch bodies by order)
                import sigs
                pick = [(k, b) for n, k, b, params, st, mis in sigs.paired(found[0]) if n == name and normsig(params) == want and b]
                if pick: out[(kind, t, mem)] = ("method (overload #%d of %d by signature)" % (pick[0][0], len(found[1])), pick[0][1]); continue
            if found: out[(kind, t, mem)] = ("method" if len(found[1]) == 1 else "method (%d overloads)" % len(found[1]), found[1][0])
            else: out[(kind, t, mem)] = ("unresolved", None)
    return out

def dyn_resolve(names):
    tab = rtypes.table(); out = {}
    for n in names:
        hits = [t for t, ty in tab.items() if ty.fields and any(f[0] == n for f in ty.fields) and t.split(".")[0] in ("TowerFall", "Monocle")]
        out[n] = hits
    return out

if __name__ == "__main__":
    a = sys.argv[1:]
    verbose = "-v" in a; a = [x for x in a if x != "-v"]
    only = None
    if "--class" in a: i = a.index("--class"); only = a[i + 1]; del a[i:i + 2]
    il_dir = a[0] if a else IL_DIR
    refs, dyn = scan(il_dir, only)
    res = resolve(refs)
    by = collections.Counter(); uses = collections.Counter()
    for k, (how, v) in res.items():
        cls = "resolved" if how not in ("unresolved", "no type") else how
        by[(k[0], cls)] += 1; uses[(k[0], cls)] += refs[k]
    print(f"TF.EX network part{' (class ' + only + '*)' if only else ''}: {len(refs)} distinct TowerFall/Monocle members, {sum(refs.values())} uses")
    for kind in ("field", "method"):
        tot = sum(v for (k, c), v in by.items() if k == kind)
        ok = by[(kind, "resolved")]
        print(f"  {kind:6}: {ok}/{tot} resolve ({100 * ok / max(tot, 1):.0f} %); by use {uses[(kind, 'resolved')]}/{sum(v for (k, c), v in uses.items() if k == kind)}"
              f"  [no type on the Switch: {by[(kind, 'no type')]}, unresolved: {by[(kind, 'unresolved')]}]")
    how = collections.Counter(h.split(" (")[0] for h, v in res.values())
    print("  how:", dict(how))
    bysig = sum(1 for h, v in res.values() if "by signature" in h); amb = sum(1 for h, v in res.values() if h.endswith("overloads)"))
    print(f"  overloaded methods: {bysig} picked by their IL signature (sigs.py), {amb} not (a signature sigs.py can't parse: generics, by-ref)")
    dr = dyn_resolve(dyn)
    okd = sum(1 for n in dr if len(dr[n]) >= 1)
    print(f"  DynamicData names: {len(dyn)} distinct ({sum(dyn.values())} uses); {okd} found in some type's field records "
          f"(BRUTE keeps no private fields in reflection, so these need accessor / code analysis)")
    if verbose:
        for k in sorted(res):
            how, v = res[k]
            if how in ("unresolved", "no type") or only:
                print(f"    {how:22} {k[0]:6} {k[1]}::{k[2]}  x{refs[k]}" + (f"  -> {v:#x}" if isinstance(v, int) else ""))
        for n in sorted(dyn): print(f"    dyn {n:30} x{dyn[n]}  {','.join(dr[n]) or '-'}")
