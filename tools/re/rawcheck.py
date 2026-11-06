#!/usr/bin/env python3
"""rawcheck.py: step B's marker (docs/tfstate-rollback-measurement.md). The raw dump (hw.txt "staterawdump <n>", tfexgen_state.cpp
RawDump) writes sd:/xplay/raw/<Class>.bin: records [u32 'TFRW', u32 tick, u32 size, u32 address low] + the object's bytes. Checks
each file parses to the end, each record's size equals the class's instance size (its type record, rtypes.py), and, given that run's
slot maps, that every class with a slot map has a file.

  rawcheck.py <raw dir> [slot-map file]"""
import os, re, sys, struct, collections
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rtypes

def records(path):
    """[(tick, size, addr, bytes)], and the bytes left over that don't parse (0 when complete)"""
    b = open(path, 'rb').read(); out = []; at = 0
    while at + 16 <= len(b):
        magic, tick, size, addr = struct.unpack_from('<4I', b, at)
        if magic != 0x57524654 or at + 16 + size > len(b): break
        out.append((tick, size, addr, b[at + 16:at + 16 + size])); at += 16 + size
    return out, len(b) - at

def sizes():
    """{file stem: {instance sizes of the classes with that short name}} for every class rtypes knows; 0 = a generic definition (each
    instantiation has its own record and size: not checkable here)"""
    out = collections.defaultdict(set)
    for c, t in rtypes.table().items():
        stem = re.sub(r'[^A-Za-z0-9_]', '_', re.split(r'[.+]', c)[-1])
        if isinstance(t.size, int): out[stem].add(t.size)
    return out

def main(argv):
    d = argv[0]; known = sizes()
    files = sorted(f for f in os.listdir(d) if f.endswith('.bin'))
    bad, nrec, unknown, generic, ticks = [], 0, [], [], {}
    for f in files:
        recs, left = records(os.path.join(d, f)); nrec += len(recs); stem = f[:-4]
        if left: bad.append(f'{stem}: {left} bytes past the last whole record')
        if stem not in known: unknown.append(stem); continue
        ok = known[stem]
        wrong = sorted({r[1] for r in recs if r[1] not in ok}) if 0 not in ok else []
        if wrong: bad.append(f'{stem}: record sizes {[hex(w) for w in wrong]}, the class is {[hex(x) for x in sorted(ok)]}')
        if 0 in ok: generic.append(stem)
        if len(ok) == 1 and 0 not in ok and len({r[3][:8] for r in recs}) > 1:   # (generics / two classes of one name: several)
            bad.append(f'{stem}: records with different type descriptors')
        ticks[stem] = len({r[0] for r in recs})
    print(f'rawcheck: {len(files)} class files, {nrec} records; {len(bad)} wrong, {len(unknown)} classes rtypes has no size for, '
          f'{len(generic)} generic (size not checkable); classes over 2+ ticks: {sum(1 for n in ticks.values() if n > 1)}')
    for x in bad: print('  WRONG', x)
    if unknown: print('  no size known:', ' '.join(unknown))
    if len(argv) > 1:
        mapped = {re.sub(r'[^A-Za-z0-9_]', '_', m.group(1)) for m in re.finditer(r'\[state\] slots ([\w`+<>]+):', open(argv[1]).read())}
        tab = rtypes.table()
        values = {re.sub(r'[^A-Za-z0-9_]', '_', re.split(r'[.+]', c)[-1]) for c, t in tab.items() if isinstance(t.size, int) and t.size < 0x10}
        missing = sorted(m for m in mapped - {f[:-4] for f in files} if m not in values)   # (boxed values: Int32, enums)
        print(f'classes with a slot map: {len(mapped)}; without a raw file: {len(missing)}', ' '.join(missing[:40]))
    return not bad

if __name__ == '__main__':
    sys.exit(0 if main(sys.argv[1:]) else 1)
