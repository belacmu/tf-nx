# Sharing the Switch mods with other players (design notes, 2026-11-04)

## The goal (the user)
A Switch-only player (no PC copy, no game dump, no PC levels) drops individual mod folders onto the SD card and everything works.
Optionally a Switch homebrew app downloads / updates those folders. No automatic Wi-Fi updates of the module. A curated list of
supported code mods is acceptable. Never shared: the game's files, the SSB roster (ours only). TowerBall: later.

## Decisions so far
- **Teams+** (the PC mod `TeamsPlus`) is the model: one source with the PC, translated into the module, turned on by its folder in
  `sd:/xplay/mods/`. The old three-team code is gone (tfex/TeamsPlusNative.cpp is what remains).
- **PC levels:** no files from the user. A small per-level delta (the 40 levels that really differ; all 141 differ byte-wise, mostly
  formatting such as `MiasmaH="False"`) ships with the release and is applied on the console, in memory, to the Switch's own levels.
  TF.EX ships no level data (it assumes every PC has the same files) - to confirm against its source.
- **Our art:** shipped alone (our images only), registered at runtime through the FortRise sprite / subtexture registries instead of
  rewriting the game's atlases (tools/megamod/romfs.py today). First case to test: the 8-player controller icons 7/8.
- **8 players:** through WiderSet as a FortRise-style mod folder ("the Switch version of WiderSet"), opening 8 players local and online,
  playing WiderSet's levels (`PCWide`) in both, retiring the megamod's 209 spawn-edited `6P-Versus` levels. Reverses "wide mode is
  native" in part. A prebuilt translation is still WiderSet's code: needs Teuria's permission. Its levels too (GameBanana: no
  redistribution).

## Research: can the module load code mods as separate files? (2026-11-04)
Wanted for the dream: each code mod its own file in its folder, no pre-combined module per mod set (Atmosphere loads one `subsdk9`).
- Executable memory at runtime: **available.** The npdm (hw-probe/exlaunch/config.json) grants `svcCreateCodeMemory` /
  `svcControlCodeMemory` (0x4b / 0x4c) and `svcSetProcessMemoryPermission` / `svcMapProcessCodeMemory`; exlaunch already uses a JIT area
  for hook trampolines (setting.hpp `JitSize` 0x14000 = 409 trampolines).
- A dynamic linker: **the source is there.** exlaunch carries `source/rtld/` (ModuleObject, relocation.cpp, ld.cpp: ELF relocation and
  symbol resolution), used for its AsRtld load kind; we build as `LOAD_KIND := Module`, so it isn't used today.
- Nothing loads a plugin today. What one would need: a plugin format (a position-independent ELF / NRO built against the core's
  headers), a loader in the core (read `sd:/xplay/mods/<mod>/*.nro`-like file, copy into code memory, relocate, resolve imports against a
  table the core exports, call its init), and a versioned core ABI (generated TF.EX / FortRise types change every TF.EX release, so a
  plugin is built for one core version).
- Not checked: whether the game links nn::ro (the SDK's own NRO loader, what Skyline-style plugins use); our own loader doesn't need it.
- Shared limits: plugins draw from the same trampoline pool (log and fail clearly when full).

**Verdict:** plausible, no blocker found in the code.

## Code plugins: the proof (2026-11-04, Ryujinx)
The goal (the user): a Switch port of FortRise. FortRise on the PC loads a mod folder's content and its DLL; here the content part is
the translated FortRise loader (sd:/xplay/mods) and the DLL's stand-in is a precompiled `plugin.elf` in the same folder.
- **Loader:** `hw-probe/exlaunch/source/program/plugins.cpp`, API `plugin_api.h` (C; versioned table: log, game_base, hook). A plugin
  imports nothing. Build one with `plugins/build.sh <Plugin>` (PIC, no libc, `-z separate-code`: code and data on separate pages).
- **Where it runs:** at the game's first GameData.Load (+0xa46740), before the game's data loads. Not in exl_main: the SDK's file
  system isn't up then (`MountSdCardForDebug` faulted in `nn::fs::detail::Register`).
- **Memory:** a 256 KB zero-filled area in the module's own .text. The image is written through a writable alias
  (svcMapProcessMemory, as exlaunch's hook area), then unmapped. Code pages stay read+execute; data pages are made RW and read-only
  pages R (svcSetProcessMemoryPermission). Ryujinx allows only one permission change from the module's code state: making the area RW
  first and then RX was refused (0xd401).
- **Pools:** `plugin code area KB` and `plugins` (8 slots) in the pools log; running out is `[pools] EXHAUSTED`. Plugins share the hook
  trampolines (324/409 used 15 s after boot with the proof plugin).
- **Proof:** `plugins/HelloPlugin` hooks Monocle's Music.Play (+0x9410d0, also hooked by FrModsBoot: they chain). Log lines:
  `[plugins] HelloPlugin: 16 KB at +0x0 of the code area (1 code, 2 read-only, 1 data pages), starting it`,
  `[plugin] HelloPlugin: hooked Music.Play (ok)`, `[plugins] 1 plugin(s) found in 1 mod folder(s), 1 started`, and at the title music
  `[plugin] HelloPlugin: Music.Play call 1 done` (its counter in writable data). 150 s, no guest exception.
- **Not checked:** the console (the real kernel's permission rules; hardware checklist: the four lines above); a plugin with
  relocations (the proof had none: the loader handles RELATIVE and ABS64/GLOB_DAT/JUMP_SLOT to its own symbols); C++ in a plugin
  (needs its own runtime pieces, or the API to provide them).
- **Next:** what a real plugin needs from the core: the translated TF.EX / FortRise types and the generated runtime (GC roots, strings,
  hooks by binding name), so a translated mod (Teams+, WiderSet) builds as a plugin against a core version.

## TF.NX: the shape (the user, 2026-11-04)
**Superseded by the plan: docs/PLAN-TFNX-2026-11-04.md** (built-in mods dormant until their folder is there, a folder-vs-logic
compatibility check, plugins parked). The first sketch, kept for the record:
**TF.NX** = the core: TF.EX (translated) + the Switch port of FortRise (content loader + the plugin loader) + the Switch's own systems
(rollback, transport, wide mode). Fixed, one `subsdk9`. Everything else is a mod folder in `sd:/xplay/mods/<Mod>/`: the PC mod as is
(meta.json, Content) plus, for a code mod, its Switch build `plugin.elf` (by mod name and version, built against one TF.NX version).
Added, removed and updated one folder at a time. Archer / content mods already work this way.

Work, in order: (1) TF.NX exports its translated types and runtime to plugins, the loader links a plugin's imports, the translator
builds one mod as a plugin: Teams+ first (added / removed as a folder); (2) a plugin's state in TF.EX's rollback saves (Teams+ online in
sync); (3) TowerBall out of the megamod into a plugin; WiderSet translated as a plugin (8 players; Teuria's permission); (4) others
(Soccer...) one at a time. First of all: one console run of the plugin proof (the real kernel's permission rules).

## Open
- Permissions: TF.EX, FortRise licences for the translated output; Teuria for WiderSet.
- The homebrew app: download / update mod folders and plugins, version checks (game 1.4.1.6, core version vs plugin version).
