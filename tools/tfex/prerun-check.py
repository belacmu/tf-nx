#!/usr/bin/env python3
"""prerun-check.py: static checks of the generated code that is on by default, before an emulator run (docs/tfstate-rollback-measurement.md,
"batch 1"). Each finding below cost a run on 2026-10-21 when it was found by crashing instead:
  1. unresolved game members (binding gap dummies) used by a default-on patch unit: read as defaults, a body may dereference them;
  2. a static game field read as an instance field (gen::Fld with a static storage address): TF.State's AlarmPatch read Alarm.cached
     at __instance + 0x488bcb0;
  3. reflection left in default-on code (AccessTools / Traverse / MethodInfo calls): the Switch has no reflection-made delegates;
  4. a generated hook on an address a hand hook also takes (exlaunch chains them: both run; the hand port is a duplicate to delete);
  5. a generated hook on a method rnd.cpp's whitelist checks the first instruction of (one failed check turns every RNG hook off);
  6. a hook argument declared as a by-value C++ container (gen::Arr / gen::Str ...): the game passes one register (a managed
     pointer), so every later argument shifted (TreasureChest's ctor made chests from garbage);
  7. (retired 2026-10-22: statics made inside the rollback window; with TF.State's LoadState as the rollback no memory restore takes them back)
  8. a no-op rule the generated code reaches that tools/tfex/noop-reviewed.toml doesn't list (doing nothing must be shown to give
     TF.EX's result: the ITfStateApi wildcard no-op hid ResetRng / SetSeed and a desync); [open] ones are named, not failed;
  9. trampoline headroom (exlaunch's fixed pool);
 10. translator gaps in default-on code outside TF.State's GetState / LoadState (an unsupported ThenBy emptied GenLevels' list).
Exit 1 when 1-6, 8 or 10 find anything; 9 is reported.

  tools/tfex/prerun-check.py"""
import glob, os, re, sys

here = os.path.dirname(os.path.abspath(__file__))
prog = os.path.join(here, '..', '..', 'hw-probe', 'exlaunch', 'source', 'program')
gen = os.path.join(prog, 'tfex', 'gen')

binding = open(os.path.join(gen, 'tfexgen_binding.hpp')).read()
bind = {m.group(2): int(m.group(3), 16) for m in re.finditer(r'constexpr (uintptr_t|uint32_t) (\w+) = (0x[0-9a-f]+);', binding)}
static_f = {m.group(1) for m in re.finditer(r'constexpr uintptr_t (f_\w+) = 0x[0-9a-f]+;', binding)}
unres = {m.group(1): m.group(2) for m in re.finditer(r'constexpr \w+ (\w+) = 0x7fff0000u \+ \d+;\s*/\* UNRESOLVED ([^:]+)', binding)}

hooks = open(os.path.join(gen, 'tfexgen_hooks.cpp')).read()
# every generated patch class is on at boot (hooks::EnableAll over kClasses, since 2026-10-26)
i = hooks.index('kClasses[]'); defaults = set(re.findall(r'"(\w+)"', hooks[i:hooks.index('};', i)]))

# the default-on units: each State_* file whose class is in the defaults, and the Auto_ units it includes (one level)
units = {}
for f in glob.glob(os.path.join(gen, 'tfexgen_State_*.cpp')):
    src = open(f).read()
    m = re.search(r': (\w+) \*/', src.split('\n')[1])
    if m and m.group(1) in defaults: units[os.path.basename(f)] = src
for name, src in list(units.items()):
    for inc in re.findall(r'#include "(tfexgen_Auto_\w+)\.hpp"', src):
        p = os.path.join(gen, inc + '.cpp')
        if os.path.exists(p) and inc + '.cpp' not in units: units[inc + '.cpp'] = open(p).read()

# reviewed findings, each with its reason (a row that starts with the key is allowed)
ALLOWED = {
    'tfexgen_State_LastManStandingRoundLogic.cpp: TowerFall.VersusLevelSystem.LastLevel': 'OnRoundStart\'s test-mode dump only (StateFlags.IsTestMode is false on the Switch)',
    'tfexgen_Auto_EntityDumper.cpp: bind::m_': 'EntityDumper: the same test-mode dump only',
}
bad = 0
def report(title, rows):
    global bad
    new = [r for r in rows if not any(r.startswith(k) for k in ALLOWED)]
    print(f'== {title}: {len(new)}' + (f' (+{len(rows) - len(new)} reviewed)' if len(rows) > len(new) else ''))
    for r in new: print('   ', r)
    bad += len(new)

# 1. unresolved members (State_ units only: Auto_ extensions are TF.State's GetState / LoadState, whose gaps are the measurement's)
rows = []
for name, src in sorted(units.items()):
    if not name.startswith('tfexgen_State_'): continue
    for b in sorted(set(re.findall(r'bind::(\w+)', src)) & unres.keys()): rows.append(f'{name}: {unres[b]}')
report('unresolved game members in default-on patch units (pin them, or leave the patch out)', rows)

# 2. static fields read as instance fields
rows = [f'{n}: {m.group(0)[:100]}' for n, src in sorted(units.items()) for m in re.finditer(r'gen::Fld<[^>]*>\([^,]+, bind::(f_\w+)\)', src) if m.group(1) in static_f]
report('static fields read as instance fields', rows)

# 3. reflection
rows = [f'{n}: {m.group(0)}' for n, src in sorted(units.items()) for m in re.finditer(r'bind::m_(AccessTools|Traverse|MethodInfo|MethodBase|FieldInfo|Type_Get)\w*', src)]
report('reflection calls in default-on code', rows)

# 4 / 5. hooks of the default classes against hand hooks and rnd's whitelist
inst = {m.group(1): bind.get(m.group(2)) for m in re.finditer(r'(Hook_\d+)::InstallAtOffset\(bind::(\w+)\)', hooks)}
cls = {}
for m in re.finditer(r'!std::strcmp\(cls, "(\w+)"\)[^;]*?g_on_(Hook_\d+)', hooks): cls.setdefault(m.group(2), set()).add(m.group(1))
on = {h: inst.get(h) for h, cs in cls.items() if cs & defaults}
hand, sites = {}, {}
for f in glob.glob(os.path.join(prog, '**', '*.cpp'), recursive=True):
    if os.sep + 'gen' + os.sep in f: continue
    src = open(f, errors='ignore').read()
    for m in re.finditer(r'(\w+)::InstallAtOffset\(\s*(0x[0-9a-f]+)', src): hand.setdefault(int(m.group(2), 16), []).append(f'{os.path.basename(f)}:{m.group(1)}')
    if os.path.basename(f) == 'rnd.cpp':
        offs = {m.group(1): int(m.group(2), 16) for m in re.finditer(r'(\w+) = \{ "[^"]+", (0x[0-9a-f]+)', src)}
        for m in re.finditer(r'(\w+)::InstallAtOffset\(off::(\w+)\.offset\)', src):
            if m.group(2) in offs: hand.setdefault(offs[m.group(2)], []).append(f'rnd.cpp:{m.group(1)}')
        if 'bool wl =' in src:   # (the whitelist's checks are gone since batch 2 part 3: every registration is generated)
            wl = src[src.index('bool wl ='):src.index(';', src.index('bool wl ='))]
            sites = {offs[n]: n for n in re.findall(r'ok2\(off::(\w+)\)', wl) if n in offs}
report('generated hooks on an address a hand hook takes', [f'{hex(a)} {",".join(sorted(cls[h] & defaults))} <-> {" ".join(hand[a])}' for h, a in sorted(on.items(), key=lambda x: x[1] or 0) if a in hand])
report("generated hooks on a method rnd.cpp's whitelist checks", [f'{hex(a)} {",".join(sorted(cls[h] & defaults))} <-> rnd whitelist {sites[a]}' for h, a in sorted(on.items(), key=lambda x: x[1] or 0) if a in sites])

# 6. hook arguments that are C++ containers by value
report('hook arguments declared as C++ containers by value', [f'{m.group(1)}: {m.group(2)}' for m in re.finditer(r'HOOK_DEFINE_TRAMPOLINE\((Hook_\d+)\) \{\n    static [^(]+\(([^)]*gen::(?:Arr|Str|VList|ValMap)<[^)]*)\)', hooks)])

# 8. reached no-ops: each reviewed (tools/tfex/noop-reviewed.toml)
import tomllib
rev = tomllib.load(open(os.path.join(here, 'noop-reviewed.toml'), 'rb'))
reached, cur = [], None
for l in open(os.path.join(gen, 'tfexgen_report.txt')):
    if not l.startswith(' '): cur = l.strip(); continue
    if cur.startswith('service map: no-op') or cur.startswith('service map: not on the Switch yet'): reached.append(l.strip())
report('reached no-op rules not reviewed (tools/tfex/noop-reviewed.toml)', [r for r in reached if r not in rev.get('reviewed', {}) and r not in rev.get('open', {})])
opens = [r for r in reached if r in rev.get('open', {})]
if opens: print('   (open, to be routed: ' + ', '.join(r.split('.')[-1] for r in opens) + ')')

# 10. translator gaps (TFEXGEN_STATE_GAP*) in default-on code, TF.State's GetState / LoadState included since they are the rollback (2026-10-22): a gap does
# nothing or gives a default, so a body runs on wrong data (2026-10-21: an unsupported ThenBy gave GenLevels an empty level list,
# and the first level load crashed). Reviewed ones, with their reason, in GAPS_REVIEWED.
GAPS_REVIEWED = {
    'tfexgen_Auto_StateContext.cpp Ctor': "_gameplayRandom: only GetState / LoadState read it; gameplay draws through the routed IRngService.Gameplay rule",
    'tfexgen_Auto_ServiceCollections.cpp get_Cache': "the LazyCache: AddEntityToCache is the glue cache (GlueState), so _cachedPickupEntries stays empty and PurgeCachedPickup never reaches get_Cache",
    'tfexgen_Auto_APIManager.cpp RegisterStateEvents': "mods' state events: only FortRise modules register them (ModExports), none on the Switch",
    'tfexgen_Auto_EntityDumper.cpp': "the test-mode entity dump (StateFlags.IsTestMode is false on the Switch)",
    'tfexgen_State_Level.cpp AddChestsState': "TimeSpan.FromMinutes(5): the entity cache's expiry, which the Switch's cache (GlueState) doesn't have; the value is unused",
    'tfexgen_Auto_Rng.cpp ToState': "DeterministicRandom.State: the Rng model's own object; the Switch's state reads and writes rnd.cpp's stream through glue/State.cs (UpdateRng)",
    'tfexgen_Auto_Rng.cpp Debug': "a debug string's hex formatting",
    'tfexgen_Auto_LanternExtensions.cpp LoadState': "Lantern.homeChain: not a field of the game the Switch runs (absent from the PC IL too); the re-link to the chain is skipped",
}
rows = []
for n, src in sorted(units.items()):
    fn = None
    for line in src.split('\n'):
        m = re.match(r'^\S.*?\b(\w+)\(.*\) \{$', line)
        if m: fn = m.group(1)
        # a gap whose stand-in is called (a sequence or a function: `TFEXGEN_STATE_GAP_EXPR(/* ... */)(`) traps when reached: no review
        # excuses it (StateContext.LoadDesiredSfx was reviewed as harmless and stopped the first TF.State load, 2026-10-22)
        called = re.search(r'TFEXGEN_STATE_GAP_EXPR\(/\*.*?\*/\)\(', line) is not None
        if 'TFEXGEN_STATE_GAP' in line and fn and n not in GAPS_REVIEWED and (called or f'{n} {fn}' not in GAPS_REVIEWED):   # (a whole-unit review: the unit is never reached)
            rows.append(f'{n} {fn}{" (CALLED: traps)" if called else ""}: {line.strip()[:110]}')
report('translator gaps in default-on code (the state path included)', rows)

# 9. trampolines: the default classes' hooks + the hand ones (the boot log's [pools] line is the measured number)
print(f'== trampolines (estimate): {len({a for a in on.values() if a})} generated default-on hook addresses + {sum(len(v) for v in hand.values())} hand installs; pool 327')
sys.exit(1 if bad else 0)
