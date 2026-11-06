#!/usr/bin/env python3
"""stateconfirm.py: step C2 of docs/tfstate-rollback-measurement.md. For each private field TF.State reads (versus, then quest /
trials / Dark World), its offset in the final map and the evidence for it:
  compiled code, confirmed alone: a translate.toml [pin], reflection, the compiled accessor (accessors.json);
  independent sources, two agreeing confirm:
    slots  the runtime slot maps (layoutdb.json: an object of the field's type was there)
    bytes  the raw dump: plausible in every record of its class and not zero in at least one (rawverify.py's kinds)
    rule   the layout rule with this field's own offset left out (layoutpredict.py, leave-one-out)
    uses   the methods that use the field (privfield.py's scan of the Switch bodies), with every other placed field's bytes
           excluded: the one offset most using methods touch, with the field's width
(The constructors' stores, ctorfields.py, were tried and dropped: they misaligned 3 times in about 45 (Player.graceLedgeDir on
baseHeadScale, Player.flapGravity on wallStickMax, Scene.tagsToRemove), where the slot maps, the raw bytes and the rule agree.)
Fails (exit 1) if two sources disagree.

  stateconfirm.py <raw dir> [--list] [--json out]   (--json: {field: {offset, status, sources}} for staterisk.py)"""
import os, re, sys, json, collections
here = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, here)
import rtypes, layoutpredict as lp, rawverify as rv, rawcheck, privfield
root = os.path.join(here, '..', '..')
# (reported apart; in scope since C3: TF.EX's lobby takes Trials, its DARK PORTALS scenario brings portals and enemies into versus)
QUEST = ('Dummy', 'EnemyAttack', 'QuestSpawnPortal', 'TechnoMage', 'TrialsControl', 'ChaliceGhost')
# disagreements with a reason: the weaker source is wrong (a new disagreement fails the check)
EXPLAINED = {
    'TowerFall.PauseMenu.menuType': 'uses: 0xc4 is controllerDisconnected (not placed, so not excluded); the pin is confirmed in Update',
    'TowerFall.PauseMenu.optionIndex': 'uses: 0xc4 is controllerDisconnected (not placed, so not excluded); the pin is confirmed in Update',
}

def main(argv):
    lay = lp.Layout(os.path.join(root, '.cache', 'towerfall-pc.il'))
    ldb = json.load(open(os.path.join(here, 'layoutdb.json')))['placed']
    acc = json.load(open(os.path.join(here, 'accessors.json')))
    pre = json.load(open(os.path.join(here, 'layoutpredict.json')))['placed']
    tt = open(os.path.join(root, 'tools/tfex/translate.toml')).read()
    pins = {f'{m.group(1)}.{m.group(2)}': int(m.group(3), 16) for m in re.finditer(r'\[pin\."([\w.+`]+)\.(\w+)\(\)"\]\s*off = (0x[0-9a-f]+)', tt)}
    tab = rtypes.table()
    refl = {f'{c}.{n}': o for c, t in tab.items() for n, o, a, ty in (t.fields or []) if n and o is not None and not ((a or 0) & rtypes.STATIC)}
    h = open(os.path.join(root, 'hw-probe/exlaunch/source/program/tfex/gen/tfexgen_binding.hpp')).read()
    used = set(re.findall(r'constexpr uint32_t f_\w+ = [^;]+;\s+/\* (?:UNRESOLVED )?([\w.+`]+\.\w+)', h))
    fields = sorted(k for k in used if k not in refl and dict(lay.decl.get(k.rsplit('.', 1)[0], [])).get(k.rsplit('.', 1)[1]) is not None)
    # raw records per class (with its subclasses' records)
    recs = collections.defaultdict(list)
    stems = {}
    for c in lay.tab: stems.setdefault(re.sub(r'[^A-Za-z0-9_]', '_', re.split(r'[.+]', c)[-1]), c)
    for f in os.listdir(argv[0]):
        if f[:-4] in stems:
            rs, _ = rawcheck.records(os.path.join(argv[0], f)); c = stems[f[:-4]]
            while c and c in lay.tab: recs[c] += [r[3] for r in rs]; c = lay.tab[c].parent
    known = lp.known_offsets(lay, ldb, acc)
    finalmap = {}   # the final map: the strongest source of each field, then the rule
    for src in (pins, refl, acc, ldb):
        for k, o in src.items(): finalmap.setdefault(k, o)
    for k, o in pre.items(): finalmap.setdefault(k, o)
    def held(c, skip):
        """byte ranges of every placed field of c and its bases but `skip`"""
        out, x = [], c
        while x and x in lay.tab:
            for n, it in lay.decl.get(x, []):
                o = finalmap.get(f'{x}.{n}')
                sa = lay.size_align(lay.ty(it))
                if o is not None and sa and f'{x}.{n}' != skip: out.append((o, o + sa[0]))
            x = lay.tab[x].parent
        return out
    def uses(c, n):
        if privfield._uses is None: privfield._scan_il()
        ty = lay.tab.get(c); size = ty.size if ty and isinstance(ty.size, int) else 1 << 16
        want = privfield._want_width(privfield._ftypes.get((c, n), '?'))
        ex = held(c, f'{c}.{n}')
        votes, used = collections.Counter(), 0
        for (cc, m, p) in privfield._uses.get((c, n), {}):
            if cc != c: continue
            body, mis = privfield._body_of(c, m, p)
            if not body: continue
            used += 1
            for o, ws in privfield._this_accesses(body).items():
                if 0x10 <= o < size and not any(b <= o < e for b, e in ex) and (want is None or ws & want): votes[o] += 1
        if re.search(r'Vector2|Point\b', privfield._ftypes.get((c, n), '')):   # a pair of floats: counted by its X (a method may touch only Y)
            for o in sorted(votes):
                if o - 4 in votes: votes[o - 4] = max(votes[o - 4], votes[o]); del votes[o]
        top = votes.most_common()
        if not top or (len(top) > 1 and top[1][1] == top[0][1]) or top[0][1] < max(1, 0.6 * used) or (used > 1 and top[0][1] < 2): return None
        return top[0][0]
    rows, errors = [], []
    for k in fields:
        c, n = k.rsplit('.', 1)
        ev = {}
        if k in pins: ev['pin'] = pins[k]
        if k in acc: ev['accessor'] = acc[k]
        if k in ldb: ev['slots'] = ldb[k]
        kn = dict(known); kn.pop((c, n), None)
        p, _ = lp.predict(lay, kn)
        if (c, n) in p: ev['rule'] = p[(c, n)]
        uo = uses(c, n)
        if uo is not None: ev['uses'] = uo
        final = next((ev[s] for s in ('pin', 'accessor', 'slots') if s in ev), pre.get(k))
        it = dict(lay.decl[c])[n]; ks = rv.kinds(lay, it)
        if final is not None and recs.get(c) and ks:
            vals = [r[final + ro:final + ro + sz] for r in recs[c] for ro, kd, sz in ks if final + ro + sz <= len(r)]
            plaus = all(final + ro + sz <= len(r) and rv.plausible(kd, r[final + ro:final + ro + sz]) for r in recs[c] for ro, kd, sz in ks)
            if plaus and any(any(v) for v in vals): ev['bytes'] = final
            elif not plaus: errors.append(f'{k}: implausible bytes at {final:#x}')
        offs = set(ev.values())
        if len(offs) > 1 and k not in EXPLAINED: errors.append(f'{k}: ' + ', '.join(f'{s} {o:#x}' for s, o in ev.items()))
        compiled = any(s in ev for s in ('pin', 'accessor'))
        indep = [s for s in ('slots', 'bytes', 'rule', 'uses') if s in ev and ev[s] == final]
        status = 'confirmed' if final is not None and (compiled or len(indep) >= 2) else 'placed' if final is not None else 'missing'
        rows.append((k, final, status, sorted(ev)))
    for scope, sel in (('versus', lambda k: k.rsplit('.', 1)[0].split('.')[-1].split('+')[0] not in QUEST),
                       ('quest / trials / Dark World', lambda k: k.rsplit('.', 1)[0].split('.')[-1].split('+')[0] in QUEST)):
        part = [r for r in rows if sel(r[0])]
        cnt = collections.Counter(r[2] for r in part)
        print(f'{scope}: {len(part)} fields; confirmed {cnt["confirmed"]} ({100 * cnt["confirmed"] // max(1, len(part))}%), '
              f'placed only {cnt["placed"]}, missing {cnt["missing"]}')
        if scope == 'versus':
            ev = collections.Counter('+'.join(r[3]) for r in part if r[2] == 'confirmed')
            print('  confirmed by:', ', '.join(f'{k} {v}' for k, v in ev.most_common()))
            if '--list' in argv:
                for r in part:
                    if r[2] != 'confirmed': print(f'  {r[2]:8s} {r[0]} {hex(r[1]) if r[1] is not None else "-"} ({"+".join(r[3]) or "no source"})')
    jo = next((argv[i + 1] for i, a in enumerate(argv) if a == '--json'), None)
    if jo: json.dump({r[0]: {'offset': r[1], 'status': r[2], 'sources': r[3]} for r in rows}, open(jo, 'w'), indent=1, sort_keys=True)
    print(f'stateconfirm: {len(errors)} disagreements')
    for e in errors: print('  DISAGREE', e)
    return 1 if errors else 0

if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
