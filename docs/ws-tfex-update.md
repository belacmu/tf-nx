# Workstream tfex-update (2026-10-16)

**Status (2026-10-22): TF.EX's test mode on the Switch, 60 and 240 Hz, 0 sync-test mismatches** (joint batch with Integration B2, ws/tfex-test).
hw.txt `tfextest <TF.EX's test args>` (e.g. `tfextest LMS 0 1 42 2`) runs TF.EX's own dev-console `test` (TFCommands.LaunchTestMode, generated)
from the main menu: StartTestMode, SetTestMode, StartGame, NetplayManager.Init with ggrs_ffi's SyncTestSession (a rollback every frame). B2's
`hz 60|240` sets the session rate, and B2's gate drives the session (BeginTestSession). Found on the way:
- PrepareTestSession (B2): capture and TF.State's context before the level is built; without it the TreasureSpawner's Random wasn't restored.
- TF.State load gaps: TimeSpan.FromMinutes (AddChestsState's cache), a plain DynamicData Get (Lantern's homeChain).
- The DynamicData side table now has ConditionalWeakTable semantics: weak keys via Boehm disappearing links, slots reused. Before, it filled
  with the number of loads (POOL FULL at ~99 s at 240 Hz, then a mismatch); long lobby matches were exposed too.
- GetNameForSeat is TF.EX's own (the hand copy returned null: a test session's PlayerIndicator crashed); the gate runs a test session
  without synchronization (TF.EX: IsSynchronized() || Test).
Runs: tools/testmode.sh 60 / 240 (hidden): PASS, 0 mismatches over 300 s each.

**Status (2026-10-20, later): custom skins (item 11) on, both directions** (docs/research-fortrise-mods-on-switch.md section 7).
TF.EX's SkinStreamService and SkinOverlayService are generated and on. The Switch shows an opponent's streamed archer without the
mod, and streams its own mod archer to an opponent without it. Emulator, both directions against the browser: PASS (0 of 592, 0 of
596). The runtime stand-ins (MessagePack, SHA-256, base64, DEFLATE / inflate, texture rect reads) and the large lobby messages are
listed there.

**Status (2026-10-20): FortRise content-mod archers on the Switch** (docs/research-fortrise-mods-on-switch.md section 6). FortRise.Content's
archer loaders and FortRise's ModArchers / ArcherRegistry are translated from `vendor/fortrise`, and TF.EX's custom-archer code
(ArcherDataExtensions) is on. Mods load from `sd:/xplay/mods/<folder>/` after GameData.Load. Emulator: the lobby scenario with the mod
archer on both sides, against the browser, PASS (0 of 599 ticks differ). Translator additions found on the way: `in` parameters, C# 14
extension blocks, XML node loops, empty `[]` into collections, arrays in POD structs, `is { } x`, System.Xml vtable dispatch,
`[pin] slot`, `[instantiation]`, `TFEX_TRACE_UNITS` (method-entry trace for debugging, off), local functions placed before their first
use, `?.` over a model list, `int.TryParse` / `String.StartsWith|EndsWith|Contains(char)` (not in Mono).

**Status (2026-10-17, evening):** target **TF.EX v0.21.1**. Coverage **393 of 438 applicable**, 474/544 all (compile-checked; a
unit's excluded members count). This session owns the translator, bind.py / [pin] / tools/re, gen/ and binding.json; Integration A
(menus, lobby, roll call) and B (match and after) own [service], the runtime services and the hand code, and adopt the units.
- Units adopted or in adoption: NetplayButton / NetplayMenuButton / CompactCoOpButton (A), RollcallReconcile (A, on by default),
  RollCallElementPatch whole (A's switch-over), NetplayEntry (A), MainMenuStale (A), PauseMenuPatch / HUDPatch / MatchEndChoices,
  MapScenePatch functions, SessionPatch / PlayerPatch functions (B: their hand hooks call the generated functions).
- Prepared, waiting for a rule: LightPauseMenu + LevelPausing (B: LightPauseMenu.Pressed), MapScenePatch.StartSession_Prefix
  (IArcherService.ApplyToGame / CompactSeatsToHandles). MatchmakingService whole class: compiles against stubs, held until the
  switch-over is planned with A.
- Tools: tools/tfex/new-tag.sh (the TF.EX update routine), translator/test/hook-collisions.py (generated vs hand hook targets),
  hook-callers.py (how game code reaches each target: direct calls, delegates, inlined).
- Fixed today besides units: unscanned managed statics in generated code (GC roots), the Scene.Remove<T> mis-binding, the exlaunch
  hook pool (0x10000; with B), the own-lobby self-test.
Details below ("Phase 4", "v0.21.1", "Core multiplayer path").

### Menus track (Phase 2, rows 1-5): TF.EX's generated screens as the real path (hw.txt `genmenus 1`, default OFF, build-checked only)
With genmenus on, tfex/NetplayScreens.cpp's CallStateFunc hook hands every netplay state to TF.EX's generated
MainMenu_CallStateFunc, and each menu frame to MainMenu_Update_Postfix (gen/tfexgen_NetplayMenus.cpp; LoaderPatch on with it).
Screens: NetplaySelect, PrivateSelect, LobbyBrowser (list, refresh, join), LobbyBuilder (create / edit), PrivateJoinCode (TF.EX's
JoinCodeEntry), QuickPlaySearch. TFEX-DIFFs kept by hand: the join code comes from the Switch's software keyboard
(NetplayScreens::KeyboardCode = TF.EX's clipboard paste; also the harness's test code), the PC keyboard / mouse read as nothing
pressed (translator), no COPY CODE guide (no clipboard), LobbyCodeFrame shows a private lobby's code on the roll call.
Emulator status (before the test pause): lobby browser -> builder -> TF.EX creates the lobby -> roll call worked; the join crash
(WiderSet StrMap) is fixed since; nothing after the roll call verified.

Hand code genmenus retires once tested (tfex/NetplayScreens.cpp, ~706 lines; line numbers at 2026-10-17 menus track 1):
| screen | hand code | lines |
|---|---|---|
| NetplaySelect | CreateNetplaySelect / Destroy + the hand NetplayMenuButton / VersusSelectBanner glue (66-174) | ~110 |
| LobbyBrowser | 190-313 (rows, refresh, OnBrowse, BrowserFrame) + BladeConfirmHook / JoinRow (599-629) | ~155 |
| LobbyBuilder | 314-432 (options, BuilderFrame) + OptionsUpdateHook (630-650) + EditLobby | ~150 |
| PrivateSelect | 433-450 | ~18 |
| PrivateJoinCode | 451-505 minus ReadJoinCode / KeyboardCode (kept: the keyboard TFEX-DIFF) | ~35 |
| QuickPlaySearch | 521-563 | ~43 |
| dispatch | Dispatch, IsCustom, the hand branch of the CallStateFunc hook, Frame's hand branch | ~40 |
Kept: LobbyCodeFrame, ReadJoinCode / KeyboardCode, the CallStateFunc hook (routing only), Frame. About 550 of 706 lines go.
In A's tfex/MainMenuPatch.cpp, the hand LobbyBuilder START (CreateLobby, AddWiderSetLobbyMod, OnCreateSuccess/Fail, ~80 lines) and
JoinPrivateFromScreen become unused too (TF.EX's builder / SubmitJoinCode): A's call.
- **Late 2026-10-17 (stop for reassessment):** NETPLAY globe icon (romfs PNG); MatchmakingService regenerated for A (genmm, ping
  stats) with the runtime fixes (ManagedUtf8 length, model temporaries, NewValStatic for fields); NetplayPreferences.Name / Server
  rules (the generated MatchmakingService sent "PLAYER"). genmenus (hw.txt, off): NetplayScreens hands CallStateFunc and each menu
  frame to TF.EX's generated MainMenu_CallStateFunc / MainMenu_Update_Postfix (LoaderPatch on with it); in Ryujinx it creates the
  lobby with TF.EX's code and reaches roll call. NetplayManager step (a): unit written, parked in translate.toml until B's rules.

TFEX-BASE.md section 8 step 3: the method splitter, the manifest of the existing port, the overrides (ledger as data) and the
update report. Branch `ws/tfex-update` (from `ws/tfex-base`). Everything is in `tools/tfex/` (README.md there: how to run it).

## Result

`tools/tfex/update.sh v0.19.2 v0.21.0` → `tools/tfex/reports/v0.19.2-v0.21.0.md` in ~25 s cold, 2-3 s cached.

- **Section 6 check: 54 of 54 items in the expected class** (`tools/tfex/expect/v0.19.2-v0.21.0.toml` is section 6 written as
  data; the report's section 8 re-runs the check). Wrong labels from the first rules, fixed in `rules.toml`:
  `RequestNetplayEntry` (the DLC gate: C+D, and the NoRandom clear is in `ResolveNetplayEntry`, not `RequestNetplayEntry`),
  `Lobby.IsReportEnabled` (labelled D by the MatchReport rule), the failed-connection messages (labelled B), the relay helpers,
  `OnLevelLoadFinish` (A+B+D; only adds a MatchReport call: now D).
- 174 changed members (153 real, 21 syntax-only). A 32, B 37, C 31, D 46, E 28, unlabelled 0.
  The splitter's normalizer catches section 6's "E: a fifth of the lines are C# 12 syntax" (21 members: `new()`, `[]`, `[.. x]`).
- Against the manifest: 13 A, 4 B, 4 C members are **stale** (ported code behind), 13+2 A are new/unported, the rest skipped or
  overrides to review (GGRSFFI, scenarios, NetplayRoundLogic, TFCommands).
- **New "lands near"** for unported members: our port of their upstream callers (e.g. `Lobby.SessionPlayers` → our
  `ResolveArchers`, `ResolveArcher`, `MaybeNativeStart`; `PlayerPatch.RefreshSprites` → `xplay.cpp LevelUpdateHook`).
- Patch targets: 9 new, `TFGame.Draw` the only one missing on 1.4.1.6 (MatchReport's finalizer): same as section 6.
- B to-do lists the four new message types, five Lobby fields, `room_peer_id`, `/relay/room/`, `/relay/ping_measurement/`.

Sanity runs (`reports/v0.19.2-v0.20.0.md`, `v0.20.0-v0.20.1.md`, `v0.20.1-v0.21.0.md`): no unlabelled change, per-step counts add up
to the full range (A 12 + 1 + 19 = 32). A report from a tag newer than the port lists the older stale entries under "Still behind".

**Found beyond section 6:** v0.20.1 (#273 "prevent a previous input kept from manual quit into a next round") adds
`_inputService.ResetPolledInput()` to `NetplayManager.Init`. It's labelled A (input state at session start).

## Manifest and overrides

Made by reading our code against TF.EX v0.19.2 in three parts (ui: TF.EX.Patchs + CustomComponent; svc: Domain/Core/Common/API;
state: TF.State.Patchs, level generation, RNG). 520 manifest entries (most member-level, some type/file-level, each with a
confidence), 159 skips, 88 replaces, 4 name mappings; hashes filled at v0.19.2. `manifest.py check v0.19.2`: one dead key
(`UpnpService.cs`, commented out upstream: kept). `manifest.py coverage v0.19.2` over network + TF.State.Patchs + level/RNG:
3379 members: 1107 ported, 594 replaced, 613 skipped, **1065 unaccounted**. These are mostly section 4's "to undo" list: skins,
series, Notification, changelog, LightPauseMenu, InputDelayAdvisor, spectators, and variant-only enemies. They show up as work when they change.

Confidence: the `at` locations are good for tfex/*.cpp (TF.EX names kept) and the TF.State patches (from TFSTATE-AUDIT.md);
xplay.cpp's start/end path is medium; a few UI look-alikes are low (`conf = "low"`). Coverage over precision as asked.

## Limits

- Hash = syntax. A change elsewhere that changes what a ported member *means* is not seen on that member. Example: the new
  `InitVersusButtons` unlock patch changes what our `Tower::Select` (mapped to `GetRandomVersusTower`, unchanged) must do. The report
  shows it as a new A item whose "lands near" names `Tower::Select`, so a human still has to read the A items.
- The normalizer drops the type of `new T(..)` only where C# can target-type it. A type change there would read as syntax-only.
- Classes come from path and token rules. A member that mixes classes gets several labels (A+B); review those.

## For the manager: TFEX-BASE.md updates

1. Section 6, A table: add `NetplayManager.Init` `ResetPolledInput()` (v0.20.1 #273). Note that `RequestNetplayEntry` holds
   the DLC gate and `ResolveNetplayEntry` the NoRandom clear.
2. Section 7: the overrides file is `tools/tfex/overrides.toml` (not `tfex-overrides.toml`). The command is
   `tools/tfex/update.sh <old> <new> [out]`. It also needs the old tag, because a port can be several tags behind. Manifest keys are
   `"<path under vendor/tfex/src> <Type>.<Member>"` (e.g. `"network/TF.EX.Patchs/Scene/MapScene.cs MapScenePatch.InitVersusButtons_Prefix"`).
3. Section 1 rule 4: "port each hunk to its mapped place" → run `tools/tfex/update.sh`, port, then `manifest.py fill`.
   Section 8 step 3: done.
4. Ledger decisions the mapping needed (now in overrides.toml, please confirm or move to work):
   - custom archer mods (`ArcherDataExtensions` Exists/GetCustomArcherId…), custom variants (`ContainsCustomVariant`,
     `CustomVariantTitles`), `GameFilesIntegrity`/`GameFilesManifest`: skipped as S-native. There's no ledger row for them, and
     section 2 lists GameFilesIntegrity as "–". Add rows or delete the skips.
   - `Session` GotoNextRound / CreateResults / PlayWinnerVictoryMusic / StartRound skipped as S-rollback ("TF.State doesn't
     snapshot VersusMatchResults; the memory copy does"). Not checked on hardware.
   - `TeamReviverPatch` as replace S-mode (`pcgameplay.cpp`), though section 2 lists TeamReviver under "Missing".
   - `PlayerIndicatorStatePatch` (TF.EX's indicator doesn't bob/flash), `Arrow.DoWrapRender`, `MatchVariants.Randomize`,
     `LevelLoaderXMLStatePatch.Reset`, `LastManStanding.OnLevelLoadFinish`: skipped (cosmetic / menu / S-rollback).
5. Gaps the mapping found (no entry, so they show as work; candidates for docs/TFSTATE-AUDIT.md / section 2):
   - **Variants are reachable in netplay** (`tfex/NetplayManager.cpp` applies the lobby's variants). The variant-gated TF.State
     patches are then needed: DarkPortalsVariantSequence (also spawns Bat/Slime), `Player.DropArrow` (ClumsyArchers),
     `OrbLogic.DoOffsetWorldVariant` / `StartScroll`, TreasureChest (bottomless), PlayerCorpse ghost counter, PlayerGhost.
     TFSTATE-AUDIT.md's "not needed for a default match" is right only while no variant is on.
   - `OrbPickupPatch` (settings guard around `OrbPickup.OnPlayerCollide`) is not hooked.
   - `WhiteArcherPrison.Spawn`: TF.EX replaces the prison with `WhiteArcherPrisonBroken` in netplay; nothing in our code. Possible desync.
   - `PlayerPatch.Player_Update_Postfix` kills a disconnected player (and did the DoWrapRender). Not mapped.
   - `GetArrowShufflePickups` has the settings guard but no RegisterRng (GAMEPLAY-RNG-PORT A27).
   - `ChaliceGhostPatch`: no ChaliceGhost type on 1.4.1.6.
6. Section 2's tables could point at the manifest instead of repeating locations (one source).

## Translator (phase 2, 2026-10-16)

TFEX-BASE.md section 7 "With the translator", section 8 step 5. `tools/tfex/translate.sh` (README.md "Translator") turns TF.EX's C#
into C++ for the Switch in `tfex/gen/` (generated only). Built on Roslyn's semantic model over TF.EX's whole source compiled against
the PC TowerFall.exe / FNA / FortRise (561 files, 166 binding errors, none in translated code), and tools/re for the Switch side.

**Stage 1, models (done).** `Models/WebSocket/**` (74 classes) -> `tfexgen_Models.{hpp,cpp}`: structs with fixed capacities
(translate.toml) and JSON read/write. Oracle: TF.EX's own model classes compiled with MessagePack (`test/oracle`), doing what TF.EX does
with server JSON. Samples: 45 real messages captured from the local tfex-server (`test/capture.mjs`) + 7 synthetic v0.21.0 ones
(Series, RelayedPairs, MatchPlayers, KickedFromLobby, escapes). **Generated JSON = TF.EX's byte for byte on 50/50.** The hand-written
`tfex/Models.*` read every real lobby like TF.EX (22/22); on the synthetic ones: `MapId` absent -> 0 (TF.EX -1), `Series` dropped (v1).
Found on the way: TF.EX sends empty messages as `{"KeepAlive":[]}` (array form), and MessagePack escapes `\n` (tfex/Json.cpp writes `\u000a`;
the generated code has its own writer).

**Stage 2, static helpers (done).** `ConnectionIcon.cs` and `MainMenuPatch.RenderBannerWithDots` (v0.21.0) -> tfexgen_ConnectionIcon,
tfexgen_MainMenuBanner, compiled and linked; hw.txt `gendemo 1` draws them from a MainMenu.Render postfix through translated glue
(`translator/glue/Demo.cs`: SpriteBatch.Begin with the UI camera, as TF.EX's postfix). Ryujinx: `docs/ws-tfex-update/stage2-gendemo.png`.
Iterators (`yield return` tuples) became callbacks; game statics read at rtypes' storage address (first run crashed reading them as
Game.hpp-style slots).

**Stage 3, Notification (done).** `Notification.cs` -> tfexgen_Notification: a real Monocle.Entity subclass (MakeSubclass from the bound
base type, overrides checked against and placed in the bound vtable slots, statics block = the class constructor, 5 lambdas and a
method group as game delegates, LINQ chains as internal iteration into snapshots, type patterns / switch, DynamicData by name).
**Acceptance: `tools/re/notif-test.sh "gennotiftest 200"` behaves as the hand port** (slide-in -130 -> 95, 250 frames, slide-out,
removal, second toast at -120 -> 100, full GC, `is Entity`): `docs/ws-tfex-update/stage3-generated-notification.png`. Every game member
bound mechanically equals tfex/Game.hpp's hand-found value. Not run: the in-match (Level, rollback) variant.

Constructs that still needed a hand rule (tfex/gen/tfexgen_report.txt):
| rule | why |
|---|---|
| `Scene.Add<T>` -> glue `Rewrites.SceneAdd` (C#) | generic method: no instantiation over a TF.EX class in the binary (subclass row 6) |
| `Notification.CenterX` -> glue `Ledger.NotificationCenterX` + `[Cpp] MainMenuPatch::IsWide()` | ledger S-wide (DI WiderSet API) |
| pin `SpriteBatch.Begin` (7 args) | the Switch's FNA keeps one Begin body; checked at a Monocle call site |
| pin `Dictionary<int, Layer>.get_Item` | tools/re classmeta.records crashes on that instance (below) |
| pin `Layer.toAdd` (+0x60) | private field: not in BRUTE's reflection (subclass row 16) |
Mechanical now (were MAP rows in docs/ws-tfex-subclass.md): rows 1-5, 7-15, 17, 19 (strings built with String.InternalAllocateStr), 20; row 21 (exceptions) has no case in Notification.cs.

**Stage 4, coverage (`reports/translator-coverage-v0.21.0.md`).** Today 158 of 434 methods (36 %), 20 % of lines of TF.EX.Patchs +
CustomComponent translate. 40 of 128 Harmony patch bodies translate, but hook registration (prefix/postfix) isn't generated yet.
What blocks the rest is mostly not C#: the boundary to TF.EX's services (ServiceCollections.Resolve*, IMatchmakingService /
INetplayManager calls, ~150 methods), Lobby/Player properties (the stage-1 structs aren't wired into code units yet), `Nullable<T>` and
`?.` on value types (~90), then game members missing on the Switch (~100: XGamepadInput, FortRise additions, inlined bodies).

Next (in order of payoff): a service map (interface method -> the hand-ported C++ service, as a table in translate.toml), code units
using the generated models, Nullable/`??`, Harmony patch registration (prefix-can-skip / postfix / `ref __result`), then try/catch
(exlaunch has no exceptions: a rule per site).

For the manager:
- `tfex/Notification.cpp` (hand) defines `brute::MakeSubclass` / `NewDelegate`, which the generated class uses: platform code living in
  a component file. Move it to a platform file before the hand Notification is retired.
- tools/re bug: `classmeta.records('System.Collections.Generic.Dictionary`2[System.Int32, Monocle.Layer]')` raises on a pre-index operand
  (`[sp, #0x18]!`, classmeta.py:123); bind.py records it per member instead of failing.
- The generated Notification is behind hw.txt `gennotiftest`; nothing calls it otherwise. Swapping the real call sites (MainMenuPatch's
  create-failed toast) is a one-line change in a file other sessions own.
- xplay.cpp: +3 lines (include tfex/gen/tfexgen_glue.hpp, `tfex::gen::HwLine(line)` in the hw.txt parser, `tfex::gen::Install()`).

## Translator phase 3 (2026-10-16/17)

**Coverage: 325 of 542 methods (60 %), 3329 of 9326 lines** of TF.EX.Patchs + CustomComponent translate at v0.21.0, compile-checked
(`tools/tfex/translate.sh coverage v0.21.0` -> `tools/tfex/reports/translator-coverage-v0.21.0.md`). Phase 2 ended at 159/434; the
phase start (honest, compile-checked) was 261/548. The total dropped to 542 because positional-record accessors are storage, not
methods. Target was 70 % (380): stopped at 60 % on the user's call. Steps: 261 -> 277 (arrays, initializers, closures, model locals) ->
301 (delegates in static units, input service) -> 325 (collection/switch expressions, Task.Run, strings, services, compile fixes).

### Goal 3: Harmony hooks, proven end to end
`translator/HookGen.cs`: for a unit with `hooks = true` in translate.toml, every [HarmonyPatch] method becomes part of one exlaunch
trampoline per target (`tfexgen_hooks.cpp`). It handles prefix (a bool prefix returning false skips the original), postfix, finalizer
(no exceptions on the Switch: `__exception` is null), `ref __result`, `__state` (one per class and target), `__instance`, `___field`,
target parameters by name (by reference when the patch takes `ref`), [HarmonyPriority], and MethodType Getter/Setter/Constructor.
Several patch classes on one target share its hook (a per-class on-flag each). Transpilers and reverse patches are reported, not
generated. hw.txt `genpatch <Class>` installs a class's hooks at boot, on top of any hand hook on the same function (exlaunch relocates
the hand hook's branch: generated -> hand -> original). Without the flag nothing generated is installed. Each hook logs its first 8 calls.

Proof: `ScreenTitlePatch` (Entity/ScreenTitle.cs). Run `tools/tfex/translator/test/gen-demo.sh -s 150 -w
'\[ui\] press Right \(LOBBIES\)' "lobbyui 3" "genpatch ScreenTitlePatch"`. The generated ChangeState prefix handles NetplaySelect (62)
and LobbyBrowser (59): it sets `targetTexture` to menuTitles/fight and skips the original, so the hand NetplayScreens hook under it does
not run for those states. It runs for every vanilla state ("original runs" in the log). Screenshot: `ws-tfex-update/stage4-genpatch-screentitle.png`.
Also generated: VersusCoinButtonPatch, ReadyBannerPatch (chained over the hand GetReadyState hook).

### Goals 1-2: what the translator does now (new in phase 3)
- Classes: abstract base slots (bind.py counts the vtable to the interface table; abstract slots from tools/re vtable.py; EnsureClass
  checks them null: MenuItem subclasses), get-only auto-properties set in constructors, positional records (properties + primary
  constructor), object initializers on game classes and ours, `[IgnoreMember]` model auto-properties as local struct state
  (Lobby.CanJoin; never in JSON; models-test still 43/43 + 7/7).
- Delegates: lambdas capturing locals become closures (captured values in a managed object[], the delegate's target), when the
  captures never change after the lambda is made (else reported). Delegates in static units get a host class per unit for their method
  records (`gen::HostClass`) and a managed target. Up to 64 delegate sites per class.
- Arrays: `new T[n]`, initializers, `params`, collection expressions -> managed arrays through BRUTE's NewArray (+0x6110, found by
  tfex-subclass): `gen::NewArr`; `bind::rt_StringArray` for C++ callers. `[a, b]` for List<T> and its interfaces -> List ctor + Add.
  ToList/ToArray whose result is handed on (argument, field, return) -> a managed List/array; consumed locally -> a snapshot as before.
- Expressions: switch expressions with relational/or/and/discard patterns, tuple deconstruction statements, String
  Join/Equals/Split/ToUpper/ToLower as gen:: helpers (Mono's overloads differ from .NET's: tfex-subclass's LobbyInfos crash), `Task` -> void.
- `Task.Run(...)` -> the body inline (S-async: the Switch's services never block; their callbacks come on the game thread). Only when
  nothing follows an await; 4 methods still have code after one.
- Service map (overrides.toml [service]): IInputService.GetInputIndex/GetLocalPlayerInputIndex, IMatchmakingService
  LeaveLobby/UpdatePlayer/RunOnGameThread (TF.EX Actions kept alive in uncollectable memory until called) / CanEditLobbySettings,
  replay/input-displayer/mod APIs (D, S-native), ILogger, OperatingSystem, GGRSFFI (todo: 0).
- Ledger / hand rules (each in tfexgen_report.txt): Notification's `new Canvas(w, h)` -> `tfex::Notification::SharedCanvas` (S-rollback,
  tfex-subclass's request; a weak symbol until ws/tfex-subclass merges, falling back to the creation). The `[[rewrite]] new = ... in = ...`
  form is new. VersusGraphics.GetVersusGraphics (decompiled Span code) is replaced by the same list built with Add (behaviour unchanged).
- Safety: an unresolved game member is declared only for the coverage compile. A use in the module is a compile error, not a call to
  Base() + 0. A constructor with no body now blocks its method (before, it was counted as translated).

### Goal 4: PC -> Switch
- Game enums by name: bind.py reads each literal's value on the Switch. Generated code keeps game enum values as the Switch's:
  literals map by name (MenuState.VersusOptions 7 -> 8), conversions to and from ints and TF.EX enums map the PC's numbers both ways
  (MenuStateExtensions.ToDomainModel), and `Enum.IsDefined` checks the Switch's literals. A value neither side has (TF.EX's 59..66)
  passes unchanged. Known loss: the Switch's VersusSub (7, the STANDARD/WIDE screen) has no PC literal and reads as the domain's
  VersusOptions.
- `GameData.VersusTowers[mapId]` -> the tower whose LevelData.ID is mapId (the PC's list index is the ID; the Switch's order differs,
  as tfex/NetplayManager.cpp SelectStandard already knew). Fixed LobbyPanel's map icon and title.
- Pins added: ScreenTitle.targetTexture +0x80.

What remains, by root cause (methods each blocks, its callers included; from the coverage dump):
| cause | methods | what it needs |
|---|---|---|
| generated C++ doesn't compile | 59 direct | snapshots vs game lists in a few shapes, model collections returned across units (`auto`), TF.EX value types |
| game members missing on the Switch | 42 | PC-only input (XGamepadInput, MInput.XGamepadData, PlayerInput.MenuAlt2Check/Alt2Icon), private fields not in BRUTE's reflection (Variant.value/playerValues, RollcallElement.state, PauseMenu.menuType: pins), SDL3 clipboard, HarmonyLib.AccessTools |
| TF.EX value types (`struct Input`, `Vector2f`) | ~40 | a struct-unit kind (C++ POD + functions over `T&`); blocks the PC input patches |
| LINQ shapes | 18 | IEnumerable sources from game generics (GetAll<T>), ToDictionary, non-lambda arguments |
| DynamicData on unknown objects | 18 | the member's owner type at the call site |
| async methods (`await` outside Task.Run) | 23 | a rule per site (most await a service call that doesn't block here) |
| `Invalid` | 14 | members the reference TowerFall.exe/FortRise lack (FortRise additions): PC->Switch map or skip |

### For the manager (merge notes)
- Ready to merge: ws/tfex-update up to fb47c70. The module builds. tfex-subclass merged it into ws/tfex-subclass and uses the generated
  VersusSelectBanner, LobbyPanel and LobbyInfos there (tested in Ryujinx; the map-icon fix fb47c70 is waiting for their retest).
- Ryujinx tests here (private slot `update`): genpatch ScreenTitlePatch (above); `genbanner 120` (glue/BannerTest.cs): a generated
  VersusSelectBanner from translated C# with a delegate, a params string[], a static lambda and a closure
  (`ws-tfex-update/genbanner-versusselectbanner.png`, `genbanner-static-closure.png`).
- Promised to tfex-lockstep (not done): GameplayLayerPatch.RenderPings/UpdatePingAlpha/DrawPingLine through their
  MatchmakingService::GetPingTo/GetConnectionQualityTo, ConnectionQualities::ToColor, PingStats (contract in their message; needs an
  adapter from ws::Player to models::Player by RoomPeerId). InputDelayAdvisor stays unported: it needs a Switch design for its PC input.
- The vendor/tfex submodule checkout had drifted to an old commit in this worktree. The translator reads by tag (git archive), so
  output was unaffected; restored to the committed v0.21.0.
- xplay.cpp: still the same +3 lines.

### Tests run for phase 3 (Ryujinx slot `update`) and what was NOT tested (hardware checklist)
Run:
- Menu walk `lobbyui 3` + `genpatch ScreenTitlePatch`: FIGHT! title on NetplaySelect/LobbyBrowser from the generated prefix; vanilla states pass through to the hand hook.
- `genbanner 120`: generated VersusSelectBanner from translated C#, with a class delegate, params string[], a static-unit lambda and a closure.
- `lobbyui 3` + `genpanel 1` (fb47c70): generated LobbyPanels for maps 0 and 3 show SACRED GROUND / FLIGHT, their icons, mode and length
  (`ws-tfex-update/genpanel-lobbypanel-maps-0-3.png`). Title and icon come from the same tower lookup; the icon was not compared with
  the game's own map select screen.
- Host: models-test (43/43 + 7/7), module build, compile-checked coverage.
Not tested:
- A lobby match with `notif 1` and genpatch ScreenTitle/VersusCoinButton/ReadyBanner on. tools/lobby-vs-browser.sh failed four times
  in this checkout, including the baseline without these flags: the page never joins (it ends in QUICK PLAY's search, also when driven
  by hand after it connected). The generated hooks installed and ran in the menus of those runs. The branch is 32 commits behind master.
- VersusCoinButtonPatch and ReadyBannerPatch effects on screen (their screens need a lobby or a second player); the static-unit
  lambda without a closure (a host-class instance as target, fb47c70); LobbyInfos selection tweens (closures) beyond tfex-subclass's
  browser run.
- Nothing on real hardware.
- After merging master (682bb7f) and with LVB_PREKEYS=c, one more run (vsstop 600) also didn't join. The page reached its online menu
  only at t+127 s on a busy Mac, after the harness's fixed key time, and sat on QUICK PLAY untouched. The manager runs the lobby match
  on ws/tfex-base.

### Next steps (open at the wrap-up, 2026-10-17)
1. RenderPings for tfex-lockstep (not started): add a GameplayLayerPatch unit with `hooks = true`. Map IMatchmakingService.GetPingTo /
   GetConnectionQualityTo in overrides.toml [service] to adapters in tfexgen_services.cpp. The adapters find the models::Player whose
   RoomPeerId matches the ws::Player in GetOwnLobby().Players and call MatchmakingService::GetPingTo/GetConnectionQualityTo (-1 = null),
   declared weak until ws/tfex-lockstep merges. Map ConnectionQualities.ToColor to `tfex::ConnectionQualities::ToColor` (packed RGBA).
   Map PingStats to `struct tfex::PingStats {rtt, spike, loss_percent, samples}`.
2. A lobby match with genpatch ScreenTitlePatch/VersusCoinButtonPatch/ReadyBannerPatch + notif 1 (never ran here: the harness timing; the
   manager ran one on ws/tfex-base).
3. Coverage past 60 %: in this order: TF.EX value types (struct units: Input, Vector2f), the compile-only failures, pins for private
   fields (Variant.value/playerValues, RollcallElement.state, PauseMenu.menuType), DynamicData owner types, async rules.

## Phase 4 (2026-10-17)

### Step 1: RenderPings (GameplayLayerPatch), generated
v0.21.0's RenderPings is not what the phase-3 plan assumed: in a match it reads `INetplayManager.GetNetworkStatsPerSeat()` (GGRS
ping per remote seat), `IsRelayedWith`, the archer's color and `ConnectionQualities.GetQualityByLatency`; `GetPingTo` /
`GetConnectionQualityTo` are only the lobby's (MainMenu.cs, the hand port tfex/RollcallPings.cpp). Both are mapped now.
New in the translator:
- **TF.EX value types** (translator/StructsGen.cs): every TF.EX struct that is plain data (numbers, enums, FNA value types, other
  such structs; auto-properties by their name) is a C++ POD in `tfexgen_structs.hpp` (`tfex::gen::st`): NetworkStats, PingStats,
  Input, Vector2f (both), RightStick, ... Field and auto-property reads/writes, `new T()` / `new T { a = x }`. A struct's methods
  and operators go to its unit (`self` by value; `op_GreaterThan(a, b)`): written, not yet exercised by a module unit.
- **Dictionaries over a TF.EX value type** (`IReadOnlyDictionary<int, NetworkStats>`): `gen::ValMap<K, V>*` (code_rt): Count, Keys,
  Values, indexer, ContainsKey, TryGetValue, LINQ source.
- **foreach over a lazy LINQ chain** (`perSeat.Keys.OrderBy(...)`, Where, Select ...): the body is the sequence's visitor
  (break/continue map as for iterators; a return inside is still reported).
- uint/ushort/sbyte -> string; `Rectangle.Intersects`.
Service map (overrides.toml): `INetplayManager.GetNetworkStats(PerSeat)` -> tfexgen_services.cpp over xplay.cpp's GGRS session
(`tfx_ping_ms` per remote handle; send queue, kbps, frames behind: 0); `IMatchmakingService.GetPingTo` / `GetConnectionQualityTo`
-> MatchmakingService by RoomPeerId (were fixed "todo" values); `SpectatorInputDisplay.*` no-op (spectators: todo).
`VersusModeExtensions.IsNetplay` was the menu flag only (tfex::IsNetplay, off during a match): now also true while a GGRS session
exists (`svc::InSession`), as TF.EX's VersusMatchSettings.Mode stays Netplay in a match. Not `rnd::Capture`: hw.txt `rngcapture 1`
(on the console) forces that on in the menus. This also changes the generated ReadyBannerPatch / VersusCoinButtonPatch (only while a
session exists). Note: `INetplayManager.IsInit/IsSynchronized/IsServerMode` still map to `NetplayActive()` = rnd::Capture, which
`rngcapture 1` forces on: a candidate for InSession too (services piece).
xplay.cpp: +1 line (`NpSessionForStats`, the GGRS session, local seat and player count for the services). Now +4 in all.

Tests:
- Ryujinx (slot `update`), lobby-vs-browser, vsstop 600, LVB_PREKEYS=c, hw.txt + `notif 1`, `genpatch GameplayLayerPatch`,
  `ScreenTitlePatch`, `VersusCoinButtonPatch`, `ReadyBannerPatch`: two runs, both joined and synchronized, **0 differing ticks of 599**
  each. Run 1 (before the IsNetplay fix): the hook ran every frame, drew nothing. Run 2: "P2 : 429 MS" top left in P2's color, the
  quality's red and the relay/direct icon, every frame of the match. 429 ms is GGRS's ping between the emulator and a headless page on
  a busy Mac, not checked against the page's own number.
- Host: module build, models-test 43/43 + 7/7.
Not tested (hardware checklist):
- On the console: the ping line's value and color against the browser player's own TF.EX overlay; the fade when an archer or a
  flying arrow is under it (UpdatePingAlpha); 3-4 player lines; the spectator line (spectators not ported).
- The lobby's GetPingTo / GetConnectionQualityTo through the generated path (the lobby uses the hand RollcallPings port; no generated
  caller is installed yet).
- ReadyBanner / VersusCoinButton patch effects on screen: in these runs their hooks were installed but never called (the scripted
  Switch host, lobbyscript 1, never shows the roll call's ready banner or the coin button screen).

### Step 2: the lobby match with genpatches + notif 1
The two runs above are that match (ScreenTitle/VersusCoinButton/ReadyBanner/GameplayLayer genpatches + notif 1): the page joined at
t+68/73 s, synchronized, in sync to vsstop. ScreenTitle's ChangeState/ctor hooks ran (vanilla states pass through); the generated
Notification class was made (`[notif] ... (generated, tfex/gen) : Monocle.Entity made`), but no toast was triggered in this flow.
Still needs a hand-driven or hardware lobby: the ready banner and coin button with the generated hooks, a notification shown.

### Step 3: coverage 311 -> 382 (translator) -> 401 (with master's pins and services)
**The old numbers were inflated.** compile-gen never wrote `tfexgen_structs.hpp` into its folder, and attribute-errors.py dropped
`fatal error` lines and errors before a unit's first method marker, so whole units that didn't compile still counted. Both fixed
(Coverage.cs writes the structs header; an error outside any method marks the unit). Honest baseline after step 1: 311/542 (phase 3's
325 was the same kind of count). Every number below is compile-checked.

Translator (each a general rule, not a per-method patch):
- IEnumerable<T> returned by TF.EX code -> a snapshot (`gen::Snap<T>`, SnapCap 64; C#'s lazy sequence evaluated at the return).
- Model collections: a method returning a model's list gets the list's C++ type (`auto` can't cross units); model lists tracked through
  indexers, element picks and locals (`series.Sides[side]`); model lists assigned from a managed collection are copied
  (`gen::SetModelList`); `++` / compound assignment on model scalars.
- Snapshot locals (`var x = ....ToList()`) stay snapshots only while just enumerated; any List use makes them the managed List.
- Collection interfaces over game objects (IReadOnlyList/IList/ICollection: Count, indexer, foreach, LINQ) through the List<T>
  instantiation or T[] at run time (`gen::IList_`; anything else is fatal).
- Enum.TryParse<E>, Array.IndexOf, double/float -> string (`gen::FromDouble`: integral as integer, else %.15g/%.17g; exponents print
  `e+NN` where C# prints `E+NN`), BitConverter bit casts, Vector2.LengthSquared, `_ = x`, Dictionary Keys/Values as LINQ sources,
  IEnumerable fields holding a List, corlib type tests and constructors (System.Random), static array initializers in class units.
- DynamicData / Traverse through locals and parameters (owner type from the initializer, or from every call site of the method).
- Delegate Invoke through vtable slot 0x58 for instantiations the game never compiled (Func<Subtexture>, Func<an interface of ours>);
  bound instantiations are called as before. Not run yet with arguments.
- `Task.Run` with statement awaits: inlined, the continuation runs right after the request is issued (S-async); an awaited result
  still blocks.
- Fixes: deconstruction targets were declared twice; static fields initialized by a method call came before its declaration; a static
  auto-property in a class unit was emitted twice; `?.` on a nullable value; Count/indexer on snapshot locals; generic List<T> over a
  type parameter (its constraint's instantiation).
Ledger as data:
- glue/Ledger.cs + translate.toml [[replace]]: FortRise custom archers / variants (GetCustomArcherId, ResolveCustomArcher,
  GetInstalledArcherMods, ContainsCustomVariant, CustomVariantTitles, FindVariantIcon) -> TF.EX's bodies with an empty registry.
- translate.toml **[invalid]** (new): FortRise members the reference TowerFall.exe lacks, by C# source text (Roslyn has no symbol):
  `MapScene.NoRandom.Clear()` -> no-op.
- overrides.toml [service] (before the services session took it over): GameFilesIntegrity.IsModified, MatchReport (todo),
  ClipboardService, ScenarioSweeper, InstantReplayFootage, TFGamePatch's updater/warnings, WiderSetMenu.Resolve.
Module: the configured units' generated code is unchanged by step 3 except GameplayLayerPatch's declaration order and new GlueLedger
functions nothing installed calls yet; after merging master the regenerated gen/ also carries the services session's mappings.

Remaining (from the coverage dump, by what blocks most): Coroutine from a TF.EX iterator (`new Coroutine(Seq())`), game value types
(InputState, KeyboardState, ArcherData.PortraitInfo), records/structs holding references (ChangelogPanel.Segment), Harmony reflection
(AccessTools, MethodBase.Invoke), PC-only input (XGamepadInput, MenuAlt2Check), LINQ GroupBy / ToDictionary / method-group arguments,
closures over changing locals, string overloads the binder can't pick (Split, String ctor, Console.WriteLine).

### Step 3, second half: 401 -> 425 (2026-10-17)
- Virtual / abstract game property getters through their vtable slot (PlayerInput.MenuAlt2Check ...): before, a getter body or a
  backing field was looked for. DynamicData.Invoke of game methods by name and argument count (TweenBGCameraToY, Layer.Add ...).
- ICollection<T> Add / Clear / Remove / Contains over a game List<T> / T[] (as Count before).
- `seat == inputService.GetLocalPlayerInputIndex()` (and `!=`) -> `svc::IsLocalSeat(seat)` (several local players per device,
  docs/research-couch-online.md section 5; the services session's group 3 provides it). manifest.toml: TODO for a seat mask at
  NetplayManager.LocalSeat.
- FortRise members: translate.toml [invalid] also takes assignments (`set`); EnableFixedTimeStep -> S-60hz no-op, IsCustom /
  RiseCore.WillRestart sets and CustomVariants.Any -> S-native; RightStickShot.IsVariantActive -> false (glue/Ledger.cs).
  **Not done: the FortRise-patched assembly as [refs]** (docs/research-fortrise.md item 3): FortRisePatcher (browser
  tools/FortRisePatch, 5.5.0-beta.3) refuses the only TowerFall.exe on this Mac (/Applications/TowerFall.app, 1.3.2.0; it needs
  1.3.3.1+), and no PC exe or patched DLL is on disk. Needs the PC exe path or an OPFS export of TowerFall.Patch.dll.
- Glue rewrites: Scene.Add<T>(T[] / List<T>), Calc.Shuffle over MapScene's buttons (Monocle's body, the same Random draws);
  Screen.Center from Width/Height (1.4.1.6's Screen has no Center); String Trim/TrimStart/TrimEnd/StartsWith/EndsWith, Char.IsUpper
  (ASCII), Console.WriteLine -> no-op; game/corlib List indexer set.
Tests: module build on every commit; models-test 43/43 + 7/7; three Ryujinx lobby matches on merged builds (slot `update`, vsstop
600, genpatch GameplayLayer [+ ScreenTitle/VersusCoinButton/ReadyBanner] + notif 1): all in sync (0 differing ticks), the
GameplayLayer hook ran. **Screenshots failed in all three** ("could not create image from window" for the right window id, and a
full-screen capture made no file either: probably macOS screen-recording permission), so after the master merge the ping line and the
now-live DISCONNECTED banner (`svc::IsDisconnected()`, from the services session) were not checked on screen.
Not tested: none of the methods that became translatable in step 3 runs in the module (no configured unit installs them yet);
delegate Invoke through slot 0x58 with arguments; ShuffleMapButtons against the PC's RANDOM pick; nothing on hardware.

### Follow-ups: 425 -> 440 (2026-10-17)
- **The translator's game reference is the FortRise-patched PC assembly** (docs/research-fortrise.md item 3): the Steam PC exe
  (1.3.3.x, copied to the main checkout's `.cache/towerfall-steam/`; the Steam folder untouched) patched by the browser's
  tools/FortRisePatch (FortRise 5.5.0-beta.3; TF.EX 0.21.0 targets beta.2) into `.cache/fortrise-patched/TowerFall.Patch.dll`.
  [refs] lists it first; the game is one slot in Workspace.cs (the vanilla exe only as a fallback). No Invalid operations remain.
  To rebuild it: `cd ~/Code/Towerfall\ Browser/tools/FortRisePatch && dotnet run -c Release -- <copy of TowerFall.exe> <.cache>/fortrise-patched`.
  FortRise's members now bind as members the Switch lacks: **translate.toml [fortrise]** marks them `absent` (reads: default / empty
  sequence; writes, calls on them, foreach over them: nothing; TF.EX with no FortRise mod): EnableFixedTimeStep (S-60hz),
  RiseCore.WillRestart, MapScene.NoRandom, MatchSettings.IsCustom / CustomVersusModeName, MatchVariants.CustomVariants,
  GameModeRegistry.ModesToVersusGameMode, VersusModeButton.currentIndex. Side effect in a configured unit: LobbyInfos'
  `Sounds.ui_click.Play()` default pan is the PC's 160 (the Mac 1.3.2.0 reference had 210).
- Local seats: `x == GetLocalSeat()` -> `svc::IsLocalSeat(x)`, `x == GetLocalPlayerInputIndex()` -> `svc::IsLocalInputIndex(x)` (a GGRS
  handle in a match), 16 sites (KeyboardInput, XGamePadInput, RollCallElement).
- Collections the game never compiled (Dictionary<Variant, bool>, Dictionary<Variant, bool[]>, HashSet<int>, Dictionary<int, (int, int)>,
  List<Input>): C++ containers (gen::ValMap / ValSet / VList) in GC_malloc_uncollectable memory (scanned: game objects and managed
  arrays held there stay alive); objects made in a method come from a ring of 8 per type (8 live at most), a static field's is its own.
  Which instantiations: the binding has no type for them (probed without counting as a use).
- TF.EX's own DynamicData state on game objects (RollcallElement.openSeatOrb, OutlineText.IsPlayerName, Engine.FrameCounter,
  Player.isAimingRight / rightStickHeld): one side table, (member name, object) -> 8 bytes, scanned uncollectable memory, 256 entries,
  the oldest replaced. UI state only: it isn't in the rollback snapshot (RightStickShot's is gameplay state, but that variant is
  never active on the Switch).
- Also: Dictionary.Remove(key, out v), a game object's array field set through its managed object, our arrays to System.Array /
  object parameters, LINQ results over model lists are snapshots (not the struct's list).
Tests: module build on every commit; a Ryujinx lobby match on the merged build with tfex-adopt's defaults (GameplayLayerPatch,
VersusCoinButtonPatch on; no genpatch lines): in sync (0 of 599), the GameplayLayer and ScreenTitle hooks ran. No screenshot.
Not tested: the containers and the side table run in no configured unit yet; the pan change; nothing on hardware.

### Follow-ups for adoption and the ledger (2026-10-17)
- Units for tfex-adopt: WhiteArcherPrisonPatch (prefix on WhiteArcherPrison.Spawn), FightButtonPatch (postfix on
  FightButton.MenuAction), RollcallLayout, LobbyBorderButton. **Not** NetplayMenuButton / NetplayButton / CompactCoOpButton: they
  subclass MainModeButton, whose abstract slots ImageRotation.set, ImageY.get/set and MenuAction don't bind (binding.json "no body
  on the Switch"); a guessed slot would crash, so they wait for disassembly-verified pins. CompactCoOpButton also needs a Coroutine
  from a TF.EX iterator (TF.EX's only one: its skull animation).
- **Coverage correction 440 -> 404**: the vtable slots a class unit overrides were not charged to any method, so classes that can't
  be built counted as translatable. Now every method of such a class is blocked (36 methods, all from the four slots above).
- **Coverage reads the ledger**: overrides.toml [skip] members are listed apart ("not applicable on the Switch") and out of the
  applicable count. New skips: LightPauseMenu's PC gamepad (Pressed / PadPressed / PadButtonPressed), GetRightStick (PC right stick),
  InputDelayAdvisor.GameMousePosition (PC mouse), SeriesVariantLock.GetMethod (Harmony run-time reflection; the Switch's hooks are
  exlaunch's).
- Game structs through game objects (archer.Portraits.Win: one load at the summed offset; struct field offsets are from the struct's
  start). LINQ method groups (Where(IsCodeChar)). Locals that lambdas capture and the code changes: a one-element managed array (C#'s
  shared display class): CreatePrivateJoinCode translates.
Not tested: none of these run in a configured unit except the four new units, which tfex-adopt switches over and tests.

### Coroutines from TF.EX iterators (2026-10-17)
`Add(new Coroutine(Seq()))` over a TF.EX iterator (TF.EX's one use: CompactCoOpButton's skull animation): the iterator also gets a
C++20 coroutine twin (`Seq_co`, `co_yield` where C# yields; gen::CoGen in tfexgen_code_rt.hpp), its frame in GC_malloc_uncollectable
memory (scanned; not reclaimed: one small frame per run). glue/Rewrites.cs CoroutineOf makes a looping 1-frame Tween (TF.EX's own
Tween / OnUpdate shape) that steps it as Monocle's Coroutine.Update does: an int yield skips that many frames (Counter at TimeMult 1),
anything else continues next frame, the end removes the Tween. Not supported: an iterator yielding another IEnumerator (nested),
float waits as seconds (Coroutine itself treats them as "next frame" too).
Test (Ryujinx slot `update`, hw.txt `gencotest 60`, translator/glue/CoTest.cs): steps logged at entity frames 0, 11, 22, 33 and the
end at 44 (yield 10 -> resumed 11 frames later, as Monocle). Also: lambda captures fixed (a declaring deconstruction is not a later
write; locals a lambda declares are not its captures), lobby-model dictionaries (CustomMod.Data), [service] `set` for property setters.

### Boot crash fix: static initializers made at first use (2026-10-17)
ef03eef crashed at boot: the LobbyBuilder units pull in Auto_Constants, whose `NETPLAY_SAFE_MAP = new List<string> { ... }` (newly
translatable) ran as a C++ static initializer in exl_module_init, before the game existed. Now a static field or static auto-property
of a static unit whose initializer isn't a constant is made at first use (an accessor with a flag: C#'s static constructor runs at the
first access too); callers in other units call the accessor. Coroutine frames: a finished run's frame and run record are zeroed and
reused (GC_free isn't bound): memory is bounded by the most runs alive at once; an abandoned run (its Tween gone before the end) keeps
its frame. Tests (Ryujinx slot `update`): boot + `gencotest 60` (two coroutines alongside, the first's Tween removed at its end, a run
record reused, a third started from a coroutine: frames 0/11/22/33/44, 51, 52/58), and a lobby match with the default patches: in sync
(0 of 599), the GameplayLayer hook ran.

## v0.21.1 (2026-10-17)

`tools/tfex/update.sh v0.21.0 v0.21.1` -> `tools/tfex/reports/v0.21.0-v0.21.1.md`; vendor/tfex moved to v0.21.1 (bcebd12); gen/ (models +
code) regenerated. **Simulation: unchanged** (confirms docs/research-tfex-feature-gaps.md V.6): the A items are TreasureSpawner /
OrbPickup postfixes turned into finalizers (identical without exceptions, and the Switch build has none), `Arrow_Init_Postfix` now gated
on capture (only differs outside netplay), `NetplayManager.Reset` clearing FortRise's NoRandom only in netplay (absent on the
Switch). Manifest: the three removed postfix keys renamed to the finalizers, hashes refilled for the reviewed A members.
Generated code: only the new models (Notice / NoticeMessage, UpdateRequired / UpdateRequiredMessage) and the version labels; models-test
43/43 + 7/7 at v0.21.1.

Hand-written to-do (for tfex-adopt; the report's sections 1 and 3 have the diffs):
- B `MatchmakingService.HandleMessageContents`: handle the new "Notice" (show `Notice.Text` as a notification, 10, 600) and
  "UpdateRequired" (`HandleUpdateRequired(version)`) server messages (models generated).
- `MATCHMAKING_URL`: `<server>/ws?version=<TF.EX version>` (our tfex/MatchmakingService.cpp NetThread builds `<server>/ws`).
- `ConnectToServerAndListen`: the switch to NetplaySelect after connecting now runs on the game thread (RunOnGameThread).
- C `RollcallElement_NotJoinedUpdate`: a failed LeaveLobby goes to the LobbyBrowser (was VersusOptions).
- `MainMenu_CallStateFunc`: on "Create", `RemoveStaleMenuItems(menu, state)` first.
- `MainMenu_CreateMain`: clears the netplay mode (ClearNetplayMode, NetplayOptions.Restore, ResetMode when set, ResetVersusChoices
  when it was netplay).
- D (not ours): TFGameUpdate_Patch aborts instant replay footage when disconnected.

New units (for tfex-adopt, hooks install only with hw.txt `genpatch <Class>`): PlayerPatch (exclude RefreshSprites: ledger skip),
SessionPatch, MatchSettingsPatch, VersusVariantButtonPatch, ResultsVoteLock (a helper: no hooks), HUDPatch, VersusModeButtonPatch,
MatchVariantsPatchs, LobbyUpdateFeed, LobbyVarianText. Translator: unit `exclude` (also for HookGen), service-mapped methods keep the
service's return type, snapshot locals stay snapshots when only Count / an element is read, auto units pull in the static auto-properties
their members use.
Tests (Ryujinx slot `update`): lobby matches with the default patches at v0.21.1, before and after the new units: in sync (0 of 596,
0 of 597). Each new hook class alone or in pairs (MatchSettingsPatch; SessionPatch; HUD + VersusModeButton; VersusVariantButton +
MatchVariants): in sync. **Open:** with PlayerPatch + SessionPatch, or the six classes without PlayerPatch, together, the Switch crashes
at lobby start inside the hand `VariantsExtensions::ApplyVariants` (NetplayManager.cpp StartLobbyMatch, an invalid read walking
VersusMatchSettings.Variants), before any of those hooks is called (only MatchVariants.ctor / ScreenTitle ran). Not the hook pool:
raising exlaunch's JitSize 0x8000 -> 0x10000 changed nothing (reverted). Not root-caused yet.

## Core multiplayer path: what is still hand-written, ranked (2026-10-17)

Method: manifest.toml `at` (which hand-written file ports each TF.EX member) crossed with coverage at v0.21.1 (translatable = no blocker
and compiles). "ok" = translatable today but no unit yet.

| rank | TF.EX | hand-written now | state | what it takes |
|---|---|---|---|---|
| done | PauseMenuPatch (end-of-match vote menu, QUIT) | tfex/MainMenuPatch.cpp Pause*Hook, Vote, QuitToNetplayEntry | unit 3858aa1 (hooks off): 5 of 6 hooks; VersusRematch waits for a SeriesContinueChoice rule | tfex-adopt: the rule, then switch over. TF.EX's QUIT (Level.GoToNetplayEntryMenu) has none of the Switch teardown (xplay::EndLobbyMatch, TFGame.Players cleared): it must move into LeaveLobby or a MainMenu hook |
| done | HUDPatch + MatchEndChoices (results finished -> NotifyMatchEnded, the vote panel) | OnLobbyResultsFinished (xplay -> MainMenuPatch.cpp) | units, fully translatable | tfex-adopt: switch over together with PauseMenuPatch |
| done | MatchmakingService ReconcileRollcall* | MainMenuPatch.cpp roll-call pull | 51168c4 (commented out) | tfex-adopt: 10 [service] rules |
| 1 (ready) | MatchmakingService.cs whole class (126 methods; 141 manifest entries) | tfex/MatchmakingService.cpp 652 lines + Models.cpp/hpp | as a generated class (not hosted): 75/126 translate; 51 blocked | translator: Distinct, Except, ToHashSet, SetEquals, SelectMany as a source, spread into a set, `using var`, enum ToString, Task.Run with code after an await (~10 constructs). tfex-adopt: ~10 service rules (IArcherService Add/Remove/Reset, IInputService Ensure*, ISkinStreamService x5, INetplayManager.AddLateSpectator) and the socket edge (send, receive loop) as services |
| 2 | MapScenePatch (6/7 ok) | tfex/MapScenePatch.cpp 191 lines | translatable, no unit | a unit with hooks; tfex-adopt switches over |
| 3 | TFGamePatch NetplayEntry request/resolve, OnSceneTransition; RollCallElementPatch (12 ok) | tfex/MainMenuPatch.cpp | translatable, no unit | units; the menu entry flow |
| 4 | TFGamePatch frame loop (25 ok) | xplay.cpp FrameHook, NetplayGate, time step | translatable, no unit | lockstep-critical: last, with the coordinator |
| - | NetplayManager.cs (28 entries) | tfex/NetplayManager.cpp | not in the coverage set | after MatchmakingService (same approach) |

### MatchmakingService as a generated class (2026-10-17)

Translates whole: 116 methods, nothing unsupported, compiles in the real build mode against stubs of its edge (the commented unit in
translate.toml; rules in tools/tfex/reports/matchmakingservice-edge-rules.toml). Translator work for it: string keys by content
(gen::StrK), Distinct / Except / Concat / ToHashSet / SetEquals, spreads into sets and arrays, SelectMany over an own iterator, enum
names, `await` of TF.EX / service calls (S-async), MessagePack as JSON through the models' JWrite / JRead (runtime/tfexgen_msgpack.*),
model fields owning their storage (a model list by value, a single model in 4 boxes of its own: gen::KeepModel), [service] rules on a
class's own fields (field initializers too), Task.Run of an own method group.

The edge stays the Switch's (10 members excluded, mapped by [service] rules on the class's own members): Send (the socket),
ConnectToServerAndListen / EnsureConnection / IsConnectedToServer / Close, RunOnGameThread / DrainGameThreadActions, LastStateChecksum
(SHA256 of the rollback state), SwitchPingMeasurement (net.cpp), GetRelayedPeersFrom (a model list of lists, not supported yet),
GetEndGameStatus (its service already exists). Edge fields (_webSocket, _sendGate, gameThreadActions, the cancellation token) have
rules whose `set` drops the value. Services: IArcherService Add/Remove/Reset, IInputService EnsureLocalControllerSeat (bool) /
EnsureRemoteControllers, INetplayManager.AddLateSpectator, ISkinStreamService (a to-do no-op).
Integration (tfex-adopt): one instance (New(...) on a menu, outside rollback), the net thread queues each received text and the game
thread calls HandleMessageContents (TF.EX runs it on its receive thread; generated code allocates game objects, so the game thread);
IMatchmakingService's [service] rules then point at the generated methods instead of tfex/MatchmakingService.cpp.
Not done: a runtime test (nothing calls it yet); GetRelayedPeersFrom (string[] Contains binds to MemoryExtensions over a span, and
RelayedPairs is a model list of lists).

### CORE-FLOW rows 8-16: what TF.EX code is still not generated (2026-10-17)

- Rows 8, 11, 13-16 are generated (RollcallReconcile / RollCallElementPatch, LightPauseMenu / LevelPausing, PauseMenuPatch / HUDPatch /
  MatchEndChoices, SessionPatch, MapScenePatch). Row 13's open item (the vote panel isn't drawn) is not the call: Draw.TextJustify's
  ABI matches the game's body (font x0, text x1, position s0/s1, color w2, justify s2/s3); likely where/when HUD layer 4 is drawn.
- Rows 9, 10, 12 (match start, the match, disconnect) run through TF.EX's NetplayManager.cs, still the hand mirror (tfex/NetplayManager.cpp)
  plus xplay's native GGRS session. Probe (a temporary unit, not configured): as a generated class 71 of 93 methods translate today.
  The other 22 need: service rules (IGameContext SetLocalSeat / ResetPlayersIndex / Reset / GetLocalPlayerIndex, IRoundPlayerSpawner.
  SpawnRoundPlayers, IMatchmakingService QueueSpectatorNotice / GetRelayedPeers / GetRelayUrl, IArcherService.ApplyToGame), `set` rules
  for the fields it writes (IsInInit, IsTestMode, IsRollbackFrame, IsRestoring, IsReplayMode, IsCaptureActive), and translator work
  (ValueTuple fields of non-model tuples, ToDictionary, ElementAtOrDefault, `using var` on GGRSFFI status objects, ActionResult). It owns
  the GGRS session, which xplay runs natively here: replacing it is lockstep-critical and needs a plan with Integration B and the
  coordinator before any of it is configured.

### NetplayManager split (draft for Integration B; principle from the coordinator, 2026-10-17)

TF.EX's code for the logic of rows 9 and 12 (match start, disconnect) and the session flags; xplay's native GGRS / rollback stays the
engine (the session object, save/load, the transport); spectators after priority 1. Draft, by NetplayManager.cs method:
- **TF.EX (generated):** the flags and modes: IsTestMode, IsRollbackFrame / SetIsRollbackFrame, IsInit, IsSynchronized, IsDisconnected,
  IsUpdating / SetIsUpdating, IsReplayMode, IsServerMode, GetNetplayMode, HasSetMode, Set{Test,Local,Replay,Server}Mode, ResetMode,
  SetRoomAndServerMode, Reset. Match start (row 9): EstablishSessionIfSynchronized, OnSessionEstablished, SpawnFirstRound,
  AbortIfSynchronizationFailed, AbortToVersusOptions, ConsumeAbortToVersusOptions, FailedConnectionMessage, HasFailedInitialConnection,
  GetFailedConnectionMessage. Disconnect / end (row 12): EndMatchOnNetplayError, EndMatch, IsUnfinishedTeamMatch. Players: GetPlayer2Name,
  UpdatePlayer2Name, GetNameForSeat, SetLocalSeat, UpdatePlayers, UpdateNumPlayers, GetNumPlayers, GetScore. Input delay:
  GetEffectiveInputDelay, Set/ClearSessionInputDelay. Requests: UpdateNetplayRequests, HaveRequestToHandle, ConsumeNetplayRequest.
- **xplay (native, [service] rules into it):** Init / NativeInit / Connect / ApplyRelay (session creation: SafeBytes<GGRSConfig>), Poll /
  NativePoll / GetEventAndUpdate, AdvanceFrame / CanAdvanceFrame / IsFramesAhead, SaveGameState / AdvanceGameState / LoadGameState,
  HaveFramesToReSimulate / UpdateFramesToReSimulate / GetFramesToReSimulate, GetNetworkStats / GetNetworkStatsPerSeat /
  UpdateNetworkStats, GetSessionFps, PublishCaptureFlag.
- **Out (after priority 1):** spectators (SetSpectatorMode / Catchup, AddLateSpectator, FlushPendingLateSpectators, RegisterLateSpectator,
  AddSpectators), GetArchersInfo (replay).
Needs from B before a unit: [service] rules for the xplay group and IGameContext / IRoundPlayerSpawner / the 3 matchmaking calls /
ApplyToGame; `set` rules (or plain generated fields) for the six flags; which of TF.EX's flag fields become the generated object's
state vs xplay's. Translator side: done (1fc2779), the generated class compiles once those exist. Status: draft, waiting for B.

**Note for step (c) (B, review of e31c6b2):** EstablishSessionIfSynchronized clears _pendingSessionRoundLogic, an object field. Call it
either before rb::Init (the first GGRS save) or inside a simulated tick; otherwise a rollback restore brings the field back and the round
spawns twice. GetPlayers' real mapping (the lobby's session players by handle) is also step (c).

#### NetplayManager step (a): the unit (2026-10-17, approved by the coordinator; B's corrections in docs/ws-match.md)
translate.toml [[unit]] NetplayManager: the class minus the xplay group (session creation, polling, advance/save/load, resimulation counts,
network stats, fps, PublishCaptureFlag, requests), spectators and replay. Generated (tfexgen_NetplayManager.cpp) and compiled; nothing calls
it yet. A call to one of the unit's own excluded members is now an explicit "excluded member without a [service] rule" marker (it was an
undeclared call). The rules B writes (overrides.toml [service]); each marker below is a TFEXGEN_UNSUPPORTED_EXPR until its rule exists:
- get/set rules for the session-state fields (B's list): _isInit, _netplayMode, _isRollbackFrame, _isUpdating, _framesToReSimulate,
  _framesAhead; IsSynchronized / IsDisconnected.
- ExFlags set rules: IsTestMode, IsReplayMode, IsRollbackFrame, IsRestoring; GGRSFFI.IsInInit (set).
- excluded members called from generated ones: PublishCaptureFlag (set__isInit, set__netplayMode), IsReplayMode / IsSpectatorMode
  (FailedConnectionMessage, IsUnfinishedTeamMatch, GetNameForSeat; value false), FlushPendingLateSpectators (noop).
- IGameContext: GetLocalPlayerIndex, GetPlayers, Reset, ResetPlayersIndex, SetLocalSeat. IArcherService.ApplyToGame.
  IRoundPlayerSpawner.SpawnRoundPlayers (B: none, the vanilla path).
- Reset's status: GGRSFFI.netplay_reset() is absent, so the chain reads StatusExtension.ToModelGGrsFFI(Status{}), StatusImpl.IsOk /
  Dispose and InfoHandle.AsString: rule netplay_reset -> xplay::EndLobbyMatch, and ToModelGGrsFFI / IsOk / AsString as values (ok).

## TF.State patches (evaluation rec. 8; hw.txt `genstate`, default off; 2026-10-18)

TF.State gameplay patch classes that translate fully are generated as hook units (translate.toml `State_*`, hooks = true). Their hooks
are installed only under hw.txt `genstate 1` (tfexgen_glue: kStatePatches; `tfex::gen::GenStateOn()` tells the hand ports to stand
aside, which is Integration B2's half). With `genstate 0`, the default, nothing changes. Coverage of the rest:
tools/tfex/reports/state-coverage-v0.21.1.md (113 of 219 TF.State methods).

| TF.State patch class | game methods it hooks | translates fully? | wired behind genstate? | hand port it replaces (stands aside under genstate 1: B2) |
|---|---|---|---|---|
| ArrowCushionPatch | ArrowCushion.AddArrow (prefix, postfix) | yes | yes | rnd.cpp AddArrowHook (A3) |
| LevelEntityPatch | LevelEntity.Render (prefix, postfix) | yes | yes | xplay.cpp / tfstate_ports LevelEntityRenderHook (0xa81400) |
| BatPatch | Bat ctor, Bat.Update (postfix) | yes | yes | none: not hand-ported (new behaviour under genstate 1) |
| ChainPatch | Chain ctor (prefix, postfix) | yes | yes | rnd.cpp ChainCtorHook (A4) |
| CrownRngPatch / SteelHatRngPatch / WoodenHatRngPatch | Crown / SteelHat / WoodenHat.Update (prefix, postfix) | yes | yes | rnd.cpp HatUpdateHook0-2 (A6-A8) |
| LavaPatch | Lava.BubbleComplete (prefix, postfix) | yes | yes | rnd.cpp BubbleCompleteHook (A9) |
| LavaControlPatch | LavaControl ctor, Added, Extend (prefix, postfix) | yes | yes | rnd.cpp LavaCtorHook, LavaAddedHook, LavaExtendHook (A10-A12) |
| PickupPatch | Pickup ctor (postfix), Pickup.FinishUnpack (postfix) | yes | yes | tfstate_ports PickupCtorHook (ctor); FinishUnpack is TF.State's snapshot flag (ledger S-rollback: harmless) |
| PlayerStatePatch | Player ctor, Player.DropArrow, Player.LoseHat (prefix, postfix) | yes | yes | rnd.cpp LoseHatHook (A22); DropArrow: none (new); ctor: TF.State's isAimingRight field (S-rollback) |
| PlayerGhostPatch | PlayerGhost ctor, DespawnEnter, Update; Enemy.SetState | yes | yes | none: not hand-ported (new behaviour under genstate 1) |
| SlimePatch | Slime ctor, Slime.Update (postfix) | yes | yes | none: not hand-ported (new behaviour under genstate 1) |
| MiasmaPatch, IciclePatch, SensorBlockPatch, BrambleArrowPatch, PlayerCorpsePatch, MoonGlassBlockPatch, OrbPickup, TreasureSpawnerPatch, OrbLogicPatch, RoundLogicPatch, SFXPatch, Arrow (EnterFallMode / Drop), CalcPatch ... | | no (see state-coverage) | no | hand ports stay (xplay.cpp, rnd.cpp, tfstate_ports.cpp, sfx.cpp) |

## Menu crash 01792332348 (efe8f6c): finding, 2026-10-18 (not fixed; usage cap)

`svc::EnsureLocalControllerSeat` (tfex/runtime/tfexgen_services.cpp) is a line-for-line copy of TF.EX's
`InputService.EnsureLocalControllerSeat` (network/TF.EX.Domain/Services/TF/InputService.cs:71-111). TF.EX itself writes the displaced
entry of `PlayerInputs[localSeat]` (null when that seat had no controller) into the local controller's old slot. So replacing the
stand-in with TF.EX's generated InputService would crash the same way.

The Switch differs in what follows: its `MInput.UpdateJoysticks` calls `UpdateStyle` on `TFGame.PlayerInputs[i]` for every attached
JoyCon i, so an attached controller's index must never hold null. On the PC, gamepads are updated through their own list.

So the fix is a real TFEX-DIFF (S-input), not a null check. It needs a decision on one of:
- (a) Swap with an object: give the old slot an input object for the attached controller (a JoyConInput for that index) instead of
  `displaced` when that is null. TF.EX's logic is otherwise kept.
- (b) Don't move PlayerInputs: map seat -> controller where TF.EX reads the local seat's input. That touches the netplay input path,
  which is xplay's.
- (c) Make UpdateJoysticks skip a null slot: a hook on the game's MInput.UpdateJoysticks (address from tools/re).
Next step: read MInput.UpdateJoysticks in the disassembly (tools/re/disasm.py) to see what it indexes, then pick (a) or (c).

## genstate findings (B2's Ryujinx runs, 2026-10-18; open, translator's)

1. **Crash under `genstate 1`** at the first Pickup ctor (PickupPatch.Pickup_ctor -> Auto_EntityExtensions::DeleteComponent ->
   GetComponent). Checked the bindings in the disassembly, and they are right:
   - `m_Entity_get_Components` (0x92d8f0) = `ldr x0,[x0,#0x28]`;
   - `List<Component>.get_Count` (0x1c526f0) reads +0x18;
   - `get_Item` (0x91ea10) checks the size at +0x18, then the items array at +0x10 with its length: a consistent List<T> layout.

   Next suspects:
   - (a) the `self_` the generated ctor-postfix hook passes (Hook_25's Callback, for a constructor: is it the new Pickup?);
   - (b) `gen::As(component, TypeOf(t_Tween))` on a component whose class word isn't what As expects;
   - (c) RemoveSelf / List.Remove during the ctor (the Pickup's Components may still be adding).

   One Ryujinx run with a log line in DeleteComponent (entity, Components, count) settles it.
2. **Desync with genstate as the default** (B2's ce6af75): GGRS reports it from the first check at frame 120, before input. That
   points at something at level load. Bisect by enabling the 13 classes one at a time (`genpatch <Class>` lines instead of genstate),
   starting with LavaControl / Lava / Chain / the hats / Pickup.

## genstate, 2026-10-19 (tfex-translator): crash fixed; default-on and hand ports deleted; desync open
- **Crash: fixed at its cause (4086f82).** HookGen's constructor hooks took the object from the ctor's return value. On the Switch a ctor
  called with x0 = an object the caller allocated doesn't return it (Pickup's ctor returned 1 on one path), so TF.State's ctor
  postfixes ran on garbage. Now: `self` when the caller passed one, else the return value (the x0 = null form allocates and returns it:
  MainMenu.set_State -> ScreenTitle..ctor). The full genstate-1 run then stays in sync to tick 1786, with 3 pickups, a death and an arrow
  into a corpse.
- **Default-on, hand ports deleted (this commit).** tfexgen_glue installs the 13 State_* classes at boot, and the genstate setting is
  gone. Deleted:
  - rnd.cpp: AddArrowHook, LoseHatHook, the hat Update hooks (DiscoverHat / HatCreateHook), ChainCtorHook, LavaCtorHook /
    LavaAddedHook / LavaExtendHook, BubbleCompleteHook.
  - tfstate_ports.cpp: PickupCtorHook / PickupTweenHook (g_pickupTargets and its rollback side copy), LevelEntityRenderHook.
- **Open: desync from frame 120** (the browser's GGRS checksum differs from frame 120 on, constant on both sides, before any input) on
  run 214122: tower RANDOM, which had chains (8 Chain ctor hooks). The earlier in-sync genstate-1 runs had no Chain hooks. Checked and
  ruled out:
  - the Chain ctor address (generated and hand port both 0x9cc380);
  - TF.State's ChainPatch (Register / Unregister around the ctor, the same as the hand port);
  - extra float arguments (0x9cc380 writes s2 / s3 before reading them).

  Next:
  - pin the tower with chains (hw.txt lobbymap) and compare against 4086f82 (genstate 1 with the hand ports present but standing aside);
  - the same tower with the hand ports at master, to see whether the frame-120 desync is the generated patches' or the tower's.
- **Desync fixed (next commit):** TF.State's hooks are installed after the module's own hooks: tfex::gen::InstallState, from xplay's
  LoadScript once hw.txt is read, the same moment "genstate 1" used. Installed from Install (before rb / rnd / xplay's hooks) they sat
  innermost in each shared address's chain, and a lobby on MapId 0 (chains) desynced from frame 120.
  - Pinned comparison on MapId 0: 4086f82 with the hand ports, in sync; 4086f82 genstate 1, in sync; 3481ec2 (installed in Install),
    desync from 120; installed late, in sync (0 of 896).
  - Then a random-tower lobby to tick 1800 with genstate the default and the hand ports gone: in sync (0 of 1790), with chains, 3 pickups
    and a lost hat.
  - **Not yet identified:** which other hook on a shared address the order matters for (the next step for the cause, before relying on
    the order; candidates: rb::InstallHooks' and rnd's remaining hooks on Player / Arrow / LevelEntity addresses).
- **Cause identified (2026-10-19, late): no hook order at all; rnd's stale first-instruction checks.** Shared addresses checked: the 13
  State classes' targets against rb::InstallHooks, rnd, xplay, tfstate_ports and the run-time hooks (DrawSkip, Efm, Miasma). The only one
  shared was rnd's DropArrowHook on Player.DropArrow, a hand copy of PlayerStatePatch.Player_DropArrow: deleted. Taking it out alone
  didn't help. The real cause: rnd::Install ANDs together the first-instruction checks of its whitelist sites, still including
  ArrowCushion.AddArrow, Player.LoseHat, LavaControl.Extend and Lava.BubbleComplete, whose rnd hooks went in 3481ec2. xplay_main
  (tfex::gen::Install) runs before rnd_main, so the State hooks had already replaced those instructions. hw-log said "Player.LoseHat
  isn't as expected (14fb350c, wanted d10183ff)". `wl` went false and none of rnd's whitelist hooks were installed: no
  Session.StartGame reseed, GenLevels, ArrowType... So the frame-120 digest had the same positions but other rng words. Installed
  from LoadScript (after rnd_main) the checks passed, which is why "late" worked.
  - Fix: rnd checks only what it hooks; a failed whitelist check now logs an ERROR line. InstallState goes back into
    tfex::gen::Install (at load, like TF.EX's Harmony patches on PC). The order against the module's hooks is irrelevant: no shared
    address remains. TF.EX's own prefix/postfix semantics are inside each generated hook, so no Switch hook needs to sit inside or
    outside them.

## Wide online menus without the side art (2026-10-20)
The user (console, master b4236ae): in the WIDE online menus the Switch still showed the 4-player curtains. The log had the wide
layout set at NetplaySelect ("[menu] the Switch's wide screen layout (SixPlayer 1, RollcallMode 1)").
- **What the curtains are:** Monocle.Screen.PadOffset (+0x48), the x the 420-wide canvas is drawn at. At -50 the side art
  (statics Screen.LeftImage / RightImage) shows; at 0 it's off-screen.
- **Who sets the offset:** MainMenu.Update (+0xa9f4d0), with Calc.Approach by 3 * TimeMult:
  - toward 0 only while MainMenu.RollcallMode == SixPlayer (1) and the state is the roll call (5) or VERSUS's STANDARD / WIDE
    screen (7);
  - states 4 and 8-10 keep it;
  - every other state moves it toward -50.
  The local 6-player path is picker (7) -> roll call (5) -> Fade (4) -> map, so it never meets another state. TF.EX's netplay
  screens (59 LobbyBrowser ... 65 QuickPlaySearch, 61 the series lobby) aren't in the game's list, so they slid back to -50.
  What the local WIDE button sets (WideModeButton.MenuAction +0xbce530: CurrentMatchSettings = VersusMatchSettings, SixPlayer 1,
  RollcallMode 1) was already set, apart from CurrentMatchSettings, which nothing in this decision reads. Background.get_Atlas
  picks SixPlayerBGAtlas on RollcallMode 1 (also set already).
- **Fix (TFEX-DIFF S-wide):** with the wide layout on (WiderSet's IsWide), TF.EX's states 59-65 get the roll call's rule. A
  MainMenu.Update hook (tfex/MainMenuPatch.cpp) moves PadOffset toward 0 from where it was before the frame's Update, at the
  game's own rate. On the PC, WiderSet widens every netplay menu while IsWide. No drawing changed.
- Not covered: the map screen is its own scene (MapScene), not MainMenu: if it still shows the side art on wide, that's a
  separate rule.
- Hidden run (lobby, hw.txt lobbywide 1): "[menu] wide: TF.EX menu state 62 keeps the Switch's wide screen (PadOffset -50.0 -> 0, as
  the roll call)", then -47, -44 (the game's 3-per-frame step). The Switch went through 62 -> 59 -> 60 -> roll call 5 with no
  exception. The harness page can't join a wide lobby ("WIDERSET MOD REQUIRED": the page has no WiderSet), so no match.
- Console check: NETPLAY -> WIDE: no side art on NETPLAY's select screen, the lobby browser / builder, quick play's search and the
  lobby roll call, and again after QUIT back to the netplay menu. Log: "[menu] wide: TF.EX menu state 62 keeps the Switch's wide
  screen (PadOffset -50.0 -> 0 ...)". STANDARD: the side art stays.

## Controllers: TF.EX's InputService on the Switch's controller slots (2026-10-20)
Three symptoms, one cause:
1. As seat 1 of a lobby, the Switch's menus ignored its input until the other player picked.
2. The menu crash after a lobby match (crash-01792351693, "Menu crash 01792332348").
3. A second controller pair attached took a seat, and the user's controller then did nothing.

**How the Switch binds controllers** (tools/re):
- `MInput.JoyCons[i]` is npad i: MInput.UpdateJoysticks (+0x93d400) creates JoyConData(i) at index i when npad i connects.
- `TFGame.PlayerInputs[i]` is `new JoyConInput(i)` for every attached i: PlayerInput.AssignInputs (+0xb054b0) builds a 6-slot
  array, then MenuInput.UpdateInputs.
- MainMenu.Update, every 60 frames in states 2/3/5/6, unless MainMenu.NoGamepadUpdates / NoGamepads, runs UpdateJoysticks, which
  calls `((JoyConInput)PlayerInputs[i]).UpdateStyle()` for every attached i. When a controller attaches or detaches
  (GamepadsChanged), it runs AssignInputs.

**What TF.EX assumes on the PC:**
- Its InputService rearranges PlayerInputs. OnJoinSuccess runs EnableAllControllers, then DisableAllControllerExceptLocal: every
  slot is emptied but the local seat's, which gets the local controller. EnsureLocalControllerSeat moves it on each lobby update.
  DisableAllControllers outside a lobby empties the arrays; EnableAllControllers outside a lobby rebuilds them (AssignInputs +
  UpdateInputs).
- The PC's gamepads update through their own list. TF.EX's MainMenu Update prefix sets NoGamepadUpdates in a lobby, so the
  vanilla refresh doesn't undo its seats.

**Our stand-ins broke this:**
- DisableAllControllerExceptLocal was a no-op, so the local controller reached its seat only on the next LobbyUpdate (symptom 1).
- EnableAllControllers never ran AssignInputs, so after a lobby the emptied npad slot stayed null and the next main-menu refresh
  crashed in UpdateStyle (symptom 2).
- The local controller was "the first slot", so after a rebuild the new pair's object at slot 0 became "local" (symptom 3).

**Now:**
- **TF.EX's InputService is generated** (translate.toml unit InputService, hosted): GetInputIndex, ObserveLocalDevice,
  RebindLocalInput, HasMenuActivity, EnsureLocalControllerSeat, Disable / EnableAllControllers, IsInputLocked, IsInNetplayLobby,
  DisableAllControllerExceptLocal.
  - GameContext's store (the local controller in a GC-scanned slot, the lock flag) is the runtime's (IGameContext rules).
  - The stand-ins are deleted: svc::EnsureLocalControllerSeat / InputIndexOf, and InputLock.cpp's Disable / Enable / IsInputLocked
    / ExceptLocal and its "emptied" gate.
- **One TFEX-DIFF (S-input), InputLock.cpp BeforeMenuUpdate:** the Switch's controller refresh also waits (NoGamepadUpdates) while
  TF.EX's InputService holds the input locked. One example is the NETPLAY entry's checks on the main menu, where TF.EX empties the
  arrays. EnableAllControllers gives the npad order back when it unlocks outside a lobby. In a lobby, TF.EX's own NoGamepadUpdates
  already applies.
- `[[rewrite]] expr` (a new translator rule kind: one expression, matched by its text in one type): TF.EX's `WiderSet ? 8 : 4`
  controller slots become the Switch's six (svc::ControllerSlots).
- GetInputIndex's LINQ over an anonymous type is a "same behaviour" [[replace]] (a loop).
- **Translator fix:** a game static array field given a new array (`TFGame.PlayerInputs = new PlayerInput[n]`) now writes the
  static. Before, it wrote into a temporary Arr wrapper; no unit did this before InputService.
- lobbymatch.cpp ClearFilledSlots (B's; TF.EX's RemoveFakeControllers' stand-in) clears only the slots that still hold the
  controller object FillControllers put there. After a QUIT, TF.EX's EnableAllControllers has rebuilt PlayerInputs in npad order.
- Hidden run (smoke join: the page hosts, the Switch joins as seat 1): PASS, in sync (0 differing ticks of 597). The Switch
  readied in seat 1 ("seat 1 draws LAST OF", players 0100) before the page picked (symptom 1). PlayerInputs non-null mask 02 at
  the start (only the local seat, TF.EX's DisableAllControllerExceptLocal). TF.EX's lock / unlock around every lobby-list request
  with "the Switch's controller refresh waits". Not run: a second controller (Ryujinx emulates one Pro Controller here).
- Console checks:
  - (1) Join an iPhone's lobby as seat 1: the Switch picks and readies right away. Log: "[input] the local controller: ... (PlayerInputs
    index 0)" at the menu, then the seat in "[lobby] seat 1 draws ...".
  - (2) Play a lobby match, QUIT, go back to the main menu and wait 5 s on it (the 60-frame refresh): no crash. Detach and
    reattach the JoyCons on the main menu: still no crash, and the menus answer.
  - (3) In a lobby, attach a second JoyCon pair: it takes no seat, and the first pair keeps working. After leaving, the
    second pair works in local VERSUS.

## Variants (2026-10-20)
- TF.EX's VersusVariantButtonPatch, MatchVariantsPatchs and VersusModeButtonPatch (generated, nothing unsupported) are on by default
  (xplay.cpp's list).
- The netplay variant rules were already generated and called from generated code: MatchVariantsExtensions.ApplyVariants /
  MissingVariants (MatchmakingService, NetplayMenus) and VersusModeExtensions.NormalizeForNetplay / ApplyNetplayVariantRules. The
  inventory's "hand-written" was stale.
- The one variant stand-in left is VariantsExtensions::FirstUnported (NetplayManager.cpp; MainMenuPatch's join refusal / warning). It
  stays as a TFEX-DIFF guard: TF.State's variant-specific patches (DarkPortalsVariantSequence, PlayerCorpse's ghost revive, Bottomless
  Treasure) aren't ported (not among the 13 State classes). It goes when they are (the TF.State queue).
- Test preset (tests only): hw.txt `lobbyvariants TITLE|TITLE` switches those builder variants on (Variant.set_Value on
  VersusMatchSettings.Variants when the LobbyBuilder is made), as a player's toggles would. TF.EX's START reads the toggles. The
  log line lists all 93 titles.
- New log line, once per lobby match: "[vs] match variants on (n of 93): ..." (lobbymatch.cpp NativeSessionSetup, the MatchSettings
  the session starts with). Compare it with the page's "MatchSettings.Variants.on".
- Hidden runs (lobby, the Switch hosts through TF.EX's builder):
  - ALWAYS DARK + SLOW TIME: PASS, 0 of 899.
  - START WITH BOMB ARROWS + SLOW TIME + ALWAYS DARK: PASS, 0 of 895. The lobby sent `"Variants":["START WITH BOMB ARROWS",...]`,
    and both sides' matches had the same three on (Switch "[vs] match variants on (3 of 93): START WITH BOMB ARROWS|ALWAYS DARK|SLOW
    TIME", page the same).
  - Not run: the page hosting with variants (the page has no variant preset); the VERSUS options' variant button in netplay (the
    VersusVariantsButton / VersusModeButton hooks didn't fire in a lobby flow).
- Console check: host a lobby, turn on a few variants in the builder (e.g. START WITH BOMB ARROWS, SLOW TIME), and have the iPhone
  join: the iPhone's lobby shows them, and both play with bomb arrows in slow time, in sync. Log: the "Variants":[...] in the
  CreateLobby send, and "[vs] match variants on (2 of 93): START WITH BOMB ARROWS|SLOW TIME". Then join an iPhone lobby with variants
  on: the same line lists the iPhone's.

## Series / best-of (2026-10-20)
- **SeriesScreen** (unit, whole class, nothing unsupported) was already generated and wired: TF.EX's MainMenuPatch.HandleSeriesLobby
  (generated NetplayMenus) makes it in the SeriesLobby state (61).
- **SeriesVariantLock, as TF.EX's own code (on by default):**
  - SeriesVariantLock.IsLocked / NotifyLocked / DrawLockedOverlay (auto unit);
  - VariantButtonConfirmPatch / VariantButtonRenderPatch: the builder's preset buttons refuse "LOCKED (SERIES ON)" and draw the overlay;
  - VariantTogglePatch: toggles refused when restricted in netplay, when a player lacks the variant, or under the series lock.
  - Their Harmony targets come from `TargetMethods()`, i.e. reflection over LockedButtons with AccessTools.DeclaredMethod. They are
    now data: a translator feature, the unit key `targets = ["Type.Method", ...]`, keeping DeclaredMethod's rule (the type must
    declare the method). The 5 vanilla buttons x OnConfirm / Render are bound; FortRise's 3 aren't on the Switch.
- **SpectatorSeriesTally: not translated now.** TF.EX creates it only when this side is a spectator (Level.cs
  HandleSpectatorsSeriesTally, `IsSpectator()`), and the Switch can't spectate yet. Translating it would be generated code with no
  caller; it comes with the spectators item.
- **S-start, the one change to Switch code:** the native lobby-match start (MainMenuPatch: on IsLobbyReady,
  NetplayManagerStartLobbyMatch) fired only from the roll call (5). From TF.EX's series screen (61), its StartNextGame is the same
  vanilla Fade -> GotoVersusLevelSelect path, so the native start now covers 61 too. Before, game 2 ran the vanilla local start, then
  left the lobby.
- **Tests:**
  - hw.txt `lobbyseries N` presets the builder's SERIES (GameData.BestOf, as LobbyVersusSeriesButton.Select writes it).
  - smoke scenario `series` (best of 3, `lobbylength 0`): the Switch's auto-UI and the page (LVB_PAGESERIES) press A / Confirm on
    the series screen (the tower when it is their pick, then READY), until "[lobby] series: ... finished 1".
  - New log line while the series screen is up, on change: "[lobby] series: wins a-b of n needed, finished 0|1".
- **Hidden runs (smoke `series`, the Switch hosts, the page joins):**
  - best of 3: game 1, then TF.EX's series screen ("series: wins 1-0 of 2 needed, finished 0"). The Switch picked the tower
    (SeriesPickMap) and readied, the page readied, and game 2 started on the Switch's pick and synchronized (map 8, digest clean,
    16,500 frames).
  - Game 2 had nobody scoring: the harness's scripted input drives only the first match, so a best of 3 can't finish here.
  - best of 1 (`series "lobbyseries 1"`): game 1, then the series screen with "series: wins 0-1 of 1 needed, finished 1" (the end).
  - Not run: a series that goes to game 3, the variant lock buttons (the auto-UI doesn't press them), spectators' tally.
- **Console check:** host a lobby with SERIES best of 3 and the iPhone joins.
  - After game 1, both see TF.EX's series screen: the tally, the loser picks the tower, then READY on both.
  - Game 2 starts on that tower. Play to 2 wins: the series screen shows the end (NEW SERIES / LEAVE).
  - In the series lobby's builder, the preset buttons (DISABLE ALL, RANDOMIZE, the tournament presets) are dimmed and say
    "LOCKED (SERIES ON)".
  - Log: "[lobby] series: wins 1-0 of 2 needed, finished 0" ... "finished 1", and "[tfex] StartLobby: ... map <the pick>" for each game.

## Input-delay advice (2026-10-20)
- **TF.EX's InputDelayAdvisor.Update runs** (its `todo` no-op rule is gone). On the Switch, the controller gesture (hold ALT2 = L / ZL,
  Left / Right step the delay) reads the JoyConInput's MenuAlt2Check / MenuLeftCheck / MenuRightCheck, as TF.EX reads the
  PC pad's.
  - Two Switch differences, as rules: UpdateMouse / GameMousePosition are `[fortrise] absent` (S-input: no mouse);
    MainMenuPatch.IsCopyCodeGuideVisible is false (S-keyboard: no COPY CODE guide, the code is centered at the top).
- **The delay reaches the session the TF.EX way:** advisor -> NetplayManager.SetSessionInputDelay (the generated object's
  _sessionInputDelay) -> GetEffectiveInputDelay -> NetplayManager.Init's GGRSConfig.InputDelay (or the series' delay) -> the
  config JSON at 60 Hz, rounded up (glue Ffi.ConfigJson, S-60hz).
  - xplay's own copy (g_npDelay, which aligns the test script's inputs) is now set from that config at Init. Log: "[np] input
    delay N frames at 60 Hz (TF.EX's GGRSConfig.InputDelay M at 240 Hz)".
- **hw.txt `npdelay` is retired:** it was parsed into g_npDelayCfg, which nothing read. It's deleted from hwconfig and from the
  harness defaults.
- **Translator:**
  - a local ToArray() used for `.Length` stays a value snapshot (it became a managed array of a type the game never compiled);
  - `x is T v` on a model of type T is a null test (it asked for a runtime type models don't have).
- **Test:** hw.txt `lobbydelaysteps N`: on the roll call the auto-UI holds L / ZL and taps Right (Left if N < 0) N times.
- **Hidden run** (lobby, `lobbydelaysteps 8`): PASS, in sync (0 of 896).
  - "Session input delay set to 9 .. 16" (TF.EX's log), and the Switch's UpdatePlayer carries `"InputDelay":16`.
  - netplay_init config `"InputDelay":4`, "[np] input delay 4 frames at 60 Hz (... 16 at 240 Hz)".
  - The page kept its own 2: in a plain lobby each side plays its own delay, as on the PC. Only a series lobby's host publishes one
    (SeriesInputDelay, PublishHostDelay), not run here.
- **Console check:** in a lobby's roll call, hold L (or ZL) and press Right a few times: the delay track's marker moves and shows
  the ms. Start the match: the log has "[tfex] Session input delay set to N" and "[np] input delay K frames at 60 Hz (TF.EX's
  GGRSConfig.InputDelay N at 240 Hz)". Hold L for 1.5 s without moving: the override resets (back to the settings' delay).

## Wide online menus, the second time (2026-10-20): the sign was the wrong way round
- The console (master b7c5bbf, 20261020-063034-prev-hw-log): NETPLAY, then moving Right on the STANDARD / WIDE screen slid the
  curtains away; after WIDE, in TF.EX's menus they slid in again. The log had PadOffset -50, -47, -44.
- **Cause:** MainMenu.Update's rule is the other way round from what 11a3712 assumed. PadOffset -50 is the side art slid out (the
  six-player picker and roll call, RollcallMode 1, states 5 / 7). 0 is the side art in: the main menu and every other state, read
  from the disassembly's `tbz w20` that picks the -50 constant for exactly that case. The picker had slid it to -50 (the hover on
  WIDE sets RollcallMode 1). 11a3712 then moved TF.EX's states toward 0, i.e. slid the curtains back in.
- **Fix:** with the wide layout on, TF.EX's states 59-65 get the roll call's target, -50 (kWidePad, MainMenu.Update's own
  constant), at the game's step. No other code writes PadOffset (two writers in the binary: MainMenu.Update, and a
  SavingInfoScene sequence).
- **Trace kept (console log, bounded):** "[menu] wide trace: state S frame F PadOffset start A (written outside MainMenu.Update),
  after the game's Update B, after ours C". It runs in the picker (7) and TF.EX's states, frames 1-3 and every 10th to 60, at most 150
  lines.
- **Hidden run** (lobbywide 1): state 62 frame 1 start -50 (from the picker), then every frame "after the game's Update -47.0, after
  ours -50.0". The same in 59 and 60; the roll call (5) is the game's own -50.
- **Console check:** NETPLAY, Right (the curtains slide away), A (WIDE): no curtains on the select screen, the lobby browser /
  builder, quick play's search or the roll call. Log: "[menu] wide trace: state 62 frame 2 ... after ours -50.0". STANDARD: the
  curtains stay.
- **Checked against the game's own six-player path (the user asked, 2026-10-20).** Same trace, test driver `lobbyui 4` (VERSUS ->
  WIDE -> the local roll call) vs the netplay path (`lobbywide 1`):
  - local: main menu / picker before WIDE PadOffset 0. After WIDE, the local roll call (5) at -50 from frame 1 (the game's
    own), RollcallMode 1, VersusMatchSettings.SixPlayer 1, CurrentMatchSettings = VersusMatchSettings.
  - netplay: the same 0 before WIDE. After WIDE, TF.EX's 62 / 59 / 60 at -50 every frame (the game's Update alone pulls toward 0,
    -47 per frame; the fix holds -50), the roll call at the game's own -50, RollcallMode 1, SixPlayer 1, CurrentMatchSettings null.
  - So -50 is the six-player value, and every netplay screen now matches it. Not seen: the picture itself (hidden runs draw
    nothing); the console run shows it.
  - Difference left: the local WIDE button sets CurrentMatchSettings = VersusMatchSettings, and the netplay path leaves it null.
    Nothing in the menus reads it (only the match start), so it isn't the curtains.
- The trace now also covers the main menu (3) and the roll call (5), with RollcallMode, VersusMatchSettings.SixPlayer and
  CurrentMatchSettings, for both paths (250 lines at most).

## The input-delay advice's button: L or ZL (2026-10-20, the user's console report)
The console showed the advice's button as "L", but the user found the gesture on ZL.
- **TF.EX's binding:** the advisor reads `input.MenuAlt2Check` and draws `input.Alt2Icon`, both from the controller's own class. On
  the PC, XGamepadInput.MenuAlt2Check is `Check(Buttons.LeftShoulder) || LeftTriggerCheck(0.1)`: LB **or** LT.
- **The Switch:** the game's JoyConInput.MenuAlt2Check (+0xa61cd0) has the same shape: JoyConData.Check(0x100 = LeftShoulder) ||
  LeftTriggerCheck. On the Switch that is L **or** ZL. Its Alt2Icon is the game's own glyph for that binding ("L").
  - So read and glyph already agree with TF.EX: TF.EX's bumper-or-trigger is the Switch's L-or-ZL. There's no remapping to make,
    and none was made.
  - (My earlier b7424f6 note said "L / ZL"; the driver pressed both at once, which couldn't tell them apart.)
- **Hidden runs, each button alone** (hw.txt `lobbydelaybutton L|ZL`, new): L alone 8 -> 12 and ZL alone 8 -> 12, each "[np] input
  delay 3 frames at 60 Hz". Both work in Ryujinx, so the console's "only ZL" isn't reproduced here: a hardware-only case.
- **Logging to settle it on the next console run** (AGENTS: hardware-only -> log, don't chase in the emulator), on the roll call,
  20 lines at most, on change: "[menu] input-delay button: L a, ZL b -> MenuAlt2Check c (JoyCon n)". Hidden run: "L 1, ZL 0 ->
  MenuAlt2Check 1" while L was held.
- **Console check:** on a lobby's roll call press L alone, then ZL alone, each with Right. Each press logs one "input-delay button" line.
  - If the log shows "L 1 ... MenuAlt2Check 1" but nothing moves, the problem is past the read (TF.EX's gesture).
  - If it shows "L 0" while L is held, the console's controller maps L elsewhere (that line's JoyCon number and style tell us).

## Changelog panel (2026-10-20): stopped here by the user's change of plan, working as far as it goes
- **TF.EX's code, on by default:**
  - Changelog's Configure / SelectUnseen / MarkSeen and HandleChangelog (generated);
  - ChangelogPanel (a class of ours);
  - MainMenuPatch.MainMenu_set_State (hook unit MainMenuSetState: PRESS START -> Main becomes the changelog, MenuState 66, while
    releases are pending).
- **TFEX-DIFF S-native:** Changelog.Parse is Markdig on the PC. On the Switch, glue ChangelogMarkdown.Parse does the same blocks for
  CHANGELOG.md's markdown (`##` / `###` headings, `-` items, continuation lines) with TF.EX's helpers. Its ToDisplayText has no
  Unicode normalization: accented letters become spaces; the file is ASCII. Markdig-only members are `absent`.
- **Data:** TF.EX's Changelog/CHANGELOG.md ships as sd:/xplay/tfex/CHANGELOG.md (tools/devserve.list). The version is TFEX_VERSION.
  LastVersionSeen comes from TF.EX.settings.json, and marking seen saves it (NetplaySettingsFile::SaveLastVersionSeen).
- **Translator, for it:**
  - System.Version as a packed int64 (TryParse, comparisons, ToString);
  - Trim / Split over `params ReadOnlySpan<char>`, Split(char, RemoveEmptyEntries), Split(string, count);
  - an IEnumerable<T> parameter every caller passes a List<T>;
  - positional record structs; arrays of our structs as object[] (GC-scanned);
  - a hooks unit's `members` limits its patches.
- **Hidden run (lobby, settings without LastVersionSeen):**
  - "[tfex] changelog: sd:/xplay/tfex/CHANGELOG.md (4783 bytes), version 0.21.1, last seen none: releases pending";
  - "hook MainMenu.set_State (value 66)", "[menu] state 66", A (CONTINUE), state 3;
  - "[tfex] changelog seen: LastVersionSeen 0.21.1", then the settings saved. The lobby match after it passed, in sync (0 of 291).
- **Not done:** the panel's picture (hidden run), scrolling through several releases, a check that a second boot shows nothing (the
  harness restores the settings file around each run).
- **Console check:** after publishing (devserve sends CHANGELOG.md), the first boot shows TF.EX's changelog after PRESS START. A
  continues to the main menu, and the next boot doesn't show it. Log: "[tfex] changelog: ... releases pending", then "changelog seen:
  LastVersionSeen 0.21.1"; the second boot logs "nothing pending".

## Player indicator (2026-10-20): the name over the archer
- **TF.EX's PlayerIndicatorPatch, generated and on by default:** while capture is active (an online match), the PlayerIndicator's
  constructor postfix sets its text to NetplayManager.GetNameForSeat(playerIndex).
- **The blocker** was the indicator's private fields, read through Traverse: pinned from the Switch's constructor (+0xb04bf0). `text` is
  at +0x30 ((playerIndex + 1).ToString() into a concat) and `playerIndex` at +0x50 (its int argument).
- The hook passes the constructor's Vector2 (s0 / s1) through to the original.
- No stand-in existed (the Switch showed the game's P1 / P2).
- **The text source is GetNameForSeat's [service] rule** (tfexgen_services.cpp). It stays: it carries the Switch's several local
  players per console (the main one gets the preference's name). TF.EX's own NetplayManager.GetNameForSeat is generated too, a
  candidate to replace the rule once local multiplayer is checked against it.
  - New bounded log: "[tfex] GetNameForSeat(n) -> NAME".
- **Hidden run (lobby, vsstop 600):** "hook PlayerIndicator.ctor call 1 (playerIndex 0)", "GetNameForSeat(0) -> RYUJINX (the local
  player)", "call 2 (playerIndex 1)", "GetNameForSeat(1) -> PLAYER". In sync (0 of 590).
- **Console check:** in a lobby match against the iPhone, the indicator over each archer at the round start shows the player's name
  (the Switch's own name, the iPhone's name) instead of P1 / P2. Log: "[tfex] GetNameForSeat(0) -> <name>" for each seat.

## Netplay options / server screen (2026-10-20)
Done (the coordinator's item 8; A = 9c5cf9f, B = the commit after it):
- A. netplay.txt: drop its TF.EX keys (name / server / inputdelay: a first-run import into TF.EX.settings.json). Keep the
  Switch-only connection keys (ip / rtcbind / stun, S-native). The harness (tools/lobby-vs-browser.sh) writes TF.EX.settings.json
  (Server = the local server, Name RYUJINX, InputDelayMs) instead of netplay.txt's three keys.
- B. The screen:
  - TF.EX's NetplaySettings.Create (FortRise ISettingsCreate: CreateNumber / CreateOptions / CreateOnOff / CreateInput /
    CreateCustomOptions(ServerOptionsButton)) is generated.
  - TFEX-DIFF S-native: FortRise's module settings page -> the game's OPTIONS screen. A prefix on MainMenu.InitOptions(List<OptionsButton>)
    in the Options state appends the entries; the five ISettingsCreate calls are [[rewrite]]s to glue (translator/glue) that make
    the same OptionsButtons (SetCallbacks: value text, left / right, confirm).
  - ServerOptionsButton is a class of ours (OptionsButton subclass). FortRise's UIInputText -> the software keyboard (swkbd, as the
    join code). The fortrise_description DynamicData is a no-op (no description line on the Switch's options screen).
  - Each change runs TF.EX's Apply and saves TF.EX.settings.json.
- **Result (B, as built):**
  - TF.EX's NetplaySettings (Create / Apply) and ServerOptionsButton are generated (units NetplaySettings, ServerOptionsButton), on
    by default. TokenService isn't needed by this flow (not added).
  - Reached from the OPTIONS screen: the Switch has no FortRise MODS menu (where TF.EX's settings sit on PC), so TF.EX's entries are
    appended to the game's OPTIONS list. tfex/NetplayOptions.cpp hooks MainMenu.InitOptions (0xaa38d0; in state Options it calls
    gen::NetplaySettings::Create) and MainMenu.DestroyOptions (0xaa3ad0; it copies the settings into the store and saves
    sd:/xplay/TF.EX.settings.json).
  - The glue TfexGlue.SettingsUi (translator/glue/SettingsUi.cs) stands in for FortRise's OptionsCreate: the five
    ISettingsCreate calls and `new UIInputText(...)` are [[rewrite]]s in translate.toml. Text entry is the software keyboard
    (swkbd ReadText, with the current value as its initial text); the typed text comes back one frame later, as UIInputText's
    callback would. FortRise's description line isn't shown (the Switch's options screen has no such line). All TFEX-DIFF S-native.
  - Translator: delegate types of instantiations the game never compiled are substituted with all-object delegates of the same
    register shape; nested lambdas reach boxed captures via the closure; a FortRise virtual override gets no vtable slot.
  - netplay.txt keeps only the Switch's connection keys (ip / rtcbind / stun). Name / server / input delay live only in
    TF.EX.settings.json (a missing file = TF.EX's defaults, then saved).
  - Test glue: hw.txt `optionsdrive 1` with `optionstext1..4 <text>` (typed instead of the keyboard, in order) drives OPTIONS ->
    NETPLAY NAME and SERVER, back, then the normal NETPLAY flow.
- **Hidden run (smoke `options`, slot update, run 080540):** the harness starts the Switch with server ws://127.0.0.1:1 (fails:
  "connect 127.0.0.1:1 failed"); on OPTIONS the driver types OPTNAME and ws://127.0.0.1:13362 into TF.EX's entries; "OPTIONS
  closed: TF.EX's settings stored (name OPTNAME, server ws://127.0.0.1:13362, input delay 32 ms, ...)", "settings saved to
  sd:/xplay/TF.EX.settings.json"; then "matchmaking thread started; server ws://127.0.0.1:13362, name OPTNAME", "ws: connected to
  127.0.0.1:13362", CreateLobby / UpdatePlayer with "Name":"OPTNAME", StartLobbyChoice, netplay_init with "Name":"OPTNAME".
- **Console check:** OPTIONS shows TF.EX's entries below the game's (NETPLAY NAME, SERVER, INPUT DELAY ...). Change NETPLAY NAME
  with the keyboard, leave OPTIONS, restart the game: the name stays, and the iPhone sees it in the lobby. Log: "[tfex] OPTIONS:
  TF.EX's netplay settings added (NetplaySettings.Create)", then on leaving "[tfex] OPTIONS closed: TF.EX's settings stored (name
  <new name>, server <url>, ...)" and "settings saved to sd:/xplay/TF.EX.settings.json"; after the restart "matchmaking thread
  started; server <url>, name <new name>".

## Match reports (2026-10-20)
The coordinator's item 9: TF.EX's MatchReport (MatchReport.cs) generated and on.
- **TF.EX's flow:** NetplayRoundLogic's first OnLevelLoadFinish calls MatchReport.Begin(the session's players) when the lobby has
  IsReportEnabled (set by the server); a DesyncDetected event marks the match (NetplayManager, generated: MarkDesync); when the replay
  is exported (ReplayService.Export: on a disconnect, on leaving the lobby, at the end) OnReplayExported sends a report if the match
  desynced ("desync") or is unfinished ("disconnect"): a zip of the player's log (+ replay, + exception) POSTed on a worker to
  <server as http(s)>/report?group=<sorted names>&name=<name>&kind=<kind>; the server's answer is logged.
- **On the Switch:**
  - MatchReport is a unit (all members); the `todo` no-op rule is gone.
  - Begin: EndOfMatch.cpp GenSetPending (the Switch's stand-in for that OnLevelLoadFinish block) calls it after NetplayManager.Init.
  - IReplayService.Export ([service] rule, svc::ExportReplay): TF.Replay isn't on the Switch (item 12), so ReplayApi's Export gives
    no file (null); ReplayService.Export's own last step, MatchReport.OnReplayExported(null), runs. Every generated Export call site
    (NetplayManager, MatchmakingService, LightPauseMenu, HUDPatch) now reaches it.
  - The BCL classes it uses that the game's code lacks are runtime stand-ins (runtime/tfexgen_bcl.cpp, overrides.toml [service]
    rules under "the BCL classes the game's code lacks"): MemoryStream (a 1 MB module buffer: the module heap can't take a report),
    ZipArchive + entry streams (a real zip: local headers, CRC-32, DEFLATE by miniz_oxide through ggrs-switch's tfx_deflate_*,
    streaming; central directory), CreateEntryFromFile (nn::fs), HttpClient.PostAsync (netws::HttpPost: http / https), File.Exists,
    Path.GetFileName, Uri.EscapeDataString, Encoding.UTF8.GetBytes. Fixed pools with magic words; running out (a second archive, more
    than 8 entries, a full buffer) is logged as an ERROR, an oversized entry is left out and the zip stays valid.
  - Task.Run(() => Upload(...)): [[rewrite]] to glue Reports.RunUpload (a game Thread, one upload at a time; S-native).
  - ReadCurrentLog: [[replace]] glue Reports.CurrentLog: the Switch's session log is the hw log (sd:/xplay/hw-log.txt: flushed, read
    without its padding), not a file in <game>/Logs (TFEX-DIFF S-native, the console's own log).
  - TFEX-DIFFs: zip entry times are 1980-01-01 (the runtime has no wall clock); OnCrash is generated but nothing calls it (TF.EX's
    Harmony finalizers on TFGame.Update / Draw catch managed exceptions; the Switch's crashes are aborts, reported by the crash
    report + hw log telemetry).
- **Translator** (general): `[service]` rules on constructors ("<Type>..ctor", exact) and `$self` in `set` rules; a logger's message
  template gets its placeholders filled (every TF.EX log line with arguments: before, "{kind}" was printed as is); Order(comparer)
  (ordinal strings); new string([.. seq]); s.AsSpan(n) as a string; Task.Wait on a TF.EX async call; char.IsAscii*; `Task?` is
  void; call [[rewrite]]s honour `in`.
- **MatchEnded's checksum (evaluation rec 11): left null, not this flow.** TF.EX's LastStateChecksum is a SHA-256 of TF.State's
  serialized state; the Switch's rollback state is rollback.cpp's memory snapshot (S-rollback), whose hash could never equal a PC
  peer's, and our server ignores the field.
- **Servers:** TF.EX's server decides IsReportEnabled. Ours (the browser repo's cloudflare/tfex-server) always sends false, and its
  /report takes only the pages' JSON performance reports: on the console nothing is sent until the browser side turns reports on and
  accepts TF.EX's zips (their decision). The harness's local server (test-page worktree, local commit) does both under TFEX_REPORTS=1
  (lobby-vs-browser LVB_REPORTS=1): it checks the zip's central directory, logs "match report accepted: ... <entries>", and keeps the
  zip in the run's out dir.
- **Rust lib:** ggrs-switch gains src/deflate.rs (miniz_oxide 0.9.1, already in the lock through flate2): publishing needs the
  Rust lib step (build.sh) for tfx_deflate_*.
- **Hidden run (smoke `report`, slot update, run 083735):** "MatchReport.Begin: the lobby has reports on (2 players)"; the page leaves
  8 s into the match; TF.EX's lobby update -> Export -> "zip: report built: 1 entry, 13 KB from 74 KB", "HttpClient.PostAsync
  127.0.0.1:13362/report?group=PLAYER-RYUJINX&name=RYUJINX&kind=disconnect (12431 bytes): status 200", "Sent disconnect report
  (12 KB), server answered 200"; the server: "match report accepted: kind=disconnect group=PLAYER-RYUJINX name=RYUJINX: 12431 bytes,
  RYUJINX_log.txt (75642 bytes, 12303 packed, method 8)"; `unzip -t` OK. No guest exception, digest clean.
- **Console check** (needs a server with reports on: today only the local harness server; on ours, check that nothing is sent):
  against the iPhone in a lobby match on the normal server, quit the iPhone mid-match: the Switch logs no "MatchReport.Begin" (our
  server: IsReportEnabled false) and no "zip:" / "Sent ... report" lines, and leaves the lobby as before. With a reports-on server: "[tfex]
  MatchReport.Begin: the lobby has reports on (N players)", then after the iPhone leaves "[tfex] zip: report built: 1 entry, ...",
  "[tfex] HttpClient.PostAsync <host>:<port>/report?...&kind=disconnect (...): status 200" and "[tfex] Sent disconnect report (N KB),
  server answered 200"; no "ERROR" line from zip / MemoryStream / HttpClient.

## Spectators (2026-10-20): stopped at the rate-independent part (the user: no 60 Hz fixes, spectators wait for 240 Hz)
The coordinator's item 10. The user, mid-item: "it is not worth making fixes for spectators to work at 60hz. We are hoping to go over
to 240hz later on". Kept: TF.EX's own spectator code, generated and on (it doesn't depend on the rate). Undone: the 60 Hz workarounds.
- **Done (generated, on):** NetplayManager's spectator members are no longer excluded (SetSpectatorMode, SetSpectatorCatchup,
  IsSpectatorCatchupEnabled, IsSpectatorMode, AddLateSpectator, FlushPendingLateSpectators, RegisterLateSpectator, AddSpectators). The
  `todo` rules are replaced by calls into the generated NetplayManager: INetplayManager.AddLateSpectator / SetSpectatorMode /
  IsSpectatorMode / IsSpectatorCatchupEnabled / AddSpectators, UpdatePlayers with the lobby's spectators (it passed none), and the
  Services.NetplayManager IsSpectatorMode / FlushPendingLateSpectators rules are gone. `_netplayMode`'s set keeps Spectator
  (svc::SetNetplayMode; GetNetplayMode 5, IsServerMode false while spectating).
- **Hidden runs (smoke `spectate`, slot update; the Switch hosts, page A plays, page B spectates):** late (SMOKE_SPECTATE=late, run
  090416): the Switch logs "server message SpectatorJoined", "Spectator <id> joined the running match", "Late spectator <id> added to
  the session" (TF.EX's), "[ggrsffi] Late spectator <id> has no WebRTC connection yet, deferring", "Deferred late spectator <id> added
  (handle 2)"; the Switch and page A stay in sync (0 of 2396 ticks). The spectator then drops: at 60 Hz GGRS's input queue (128 frames)
  is smaller than its 240-frame late-spectator send window, so a spectator joining after frame 128 is disconnected as it synchronizes
  (protocol.rs send_input_batch; at 240 Hz the queue is 512). A dropped spectator doesn't end the match. A ggrs_ffi workaround (a
  larger queue at 60 Hz) made it watch to the end (run 091011, 0/2393 and 0/2298 ticks) and was reverted (60 Hz only). The page also
  needed a test-page change to spectate at 60 Hz (SwitchLobby: a spectating page at the session's 60 Hz), reverted too.
- **Not done (waits for 240 Hz):** the Switch spectating (xplay's native start refuses a match without a seat; the gate's spectator
  path: no rollback, TF.EX's catch-up, pending notices), SpectatorRollcallGate, SpectatorSeriesTally, GameplayLayer's spectator lines,
  the input display (TF.EX's SpectatorInputDisplay only forwards to TF.InputDisplayer, a separate 1.3k-line mod with its own icons: it
  would come in built-in), and the browser's "no spectators" rule (browser repo, web/Netplay/SwitchLobby.cs SpectatorsPrefix).
- **Harness kept:** lobby-auto / lobby-vs-browser `LVB_SPECTATE=early|late` (the last of LVB_PAGES spectates: LOBBIES, Tab = SPECTATE),
  smoke `spectate` (also checks the spectating page's console: "this page spectating", "Synchronized with", "first round spawned", no
  page error). It needs the page to spectate a Switch lobby, so it fails until then (240 Hz).
- **Console check:** a lobby match against the iPhone plays as before; with a third device spectating the Switch's lobby the Switch
  logs "[tfex] Late spectator <id> added to the session" (or the spectator in "netplay_init config: ... Spectators") and the match goes
  on whatever happens to the spectator (no "every remote gone").


### Replays (item 12): held by the user (2026-10-20), findings so far (no code)
- TF.Replay records TF.State's serialized game state every frame (`Record { Inputs, State, Frame }`), and playback and seek load
  those states (`LoadGameStateBytes`). It doesn't re-simulate inputs. Replay files are TF.State bytes (schema `TF.State/x`), so
  desktop compatibility needs TF.State's own serialization.
- TF.EX's instant replay (InstantReplayFootage) keeps a TF.State keyframe every 60 ticks. At round end (Bake) it restores one up to
  ~20 s back, re-simulates to the round's end in one frame while rendering the footage, then restores the live state.
- On the Switch, `ITfStateApi` is all no-ops (S-rollback). The memory-snapshot rollback keeps only a short window and can't give a
  keyframe from 20 s back. A full heap snapshot is ~95 MB.
- TF.State is 294 files / 21k lines: GameState models (103 `[MessagePackObject]` types, LZ4 block-array compression), per-entity
  GetState / LoadState (4.7k lines), Harmony patches (3k lines).
- Files would go to `sd:/xplay/replays` in place of `FortRise/Saves/TF.Replay`. On the round-end stall, Bake re-simulates up to
  the footage length in one frame.
- Next step held: the TF.State measurement (docs/tfstate-rollback-measurement.md) decides how state capture works on the Switch.

## Dry run of the update process: v0.21.1 -> v0.22.0 (2026-10-23, report only, nothing bumped)
`tools/tfex/update.sh v0.21.1 v0.22.0` ran clean (about 3 min, both translator runs included, no failure). Findings:
- **Size:** 77 changed members in 30 files; 31 generated files change (most small; tfexgen_hooks.cpp and NetplayMenus.cpp are ~700 / ~400 lines of churn).
- **One theme:** archers that spawn without a hat (`StartsWithoutHat`, `SetSpawnHatRules`, `SpawnHatRules`, `ApplySpawnHatRule`, a Player.SpawnedInsideSolidCheck prefix).
  Lockstep (A): TF.State's Player patch + NetplayManager.Reset call `SetSpawnHatRules(null)`. Protocol (B): `Player.StartsWithoutHat` in the lobby model
  (tfex/Models.*; the browser's tfex-server and page must carry it too, or lobbies between the two disagree).
- **UI (C):** a changelog screen rework (ChangelogButton, Changelog.Releases, ChangelogPanel), MainMenu / MenuState / ScreenTitle changes.
- **Hand code to revisit:** MainMenuPatch.cpp (RollCall Enter/LeaveJoined publish the new field), xplay.cpp StartLobbyMatch (NetplayRoundLogic.OnLevelLoadFinish
  now returns when `HasSessionDisconnected()`), rollback side (`SetSpawnHatRules` on the state API: a no-op stand-in to add).
- **Translator gaps new at v0.22.0 (3):** `new bool?[n]` (ArrayCreation) and `foreach ((var h, var p) in GetPlayers())` in ArcherService.ApplyToGame;
  `DateTime.TryParseExact` in MatchReport.GetOpenedTime. All three are in UI / archer code; ask tfex-translator. The other ~230 log lines are unchanged from v0.21.1.
- **Process notes:** a fresh worktree needs `git submodule update --init vendor/fortrise` (ensure-vendor.sh does it, but check-gen noise otherwise);
  the report's "Required checks" want a Switch-vs-browser match with the browser on v0.22.0 (the page must be on the same version).
- **Not done:** the real bump (translate.sh all v0.22.0, build, smoke lobby / rematch). Expected work: the 3 translator gaps, the SetSpawnHatRules stand-in,
  Models + MainMenuPatch for StartsWithoutHat, the browser side.

## v0.23.x: what we learned (2026-10-24, deferred: the user chose v0.22.2 first)
Tags after v0.22.2: v0.23.0 ("prepare ranked", 4d3a900), v0.23.1 (b32ab0e, 2026-10-23). `update.sh v0.21.1 v0.23.1`: 471 changed members in
66 files (v0.22.2 alone: 81).
- **Ranked play is user-visible:** MainMenu's netplay screen gets RANKED (1v1 best-of-3 series), a ranked QUEUE and LEADERBOARD
  (MenuState.Leaderboard = 69; LeaderboardPanel; PauseMenu's QUIT for ranked lobbies; MatchmakingService.EnterRanked / ExitRanked /
  RequestLeaderboard, LeaderboardCacheDuration 60 s). Also: replay browser favourites / remove / sizes (03266b3), a controller-swap fix
  (b32ab0e), prism / brambles / trigger-arrow fixes, inactive variants kept out of the game state (d9f58f2, lockstep).
- **Lobby protocol (class B):** new client messages EnterRanked / ExitRanked / GetLeaderboard and server Leaderboard / ranked messages
  (Models/WebSocket/...). Ranked runs on TF.EX's official server; our tfex-server (browser repo) has none of it, so Switch-browser
  crossplay has no ranked until it does.
- **Dependencies:** TF.EX's release bundles ggrs-ffi v2.2.0 (2faa81c); the Switch (ggrs-ffi-switch) and the browser build 2fe64d9 /
  cf60182. TF.EX's own FFI calls didn't change v0.21.1..v0.23.1 (the newer ggrs-ffi matters for play against PC TF.EX, not Switch-browser).
  FortRise minimum 5.5.0-beta.4 (ac53b8b; the browser pins beta.3).
- **Translator gaps at v0.23.1 (7):** `new bool?[n]` and `foreach ((var h, var p) in GetPlayers())` (ArcherService.ApplyToGame, also in
  v0.22.x); `DateTime.TryParseExact` (MatchReport, also v0.22.x); LeaderboardPanel's `List<LeaderboardEntry> entries = []` fields (a class
  field of a model-list type comes out as `auto`: needs the model list's pointer type, and `[]` = nullptr); method groups as delegates
  (`panel.OnLoaded`, `panel.OnFailed` in NetplayMenus.CreateLeaderboard); tuple `==` (`shownGuides == (confirm, back)`, SeriesScreen.SetGuides);
  a `bool?` in an interpolated string (SeriesScreen.LogInputs).
- **Browser:** `tools/update-tfex.py --check` refuses v0.23.1 for the ggrs-ffi, FortRise and lobby-protocol reasons above.

## v0.22.2: the Switch is updated (2026-10-24, branch claude/tfex-v0.22.2); the browser's plan
**Switch (done on the branch, not merged):** gen/ at v0.22.2, vendor/tfex pinned to v0.22.2 (fb004df). Translator: `new bool?[n]`
(Opt<T> elements in a zeroed managed block), a `[service]` rule may return a snapshot (`snap = true`, IGameContext.GetPlayers), foreach
deconstruction variables are no longer hoisted (two loops reusing a name), `DateTime.TryParseExact` for fixed numeric formats
(gen::DateTryParseExact). Hand code: NetplayRoundLogic.OnLevelLoadFinish's new `HasSessionDisconnected` return in xplay.cpp's
load-finish hooks (EndOfMatch::SessionDisconnected). v0.22.2's `IsRunningUnsynchronizedUpdate` needs nothing: the Switch's gate never runs
Level.Update unsynchronized (netplay_gate.cpp). Ryujinx: 240 Hz sync test 0 mismatches; smokes against the browser page (still v0.21.1):
lobby 0/595, rematch PASS, join 0/596 (a first join run failed on the page's side, it never made its lobby; master passed the same run, and
the branch passed on the rerun: a harness flake).

**Browser plan (keep both on v0.22.2 for crossplay):**
1. `tools/fetch-tfex.sh` TFEX_VERSION v0.21.1 -> v0.22.2 (+ its zip sha256), `vendor/tfex/hosted.json`; or, if `tools/update-tfex.py` refuses the
   release's bundled ggrs-ffi (its release workflow bundles the newest ggrs-ffi at release time; the browser builds 2fe64d9), build TF.EX
   v0.22.2 from source (`TFEX_SOURCE`, the path the local-multiplayer PR already uses). Crossplay only needs the same TF.EX code on both
   sides and the same ggrs-ffi between Switch and browser (both 2fe64d9).
2. `cloudflare/tfex-server`: TFEX_PROTOCOL v0.21.1 -> v0.22.2 after checking the one protocol change, `Player.StartsWithoutHat` (Lobby.cs).
   `matchmaker.js` rebuilds each player from a field whitelist, so it must pass `StartsWithoutHat` on, or hatless spawns stay off through
   our server (both sides then read null: in sync, just without the feature).
3. FortRise: v0.22.2 doesn't raise the minimum (that came in v0.23.0, beta.4).
4. Browser-side patches that detour TF.EX internals (web/Netplay/*.cs): re-check them against v0.22.2 (ArcherService.ApplyToGame,
   NetplayManager.Reset, TFGame.Update changed).
5. Then: the Switch's smokes against a v0.22.2 page (lobby, join, rematch), the harness page rebuilt from the updated browser main.
