# TF.NX catalog mods

Every code mod TF.NX carries the logic of lives in its own folder here, `mods/<name>/`, named after its meta.json `name` (FortRise's
namespaced id: `Teuria.WiderSet`, `Switch.TowerBall`). On the Switch the mod is dormant until its folder is in `sd:/xplay/mods/`; the
folder is the PC release as it is. Background: docs/PLAN-TFNX-2026-11-04.md. The overview for players: docs/MODS-CATALOG.md (generated).

## A mod's folder

| file | what |
|---|---|
| `mod.toml` | its catalog entry (below) |
| `meta.json` | its release's FortRise metadata, as the release ships it (a translated mod's: a copy of its vendored one, checked) |
| `src/` | its native C++ (compiled into the module from here: tools/build-probe.sh copies it to source/program/mods/<name>/) |
| `build_folder.*` | builds its SD folder from files we can't keep in git (third-party art, the player's own download) |
| `NOTES.md` | anything else worth knowing (optional) |

A translated mod's code stays where the translator puts it (tools/tfex/translate.toml `[[unit]]`s over its vendored source, gen/).

## mod.toml

| key | meaning |
|---|---|
| `meta` | its meta.json: name, version (the version its logic is: what TF.EX's lobby shows), dependencies, `update` (its GameBanana id: the update check) |
| `logic` | `translated` or `native` |
| `vendored` | a translated mod's vendored release: where checked out, its meta.json must equal ours and `needs` hold every `"Content/..."` literal of its source (builds never need it) |
| `needs` | more files its folder must have; a folder missing one doesn't load (named in the log and on the MODS screen) |
| `boot` | the C++ function that starts it once its folder loaded (FortRise constructing its module): its hooks go in here, never at boot |
| `early` | only when something must happen before the game's own setup (WiderSet's 8 seats: the player arrays), at module start, folder or not; it must be inert without the folder |
| `replaces` | game textures its own replace while it's loaded: `{ id, with, atlas }` (`with`: its folder's name for it) |
| `overlay` | game files its folder supplies while it's loaded (`*` = one path segment), when the game loads them (XML: levels, tower data) |
| `[folder] build`, `extras` | the command that builds its SD folder (empty: the player's own download), and whether only the extras build has it |
| `[release] ship` | its folder goes in TF.NX's release zip (tools/release.sh): only for a mod we may redistribute |
| `[update]` | where its newest version is announced, for tools/updates/check.py: `github = "owner/repo"`, `gamebanana = <id>` (with `seen = "<date>"` when the page has no version) or `local = "<our repository's meta.json>"`; none: its meta.json's own `update` field |
| `[overview]` | `source`, `download`, `permission`, `works`: docs/MODS-CATALOG.md |
| `[check]` | `with`: its runs (localrun / smoke); `expect`: log lines one of them must show |

A translated mod's new release, in one command: `tools/mods/update.sh <name> [<tag>]` (its latest GitHub release by default): the vendored
submodule at the release's tag, meta.json the release's, gen/ retranslated, the catalog regenerated, the module built; nothing edited by
hand. Then commit, land and publish.

After editing: `tools/mods/catalog.py` (the module's table, modcatalog.gen.hpp, and docs/MODS-CATALOG.md; build-probe refuses a stale one).

## Rules (lessons from Teams+, TowerBall, WiderSet; FortRise's own design)

1. **Use FortRise's mechanism first.** A mode, tower, variant, arrow, pickup, theme, music, sound or texture goes through the matching
   FortRise registry (vendor/fortrise .../Core/Registry), translated if the Switch lacks it, as a PC mod would; a Switch-only mechanism
   only when none fits, with a TFEX-DIFF reason. (TowerBall's Warlord mode slot is such a stand-in until the FortRise TowerBall.)
2. **Mods never reach into each other or name each other in core code.** A mod publishes what others need as an API (modcatalog.hpp:
   `RegisterApi(kind, struct)`, FortRise's `IModInterop.GetApi`); others ask by kind.
3. **Dormant without its folder:** no hooks, no patched words, nothing in menus. Its `[mods] <name> booted: N hook trampolines` line
   shows what it costs from the shared pool (409).
4. **Its files come from its folder,** never from the romfs: its art through FortRise's content loader (`Content/Atlas/*.xml`, named
   `<name>/<texture>`), its own files through FortRise's mod paths (`mod:<name>/<path>`) or `overlay`.
5. **Our tools build a folder only from local sources,** and never ship third-party files.

## Checks

`tools/tfex/inlined.py <its patch classes>`: the game methods it patches that the Switch inlined into some callers (each needs a
fix per game method, as tfex/FrModes.cpp's). `tools/mods/check.sh <name>`: its `[check]` runs, the `expect` lines, and a boot without the built folders (dormant). Run it when the
mod or the catalog machinery changes; it's one or two emulator runs plus a boot.

## Later (FortRise has them; we take them when a mod first needs them)

- the MODS screen's on/off switch: FortRise's `blacklist.txt` and its toggler (UIModToggler), translated;
- a mod's settings page: FortRise's `ModuleSettings`;
- mods loaded straight from their `.zip` (FortRise's ZipResourceInfo);
- the update check script: each meta.json's `update` source.
