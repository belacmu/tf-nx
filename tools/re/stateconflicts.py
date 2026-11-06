#!/usr/bin/env python3
"""stateconflicts.py: rule 2 of docs/tfstate-rollback-measurement.md (ranked sources; a conflict is an error). Fails (exit 1) on:
  - a field two sources place at different offsets (confirmed pins, reflection, accessors.json, layoutdb.json);
  - two fields of a class (its own and its bases') whose bytes overlap in the final map (sources + layoutpredict.json);
  - a leave-one-out miss of layoutpredict.py that isn't in EXPLAINED below.

  stateconflicts.py [--no-loo]"""
import os, re, sys, json, collections
here = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, here)
import rtypes, layoutpredict as lp

# leave-one-out misses with a reason (a new miss fails the check)
EXPLAINED = {
    'TowerFall.MapButton.MapXIndex': 'menu data: a Switch-only field between its anchors that alignment hides',
    'TowerFall.UnlockData.AscensionBoxOpen': 'save data: a Switch-only bool one byte before it',
}

def main(argv):
    lay = lp.Layout(os.path.join(here, '..', '..', '.cache', 'towerfall-pc.il'))
    ldb = json.load(open(os.path.join(here, 'layoutdb.json')))['placed']
    acc = json.load(open(os.path.join(here, 'accessors.json')))
    pre = json.load(open(os.path.join(here, 'layoutpredict.json')))['placed']
    tt = open(os.path.join(here, '..', 'tfex', 'translate.toml')).read()
    srcs = collections.defaultdict(dict)   # field -> {source: offset}
    for c, t in lay.tab.items():
        for n, o, a, ty in (t.fields or []):
            if n and o is not None and not ((a or 0) & rtypes.STATIC): srcs[f'{c}.{n}']['reflection'] = o
    for m in re.finditer(r'\[pin\."([\w.+`]+)\.(\w+)\(\)"\]\s*off = (0x[0-9a-f]+)', tt): srcs[f'{m.group(1)}.{m.group(2)}']['pin'] = int(m.group(3), 16)
    for k, o in acc.items(): srcs[k]['accessor'] = o
    for k, o in ldb.items(): srcs[k]['slot maps'] = o
    errors = []
    for k, d in sorted(srcs.items()):
        if len(set(d.values())) > 1: errors.append(f'field {k}: ' + ', '.join(f'{s} {o:#x}' for s, o in d.items()))
    # overlaps in the final map, per class with its bases
    final = {}
    for k, d in srcs.items():
        for s in ('pin', 'reflection', 'accessor', 'slot maps'):
            if s in d: final[k] = d[s]; break
    for k, o in pre.items(): final.setdefault(k, o)
    byclass = collections.defaultdict(list)
    for k, o in final.items():
        c, n = k.rsplit('.', 1)
        it = dict(lay.decl.get(c, [])).get(n)
        sa = lay.size_align(lay.ty(it)) if it else None
        if sa: byclass[c].append((o, o + sa[0], n))
    for c in byclass:
        chain, x = [], c
        while x and x in lay.tab: chain.append(x); x = lay.tab[x].parent
        fs = sorted((b, e, f'{cc.split(".")[-1]}.{n}') for cc in chain for b, e, n in byclass.get(cc, []))
        for (b0, e0, n0), (b1, e1, n1) in zip(fs, fs[1:]):
            if b1 < e0: errors.append(f'overlap in {c}: {n0} {b0:#x}..{e0:#x} and {n1} {b1:#x}..{e1:#x}')
    errors = sorted(set(errors))
    if '--no-loo' not in argv:
        known = lp.known_offsets(lay, ldb, acc)
        keys = [k for k in known if k[1] and k[0].startswith(('TowerFall.', 'Monocle.')) and not lp.BACKING.match(k[1]) and k[0] in lay.decl]
        right = wrong = 0
        for k in keys:
            kn = dict(known); del kn[k]
            p, _ = lp.predict(lay, kn)
            if k in p:
                if p[k] == known[k]: right += 1
                else:
                    wrong += 1
                    if f'{k[0]}.{k[1]}' not in EXPLAINED: errors.append(f'leave-one-out: {k[0]}.{k[1]} predicted {p[k]:#x}, known {known[k]:#x}')
        print(f'stateconflicts: leave-one-out {right} exact, {wrong} wrong ({len(EXPLAINED)} explained)')
    print(f'stateconflicts: {len(errors)} conflicts')
    for e in errors[:40]: print('  CONFLICT', e)
    return 1 if errors else 0

if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
