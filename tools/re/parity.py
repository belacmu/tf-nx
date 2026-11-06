#!/usr/bin/env python3
"""parity.py: what differs between a class's code on the PC (TowerFall.exe, the browser/TF.EX build) and on the Switch (1.4.1.6 main).

For each method of the class, both sides' float constants (and integer immediates), side by side, with the methods whose constants
differ flagged. The Switch side comes from the class's metadata builder (classmeta.py): every declared member's invoke wrapper ->
DIRECT body, or VIRTUAL slot -> the class vtable (descriptor + 8, filled by R_AARCH64_RELATIVE relocations: relocs.py) -> body.
The PC side comes from the IL listing of the game TF.EX players run: TowerFall 1.3.3.1 patched by FortRise (tools/re/pc-il.sh,
cached at switch-online/.cache/towerfall-pc.il). Not the vanilla exe: FortRise changes game numbers (tools/re/fortrise-diff.py).

  parity.py TowerFall.Icicle [more...]      -v: every method, not only the differing ones
Floats: AArch64 `fmov s/d, #imm`, `ldr s/d, [x, #off]` after `adrp x` (literal in .rodata) and `mov/movk w; fmov s, w`.
PC floats: ldc.r4 / ldc.r8. Integers: Switch `mov w,#imm` / `cmp w,#imm`, PC ldc.i4*. Only floats decide "differs" (integer
immediates also come from field offsets, array strides etc. on the Switch); integers are shown for reading.
Overloads: a name with several bodies is compared as the union of its overloads on both sides."""
import bisect, collections, os, re, struct, sys
from capstone import Cs, CS_ARCH_ARM64, CS_MODE_LITTLE_ENDIAN
import metaresolve as mr, classmeta as cm, relocs

img, TEXT_END = mr.img, mr.TEXT_END
md = Cs(CS_ARCH_ARM64, CS_MODE_LITTLE_ENDIAN)
md.skipdata = True   # keep going past words capstone can't decode
RODATA = (0x2863000, 0x450d168)
IL = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", ".cache", "towerfall-pc.il")   # switch-online/.cache
R = relocs.relative()

# ---------------- PC: IL -> {class: {method: {"f": [floats], "i": [ints]}}}
def parse_il(path=IL):
    out = collections.defaultdict(lambda: collections.defaultdict(lambda: {"f": [], "i": [], "c": []}))
    stack, cur, pending, depth_cls = [], None, None, []
    depth = 0
    for line in open(path, errors="replace"):
        s = line.strip()
        if s.startswith(".class"):
            name = s.split()[-1]
            pending = ("class", name)
            continue
        if s.startswith(".method"):
            pending = ("method", s)
            continue
        if pending and pending[0] == "method" and not s.startswith("{"):
            pending = ("method", pending[1] + " " + s)
            continue
        if s.startswith("{"):
            depth += 1
            if pending and pending[0] == "class":
                depth_cls.append((depth, pending[1]))
            elif pending and pending[0] == "method":
                m = re.search(r"([\w.<>`$|']+)\s*\(", pending[1])
                cur = (depth, m.group(1).strip("'") if m else "?")
            pending = None
            continue
        if s.startswith("}"):
            if cur and depth == cur[0]: cur = None
            if depth_cls and depth_cls[-1][0] == depth: depth_cls.pop()
            depth -= 1
            continue
        if cur and depth_cls:
            cls = "+".join(n for _, n in depth_cls)
            m = re.search(r"\bldc\.r[48]\s+(\S+)", s)
            if m:
                try: out[cls][cur[1]]["f"].append(float(m.group(1)))
                except ValueError: pass
            m = re.search(r"\bldc\.i4(?:\.s)?\s+(-?\w+)", s)
            if m:
                try: out[cls][cur[1]]["i"].append(int(m.group(1), 0))
                except ValueError: pass
            m = re.search(r"\b(call|callvirt|newobj)\b.*?\s([\w.]+(?:`\d+)?)(?:<[^>]*>)?::([\w.<>`']+)\s*[(<]", s)
            if m: out[cls][cur[1]]["c"].append((m.group(1), m.group(2) + "::" + m.group(3).strip("'")))
            m = re.search(r"\bldc\.i4\.(m1|M1|\d)\b", s)
            if m: out[cls][cur[1]]["i"].append(-1 if m.group(1).lower() == "m1" else int(m.group(1)))
    return out

# ---------------- Switch: function extents and constants
_starts = None
def starts():
    """Every known function start: bl targets and relocated code pointers."""
    global _starts
    if _starts is None:
        st = set(v & 0x7fffffffffffffff for v in R.values() if 0 < (v & 0x7fffffffffffffff) < TEXT_END)
        words = struct.unpack_from("<%dI" % (TEXT_END // 4), img, 0)
        for i, w in enumerate(words):
            if (w & 0xFC000000) == 0x94000000:
                imm = w & 0x03FFFFFF
                if imm & (1 << 25): imm -= 1 << 26
                st.add(i * 4 + imm * 4)
        _starts = sorted(st)
    return _starts

def extent(body):
    s = starts(); i = bisect.bisect_right(s, body)
    end = s[i] if i < len(s) else body + 0x400
    return body, min(end, body + 0x4000)

fl_weak = []
def consts(body, weak_out=None):
    """(floats, ints) of a Switch body; with weak_out, also the integer bit patterns that decode to plausible floats."""
    global fl_weak
    fl_weak = weak_out if weak_out is not None else []
    a, b = extent(body)
    fl, it = [], []
    page, wv = {}, {}
    for ins in md.disasm(img[a:b], a):
        mn, op = ins.mnemonic, ins.op_str
        ops = [x.strip() for x in op.split(",")]
        if mn == "adrp":
            page[ops[0]] = int(ops[1].lstrip("#"), 16)
        elif mn == "add" and len(ops) == 3 and ops[1] in page and ops[2].startswith("#"):
            page[ops[0]] = page[ops[1]] + int(ops[2].lstrip("#"), 0)
        elif mn == "fmov" and ops[0][0] in "sd" and ops[1].startswith("#"):
            try: fl.append(float(ops[1].lstrip("#")))
            except ValueError: pass
        elif mn == "fmov" and ops[0][0] == "s" and ops[1].startswith("w") and ops[1] in wv:
            fl.append(struct.unpack("<f", struct.pack("<I", wv[ops[1]] & 0xffffffff))[0])
        elif mn == "fmov" and ops[0][0] == "d" and ops[1].startswith("x") and ops[1] in wv:
            q = struct.pack("<Q", wv[ops[1]] & 0xffffffffffffffff)
            fl += [struct.unpack("<d", q)[0]] + list(struct.unpack("<2f", q))
        elif mn == "fmov" and ops[0].startswith("v") and ops[-1].startswith("#"):
            try: fl.append(float(ops[-1].lstrip("#")))
            except ValueError: pass
        elif mn == "ldr" and ops[0][0] in "sd" and len(ops) >= 3 and ops[1].startswith("[x") and ops[2][:1] in "wx":
            base = ops[1].lstrip("[")
            if base in page and RODATA[0] <= page[base] < RODATA[1] - 16:   # a constant table indexed by a register (bool ternaries)
                fl += list(struct.unpack_from("<2f" if ops[0][0] == "s" else "<2d", img, page[base]))
        elif mn == "ldr" and ops[0][0] in "sdq" and len(ops) >= 2:
            m = re.match(r"\[(x\d+)(?:, #(0x[0-9a-f]+|\d+))?\]", ",".join(ops[1:]).replace(" ", "").replace(",#", ", #"))
            if m and m.group(1) in page:
                addr = page[m.group(1)] + (int(m.group(2), 0) if m.group(2) else 0)
                if RODATA[0] <= addr < RODATA[1] - 16:   # literals only (.data addresses are static fields)
                    if ops[0][0] == "s": fl.append(struct.unpack_from("<f", img, addr)[0])
                    elif ops[0][0] == "d":   # a double, or a packed Vector2 (two floats) when it doesn't read as a plain double
                        dv = struct.unpack_from("<d", img, addr)[0]
                        fl += [dv] if 1e-9 < abs(dv) < 1e12 and abs(float("%.6g" % dv) - dv) < 1e-6 * max(1, abs(dv)) else list(struct.unpack_from("<2f", img, addr))
                    else: fl += list(struct.unpack_from("<4f", img, addr))
        elif mn in ("mov", "movz") and ops[0][0] in "wx" and len(ops) == 2 and ops[1].startswith("#"):
            v = int(ops[1].lstrip("#"), 0); wv[ops[0]] = v; wv[("x" if ops[0][0] == "w" else "w") + ops[0][1:]] = v
            it.append(v if v < 0x80000000 else v - (1 << 32))
        elif mn == "movk" and ops[0] in wv:
            sh = int(ops[2].split("#")[1], 0) if len(ops) > 2 else 0
            v = int(ops[1].lstrip("#"), 0)
            wv[ops[0]] = (wv[ops[0]] & ~(0xffff << sh)) | (v << sh)
            other = ("x" if ops[0][0] == "w" else "w") + ops[0][1:]; wv[other] = wv[ops[0]]
            if it: it.pop()
            it.append(wv[ops[0]] if wv[ops[0]] < 0x80000000 else wv[ops[0]] - (1 << 32))
        elif mn == "cmp" and len(ops) == 2 and ops[1].startswith("#"):
            it.append(int(ops[1].lstrip("#"), 0))
    # float constants stored through integer registers (`mov w8, #0xc04ccccd; str w8, [x19, #speedY]` = -3.2f; a 64-bit mov/movk
    # pair = two packed floats, e.g. a Vector2): kept apart as "weak" floats; report() counts one when it matches a PC constant or
    # looks like a decimal literal
    weak = []
    for v in list(wv.values()) + [x & 0xffffffff for x in it if x > 0xffff or x < 0]:
        v &= 0xffffffffffffffff
        if v > 0xffffffff:
            dv = struct.unpack("<d", struct.pack("<Q", v))[0]
            if 1e-6 < abs(dv) < 1e9 and abs(float("%.6g" % dv) - dv) < 1e-9 * max(1, abs(dv)):
                weak.append(dv); continue   # a double constant built in an x register (0x3feb333333333333 = 0.85)
            f32 = struct.unpack("<f", struct.pack("<f", dv))[0] if 1e-6 < abs(dv) < 1e9 else None
            if f32 is not None and f32 == dv:
                weak.append(f32); continue  # a float literal widened to double ((double)0.85f = 0x3feb333340000000)
        for half in ((v & 0xffffffff, v >> 32) if v > 0xffffffff else (v,)):
            f = struct.unpack("<f", struct.pack("<I", half))[0]
            if half > 0xffff and 1e-4 < abs(f) < 1e6: weak.append(f)
    fl_weak.extend(weak)
    return fl, it

def calls(body):
    """Direct bl targets of a Switch body."""
    a, b = extent(body)
    out = []
    for ins in md.disasm(img[a:b], a):
        if ins.mnemonic in ("bl", "b") and ins.op_str.startswith("#"):
            t = int(ins.op_str[1:], 16)
            if t < a or t >= b: out.append(t)
    return out

_names = None
def call_names(bodies):
    global _names
    if _names is None:
        import callmap; _names = callmap.body_names()
    return set(_names[t] for b in bodies for t in calls(b) if t in _names)

def descriptor(typename, sites):
    """The class descriptor (vtable = reloc[desc + 8]): the last data reference before the type-name reference in the builder."""
    tok = mr.token(typename)
    for i, (o, t) in enumerate(sites):
        if t == tok:
            for j in range(i - 1, max(i - 12, 0), -1):
                oj, tj = sites[j]
                if 0x4500000 <= tj < 0x4900000 and o - oj < 0x80 and (tj + 8) in R: return tj
    return None

def switch_methods(typename, sites):
    """{method name: [bodies]} from the builder's method records (classmeta.records: exact name -> body pairs)."""
    res = collections.defaultdict(list)
    for n, body, flags in cm.records(typename, sites) or []:
        res[n].append(body)
    return res, descriptor(typename, sites), None

def norm(fs):
    """The set of distinct constant magnitudes: the Switch compiler loads a repeated constant once and reuses the register, and
    subtracts a positive constant where the IL loads a negative one."""
    return collections.Counter(set(round(abs(f), 4) for f in fs if f == f and abs(f) < 1e30))

LAMBDA = re.compile(r"^<([^>]*)>b__.*$")
def group(names_to_vals):
    """Compiler-generated lambdas (`<Method>b__N` on the PC, `<Method>b__N_M` on the Switch) compared as one group per method."""
    out = collections.defaultdict(list)
    for n, v in names_to_vals.items():
        m = LAMBDA.match(n)
        out[f"<{m.group(1)}>lambdas" if m else n].append(v)
    return out

def decimalish(f):
    """A float whose shortest decimal form is short (a literal someone typed, like 3.6 or 0.012)."""
    return len(repr(float(struct.unpack("<f", struct.pack("<f", f))[0])).rstrip("0")) < 0 or len(("%.6g" % f).replace("-", "")) <= 6 and abs(float("%.6g" % f) - f) < 1e-6 * max(1, abs(f))

def report(typename, il, sites, verbose=False):
    sw, desc, vt = switch_methods(typename, sites)
    pcm = il.get(typename, {})
    print(f"== {typename}  ({sum(len(v) for v in sw.values())} Switch bodies, {len(pcm)} PC methods)")
    pcm_g = group(pcm)
    pc = {k: [f for d in v for f in d["f"]] for k, v in pcm_g.items()}
    swg = {k: [b for bl in v for b in bl] for k, v in group(sw).items()}
    allpc = norm([f for v in pc.values() for f in v])
    def sw_floats(bodies):
        strong, weak = [], []
        for b in bodies:
            w = []; strong += consts(b, w)[0]; weak += w
        keep = [f for f in weak if round(abs(f), 4) in allpc or decimalish(f)]
        return norm(strong + keep)
    ndiff = 0
    allsw = collections.Counter()
    for n in sorted(set(pc) | set(swg)):
        sf = sw_floats(swg.get(n, []))
        allsw |= sf
        if n not in pc:
            if verbose: print(f"  {n:34} Switch only  bodies {[hex(b) for b in swg[n]]}")
            continue
        pf = norm(pc[n])
        if n not in swg:
            if pf - norm([0, 1]): print(f"  {n:34} not found on the Switch (PC floats {sorted(pf)})"); ndiff += 1
            continue
        only_pc = sorted(k for k in pf if k not in sf and k not in (0.0, 1.0))
        only_sw = sorted(k for k in sf if k not in pf and k not in (0.0, 1.0))
        pcalls = set(c for d in pcm_g.get(n, []) for k, c in d["c"])
        for _ in range(2):   # callees of callees: the Switch build inlines small methods, exposing what they call
            more = set()
            for c in pcalls:
                cls, meth = c.split("::", 1)
                for d in [il.get(cls, {}).get(meth)] if meth in il.get(cls, {}) else []:
                    more |= set(cc for k, cc in d["c"])
            pcalls |= more
        scalls = call_names(swg[n])
        NOISE = ("::get_", "::set_", "::op_", "Monocle.Layer::Add", "Monocle.Layer::Remove", "::.ctor", "Monocle.Scene::Add", "Monocle.Scene::Remove")
        game = lambda c: c.split("::")[0].split(".")[0] in ("TowerFall", "Monocle") and not any(x in c for x in NOISE)
        new_sw = sorted(c for c in scalls if c not in pcalls and game(c) and c.split("::")[0] != typename)
        if only_pc or only_sw:
            ndiff += 1
            print(f"  {n:34} DIFFERS  PC-only {only_pc}  Switch-only {only_sw}  bodies {[hex(b) for b in swg[n]]}")
        if new_sw:
            print(f"  {n:34} CALLS    Switch-only calls {new_sw}")
        elif verbose:
            print(f"  {n:34} same {sorted(pf)}")
    print(f"  class level: PC-only {sorted(k for k in allpc if k not in allsw and k not in (0.0, 1.0))}  Switch-only {sorted(k for k in allsw if k not in allpc and k not in (0.0, 1.0))}")
    print(f"  -> {ndiff} method(s) to look at")

if __name__ == "__main__":
    verbose = "-v" in sys.argv
    il = parse_il()
    sites = cm.index()
    for t in [a for a in sys.argv[1:] if not a.startswith("-")]:
        report(t, il, sites, verbose)
