#!/usr/bin/env python3
"""hook-collisions.py: generated hook targets (tfex/gen/tfexgen_hooks.cpp) that hand-written code also hooks (an InstallAtOffset(0x...)
anywhere else under hw-probe/exlaunch/source/program, a literal or a named constexpr offset). Both run: exlaunch chains hooks on one
address (lib/hook/nx64/hook_impl.cpp "Chained hooks per address", since 2026-10-18): the hook installed later is the outer one and its
Orig is the earlier one. Hand hooks install at boot and a mod's generated hooks at its boot, after them, so a mod's patch wraps ours as
FortRise's Harmony patch wraps the game's code. Listed for review (the order, a hand Wrap shell where ours must come first); exit 0.
A generated class enabled at boot (TF.EX's, EnableAll) may install before or after a hand hook: its line says "boot order"."""
import os, re, sys

here = os.path.dirname(os.path.abspath(__file__))
prog = os.path.normpath(os.path.join(here, "../../../../hw-probe/exlaunch/source/program"))
gen = os.path.join(prog, "tfex/gen")

bind = {m.group(1): int(m.group(2), 16) for m in re.finditer(r"constexpr \w+ (\w+) = (0x[0-9a-f]+);", open(os.path.join(gen, "tfexgen_binding.hpp")).read())}
hooks = open(os.path.join(gen, "tfexgen_hooks.cpp")).read()
generated = {}   # address -> [(class, method)]
for m in re.finditer(r'strcmp\(cls, "(\w+)"\) && !Listed\(skip, "(\w+)"\)\).*?InstallAtOffset\(bind::(\w+)\)', hooks):
    if m.group(3) in bind: generated.setdefault(bind[m.group(3)], []).append((m.group(1), m.group(2)))

def hand_files():
    for root, _, files in os.walk(prog):
        if os.path.abspath(root).startswith(os.path.abspath(gen)): continue
        for f in files:
            if f.endswith((".cpp", ".hpp")): yield os.path.join(root, f)

# named offsets (constexpr X = 0x..., several per declaration), with their enclosing namespaces / structs: Player_Update,
# MainMenu::CreateMain, off::AltCheck ...
consts = {}   # name -> [(scope tuple, value)]
for p in hand_files():
    text = open(p, errors="replace").read()
    stack, depth, pending = [], 0, None
    for m in re.finditer(r"\b(?:namespace|struct|class)\s+(\w+)[^;{]*\{|[{}]|constexpr\s+[\w:]+\s+([^;]*);", text):
        tok = m.group(0)
        if m.group(1):
            depth += 1; stack.append((m.group(1), depth))
        elif tok == "{": depth += 1
        elif tok == "}":
            if stack and stack[-1][1] == depth: stack.pop()
            depth -= 1
        elif m.group(2):
            for d in re.finditer(r"(\w+)\s*=\s*(0x[0-9a-fA-F]+)\b", m.group(2)):
                consts.setdefault(d.group(1), []).append((tuple(n for n, _ in stack), int(d.group(2), 16)))

def resolve(expr):
    parts = expr.split("::")
    cands = consts.get(parts[-1], [])
    q = [x for x in parts[:-1] if x and x not in ("game", "tfex")]
    hits = {v for sc, v in cands if all(x in sc for x in q)}
    return hits.pop() if len(hits) == 1 else None

hand, unresolved = {}, []   # address -> [file:line]
for p in hand_files():
    for i, line in enumerate(open(p, errors="replace"), 1):
        for m in re.finditer(r"InstallAtOffset\(([\w:]+)\)", line):
            e = m.group(1)
            a = int(e, 16) if e.startswith("0x") else resolve(e)
            if a is None:
                if not e.startswith("bind::") and e not in ("body",): unresolved.append(f"{e} ({os.path.relpath(p, prog)}:{i})")
                continue
            hand.setdefault(a, []).append(f"{os.path.relpath(p, prog)}:{i}")

# a mod's patch classes (translate.toml units with enable_all = false: enabled by the mod's generated boot, after the hand hooks)
toml = open(os.path.normpath(os.path.join(here, "../../translate.toml"))).read()
on_demand = set(re.findall(r'\[\[unit\]\]\s*\nname = "[^"]*"[^\[]*?\ntype = "(\w+)"[^\[]*?enable_all = false', toml))
shared = sorted(set(generated) & set(hand))
for a in shared:
    gens = generated[a]
    order = "chained: the mod's patch outer (installed at its boot), then ours" if all(c in on_demand for c, _ in gens) else "chained: boot order"
    print(f"0x{a:x}: generated {', '.join(c + '.' + m for c, m in gens)}; hand {', '.join(hand[a])} ({order})")
if unresolved: print("hand hooks whose offset wasn't found (not checked): " + ", ".join(unresolved))
print(f"hook-collisions: {len(generated)} generated targets, {len(hand)} hand targets, {len(shared)} shared (chained)")
