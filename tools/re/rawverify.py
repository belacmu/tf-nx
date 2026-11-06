#!/usr/bin/env python3
"""rawverify.py: step C's check on real bytes. For each field the layout databases place (layoutdb.json, accessors.json,
layoutpredict.json; translate.toml's confirmed pins), the values at that offset in the raw dump's records of its class and every
subclass (rawdump.tgz, step B) must be plausible for its type:
  bool 0/1; float32 finite, |x| < 1e7; enums -16..4095; a reference 0, a heap address or one in the module images
(string literals), 8-aligned.
A field no record of its class reaches is "unseen". Also judges the disputed earlier leads (state-pins.toml): both offsets.

  rawverify.py <raw dir> [--fields A.b,...]"""
import os, re, sys, json, math, struct, collections
here = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, here)
import rtypes, layoutdb, rawcheck, layoutpredict as lp

def plausible(kind, b):
    if kind == 'bool': return b[0] in (0, 1)
    if kind == 'ref':
        v = struct.unpack('<Q', b)[0]   # (the heap, or the module images: string literals)
        return v == 0 or (v & 7 == 0 and (0x1000000000 <= v < 0x8000000000 or 0x8000000 <= v < 0x40000000))
    if kind == 'f32':
        x = struct.unpack('<f', b)[0]; return math.isfinite(x) and abs(x) < 1e7 and (x == 0 or abs(x) > 1e-30)
    if kind == 'enum': return -16 <= struct.unpack('<i', b)[0] < 4096   # (Allegiance.Neutral, Facing.Left: -1)
    if kind == 'i32': return True   # (seeds, hashes: any value)
    return True

def kinds(lay, il_type):
    """[(relative offset, kind, size)] checks for a field of this IL type ([] when nothing is checkable)"""
    t = re.sub(r'\[[\w.]+\]', '', lay.ty(il_type)).strip()
    if t == 'bool': return [(0, 'bool', 1)]
    if t == 'float32': return [(0, 'f32', 4)]
    if t in ('int32', 'uint32'): return [(0, 'i32', 4)]
    if t.endswith(']') or t.startswith('class ') or t in ('string', 'object'): return [(0, 'ref', 8)]
    if t in ('valuetype Microsoft.Xna.Framework.Vector2',): return [(0, 'f32', 4), (4, 'f32', 4)]
    m = re.match(r'valuetype\s+(.+)$', t)
    if m:
        fs = lay.decl.get(m.group(1).replace('/', '+'))
        if fs and len(fs) == 1 and fs[0][0] == 'value__' and 'int32' in fs[0][1]: return [(0, 'enum', 4)]
    return []

def main(argv):
    d = argv[0]
    lay = lp.Layout(os.path.join(here, '..', '..', '.cache', 'towerfall-pc.il'))
    ldb = json.load(open(os.path.join(here, 'layoutdb.json')))['placed']; acc = json.load(open(os.path.join(here, 'accessors.json')))
    known = lp.known_offsets(lay, ldb, acc)
    for k, o in json.load(open(os.path.join(here, 'layoutpredict.json')))['placed'].items(): known.setdefault(tuple(k.rsplit('.', 1)), o)
    stems = {}
    for c in lay.tab: stems.setdefault(re.sub(r'[^A-Za-z0-9_]', '_', re.split(r'[.+]', c)[-1]), c)
    recs = collections.defaultdict(list)   # class -> records of it and its subclasses
    for f in os.listdir(d):
        if not f.endswith('.bin') or f[:-4] not in stems: continue
        rs, _ = rawcheck.records(os.path.join(d, f))
        c = stems[f[:-4]]
        while c and c in lay.tab: recs[c] += [r[3] for r in rs]; c = lay.tab[c].parent
    def judge(c, n, o):
        it = dict(lay.decl.get(c, [])).get(n)
        if it is None or not recs.get(c): return 'unseen'
        ks = kinds(lay, it)
        if not ks: return 'unchecked'
        for r in recs[c]:
            for ro, kd, sz in ks:
                if o + ro + sz > len(r) or not plausible(kd, r[o + ro:o + ro + sz]): return 'bad'
        return 'ok'
    cnt = collections.Counter(); bad = []
    for (c, n), o in known.items():
        if not n or not c.startswith(('TowerFall.', 'Monocle.')): continue
        v = judge(c, n, o); cnt[v] += 1
        if v == 'bad': bad.append(f'{c}.{n}@{o:#x}')
    print(f'rawverify: placed fields on real bytes: {dict(cnt)}')
    print('  implausible:', ' '.join(sorted(bad)[:60]))
    leads = {f'{m.group(1)}.{m.group(2)}': int(m.group(3), 16) for m in re.finditer(r'\[pin\."([\w.+`]+)\.(\w+)\(\)"\]\s*off = (0x[0-9a-f]+)',
             open(os.path.join(here, '..', 'tfex', 'state-pins.toml')).read())}
    dis = [(k, leads[k], known[tuple(k.rsplit('.', 1))]) for k in leads if tuple(k.rsplit('.', 1)) in known and known[tuple(k.rsplit('.', 1))] != leads[k]]
    print(f'disputed earlier leads ({len(dis)}): field  database offset: verdict | lead offset: verdict')
    for k, lo, do in sorted(dis):
        c, n = k.rsplit('.', 1)
        print(f'  {k}  {do:#x}: {judge(c, n, do)} | {lo:#x}: {judge(c, n, lo)}')

if __name__ == '__main__':
    main(sys.argv[1:])
