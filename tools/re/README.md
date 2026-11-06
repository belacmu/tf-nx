# TowerFall Switch — reverse-engineering toolkit

Differential-RE helpers for finding native offsets in the BRUTE-transpiled Switch `main` NSO.
Companion to `additional-characters/find-offsets.md` and `docs/TOWERBALL-SWITCH-PORT.md`.

## Setup

```sh
python3 -m venv venv && ./venv/bin/pip install lz4 capstone
```

## Inputs

- **PC reference:** `TowerFall.exe` (Steam) — decompile with `ilspycmd <exe> -t <Type>` (already on
  PATH at `~/.dotnet/tools/ilspycmd`) to read the real method bodies / string literals.
- **Switch binary:** extract ExeFS from the game in Ryujinx (right-click → Extract Data → ExeFS).
  Gives `main` (NSO0). Current dump: `~/Roms/Switch/Towerfall ExeFS/main` (v1.4.1.6).

## Tools

- **`nso.py <main> [out.flat]`** — decompresses the NSO0 (lz4 per-segment) into a **vaddr-accurate
  flat image**: offsets in `.flat` == module-base-relative offsets == what exlaunch
  `InstallAtOffset` wants. Prints the segment map. For `main`: text `0..0x28626c0`,
  ro `0x2863000..0x450d168`, data `0x450e000..0x483b870`.
- **`xref.py <flat> <target_hex> [text_end_hex]`** — scans `.text` for `ADRP(+ADD/LDR)` PC-relative
  references to `target`. Use to find code that touches a given string/data address. Fast
  hand-decode of ADRP + capstone-confirmed ADD.

## Key facts about this binary (learned 2026-07-05)

- **Managed strings are UTF-16LE.** ASCII grep finds nothing; search `s.encode('utf-16-le')`.
  Method/type NAMES survive as a null-terminated UTF-16 **name pool** (e.g. `GetLevelHatStates\0
  GetLevelSystem\0…` back-to-back). `GetLevelSystem` occurs exactly once.
- **It's BRUTE-over-Mono AOT.** rodata paths show `.../Switch/Brute/mono/mscorlib/...`. `.text` is
  ~42 MB (whole game+FNA transpiled). No managed DLLs in romfs — code is only in `main`.
- **Name→function is NOT a simple pointer.** 325k `R_AARCH64_RELATIVE` relocs (parsed from
  `.dynamic`→`DT_RELA`), none with addend == a name-string address; the name pool is indexed by
  offset, and metadata builders reference names via ADRP/ADD in code.
- **Method metadata entries build a struct** (name ptr at `[obj+0x120]`, code ptrs at `+0xd0`/
  `+0x130`), but those code ptrs are **vtable-dispatch invoke-thunks** (`ldr x8,[x0]; ldr x8,[x8,#8];
  ldr x8,[x8,#slot]; … br x1`), NOT the concrete compiled body. So:
  - **Virtual methods** (like `GetLevelSystem`, which is called virtually as `levelData.
    GetLevelSystem()`) can't be hooked by a flat offset off the metadata — you must resolve the
    concrete impl via the type's **vtable slot** (slot offsets seen: 0x20, 0x28), whose function
    pointer is filled by a RELATIVE reloc at load (addend = concrete offset).
  - **Non-virtual methods** are called by direct `BL` to their concrete body — those DO have a
    fixed offset suitable for `InstallAtOffset`. Prefer a non-virtual method as the first real hook.

## Runtime symbol resolver — `resolve.py` (VALIDATED 2026-07-05)

The big win: BRUTE stripped the Mono *embedding API* (0 `mono_*` symbols) but kept the full Mono
**reflection metadata** — 28,327 type/method name strings. So we don't hand-hunt an offset per method;
we resolve **by name**. Recipe (`resolve.py`):

1. Find the method NAME as a **standalone** UTF-16LE token (null-terminated both sides).
2. `xref.py`-style ADRP/ADD scan → the class-metadata **builder** site that stores the name.
3. The **next** adrp+add pair after the name = the method's invoke **wrapper** pointer (stored 0x10
   after the name in the metadata struct).
4. The wrapper's first `bl` = the concrete method **body** — the address gameplay actually calls.
   (The wrapper itself has 0 direct callers; the body has the real callers. Confirm with a BL-caller
   count.)

Validated: `ShootArrow`→`0xaf1c50` (2 callers), `CollectArrows`→`0xaf5c30` (4), `DuckingUpdate`→
`0xaea610` (1); virtual `OnCollideV` enumerates one body per overriding class. **Boot-tested in
Ryujinx:** trampolines installed at `0xaf1c50`/`0xaf5c30` (resolver-derived) install clean + boot
stable → the resolved addresses are valid, hookable function entries. Edge cases (`GetXMLPositions`
and some `OnCollideV` wrappers show "no bl") use a different wrapper shape — refine step 4 (follow
`b`/tail-call, or the wrapper IS the body). Overloaded/duplicated names need type disambiguation
(match the enclosing class builder by its type-name string).

**Implication:** the eventual mod should ship this resolver *in the injected code* (walk metadata at
boot, hook by name) instead of baking offsets — update-robust. `resolve.py` is the static
proof-of-concept / offset generator.

### `metaresolve.py` — type-qualified resolver + classifier

`type_resolve(TypeName, MethodName)` → the class builder that references the type-name token, then the
method record within it → wrapper, then **classify**: DIRECT (concrete body) vs VIRTUAL (vtable slot,
from a dispatch thunk). `classify_wrapper` handles: simple bodies, **guarded** wrappers (init-thunk
`bl` + real `bl`, picked by max game-callers), and **static/tail-call** wrappers (`ldr x0,[x1]; b
body` — stop at the first tail `b`; it's the body and ends the function). Validated: instance methods,
getters (`get_X = ldr w0,[this,#off]; ret` → field offsets), simple statics (`GetRoundLogic`→0xb37f50).
**Known gaps:** many-arg invoke wrappers (e.g. `Explosion.Spawn` unpacks 6 args before the body `bl`)
and heavily-overloaded/undiscovered names (`Level.Add`, `AddScore`) — resolve those via call sites or
by reading the field directly (e.g. write `Session.Scores` instead of calling `AddScore`).

## Recipe (differential, fallback for things without clean metadata)

1. `ilspycmd` the PC method; note unique **string literals in its body** (not just its name).
2. Find that string in `.flat` as UTF-16LE; get its offset.
3. `xref.py` the string offset → code sites that use it → walk back to the function prologue
   (`stp x29,x30,[sp,...]` / `sub sp,...` / `str x19,[sp,#-…]!`). That prologue offset is the hook
   target. (Method NAME strings land in metadata thunks; prefer strings from the method BODY.)

## PC ↔ Switch code parity — `classmeta.py`, `relocs.py`, `parity.py`, `parity_all.py`, `callmap.py`, `ctx.py` (2026-10-15)

Finds behaviour differences between the PC build (TowerFall.exe, what the browser/TF.EX runs) and the Switch's 1.4.1.6 `main`
from the code, per method, instead of by test runs.

- **`classmeta.records(Type)`**: every declared method of a class as exact `(name, body, flags)`. The class's metadata builder
  allocates arrays (`mov w0,#size; bl #0xe60`) of 0x60-byte method records: +0x00 name (UTF-16 token), +0x10 invoke wrapper,
  +0x18 **the compiled body** (adrp+add, or ldr from a slot a RELATIVE relocation fills), +0x24 attributes (0x1886 = .ctor).
  The builder is simulated (register values, stores into the array). Virtual methods come out with their own body too.
- **`relocs.relative()`**: the 325k R_AARCH64_RELATIVE relocations (MOD0 -> .dynamic -> DT_RELA): slot -> target. Class
  descriptor + 8 -> vtable; vtable slots -> bodies.
- **`parity.py Type...`**: per method, the PC's IL constants (ldc.r4/r8, from `ilspycmd -il`, cached in
  switch-online/.cache/towerfall-pc.il) vs the Switch body's float constants (fmov imm, rodata literals incl. d/q vectors and
  bool-indexed tables, integer-stored float bit patterns), compared as sets of magnitudes; and the game methods each side calls
  (Switch `bl` targets named via `callmap.py`; PC call/callvirt/newobj plus two levels of callees, to absorb inlining; accessors
  ignored). `-v` prints every method.
- **`parity_all.py`**: all TowerFall/Monocle classes -> switch-online/.cache/parity-all.txt. Known noise: 160/320 literals on the
  PC vs a screen-size variable on the Switch (6-player wide screens: 210/420), `/2` compiled as `*0.5`, `int num = 60` divided as a
  float, 1e8 / 3.7e19 decoding artefacts.
- **`ctx.py <body> <const>...`**: the Switch code around each use of a constant (with callee names).

Found this way (fixed in switch-online xplay.cpp "version gap"): Player.Jump out of mud -2.2 (PC -3.2); Player.Update sets a player
on hot coals on fire (PC only bounces). A logic change without a new constant or call (e.g. a reordered condition) is not caught:
for those, trace the gameplay draws of the tick that diverges (switch-online: hw.txt `rnggametrace N F`, browser `xlobby rngtrace=N
rngfrom=F`) and compare call sites.

## Types, fields, TF.EX coverage — `rtypes.py`, `fieldcheck.py`, `sigs.py`, `tfexrefs.py`, `genheader.py` (2026-10-16)

docs/ws-tfex-subclass.md has the findings; tfex/Game.hpp `brute::` the runtime layout they rely on.

- **`rtypes.py [Type...|--all]`**: every managed type from its builder's call to `MakeType` (+0x2460, which copies its register
  and stack arguments into the 0x168-byte runtime type): size, flags (bit 3 = holds references: scanned GC_malloc), descriptor
  (obj[0]; desc[1] = vtable), base type (through the base getter), class-constructor runner (+0x70: BRUTE checks beforefieldinit at
  the call sites), and the field records its field builder fills (0x40 bytes: name, the field type's getter, offset, attributes;
  a static's "offset" is its storage address). Only public/internal fields have records; auto-property backing fields resolve
  through their getter body. Cached in rtypes.idx (~45 s to build). `import rtypes; rtypes.field("Monocle.Layer", "Index")`.
- **`fieldcheck.py`**: rtypes against offsets the port found by hand (38 ok, 0 wrong, 6 private).
- **`sigs.py Type [names]`**: the PC signatures (towerfall-pc.il, nested classes included) paired with classmeta.records'
  Switch bodies by declaration order: the overload you want, by parameter list.
- **`tfexrefs.py [-v] [--class Name]`**: the TowerFall/Monocle members TF.EX's network DLLs reference (their IL, from the release
  zip, in switch-online/.cache/tfex-0.19.2/il) and how many resolve here (fields, methods with overloads picked by IL signature,
  DynamicData names).
- **`genheader.py`** `Type | Type.Member | --tfex [--class Name]`: Game.hpp-style constexpr accessors from the above.
- **`notif-test.sh [-f]`**: a menu run with screenshots of TF.EX's Notification (tfex/Notification.cpp); `-f` through the real
  create-failed path.
