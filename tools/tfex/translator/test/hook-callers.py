#!/usr/bin/env python3
"""hook-callers.py: how the Switch's game code reaches each generated hook target (tfex/gen/tfexgen_hooks.cpp): direct call sites (`bl`, or a tail call `b`),
and pointers to it (R_AARCH64_RELATIVE relocations: vtable slots, delegates, the metadata invoke wrappers). A target with no `bl` and
only a wrapper's pointer may be a method BRUTE inlined into its callers (Integration B, 2026-10-17: PauseMenu's constructor inlines
AddItem, whose only `bl` is a wrapper at 0x27c1e4c, so TF.EX's AddItem prefix never sees the menu's own items). A hook there runs
only for the calls that weren't inlined. A delegate target (a menu action) is reached the same way (through the wrapper), so the list
is for review, not a verdict. Needs tools/re/main.flat (plain python3)."""
import os, re, struct, sys

here = os.path.dirname(os.path.abspath(__file__))
repo = os.path.normpath(os.path.join(here, "../../../.."))
gen = os.path.join(repo, "hw-probe/exlaunch/source/program/tfex/gen")
re_dir = os.path.join(repo, "tools/re")
sys.path.insert(0, re_dir)
import relocs   # noqa: E402

TEXT_END = 0x28626c0
bind = {m.group(1): int(m.group(2), 16) for m in re.finditer(r"constexpr \w+ (\w+) = (0x[0-9a-f]+);", open(os.path.join(gen, "tfexgen_binding.hpp")).read())}
hooks = open(os.path.join(gen, "tfexgen_hooks.cpp")).read()
targets = {}   # address -> {"Class.Method"}
for m in re.finditer(r'strcmp\(cls, "(\w+)"\) && !Listed\(skip, "(\w+)"\)\).*?InstallAtOffset\(bind::(\w+)\)', hooks):
    if m.group(3) in bind: targets.setdefault(bind[m.group(3)], set()).add(f"{m.group(1)}.{m.group(2)}")

img = open(os.path.join(re_dir, "main.flat"), "rb").read()
words = struct.unpack_from(f"<{TEXT_END // 4}I", img, 0)
sites = {a: [] for a in targets}
for i, w in enumerate(words):
    if w & 0x7c000000 == 0x14000000:   # BL / B imm26 (a tail call is a B)
        off = w & 0x03ffffff
        if off & 0x02000000: off -= 0x04000000
        t = i * 4 + off * 4
        if t in sites: sites[t].append(i * 4)
rel = relocs.relative()
ptr = {a: 0 for a in targets}
pointed = set(rel.values())
for t in rel.values():
    if t in ptr: ptr[t] += 1
# a metadata invoke wrapper (tools/re README, metaresolve: the class builder's record -> wrapper -> body): a tiny function, a frame push
# (stp x29, x30, [sp, #-0x10]!) at most 0x30 before the bl, then `mov x0, xzr`/ldp and `ret` at most 0x14 after it
STP_FRAME, RET = 0xa9bf7bfd, 0xd65f03c0
def in_wrapper(site):
    i = site // 4
    start = next((k for k in range(1, 13) if words[i - k] == STP_FRAME), None)
    if start is None or any(words[i - j] == RET for j in range(1, start)): return False
    return any(words[i + j] == RET for j in range(1, 6))

# address references (adrp + add): a delegate made from the method (a menu action, an event handler) calls it through its pointer
pages = {}
for a in targets: pages.setdefault(a & ~0xfff, []).append(a)
addr = {a: 0 for a in targets}
for i, w in enumerate(words):
    if w & 0x9f000000 != 0x90000000: continue   # ADRP
    imm = ((w >> 29) & 3) | (((w >> 5) & 0x7ffff) << 2)
    if imm & 0x100000: imm -= 0x200000
    page = ((i * 4) & ~0xfff) + (imm << 12)
    if page not in pages: continue
    rd = w & 31
    for j in range(1, 5):
        if i + j >= len(words): break
        a2 = words[i + j]
        if a2 & 0xffc00000 == 0x91000000 and (a2 >> 5) & 31 == rd:   # ADD xd, xn, #imm12
            t = page + ((a2 >> 10) & 0xfff)
            if t in addr: addr[t] += 1
            break

suspect = 0
for a in sorted(targets, key=lambda x: (len(sites[x]), x)):
    real = [s for s in sites[a] if not in_wrapper(s)]
    virtual = ptr[a] >= 2
    flag = "" if real or virtual or addr[a] else "  <- no direct call: a delegate / reflection target, or inlined (review)"
    suspect += bool(flag)
    print(f"0x{a:07x}  bl {len(sites[a]):2d} (wrappers {len(sites[a]) - len(real)})  address refs {addr[a]:2d}  pointers {ptr[a]:2d}  "
          f"{', '.join(sorted(targets[a]))}{flag}")
print(f"hook-callers: {len(targets)} generated targets, {suspect} reached only through their metadata wrapper: fine for a delegate target (a menu\n"
      "  action: VersusRematch, Quit ...); for a method TF.EX expects to be called directly the calls were inlined and the hook misses them\n"
      "  (PauseMenu.AddItem in PauseMenu's constructor). Statically the two look alike: review each against how TF.EX's patch is meant to fire.")
