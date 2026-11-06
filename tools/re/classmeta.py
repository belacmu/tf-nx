#!/usr/bin/env python3
"""classmeta.py: every method of a class in the Switch binary, from its reflection-metadata builder.

The BRUTE metadata builder of a class references the class's type-name token (UTF-16, e.g. "TowerFall.Icicle"), then, record by
record, each method's name token followed by its invoke wrapper (metaresolve.py). This walks the builder in order instead of looking
up one name: from the type-name reference to the next class's type-name reference, every adrp+add that lands on a standalone UTF-16
name is a member name, and the next adrp+add after it that lands in .text is that member's wrapper (classified DIRECT body / VIRTUAL
slot by metaresolve.classify_wrapper).

  classmeta.py TowerFall.Icicle [more types...]     -> one line per member: name, kind, body or slot
  import classmeta; classmeta.members("TowerFall.Icicle")

The adrp+add index of the whole .text is built once and cached in classmeta.idx (pickle) next to main.flat."""
import os, pickle, struct, sys
import metaresolve as mr

img, TEXT_END = mr.img, mr.TEXT_END
IDX = os.path.join(os.path.dirname(os.path.abspath(__file__)), "classmeta.idx")

def _build_index():
    """sites: sorted list of (offset, target) for every adrp+add pair (same register) in .text."""
    words = struct.unpack_from("<%dI" % (TEXT_END // 4), img, 0)
    out = []
    for i in range(len(words) - 1):
        w = words[i]
        if (w & 0x9F000000) != 0x90000000: continue
        w2 = words[i + 1]
        rd = w & 0x1F
        if (w2 & 0xFF800000) != 0x91000000 or ((w2 >> 5) & 0x1F) != rd: continue
        off = i * 4
        immlo = (w >> 29) & 3; immhi = (w >> 5) & 0x7FFFF; imm = (immhi << 2) | immlo
        if imm & (1 << 20): imm -= 1 << 21
        page = ((off & ~0xFFF) + (imm << 12)) & 0xFFFFFFFFFFFF
        shift = (w2 >> 22) & 1
        add = ((w2 >> 10) & 0xFFF) << (12 if shift else 0)
        out.append((off, page + add))
    return out

def index():
    if os.path.exists(IDX) and os.path.getmtime(IDX) > os.path.getmtime(os.path.join(os.path.dirname(IDX), "main.flat")):
        with open(IDX, "rb") as f: return pickle.load(f)
    sites = _build_index()
    with open(IDX, "wb") as f: pickle.dump(sites, f)
    return sites

_name_cache = {}
def utf16_name(addr):
    """The standalone UTF-16 identifier at addr (zero code unit before and after), or None."""
    if addr in _name_cache: return _name_cache[addr]
    r = None
    if 0x2863000 <= addr < len(img) - 2 and img[addr - 2:addr] == b"\x00\x00":
        end = addr
        while end < len(img) - 1 and img[end:end + 2] != b"\x00\x00" and end - addr < 400: end += 2
        if end > addr:
            try:
                s = img[addr:end].decode("utf-16-le")
                if s.isprintable() and all(c.isalnum() or c in "_.<>`$|,[]+-" for c in s): r = s
            except UnicodeDecodeError: pass
    _name_cache[addr] = r
    return r

def members(typename, sites=None):
    """[(name, kind, value, site)] for the class's builder(s): kind DIRECT (value = body), VIRTUAL (value = slot), or UNKNOWN."""
    sites = sites or index()
    tok = mr.token(typename)
    if tok is None: return None
    starts = [i for i, (o, t) in enumerate(sites) if t == tok]
    res = []
    for si in starts:
        i = si + 1
        while i < len(sites) and sites[i][0] - sites[si][0] < 0x20000:
            o, t = sites[i]
            n = utf16_name(t)
            if n and "." in n and n != typename and n[0].isupper() and n.split(".")[0] in ("TowerFall", "Monocle", "Microsoft", "System"):
                break                                  # the next class's builder
            if n and i + 1 < len(sites) and sites[i + 1][1] < TEXT_END and sites[i + 1][0] - o < 0x40:
                kind, val = mr.classify_wrapper(sites[i + 1][1])
                res.append((n, kind, val, o))
                i += 2
                continue
            i += 1
    return res

import relocs as _relocs
from capstone import Cs as _Cs, CS_ARCH_ARM64 as _A, CS_MODE_LITTLE_ENDIAN as _L
_md = _Cs(_A, _L); _md.skipdata = True
_R = None
_by_target = None

def _mem_operand(ops):
    """(base register, address offset) of a memory operand split on commas: "[x8" "#0x18]" (offset), "[x8" "#0x18]!" (pre-index:
    the access is at base + offset), "[x8]" "#8" (post-index: the access is at base), "[x8]"."""
    first = ops[0].strip()
    base = first.lstrip("[").rstrip("]!")
    if first.endswith("]") or len(ops) < 2 or not ops[1].strip().startswith("#"): return base, 0   # [xN] or [xN], #post
    return base, int(ops[1].strip().lstrip("#").rstrip("!").rstrip("]"), 0)

def records(typename, sites=None):
    """[(name, body, flags)] from the class builder's method-record arrays, by simulating it: the builder allocates arrays
    (`mov w0, #size; bl #0xe60`) and fills 0x60-byte method records: +0x00 name (UTF-16 token), +0x10 invoke wrapper, +0x18 the
    compiled body (adrp+add, or ldr from a slot that a RELATIVE relocation fills), +0x24 method attributes (0x1886 = .ctor)."""
    global _R
    if _R is None: _R = _relocs.relative()
    sites = sites or index()
    tok = mr.token(typename)
    if tok is None: return None
    out = []
    global _by_target
    if _by_target is None or _by_target[0] is not sites:
        bt = {}
        for i, (o, t) in enumerate(sites): bt.setdefault(t, []).append(i)
        _by_target = (sites, bt)
    for si in _by_target[1].get(tok, []):
        start = sites[si][0]
        # the builder ends at the next class's type-name reference
        end = start + 0x20000
        for o, t in sites[si + 1:]:
            if o - start > 0x20000: break
            n = utf16_name(t)
            if n and "." in n and n != typename and (n.split(".")[0] in ("TowerFall", "Monocle") or ("`" in n and "[" in n and not n.startswith(typename))) and "+" not in n.replace(typename + "+", ""):
                end = o; break
        reg, arrays, cur = {}, [], None
        for ins in _md.disasm(img[start:end], start):
            mn, ops = ins.mnemonic, [x.strip() for x in ins.op_str.split(",")]
            if mn == "adrp":
                reg[ops[0]] = int(ops[1].lstrip("#"), 16)
            elif mn == "add" and len(ops) == 3 and ops[2].startswith("#") and isinstance(reg.get(ops[1]), int) and ops[0].startswith("x"):
                reg[ops[0]] = reg[ops[1]] + int(ops[2].lstrip("#"), 0)
            elif mn == "ldr" and ops[0].startswith("x") and len(ops) >= 2 and ops[1].startswith("[x"):
                b, off = _mem_operand(ops[1:])
                reg[ops[0]] = _R.get(reg[b] + off) if isinstance(reg.get(b), int) and reg.get(b) is not True else None
                if reg[ops[0]] is not None: reg[ops[0]] &= 0x7fffffffffffffff
            elif mn in ("mov", "movz") and len(ops) == 2 and ops[1].startswith("#"):
                v = int(ops[1].lstrip("#"), 0); reg[ops[0]] = v; reg["x" + ops[0][1:]] = v; reg["w" + ops[0][1:]] = v
            elif mn == "mov" and len(ops) == 2 and ops[1] in reg:
                reg[ops[0]] = reg[ops[1]]
            elif mn == "bl":
                if int(ops[0].lstrip("#"), 16) == 0xe60:
                    cur = {}; arrays.append(cur)
                    reg.pop("x0", None); reg["arr"] = True
                else:
                    cur = None
                for r in ("x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7", "x8", "x9", "x10", "x11", "x12", "x13", "x14", "x15", "x16", "x17"):
                    if r != "x0" or cur is None: reg.pop(r, None)
            elif mn in ("str", "stp") and cur is not None and ins.op_str.find("[x0") >= 0:
                _, off = _mem_operand(ins.op_str[ins.op_str.index("["):].split(", "))
                srcs = ops[:2] if mn == "stp" else ops[:1]
                size = 8 if srcs[0].startswith("x") else 4
                for k, r in enumerate(srcs):
                    cur[off + k * size] = reg.get(r) if not r.endswith("zr") else 0
        for arr in arrays:
            n = max(arr) // 0x60 + 1 if arr else 0
            for k in range(n):
                name = utf16_name(arr.get(k * 0x60) or 0)
                body = arr.get(k * 0x60 + 0x18)
                if name and body and body < TEXT_END:
                    out.append((name, body, arr.get(k * 0x60 + 0x24)))
    if "`" in typename and out:   # a generic instance: its builder doesn't end at a TowerFall/Monocle name; the next instance's
        first = out[0][0]          # records follow and start with the same member name again
        for i in range(1, len(out)):
            if out[i][0] == first: out = out[:i]; break
    return out

if __name__ == "__main__":
    sites = index()
    for t in sys.argv[1:]:
        m = members(t, sites)
        if m is None: print(f"{t}: no type token"); continue
        print(f"== {t}: {len(m)} members")
        for n, k, v, o in m:
            print(f"  {n:40} {k:8} {('0x%x' % v) if v is not None else '-':>10}  @0x{o:x}")
