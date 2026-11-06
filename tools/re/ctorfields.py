#!/usr/bin/env python3
"""ctorfields.py: Switch offsets of a class's fields from its constructors: the PC IL's stfld sequence (towerfall-pc.il) lined up with
the Switch constructor's stores through `this` (classmeta via sigs.paired), anchored on fields whose offset is known (reflection records,
translate.toml [pin]); between two anchors, equal counts of stores pair one to one, in order, if the store widths agree with the PC types.

A strong lead (the Player's counters: jumpBufferCounter 0x1a8 as hotcoals.cpp has it), still a lead: confirm before a [pin].

  ctorfields.py TowerFall.Player            -> field offsets with their evidence
  import ctorfields; ctorfields.solve("TowerFall.Player", known) -> {field: offset}"""
import os, re, sys
import rtypes, sigs
from capstone import Cs, CS_ARCH_ARM64, CS_MODE_LITTLE_ENDIAN
_md = Cs(CS_ARCH_ARM64, CS_MODE_LITTLE_ENDIAN)
_img = None
W = {'bool': 1, 'uint8': 1, 'int8': 1, 'char': 2, 'int16': 2, 'uint16': 2, 'int32': 4, 'uint32': 4, 'float32': 4, 'int64': 8, 'uint64': 8, 'float64': 8}

def _il_ctors(cls):
    """[(field, type) per stfld] of each .ctor of cls in the PC IL, in order"""
    il = open(sigs.PC_IL, encoding='utf-8', errors='replace').read()
    short = cls.split('.')[-1].split('+')[-1]
    m = re.search(r'\.class [^\n]*?\b' + re.escape(cls.split('+')[-1]) + r'\b[^\n]*\n', il)
    if not m: return []
    end = il.find('} // end of class ' + cls.split('+')[-1], m.end())
    body = il[m.end():end if end > 0 else len(il)]
    out = []
    for cm in re.finditer(r'\.method [^\n]*?specialname rtspecialname[^\n]*?\n?\s*instance void\s+\.ctor', body):
        e = body.find('} // end of method ' + short + '::.ctor', cm.end())
        seq, made = [], None
        for line in body[cm.end():e].splitlines():
            nm = re.search(r'newobj\s+instance void\s+(?:class\s+)?(?:\[[^\]]+\])?([\w.`/]+?)(?:<[^>]*>)?::\.ctor', line)
            if nm: made = nm.group(1).replace('/', '+')
            sm = re.search(r'stfld\s+(.*?)\s+[\w.`/<>\[\]]*?' + re.escape(short) + r'::(\w+)', line)
            if sm: seq.append((sm.group(2), sm.group(1), made)); made = None
        out.append(seq)
    return out

def _switch_stores(addr):
    global _img
    if _img is None: _img = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'main.flat'), 'rb').read()
    this, out, called = None, [], None
    for ins in _md.disasm(_img[addr:addr + 0x6000], addr):
        if this is None and ins.mnemonic == 'mov' and re.match(r'x\d+, x0$', ins.op_str): this = ins.op_str.split(',')[0]
        if ins.mnemonic == 'ret': break
        if ins.mnemonic == 'bl': called = int(ins.op_str.lstrip('#'), 16)
        m = re.match(r'([xwsdqb]\w*|[xw]zr), \[(x\d+), #(0x[0-9a-f]+)\]$', ins.op_str)
        if m and this and m.group(2) == this and ins.mnemonic in ('str', 'strb', 'strh', 'stur'):
            r = m.group(1)
            w = 1 if ins.mnemonic == 'strb' else 2 if ins.mnemonic == 'strh' else 8 if r[0] in 'xd' or r == 'xzr' else 4 if r[0] in 'ws' or r == 'wzr' else 16
            out.append((int(m.group(3), 16), w, called)); called = None
    return out

_ctor_of = {}
_generic_ctors = None
def _generic_ctor(addr):
    """System.Collections.Generic.List`1 / Dictionary`2 ... for a constructor body the translator's binding knows (generic instantiations
    have no paired record): tools/tfex/binding.json m_<Type>_1_<hash>__ctor_<n>"""
    global _generic_ctors
    if _generic_ctors is None:
        import json
        _generic_ctors = {}
        bj = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'tfex', 'binding.json')
        for k, v in (json.load(open(bj)).items() if os.path.exists(bj) else []):
            m = re.match(r'm_(List|Dictionary|HashSet|Stack|Queue)_(\d)_\w+__ctor', k)
            if m and isinstance(v, dict) and isinstance(v.get('body'), int): _generic_ctors[v['body']] = f'System.Collections.Generic.{m.group(1)}`{m.group(2)}'
    return _generic_ctors.get(addr)
def _ctor_type(addr, types):
    """the type (of `types`) whose constructor is at addr, or None"""
    g = _generic_ctor(addr)
    if g and g in types: return g
    for t in types:
        if t not in _ctor_of:
            try: _ctor_of[t] = {a for name, ov, a, *rest in (sigs.paired(t) or []) if str(name) == '.ctor' and a}
            except Exception: _ctor_of[t] = set()
        if addr in _ctor_of[t]: return t
    return None

def _width(ty):
    ty = ty.strip()
    if ty in W: return W[ty]
    if ty.startswith('valuetype'): return None
    return 8

def _align(pcd, sw, known):
    """Needleman-Wunsch over the two store sequences: a PC field pairs with a Switch store of its width (a known field only with its
    own offset); either side may skip (the Switch's own fields, PC stores BRUTE folded away). Returns [(field, offset)]."""
    n, m = len(pcd), len(sw)
    NEG = -10**9
    S = [[NEG] * (m + 1) for _ in range(n + 1)]; B = [[None] * (m + 1) for _ in range(n + 1)]
    S[0][0] = 0
    for i in range(n + 1):
        for j in range(m + 1):
            if S[i][j] == NEG: continue
            if i < n and S[i][j] - 1 > S[i + 1][j]: S[i + 1][j] = S[i][j] - 1; B[i + 1][j] = (i, j, None)
            if j < m and S[i][j] - 1 > S[i][j + 1]: S[i][j + 1] = S[i][j] - 1; B[i][j + 1] = (i, j, None)
            if i < n and j < m:
                f, pw, pt = pcd[i]; o, w, st = sw[j]
                if f in known: sc = 50 if known[f] == o else NEG
                elif pt and st: sc = 20 if pt == st else NEG   # both made by a constructor: the same type
                else: sc = 3 if (pw is None or pw == w) else NEG
                if sc > NEG and S[i][j] + sc > S[i + 1][j + 1]: S[i + 1][j + 1] = S[i][j] + sc; B[i + 1][j + 1] = (i, j, (f, o))
    out, i, j = [], n, m
    while (i, j) != (0, 0) and B[i][j]:
        pi, pj, pair = B[i][j]
        if pair: out.append(pair)
        i, j = pi, pj
    return out[::-1]

def solve(cls, known):
    """{field: offset} from the constructors, given known {field: offset}: only pairs that every constructor alignment agrees on"""
    votes = {}
    pcs = _il_ctors(cls)
    sws = [a for name, ov, a, *rest in (sigs.paired(cls) or []) if str(name) == '.ctor' and a]
    tab = rtypes.table()
    base = tab[cls].parent if cls in tab else None
    base_size = tab[base].size if base in tab and tab[base].size else 0   # stores below it are the base class's fields
    for pc in pcs:
        pcd = []
        types = {mt for f, t, mt in pc if mt}
        for f, t, mt in pc:
            if f not in [x[0] for x in pcd]: pcd.append((f, _width(t), mt))
        for a in sws:
            sw = []
            for off, w, called in _switch_stores(a):
                if off >= base_size and off not in [x[0] for x in sw]: sw.append((off, w, _ctor_type(called, types) if called else None))
            if not any(f in known for f, _, _ in pcd): continue
            for f, o in _align(pcd, sw, known):
                if f not in known: votes.setdefault(f, set()).add(o)
    taken = {}
    for f, os_ in votes.items():
        if len(os_) == 1: taken[f] = next(iter(os_))
    # an offset two fields claim: neither
    counts = {}
    for o in taken.values(): counts[o] = counts.get(o, 0) + 1
    return {f: o for f, o in taken.items() if counts[o] == 1 and o not in known.values()}

if __name__ == '__main__':
    cls = sys.argv[1]
    t = rtypes.table().get(cls)
    known = {n: o for n, o, a, ty in (t.fields or [])} if t else {}
    for f, o in sorted(solve(cls, known).items(), key=lambda x: x[1]): print(f'{f:30s} {hex(o)}')
