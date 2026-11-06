#!/usr/bin/env python3
"""inlined.py: which game methods a translated mod patches were inlined by the Switch's build (docs/PLAN-TFNX-2026-11-04.md section 9,
gap 1). On the PC a Harmony patch runs wherever the method is called; on the Switch the AOT compiler copied small methods into some of
their callers, and a hook on the method's own body never sees those copies (Teams+'s score slots: GetScoreIndex inlined into nine
Session methods, fixed by hand in tfex/FrModes.cpp).

For every generated hook target (tfex/gen/tfexgen_hooks.cpp), its callers in the PC's IL (.cache/towerfall-pc.il: FortRise-patched
1.3.3.1) against the functions of the Switch's binary that call its body directly (a `bl` to it, tools/re/main.flat, named by
tools/re/callmap.idx). A PC caller whose Switch body exists but never calls the target: the target was inlined there (or that caller
was rewritten), so the patch misses it. Virtual targets are called through the vtable (no `bl`): listed, not checked.

Usage: tools/tfex/inlined.py [patch class prefix filter ...]   (e.g. `DataHooks RulesHooks`; default: every hook target)
Output: one line per target with misses, then a summary line `INLINED <n> of <m> targets missed by some callers`. Exit 0 always.
The RE files live in the main checkout ($XPLAY_RE_DIR: tools/re there; the IL: its .cache/towerfall-pc.il). """
import bisect, os, pickle, re, struct, subprocess, sys

here = os.path.dirname(os.path.abspath(__file__))
repo = os.path.normpath(os.path.join(here, "../.."))
main = subprocess.run(["git", "-C", repo, "worktree", "list", "--porcelain"], capture_output=True, text=True).stdout.split("\n")[0].removeprefix("worktree ")
re_dir = os.environ.get("XPLAY_RE_DIR", os.path.join(main, "tools/re"))
il_path = os.path.join(main, ".cache/towerfall-pc.il")
gen = os.path.join(repo, "hw-probe/exlaunch/source/program/tfex/gen")
TEXT_END = 0x28626C0   # (xref.py's: the end of .text in main.flat)
filters = sys.argv[1:]


def hook_targets():
    """[(target "TowerFall.ArcherData.Initialize", patches "DataHooks.Initialize_Postfix (postfix)", Switch body offset)]"""
    bind = {m.group(1): int(m.group(2), 16) for m in re.finditer(r"constexpr uintptr_t (\w+) = (0x[0-9a-f]+);", open(os.path.join(gen, "tfexgen_binding.hpp")).read())}
    src = open(os.path.join(gen, "tfexgen_hooks.cpp")).read()
    body = {m.group(1): m.group(2) for m in re.finditer(r"if \(!g_installed_(Hook_\d+) && bind::(\w+)\)", src)}
    out = []
    for m in re.finditer(r"/\* ([\w.`]+?)(?:\(([^)]*)\)|\.(get|set)): (.*?) \*/\nbool g_on_(Hook_\d+)", src):
        hook = m.group(5)
        if hook in body and body[hook] in bind and bind[body[hook]]:
            if m.group(3):   # a property's accessor ("TowerFall.MatchSettings.TeamMode.get"): the IL's get_TeamMode / set_TeamMode
                owner, prop = m.group(1).rsplit(".", 1)
                name, arity = f"{owner}.{m.group(3)}_{prop}", (0 if m.group(3) == "get" else 1)
            else:
                ps = m.group(2).strip()
                name, arity = m.group(1), (0 if not ps else ps.count(",") + 1)
            out.append((f"{name}/{arity}", m.group(4), bind[body[hook]]))   # name/arity: overloads apart
    return out


def switch_calls():
    """{callee address: [call site offsets]} for every direct `bl` and `b` in .text (a `b` counts where it lands on a body's start: a
    tail call); cached in the main checkout's .cache"""
    flat = os.path.join(re_dir, "main.flat")
    cache = os.path.join(main, ".cache", "bl-b-callers.pickle")
    if os.path.exists(cache) and os.path.getmtime(cache) > os.path.getmtime(flat):
        return pickle.load(open(cache, "rb"))
    data = open(flat, "rb").read()[:TEXT_END]
    calls = {}
    for i, (w,) in enumerate(struct.iter_unpack("<I", data)):
        if w & 0x7C000000 == 0x14000000:   # BL / B imm26 (a B onto a function's first instruction is a tail call)
            imm = w & 0x3FFFFFF
            if imm & 0x2000000: imm -= 0x4000000
            calls.setdefault(i * 4 + imm * 4, []).append(i * 4)
    os.makedirs(os.path.dirname(cache), exist_ok=True)
    pickle.dump(calls, open(cache, "wb"))
    return calls


def pc_callers(targets):
    """{"TowerFall.ArcherData::Initialize": {caller "TowerFall.TFGame::Load", ...}} from the PC IL, and the set of virtual targets"""
    want = {Key(t) for t in targets}
    callers = {w: set() for w in want}
    virtual = set()
    stack, pending_class, pending_method, header = [], None, None, []
    for line in open(il_path, encoding="utf-8", errors="replace"):
        s = line.strip()
        if s.startswith(".class"):
            pending_class = s.split()[-1]; header = None; continue
        if s.startswith(".method"):
            header = [s]; continue
        if header is not None and pending_method is None and (s.endswith("cil managed") or "cil managed" in s or "runtime managed" in s):
            header.append(s)
            h = " ".join(header)
            m = re.search(r"([\w.<>`|]+)\s*(<[^>]*>)?\s*\(", h.split("cil managed")[0])
            pending_method = (m.group(1) if m else "?", "virtual" in h)
            header = None; continue
        if header is not None:
            header.append(s); continue
        if s == "{":
            if pending_class is not None: stack.append(("class", pending_class)); pending_class = None
            elif pending_method is not None:
                stack.append(("method", pending_method[0]))
                cls = "+".join(n for k, n in stack if k == "class")
                if pending_method[1]: virtual.update(w for w in want if w.startswith(f"{cls}::{pending_method[0]}/"))
                pending_method = None
            else: stack.append(("block", ""))
            continue
        if s.startswith("}"):
            if stack: stack.pop()
            continue
        if " call" in line and "::" in s:
            m = re.search(r"\bcall(?:virt)?\s+(?:instance\s+)?.*?\s([\w.`+<>]+::[\w.<>|]+)\s*(?:<[^(]*>)?\((.*)\)\s*$", s)
            if m:
                args = m.group(2).strip()
                depth, n = 0, (0 if not args else 1)
                for ch in args:
                    depth += ch in "<[(" ; depth -= ch in ">])"
                    if ch == "," and depth == 0: n += 1
                callee = re.sub(r"`\d+", "", m.group(1)) + f"/{n}"
                if callee in callers:
                    cls = "+".join(n for k, n in stack if k == "class")
                    meth = next((n for k, n in reversed(stack) if k == "method"), "?")
                    callers[callee].add(f"{cls}::{meth}")
    return callers, virtual


def Key(t):
    """"TowerFall.ArcherData.Get/2" -> "TowerFall.ArcherData::Get/2" (the IL's spelling, the arity kept)"""
    name, arity = t.replace("`", "").rsplit("/", 1)
    return name.rsplit(".", 1)[0] + "::" + name.rsplit(".", 1)[1] + "/" + arity


def main_():
    targets = hook_targets()
    if filters: targets = [t for t in targets if any(p.split(".")[0].startswith(f) for f in filters for p in t[1].split(", "))]
    names = pickle.load(open(os.path.join(re_dir, "callmap.idx"), "rb"))   # {Switch body offset: "Class::Method"}
    starts = sorted(names)
    by_name = {}
    for off, n in names.items(): by_name.setdefault(n, []).append(off)
    calls = switch_calls()
    pcs, virtual = pc_callers([t for t, _, _ in targets])

    def func_of(site):
        i = bisect.bisect_right(starts, site) - 1
        return names[starts[i]] if i >= 0 else None

    missed = 0
    for t, patches, off in sorted(targets):
        key = Key(t)
        if key in virtual:
            print(f"  virtual  {key}: called through the vtable (not checked) [{patches}]"); continue
        sw = {func_of(s) for s in calls.get(off, [])}
        miss = sorted(c for c in pcs.get(key, ()) if "+" not in c and c in by_name and c not in sw)
        unknown = sorted(c for c in pcs.get(key, ()) if "+" in c or c not in by_name)
        if miss:
            missed += 1
            print(f"  INLINED  {key} (+{off:#x}): {len(miss)} of {len(pcs.get(key, ()))} PC callers don't call it on the Switch: {', '.join(miss)}"
                  f"{f'; {len(unknown)} more not named in the call map' if unknown else ''} [{patches}]")
    print(f"INLINED {missed} of {len(targets)} targets missed by some callers (virtual: {sum(1 for t, _, _ in targets if Key(t) in virtual)})")


main_()
