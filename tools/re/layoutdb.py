#!/usr/bin/env python3
"""layoutdb.py: the Switch's offsets of game reference fields, from runtime slot maps (docs/tfstate-rollback-measurement.md, phase 1).

Input: "[state] slots <Class>: <off>=<Type> ..." lines (hw.txt "statelayout <every>", tfexgen_state.cpp): for live objects, each
8-byte slot that held an object and that object's runtime type. For each class, its fields and its base classes' (the PC IL's
declaration order) are matched to those slots:
  - known offsets first (reflection records, translate.toml [pin], accessors.json): they claim their slots;
  - then, for each declared type, the unclaimed slots whose runtime type is that type or a subclass of it: as many slots as fields of
    that type -> paired in order (declaration order = offset order within a type: checked on the known fields, 157 of 163 groups);
  - a field placed differently by two snapshots / classes is dropped.
Only reference fields; a field that was null in every snapshot stays unplaced.

  layoutdb.py <slots files...> [--il PATH] [--out layoutdb.json] [--pins]   (--pins: state-pins.toml entries for the placed fields)"""
import os, re, sys, json, collections
import rtypes

here = os.path.dirname(os.path.abspath(__file__))
PRIM = {'float32': 'Single', 'float64': 'Double', 'int32': 'Int32', 'uint32': 'UInt32', 'int64': 'Int64', 'bool': 'Boolean',
        'uint8': 'Byte', 'int16': 'Int16', 'char': 'Char', 'string': 'String', 'object': 'Object'}

def il_fields(il_path):
    """{class: [(field, il type)]} instance fields in declaration order"""
    out, stack = {}, []
    for line in open(il_path, encoding='utf-8', errors='replace'):
        s = line.strip()
        if s.startswith('.class'):
            name = re.search(r"([\w.`<>']+)\s+extends", s).group(1) if 'extends' in s else s.split()[-1]
            stack.append(name); out.setdefault('+'.join(stack), []); continue
        if stack and s.startswith('} // end of class'): stack.pop(); continue
        m = re.match(r"\.field\s+(.*?)\s+('[^']+'|[\w<>$`]+)\s*$", s)
        if stack and m and ' static ' not in ' ' + m.group(1) + ' ':
            out['+'.join(stack)].append((m.group(2).strip("'"), m.group(1)))
    return out

def runtime_name(il_type):
    """the runtime type name an object in a field of this IL type would show (TypeName): None for value types"""
    t = il_type.strip()
    am = re.search(r'(?:class\s+|valuetype\s+)?(?:\[[^\]]+\])?([\w.`/+]+)(?:<[^>]*>)?\[\]\s*$', t)
    if am: return PRIM.get(am.group(1), re.split(r'[./+]', am.group(1))[-1]) + '[]'
    if t in ('string', 'object'): return PRIM[t]
    m = re.search(r'class\s+(?:\[[^\]]+\])?([\w.`/+]+)', t)
    return re.split(r'[./+]', m.group(1))[-1] if m else None

def main(argv):
    files = [a for a in argv if not a.startswith('--') and os.path.exists(a)]
    il = next((argv[i + 1] for i, a in enumerate(argv) if a == '--il'), os.path.join(here, '..', '..', '.cache', 'towerfall-pc.il'))
    out = next((argv[i + 1] for i, a in enumerate(argv) if a == '--out'), os.path.join(here, 'layoutdb.json'))
    decl = il_fields(il)
    # a map continued over several lines ("[state] slots+ <Class>:"): one line again
    joined = []
    for f in files:
        lines = []
        for line in open(f):
            line = line.rstrip('\n')
            m = re.match(r'\[state\] slots\+ ([\w`+]+):(.*)', line)
            if m and lines and lines[-1].startswith(f'[state] slots {m.group(1)}:'): lines[-1] += m.group(2)
            elif line.startswith('[state] slots '): lines.append(line)
        tmp = f + '.joined'; open(tmp, 'w').write('\n'.join(lines) + '\n'); joined.append(tmp)
    files = joined
    tab = rtypes.table()
    short2full = collections.defaultdict(list)
    for c in decl: short2full[re.split(r'[.+]', c)[-1]].append(c)
    def full_of(short):
        cands = short2full.get(short, [])
        pref = [c for c in cands if c.startswith(('TowerFall.', 'Monocle.'))]
        return (pref or cands or [None])[0]
    def chain(full):   # base first
        out_, c = [], full
        while c and c in tab and c not in ('System.Object',):
            out_.append(c); c = tab[c].parent
        return out_[::-1]
    def is_a(rt_short, declared_short):
        if rt_short == declared_short: return True
        c = full_of(rt_short)
        while c and c in tab:
            c = tab[c].parent
            if c and re.split(r'[.+]', c)[-1] == declared_short: return True
        return False
    known = {}   # (class, field) -> offset
    for c, t in tab.items():
        for n, o, a, ty in (t.fields or []):
            if o is not None and not ((a or 0) & rtypes.STATIC): known[(c, n)] = o
    tt = open(os.path.join(here, '..', 'tfex', 'translate.toml')).read()
    for m in re.finditer(r'\[pin\."([\w.+`]+)\.(\w+)\(\)"\]\s*off = (0x[0-9a-f]+)', tt): known[(m.group(1), m.group(2))] = int(m.group(3), 16)
    acc = os.path.join(here, 'accessors.json')   # backing fields, from the compiled accessors (step C): their slots are taken
    if os.path.exists(acc):
        for k, o in json.load(open(acc)).items(): known.setdefault(tuple(k.rsplit('.', 1)), o)
    votes = collections.defaultdict(set)
    # each class's slots over all its snapshots (a field null in one is usually set in another); a slot that showed two unrelated types
    # is dropped
    merged = collections.defaultdict(dict)
    for f in files:
        for line in open(f):
            m = re.match(r'\[state\] slots ([\w`+]+):(.*)', line.strip())
            if not m or len(line.rstrip('\n')) >= 500: continue
            for o, t in re.findall(r'(\w+)=(\S+)', m.group(2)):
                o = int(o, 16); d = merged[m.group(1)]
                if o in d and d[o] != t and not (is_a(t, d[o]) or is_a(d[o], t)): d[o] = None
                elif o not in d or (d[o] and is_a(d[o], t)): d[o] = t   # (the more general of two related types)
    views = [(cls, {o: t for o, t in d.items() if t}) for cls, d in merged.items()]   # the merged view of each class
    cut = {}   # a view from a line the log cut (500+ characters, older dumps): equal counts only there (a field past the cut makes them differ)
    for f in files:   # and each snapshot on its own
        for line in open(f):
            line = line.rstrip('\n')
            m = re.match(r'\[state\] slots ([\w`+]+):(.*)', line)
            if not m: continue
            toks = re.findall(r'(\w+)=(\S+)', m.group(2))
            if len(line) >= 500 and toks: toks = toks[:-1]
            v = {int(o, 16): t for o, t in toks}
            views.append((m.group(1), v))
            if len(line) >= 500: cut[id(v)] = max(v) if v else 0
    known0 = dict(known); first_pass = set()
    for passno in (0, 1):
      if passno == 1: first_pass = {k for k in known if k not in known0}
      for cls, slots in views:
            full = full_of(cls)
            if not full: continue
            fields = [(c, n, runtime_name(ty)) for c in chain(full) for n, ty in decl.get(c, []) if runtime_name(ty)]
            claimed = set()
            for c, n, rt in fields:
                if (c, n) in known: claimed.add(known[(c, n)])
            bytype = collections.OrderedDict()   # (declaring class, type): a class's own fields live in its own byte range
            for c, n, rt in fields:
                if (c, n) not in known: bytype.setdefault((c, rt), []).append((c, n))
            def rng(c):   # [the parent's size - 7, the class's size): its fields' offsets (C++ tail padding reuse)
                par = tab[c].parent if c in tab else None
                lo = (tab[par].size or 16) - 7 if par in tab and tab[par].size else 16
                return lo, (tab[c].size or 1 << 20) if c in tab else 1 << 20
            # exact types first, then subclasses
            for exact in (True, False):
                for (dc, rt), fs in bytype.items():
                    lo, hi = rng(dc)
                    os_ = sorted(o for o, t in slots.items() if o not in claimed and lo <= o < hi and (t == rt if exact else is_a(t, rt)))
                    if fs and len(os_) == len(fs):
                        for (c, n), o in zip(fs, os_):
                            votes[(c, n)].add(o); claimed.add(o)
                        bytype[(dc, rt)] = []
                    elif id(slots) not in cut and fs and 0 < len(os_) < len(fs) and os_[-1] - os_[0] == 8 * (len(fs) - 1) and \
                            not any(o in claimed for o in range(os_[0], os_[-1] + 8, 8)) and not any(o in slots and o not in os_ for o in range(os_[0], os_[-1] + 8, 8)):
                        # a run of N same-typed fields over exactly N words, some null now: in order, the holes included
                        for i, (c, n) in enumerate(fs):
                            votes[(c, n)].add(os_[0] + 8 * i); claimed.add(os_[0] + 8 * i)
                        bytype[(dc, rt)] = []
      if passno == 0:   # fields every view agreed on are known for the second pass (base classes' fields above all)
        for (c, n), v in list(votes.items()):
            if len(v) == 1 and (c, n) not in known: known[(c, n)] = next(iter(v))
        votes.clear()
    placed = {f'{c}.{n}': next(iter(v)) for (c, n), v in votes.items() if len(v) == 1}
    placed.update({f'{c}.{n}': o for (c, n), o in known.items() if (c, n) in first_pass})
    # an offset two fields of one class claim: neither
    byclass = collections.defaultdict(list)
    for k, o in placed.items(): byclass[k.rsplit('.', 1)[0]].append((o, k))
    for c, lst in byclass.items():
        cnt = collections.Counter(o for o, k in lst)
        for o, k in lst:
            if cnt[o] > 1: placed.pop(k, None)
    conflicted = sorted(f'{c}.{n}' for (c, n), v in votes.items() if len(v) > 1)
    json.dump({'placed': placed, 'conflicted': conflicted}, open(out, 'w'), indent=1, sort_keys=True)
    print(f'layoutdb: {len(placed)} reference fields placed, {len(conflicted)} conflicting -> {out}')
    if '--pins' in argv:
        for k, o in sorted(placed.items()):
            print(f'[pin."{k}()"]\noff = {hex(o)}\nwhy = "layoutdb.py: runtime slot maps matched to the PC fields by type and order"')
    return placed

if __name__ == '__main__':
    main(sys.argv[1:])
