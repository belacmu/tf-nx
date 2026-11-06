#!/usr/bin/env python3
"""statescore.py: the markers of docs/tfstate-rollback-measurement.md. Of the private fields TF.State's units read (the f_ bindings of
tfexgen_binding.hpp that reflection doesn't give), how many have an offset from a confirmed pin (translate.toml) or from the layout
databases; split into versus and quest / trials / Dark World classes; and any database offset that disagrees with a confirmed pin or
with an earlier lead (tools/tfex/state-pins.toml).

  statescore.py [layoutdb.json]            phase 1 / step A: reference fields, layoutdb.json only
  statescore.py --all                      step C: every field; layoutdb.json + accessors.json + layoutpredict.json"""
import json, re, sys, os
here = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, here)
import layoutdb, rtypes
QUEST = ('Dummy', 'EnemyAttack', 'QuestSpawnPortal', 'TechnoMage', 'TrialsControl', 'ChaliceGhost')   # not in versus netplay
ALL = '--all' in sys.argv
args = [a for a in sys.argv[1:] if not a.startswith('--')]
ldb = json.load(open(args[0] if args else os.path.join(here, 'layoutdb.json')))['placed']
if ALL:
    for k, o in json.load(open(os.path.join(here, 'accessors.json'))).items(): ldb.setdefault(k, o)
    for k, o in json.load(open(os.path.join(here, 'layoutpredict.json')))['placed'].items(): ldb.setdefault(k, o)
root = os.path.join(here, '..', '..')
decl = layoutdb.il_fields(os.path.join(root, '.cache', 'towerfall-pc.il'))
h = open(os.path.join(root, 'hw-probe/exlaunch/source/program/tfex/gen/tfexgen_binding.hpp')).read()
used = set(re.findall(r'constexpr uint32_t f_\w+ = [^;]+;\s+/\* (?:UNRESOLVED )?([\w.+`]+\.\w+)', h))
tab = rtypes.table()
refl = {f'{c}.{n}' for c, t in tab.items() for n, o, a, ty in (t.fields or []) if o is not None}
pins = lambda f: {f'{m.group(1)}.{m.group(2)}': int(m.group(3), 16) for m in
                  re.finditer(r'\[pin\."([\w.+`]+)\.(\w+)\(\)"\]\s*off = (0x[0-9a-f]+)', open(os.path.join(root, 'tools/tfex', f)).read())}
conf, leads = pins('translate.toml'), pins('state-pins.toml')
ref = sorted(k for k in used if k not in refl and (dict(decl.get(k.rsplit('.', 1)[0], [])).get(k.rsplit('.', 1)[1]) is not None if ALL else
             layoutdb.runtime_name(dict(decl.get(k.rsplit('.', 1)[0], [])).get(k.rsplit('.', 1)[1]) or '')))
quest = lambda k: k.rsplit('.', 1)[0].split('.')[-1].split('+')[0] in QUEST
for name, part in (('versus', [k for k in ref if not quest(k)]), ('quest / trials / Dark World', [k for k in ref if quest(k)])):
    have = [k for k in part if k in conf or k in ldb]
    print(f'{name}: {len(have)} of {len(part)} {"fields" if ALL else "reference fields"} have an offset ({100 * len(have) // max(1, len(part))}%)')
    if name == 'versus': print('  missing:', ' '.join(k.replace('TowerFall.', '') for k in part if k not in have))
print('layoutdb vs confirmed pins, differ:', [k for k in ldb if k in conf and conf[k] != ldb[k]])
dl = [(k, hex(leads[k]), hex(ldb[k])) for k in ldb if k in leads and leads[k] != ldb[k]]
print(f'layoutdb vs earlier leads: {sum(1 for k in ldb if k in leads and leads[k] == ldb[k])} agree, {len(dl)} differ', dl)
