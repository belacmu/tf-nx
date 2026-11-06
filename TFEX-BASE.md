# TF.EX as the base of the Switch mod

Started 2026-10-16. The goal (user): the Switch mod is **TF.EX, modified to run on the Switch**, not a mod inspired by it.
Whatever TF.EX adds (a menu, a notification, a lobby panel) should reach the Switch by taking TF.EX's update, not by
re-creating a look-alike. What differs from TF.EX is a short written list of changes we need (section 3), each with its reason.

How TF.EX's code gets onto the Switch (hand port, mechanical translation, or a mix) is **not decided**: we lack the facts.
Section 5 keeps the evidence, sections 6-7 what an update takes and how to make it fast, section 8 the next experiments. Until then the hand port goes on, held to the rules in section 1.

## 0. Upstream

- `vendor/tfex/`: git submodule of https://github.com/Fcornaire/TF.EX (DShad), **pinned at v0.19.2** (`978a582`), the version
  the port mirrors. After a clone: `git submodule update --init vendor/tfex`. The release zip the browser uses is
  `~/Code/Towerfall Browser/vendor/tfex/DShad.TF.EX-v0.19.2.zip`. The real source replaces the ilspy decompile that
  TFEX-UI-SPEC.md cites (it lived in a /tmp scratchpad and is gone); the class and file names are the same.
- **License: GPLv3.** A Switch mod that is a translation or port of TF.EX is a derivative work: if we distribute it, it is
  GPLv3 with source. Decide this before any public release.
- **Port level (2026-10-16, phase 2):** **v0.21.0** (submodule moved). Still behind: ping measurement and what needs it
  (PeerMeasured, rollcall pings + ConnectionIcon on screen, InputDelayAdvisor, the in-game ping overlay), TF.EX's per-pair relay
  (needs ggrs_ffi's relay framing, ping measurement and server routes), the disconnected-player kill in 3-4 player matches, the
  failed-connection timeout (docs/ws-tfex-lockstep.md 5.2-5.4). `tools/tfex/update.sh v0.21.0 v0.21.0` lists them as "still behind".
- **Cost of one update by hand (measured):** v0.19.2→v0.21.0's B and C parts took one session about 17 minutes of its clock,
  half of it deciding where each of ~90 changed members lands. The update report makes this a work list, not a search.
- Upstream already has **v0.20.0, v0.20.1, v0.21.0** (2026-10-14/15), not ported yet: 39 .cs files, +1036 −121 since v0.19.2
  (MatchReport +194 new, MatchmakingService +198, GameplayLayer +81, ConnectionIcon +63 new, NetplayManager +50).
  Releases come every 1-3 days; about 75-85 % of each is network UI and services, 15-25 % state/replay, none in the GGRS core.

TF.EX's repo (575 .cs files, 56k lines, net10.0, FortRise 5.5 + Harmony):

| part | lines | what the Switch does with it |
|---|---|---|
| `src/network` TF.EX.Domain / Patchs / Core / Common / API | 26k | **the base**: port it (section 2). |
| `src/state` TF.State (per-field snapshots, MessagePack) | 20.5k | **replaced wholesale** by the raw-memory rollback (ROLLBACK.md). Only its *gameplay patches* (TF.State.Patchs) are ported. |
| `Domain/Externals/GGRSFFI.cs` (Rust `ggrs_ffi`) | | replaced by `ggrs-switch/` (same GGRS fork, other entry points). |
| `src/replay` TF.Replay | 7.7k | not ported. |
| `src/utilities/TF.InputDisplayer` | 1.3k | not ported. |

## 1. Rules for the port (from now on)

1. **One TF.EX file → one place in our code**, named after it (`tfex/<TF.EX class>.cpp`), with a header comment citing the
   TF.EX path and the upstream tag it was ported from. Methods keep TF.EX's names and order.
2. **No look-alikes.** When TF.EX has a component (Notification, LobbyPanel, VersusSelectBanner, Loader…), port that component:
   its fields, its Update/Render, its sprites from TF.EX's atlas. If something blocks that on the Switch, record the reason in
   section 3 before writing the substitute.
3. **Every difference from TF.EX is in section 3** (with the reason and the code location), or it is a bug. Code comments that
   mark a difference say `TFEX-DIFF:` plus the row's key, so `grep -rn TFEX-DIFF` lists them.
4. Bumping the pinned tag is a normal change: `tools/tfex/update.sh <old> <new>` (report: stale / new / overrides to review,
   each change labelled A-E, with where it lands in our code), port, `tools/tfex/manifest.py fill` for what you ported, move the
   submodule, and say in the commit which tag the port now matches. (tools/tfex/README.md.)
5. `tools/tfex/patch-targets.sh [tag]` lists TF.EX's Harmony patch targets and whether each exists on 1.4.1.6 by name
   (v0.19.2: 239 targets in 104 types; 198 found, 21 with no type on the Switch (`XGamepadInput` ×19, `ChaliceGhost`),
   6 methods missing (`LevelEntity.Init`, `ParticleType.Emit`, `Session.HandlePausing`, `Session.Update`,
   `KeyboardInput.get_MenuSaveReplay*`), 14 parse artefacts). Run it on each new tag: a new target is new work.

## 2. Status per TF.EX area (v0.19.2)

Status: **M** mirrored (TF.EX's structure), **R** reimplemented differently, **–** missing. "Need" marks a difference
section 3 justifies; "no" marks one that has no Switch reason and should become the real TF.EX code.

### Services and models (TF.EX.Domain)
| TF.EX | ours | status |
|---|---|---|
| `Services/MatchmakingService.cs` | `tfex/MatchmakingService.cpp` | M. Missing: SkinChunk, SpectatorJoined, SeriesLobby, PublishInputDelay, PublishCustomSkin, ping/ConnectionQuality, CanEditLobbySettings, RollcallReconcile. |
| `Models/WebSocket/*`, `Lobby.cs` | `tfex/Models.cpp`, `tfex/Json.cpp` | M field for field; series reduced to a bool; skin/series/spectator messages missing. |
| `Services/NetplayManager.cs` | `tfex/NetplayManager.cpp` → `xplay.cpp` StartLobbyMatch/EndLobbyMatch | R (need: S-start). No spectators. |
| `ArcherService`, `MatchVariants`, `MapScene` patch | `NetplayManager.cpp` | M. Alt skins dropped (`xplay.cpp` "v1 plays the normal archers"): no. |
| `InputService`, `Models/Input.cs` | `xplay.cpp` ToTFInput, `ggrs-switch/src/lib.rs` | M. |
| `NetplayPreferences`, `NetplaySettings`, `ServerOptionsButton` | `sd:/xplay/netplay.txt` | R (need: S-settings). No options UI: no. |
| `GGRSConfig`, `Constants` (240 Hz, input delay) | 60 Hz, `npdelay` from hw.txt | R (need: S-60hz). The lobby's InputDelay/SeriesInputDelay are not used for the session: no. |
| `Core/RoundLogic/NetplayRoundLogic.cs` | generated (`tfexgen_NetplayRoundLogic`, a real RoundLogic subclass), made by `xplay.cpp` GetRoundLogicHook | T since 2026-10-26; the MatchSettings.Mode stays the lobby's vanilla 3/4/5 (S-start). |
| `TokenService`, `SteamIdentity` | plain Identify `{player_id, name}` | R (need: S-identity). |
| `UpnpService` | STUN in `tfx-rtc` | R (need: S-transport). |
| `AutoUpdater`, `UpdaterDialog`, version check | none | – (need: S-update for the updater; the version check: no). |
| `GameFilesIntegrity`, `GameFilesManifest` | `HaveModifiedGameFiles=false` | – |
| `SkinOverlayService`, `SkinStreamService` | none | – |
| `ReplayService`, `InstantReplayFootage` | recorder nulled | – |
| `InputDelayAdvisor`, `SpectatorInputDisplay`, `Utils/Changelog.cs` | none | – |
| Test tooling (`SyncTestUtilsService`, scenarios, `DebugLayer`, `TFCommands`) | rbdrill, `vs0/vs1.txt`, hw.txt | R (need: S-tools). |
| `TFEXModModule`, DI, `Api/*`, `Interop/*` | exlaunch `main.cpp`, singletons; WiderSet = `Models.cpp` WiderSetIsWide | R (need: S-native). |

### Custom components (`TF.EX.Domain/CustomComponent`, 25 classes, 3.8k lines)
| TF.EX | ours | status |
|---|---|---|
| **Notification** (toast queue, 53 call sites) | **`tfex/Notification.cpp`: TF.EX's class, ported line by line as a real Monocle.Entity subclass** (docs/ws-tfex-subclass.md). Wired at one call site (create failed); the other messages are still fixed OutlineText lines (`NetplayScreens.cpp` Status/CodeStatus, `MainMenuPatch.cpp` seat label) or not shown ("Synchronized!", "Player left", "DESYNC DETECTED"…) | M; call sites: –, **no** |
| VersusSelectBanner | 3 OutlineTexts, no waving banner (`NetplayScreens.cpp:81`) | R, no |
| LobbyPanel, LobbyInfos | BladeButtons + OutlineTexts | R, no |
| LobbyBorderButton, LobbyVersus*Buttons | vanilla OptionsButtons; series button missing | R, no |
| NetplayButton, NetplayMenuButton, CompactCoOpButton | vanilla ArchivesButton/CoOpButton, text and icon swapped | R, **no** (since 2026-10-16 a real MenuItem subclass is possible, see S-subclass); the icon stand-in for `imgs/netplay.png`: no |
| JoinCodeEntry | swkbd (`swkbd.cpp`) | R (need: S-keyboard) |
| ChangelogPanel, LobbyUpdateFeed, LobbyVarianText, MatchEndChoices, SeriesScreen, SpectatorSeriesTally, Fader, MenuIcons, Text, Loader | none (MatchEndChoices: always ARCHER SELECT) | – |

### Patches (TF.EX.Patchs, 41 files, 34 patch classes)
| TF.EX | ours | status |
|---|---|---|
| `Scene/MainMenu.cs` (MainMenuPatch) | `tfex/MainMenuPatch.cpp`, `tfex/NetplayScreens.cpp` (states 59-65, same numbers) | M. Changelog state and input-delay advisor missing. |
| `RollCallElement`, `ReadyBanner`, `RollcallLayout` | `MainMenuPatch.cpp` hooks | M. Team labels missing. |
| `Engine/TFGame.cs` | `xplay.cpp` FrameHook | R (need: S-native) |
| `Scene/Level.cs`, `Session.cs`, `HUD.cs`, `ScreenTitle.cs` | `xplay.cpp` LevelUpdateHook, HandlePausingHook, EndRound/CreateResults; `NetplayScreens.cpp` ScreenTitle hook | M |
| `PauseMenu.cs` + `LightPauseMenu` | pausing disabled in netplay | – |
| `PlayerInput/*` (XGamepadInput, RightStickShot, ResultsVoteLock, SpectatorRollcallGate) | MenuConfirm hooks, vanilla GetState | R (need: S-input); right-stick shot and vote lock missing |
| `Scene/WiderSetMenu.cs` | the Switch's own STANDARD / WIDE screen | R (need: S-wide) |
| MatchSettings, Variant*, Versus*Button, FightButton, Loader, Music, SaveData, PlayerIndicator, GameplayLayer, ReplayViewer, VersusPlayerMatchResults… | FightAction hooked; level load via `pcgameplay.cpp` | mostly – |

### TF.State
The Domain models and every `GetState`/`LoadState` in TowerFallExtensions (16k lines, about 320 private fields through
DynamicData, 103 MessagePack classes) are **replaced by the memory copy** (S-rollback): nothing to port, and nothing to port
when they change. TF.State.Patchs change gameplay while capture is on and **must** be mirrored or the sides desync. They are, in
`rnd.cpp`, `xplay.cpp`, `dm.cpp`, `sfx.cpp`, `miasma_seq.hpp` (see docs/TFSTATE-AUDIT.md). Missing: the Spikeball.Update
coroutine→timer rewrite (docs/ws-parity.md F2), PlayerCorpse ghost counter/ReturnAsGhosts, TreasureChest (bottomless),
PlayerGhost/TeamReviver (ghost variant; TeamReviver matched to the PC in `pcgameplay.cpp`), quest/Dark World enemies, Monocle
Audio.Stop.
**Found by the manifest (tfex-update), to check:** lobbies can turn variants on (`NetplayManager.cpp` applies them), so the
variant-gated patches are needed after all: DarkPortalsVariantSequence (also spawns Bat/Slime), `Player.DropArrow` (Clumsy
Archers), `OrbLogic.DoOffsetWorldVariant`/`StartScroll`, TreasureChest (bottomless), PlayerCorpse ghost counter, PlayerGhost.
Also unported: `OrbPickupPatch` (settings guard around `OrbPickup.OnPlayerCollide`), `WhiteArcherPrison.Spawn` (TF.EX puts
`WhiteArcherPrisonBroken` in netplay: possible desync), `PlayerPatch.Player_Update_Postfix`'s kill of a disconnected player,
`GetArrowShufflePickups`' RegisterRng (GAMEPLAY-RNG-PORT A27). The desync check is a gameplay digest (`digest.hpp`), not fletcher16 over the serialized state (S-rollback).

## 3. Intentional differences (the ledger)

Each has a key for `TFEX-DIFF:` comments. A row that loses its reason gets deleted and its code becomes TF.EX's again.

| key | difference | why the Switch needs it | where |
|---|---|---|---|
| S-native | No FortRise/Harmony/DI: an exlaunch module hooking native functions by offset | The Switch game is a BRUTE build (C# converted to C++, native); no .NET runtime to load TF.EX's DLLs | `main.cpp`, `xplay.cpp`, `offsets.hpp` |
| S-rollback | Rollback restores raw game memory (page undo log), not TF.State's per-field snapshot; digest instead of fletcher16 | No reflection or MessagePack on native objects; the memory copy covers every type for free | `rollback.cpp`, ROLLBACK.md, `digest.hpp` |
| S-60hz | 60 updates per second, not 240 | Switch CPU budget; rollback resim cost | `xplay.cpp`; LOBBY-PROTOCOL.md. Means desktop TF.EX can't share a session |
| S-transport | GGRS through `ggrs-switch` + `tfx-rtc` (str0m), relay fallback; STUN instead of UPnP | No `ggrs_ffi` build for the Switch; no UPnP on the console network | `ggrs-switch/`, `tfx-rtc/`, `net.cpp` |
| S-start | Lobby match started natively by `xplay.cpp` StartLobbyMatch with MatchSettings.Mode = the lobby's 3/4/5; its round logic is TF.EX's NetplayRoundLogic (generated), made by `xplay.cpp` GetRoundLogicHook in place of FortRise's GetRoundLogic patch | No FortRise mode registry and no mode 11 on the AOT build: GetRoundLogic, GoalScore and PauseMenuType switch on Mode 0..7 and throw above it, TeamMode is inlined as `Mode == 5` | docs/TEAM-MODES-RE.md, docs/ws-netplay-roundlogic.md |
| S-mode | (2026-10-26: NetplayRoundLogic is TF.EX's own code, generated; no vanilla round logic or hand spawn in a lobby match.) What stays: the PC's Mode-11 behaviour where the game reads Mode (no HH overtime, no TeamReviver, the FFA level filter: TEAM-MODES-RE.md section 3) as `pcgameplay.cpp` instruction patches | same as S-start | `pcgameplay.cpp` |
| S-subclass | TF.EX's classes that extend game types are made at run time (`brute::MakeSubclass`: runtime type, descriptor, vtable cloned from the base), before any rollback window; a generic over a new type uses the base type's instantiation (`List<Entity>` for `List<Notification>`) | BRUTE has no compiler for new classes; the five records it needs can be built from the base's (docs/ws-tfex-subclass.md). The old meaning (reuse vanilla objects) is **no longer a reason**: those look-alikes go to section 4 | `tfex/Notification.cpp`, `tfex/Game.hpp` `brute::` |
| S-fspath | Level paths keep `/` (TF.EX v0.21.0 turns them into `\`) | nn::fs paths take `/`; the strings only meet `levels[0] == lastLevel` from the same list, so no pick changes | `pcgameplay.cpp` GetLevelsHook |
| S-input | Switch controller input, not XGamepadInput (no such type on 1.4.1.6) | different input classes | `xplay.cpp` |
| S-keyboard | Join code typed on the system keyboard, and the code shown on screen | no clipboard or keyboard on the Switch | `swkbd.cpp`, `NetplayScreens.cpp:343` |
| S-identity | Identify with `{player_id, name}`, `Platform:"Switch"` | no Steam | `MatchmakingService.cpp`, LOBBY-PROTOCOL.md |
| S-settings | Preferences from `sd:/xplay/netplay.txt` (+ rtcbind, stun) | no FortRise ModuleSettings | `NetplayPreferences::Load` |
| S-update | No auto-updater | the mod isn't installed from a zip in a Mods folder | |
| S-wide | Wide towers are built in; STANDARD / WIDE screen instead of WiderSetMenu | 1.4.1.6 ships wide towers (420/210 screens) | `MainMenuPatch.cpp`, docs/ws-wide.md |
| S-pclevels | PC level files and gameplay constants in online mode | match TF.EX players on 1.3.3.3 | `pcgameplay.cpp` |
| S-localguest | A second couch player on its own connection | Switch couch play; not in TF.EX | `tfex/LocalGuest.*`, docs/ws-archer3-local.md |
| S-tools | Test tooling is ours (rbdrill, scripted vs files, hw.txt) | runs under Ryujinx and on hardware | `xplay.cpp` |

**Proposed by the manifest work, to confirm** (now `skip`/`replace` in `tools/tfex/overrides.toml`): custom archer mods
(`ArcherDataExtensions` Exists/GetCustomArcherId…), custom variants (`ContainsCustomVariant`, `CustomVariantTitles`) and
`GameFilesIntegrity`/`GameFilesManifest` skipped as S-native; `Session` GotoNextRound / CreateResults / PlayWinnerVictoryMusic /
StartRound skipped as S-rollback (not checked on hardware); `TeamReviverPatch` as replace S-mode (`pcgameplay.cpp`);
PlayerIndicatorStatePatch, `Arrow.DoWrapRender`, `MatchVariants.Randomize`, `LevelLoaderXMLStatePatch.Reset`,
`LastManStanding.OnLevelLoadFinish` skipped (cosmetic / menu / S-rollback).

## 4. Differences with no Switch reason (to undo)

From the inventory (comments saying "v1", "look-alike", "instead of"). Each becomes TF.EX's code:
- **Notification**: the class is ported; wire TF.EX's other call sites (in-match ones included) and remove the OutlineText
  stand-ins. Before the in-match ones: a toast created between ticks (TF.EX does it from network callbacks) is erased by a rollback
  whose target is earlier (stress run: 6 of 18). Fix in the rollback glue: run such UI creations as events at the next simulated
  tick, or re-apply them after a restore. A `Canvas` made inside a resimulated tick would leak a render target.
- The other custom components and button look-alikes, now as real subclasses (S-subclass).
- VersusSelectBanner, LobbyPanel/LobbyInfos, LobbyBorderButton look-alikes; TF.EX's atlas sprites (`Content/Atlas`, incl.
  `menuAtlas/gamemodes/netplay.png`) instead of borrowed vanilla icons.
- Version check, series lobbies, MatchEndChoices (rematch), changelog, spectating, ConnectionQuality/ping colors, the lobby's
  input delay used for the session, team labels, alt skins, pause menu (LightPauseMenu), loader on create.
- Stale v1 comments: `NetplayScreens.cpp:13`, `:223`; docs/ws-lobby-ui.md "Flow (v1)".
- v0.21.0 leftovers: NoRandom clear at netplay entry (`MainMenuPatch.cpp`, TF.EX's ResolveNetplayEntry), v0.20.1's
  `ResetPolledInput()` at session start (#273: can a held input carry into a new match on the Switch?), then B and C of section 6.
- tfex-server: `MatchPlayers` (docs/ws-tfex-lockstep.md section 2: set at start, kept through rematch, cleared on archer
  select / series / end), then a test with a joiner or spectator arriving mid-match.

## 5. Compiling TF.EX for the Switch: what we know (2026-10-16)

The hybrid we are weighing: TF.EX's own C# turned into Switch code mechanically, with our section-3 changes layered on top as
overrides. Facts first, then estimates.

**What the BRUTE binary gives us (facts).** Type and method names survive (28k names, `tools/re/README.md`); every method body
is found by name (`classmeta.records`); vtables through the relocations (`relocs.py`); field records carry name (+0x80) and
offset (+0x90) (`fields.py`), but there is no per-class field walker yet. 88 % of TF.EX's patch targets exist on 1.4.1.6 by name
(section 1, rule 5). Whole game: 480 of 3,819 PC methods not found (inlined, renamed or removed content).

**What TF.EX needs (facts, v0.19.2).** 482 HarmonyPatch attributes (148 prefixes, 88 of them can skip the original; 187
postfixes; one IL transpiler; one reverse patch), no MonoMod IL hooks. 510 DynamicData uses on 384 private member names (310
of them in TF.State, which we don't need). async/Task in 16-20 files, threads in 15, LINQ 673 calls, lambdas 1,255. Libraries:
MessagePack (state/replay), Microsoft DI (96 files), Markdig (changelog only). **45 classes subclass game types** (Entity 14,
Arrow 12, MenuItem 6, …): every custom component does. The part we would compile is network Domain + Patchs + Core, about
12-15k lines.

**Measured since (2026-10-16, docs/ws-tfex-subclass.md):**
- The members TF.EX's compiled network DLLs reference resolve on 1.4.1.6 automatically: **fields 171/182 (94 %), methods
  305/338 (90 %)** (`tools/re/tfexrefs.py`; `rtypes.py` walks 14,662 types and 17,927 fields; `fieldcheck.py`: 38/38 known
  offsets agree). Misses: the Switch's other input classes, abstract getters (vtable slots), generic methods, FortRise additions.
  Private members by string (`DynamicData`, 55 names) are mostly not in BRUTE's reflection: 6 resolve, the rest need code analysis.
- **A real game subclass works** (the deciding question): Notification runs as its own runtime type, allocated, updated, drawn
  and removed by the game, scanned by the GC (Boehm is conservative: no GC bitmap needed), restored correctly by the
  raw-memory rollback (24 rollbacks, 0 differing ticks).
- Of Notification.cs's constructs, most map by rule. The 21 that needed a decision are listed with what a translator needs:
  class/vtable synthesis, statics blocks, generics over new types, lambdas → game delegates, call-site cctor runs (BRUTE checks
  beforefieldinit at each call), string literals and corlib/FNA signatures, LINQ, private fields, DI → overrides, no exceptions.
  `tools/re/genheader.py` already emits the accessor header.

**Options (estimates).**
- *Run TF.EX's IL (NativeAOT/bflat, or an embedded interpreter):* not viable. A second managed runtime and GC beside BRUTE's
  Boehm heap; it needs TLS (the Switch target has none: `ggrs-switch/aarch64-unknown-linux-musl-notls.json`), can't subclass
  BRUTE classes or hold BRUTE references safely, and lives outside the rollback memory copy.
- *Translate TF.EX's C# to C++ (Roslyn), against a header generated from the binary:* viable in principle for the network part.
  Game members become offset accesses and native calls (today's `tfex/Game.hpp`, generated); Harmony prefix/postfix become hook
  registrations; DynamicData strings resolve at translate time; DI becomes singletons; async becomes polling state machines or
  stays hand-written (the 4 service files; the networking is ours anyway). Our changes go in as a mapping file (PC member →
  Switch member or replacement) plus `switch-overrides/` that replace translated classes by name (e.g. JoinCodeEntry →
  swkbd). The open questions: synthesizing a BRUTE subclass (cloned vtable and class descriptor, Boehm allocation, GC bitmap)
  for the 45 subclasses (**answered: works**), and C# semantics that touch determinism. Rough cost 2-4 person-months,
  medium-high risk before the experiment; now the main unknowns left are LINQ, private fields and async, all in known places.
- *Hand port with automation:* the manifest (TF.EX file → our file), a per-tag diff report listing stale ported functions, the
  patch-target check, and a generated `Game.hpp`. 1-2 weeks, low risk; an update then costs about 0.5-2 days.

These aren't exclusive: the generated header and the TF.EX→our mapping help both, and the translator could start with the
custom components (UI, no simulation, small) while services stay hand-ported.

## 6. What an update looks like: v0.19.2 → v0.21.0, read hunk by hunk (2026-10-16)

7 commits over 4 days, 39 .cs files, +1036 −121. Sorted by what the Switch has to do with each change:

**A. Lockstep: gameplay or match setup that must equal the opponents' TF.EX, or the match desyncs / plays another tower.**
Small (~150 lines) but each one is a correctness issue the day a Switch plays someone on the new version.
| change | TF.EX | ours today |
|---|---|---|
| Random tower: in netplay the four unlockable towers count as unlocked while the map buttons are built, and `MapScene.NoRandom` is cleared | `Patchs/Scene/MapScene.cs` InitVersusButtons prefix/postfix; `Engine/TFGame.cs` RequestNetplayEntry; NetplayManager reset | `tfex/NetplayManager.cpp` Tower: "each side draws from its own unlocked towers" (0.19.2 rule). **Stale: a Switch with fewer unlocks picks a different tower.** |
| Level list sorted by folder then file name, `/`→`\` (Linux/Windows level order) | `State.Domain/Extensions/VersusLevelSystemExtensions.cs` | `pcgameplay.cpp` GetLevelsHook builds the PC list; order needs checking against the new sort |
| Unlock events off in a netplay session (purple archer portal, moon break, select Sunken City / TowerForge) | `State.Patchs/UnlockData.cs` (new, 4 getters) | not hooked |
| Players' sprites refreshed (DoWrapRender) once at the end of Level.Update instead of per player in Player.Update | `Patchs/Entity/LevelEntity/Player.cs`, `Patchs/Scene/Level.cs` | n/a if sprites aren't gameplay; to confirm |
| Session players = `Lobby.MatchPlayers` when the server sends it (seats, archers, teams, controllers) | `Lobby.SessionPlayers`, used in 7 places | uses `Players` |
| (found by the tools) held input reset at session start | v0.20.1 #273, `NetplayManager.Init` `ResetPolledInput()` | not ported |
| TF.State snapshot fixes (Lantern home chain, Arrow.CannotHit dead player, lastCaught depth) | `State.TowerFallExtensions/*`, `State.Patchs/Entity/LevelEntity/Lantern.cs` | not needed (S-rollback): memory copy has no such bug. Test only. |

**B. Protocol: the lobby server's messages.** Three-sided: TF.EX, our server (`Towerfall Browser/cloudflare/tfex-server`, a
TF.EX-compatible rewrite; TF.EX's own server is not in its repo) and the Switch.
New: `KickedFromLobby` (S→C), `PeerMeasured` (C→S), Lobby fields `RelayedPairs`, `IsCheckingConnections`, `IsReportEnabled`,
`MatchPlayers`; endpoints `/relay/room/<room>?peer=` and `/relay/ping_measurement/<room>`; `ping_measurement_*` gain an
`isRelay` flag. `IsAwaitingHostStart` now waits while the server checks connections.
**Note:** 0.20.0 gives TF.EX its own relay fallback ("only the pairs that can't connect go through the server"). That is the
job of our S-transport relay: adopting TF.EX's scheme would shrink a Switch difference.

**C. UI** (~250 lines): `ConnectionIcon` (new, pixel art drawn with Draw.Rect), ping overlay fading behind archers/arrows,
"CHECKING CONNECTIONS…" banner (`RenderBannerWithDots`, also used for "MATCH STARTING"), the connection icon beside rollcall
pings, five new Notification texts (kicked, connection updated, Dark World DLC required, two connect-failure messages),
InputDelayAdvisor's relay factor. All "draw game things with game functions": the most mechanical code there is, and the
part that fails our "no look-alikes" rule fastest when ported by hand.

**D. Not ours** (~450 lines): MatchReport (crash/desync upload, +194), instant replay (+90), ReplayService, Scenarios (+60),
TFCommands, LightPauseMenu (no pause menu), ModIO (FortRise path), Dark World DLC gate (always true on the Switch).

`patch-targets.sh v0.21.0`: 248 targets (9 new); 205 found on 1.4.1.6. The one new miss is `TFGame.Draw` (MatchReport's crash
finalizer, not ours).

**E. Noise:** about a fifth of the changed lines are C# 12 syntax (`= []`, `new(...)`, `[.. x]`), reordered usings, comments.

What this says: a hand port of A+B+C is a day or two **if** someone knows where each hunk goes; finding that out is most of
the work today (no record of which of our functions came from which TF.EX method). A is small but silent: nothing fails until
a match goes to a different tower. B also needs the server. C is where translation would make updates instant.

**Status (2026-10-16):** ported on `ws/tfex-lockstep`, merged here: tower pool (four towers count as unlocked, the save is no
longer changed), NoRandom cleared at match start, level order (same list on Windows/macOS: ported anyway), UnlockData getters
(the purple portal and moon break are gameplay entities on the level loader's path), MatchPlayers/SessionPlayers with a fallback
to Players, and NormaliseSeats (missing since 0.19.2). DoWrapRender and the snapshot fixes: nothing to do under S-rollback (reasoning
in docs/ws-tfex-lockstep.md). Tests: a C# model of TF.EX's selection (`docs/ws-tfex-lockstep/selection-model`) = the Switch's
live selection (self-test each boot); two lobby matches against the browser, 0 differing ticks. Not tested: wide/team lobbies, a real
v0.21.0 opponent (the browser runs 0.19.2, which unlocks everything, so it plays what v0.21.0 plays), a server sending MatchPlayers.
`tools/tfex/update.sh v0.19.2 v0.21.0` puts all 54 items of this section in the expected class (`tools/tfex/expect/`).

## 7. Design for (almost) instant updates

The target: on a new TF.EX tag, a script tells us within minutes **what changed that concerns us, where it lands, and which
class (A-E) it is**; C-type changes rebuild without a human; A-type changes come with the test that proves them; B-type
changes come with the server's to-do. Four layers, from the bottom:

1. **Upstream** (`vendor/tfex`, pinned tag): never edited.
2. **Switch platform** (hand-written, stable, rarely touched by a TF.EX update): rollback, GGRS, transport, input, the hook
   machinery, the generated game header (`Game.hpp` from `tools/re`, so `TowerFall.MapScene.InitVersusButtons` or
   `Unlocks.SunkenCity` is a name, not an offset typed by hand).
3. **TF.EX code on the Switch**: one C++ unit per TF.EX file. A manifest (`tools/tfex/manifest.toml`, kept out of the C++ so
   tooling never touches code others are editing) maps each TF.EX method to our file:function with the hash of the upstream
   body it was ported from, e.g. `"Patchs/Scene/MapScene.cs MapScenePatch.InitVersusButtons_Prefix" = { at =
   "tfex/NetplayManager.cpp Tower::Select", hash = "…" }`. This layer is hand-ported today and becomes *generated* piece by
   piece as the translator learns constructs (models first, then UI).
4. **Overrides** (the section 3 ledger as data): `tools/tfex/overrides.toml`: per TF.EX file/method, one of `port` (default),
   `skip <reason>` (MatchReport, replay, Upnp, AutoUpdater, TF.State engine…), `replace <S-key> <our function>`
   (JoinCodeEntry → swkbd), plus the PC→Switch member map for names that differ (`XGamepadInput` → the Switch input class).
   Each `replace` also stores the upstream body hash, so an upstream change to something we replaced is flagged, not lost.

**The update command** (`tools/tfex/update.sh <old> <new> [out]`, built 2026-10-16: the old tag too, since a port can be
several tags behind; manifest keys are `"<path under vendor/tfex/src> <Type>.<Member>"`), with no translator yet:
- diff the tags per method (a small C# method splitter over the two trees: Roslyn via `dotnet`, already on this Mac);
- join with the manifest and the overrides → report: *stale* (ported method changed), *new* (no port, not skipped),
  *override to review*, *skipped, ignore*;
- label each hunk A-E by path and content (TF.State.Patchs, RNG/level/tower/unlock code, `Lobby`/`Models/WebSocket` → A or B);
- run `patch-targets.sh` on the new tag (new Harmony targets missing on 1.4.1.6 = new RE work);
- for A: the required check is a Switch-vs-TF.EX match on the new version (`tools/switch-vs-browser.sh` with the browser on
  that TF.EX zip) and the rollback drill; for B: a protocol diff of `Models/WebSocket` for tfex-server.

**With the translator** (grows gradually; each stage removes a class of hand work and is useful alone):
1. `Models/WebSocket/*` → `tfex/Models.*` + JSON (pure data classes; B-type changes become automatic on the Switch side).
2. Static draw/logic helpers and patch bodies that only call game code (`ConnectionIcon`, `RenderBannerWithDots`,
   InputDelayAdvisor's math): C-type changes become automatic.
3. CustomComponents (Entity/MenuItem subclasses) **if** section 8.2 shows we can make a real game subclass.
4. Harmony patch classes → hook registration tables (prefix-can-skip, postfix, `ref __result`, `__state`), so a new patch in
   a new release is a new hook without a human, when its target resolves on 1.4.1.6.
5. Services (async/Task, websockets) stay hand-ported longest; the stale-method report covers them.

"Instant" is realistic for C and the Switch side of B. A always needs a test run against the real TF.EX, and B needs the
server; the tooling makes them **known and listed** the minute a tag appears instead of found during a desync.

## 8. Next steps

Done 2026-10-16: the field walker and coverage (1), Notification as a real subclass (2), the update report (3), v0.21.0's A part (4).

1. **Rollback-safe UI events**: toasts (and any UI) created between ticks survive a rollback (section 4). Needed before the
   in-match notifications.
2. **Wire Notification everywhere TF.EX calls it** and delete the OutlineText stand-ins; then the next component as a real
   subclass (VersusSelectBanner, LobbyPanel), by the construct list.
3. **Variant-gated TF.State patches and the other gaps** in section 2's TF.State list (desync risks when variants are on).
4. **v0.21.0 B and C**: protocol (with tfex-server, ask before touching it) and UI (ConnectionIcon, ping fade, CHECKING
   CONNECTIONS, the new messages); then move the submodule to v0.21.0. Time it.
5. **Translator, stage 1** (WebSocket models), with `tools/re/genheader.py` as its header source; the construct list in
   docs/ws-tfex-subclass.md is its spec for stage 3.
6. A v0.21.0 opponent in the browser harness (browser repo, local only).

## 9. Workstreams (started 2026-10-16, managed from the `ws/tfex-base` session)

Three sessions, each on its own branch cut from `ws/tfex-base`, files kept apart. They report to the `ws/tfex-base` session,
which reviews, merges them into `ws/tfex-base` and tells the coordinator when it is ready for `master`.

| name | job (section 8 step) | owns (only it edits these) | Ryujinx |
|---|---|---|---|
| `tfex-update` | 3: method splitter, `tools/tfex/manifest.toml` for the existing port, `tools/tfex/update.sh`, `tfex-overrides.toml`; checked against section 6 | `tools/tfex/**` | no |
| `tfex-subclass` | 1 + 2: field walker, then TF.EX's Notification as a real Entity subclass (the deciding experiment) | `tools/re/**` (new tools), `tfex/Notification.*` (new), `tfex/Game.hpp`, one call site in `tfex/MainMenuPatch.cpp` (the create-failed notification) | private slot |
| `tfex-lockstep` | 4 (A part): port v0.21.0's lockstep changes (tower pool unlocks + NoRandom, level order, UnlockData getters, SessionPlayers/MatchPlayers) | `tfex/NetplayManager.cpp`, `tfex/Models.*`, `tfex/MatchmakingService.*`, `pcgameplay.cpp`, `rnd.cpp` | private slot |

Shared files (`xplay.cpp`, `main.cpp`, `offsets.hpp`): one or two lines per session at most (a hook install), never refactors.
Each session writes its notes to `docs/ws-<name>.md`.

All three finished and were merged into `ws/tfex-base` on 2026-10-16 (notes: docs/ws-tfex-update.md, docs/ws-tfex-subclass.md,
docs/ws-tfex-lockstep.md); the merged tree builds.
Merged build tested 2026-10-16 (`tools/lobby-vs-browser.sh`, seed 42, netdelay 40 / jitter 20, lobbypred 8): tower self-test OK,
synchronized, **0 differing ticks of 2417** against the browser (TF.EX 0.19.2); the run was ended by the stall watchdog after
repeated NetworkInterrupted events on a loaded host (load ~24), not by a desync. Build note: `tools/build-probe.sh` copies
`rustlib/libggrs_spike.a` only when it's missing and make doesn't relink when it changes, so an old checkout can link a module
without `tfx_rtc_new` (crash at match start: "Unresolved symbol"); refresh the .a and delete the mirror's .elf.

### Phase 2 (started 2026-10-16, after ws/tfex-base was merged into master as 694d6c6)

The same three sessions, each having merged master first:

| session | job | owns |
|---|---|---|
| tfex-update | **the C#->C++ translator** (section 7): stage 1 WebSocket models (oracle: hand `tfex/Models.*`), stage 2 ConnectionIcon / RenderBannerWithDots, stage 3 Notification regenerated (oracle: the hand port, same behaviour in notif-test.sh), then a coverage report over TF.EX.Patchs + CustomComponent | `tools/tfex/**`, new `tfex/gen/**` (generated only) |
| tfex-subclass | **Notification end to end**: UI made between ticks survives rollback; TF.EX's call sites wired in the menu files, OutlineText stand-ins removed; the full call-site list for the others; then the next component as a real subclass | `tfex/Notification.*`, `tfex/MainMenuPatch.cpp`, `tfex/NetplayScreens.cpp` |
| tfex-lockstep | **finish v0.21.0** (B protocol, C UI, leftovers), wire the call sites in its files, move the submodule to v0.21.0, **time it** (the hand-port baseline the translator must beat); server needs written down, not changed | `tfex/MatchmakingService.*`, `tfex/NetplayManager.cpp`, `tfex/Models.*`, `tfex/Json.*`, `pcgameplay.cpp`, `rnd.cpp`, net.cpp relay/rtc |

`xplay.cpp` and `rollback.cpp` belong to the coordinator's hardware fixes: a line or two at most from these sessions.
For the coordinator, not this effort: the gameplay gaps in section 2's TF.State list (variant-gated patches, WhiteArcherPrison,
OrbPickup guard): desync risks in today's mod.

Phase 2 results, merged into `ws/tfex-base` 2026-10-16:
- **Translator** stages 1-2 (`tools/tfex/translate.sh`, Roslyn operation tree → C++, game members bound through `tools/tfex/
  binding.json` from tools/re): TF.EX's 74 WebSocket model classes = TF.EX byte for byte on 50 messages; ConnectionIcon and
  RenderBannerWithDots generated, compiled, drawn in Ryujinx (hw.txt `gendemo 1`). One hand rule so far (SpriteBatch.Begin).
  Stage 3 (Notification regenerated) in progress. `ConnectionIcon` now exists twice (hand `tfex/ConnectionIcon.*`, generated
  `tfex/gen/`): the generated one replaces the hand one once a call site exists.
- **Notification**: toasts made between ticks are replayed at the next tick, so they survive rollbacks (stress: 18 toasts, 0
  mismatches, 0 differing ticks); canvases shared, none made in a resimulated tick; menu call sites wired; all TF.EX call sites
  listed with owner (docs/ws-tfex-subclass.md). Still behind `notif 1`.
- **v0.21.0** finished as above; the hand Models/Json fixed to TF.EX's JSON (MapId default -1, Series kept, escapes, `[]`).
- **Translator stages 3-4**: Notification.cs generated as a real Entity subclass, same behaviour as the hand port in Ryujinx;
  coverage today **158 of 434 methods (36 %)** of TF.EX.Patchs + CustomComponent (`tools/tfex/reports/translator-coverage-
  v0.21.0.md`); Harmony hook registration not generated yet. BetweenTicks split out of Notification (`tfex/BetweenTicks.*`).

### Phase 3 (started 2026-10-16, late)

| session | job |
|---|---|
| tfex-update | translator coverage >70 % (properties, nullables, LINQ, overloads, TF.EX Domain units or overrides to our services, missing game types via the PC->Switch map) and **Harmony hook generation**, proven on one real patch class |
| tfex-subclass | the mod **uses** generated components: generated Notification behind the API (hand class body deleted), then VersusSelectBanner, LobbyPanel/LobbyInfos generated instead of look-alikes; the integration pattern written down |
| tfex-lockstep | ping measurement (ggrs-switch + MatchmakingService, PeerMeasured), rollcall pings with the generated ConnectionIcon (hand copies deleted), the disconnected-player kill, tfex-server changes as an unapplied patch |

Phase 2 is ready for master (docs/ws-tfex-base.md "Phase 2").
