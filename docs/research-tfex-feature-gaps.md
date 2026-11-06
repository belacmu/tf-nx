# TF.EX v0.21.1 features the Switch doesn't have yet: audit and ranked plan (2026-10-17)

This is a read-only audit (no code, no build, no Ryujinx). It lists every user-visible or protocol-visible TF.EX feature
that the Switch port lacks, or only fakes, after tfex-services group 6 and tfex-adopt units 1-8 (master 84e6ecd).
The target is **v0.21.1**, the user's decision of 2026-10-17 for both the Switch and the browser. The per-feature sections cite
v0.21.0 lines; v0.21.1 changes none of the gap features, only what V.6 lists.
For each one it says how to build it the TF.EX way, then ranks the work into parallel workstreams.

Sources:
- `vendor/tfex` (v0.21.0, its CHANGELOG and README roadmap);
- `tools/tfex/overrides.toml` (`[skip]` and the `key = "todo"` service rules);
- `tools/tfex/reports/translator-coverage-v0.21.0.md` (356/435 applicable);
- docs/ws-tfex-services.md "Still todo", docs/ws-tfex-adopt.md, docs/ws-tfex-lockstep.md, docs/research-fortrise.md, docs/research-couch-online.md;
- TFEX-BASE.md sections 2-6, LOBBY-PROTOCOL.md, TFEX-UI-SPEC.md, STATUS.md;
- the browser repo: worktree `switch-rtc`, `web/Netplay/SwitchLobby.cs`, and `cloudflare/tfex-server`.

TF.EX paths are relative to `vendor/tfex/src/network/`. Abbreviations: NM = `TF.EX.Domain/Services/NetplayManager.cs`,
MMS = `TF.EX.Domain/Services/MatchmakingService.cs`, IS = `TF.EX.Domain/Services/TF/InputService.cs`, MM =
`TF.EX.Patchs/Scene/MainMenu.cs`, TFG = `TF.EX.Patchs/Engine/TFGame.cs`. Switch paths are relative to
`hw-probe/exlaunch/source/program/`. BR = the browser repo's `switch-rtc` worktree.

## V. Headline: the Switch ports v0.21.0, the browser runs 0.19.2; both move to v0.21.1

**The browser plays stock TF.EX 0.19.2** (BR `docs/MULTIPLAYER.md:10`, `vendor/tfex/DShad.TF.EX-v0.19.2.zip`; the browser repo holds no
TF.EX source, and a decompiled 0.19.2 is at `.cache/tfex-0.19.2/src`). The Switch ports v0.21.0 (`vendor/tfex` at 04ef5d0), and so do
the translator, the services and the v0.21 lobby fields. The server is our own 0.19.2-protocol rewrite.

Everything that changed between the two is below. Sources:
- `tools/tfex/reports/v0.19.2-v0.21.0.md` (174 changed members in 39 files; all 54 items of the hand reading confirmed);
- `git diff v0.19.2 v0.21.0` in vendor/tfex (7 commits: 3eeaeb0 relay, e9c8103 reports, c9b4f80 Linux level order, 779be98 #273,
  e52db68 DLC gate #274, daa6de7 "lantern, arrow, shooting", 04ef5d0 random-tower exclusion and unlock events);
- the browser's own patches (BR `web/BrowserHost.cs:332-370` UnlockArchers, `web/Netplay/SwitchLobby.cs`).

### V.1 Gameplay and match setup (desync-relevant)

The "Switch" column is what master does today: the v0.21.0 rule. The browser column is 0.19.2 as patched by the browser.

| change (0.19.2 → 0.21.0) | TF.EX | browser 0.19.2 | Switch | can the pair desync? |
|---|---|---|---|---|
| Random tower pool: the four unlockable towers count as unlocked while the map buttons are built | `TF.EX.Patchs/Scene/MapScene.cs:62-97` | uses its own unlocks, **but the browser sets all four for every player** (BrowserHost.cs:341-366) | v0.21 rule (`tfex/NetplayManager.cpp` Tower) | **no**: same pool (the selection model: v0.21.0 = 0.19.2 with all unlocked, docs/ws-tfex-lockstep.md section 3) |
| `MapScene.NoRandom` cleared on netplay entry and on reset | TFG:524, NM:848 | **honours the player's own random exclusions** | cleared (v0.21) | **yes, if the browser player has excluded a tower from random**: MapId −1 then draws from a different list. Rare; only random-map lobbies |
| Level list sorted by folder then name, `/` → `\` | `state/TF.State.Domain/Extensions/VersusLevelSystemExtensions.cs` | file-listing order | sorted (v0.21; `/` kept: S-fspath) | no for the browser's files today (verified: "0.21.0 == 0.19.2 for every seed and tower", same levels in the runs). It would differ only if the browser's file listing stopped being alphabetical |
| Unlock events off in an EX session (purple portal, moon break, Sunken City / TowerForge auto-select) | `state/TF.State.Patchs/UnlockData.cs` | live, **but the browser's flags make all four getters false** (PurpleArcherRevealed, Moonstone, SunkenCity, TowerForge set) | suppressed (v0.21, `rnd.cpp`) | **no** with the browser's UnlockArchers |
| Session players = `Lobby.MatchPlayers` when the server sends it | `Models/WebSocket/Lobby.cs` SessionPlayers | Players | MatchPlayers, falling back to Players | **no**: our server never sends MatchPlayers |
| ResetPolledInput at session start (#273) | NM:170 | absent (0.19.2 can start a match with a held input) | n/a (no accumulator, section 1.1 C) | no (inputs are exchanged, not recomputed) |
| Player sprites refreshed once per Level.Update (archer-render desync *warning*) | `TF.EX.Patchs/Entity/LevelEntity/Player.cs:48`, Level.cs | per player | n/a (S-rollback, the digest has no sprite state) | no |
| **TF.State snapshot fixes**: an arrow's `CannotHit` when the shooter is dead; `lastCaught` arrow no longer in the level (catch with a full quiver, then pick up); a falling lantern's home chain | `state/TF.State.TowerFallExtensions/Entity/LevelEntity/Arrow.cs:52`, `Player.cs:146-158`, `Lantern.cs:62-70` + `TF.State.Patchs/Entity/LevelEntity/Lantern.cs` | **has the bugs**: a rollback *on the browser* across one of these restores the wrong state | not affected: memory rollback restores exact bytes (S-rollback) | **yes, on the browser's side**: after a browser rollback in those situations the browser diverges from the correct simulation, which the Switch runs. The Switch can't fix this; only the browser moving to v0.21.0 does |
| Dark World DLC required online | TFG:466 | not required | always true | no |
| Instant replay blocks EndRound while baking | `TF.EX.Patchs/Session.cs` | – | off: the Switch sends UseInstantReplay false, so TF.EX's all-players rule turns it off for everyone | no |

So for gameplay, **the Switch's v0.21 rules equal the browser's 0.19.2 as the browser configures it**, with two exceptions:
- a browser player's NoRandom exclusions (rare; the fix is two lines in SwitchLobby.cs: clear NoRandom in a Switch lobby);
- 0.19.2's own TF.State restore bugs, which desync the browser after its own rollbacks. These can't be fixed on the Switch.

### V.2 Lobby protocol

Everything 0.20/0.21 added is additive, and each side tolerates the other's absence:
- `PeerMeasured` (C→S): the Switch sends it; our server drops unknown messages (`matchmaker.js:109`, `Object.hasOwn(handlers, name)`).
- `KickedFromLobby` (S→C): the Switch handles it; our server never sends it.
- Lobby `RelayedPairs`, `IsCheckingConnections`, `IsReportEnabled`, `MatchPlayers`: the Switch reads them when present; the server
  drops unknown client fields and never sends them.
- `IsAwaitingHostStart` waiting while connections are checked never triggers.
- `/relay/room/`, `/relay/ping_measurement/`: TF.EX's relay fallback is unused. The Switch's relay is our own (S-transport), and the
  browser in a Switch lobby uses `TowerFallBrowser.Relay` or switch-rtc WebRTC.

**Nothing in the protocol differs in a way that breaks a lobby today.** If the browser moves to v0.21.1 on today's server, the
v0.21 client would also tolerate the missing fields. That is how TF.EX reads them: null/false defaults.

### V.3 GGRS FFI and native transport
- 0.20.0 changed the `ping_measurement_*` signatures (an `isRelay` flag) and added relay peers to `NetplayServerConfig`/`NetplaySpectatorConfig`.
- 0.21.0's "improved sent network packets for unstable connections" is inside TF.EX's native netplay library.
- **Neither side of our pairing runs TF.EX's native library.** The browser builds its own ggrs-ffi (BR `netplay/`, `ggrs-ffi.patch`),
  and the Switch runs ggrs-switch + tfx-rtc. Wire compatibility is between those two, independent of the TF.EX tag. A browser move
  to v0.21.1 needs only its FFI imports updated for the new `ping_measurement_*` signatures; the browser's lib can take and ignore
  `isRelay`.

### V.4 UI only
ConnectionIcon, ping fading, the CHECKING CONNECTIONS banner, five notifications, InputDelayAdvisor's relay factor, MatchReport and
the instant replay viewer. None reach the other peer.

### V.5 Decided (the user, 2026-10-17): everything targets TF.EX v0.21.1

The Switch and the browser both move to the latest TF.EX, **v0.21.1** (tag in vendor/tfex, released 2026-10-15/16). The Switch is on
v0.21.0 today. For the Switch's bump, see V.6; for the browser's list, see section B below.

### V.6 v0.21.0 → v0.21.1 (`git diff v0.21.0 v0.21.1`, 38 files, +602 −85)

| change | TF.EX (v0.21.1) | relevance for the Switch | desync / lobby? |
|---|---|---|---|
| Lobby URL `/ws?version=<TF.EX version>`; new server message **`UpdateRequired {version}`** → HandleUpdateRequired: reset lobby and peer, "ONLINE PLAY REQUIRES TF.EX x", back to Main | MMS (MATCHMAKING_URL, HandleUpdateRequired) | **new protocol**: the Switch must send `?version=0.21.1` (`tfex/MatchmakingService.cpp:240` opens `/ws`) and handle UpdateRequired once a server enforces versions. Our server ignores both today | lobby (a future server that enforces it would refuse a Switch without `?version=`) |
| New server message **`Notice {text}`** → a 10 s Notification | MMS, `Models/WebSocket/Server/NoticeMessage.cs` | handle it: a toast through the ported Notification (tfex-adopt, `MatchmakingService.cpp` message switch) | no (UI) |
| Host left: the menu change runs on the game thread | MMS | the Switch already acts on the game thread (`g_lobbyClosed`); the target state is NetplaySelect (W.2 item 4) | no |
| Failing to leave a lobby → LobbyBrowser (was VersusOptions) | MMS | the hand LeaveLobby fail path: check it goes to LobbyBrowser | no |
| `NoRandom.Clear()` on NetplayManager reset only when a session or mode was set | NM:845-851 | the Switch's NoRandom clear (`tfex/NetplayManager.cpp` / MainMenuPatch entry): match the condition | no (offline only) |
| Leaving the online menus: ClearNetplayMode + NetplayOptions.Restore + ResetMode + ResetVersusChoices ("co-op no longer tries to play online") | `TF.EX.Patchs/Scene/MainMenu.cs` | the Switch: `pcgameplay::Set(false)` / ClearNetplayMode on leaving. Check that co-op after NETPLAY is offline (hardware checklist line) | no |
| `RemoveStaleMenuItems` on a state's Create | `TF.EX.Patchs/Scene/MainMenu.cs` | a menu robustness fix for TF.EX's states: the generated CallStateFunc path or NetplayScreens' Create (adopt) | no |
| Input patches (KeyboardInput / XGamePadInput / PlayerInput: instant-replay skip in local matches) | `TF.EX.Patchs/PlayerInput/*` | S-input / D (no instant replay) | no |
| [State] brambles: sound after a rollback, brothers re-linked; a bolt arrow destroyed by a catch shock no longer returns (pending removals kept on load) | `state/TF.State.TowerFallExtensions/Level.cs:830-845`, BrambleSpreadController | S-rollback: the memory copy restores exact bytes (pending-removal sets included). Nothing to port; test only | for the browser: these are TF.State restore bugs it has until it moves |
| [State] `Arrow.Init` counter reset only while capture is active | `state/TF.State.Patchs/Entity/LevelEntity/Arrow.cs:91-100` | the Switch has no such reset (xplay.cpp:1595 notes Arrow.Init); offline only | no |
| [State] LocalSettingsGuard / RegisterRng restores in Finalizers instead of Postfixes (TreasureSpawner ctor, GetRandomArrowType, GetArrowShufflePickups, GetChestSpawnsForLevel, OrbPickup) | `state/TF.State.Patchs/Entity/TreasureSpawner.cs`, `OrbPickup.cs` | exception-safety only; the Switch's RAII guards (`RegGuard`, `UnlockGuard`) already restore on every exit. **F2 unchanged**: GetArrowShufflePickups still runs under RegisterRng (item B) | no |
| `ModesExtensions.ToModel`: FortRise modes ending in "/Netplay" → Netplay, other custom → Custom | `state/TF.State.Domain/Extensions/ModesExtensions.cs` | S-mode (no FortRise modes); `IsNetplay` is the Switch's own | no |
| Replay: SaveProgressGuard, seeking, missing tower, Workshop | `src/replay/*` | D | no |
| AutoUpdater `GetCurrentVersion`, ScenarioCatalog | | S-update / S-tools, except the version string the URL needs | no |

**For the Switch:**
1. `tools/tfex/update.sh v0.21.0 v0.21.1` and the vendor bump (tfex-translator: the gen/ diff and coverage).
2. The `?version=` query plus the UpdateRequired and Notice handlers (tfex-adopt, hand MatchmakingService; a constant for the
   version, S-update), and the two menu-state details above.

None of it changes the simulation.

### V.7 The console desync of 2026-10-17 (an arrow took only the hat on the PC, killed on the Switch)

The desync session owns the analysis; this note only answers "is it a version difference?".
- **The hat rule itself is not a TF.EX version difference.** TF.EX touches hats in one place, the same in both versions:
  `Player.LoseHat` wrapped in RegisterRng (0.19.2 `TF.State.Patchs/.../PlayerStatePatch.cs:48-60`; the Switch's `rnd.cpp:398`
  LoseHatHook). Whether an arrow takes the hat or kills is decided by the game's own Player/Arrow code. That is a PC 1.3.3.3 vs
  Switch 1.4.1.6 question (tools/re/parity.py on the Player arrow-hit path and the Hat classes; docs/PARITY-TRIAGE.md lists
  SteelHat/WoodenHat Update/CollideH/CollideV only as constant-folding differences).
- **A version-linked candidate exists on the browser side.** 0.19.2's TF.State saves an arrow's `CannotHit` as "has an unhittable
  entity" even when that entity is a dead player. On a restore it then keeps whatever `CannotHit` the arrow holds in the current
  timeline (0.19.2 `ArrowExtensions.cs:90,249-252`). That is the bug daa6de7 fixed in 0.21.0 ("desync when an archer dies right after
  shooting"). The `lastCaught` row is similar.
- If the browser log shows a rollback whose window covers that arrow's flight, this is the first suspect. If there was no rollback
  on the browser in that window, the version difference is ruled out; look at the game-code parity and at the Switch's own rollback.

## B. What the browser must change: TF.EX 0.19.2 → v0.21.1 (for the browser session)

Browser repo (BR = worktree `switch-rtc`; the Switch-interop code is only on `main` and `switch-rtc`, not on `perf/ahead-threshold`):
1. **Swap the TF.EX zip:** `vendor/tfex/DShad.TF.EX-v0.19.2.zip` → v0.21.1 (and the TF.State / TF.Replay / TF.InputDisplayer DLLs
   that come with it). FortRise: v0.21.x targets 5.5.0-beta.2; the browser runs beta.3 (check that it loads; the FortRise member
   list is in docs/research-fortrise.md).
2. **Native netplay lib (BR `netplay/`, `ggrs-ffi.patch`):**
   - the 0.20 signatures `ping_measurement_start(url, isRelay)`, `ping_measurement_stats(peer, isRelay, out stats)`,
     `ping_measurement_stop(isRelay)`;
   - relay peers in `NetplayServerConfig` (RelayUrl, RelayPeers) and `NetplaySpectatorConfig.RelayUrl`: accept and ignore them while
     `RelayedPairs` stays empty;
   - check every `DllImport` in `TF.EX.Domain/Externals/GGRSFFI.cs` v0.21.1 against the lib's exports (`netplay_set_pending_checksum`
     from PR #64 stays: GameplayDigest).
3. **Re-target the browser's Harmony patches** (each names TF.EX members that changed between 0.19.2 and v0.21.1):
   - `web/Netplay/SwitchLobby.cs`: `NetplayManager.UpdatePlayers` (now fed from `Lobby.SessionPlayers`), `AddSpectators` /
     `AddLateSpectator` (moved: NM:1085/1129), the lobby-mod injection.
   - `TfexFps.cs`: the `GGRSConfig.Fps` getter, `NetplayPreferences.ToFrames`, and the advisor's 4.1667 ms/tick constant
     (`InputDelayAdvisor` gained `RelayRollbackFactor`).
     **Also fix the 240/60 unit bug:** an advisor-applied `_sessionInputDelay` and the published `InputDelay` are in 240 Hz ticks
     while a Switch session runs at 60 (section 1.2 F).
   - `GameplayDigest.cs`: the checksum hook point; v0.20+ `AdvanceFrame` ends the match on a mismatch ("DESYNC DETECTED - match
     ended", NM:510-555). With the digest on both sides that is the intended behaviour. Make sure a one-sided digest can't end matches.
   - `ReplayKeyStates.cs`, `ReplayRecordFix.cs`: replay changed in 0.20/0.21.1 (SaveProgressGuard, StandaloneRecorder).
   - `?tfexplatform=Switch` (instant replay off), XplayLobby / XplayNet harness commands.
4. **New v0.20-v0.21.1 behaviour to check in the browser:**
   - the Dark World DLC gate (TFG:466: the browser ships DarkWorldContent; it must read as installed);
   - NoRandom cleared online (removes V.1's exclusion difference);
   - MatchReport: off while the server leaves `IsReportEnabled` false;
   - the `?version=` query on `/ws`;
   - instant replay changes (still off in Switch lobbies through UseInstantReplay);
   - the CHECKING CONNECTIONS / relayed-pair paths stay idle on today's server.
5. **Server (BR `cloudflare/tfex-server`), optional for the switch-over:**
   - accept and ignore `?version=` (it does), then decide whether to send `UpdateRequired` / `Notice`;
   - the v0.21 patch (docs/ws-tfex-lockstep/tfex-server-v0.21.patch: MatchPlayers, IsCheckingConnections + PeerMeasured,
     KickedFromLobby, IsReportEnabled) — MatchPlayers is needed before spectators (L);
   - the relay stays ours (S-transport; TF.EX's needs UDP).
6. **Tests:**
   - browser vs browser on v0.21.1 (the public site's own play);
   - xnet and xlobby vs the Switch in Ryujinx with the digest (0 differing ticks), including a random-map lobby and a team lobby;
   - one run with rollbacks (`?netdelay=`).
   - Deploying the site needs the user's approval (it changes every player's TF.EX version).

## W. The Switch's custom lobby: is it a good solution?

Question from the user: "We made a custom solution for the switch lobby, but we don't know if it's a good one or not."

Files read:
- `tfex/NetplayScreens.cpp` (593 lines: NetplaySelect, LobbyBrowser, LobbyBuilder, PrivateSelect, PrivateJoinCode, QuickPlaySearch);
- `tfex/MainMenuPatch.cpp` (1055 lines: the NETPLAY button, the lobby screen = the vanilla roll call driven by hooks, the end-of-match
  vote, LocalGuest, the hw.txt test driver);
- `tfex/MatchmakingService.cpp` (643 lines: the WebSocket client and the lobby state);
- `tfex/NetplayManager.cpp` (443 lines: match setup from the lobby).

### W.1 What is TF.EX's and what is custom

| part | TF.EX's (port or generated) | custom (Switch-only design) |
|---|---|---|
| protocol, models, JSON | messages and fields mirror TF.EX field for field (`tfex/Models.*`; the generated Models are byte-identical, ws-tfex-update) | identify as `Platform:"Switch"` (S-identity) |
| menu states | TF.EX's state numbers 59-65, entered through `MainMenu.CallStateFunc` exactly as TF.EX does (NetplayScreens.cpp:463-486); ScreenTitlePatch generated | – |
| screens' look | generated: Notification, VersusSelectBanner, LobbyPanel + LobbyInfos, ConnectionIcon/pings, ReadyBanner, RollcallLayout, FightButton | vanilla objects as stand-ins: NETPLAY = an ArchivesButton (MainMenuPatch.cpp:9-14, `IsNetplayLook`), LobbyBuilder = vanilla OptionsButtons (NetplayScreens.cpp:281-283), seat status = OutlineText labels (`ShowSeatLabel`, MainMenuPatch.cpp:380-410) |
| lobby screen | the vanilla roll call, as in TF.EX (TF.EX also reuses `RollcallElement`) | **TF.EX's RollCallElementPatch + ReconcileRollcall are re-implemented as a per-frame pull**: each element reads its seat from `GetOwnLobby()` in its Update hook (`NotJoinedUpdateHook` / `JoinedUpdateHook`, MainMenuPatch.cpp:548-686). TF.EX instead *pushes* each LobbyUpdate once, on the game thread (MMS:906 → `ReconcileRollcallIfPending` :1290) |
| match start / rematch | `IsLobbyReady`, StartLobbyChoice, the map screen (docs/ws-map-select.md) | started natively by `NetplayManagerStartLobbyMatch` when `IsLobbyReady()` is seen in the roll call (MainMenuPatch.cpp:982-988; S-start) |
| end of match | votes go out as TF.EX's messages | the vanilla VersusMatchEnd pause menu with REMATCH!/ARCHER SELECT/QUIT hooked (`PauseRematchHook` etc., :743-765) instead of MatchEndChoices + PauseMenuPatch |
| input | – | the first local controller is bound to the lobby seat by hand (:572-576); no input lock (H); the join code comes from the system keyboard (S-keyboard) |
| couch | – | LocalGuest / controller 2 (S-localguest, docs/ws-archer3-local.md) woven into the same hooks |
| tests | – | the hw.txt UI driver (`AutoUi`, `lobbyui`, `lobbypad2`, `lobbyvote`, …) lives in the product file (MainMenuPatch.cpp:784-935) |

### W.2 Verdict

**The architecture is sound and close to TF.EX where it matters for correctness:**
- the wire protocol, the models and the state machine (state numbers, CallStateFunc dispatch) are TF.EX's;
- every value that feeds the simulation is re-derived from the lobby at match start (`tfex/NetplayManager.cpp`: SessionPlayers,
  NormaliseSeats, ResolveArchers, variants, teams), which is TF.EX's own `RestoreArchersFromLobbyIfNeeded` safety net;
- so a roll call display glitch can't become a desync;
- it is proven end to end in Ryujinx: host, join, private, quick play, editing, 3 players, wide/team (STATUS.md, ws-tfex-adopt.md).

**What is weaker, by risk:**
1. **No unready follow-up (lobby, user-visible).** TF.EX's `UnreadyOwnRollcall` (MMS:1254-1287) drops your portrait when the server
   un-readies you. Our server does that to everyone on `UpdateLobbySettings` (BR `matchmaker.js:724-726`).
   - The Switch's `JoinedUpdateHook` never checks its own `Ready` (MainMenuPatch.cpp:672-685), so after a host edit the Switch player
     still looks joined while the server says not ready. The generated ReadyBanner reads the server and says not ready.
   - The host can't start, and the player has to press B then A without knowing why.
   - Fix: in JoinedUpdate, if `me->Ready` went true → false, run TF.EX's UnreadyOwnRollcall (leave the portrait, state 0). This is
     tfex-adopt's file, about 10 lines. Better: the generated reconcile (item I).
2. **Pull instead of push (maintainability, edge cases).** Re-reading the lobby per element per frame works, but it is not TF.EX's
   ReconcileRollcall. These have no counterpart:
   - the "X joined" / "Player left" toasts;
   - the platform/modified-files warnings;
   - `HandleControlChange` when the server moves the local seat (a seat change mid-lobby leaves the local element bound to the old seat);
   - `ApplyGameSettingsToMatch` per update.

   It is the main reason items I/J don't simply drop in. Replacing it with the generated reconcile (I) removes most of
   `MainMenuPatch.cpp:412-500`.
3. **Stand-in widgets.** ArchivesButton for NETPLAY, OptionsButtons for the builder, OutlineText seat labels. Functionally fine, but
   each is a look-alike TFEX-BASE section 4 says to undo, and each blocks a TF.EX feature: the builder has no series button and no
   variants page, so Switch hosts can't offer variants. The generated replacements exist (NetplayMenuButton 10/10, LobbyBorderButton +
   five LobbyVersus*Buttons all complete).
4. **Lobby closed → main menu, not NetplaySelect.**
   - TF.EX, when the host leaves in the menu: `State = NetplaySelect` (MMS:953-962).
   - Switch: `ConsumeLobbyClosed` → `MainMenu::Main` (MainMenuPatch.cpp:979).
   - Small parity fix (adopt).
5. **No input lock** (H): double-submits are prevented by ad-hoc flags (`g_joining`, the 3 s code cooldown, `g_voted`) rather than
   TF.EX's lock-until-reply.
6. **Test driver and couch code inside the product hooks.**
   - The hw.txt driver (~150 lines) and LocalGuest branches run through the same Update hooks as real play.
   - A real bug hides there: `MainMenuPatch.cpp:659` `if (g_guestNet && ch != g_guestSentArcher || alt != g_guestSentAlt)`. `&&`
     binds tighter than `||`, so in local-guest mode an alt change calls PublishGuest. It returns early without a guest peer, so it is
     harmless today, but wrong.
   - Moving the driver to its own file (it only needs the press bits) would make the product path readable.
7. **Match start from a roll-call poll** (S-start): it is started when `IsLobbyReady()` is seen while the menu is in Rollcall.
   - TF.EX starts on the `StartLobby` message → StartVersus → MapScene.
   - Equivalent today, but a StartLobby that arrives while the Switch is on another state (e.g. the builder while editing) waits
     until it returns to the roll call.
   - Worth a comment or a check that the server can't start while the host is editing.

**Recommendation:** keep the design (protocol, states, start-time re-derivation) and finish turning it into TF.EX's code, in this
order:
- (1) the unready fix now (small, user-visible);
- (2) the generated ReconcileRollcall replacing the per-element pull (item I);
- (3) the LobbyBuilder buttons, then NetplayMenuButton, replacing the stand-ins (N; ws-tfex-adopt.md item 8);
- (4) the input lock (H), then MatchEndChoices/PauseMenuPatch replacing the vote hooks (M);
- (5) move the hw.txt driver out of MainMenuPatch.cpp.

None of this needs a redesign. The custom parts are the ones TF.EX's own code will replace as the translator and adoption go on.

## 0. Two facts that shape the ranking

1. **The peer the Switch plays today is TF.EX 0.19.2, not 0.21.0.**
   - The browser loads the unmodified `DShad.TF.EX-v0.19.2.zip` (BR `docs/MULTIPLAYER.md:10`).
   - Our tfex-server (BR `cloudflare/tfex-server`) implements the 0.19.2 protocol. It has no `KickedFromLobby`, no
     `IsCheckingConnections`/`RelayedPairs` and no `MatchPlayers`. The v0.21 server patch (docs/ws-tfex-lockstep/tfex-server-v0.21.patch)
     is not applied.
   - So 0.20/0.21-only *features* can't break a match against today's browser. The few 0.21 *gameplay rules* the Switch already
     follows are covered in section V, with the two places where they can still differ.
   - Everything 0.19.2 already had (spectators, series, skins, the input-delay advisor, the input lock, the reconcile, the abort path)
     is live on the other side now.
2. **In a lobby with a Switch player, the browser already hides several features** (BR `web/Netplay/SwitchLobby.cs`):
   - it drops spectators from the GGRS config and blocks `AddSpectators`/`AddLateSpectator` (`:58-59,86-88,118,137-142`);
   - it forces 60 Hz (`:120`);
   - it sets the `TowerFallBrowser.Relay` and `WiderSet` lobby mods (`:145-168`).

   The Switch also refuses to join InGame and series lobbies (`tfex/MainMenuPatch.cpp:278-288` WhyNotJoinable).

   **Most gaps are therefore "hidden feature" today, not "broken match".** The exceptions are the gameplay gaps in 1.1-1.2, which a
   browser host can reach today by turning on a variant.

How "browser dependence" is graded below:
- **desync**: the simulation differs.
- **lobby**: a lobby or menu flow gets stuck or goes out of step.
- **hidden**: the feature is simply absent on the Switch side.
- **none**: nothing on the other side changes.

## 1. Gaps, by area

### 1.1 Gameplay determinism (desync-relevant)

**A. Variant-gated TF.State patches: Bottomless Chests, Return As Ghosts, Dark Portals (with Bat and Slime)**
- **TF.EX:**
  - `state/TF.State.TowerFallExtensions/Entity/LevelEntity/TreasureChest.cs:39,86` (bottomlessCounter);
  - `state/TF.State.TowerFallExtensions/Level.cs:207,668,1601,2199` (PlayerGhost) with `Entity/LevelEntity/PlayerCorpse.cs`/`PlayerGhost.cs`;
  - `state/TF.State.Patchs/DarkPortalsVariantSequence.cs:34` (`Step`: the coroutine turned into a phase machine, stepped from
    `state/TF.State.Core/Api/TfStateApi.cs:76`);
  - Bat/Slime state at `Level.cs:209,617,944,1621,1633,2271`.
- **Switch:**
  - Nothing for these three. Clumsy Archers, Offset World and Always Scrolling are ported (STATUS.md; Always Scrolling is untested).
  - The vanilla coroutine/`new Random()` paths run instead.
  - `VariantsExtensions::HasAll` (`tfex/NetplayManager.cpp:90`) accepts these titles because the Switch knows them. A Switch joining
    a browser-hosted lobby with one of them on plays the vanilla code.
- **Browser dependence:** **desync**, reachable today (the browser host's VARIANTS page).
- **How to build:** hand port into the memory-rollback world (S-rollback).
  - The memory copy already restores entity fields, so only the *RNG streams* and *coroutine → counter* changes matter.
  - The plan and test recipe are in docs/ws-tfex-lockstep.md "Next steps".
  - The translator doesn't cover `state/` (no TF.State units). These are rnd.cpp-style hooks.
- **Size:** chests ½-1 day; ghosts 1 day; Dark Portals plus Bat/Slime 1 day+.
- **Stopgap (hours):** add the three unported titles to the join refusal (HasAll's caller WhyNotJoinable, `tfex/MainMenuPatch.cpp:285`),
  so the lobby list shows "CAN'T JOIN - MISSING VARIANTS". This is TF.EX's own refusal text.
- **Depends on:** nothing.

**B. FortRise F2: ArrowShuffle's second shuffle + TF.State A27 RegisterRng**
- **TF.EX/FortRise:**
  - `patch_TreasureSpawner.cs:251-265` (FortRise);
  - `state/TF.State.Patchs/Entity/TreasureSpawner.cs:65-86` (RegisterRng around `GetArrowShufflePickups`).
- **Switch:** `rnd.cpp:524` ArrowShuffleHook applies only the UnlockGuard. There is no RegGuard and no second `Calc.Shuffle`.
- **Browser dependence:** **desync**. The browser runs FortRise 5.5.0-beta.3, so it has F2. Reachable when a lobby turns ArrowShuffle
  on, or on a tower with `arrowShuffle`.
- **How to build:** hand port, about 20 lines (`RegGuard g;` + a second shuffle on the gameplay stream, docs/research-fortrise.md
  item 1), then one lockstep run with the variant on.
- **Size:** 1-2 h + 1 run.
- **Depends on:** nothing.

**C. ResetPolledInput (v0.20.1 #273)**
- **TF.EX:**
  - IS:223 resets the accumulated `PolledInput`;
  - called per tick after `netplay_advance_frame` (TFG:625), at `NetplayManager.Init` (NM:170, the #273 fix), and in LightPauseMenu
    Close/Quit (:159, :189).
  - #273 fixed a press left over from quitting a match becoming the first GGRS input of the next one.
- **Switch:**
  - service rule todo no-op (`overrides.toml:422`).
  - The Switch does **not** accumulate. `xplay.cpp` NpPadInput (`:2408`) samples `PlayerInputs[seat].GetState()` once per advanced
    frame, and the game's own InputState computes pressed edges.
  - So the bug class mostly doesn't exist. The one remaining case is a button *held* through the level load.
- **Browser dependence:** none (only the local input sent through GGRS; the peer receives whatever we send).
- **How to build:**
  - Mostly n/a by design. Record it as a ledger entry (S-input: "no polled-input accumulator; NpPadInput samples per frame") and turn
    the todo into a documented no-op.
  - Optional check: hold A through a lobby match start in Ryujinx and look at the first `pressed` bits.
- **Size:** ½ h + 1 short run.
- **Depends on:** nothing.

**D. Instant replay flag (UseInstantReplay)**
- **TF.EX:** on only if every lobby player has `UseInstantReplay` (`TF.EX.Patchs/Session.cs:120-132`). That would make the browser
  resimulate rounds the Switch doesn't.
- **Switch:** sends `UseInstantReplay = false` (`tfex/MainMenuPatch.cpp:492,535`). Done: not a gap. Listed only because it is the one
  replay feature that is desync-relevant.

### 1.2 Session control (match start/abort, delay, pause)

**E. Failed initial connection → AbortToVersusOptions**
- **TF.EX:**
  - NM:231 `Connect` (init failure) and NM:263 `AbortIfSynchronizationFailed` (not synchronized in 30 s, `Constants.cs:15`, or
    disconnected) → NM:283 `AbortToVersusOptions`.
  - TFG:220-243 consumes it: DisconnectFromServer, ResetPeer, back to the netplay entry menu, EnableAllControllers, a 10 s notification
    "Connection to the other players failed".
- **Switch:**
  - The message side is ported (`tfex/NetplayManager.cpp:375`, `HasFailedInitialConnection`/`GetFailedConnectionMessage` service rules).
  - Nothing calls it. `xplay.cpp` NetplayGate (`:3017`) handles a failed sync its own way, so the player sees the Switch's own
    behaviour, not TF.EX's menu + toast.
- **Browser dependence:** **lobby** (mild).
  - Both sides time out independently. The browser leaves via the server disconnect, which the server turns into a hostless
    LobbyUpdate (host) or a 33 s vote clock (BR `matchmaker.js:203-233`).
  - If the Switch waits longer or returns to a different screen, the two drift apart in menus, but nothing hangs forever.
- **How to build:** hand code in xplay.cpp (coordinator's file):
  1. a timer from StartLobbyMatch;
  2. on 30 s unsynchronized or disconnected before sync: EndLobbyMatch, DisconnectFromServer as TF.EX does, `NetplayManagerAbortToVersusOptions(info)`;
  3. the menu change via the existing NetplaySelect path.

  The rest (the toast) already translates. Design in docs/ws-tfex-lockstep.md 7.4.
- **Size:** ½ day + 1 run (block the page's connection, e.g. a wrong room).
- **Depends on:** nothing. It also unlocks TFG's consumer code if TFGamePatch ever translates.

**F. Session input delay: SetSessionInputDelay / ClearSessionInputDelay, InputDelayAdvisor, the series host delay**
- **TF.EX:**
  - NM:886-894 (`GetEffectiveInputDelay`, Set/Clear); NM:155 `GGRSConfig.InputDelay = lobby.SeriesInputDelay ?? GetEffectiveInputDelay()`.
  - `TF.EX.Patchs/InputDelayAdvisor.cs`:
    - `Update` :78 (called from MM:416), `UpdateProposal` :165 (ceil((ping/2 − accepted rollback ms)/frame ms), clamped);
    - modes Enabled / Manual (MenuAlt2 + left/right slider) / Disabled;
    - `Apply` :369; `PublishHostDelay` :157 → MMS:1816 `PublishInputDelay`.
- **Switch:**
  - The session delay is fixed from hw.txt `npdelay` at session creation. There is no write path (`overrides.toml:376`).
  - PublishInputDelay is mapped (services group 5). The advisor is 30/35 methods translatable, but unused: no unit, nothing to apply to.
  - `SeriesInputDelay` is not read.
- **Browser dependence:** **hidden** (GGRS delay is per peer; never a desync).
  - **Browser bug found here:** the browser's advisor and published `InputDelay` are in 240 Hz ticks while its lobby runs at 240 and
    its Switch session at 60. An advisor-applied delay becomes 4× too long, and a browser series host would publish a 240 Hz value
    (BR `TfexFps.cs` rescales only `ToFrames`).
  - So before the Switch honours `SeriesInputDelay`, units must be agreed: browser-side fix, or the Switch divides by 4 when the
    publisher's platform isn't Switch.
- **How to build:** three parts.
  1. A write path: an xplay.cpp setter read at session creation (coordinator), then services Set/Clear map to it (tfex-services).
  2. An advisor unit (tfex-translator: InputDelayAdvisor; the 5 missing methods are the mouse skip and LINQ). Its Manual mode needs
     MenuAlt2 + left/right on the Switch's menu input (S-input replace: the hook points are XGamePadInput.cs:478/486 in TF.EX; on the
     Switch, the roll call's input hooks in `tfex/MainMenuPatch.cpp`).
  3. `SeriesInputDelay` in the hand `NetplayManager.cpp` session config (with series, item K).
- **Size:** write path ½ day; advisor unit + input 1 day; per-pair ping stats exist (RollcallPings).
- **Depends on:** the 240/60 unit decision (browser repo) before honouring a browser's published value.

**G. LightPauseMenu (in-match RESUME / QUIT)**
- **TF.EX:**
  - `TF.EX.Domain/CustomComponent/LightPauseMenu.cs`: the game keeps running while it is open.
  - Neutral input goes to GGRS while it is open (TFG:614).
  - QUIT exports the replay, ResetPolledInput, leaves (:159-190).
- **Switch:**
  - `xplay.cpp:1236` HandlePausingHook suppresses pausing in netplay entirely. There is no in-match quit; leaving means waiting for the
    end, or the remote leaving.
  - Translator: 10/12 (`Pressed`/`PadPressed`/`PadButtonPressed` skipped as S-input).
- **Browser dependence:** none for the browser (a Switch quit looks like a disconnect, already handled: "a mid-match leave ends the
  match at once"). It is a hidden feature for the Switch player.
- **How to build:** translator unit (LightPauseMenu) + an S-input replacement for its three input methods (the game's
  MenuInput) + a call site in xplay.cpp's netplay Start handling (open the menu instead of pausing; neutral input while open,
  like the VersusMatchResultsInputGate path at `xplay.cpp:2426`).
- **Size:** 1 day.
- **Depends on:** C (QUIT's reset: n/a) and the input lock (H), since TF.EX locks controllers on QUIT.

### 1.3 Lobby and menu flow

**H. Input lock: DisableAllControllers / EnableAllControllers / DisableAllControllerExceptLocal / IsInputLocked**
- **TF.EX:**
  - IS:258, 271, 284, 294; lock state `TF.EX.Domain/Context/GameContext.cs:127-135`, self-releasing after 5 s.
  - About 30 call sites lock while a server round trip runs and unlock in its callback: MM create/join/refresh/join-code,
    `RollCallElement.cs:102-109,273-280,502-509` around UpdatePlayer, `PauseMenu.cs:101,139,238` (votes, quit), TFG:235 (abort).
  - `IsInputLocked` is read only by `IsForeignSeat` (`XGamePadInput.cs:112`, `KeyboardInput.cs:135`): menu buttons return false
    while locked.
  - `DisableAllControllerExceptLocal` keeps only the local device in the local seat.
- **Switch:**
  - todo no-ops (`overrides.toml:411-424`). The hand screens use their own guards (`g_joining`, the 3 s join cooldown, `g_voted`).
  - The local-seat binding is NpPadInput's "first local controller" fallback (`xplay.cpp:2414`) plus the roll call hooks.
- **Browser dependence:** none (local).
  - Its value is the double-submit protection while a request is in flight.
  - TF.EX's 5 s timeout means a lost LobbyUpdate can't freeze the menus. Our server answers every UpdatePlayer with a LobbyUpdate
    (BR `matchmaker.js:692-708`).
- **How to build:** a new service + a hand S-input piece.
  - Service: `svc` lock state with the 5 s timeout (tfex-services: `tfexgen_services.cpp`).
  - S-input: one check where the Switch's menu input reaches the netplay screens. It is the one place that replaces TF.EX's
    `IsForeignSeat` (`tfex/MainMenuPatch.cpp` NotJoinedUpdateHook/JoinedUpdateHook, `tfex/MapScenePatch.cpp` menu input, `xplay.cpp`
    MenuConfirmHook). It returns "no press" while locked.
  - `DisableAllControllerExceptLocal` = "except the local seats" (`svc::LocalSeatMask`, couch plan: docs/research-couch-online.md
    section 5); it must not become a scalar.
  - Generated callers then lock and unlock for free.
- **Size:** service ½ day; S-input gate ½ day (touches adopt's hand files).
- **Depends on:** nothing. G, M and the generated menus use it.

**I. Roll call reconcile: RequestRollcallReconcile / ReconcileRollcallIfPending (MMS:2017, 1290, ReconcileRollcall :1337)**
- **TF.EX:** from each LobbyUpdate, on the game thread, while in Rollcall:
  - remote ready seats: `ResolveArchers` → `TFGame.Characters`/`AltSelect`, ArcherService, remote controllers, element state 1;
  - unready → state 0; vacant seats cleared; "joined"/"left" toasts;
  - modified-files/platform warnings once per peer;
  - plus UnreadyOwnRollcall :1254, NormaliseSeats :891, ApplyTeamsToMatchSettings :1313, ApplyGameSettingsToMatch :1087;
  - `RequestRollcallReconcile` re-runs it after a lobby edit (MM:531, 1052).
- **Switch:**
  - A hand equivalent exists: `tfex/MainMenuPatch.cpp:451-500` (ResolveArcher, AltSelect, remote seats) and `tfex/NetplayManager.cpp:23-50`
    (SessionPlayers, NormaliseSeats, ResolveArchers at start).
  - The services map both calls to todo no-ops (`overrides.toml:336-337`), so generated callers get nothing.
  - Differences not yet audited: ResolveArcher takes "the lowest free vanilla"; TF.EX takes `PickUnusedVanilla`
    (`TF.EX.Domain/Extensions/ArcherData.cs:102`). The toasts and warnings are missing.
- **Browser dependence:** **desync if wrong**.
  - Archers, alts and teams feed the match. Today the start-time `RestoreArchersFromLobbyIfNeeded` equivalent (NetplayManager.cpp)
    re-derives them, so the roll call display is the only thing at risk.
  - The custom-archer fallback matters only if a browser player picks a FortRise custom archer, which the Switch never has: the
    `PickUnusedVanilla` order must then match exactly.
- **How to build:** translator + services.
  1. Make MatchmakingService's reconcile a generated unit (tfex-translator: MMS isn't in the coverage set today, only Patchs +
     CustomComponent; add the reconcile methods as a unit).
  2. Map Request/IfPending to it (tfex-services).
  3. Retire `tfex/MainMenuPatch.cpp:451-500` (tfex-adopt).

  First, a 1-hour check: diff `ResolveArcher` against `PickUnusedVanilla` (cheap, desync-relevant).
- **Size:** check 1 h; unit + adoption 1-2 days.
- **Depends on:** H (the generated code calls the input lock).

**J. ShowMatchStartWarnings (MMS:1910, DesyncRisks :1935; called at `TF.EX.Patchs/Scene/Level.cs:215`, round 0, once synchronized)**
- **TF.EX:** for each other player with `HaveModifiedGameFiles` or a different `Platform`: "X IS PLAYING ON <PLATFORM> - NETPLAY MAY DESYNC".
- **Switch:** a todo no-op (`overrides.toml:350`). LevelPatch's generated code would call it. Notification is ported, and rollback-safe
  between ticks.
- **Browser dependence:** hidden. The browser already shows "Switch" warnings about us.

  Note: once enabled, the Switch would warn on *every* browser opponent ("WINDOWS"/"Linux" ≠ "Switch"). That is TF.EX's behaviour
  but noisy for this pairing. Decide: show it as TF.EX does, or ledger it (S-platform: Switch ↔ browser is the supported pairing).
- **How to build:** translator only (MMS method as a unit) + a services map + the generated LevelPatch call site
  (`ClearWaitingNotificationOnceSynchronized`).
- **Size:** ½ day.
- **Depends on:** the ledger decision above.

**K. Series (Best-of-X): IsSeriesPicker (MMS:851), PickSeriesMap (MMS:2042), SeriesContinueChoice (MMS:2037), SeriesLobby (MMS:807)**
- **TF.EX:**
  - `TF.EX.Domain/CustomComponent/SeriesScreen.cs` (picker UI, tally), `LobbyVersusSeriesButton` (`EnforceSeriesRules` :70: 1v1/2v2,
    MapId −1, LMS/TDM, TournamentRules), `SpectatorSeriesTally`.
  - Series end-of-match menu: REMATCH → CONTINUE, no ARCHER SELECT (`PauseMenu.cs:184-193`).
  - `Lobby.SeriesInputDelay` (F).
  - Server-driven (BR `matchmaker.js:410-500`): loser side picks, random tower after 62 s, votes resolve after 33 s, abort on leave.
- **Switch:**
  - Refuses series lobbies (`tfex/MainMenuPatch.cpp:281`); keeps `Series` as raw JSON (`tfex/Models.hpp:71-72`); ignores
    `SeriesLobby` (`tfex/MatchmakingService.cpp:215`).
  - The LobbyBuilder has no series button (`tfex/NetplayScreens.cpp:283`).
  - Translator: SeriesScreen 27/36 (blocked: FirstAvailableTower, UpdateTowerPick, DrawFinished, DrawTowers, ScoreLine, ScoreOf);
    LobbyVersusSeriesButton 9/9; SpectatorSeriesTally 4/4; Auto_Series model generated.
- **Browser dependence:**
  - Today: hidden (the Switch can't join or create series lobbies).
  - Once joinable: **lobby**. A Switch that joins but doesn't speak series would sit out of step until the server's timers move it on;
    the match itself is driven by MapId/Seed and stays in sync.
- **How to build:** mostly translator + services + one hand message.
  1. Finish SeriesScreen (tfex-translator: 9 methods; mostly LINQ/collection constructs already on its list).
  2. Services: IsSeriesPicker from the lobby, PickSeriesMap/SeriesContinueChoice as two messages in the hand MatchmakingService
     (tfex-services maps, tfex-adopt adds the senders and the SeriesLobby handler → ReturnToMenu(SeriesLobby)).
  3. Menu state 61 (SeriesLobby) in NetplayScreens.
  4. The series button in the LobbyBuilder (comes with adopting the five LobbyVersus*Buttons, ws-tfex-adopt.md item 8).
  5. The PauseMenu CONTINUE path (`tfex/MainMenuPatch.cpp:732-750`).
  6. Lift the WhyNotJoinable refusal last.
- **Size:** 3-4 days across three owners.
- **Depends on:** F (SeriesInputDelay and its unit question), the LobbyBuilder adoption, H.

**L. Spectators: SetSpectatorMode / IsSpectatorMode (NM:1058, 1080), AddSpectators (NM:1129), AddLateSpectator (NM:1085), catch-up, notices, SpectatorInputDisplay, `netplay_frames_behind`**
- **TF.EX:**
  - The host lists lobby spectators in `GGRSConfig.Netplay.Spectators` before Init (`RollCallElement.cs:546`, `SeriesScreen.cs:369`),
    and late ones from `SpectatorJoined` → `netplay_add_spectator` (NM:1119).
  - A spectator runs a GGRS spectator session against the host.
  - Catch-up: more than 120 frames behind (Constants.cs:13) → the fixed timestep divided by 8 or 1.25 (TFG:297-300).
  - "CATCHING UP : n" (GameplayLayer.cs:53); notices (MMS:1948/1953); `SpectatorInputDisplay.cs` (input overlay, C toggles catch-up);
    `SpectatorRollcallGate`; `KickSpectatorIfMissingVariants` (MMS:1216); a relayed spectator cap of 2 (0.20).
- **Switch:**
  - The services are all todo (`overrides.toml:349,359,362-365,382`); `SpectatorJoined` is ignored.
  - ggrs-switch has no spectator support (no `add_spectator`/`frames_behind`/spectator session in `ggrs-switch/src`).
  - The lobby screen can't join as spectator (`tfex/NetplayScreens.cpp:365` "player only (v1)").
  - Translator: SpectatorRollcallGate 2/2, SpectatorSeriesTally 4/4.
- **Browser dependence:**
  - Players never depend on spectators: **no desync**.
  - Lobby: a browser spectator of any Switch lobby is never served. The browser host drops it (SwitchLobby.cs); a Switch host ignores
    it. It times out after 30 s ("Connection failed"). The players' match is unaffected.
  - The Switch must keep tolerating non-empty `Spectators` and `SpectatorJoined` (it does).
- **How to build:** large, new transport + a hand design.
  1. ggrs-switch: host side `add_player(PlayerType::Spectator)` at session build + late add; spectator side `SpectatorSession`;
     `frames_behind` (Rust; the `ggrs` crate supports both).
  2. xplay.cpp: a spectator mode (no local input, no rollback saves needed: a spectator only advances confirmed frames), catch-up
     by running extra ticks per frame (the Switch can't shrink the timestep: S-60hz; catch-up = N updates per vsync, CPU-bound).
  3. The browser: remove SwitchLobby's spectator block once the Switch serves them, at 60 Hz (BR).
  4. Then the services (tfex-services) and the UI pieces by translator: notices, the CATCHING UP text in the generated GameplayLayerPatch,
     SpectatorInputDisplay (it needs the InputDisplayer API: S-native, likely an own small overlay or skipped).
- **Size:** 1-2 weeks.
- **Depends on:** the Rust lib publish step (memory: publish-rustlib-step), the browser repo, and per-checkout harness support for a
  third peer.

**M. Match end: MatchEndChoices (30 s deadline, `CHOICE_SECONDS` :10) and ResultsVoteLock**
- **TF.EX:**
  - `TF.EX.Domain/CustomComponent/MatchEndChoices.cs` (vote tally + countdown; since 0.19.1 no choice → archer select);
  - `TF.EX.Patchs/PlayerInput/ResultsVoteLock.cs` (menu input locked after voting);
  - PauseMenuPatch (`IsNetplayEndgame`, the biggest single blocker in the coverage report: 5 callers).
- **Switch:** partial.
  - The vanilla VersusMatchEnd menu with REMATCH!/ARCHER SELECT/QUIT hooked to votes (`tfex/MainMenuPatch.cpp:732-750`).
  - No tally or countdown entity. The server's 33 s timer covers a non-voter.
  - Translator: MatchEndChoices 6/6, ResultsVoteLock 1/1, PauseMenuPatch 4/10.
- **Browser dependence:** none (the votes go through the server; timers resolve).
- **How to build:** translator + adoption.
  - MatchEndChoices as a unit (ready).
  - PauseMenuPatch's remaining 6 (tfex-translator: `IsNetplayEndgame`).
  - Then tfex-adopt swaps the hand vote hooks for the generated PauseMenuPatch. ResultsVoteLock needs H's lock.
- **Size:** 1-2 days.
- **Depends on:** H.

**N. Lobby builder: TF.EX's five LobbyVersus*Buttons and the variants page**
- **TF.EX:** `LobbyBorderButton` + Mode/Map/PlayerCount/Coin/Series buttons, the variants grid (MM CreateLobbyBuilder, TFEX-UI-SPEC 2.6).
- **Switch:** vanilla OptionsButtons as stand-ins, no series button, no variants page (`tfex/NetplayScreens.cpp:281-283`). Switch-hosted
  lobbies therefore never have variants.
- **Browser dependence:** hidden (and it limits what Switch hosts can offer).
- **How to build:** adoption: all six units exist and translate fully (ws-tfex-adopt.md item 8). The variants page needs VariantToggle
  (5/5) and VariantButton (5/7) plus the variant gate of A (a Switch host must not offer an unported variant).
- **Size:** 1-2 days.
- **Depends on:** A (which variants may be offered).

### 1.4 Cosmetic / account features

**O. Custom skins: PublishCustomSkin (MMS:462), SkinChunk receive (MMS:537-555), SkinOverlayService.ResolveArcherSkinned (:53), ArcherData.Get postfix**
- **TF.EX:** streams a FortRise custom archer's sprites (192 KB chunks, max 32) to lobby members; others overlay them visually. Gameplay
  uses a vanilla fallback unless everyone has the archer (`ResolveArchers`).
- **Switch:**
  - None. `SkinChunk` frames are received into the 512 KB buffer and dropped (`netws.hpp:12`, `tfex/MatchmakingService.cpp:215`).
  - PublishCustomSkin is a no-op (`overrides.toml:348`); the Switch has no custom archers (S-native), so it would never publish anyway.
- **Browser dependence:** hidden (cosmetic). The gameplay side is item I's fallback order.
- **How to build:**
  - Publishing: not applicable (no FortRise archers).
  - Receiving/overlaying: a new hand design (decode PNG chunks into a Texture2D and clone ArcherData). Only worth it if browser players
    actually use custom archers; the browser runs no archer mods by default.
- **Size:** 2-3 days for receive + overlay.
- **Depends on:** nothing. Lowest value.

**P. In-match ping overlay extras, ChangelogPanel, notifications not wired**
- GameplayLayerPatch's ping line is adopted (default on).
- ChangelogPanel (10/15) + menu state 66: hidden, cosmetic.
- "A PLAYER LEFT" on a team-match leave (docs/ws-tfex-lockstep.md 7.11): small, adopt.

### 1.5 Replays and reports

**Q. Replay recording/export/playback, instant replay, kill-cam (ReplayService, TF.Replay, InstantReplayFootage, ReplayRecorder)**
- **Switch:** all `D` in the ledger (`overrides.toml` [skip] `ReplayService`, `InstantReplayFootage`, `ReplayRecorder`, `Music`, `TfExApi`);
  `Level.ReplayRecorder` is nulled.
- **Browser dependence:** none, given `UseInstantReplay = false` (D).
- **Recommendation:** keep as D. Recording would need TF.State's per-frame capture, which S-rollback doesn't have; an input-log
  replay is possible later (GGRS confirmed inputs + seed), but that isn't TF.EX's format.

**R. MatchReport (desync / disconnect / crash uploads, `TF.EX.Domain/MatchReport.cs`, v0.20.0)**
- **TF.EX:** when the server's `Lobby.IsReportEnabled` is set, it zips the replay, log and exception and POSTs to `/report`.
- **Switch:** todo no-ops (`overrides.toml:440`). The Switch has its own telemetry POST (`kind:"switch"`, BR `reports.js:6-9`) and the
  digest-based desync event.
- **Browser dependence:** none.
- **Recommendation:** ledger it (S-tools: the Switch's own telemetry is the report path), unless the user wants TF.EX's report format
  on our server.

### 1.6 Server side (tfex-server, browser repo)

**S. v0.20/0.21 protocol on the server: MatchPlayers, IsCheckingConnections + PeerMeasured, RelayedPairs, KickedFromLobby, IsReportEnabled**
- **Switch:** the client side is ported (`tfex/MatchmakingService.cpp:193-196`, Models, `HandleKickedFromLobby` :581). It tolerates
  their absence.
- **Server:** none of them (`docs/ws-tfex-lockstep/tfex-server-v0.21.patch`, not applied). TF.EX's per-pair relay needs UDP, which a
  Worker can't take (docs/ws-tfex-lockstep.md 7.5).
- **Browser dependence:** none today (the browser is 0.19.2).
  - MatchPlayers becomes **desync-relevant** once spectators or late joiners exist: seats must not shift mid-match.
  - So it must land before or with L.
- **How to build:** apply and test the patch in the browser repo (user approval for any deploy). The relay stays S-transport.
- **Size:** 1 day incl. `test.mjs` tests.
- **Depends on:** a user decision (deploys).

### 1.7 Not applicable on the Switch (ledger, no work)

| TF.EX feature | where | key |
|---|---|---|
| Auto-updater, version check loader | `TF.EX.Common/AutoUpdater.cs`, TFG:428-598 | S-update |
| Dark World DLC gate (v0.21.0 #274) | TFG:466-472 | always true on 1.4.1.6 |
| Game-files integrity, desync-risk mod warnings | `Utils/GameFilesIntegrity.cs`, TFG:521-565 | S-native |
| UPnP | `Services/UpnpService.cs` | S-transport |
| Keyboard/mouse/PC gamepad input, clipboard | KeyboardInput, XGamePadInput, JoinCodeEntry | S-input / S-keyboard |
| Right Stick Shot variant | `TF.EX.Patchs/PlayerInput/RightStickShot.cs` | S-input; it is a TF.EX custom variant, so a lobby with it is refused by HasAll (unknown title). Confirm. |
| FortRise custom variants/archers, mod interop | Extensions/ArcherData.cs, MatchVariants.cs, Interop/* | S-native |
| Test tooling (scenarios, SyncTest, frame-by-frame, hurtboxes) | TFG, ScenarioSweeper | S-tools |

## 2. Summary table

| # | feature | browser dependence today | build | size | owner(s) |
|---|---|---|---|---|---|
| A | Bottomless / Ghosts / Dark Portals (+ join stopgap) | **desync** | hand (state ports) | 3 days (stopgap: hours) | lockstep (rnd.cpp + new files); stopgap: adopt |
| B | F2 ArrowShuffle | **desync** | hand, ~20 lines | 2 h | lockstep (rnd.cpp) |
| I | reconcile: ResolveArcher vs PickUnusedVanilla check | **desync if wrong** | check, then translator + services | 1 h; 1-2 days | adopt (check); translator, services |
| E | failed initial connection → AbortToVersusOptions | lobby (mild) | hand xplay.cpp | ½ day | coordinator |
| H | input lock | none (robustness) | service + S-input gate | 1 day | services; adopt (gate) |
| F | session input delay + advisor + series delay | hidden (browser unit bug) | xplay setter, services, translator unit | 2 days | coordinator, services, translator |
| G | LightPauseMenu (in-match quit) | hidden | translator + S-input + xplay call site | 1 day | translator, coordinator |
| M | MatchEndChoices / PauseMenuPatch / ResultsVoteLock | hidden | translator + adoption | 1-2 days | translator, adopt |
| N | LobbyBuilder buttons + variants page | hidden | adoption | 1-2 days | adopt |
| J | ShowMatchStartWarnings | hidden | translator + services | ½ day | translator, services |
| K | series | hidden → lobby once joinable | translator + services + hand messages | 3-4 days | translator, services, adopt |
| S | tfex-server v0.21 patch (MatchPlayers etc.) | none today; needed before L | server patch | 1 day | browser repo (user approval) |
| L | spectators | lobby (spectators time out) | Rust + xplay + services + browser | 1-2 weeks | coordinator + new Rust session, browser |
| C | ResetPolledInput | none | ledger (n/a by design) + 1 check | ½ h | services |
| O | custom skins (receive/overlay) | hidden | hand design | 2-3 days | later |
| P | ChangelogPanel, A PLAYER LEFT toast | hidden | translator / adopt | ½ day each | adopt |
| Q, R | replays, MatchReport | none | ledger D / S-tools | – | services (ledger lines) |

## 3. Ranked plan

The order is desync first, then lobby correctness, then features the browser exposes today, then the big transport work. Each
workstream touches its own files, so they can run in parallel. Current ownership is respected:
- tfex-translator: `tools/tfex/translator`, `tfex/gen`;
- tfex-services: `overrides.toml [service]`, `tfexgen_services.*`;
- tfex-adopt: hand `tfex/*.cpp`;
- coordinator: `xplay.cpp`, `rnd.cpp` (unless delegated), merges, publishing.

### Wave 1: correctness (start now, in parallel)

1. **W-lockstep (new session or the old ws/tfex-lockstep; files: `rnd.cpp`, new `state ports` files, nothing else).**
   - B (F2 ArrowShuffle, 2 h + 1 run) first.
   - Then A in the documented order: chests, ghosts, Dark Portals + Bat/Slime. One commit and one short lockstep run each (recipe in
     docs/ws-tfex-lockstep.md "Next steps").
   - The rnd.cpp edits need the coordinator's OK (shared file today).
2. **tfex-adopt (hand `tfex/*.cpp`).**
   - (a) The A stopgap: unported variant titles refused in WhyNotJoinable (`MainMenuPatch.cpp:285`), removed title by title as
     W-lockstep lands each port.
   - (b) The I check: ResolveArcher vs TF.EX's `PickUnusedVanilla` (1 h; fix if they differ).
   - (c) N: the LobbyBuilder's five buttons (units exist), without the variants page until A is done.
   - (d) From section W: the unready follow-up (TF.EX's UnreadyOwnRollcall in JoinedUpdateHook, about 10 lines), lobby closed →
     NetplaySelect, and the `&&`/`||` precedence fix at `MainMenuPatch.cpp:659`.
3. **Coordinator (`xplay.cpp`).** E (AbortToVersusOptions from NetplayGate: timer, teardown, toast), then the F write path (a session
   delay setter read at session creation, exposed for the services).
4. **tfex-services (`overrides.toml [service]`, `tfexgen_services.*`).**
   - H's service (lock state + 5 s timeout + the local-seat mask version of ExceptLocal).
   - C as a documented no-op (ledger S-input).
   - Q/R ledger lines.
   - Once 3 lands: Set/ClearSessionInputDelay.
   - H's S-input gate in the hand menu hooks is a short hand-off to tfex-adopt (their files).

5a. **The v0.21.1 bump, both sides (first: it changes the parity reference):**
   - tfex-translator: `tools/tfex/update.sh v0.21.0 v0.21.1`, the vendor bump, gen/ regenerated, coverage.
   - tfex-adopt: `/ws?version=0.21.1`, the `UpdateRequired` and `Notice` handlers, the menu details of V.6.
   - A browser session: section B (user approval for the deploy).

   After this, every desync against the browser is like for like: no 0.19.2 TF.State restore bugs (V.1) and no NoRandom
   difference.

### Wave 2: TF.EX's lobby and match-end flow (after wave 1's pieces it depends on)

5. **tfex-translator:**
   - PauseMenuPatch's `IsNetplayEndgame` (unblocks 5 callers → M);
   - LightPauseMenu unit (G) and InputDelayAdvisor unit (F, with the S-input replace for MenuAlt2/left/right);
   - the MMS reconcile and ShowMatchStartWarnings as units (I, J);
   - SeriesScreen's 9 missing methods (K).
6. **tfex-adopt:**
   - M (MatchEndChoices + generated PauseMenuPatch replace the vote hooks; needs H);
   - I's adoption (retire `MainMenuPatch.cpp:451-500`);
   - the variants page (N, after A);
   - the "A PLAYER LEFT" toast (P).
7. **Coordinator:** G's call site (Start in netplay opens LightPauseMenu, neutral input while open).
8. **Decisions needed from the user:**
   - J: warn on every browser opponent's platform, or ledger it?
   - F: the 240/60 Hz unit of the browser's published InputDelay. Section B proposes fixing it in the browser during the bump.
   - S: apply the server patch?

### Wave 3: series, then spectators

9. **K series** across translator (SeriesScreen) → services (IsSeriesPicker, the two messages) → adopt (SeriesLobby handler, state 61,
   CONTINUE, lift the refusal). It needs F (SeriesInputDelay) and N (the series button).
10. **S** server patch (browser repo, user-approved deploy). It goes before L because MatchPlayers protects the seats.
11. **L spectators.**
    - Its own session for ggrs-switch (spectator support in the Rust lib, publish-rustlib-step).
    - The coordinator for the xplay.cpp spectator mode and catch-up.
    - The browser repo to lift SwitchLobby's spectator block.
    - Then services + generated UI.

    Largest item. Nothing breaks without it.

### Later / optional
- O custom-skin receive/overlay (only if browser players use custom archers).
- P ChangelogPanel.
- An input-log replay (not TF.EX's format; ledger D stays).

### Parallel-safety notes
- W-lockstep and tfex-adopt both touch variant handling, but in different files: `rnd.cpp`/new ports vs `MainMenuPatch.cpp`.
  The stopgap list is the only shared knowledge; W-lockstep tells adopt each time a title becomes safe.
- H spans services (the state) and adopt (the gate): two commits by two owners, services first.
- F spans the coordinator (setter) → services (mapping) → translator (advisor unit), sequential, with no shared files.
- Never two writers on `xplay.cpp`: E, F's setter, G's call site and L's spectator mode are all the coordinator's, in that order.
