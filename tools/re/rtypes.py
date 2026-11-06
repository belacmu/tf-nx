#!/usr/bin/env python3
"""rtypes.py: every managed type of the Switch binary, with its runtime type record and its fields (name, offset, field type).

BRUTE builds each type's runtime record ("RuntimeType", 0x168 bytes) lazily, in a per-type builder that ends with `bl +0x2460`
(MakeType). MakeType copies its register and stack arguments into the record:
    x0  the record (allocated by the builder, `mov w0,#0x168; bl +0xe50`)
    w1  -> +0x18 (type kind: 0x13 class, 0x1f generic instance, ...)
    x2  -> +0x20 name, x3 -> +0x28 namespace, x4 -> +0x30 full name, x5 -> +0x38 assembly-qualified name (UTF-16, rodata)
    w6  -> +0x0c hash, x7 -> +0x88 the base type's getter (a function returning the parent's record; 0x2580 walks it for `is`)
    [sp+0x60] -> +0x00 instance size      [sp+0x68] -> +0x60 descriptor (.data; obj[0] = descriptor; desc[0] = record, desc[1] = vtable)
    [sp+0x70] -> +0x14 attributes (bit 5: interface)  [sp+0x78] -> +0x08 flags (bit 3: holds references -> GC_malloc, scanned;
               otherwise GC_malloc_atomic + memset; bit 5: has a finalizer)
    [sp+0x28] -> +0xd8 interface getters, [sp+0x30] -> +0xe0 their count
    [sp+0xd8] -> +0x70 the class-constructor runner (`if (!flag) RunCctor(flag, .cctor)`; 0 = none). BRUTE checks beforefieldinit
               at the call sites, not in the callee: code of ours that calls a game type's static method or reads its static fields
               must call this first (Alarm.Create reads Alarm.cached, null until Alarm's .cctor ran).
    [sp+0x80] -> +0xe8 the FIELD builder, [sp+0x88] -> +0xfa field count
    [sp+0xb0] -> +0x130 the method builder, [sp+0xb8] -> +0x140 method count
The field builder allocates one array (`mov w0,#n*0x40; bl +0xe60`) of 0x40-byte field records: +0x00 name, +0x08 the field type's
getter (ldr from a slot a RELATIVE reloc fills), +0x10 byte offset (instance fields; statics have their own slot), +0x20 attributes
(0x10 = static). Each type getter is `guard; bl <builder>; return [record slot]`, so getter -> builder -> name gives field types and
base types.

  rtypes.py Monocle.Entity [more]     size, flags, descriptor, vtable, base chain and every field
  rtypes.py --all > types.txt         every type
  import rtypes as T:    T.table() -> {fullname: Type}; T.fields("Monocle.Entity", inherited=True)

Cached in rtypes.idx next to main.flat."""
import os, pickle, struct, sys
from capstone import Cs, CS_ARCH_ARM64, CS_MODE_LITTLE_ENDIAN
import relocs

_here = os.path.dirname(os.path.abspath(__file__))
FLAT = os.path.join(_here, "main.flat")
IDX = os.path.join(_here, "rtypes.idx")
TEXT_END = 0x28626c0
MAKE_TYPE, ALLOC_ARRAY = 0x2460, 0xe60
img = None
_md = Cs(CS_ARCH_ARM64, CS_MODE_LITTLE_ENDIAN); _md.skipdata = True

def _img():
    global img
    if img is None: img = open(FLAT, "rb").read()
    return img

def ustr(addr):
    """The UTF-16 string at addr (stops at a zero code unit), or None."""
    im = _img()
    if not addr or not (0x2863000 <= addr < len(im)): return None
    e = addr
    while e < len(im) - 1 and im[e:e + 2] != b"\0\0" and e - addr < 1000: e += 2
    try: return im[addr:e].decode("utf-16-le")
    except UnicodeDecodeError: return None

class Type:
    __slots__ = ("name", "builder", "size", "flags", "attrs", "kind", "desc", "vtable", "parent_getter", "field_builder",
                 "nfields", "fields", "parent", "getter", "record_slot", "cctor")
    def __init__(self, **k):
        for s in self.__slots__: setattr(self, s, k.get(s))
    def __repr__(self): return f"<{self.name} size {self.size:#x}>"
Type.__module__ = "rtypes"          # the cache pickles it; it must load whether this file runs as a script or a module
sys.modules.setdefault("rtypes", sys.modules[__name__])

def _sim(start, end, R, stop_at_bl=None, track_x0_array=False):
    """A tiny forward simulation of [start, end): register values from adrp/add/mov/movk/orr and reloc-filled ldr;
    stores to [sp,#] (and, with track_x0_array, to the array returned in x0 by the last `bl ALLOC_ARRAY`).
    Returns (regs at the end, stack stores, arrays)."""
    im = _img()
    reg, stack, arrays, cur = {}, {}, [], None
    def val(r):
        if r in ("xzr", "wzr"): return 0
        return reg.get("x" + r[1:] if r[0] == "w" else r)
    def setr(r, v):
        reg["x" + r[1:] if r[0] == "w" else r] = v
    for ins in _md.disasm(im[start:end], start):
        mn = ins.mnemonic
        ops = [x.strip() for x in ins.op_str.split(",")]
        try:
            if mn == "adrp":
                setr(ops[0], int(ops[1].lstrip("#"), 16))
            elif mn == "add" and len(ops) == 3 and ops[2].startswith("#") and ops[1] != "sp":
                b = val(ops[1]); setr(ops[0], None if b is None else b + int(ops[2].lstrip("#"), 0))
            elif mn in ("mov", "movz") and len(ops) == 2:
                if ops[1].startswith("#"): setr(ops[0], int(ops[1].lstrip("#"), 0) & (0xffffffff if ops[0][0] == "w" else ~0))
                elif ops[1] != "sp": setr(ops[0], val(ops[1]))
            elif mn == "movk" and len(ops) >= 2:
                v = int(ops[1].lstrip("#"), 0); sh = int(ops[3].lstrip("#"), 0) if len(ops) > 3 else 0
                o = val(ops[0]) or 0
                setr(ops[0], (o & ~(0xffff << sh)) | (v << sh))
            elif mn == "orr" and len(ops) == 3 and ops[1] in ("wzr", "xzr") and ops[2].startswith("#"):
                setr(ops[0], int(ops[2].lstrip("#"), 0))
            elif mn == "ldr" and ops[0][0] == "x" and len(ops) >= 2 and ops[1].startswith("[") and not ops[1].startswith("[sp"):
                b = val(ops[1].lstrip("[").rstrip("]"))
                off = int(ops[2].rstrip("]!").lstrip("#"), 0) if len(ops) > 2 and ops[2].startswith("#") else 0
                setr(ops[0], R.get(b + off, ("slot", b + off)) if isinstance(b, int) else None)
            elif mn == "bl":
                t = int(ops[0].lstrip("#"), 16)
                if t == stop_at_bl: return reg, stack, arrays, ins.address
                if track_x0_array and t == ALLOC_ARRAY:
                    cur = {}; arrays.append(cur)
                    for r in list(reg): reg.pop(r)
                    reg["x0"] = "arr"
                    continue
                for r in ["x%d" % i for i in range(19)]: reg.pop(r, None)
            elif mn in ("str", "stp", "strh", "strb") and ops[-1 if mn != "stp" else 2].startswith("[") or (mn in ("str", "stp", "strh", "strb") and "[" in ins.op_str):
                m = ins.op_str[ins.op_str.index("["):]
                base = m[1:].split(",")[0].rstrip("]!")
                off = int(m.split("#")[1].split("]")[0], 0) if "#" in m else 0
                srcs = ops[:2] if mn == "stp" else ops[:1]
                size = 8 if srcs[0][0] == "x" else 4
                for k, r in enumerate(srcs):
                    v = val(r)
                    if base == "sp": stack[off + k * size] = v
                    elif track_x0_array and cur is not None and reg.get(base) == "arr": cur[off + k * size] = v
            elif mn == "ret":
                break
        except (ValueError, IndexError):
            pass
    return reg, stack, arrays, None

def _func_start(site):
    """The function containing a call site: the nearest bl target at or before it (builders are called by their getters),
    else back to the previous ret / udf padding."""
    import bisect
    if _entries:
        i = bisect.bisect_right(_entries, site) - 1
        if i >= 0 and site - _entries[i] < 0x2000: return _entries[i]
    im = _img(); o = site
    while o > 0:
        w = struct.unpack_from("<I", im, o - 4)[0]
        if w == 0xd65f03c0 or w == 0: return o   # ret / udf #0
        o -= 4
        if site - o > 0x4000: return o
    return o

_entries = None
def _bl_sites(target):
    """Call sites of target; also collects every bl target (function entries, for _func_start)."""
    global _entries
    im = _img()
    words = struct.unpack_from("<%dI" % (TEXT_END // 4), im, 0)
    out, ent = [], set()
    for i, w in enumerate(words):
        if (w >> 26) == 0x25:
            imm = w & 0x3ffffff
            if imm & (1 << 25): imm -= 1 << 26
            t = i * 4 + imm * 4
            ent.add(t)
            if t == target: out.append(i * 4)
    _entries = sorted(ent)
    return out

def _getter_builder(fn, R):
    """A type getter -> (builder it calls, record slot it returns), or (None, None)."""
    if not isinstance(fn, int) or fn >= TEXT_END: return None, None
    im = _img(); builder = None; slot = None; reg = {}; last = None
    for ins in _md.disasm(im[fn:fn + 0x80], fn):
        ops = [x.strip() for x in ins.op_str.split(",")]
        if ins.mnemonic == "bl":
            t = int(ops[0].lstrip("#"), 16)
            if 0x10000 < t < 0x2850000 and builder is None: builder = t    # skip the runtime's guard helpers (+0x2861a90...)
        elif ins.mnemonic == "adrp": reg[ops[0]] = int(ops[1].lstrip("#"), 16)
        elif ins.mnemonic == "ldr" and len(ops) > 2 and ops[1].lstrip("[") in reg and ops[0] == "x8":
            last = reg[ops[1].lstrip("[")] + int(ops[2].rstrip("]").lstrip("#"), 0)
        elif ins.mnemonic == "ldr" and ops[0] == "x0" and ops[1] == "[x8]" and slot is None:
            slot = last
        if ins.mnemonic == "ret" and slot is not None and builder is not None: break
    return builder, slot

_bl_by_target = None
def getter_of(name):
    """A type's getter when the table has none (rtypes learns getters only from field types and base types: a type nothing
    declares a field of, e.g. TowerFall.PauseMenu, has none): the function that calls the type's builder and returns its record
    slot (_getter_builder's shape). None when no such function exists."""
    global _bl_by_target
    t = table().get(name)
    if t is None: return None
    if t.getter: return t.getter
    if _bl_by_target is None:
        _bl_sites(0)
        im = _img()
        words = struct.unpack_from("<%dI" % (TEXT_END // 4), im, 0)
        _bl_by_target = {}
        for i, w in enumerate(words):
            if (w >> 26) == 0x25:
                imm = w & 0x3ffffff
                if imm & (1 << 25): imm -= 1 << 26
                _bl_by_target.setdefault(i * 4 + imm * 4, []).append(i * 4)
    R = relocs.relative()
    im = _img()
    for site in _bl_by_target.get(t.builder, []):
        # a getter is `str x19,[sp,#-0x20]!; stp x29,x30,[sp,#0x10]; ...; guard; bl builder` (Engine 0x6f1470, MainMenu 0x8b62f0):
        # back from the call to that prologue (the getter is often reached through a slot, not a bl: _func_start can't see it)
        for fn in range(site - 4, max(site - 0x80, 0), -4):
            if struct.unpack_from("<I", im, fn)[0] == 0xf81e0ff3:
                b, slot = _getter_builder(fn, R)
                if b == t.builder and slot is not None:
                    return fn
                break
    return None

def _fields(fb, n, R, getter_name):
    if not isinstance(fb, int) or fb >= TEXT_END: return []
    end = fb + 0x4000
    _, _, arrays, _ = _sim(fb, end, R, track_x0_array=True)
    out = []
    for arr in arrays[:1]:
        for k in range((n or 0)):
            name = ustr(arr.get(k * 0x40) if isinstance(arr.get(k * 0x40), int) else 0)
            off = arr.get(k * 0x40 + 0x10)
            attrs = arr.get(k * 0x40 + 0x20)
            tg = arr.get(k * 0x40 + 8)
            out.append((name, off if isinstance(off, int) else None, attrs if isinstance(attrs, int) else None,
                        getter_name(tg) if isinstance(tg, int) else None))
    return out

def _build():
    R = relocs.relative()
    sites = _bl_sites(MAKE_TYPE)
    raw = []
    for s in sites:
        st = _func_start(s)
        reg, stack, _, _ = _sim(st, s + 4, R, stop_at_bl=MAKE_TYPE)
        raw.append((st, s, reg, stack))
    by_builder = {}
    for st, s, reg, stack in raw:
        name = ustr(reg.get("x4") if isinstance(reg.get("x4"), int) else 0)
        if not name: continue
        desc = stack.get(0x68) if isinstance(stack.get(0x68), int) else None
        t = Type(name=name, builder=st, size=stack.get(0x60), flags=stack.get(0x78), attrs=stack.get(0x70), kind=reg.get("x1"),
                 desc=desc, vtable=R.get(desc + 8) if desc else None, parent_getter=reg.get("x7"),
                 field_builder=stack.get(0x80), nfields=stack.get(0x88), cctor=stack.get(0xd8))
        by_builder[st] = t
    getter_cache = {}
    def getter_name(g):
        if g in getter_cache: return getter_cache[g]
        b, slot = _getter_builder(g, R)
        t = by_builder.get(b)
        getter_cache[g] = t.name if t else None
        if t and t.getter is None: t.getter, t.record_slot = g, slot
        return getter_cache[g]
    table = {}
    for t in by_builder.values():
        t.parent = getter_name(t.parent_getter) if isinstance(t.parent_getter, int) else None
        t.fields = _fields(t.field_builder, t.nfields if isinstance(t.nfields, int) else 0, R, getter_name)
        table.setdefault(t.name, t)
    return table

_table = None
def table():
    global _table
    if _table is None:
        if os.path.exists(IDX) and os.path.getmtime(IDX) > max(os.path.getmtime(FLAT), os.path.getmtime(__file__)):
            with open(IDX, "rb") as f: _table = pickle.load(f)
        else:
            _table = _build()
            with open(IDX, "wb") as f: pickle.dump(_table, f)
    return _table

STATIC = 0x10
def chain(name):
    out, t = [], table().get(name)
    while t is not None and len(out) < 40:
        out.append(t); t = table().get(t.parent)
    return out

def fields(name, inherited=True, statics=False):
    """[(owner, field, offset, type)] instance fields (offset order), the base classes' too with inherited."""
    out = []
    for t in (chain(name) if inherited else [table()[name]]):
        for f, off, attrs, ty in t.fields or []:
            if not statics and attrs is not None and attrs & STATIC: continue
            out.append((t.name, f, off, ty))
    return sorted(out, key=lambda r: (r[2] if r[2] is not None else 1 << 30))

def field(typename, fname):
    """The byte offset of typename.fname (searching base classes), or None."""
    for owner, f, off, ty in fields(typename):
        if f == fname: return off
    return None

def show(name):
    t = table().get(name)
    if t is None: print(f"{name}: not found"); return
    fl = t.flags if isinstance(t.flags, int) else 0
    print(f"== {t.name}  size {t.size:#x}  flags {fl:#x}{' refs' if fl & 8 else ''}{' finalizer' if fl & 0x20 else ''}  "
          f"desc +{t.desc:#x} vtable +{(t.vtable or 0):#x}  builder +{t.builder:#x}  getter +{(t.getter or 0):#x}  cctor +{(t.cctor if isinstance(t.cctor, int) else 0):#x}" if isinstance(t.size, int) and t.desc else f"== {t.name} (incomplete) builder +{t.builder:#x}")
    print("   base: " + " <- ".join(x.name for x in chain(name)))
    for owner, f, off, ty in fields(name, statics=True):
        print(f"   {('+%#x' % off) if off is not None else '?':>7}  {f:32} {ty or '?':40} {'' if owner == name else '(' + owner + ')'}")

if __name__ == "__main__":
    a = sys.argv[1:]
    if a and a[0] == "--all":
        for n in sorted(table()): show(n)
    else:
        for n in a: show(n)
