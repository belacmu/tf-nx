# Teams+ (was Three Team Mode) as one FortRise mod, PC and Switch (plan, 2026-10-31)

**Name (the user, 2026-10-31): the mode is called Teams+** ("TEAMS+" in the GAME MODE carousel; the game's font has '+'). Renamed
in the PC mod (pc-fortrise 2fa1a87: the mode's Name, meta.json's displayName, the docs and the GameBanana kit). The mod's id and
folder stay `ThreeTeamMode` (an installed copy updates in place; the Switch looks for that folder), as do the code's class names.

The user (2026-10-31): the three-team mod should be a FortRise-compatible mod that makes a real new game type. A PC version already
exists, and it is that: this plan makes it the one source for both platforms and retires the Switch's hand-written copy.

**Source (the user, 2026-10-31: its own repository):** `../three-team-mod/pc-fortrise` is a git repository now (branch main, first
commit f0feb32: 0.1.1 as released; bin/, obj/, shots/ and the zips ignored). No remote yet. Its README's links to `../../docs` point
outside the repository (the shared Towerfall Switch Mods docs).

## Where things stand

| | Switch (today) | PC (`../three-team-mod/pc-fortrise`) |
|---|---|---|
| What | `megamod/threeteam.hpp` in the module (generated once from three-team-mod's exlaunch overlay, edited here since) | **ThreeTeamMode 0.1.1**, a FortRise 5 code mod (C#, ~3200 lines, `meta.json` name `ThreeTeamMode`, built against FortRise 5.3.0) |
| Game type | none of its own: rides **Team Deathmatch** in the 2-8 lobby (hooks on the inlined `Mode == 5` tests) | **its own versus mode "Three Team"** (`RegisterVersusGameMode`, `IsTeamMode`, own icon, own `RoundLogic`); vanilla TDM untouched |
| Features | yellow team, lone wolves, 4-column select, 8 players (the megamod's) | the same: YELLOW (gold sheets recoloured at runtime, custom archers too), lone wolves (white), SOLO/BLUE/YELLOW/RED select, start with any two groups, spawning for a third group, team-designed levels split per group, one results row per group, TEAM REVIVE (0.1.1), 8 players through WiderSet |
| Checked | `localrun.sh 8 5` (three teams, 30 s in, 2026-10-30) | scripted self-test (`THREETEAM_SELFTEST=1`) with real frames, 4 and 8 players (STATUS.md there) |
| Online | off (megamod passes through online) | local only (TF.EX plays its own Netplay mode) |
| Shipped | in the console build | `release/ThreeTeamMode.zip`, a GameBanana kit ready (not posted) |

So the game-type work is done on the PC. What is left is the Switch running that mod instead of its own copy.

## The target

- **One mod, one source:** the PC mod's code is the source of truth. The Switch runs it the way it runs TF.EX: translated into the
  module (tools/tfex), its Harmony patches as exlaunch hooks, its `Content/` (mode icon, banner) loaded from `sd:/xplay/mods/ThreeTeamMode/`
  like any content mod. The same zip a PC player installs is what goes on the card.
- **On when the mod is there:** like FortRise, the mode exists only when `ThreeTeamMode` is in the mods folder (the module carries the
  code; the folder turns it on). So it can sit in both Switch builds, "TF.EX Switch" included.
- **Deleted:** `megamod/threeteam.hpp`, its Patch words and romfs entries (the yellow team banner `teamC2x_3t`); the megamod keeps
  TowerBall and 8 players. Team Deathmatch on the Switch goes back to plain two-team TDM, as on the PC with the mod.

## What the Switch already has for this

- FortRise custom game modes: `Fr_ModGameModes` (RegisterVersusGameMode: the next Modes value), `Fr_MatchSettings` (TeamMode / GoalScore
  / CanPlayThisMode of a custom mode), `Fr_RoundLogic` (a custom mode's RoundLogic). TF.EX's Netplay mode (Mode 11) runs on this today.
- `tfex/FrModes.cpp`: the Switch inlines TeamMode as `Mode == 5` in four MatchSettings helpers; under a custom team mode they already
  run as TDM. Three Team gets this for free.
- Runtime recolouring: `Texture2D.GetData` (the Switch has only the whole-texture overload; TF.EX's skins use a translate rule for it)
  and `Texture2D.FromStream`/SetData paths exist (the skin stream, the mod loader).
- Content mods from the SD card, sprite/texture registries, the archer roster (so gold/white recolours of mod archers have their art).
- 8 seats (the megamod's widened arrays) and the Switch's native wide mode (memory: wide mode is native, no WiderSet port).

## Steps (sequential; each ends with its check)

1. **The PC mod on current FortRise.** It targets 5.3.0; the Switch translates FortRise 5.5.0-beta.4. Built against 5.5.0-beta.4
   (2026-10-31: `-p:ModLoaderPath=` a folder with `.cache/fortrise-patched-5.5.0-beta.4/TowerFall.Patch.dll`): compiles, 0 warnings,
   no source change. FortRise 5.5.0-beta.4 (the official osx-x64 release) is installed in the Mac's TowerFall (5.3.5 kept as
   `FortRise-5.3.5.app` beside it, same Mods). On 5.5 the mod loads and all its patches apply (its log: WiderSet detected, 2 allegiance
   lookups redirected, 5 results-board coroutine bodies patched, the sensor-block lookup patched, 11 team data entries). **Runs (2026-10-31, 08:50): the 4-player self-test passes on 5.5** (team select
   SOLO|BLUE|YELLOW|RED, gold and white recolours 3/3, yellow wins the round, one results row per group, `SELFTEST done`).
   The earlier "exits right after loading" was not Steam ("API loaded no" is only breakpad's minidump line): FortRise 5.5.0-beta.4's
   ImGui internal mod ships its Mac library as `Internals/FortRise.ImGui/Unmanaged/osx/native/libcimgui.dylib`, while
   `ModAssemblyLoadContext.LoadUnmanaged` looks in `Unmanaged/osx-x64/`; nothing is extracted, the folder is never created, and
   `File.WriteAllText(<...>/osx-x64/libcimgui.dylib.sum)` throws DirectoryNotFoundException (TowerFall's error log: LOADING ERROR in
   ImGuiModule.OnInitialization). Local fix on the Mac: a copy at `Unmanaged/osx-x64/libcimgui.dylib`. An upstream FortRise issue
   (noted, not filed). A `steam_appid.txt` beside FortRise was also tried; it is harmless (FortRise writes its own).
   Note for step 2: the mod has more IL-level patches than the one transpiler: the OnPlayerDeath allegiance redirect and the results
   board's coroutine bodies are IL edits too (its log lines above); each needs a Switch-side form.
2. **Done, the translator part (2026-10-31, 24e98fe6):** HookGen reads patches registered in code (`harmony.Patch(AccessTools.
   [Declared]Method/PropertyGetter/Constructor(...), prefix: new HarmonyMethod(typeof(C), nameof(M)))`, IHarmony's or HarmonyLib's) as
   it reads `[HarmonyPatch]`; `translator hooks-scan <mod src> _ <cfg>` lists a mod's hooks. Teams+: all 16 patches found (+ the
   self-test's 4), the 3 transpilers reported "IL-level: no hook"; gen/ unchanged. So the mod's patches stay as they are (the
   attribute rewrite below is not needed). **The three IL edits, open:** rewriting them as before/after patches only half works.
   OnPlayerDeath and SensorBlock could become a flag (prefix/postfix on the method) plus a prefix on the getter they call
   (GetPlayerAllegiance, MatchTeams' indexer), but the Switch build inlines small getters (FrModes: TeamMode in four helpers), so the
   Switch would still need native hooks at those sites. The results board's `new int[2]` sits inside the coroutine with the code that
   indexes it: no before/after form short of copying the whole Sequence coroutine (many private fields). The Switch already fixes
   that one with a single instruction patch (threeteam.hpp PLabelAllocMov, `mov w8,#2` -> N). **Decided (the user, 2026-10-31):** keep the three transpilers
   on the PC; on the Switch each is a native patch (TFEX-DIFF style, S-native: "an IL edit of the game's code"), the label array's
   taken from threeteam.hpp. Also for step 3: VisualHooks reads Player's sprites through `AccessTools.FieldRefAccess` (reflection);
   Harmony's `___bodySprite` parameters (HookGen maps them) are the translatable form.
   Previous wording of this step:
   **Make the mod translatable, in the mod itself.** It is our own code, so the PC source changes where that is simpler than translator
   work, and stays a normal FortRise mod:
   - its 19 Harmony patches are imperative `harmony.Patch(AccessTools...)` calls; HookGen reads `[HarmonyPatch]` classes. Rewrite them
     as attribute patches (or teach HookGen the imperative form; attributes are less work and the PC is happy with either);
   - the one transpiler (SafetyHooks' team lookup in a coroutine's MoveNext) as a prefix/postfix;
   - `SelfTest.cs` stays PC-only (excluded from the unit list);
   - `Wide.cs` / `WiderSetApi.cs`: WiderSet is absent on the Switch; a rule maps "is the wide set on" to the Switch's own 8-seat / wide
     state (TFEX-DIFF S-native, the same stand-in TF.EX's WiderSetMenu gets).
   The PC self-test runs again after this step: the PC mod must stay what it was.
3. **In progress (2026-10-31, branch claude/fortrise-hair-sfx, not on master):** Teams+'s source is `vendor/teamplus` (a submodule of
   ../three-team-mod/pc-fortrise, file URL; ensure-vendor.sh inits it), its own translator compilation (`Workspace.ForMod`, files
   "teamplus/..."). 16 units (translate.toml "Teams+"), rules (overrides.toml "Teams+": IHarmony.Patch no-op, SelfTest off, WiderSet ->
   the Switch's wide mode, the two transpiler-only Loads). Translator additions it needed: a mod workspace beside TF.EX's and FortRise's
   (structs "Tp_", TF.EX's glue reachable, generic glue copies shared), reflected property setters, FindIndex on value lists, struct
   constructors assigning any expression, {x:X2}, number CompareTo, array == null as object, value maps with FNA struct keys, ref array
   __result in hooks (gen::ArrRef), SetData<Color> (skins::SetColors), hosted units without FortRise's Mod base. The module builds;
   Teams+'s 8 hook classes are left out of EnableAll (unit `enable_all = false`), nothing calls its code yet, so the build behaves as before.
   **Boot done (2026-10-31, branch):** FrModsBoot.cpp TeamsPlusBoot constructs ThreeTeamModule with its folder's registry as its
   context (glue SwitchMods.TeamsPlusRegistry / ContextRegistry, by identity) and enables its 7 patch classes; emulator (slot update,
   ThreeTeamMode on its SD): `[tfex] Teams+ ready ...`, `[teamplus] Teams+ constructed ...: its mode registered, 7 of 7 patch classes
   hooked`. megamod/threeteam.hpp deleted (with its romfs banner teamC2x_3t); the megamod's 8-player hooks on ProperlyAssigned and
   CreateTeamSelect are now shells on the generated hooks (one trampoline each; Teams+'s prefix first). localtest picks teams in any
   team mode (the old code handed them out). `localrun.sh 4 5` (plain TDM, Teams+ installed and inert): PASS.
   **Playable in the emulator (2026-10-31, branch):** FortRise's GAME MODE carousel (translated) offers TEAMS+; localrun 3 and 8
   TEAMS+ PASS (3 groups; 2/2/2 + 2 wolves, gold and white recolours); a round to its end with the results board, one row per
   group (localroundend; native patch 1 of 3: the board's P-label int[2] -> int[8], +0xbba264). Test lesson: every local run
   before this stopped behind VersusStart's two intro cards (each waits for confirm), so "level ran 30 s" proved the level's
   start only; the driver now presses through them. Teams+'s meta.json priority Lowest (TF.EX's GAME MODE patch stops the scroll
   at its Netplay mode: modes registered after it were unreachable, the PC too). Still open: SensorBlock's team colour for a
   wolf and OnPlayerDeath's wolf-vs-wolf typing (native patches 2, 3: to find), FortRise's VersusStart custom mode name under
   the tower name (patch_VersusStart, an IL patch: TEAMS+ shows no subtitle), the online smoke, the core build, then land.
   **Native patches done (2026-10-31):** OnPlayerDeath's two allegiance calls answered by AllegianceForKillType at their return addresses
   (+0xb38738 / +0xb3874c; emulator: wolf P8 by wolf P7 107 vs 106, no team kill; P4 by P1 0 vs 0, a team kill); SensorBlock's
   get_Item call (+0xb42fa4) answered by Teams3.TeamDataAllegiance (installed, not exercised: no sensor block in the test level).
   localrun 8 TEAMS+ localroundend PASS: five rows (BLUE P1+P4, RED, YELLOW, WOLF P7, WOLF P8). smoke lobby with Teams+ installed PASS
   (Netplay mode 12 against the page's 11: not in the synced state). Core build OK. Known gap: the round intro's TEAMS+ subtitle.
   Left after that:  the ThreeTeamMode folder's registry as its context -> Tp_Module::Ctor, its hook classes enabled; glue
   ModRegistry(name), IModuleContext.Registry = the context's registry), the three native patches (the label array: threeteam.hpp
   PLabelAllocMov 0xbba264; the sensor block and OnPlayerDeath's allegiance compare: to find), the inlined team checks the PC hooks
   rely on (three-team-mod OFFSETS.md: TeamMode as `cmp #5`, AmountOfAllegiance, TeamSelector.GetTargetX), then steps 4-5.
   Original step text:
   **Translate it.** Units for ThreeTeamModule (constructor: registers the mode and the patches), ThreeTeamGameMode, ThreeTeamRoundLogic,
   Teams3, Assets3, Recolor, the UI classes and the hook classes; rules for what the Switch lacks. The mod's entry point runs from
   the glue's loader when `ThreeTeamMode/meta.json` is on the card (as TF.EX's module is constructed after the content mods load).
   Expect the usual: overload pins, a few game members to RE (Player's sprites are named fields: headSprite / bowSprite / bodySprite),
   the Switch's inlined team tests (FrModes covers four; the team select's columns and the results board may inline more:
   docs/TEAM-MODES-RE.md lists them).
4. **FortRise's mode carousel on the Switch.** Offline, the GAME MODE button must offer custom modes: FortRise's
   `patch_VersusModeButton` is not translated yet (translate.toml's `VersusModeButton.currentIndex` rule: "ModeSelection keeps 0").
   Translate it, so "TEAMS+" appears with its icon (min. 3 archers).
5. **Switch over and delete.** Three Team on by default (the mod folder on the card), `megamod/threeteam.hpp` and its romfs entries
   deleted, the localtest driver picking the mode by name (PLAN-MEGAMOD step 4's "modes by name"). Check: one local run, 3 and 8
   players (1v1v1, 2v2v2 + 2 wolves): yellow sheets recoloured (log), the four groups' score slots (log), a round to its results.
   Plus smoke lobby (online unchanged).
6. **Console:** publish, then the mod folder (the release zip's contents) over FTP; the STATUS checklist names the log lines.

Later, separate (not in this plan): Three Team **online**. TF.EX plays only its Netplay mode; a three-team netplay mode needs TF.EX
support (groups beyond two in its team logic and TF.State's saved scores) and both sides having the mod. A TF.EX feature request
first, per "TF.EX is correct".

## Risks

- **Recolouring cost on the Switch:** Recolor.cs reads sheets from the atlas (GetData) and builds new textures at spawn. On the Switch
  the only GetData is the whole texture (the atlas is large): cache per sheet, and measure the first-spawn hitch.
- **Inlined team logic:** the Switch's AOT build inlines `TeamMode` in places the PC calls it (TEAM-MODES-RE.md). Each one under a custom
  mode is a TFEX-DIFF like FrModes. The Switch three-team copy already found most of them (three-team-mod OFFSETS.md): use that list.
- **Mode number:** FortRise gives custom modes values in load order (TF.EX's Netplay mode took 11). Three Team's value differs by what
  is installed; nothing may hard-code it.

## Open questions for the user

1. (Answered: its own repository.)
2. Whether the PC release (0.1.1 on FortRise 5.3) should be updated and posted to GameBanana once step 2 lands (the kit is ready).
