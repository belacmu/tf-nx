#!/usr/bin/env python3
"""globref.py: who uses a word of the main module's globals (.data / .bss).

Run from tools/re (it reads main.flat there), with this folder on PYTHONPATH:
  PYTHONPATH=".:<worktree>/tools/re-verify" python3 <worktree>/tools/re-verify/globref.py <hw-log or ->   classify a `rbglobstat 1` report
  PYTHONPATH=... python3 .../globref.py 0x483be30 0x4903660 ...                                       the users of single words

A global is "used" by code through ADRP + ADD / LDR / STR / LDP / STP, or through a data slot that holds its address (an
R_AARCH64_RELATIVE relocation: how the BRUTE code reaches managed static fields and runtime objects). Each changed range is
attributed to the nearest used address at or below it, and the users' code is classified by address:
  native   below 0x48000: the platform layer (nn::audio +0x17b00.., NVN +0x19600.., nn::fs, hid +0x30xxx, vi/oe, the runtime's C++)
  runtime  0x2856000 and up: Boehm GC and the runtime's allocator glue (malloc wrapper +0x28617e0)
  managed  everything in between (BRUTE-transpiled C#: static field getters / setters, type caches)
The 0x48000 boundary is a heuristic (the last nn:: import call from the low region is at 0x3cf28); "native,managed" means both
use it. See docs/ws-perf.md, "Native state in the module's globals".
"""
import bisect, collections, re, struct, sys
import fnspan as f

TEXT_END = 0x28626C0
GLOB_LO, GLOB_HI = 0x450E000, 0x4904000   # .data 0x450e000..0x483b870, .bss to 0x4904000

img = open("main.flat", "rb").read()


def relocs():
    mod0 = struct.unpack_from("<I", img, 4)[0]
    dyn = mod0 + struct.unpack_from("<i", img, mod0 + 4)[0]
    d, o = {}, dyn
    while True:
        tag, val = struct.unpack_from("<QQ", img, o); o += 16
        if tag == 0: break
        d.setdefault(tag, val)
    out = {}
    for k in range(d[8] // 24):
        off, info, add = struct.unpack_from("<QQq", img, d[7] + k * 24)
        if info & 0xFFFFFFFF == 0x403: out[off] = add
    return out


def ldst(y):
    if (y & 0x3B000000) == 0x39000000:   # LDR/STR unsigned offset
        size, v, opc = y >> 30, (y >> 26) & 1, (y >> 22) & 3
        sc = 4 if (v and opc >= 2) else size
        return (y >> 5) & 31, ((y >> 10) & 0xFFF) << sc, opc == 0
    if (y & 0x3A000000) == 0x28000000 and ((y >> 23) & 3) in (1, 2, 3):   # LDP/STP
        opc, v, L = y >> 30, (y >> 26) & 1, (y >> 22) & 1
        sc = (2 + opc) if v else (2 if opc == 0 else 3)
        imm = (y >> 15) & 0x7F
        if imm & 0x40: imm -= 0x80
        return (y >> 5) & 31, imm << sc, not L
    return None


def code_refs():
    """address -> [(pc, is_store)] for every ADRP-based reference into the module's data / bss"""
    import array
    w = array.array("I"); w.frombytes(img[:TEXT_END])
    refs = collections.defaultdict(list)
    for i, x in enumerate(w):
        if (x & 0x9F000000) != 0x90000000: continue
        imm = (((x >> 5) & 0x7FFFF) << 2) | ((x >> 29) & 3)
        if imm & (1 << 20): imm -= 1 << 21
        page = ((i * 4) & ~0xFFF) + (imm << 12)
        if not (GLOB_LO - 0x1000 <= page < GLOB_HI): continue
        regs = {x & 31: page}
        for j in range(1, 10):
            if i + j >= len(w): break
            y = w[i + j]
            if (y & 0xFF800000) == 0x91000000:   # ADD imm
                rn, rd = (y >> 5) & 31, y & 31
                if rn in regs:
                    t = regs[rn] + (((y >> 10) & 0xFFF) << (12 if (y >> 22) & 1 else 0))
                    refs[t].append(((i + j) * 4, False)); regs[rd] = t
                else: regs.pop(rd, None)
                continue
            r = ldst(y)
            if r and r[0] in regs:
                refs[regs[r[0]] + r[1]].append(((i + j) * 4, r[2]))
                if not r[2]: regs.pop(y & 31, None)
                continue
            if (y & 0xFC000000) in (0x94000000, 0x14000000) or (y & 0xFFFFFC1F) == 0xD65F0000: break
            if (y & 0x9F000000) == 0x90000000: regs.pop(y & 31, None)
    return refs


def region(pc):
    return "native" if pc < 0x48000 else ("runtime" if pc >= 0x2856000 else "managed")


def fnstart(a):
    while not f.is_udf(a - 4): a -= 4
    return a


def main():
    refs, rel = code_refs(), relocs()
    users = collections.defaultdict(list)
    for t, l in refs.items(): users[t] += [pc for pc, _ in l]
    for slot, t in rel.items():
        if GLOB_LO <= t < GLOB_HI: users[t] += [pc for pc, _ in refs.get(slot, [])]
    keys = sorted(t for t in users if GLOB_LO <= t < GLOB_HI)

    def describe(lo, hi, win=0x100):
        i = bisect.bisect_right(keys, hi - 1)
        tg = [t for t in keys[max(0, i - 3):i] if t >= lo - win] or keys[max(0, i - 1):i]
        c = collections.Counter(region(pc) for t in tg for pc in users[t])
        fns = sorted({fnstart(pc) for pc in users[tg[-1]]})[:3] if tg else []
        cls = ",".join(k for k, _ in c.most_common()) or "slot only"
        return tg[-1] if tg else 0, cls, " ".join(hex(x) for x in fns)

    if sys.argv[1:] and sys.argv[1].startswith("0x"):
        for a in sys.argv[1:]:
            a = int(a, 16); t, cls, fns = describe(a, a + 1, 0)
            print("+%#x: nearest used +%#x (%+d), %s, users %s" % (a, t, a - t, cls, fns))
        return
    src = open(sys.argv[1], errors="replace") if sys.argv[1:] and sys.argv[1] != "-" else sys.stdin
    total = collections.Counter()
    for l in src:
        m = re.search(r"glob \+(0x[0-9a-f]+)\.\.\+(0x[0-9a-f]+) \((\d+) words\) changed in up to (\d+) ticks.*frames (\d+)\.\.(\d+)", l)
        if not m: continue
        lo, hi = int(m.group(1), 16), int(m.group(2), 16)
        t, cls, fns = describe(lo, hi)
        total[cls] += 1
        print("+%#x..+%#x %3s words %5s ticks (frames %s..%s) %s: nearest used +%#x, %s, users %s" % (
            lo, hi, m.group(3), m.group(4), m.group(5), m.group(6), "data" if lo < 0x483B870 else "bss ", t, cls, fns))
    print("ranges by users:", dict(total))


main()
