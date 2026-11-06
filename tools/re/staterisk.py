#!/usr/bin/env python3
"""staterisk.py: step C3 / rule 4 of docs/tfstate-rollback-measurement.md, the check before a run. Every field TF.State's generated
code reads or writes that isn't confirmed (a gap: no offset, a dummy at run time; or a pin that is only placed), with how the generated
code uses it, and a risk for each phase:
  GetState (reads):
    HIGH    a gap or an unconfirmed reference handed to game code (a call, an array or list walk), or a gap reference read at all:
            a stand-in or a wrong pointer there crashes (LightingLayer.spotlight did)
    MEDIUM  an unconfirmed reference only stored into the state (a wrong one gives wrong state; the field check flags it)
    LOW     a number read (a wrong one gives a wrong value, no crash)
  LoadState (writes): any unconfirmed field written is HIGH (it would corrupt game memory).
A GetState run starts only with no GetState HIGH item; LoadState only with no LoadState HIGH item (rule 4).

  staterisk.py <stateconfirm.json> [--load]   (exit 1 if a HIGH item for that run: GetState, or with --load LoadState)"""
import os, re, sys, json, glob, collections
here = os.path.dirname(os.path.abspath(__file__))
root = os.path.join(here, '..', '..')

def main(argv):
    st = json.load(open(argv[0]))
    h = open(os.path.join(root, 'hw-probe/exlaunch/source/program/tfex/gen/tfexgen_binding.hpp')).read()
    key_of = {}   # field -> binding key
    for m in re.finditer(r'constexpr uint32_t (f_\w+) = [^;]+;\s+/\* (?:UNRESOLVED )?([\w.+`]+\.\w+)', h): key_of.setdefault(m.group(2), m.group(1))
    src = {f: open(f).read().split('\n') for f in glob.glob(os.path.join(root, 'hw-probe/exlaunch/source/program/tfex/gen/tfexgen_*.cpp'))}
    rows = []
    for field, d in sorted(st.items()):
        if d['status'] == 'confirmed': continue
        k = key_of.get(field)
        if not k: continue
        uses = collections.Counter()
        for f, lines in src.items():
            for ln in lines:
                for m in re.finditer(r'gen::Fld<([^>]+(?:<[^>]*>)?)>\(([^;]*?)bind::' + re.escape(k) + r'\b\)', ln):
                    ty = m.group(1); after = ln[m.end():m.end() + 4]; before = ln[:m.start()]
                    ptr = ty.strip().endswith('*')
                    if re.match(r'\s*=[^=]', after): uses['write'] += 1
                    elif ptr and re.search(r'(GameArr|FromList|FromDict|FromArr|Fn<[^>]*>\([^)]*\))\(\s*$|\b(GameArr|FromList|FromDict|FromArr)<[^(]*\(\s*$', before): uses['to game code'] += 1
                    elif ptr: uses['reference read'] += 1
                    else: uses['number read'] += 1
        if not uses: continue
        gap = d['status'] == 'missing'
        get = 'HIGH' if uses['to game code'] or (gap and uses['reference read']) else 'MEDIUM' if uses['reference read'] else 'LOW' if uses['number read'] else '-'
        load = 'HIGH' if uses['write'] else '-'
        rows.append((get, load, field, d['status'], d['offset'], dict(uses)))
    order = {'HIGH': 0, 'MEDIUM': 1, 'LOW': 2, '-': 3}
    cg = collections.Counter(r[0] for r in rows); cl = collections.Counter(r[1] for r in rows)
    print(f'staterisk: {len(rows)} unconfirmed fields the generated code uses. GetState: {cg["HIGH"]} HIGH, {cg["MEDIUM"]} MEDIUM, '
          f'{cg["LOW"]} LOW; LoadState: {cl["HIGH"]} HIGH')
    for r in sorted(rows, key=lambda r: (order[r[0]], r[2])):
        if r[0] != 'LOW' and r[0] != '-': print(f'  GetState {r[0]:6s} {r[2]:45s} {r[3]:7s} {hex(r[4]) if r[4] is not None else "-":6s} {r[5]}')
    cnt = cg if '--load' not in argv else cl
    return 1 if cnt['HIGH'] else 0

if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
