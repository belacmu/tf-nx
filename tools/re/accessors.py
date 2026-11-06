#!/usr/bin/env python3
"""accessors.py: step C. The Switch offsets of auto-property backing fields (<P>k__BackingField), read from the compiled accessors:
get_P / set_P (resolved by name and PC signature through tools/tfex/bind.py) are a single load or store at the field's offset.
The Switch build orders those backing fields differently from the PC IL (they sit among the class's first fields), so the layout
rule (layoutpredict.py) can't place them; these offsets anchor it.

  accessors.py [--out accessors.json]   -> {"Class.<P>k__BackingField": offset}, getter and setter agreeing where both are read"""
import os, re, sys, json, collections
here = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, here)
import layoutdb, rtypes, tfexrefs
from capstone import Cs, CS_ARCH_ARM64, CS_MODE_LITTLE_ENDIAN

def accessor_offset(img, body, store):
    """the offset of a plain accessor, else None: loads (getter) or stores (setter) of `this`'s fields at consecutive offsets, then
    `ret` (`ldr w0, [x0, #off]; ret`; a Vector2: `ldr s0, [x0, #off]; ldr s1, [x0, #off+4]; ret`)"""
    ins = list(Cs(CS_ARCH_ARM64, CS_MODE_LITTLE_ENDIAN).disasm(img[body:body + 24], body))
    offs = []
    for k, x in enumerate(ins):
        if x.mnemonic == 'ret': break
        pair = x.mnemonic in ('ldp', 'stp')
        m = re.match(r'([wxsdbhq])\d+, ' + (r'[wxsdq]\d+, ' if pair else '') + r'\[x0(?:, #(0x[0-9a-f]+|\d+))?\]$', x.op_str)
        if not m or not x.mnemonic.startswith('st' if store else 'ld'): return None
        o = int(m.group(2), 0) if m.group(2) else 0
        offs.append((o, m.group(1)))
        if pair: offs.append((o + {'w': 4, 's': 4, 'x': 8, 'd': 8, 'q': 16}[m.group(1)], m.group(1)))
    else: return None
    if not offs or k != len([x for x in ins[:k]]): return None
    w = {'w': 4, 's': 4, 'x': 8, 'd': 8, 'b': 1, 'h': 2, 'q': 16}
    for (o0, r0), (o1, r1) in zip(offs, offs[1:]):
        if o1 != o0 + w.get(r0, 99): return None
    return offs[0][0]

def main(argv):
    out = next((argv[i + 1] for i, a in enumerate(argv) if a == '--out'), os.path.join(here, 'accessors.json'))
    decl = layoutdb.il_fields(os.path.join(here, '..', '..', '.cache', 'towerfall-pc.il'))
    img = rtypes._img(); got = collections.defaultdict(dict); nb = 0
    for c, fs in decl.items():
        if not c.startswith(('TowerFall.', 'Monocle.')): continue
        recs = tfexrefs.records(c)
        for n, ty in fs:
            m = re.match(r'<(\w+)>k__BackingField$', n)
            if not m: continue
            nb += 1
            for mem, body, fl in recs:
                if mem in ('get_' + m.group(1), 'set_' + m.group(1)):
                    o = accessor_offset(img, body, mem.startswith('set_'))
                    if o is not None: got[f'{c}.{n}'].setdefault(mem[:3], set()).add(o)
    res, clash = {}, []
    for k, v in got.items():
        allo = set().union(*v.values())
        if len(allo) == 1: res[k] = next(iter(allo))
        else: clash.append((k, v))
    json.dump(res, open(out, 'w'), indent=1, sort_keys=True)
    print(f'accessors: {len(res)} of {nb} backing fields placed ({sum(1 for v in got.values() if len(v) == 2)} by getter and setter agreeing), '
          f'{len(clash)} getter/setter disagreeing {clash[:5]} -> {out}')

if __name__ == '__main__':
    main(sys.argv[1:])
