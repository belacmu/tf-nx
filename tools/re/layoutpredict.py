#!/usr/bin/env python3
"""layoutpredict.py: step C of docs/tfstate-rollback-measurement.md. The Switch build lays out a class's instance fields like a C struct:
the PC IL's declaration order, natural alignment, the first field right after the parent's last byte (Bat's first field at 444 in
Enemy's 448: tail padding is reused), the size rounded up to 8. The Switch build adds fields of its own here and there (ArcherData),
so a prediction is only kept where anchors confirm it:
  - anchors: offsets known from reflection, translate.toml [pin]s and layoutdb.json (reference fields from the runtime slot maps),
    and the class's size from its type record (the end);
  - the fields between two anchors are kept when the rule, started at the first, puts the second exactly where it is (and before the
    first anchor: when started at the parent's verified end; after the last: when the predicted end rounds to the class's size).
Output: layoutpredict.json {"placed": {"Class.field": offset}, "how": {...}} and a summary.

  layoutpredict.py [--ldb layoutdb.json] [--out layoutpredict.json] [--check]   (--check: the rule against every known offset)"""
import os, re, sys, json, collections
here = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, here)
import rtypes, layoutdb

MODS = {'public', 'private', 'family', 'assembly', 'famorassem', 'famandassem', 'initonly', 'notserialized', 'specialname',
        'rtspecialname', 'static', 'literal'}
PRIM = {'bool': (1, 1), 'char': (2, 2), 'int8': (1, 1), 'uint8': (1, 1), 'int16': (2, 2), 'uint16': (2, 2), 'int32': (4, 4),
        'uint32': (4, 4), 'float32': (4, 4), 'int64': (8, 8), 'uint64': (8, 8), 'float64': (8, 8), 'native int': (8, 8),
        'native uint': (8, 8)}
EXTERNAL = {'Microsoft.Xna.Framework.Vector2': (8, 4), 'Microsoft.Xna.Framework.Vector3': (12, 4), 'Microsoft.Xna.Framework.Vector4': (16, 4),
            'Microsoft.Xna.Framework.Color': (4, 4), 'Microsoft.Xna.Framework.Rectangle': (16, 4), 'Microsoft.Xna.Framework.Point': (8, 4),
            'Microsoft.Xna.Framework.Matrix': (64, 4), 'Microsoft.Xna.Framework.Quaternion': (16, 4), 'System.TimeSpan': (8, 8),
            'System.DateTime': (8, 8), 'System.Guid': (16, 4), 'System.Decimal': (16, 4)}

class Layout:
    def __init__(self, il):
        self.decl = layoutdb.il_fields(il)
        self.tab = rtypes.table()
        self.memo = {}

    def ty(self, il_type):
        """the bare IL type of a field declaration ("private valuetype [FNA]X" -> "valuetype [FNA]X")"""
        words = il_type.split()
        while words and words[0] in MODS: words.pop(0)
        return ' '.join(words)

    def size_align(self, t):
        """(size, alignment) of a field of IL type t, or None when unknown"""
        t = re.sub(r'\[[\w.]+\]', '', t).strip()   # (assembly qualifiers)
        if t in PRIM: return PRIM[t]
        if t.endswith(']') or t.startswith('class ') or t in ('string', 'object') or t.endswith('*'): return (8, 8)
        if t.startswith('!'): return None   # a generic parameter
        m = re.match(r'valuetype\s+(.+)$', t)
        if not m: return None
        name = m.group(1)
        g = re.match(r'System\.Nullable`1<(.+)>$', name)
        if g:
            inner = self.size_align(g.group(1) if g.group(1) in PRIM else 'valuetype ' + g.group(1))
            if not inner: return None
            s, a = inner; off = (1 + a - 1) // a * a
            return ((off + s + a - 1) // a * a, a)
        if name in EXTERNAL: return EXTERNAL[name]
        key = name.replace('/', '+')
        fs = self.decl.get(key)
        if fs is None: return None
        if len(fs) == 1 and fs[0][0] == 'value__': return self.size_align(self.ty(fs[0][1]))   # an enum
        r = self.struct(key)
        return r

    def struct(self, key):
        """a value type's (size, alignment) from its fields, laid out from 0"""
        if key in self.memo: return self.memo[key]
        self.memo[key] = None
        off, al = 0, 1
        for n, it in self.decl.get(key, []):
            sa = self.size_align(self.ty(it))
            if not sa: return None
            s, a = sa; off = (off + a - 1) // a * a + s; al = max(al, a)
        self.memo[key] = ((off + al - 1) // al * al if off else 1, al)
        return self.memo[key]

    def own(self, cls, start):
        """[(field, predicted offset, size)] of cls's own fields laid out from start, and the end; a field of unknown size stops it"""
        out, off = [], start
        for n, it in self.decl.get(cls, []):
            sa = self.size_align(self.ty(it))
            if not sa: return out, None
            s, a = sa; off = (off + a - 1) // a * a
            out.append((n, off, s)); off += s
        return out, off

def known_offsets(lay, ldb, acc):
    """{(class, field): offset} from reflection, translate.toml [pin]s, the property accessors (accessors.json) and layoutdb.json"""
    known = {}
    for c, t in lay.tab.items():
        for n, o, a, ty in (t.fields or []):
            if o is not None and not ((a or 0) & rtypes.STATIC): known[(c, n)] = o
    tt = open(os.path.join(here, '..', 'tfex', 'translate.toml')).read()
    for m in re.finditer(r'\[pin\."([\w.+`]+)\.(\w+)\(\)"\]\s*off = (0x[0-9a-f]+)', tt): known[(m.group(1), m.group(2))] = int(m.group(3), 16)
    for k, o in acc.items(): known.setdefault(tuple(k.rsplit('.', 1)), o)
    strong = collections.defaultdict(set)   # bytes the stronger sources hold, per class: a slot-map placement on them is dropped
    for (c, n), o in known.items(): strong[c].add(o)
    for k, o in ldb.items():
        c, n = k.rsplit('.', 1)
        if (c, n) not in known and o not in strong[c]: known[(c, n)] = o
    return known

BACKING = re.compile(r'<\w+>k__BackingField$')

def predict(lay, known):
    """{(class, field): offset} kept, {class: its verified data end or None}. Per class: its fields with a known offset are fixed;
    the others, in the PC IL's order without the backing fields (the Switch places those elsewhere), go after one another (natural
    alignment, skipping the bytes a fixed field holds). A run between two fixed fields of that order is kept when the walk from the
    first lands exactly on the second; the run after the last one when no backing field is left unplaced and the end rounds to the
    class's size; the run before the first one starts at the parent's verified end."""
    placed, ends = {}, {'System.Object': 16, 'System.ValueType': 16}
    def end_of(c):
        if c in ends: return ends[c]
        ends[c] = None
        par = lay.tab[c].parent if c in lay.tab else None
        size = lay.tab[c].size if c in lay.tab else None
        if par is None or not isinstance(size, int): return None
        ps = end_of(par)
        own = []
        for n, it in lay.decl.get(c, []):
            sa = lay.size_align(lay.ty(it))
            if not sa: return None
            own.append((n, sa[0], sa[1]))
        fixed = {n: known[(c, n)] for n, s, a in own if (c, n) in known}
        occupied = [(fixed[n], fixed[n] + s) for n, s, a in own if n in fixed]; occ_names = [n for n, s, a in own if n in fixed]
        unknown_backing = [n for n, s, a in own if BACKING.match(n) and n not in fixed]
        seq = [(n, s, a) for n, s, a in own if not BACKING.match(n)]   # (backing fields: not in the IL's order; placed ones only occupy bytes)
        def greedy(off, n, s, a):
            while True:
                off = (off + a - 1) // a * a
                hit = next((e for (b, e), m in zip(occupied, occ_names) if m != n and b < off + s and off < e), None)
                if hit is None: return off
                off = hit
        def layouts(start, items, accept):
            """up to 2 distinct layouts of items from start that accept(layout, end) takes. At each field: the greedy spot, or after a
            fixed field that starts within the next 8 bytes (a source order may declare the field after it: ArrowCushion's rotation
            sits after <ArrowDatas>, not in the padding before it; HUD's HUDVisible after <Level>). Two layouts that both close: the
            run is ambiguous."""
            memo = {}
            def go(i, off):
                if (i, off) in memo: return memo[(i, off)]
                if i == len(items):
                    r = [((), off)]
                else:
                    n, s_, a_ = items[i]
                    opts = {greedy(off, n, s_, a_)}
                    for (b, e), m in zip(occupied, occ_names):
                        if m != n and off <= b < off + 8: opts.add(greedy(e, n, s_, a_))
                    r = []
                    for p in sorted(opts):
                        for rest, end in go(i + 1, p + s_):
                            r.append((((n, p),) + rest, end))
                            if len(r) > 64: break
                memo[(i, off)] = r
                return r
            out = []
            for lay_, end in go(0, start):
                if accept(lay_, end) and lay_ not in out: out.append(lay_)
                if len(out) > 1: break
            return out
        idx = [i for i, (n, s, a) in enumerate(seq) if n in fixed]
        pts = [(-1, ps)] + [(i, fixed[seq[i][0]]) for i in idx]
        for (i0, o0), (i1, o1) in zip(pts, pts[1:]):
            if o0 is None: continue
            start = o0 if i0 < 0 else o0 + seq[i0][1]
            res = layouts(start, [x for x in seq[i0 + 1:i1] if x[0] not in fixed] + [seq[i1]], lambda l, end: l[-1][1] == o1)
            if len(res) == 1:
                for n, o in res[0]: placed[(c, n)] = o
        i0, o0 = pts[-1]
        if o0 is not None and not unknown_backing:
            start = o0 if i0 < 0 else o0 + seq[i0][1]
            def closes(l, off):
                end = max([off] + [e for b, e in occupied] + ([ps] if ps else []))
                return (end + 7) // 8 * 8 == size
            items = [x for x in seq[i0 + 1:] if x[0] not in fixed]
            res = layouts(start, items, closes)
            if len(res) == 1:
                for n, o in res[0]: placed[(c, n)] = o
                last = max([o + dict((x[0], x[1]) for x in items)[n] for n, o in res[0]] + [start])
                ends[c] = max([last] + [e for b, e in occupied] + ([ps] if ps else []))
            elif not items and closes((), start):
                ends[c] = max([start] + [e for b, e in occupied])
        for n, o in fixed.items(): placed.setdefault((c, n), o)
        return ends[c]
    for c in list(lay.tab):
        if c.startswith(('TowerFall.', 'Monocle.')) and c in lay.decl: end_of(c)
    return placed, ends

def main(argv):
    il = os.path.join(here, '..', '..', '.cache', 'towerfall-pc.il')
    ldbp = next((argv[i + 1] for i, a in enumerate(argv) if a == '--ldb'), os.path.join(here, 'layoutdb.json'))
    out = next((argv[i + 1] for i, a in enumerate(argv) if a == '--out'), os.path.join(here, 'layoutpredict.json'))
    lay = Layout(il); ldb = json.load(open(ldbp))['placed']
    acc = json.load(open(os.path.join(here, 'accessors.json')))
    known = known_offsets(lay, ldb, acc)
    placed, ends = predict(lay, known)
    if '--check' in argv:   # each known offset left out in turn: does the rule, anchored by the others, put it back there?
        agree = differ = 0; bad = []
        keys = [k for k in known if k[1] and k[0].startswith(('TowerFall.', 'Monocle.')) and not BACKING.match(k[1]) and k[0] in lay.decl]
        for k in keys:
            kn = dict(known); del kn[k]
            p, _ = predict(lay, kn)
            if k in p:
                if p[k] == known[k]: agree += 1
                else: differ += 1; bad.append((f'{k[0]}.{k[1]}', p[k], known[k]))
        print(f'leave-one-out: {agree} known offsets predicted back, {differ} wrong, {len(keys) - agree - differ} not predicted; wrong: {bad[:12]}')
    res = {f'{c}.{n}': o for (c, n), o in placed.items()}
    json.dump({'placed': res}, open(out, 'w'), indent=1, sort_keys=True)
    print(f'layoutpredict: {len(res)} fields placed (anchored), {sum(1 for e in ends.values() if e is not None)} classes with a verified end -> {out}')

if __name__ == '__main__':
    main(sys.argv[1:])
