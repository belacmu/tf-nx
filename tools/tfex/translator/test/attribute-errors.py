"""attribute-errors.py <gen dir>: the compile errors of compile-gen.sh (<file>.err) -> the methods they fall in (the /*@M Type.Method*/
markers the translator writes before each method and lambda). Prints one Type.Method per line; header errors mark every method of
the unit."""
import os, re, sys
d = sys.argv[1]
bad = set()
for f in os.listdir(d):
    if not f.endswith(".cpp.err"): continue
    src = os.path.join(d, f[:-4])
    lines = open(src, encoding="utf-8").read().split("\n")
    marks = [(i + 1, m.group(1)) for i, l in enumerate(lines) if (m := re.match(r"/\*@M (\S+)\*/", l))]
    def owner_of(ln):
        o = [n for i, n in marks if i <= ln]
        return o[-1] if o else None
    pending_here = None
    for e in open(os.path.join(d, f), encoding="utf-8", errors="replace"):
        # a template instantiated from our file: GCC names our line ("required from here" / "required from ...")
        rf = re.match(r"(?:.*/)?([^/:]+):(\d+):\d+:\s+required from", e)
        if rf and rf.group(1) == f[:-4]:
            pending_here = owner_of(int(rf.group(2)))
            continue
        m = re.match(r"(?:.*/)?([^/:]+):(\d+):\d+: (?:fatal )?error", e)
        if not m: continue
        file, ln = m.group(1), int(m.group(2))
        if file == f[:-4]:
            if owner_of(ln): bad.add(owner_of(ln))
            else:
                for _, n in marks: bad.add(n)   # before the first method (an #include, the prelude): the whole unit
        elif file == f[:-4].replace(".cpp", ".hpp"):
            for _, n in marks: bad.add(n)   # the unit's own header is broken: the whole unit
        elif pending_here:
            bad.add(pending_here)           # an error inside a runtime template, instantiated from that method
print("\n".join(sorted(bad)))
