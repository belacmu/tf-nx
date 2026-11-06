# ws/tfex-adopt: generated TF.EX units replace the hand ports

## Priority order (the user, 2026-10-17; read this first)
1. **The core multiplayer experience, exactly TF.EX's flow and code**: NETPLAY menu -> lobby browser / quick play / private -> lobby
   and roll call (ready, edits, joined/left, warnings) -> match start -> in-match (rollback, pings, disconnect handling) -> match end,
   rematch, archer select, quit -> back to the menus. My queue, in order:
   - harness cleanup (one batch: dad2ab7 merged, vs-log writes, hw.txt defaults, tools/smoke.sh);
   - the lobby fixes (UnreadyOwnRollcall, closed lobby -> NetplaySelect, failed leave -> LobbyBrowser: done in batch 1), the lobby
     InputDelay in TF.EX's 240 Hz units, the v0.21.1 hand-written to-dos (Notice / UpdateRequired, ws?version=, NetplaySelect via
     RunOnGameThread, RemoveStaleMenuItems, CreateMain resets);
   - the roll-call reconcile with joined/left toasts (batch 1) and seat changes;
   - TF.EX's warnings exactly as TF.EX shows them (ShowMatchStartWarnings, the cross-platform warning; generated where possible, no
     custom rule);
   - the NETPLAY menu buttons (NetplayMenuButton, NetplayButton, CompactCoOpButton) once the vb_ fix lands;
   - match end / rematch / pause-menu flows from generated code (HUDPatch, MatchEndChoices, PauseMenuPatch), the input lock, the
     failed-connection path.
2. **Handling TF.EX updates nimbly** (tools/tfex update report; v0.21.1 is the target).
3. **Variants last**: only the safety gate stays (refuse a lobby with variants the Switch doesn't port: batch 1). The variant-gated
   TF.State patches are out of the queue. Spectators, series and skins come after the core works.

Branch `claude/tfex-adopt-6d66fe` (from master 254929e). Owner: the adoption session. Goal: where a generated unit
(tools/tfex/translator -> tfex/gen) is complete and equivalent, call it instead of the hand port in tfex/*.cpp, then make its
generated Harmony hooks (`genpatch <Class>`) the default once proven. One unit per commit, one hardware-checklist line per unit.


## Role from 2026-10-17 (the user's reorganisation): integration
Everything between the generated code and the game: the hand-written tfex/*.cpp, xplay.cpp's small hooks, the test glue, and (once
tfex-services stops after its vb_ fix) the service rules: tools/tfex/overrides.toml [service] and translator/runtime/tfexgen_services.*.
Target: TF.EX v0.21.1 (tfex-translator moves it). Technical details go straight to tfex-translator; merge-ready batches to the
coordinator, fewer and bigger.

### Plan (order)
1. Parked until fixes land: NetplayMenuButton / NetplayButton / CompactCoOpButton (vb_MainModeButton; patch in the side-session notes).
2. Hand hooks that mirror a TF.EX patch class with a translatable body: switch to the generated hooks (genpatch default) where the unit
   is complete, as units 1-7. Candidates by hand file: MainMenuPatch.cpp (RollCallElementPatch Render/JoinedUpdate/NotJoinedUpdate,
   XGamePadInputPatch's host start, MatchEndChoices), PlayerPatch.cpp (the FFA leaver), MapScenePatch.cpp, NetplayManager.cpp's
   VariantsExtensions (ApplyNetplayVariantRules), Session/MatchSettings/VersusVariantButton/ResultsVoteLock patches. Ranked once
   tfex-translator's current per-file coverage (v0.21.1) is in.
3. Service rules, "Still todo" of docs/ws-tfex-services.md, in the order the feature-gap audit ranks them. Cheapest first: the input
   lock (IInputService Disable/EnableAllControllers, DisableAllControllerExceptLocal = "except the local seats", IsInputLocked),
   ResetPolledInput, the roll call reconcile, ShowMatchStartWarnings; then the features that need a design: spectators, series
   (IsSeriesPicker, PickSeriesMap, SeriesContinueChoice; the LobbyBuilder series button comes back with it), skins, session input delay.
4. What stays hand-written (S-native: the Switch's own I/O): MatchmakingService (websocket client), the GGRS driver in xplay.cpp,
   Json/Models until the generated models replace them (protocol-wide, last).

### Batch 1 (2026-10-17): lobby parity from docs/research-tfex-feature-gaps.md (W.2, I)
| item | change | tested (Ryujinx, notif 1, digest both sides, vsstop 600) |
|---|---|---|
| UnreadyOwnRollcall | JoinedUpdate (own seat): our Ready true -> false while joined (the host edited) -> the portrait leaves, state 0 | page edits its lobby after the Switch readied (test/page-edit.sh): logged, lobbyui readied again, match 0/599 |
| lobby closed | ConsumeLobbyClosed (host left / kicked) -> NetplaySelect, was Main | not run (needs the page host to leave) |
| failed leave | already LobbyBrowser on both outcomes (v0.21.1's change) | no change |
| unported variants | FirstUnported (BOTTOMLESS TREASURE, RETURN AS GHOSTS, DARK PORTALS) -> "MISSING VARIANTS" at join; one warning toast in a lobby that turns one on | not run: the page's builder keys landed on QUICK PLAY twice (harness flaw; test/page-variant.sh is the recipe) |
| ResolveArchers | = TF.EX v0.21.1: invalid alt -> PickUnusedVanilla (other archer); roll call shows the same resolution | every match above (vanilla archers: no change) |
| precedence | PublishGuest condition | build |
| ReconcileRollcall notices | "<name> joined", "Player left", modified files, platform; once per lobby update on the roll call | "PLAYER joined", "PLAYER IS PLAYING ON UNKNOWN - NETPLAY MAY DESYNC", 0/599 |
Notes: the browser's TF.EX reports Platform "Unknown", so every Switch-vs-browser lobby shows that warning (TF.EX's behaviour; the
browser could publish a real platform). Sounds.ui_invalid / ui_clickSpecial aren't bound: the notices are silent (asked tfex-translator).
Seat changes (HandleControlChange / EnsureLocalControllerSeat) are still not handled: needs the input service (H).

### Batch 2 (2026-10-17): harness cleanup, then core lobby (InputDelay, v0.21.1 hand to-dos, TF.EX's warnings)
Harness (612610b, d672f73): tools/smoke.sh (AGENTS.md), the tick-log writer thread (StartThread had 4 slots), hw.txt lines merged over
lobby-vs-browser's defaults, dad2ab7 merged and fixed (waits for '[mode] online: entering netplay'; lobby-auto's leading c).
| item | change |
|---|---|
| lobby InputDelay | published in TF.EX's unit, GGRS frames at 240 Hz: the session delay (hw.txt npdelay, 60 Hz frames) x 4 (NetplayPreferences::PublishedInputDelay; logged with the (me) line); services GetEffectiveInputDelay x 4 too. Nothing on the Switch reads a peer's value (it would be ceil(v / 4)). |
| ?version= | the matchmaking URL is <server>/ws?version=0.21.1 (TFEX_VERSION) |
| Notice | {"Notice":{"text"}} -> Notification(text, 10, 600) on the game thread |
| UpdateRequired | {"UpdateRequired":{"version"}} -> ResetLobby, ResetPeer, quick play dropped, Main; a parseable version is shown at the next NETPLAY press as TF.EX's "Online play requires version X (current version : 0.21.1)" (TFEX-DIFF: no updater on the Switch), else the "ONLINE PLAY REQUIRES TF.EX X" toast |
| connect failure | the menu goes to NetplaySelect after the request's onFail (v0.21.1 ConnectToServerAndListen) |
| create failure | "Failed to create lobby" -> PrivateSelect / LobbyBrowser (was Main) |
| CreateMain | ClearNetplayMode always; after netplay ResetVersusChoices (Players false, Characters[i] = i). NetplayOptions.Apply/Restore: not ported (the Switch doesn't force TF.EX's netplay options), nothing to restore |
| ShowMatchStartWarnings | at "Synchronized!" (round 0): "<NAME> HAS MODIFIED GAME FILES / IS PLAYING ON <PLATFORM> - NETPLAY MAY DESYNC", as TF.EX (no sound: not bound) |
| RemoveStaleMenuItems | open: needs MenuItem.CreatedState and Layer.GetList (asked tfex-translator for a generated piece) |
Smoke (this batch, final build): lobby PASS 0/592, join PASS 0/593, quickplay PASS 0/599 (input delay 8 published in every path; the first
try published 0 in quick play: PublishLocal now always sets it, as TF.EX's UpdatePlayer). The smoke also caught a crash of my own first
try (an unresolved symbol at the first publish). Join logged the match-start warning
"PLAYER IS PLAYING ON BROWSER - NETPLAY MAY DESYNC".

### Batch 3 (2026-10-17): the NETPLAY buttons (vb_MainModeButton fixed on master)
- NetplayButton (main menu) and NetplayMenuButton (NetplaySelect / PrivateSelect: QUICK PLAY, LOBBIES, PRIVATE, CREATE, JOIN) are TF.EX's
  generated classes while the runtime-made classes are on (notif 1); their onConfirm Actions are native delegates
  (brute::NewNativeDelegate) into the hand actions (NetplayButton_MenuAction = TF.EX RequestNetplayEntry's path; NetplayScreens::MenuAction).
  The ArchivesButton stand-ins stay when notif is off. InstallLate now checks CoOpButton's own BaseTextScale slot against the binding's
  MainModeButton.get_BaseTextScale (it used the NETPLAY stand-in's ArchivesButton vtable).
- CompactCoOpButton (batch 4): TF.EX's generated class at (290, 180) (notif 1). TFEX-DIFF: TF.EX takes the vanilla CoOpButton off the
  to-be-spawned list; the Switch has no such list, so it stays, inert (not active, invisible, unlinked; ToStartSelected moves to the
  compact one). The bound "Scene.Remove(T)" (+0x945510) turned out to be Scene.Remove(GameTags) (disasm: arg 1 indexes Tags); reported to
  tfex-translator (MainMenuStale::RemoveStaleMenuItems calls it with an entity; nothing calls that yet).
  Smoke (slot subclass): lobby PASS 0/599, both generated main-menu buttons made, NETPLAY through the generated one. The main-menu
  screenshot (SMOKE_SHOTAT, new) didn't come out at that moment: the look is unchecked.
- Smoke (one run, private slot subclass): lobby PASS 0/598; both classes made, NETPLAY -> 7 -> 62 -> LOBBIES through the generated buttons.
- Checklist: the main menu's NETPLAY button and the NETPLAY menu's buttons look and animate like TF.EX's (icon drawn with an outline,
  base scale 1); each still opens its screen.

### Batch 5 (2026-10-17): NETPLAY entry through TF.EX's generated RequestNetplayEntry
- NetplayButton_MenuAction: the required-version check (mine), then TF.EX's TFGamePatch.RequestNetplayEntry (generated NetplayEntry:
  Dark World DLC check, version checks on the official server only, mod warnings: none on the Switch), whose enter is a native
  delegate into the hand entry (IsNetplayRequested, ApplyNetplayMode, the STANDARD / WIDE screen). NetplayPreferences.Server (a
  generated static defaulting to TF.EX's official server, which would send every press into "CHECKING VERSION": no updater on the
  Switch) is set from netplay.txt before the call, as TF.EX loads its settings. TFEX-DIFF: pointing the Switch at TF.EX's official
  server would still hang on CHECKING VERSION (IAutoUpdater isn't mapped).
- Smoke (slot subclass): lobby FAIL once (an intermittent guest crash in the runtime at in-match tick 572: PC +0x47320, null read at
  0x10, called from the main loop; reported to the coordinator), then PASS 0/597; NETPLAY went through the generated entry both times.

### Batch 6 (2026-10-17): TF.EX's roll-call reconcile, RemoveStaleMenuItems
- RollcallReconcile (generated, hosted MatchmakingService unit; translate.toml unit enabled, service rules e990cf6): with notif 1, on each
  lobby update in the roll call UnreadyOwnRollcall(lobby, previous), then ReconcileRollcallIfPending: the joined / Player left /
  modified-files / platform notices, a remote seat's ready / unready / vacancy, TFGame.Characters / AltSelect from TF.EX's own
  ResolveArchers. Replaces the hand UnreadyOwnRollcall and RollcallNotices (kept as the notif-off path).
- The per-element hand hooks stay for remote seats (they join the portrait), and ShowRemoteArcher sets the portrait's archer when a seat
  is joined either way: TF.EX's EnterJoined postfix (RollCallElementPatch) isn't generated yet; TFEX-DIFF until it is.
- v0.21.1 RemoveStaleMenuItems (generated MainMenuStale, with tfex-translator's Layer.Remove fix 83afab2) at the top of every
  CallStateFunc "Create".
- Needs tfex/gen regenerated with the RollcallReconcile unit (tfex-translator) before the branch builds from the committed tree.
- Smoke (slot subclass): lobby PASS 0/593, join PASS 0/595.
- Seen: the services' own-lobby self-test logs "write reached the hand lobby NO ... restored NO" on every build since the writable lobby
  landed (also before this batch); the LobbyBuilder edits themselves do arrive. Reported.

### Batch 7 (2026-10-17): RollCallElementPatch on; MatchmakingService swap prep
- RollCallElementPatch (generated) on at boot: gen::hooks::Enable with a skip list. NotJoinedUpdate, JoinedUpdate and Render are
  called from the hand hooks in tfexgen_hooks.cpp's order (prefix, false = skip the original and keep its result; the hand part with
  the original; postfix), since two exlaunch trampolines on one address are unsound (tfex-translator's hook-collisions.py). The local
  second player's seat skips the generated prefix (Switch-only). The constructor is skipped too: its generated hook is
  `void (self, int)` where BRUTE's is (self, index, allocate) -> object (callers pass self = null, w2 = 1 and use x0: 0xaa1444); that
  was the Layer.Render crash. Its prefix/finalizer only scope Auto_SkinSlot (no skins on the Switch). Reported to tfex-translator.
- Smoke (slot subclass): lobby PASS 0/592; the generated EnterJoined / Update hooks ran.
- MatchmakingService (generated) prep: overrides.toml [service] rules for its excluded edge
  (tools/tfex/reports/matchmakingservice-edge-rules.toml) over the hand transport. Send queues for the net thread. Connect, Close and
  EnsureConnection map onto the hand connection. SwitchPingMeasurement uses net.cpp's measurement. GetRelayedPeersFrom uses a ring of
  four uncollectable sets. peerId's set writes the hand service's. LastStateChecksum is null (as the hand MatchEnded). The skin stream
  and late spectators are no-ops (todo). With the unit uncommented and regenerated locally, tfexgen_MatchmakingService.cpp compiles; the
  build still fails in Auto_* units it pulls in (tfex-translator: Array.Empty, Lobby.get_KindValue, EnableForAll, Series' FindIndex,
  SteamIdentity). The switch-over itself (hand callers onto the generated object, the net thread queueing the text for
  HandleMessageContents) waits for that regeneration.
- Not done: the NETPLAY globe icon. MenuIcons.Online() falls back to "versus/introArrow" because nothing calls ConfigureOnline;
  it needs Texture2D / Monocle.Texture / Subtexture constructors bound (asked tfex-translator).

### Batch 8 (2026-10-17): the RollcallElement constructor hook on
- With master e8b61bc (the generated ctor hooks return the object, 3657bc8, and forward x2..x7, 6df102c), RollCallElementPatch's
  constructor prefix/finalizer are no longer skipped (only NotJoinedUpdate / JoinedUpdate / Render stay skipped: the hand hooks call them).
- Smoke (slot subclass): lobby PASS 0/592; the generated ctor hook ran for every element, no crash.

### Batch 9 (2026-10-17): TF.EX's own MatchmakingService (generated) behind the hand one, hw.txt genmm 1
- The generated class (tfex-translator a1c1c4f) owns the lobby state and handles the server's messages, as TF.EX does. The net thread
  queues them (4 x 64 KB, in the pools registry; KeepAlive answered at once, SkinChunk dropped). The game thread runs them in
  MatchmakingService::DrainGameThreadActions, then TF.EX's StartOrStopPingMeasurementIfNeeded.
- The hand class keeps the transport (socket, thread, send queue, ping measurement) and forwards its API to the object. Callers are
  unchanged. Its models are converted: the own lobby only when TF.EX's changed. Hand callbacks become delegates whose target is a
  managed closure (Invoke reads the target as an object: a plain C target crashed).
- The services call it directly for TF.EX's code: GetOwnLobby is TF.EX's ownLobby, the requests go straight to it, and the roll call
  fields are its own (MainMenuPatch calls its ReconcileRollcallIfPending).
- TFEX-DIFF: RematchLobby and ArcherSelectLobby stay on the hand path (EndOfMatch's Consume*). TF.EX's own handlers change the scene
  without NetplayManager.Reset, which isn't mapped on the Switch (Integration B). No SeriesLobby. No ping stats yet: the
  ping_measurement_stats rule waits for `out var` in [service] calls (tfex-translator).
- Found on the way: gen::ManagedUtf8 truncates at 1023 characters, so every message over 1 KB reached TF.EX's parser cut off, and the
  update where a second player appeared crashed (lobby null). Messages are converted whole here now. Also NewModel / SetModelColl
  stack temporaries (36-600 KB) and NewVal's ring used for long-lived fields. All reported to tfex-translator.
- Needs tfex/gen regenerated (runtime/tfexgen_services.* changed) before the branch builds from the committed tree.
- Smoke (slot subclass) with genmm 1: lobby PASS 0/593, join PASS 0/596, quickplay PASS 0/599, rematch PASS. Without it: lobby PASS 0/599.

### Batch 10 (2026-10-17): quick play Platform, ping stats in genmm
- Row 6: every hand UpdatePlayer now carries TF.EX's SendUpdatePlayer fields: Platform "Switch", HaveModifiedGameFiles, the input
  delay. The server creates quick play members without a Platform, and the generated roll call's EnterJoined publishes through the hand
  UpdatePlayer, so the page played the Switch at 240 Hz and desynced. Smoke quickplay PASS 0/594 (without genmm).
- Row 7: the GGRSFFI.ping_measurement_stats rule is on (out var, tfex-translator 14fb565): with genmm 1 TF.EX's own code measures and
  reports peers (server: "<switch> measured <page>"). Needs a regeneration (tfex-translator). Smoke lobby genmm 1 PASS 0/599 (local
  regeneration).

### Batch 11 (2026-10-17): TF.EX's generated MatchmakingService on by default
- hw.txt genmm defaults to 1 (genmm 0: the hand service alone, the fallback). Messages go to TF.EX's parser through gen::ManagedUtf8 again
  (tfex-translator's fix: no 1023-character cap).
- Smoke (slot subclass, defaults): lobby PASS 0/596, join PASS 0/596, quickplay PASS 0/597 (the page: "Switch lobby: this match runs at
  60 Hz", i.e. TF.EX's SendUpdatePlayer published Platform Switch), rematch PASS, quit PASS. Ping stats wait for the regeneration with
  the ping_measurement_stats rule (0dc1210).

### Row 8 notes (2026-10-17)
- Team Deathmatch hosted by the Switch (smoke lobby + hw.txt `lobbymode 5`, `lobbyteam 0`): TF.EX's generated RollcallElement_Render
  postfix draws BLUE under P1 (screenshot runs/161707). The page joins, but it never picks a team, so the lobby can't start (TF.EX's
  AreTeamsPlayable). The remote seat's label and the start wait for Integration B's `team` scenario (asked 2026-10-17).

### Batch 12 (2026-10-17): team and three smoke scenarios (rows 8, 17)
- tools/smoke.sh `team`: the Switch hosts TDM on BLUE (lobbymode 5, lobbyteam 0), the page joins on RED (LVB_TEAMKEY ArrowDown); the
  screenshot is the roll call at "[lobby] seat 1: team RED" (per-scenario shotdelay 0). PASS 0/599: P1 BLUE, P2 RED, the ping line.
  The roll call logs each seat's team; the seat debug line now shows the lobby's players (seat, ready, team).
- Row 8 fix: when TF.EX's NotJoinedUpdate prefix holds an occupied foreign seat (its player not ready yet), our "WAITING FOR PLAYERS"
  label no longer stays on it.
- tools/smoke.sh `three` (lobbymax 3, LVB_PAGES=2). The harness for several pages: every page still on TF.EX's "What's new" box gets the
  nudge (it was the first page only), a later page that didn't join retries, and every page readies again once all are in. Not passed
  yet: from ~16:45 the harness page (the browser's switch-rtc worktree, being changed then) crashes with a wasm "function signature
  mismatch" right after it connects to the server, in every lobby (also plain `lobby`); reported to the browser session. Before that,
  `team` passed.

### Retired hand lines (running list, docs/HAND-CODE-INVENTORY.md)
- d301826: MainMenuPatch hand create/join paths, 80 lines (CreateLobby, OnCreateSuccess/Fail, OnJoinSuccess/Fail, JoinFromBrowser,
  JoinPrivateFromScreen, HostFromBuilder, PublishLocalFirst, header lines).
- 2026-10-18: tfex/LobbyInfos.cpp + .hpp (92 lines, dead: TF.EX's generated LobbyInfos is what the screens use); MainMenuPatch
  WhyNotJoinable / WiderSetRefusal (24 lines, no callers: TF.EX's generated browser decides CanJoin). Smoke lobby PASS 0/599, join PASS 0/599.
- 2026-10-18: the roll call's hand fallbacks behind the generated RollCallElementPatch (g_genRollcall is set whenever its hooks install,
  independent of notif): the hand team label / team selection, the remote seat's join / leave / archer, the hand publish on join / leave
  and the hand Back -> LeaveLobby; their helpers UpdateTeamLabel, HandleTeamSelection, ShowRemoteArcher, PublishLocal (94 lines). Smoke
  lobby PASS 0/597, join PASS 0/595, team PASS 0/596; quickplay synced 0/595 (its expected line "quick play: OPPONENT FOUND" was the
  retired hand screens': told B).
- 2026-10-18 (step 1, the hand lobby readers): RollcallPings reads TF.EX's own lobby (gen::svc::GetOwnLobby) and its MatchmakingService
  (svc GetPingTo / GetConnectionQualityTo / IsRelayedWith now call the generated object directly in genmm, no hand round trip). Smoke
  team PASS 0/599 with "306 MS" on P2, lobby PASS 0/596.
- 2026-10-18: notifications on by default (Notification.cpp; hw.txt notif 0 = off: TF.EX always shows them); the notif-off fallbacks
  retired: the ArchivesButton NETPLAY stand-in and its icon swap (SetNetplayIcon), the moved vanilla co-op button, UnreadyOwnRollcall,
  RollcallNotices and their leftovers (94 lines). Smoke lobby 0/592, join 0/593, team 0/599 PASS; quickplay synced (stale expect line).
- 2026-10-18: the Switch-only local second player retired (the coordinator: several local players per device goes the TF.EX way,
  upstream Fcornaire/TF.EX#283): tfex/LocalGuest.cpp/.hpp (129), MainMenuPatch's GuestPad / GuestSeat / PublishGuest / FreeLocalSeat,
  the guest branches of the roll call hooks, GuestUpdate + the pad2 test driver, JoinRemote / LeaveRemote and leftovers (~110), the
  hw.txt keys lobbyguestnet / lobbypad2 and the controller-2 press hook in xplay.cpp (3). Smoke lobby 0/595, join 0/599, team 0/594 PASS,
  quickplay synced 0/594.
- 2026-10-18: leftovers of the retired paths (team label statics, the shown-archer cache, OnNotifyServerFail, the never-set create/find flags and the hand "NO SERVER / CREATING" seat label while the lobby is empty: TF.EX's builder shows its loader), ~45 lines. Smoke lobby 0/596, team 0/596 PASS.
- 2026-10-18 (step 1): MainMenuPatch reads TF.EX's own lobby (gen::svc::GetOwnLobby: Own / OwnEmpty / OwnPlayer / PlayerAtSeat) everywhere but WarnUnportedVariants (the hand VariantsExtensions); 17 hand-model reads moved. Smoke lobby 0/599, join 0/599, team 0/597 PASS, quickplay synced 0/596.
- 2026-10-18: TF.EX's generated MatchmakingService is the only lobby (hw.txt genmm 0 ignored): the hand MatchmakingService's lobby state,
  JSON handling, requests, ping stats, kick / update-required / connection-update handling and its model-typed API retired; it keeps the
  Switch's transport (socket thread, send / inbound / game-thread rings, ping measurement switching) and thin forwarders. The services
  call TF.EX's object directly (no hand conversions, no own-lobby sync / self-test; UpdateLobbySettings sends TF.EX's own message model);
  B's session-player services read TF.EX's lobby (Lobby.SessionPlayers). LobbyTest's hand lobby driver retired. Net ~884 lines.
  Smoke lobby 0/597, join 0/592, quickplay 0/596, team 0/594, privatejoin 0/596 PASS.
- 2026-10-18: Models.cpp / .hpp retired (no reader left: WarnUnportedVariants reads TF.EX's lobby through B's VariantTitles API, the hand-model GetOwnLobby copy and B's two StrList wrappers gone). Smoke lobby 0/594, join 0/593, team 0/595 PASS.
- 2026-10-18: main menu: TF.EX's MainMenu.CreateMain postfix (generated MainMenuCreate: the NETPLAY button via AddNetplayButtonToMenu,
  the netplay mode cleared) replaces the hand NETPLAY button, its ctor-recording hook, the compact co-op button scales and the return
  flag (~160 lines). WiderSet is built into the Switch (the user): the rule WiderSetMenu.SelectionState / Resolve = state 7, the
  Switch's STANDARD / WIDE picker, so NETPLAY goes through it as with WiderSet on the PC; its mode buttons continue to NetplaySelect
  (kept S-wide glue: ModeButtonAction). Smoke lobby 0/599 PASS (log: NETPLAY -> state 7 -> STANDARD -> NetplaySelect).
- 2026-10-18: roll call, the local controller: TF.EX's InputService.EnsureLocalControllerSeat (was a "todo" rule returning false)
  in the runtime: the local controller moves to PlayerInputs / MenuInputs[local seat], and TF.EX's UpdateLobby / reconcile then run
  HandleControlChange. The hand "first controller onto the local seat" binding in the NotJoinedUpdate hook is deleted. Smoke join 0/599
  PASS (log: "EnsureLocalControllerSeat: the local controller to seat 1"). Next: B wires TF.EX's XGamePadInputPatch into InputLock's
  getters, then the hand Confirm/Start (RequestStart, EditLobby) in the roll-call hooks goes.
- 2026-10-18: roll-call input: TF.EX's XGamePadInputPatch (generated) runs as postfixes on the JoyConInput menu getters in
  tfex/InputLock.cpp (B2's go-ahead), replacing B2's hand Gated() (kept: the PC's emptied inputs outside a lobby, S-input). Its
  intercepts start the lobby match (+, as on the console; Confirm too where the game reads it) and open the lobby editor, so the hand
  Confirm/Start code in the roll-call hooks (RequestStart, EditLobby, the CanStart wrapper) is deleted. TF.EX's forced Start once
  IsLobbyReady runs the vanilla RollcallElement.StartVersus, whose prefix/postfix the Switch's native start replaces (S-start): a hook
  skips it in a lobby. The auto-UI starts with +. The hand open-seat label stays (TFEX-DIFF: TF.EX's orb needs the PC's controlIcon;
  8928d2a reverted). Smoke lobby 0/597 in sync (its expected line was the deleted hand log: TF.EX's
  '[tfex] Send ... StartLobbyChoice' now, B2 updates smoke.sh), join 0/599 PASS.
- 2026-10-18: lobby texts: TF.EX's MainMenu_Render postfix, generated by the translator (ws/tfex-update 74f341b: per-seat ping and
  ConnectionIcon, WAITING FOR HOST, CHECKING CONNECTIONS, QUICK PLAY starting, InputDelayAdvisor, the searching count) runs on
  MainMenu.Render; tfex/RollcallPings.cpp/.hpp (the hand port) and the ping part of the roll-call render hook deleted (~60 lines).
  Smoke lobby 0/599 in sync, no exception (the pings not seen in the screenshot: it came on the map screen).
- 2026-10-18: MainMenuPatch leftovers: the ArchivesButton MenuAction hook and the four look getters (NetplayScreens' stubs return
  false: TF.EX's generated NetplayMenuButton carries its own look and action; 5 trampolines freed), and two duplicates of TF.EX's
  MainMenu Update postfix (generated, run each frame by NetplayScreens::Frame): the hand NoGamepadUpdates toggle and a second
  ReconcileRollcallIfPending call. ~45 lines. Smoke lobby 0/597 in sync (seats join and draw their archers).
- 2026-10-18: the failed server connection goes through the transport's game-thread queue (TF.EX's RunOnGameThread in
  ConnectToServerAndListen: a main menu goes to NetplaySelect) instead of a flag the menu polled (g_connectFailed /
  ConsumeConnectFailed / the Update line deleted). Build only (no smoke scenario refuses the server); same log line.
- 2026-10-18: the roll call's last hand hooks (NotJoinedUpdate, JoinedUpdate, Render) deleted: TF.EX's RollCallElementPatch
  runs as its generated hooks on those addresses (HookGen; all its methods enabled). Its prefix / postfix hold occupied foreign
  seats; a foreign open seat's input is gated by TF.EX's XGamePadInputPatch (InputLock). The open-seat label (TFEX-DIFF S-input)
  moves to the per-frame Update; the per-seat call counters in the "[lobby] seats" log go. ~60 lines. Build only (no testing
  for now, 31badab).
- Total so far: ~2,170 lines, and 1 todo rule.

- 2026-10-18: wide NETPLAY without the curtains (side art): WiderSet's IsWide (SetWide: the STANDARD / WIDE picker after NETPLAY;
  TF.EX's CreateMain postfix sets it false on the main menu) now also switches the Switch's own wide screen layout, as its
  StandardModeButton / WideModeButton do (VersusMatchSettings.SixPlayer and MainMenu.RollcallMode = 0 / 1). TFEX-DIFF S-wide: it
  stands in for WiderSet's widened menus on the PC. Build only.

- 2026-10-18: the settings store in TF.EX's shape (the user's decision, CORE-FLOW Decisions): TF.EX's NetplaySettings (generated,
  translator 010039d) holds the values, kept in sd:/xplay/TF.EX.settings.json with its JSON keys (S-native: FortRise's settings
  file); its own Apply (clamping, the upper-case name, the default server, the modes, a "local:<guid>" PlayerId) fills TF.EX's
  NetplayPreferences (generated statics: the Name / Server rules are gone), once on the first main-menu frame; normalized values
  are saved back. netplay.txt: the first-run import (name, server, inputdelay frames -> ms) and the Switch-only connection keys (ip,
  rtcbind, stun). Identify sends TF.EX's PlayerId (it was a new UUID each boot). Replays Off (UseInstantReplay false; the hand
  InstantReplay / InputDelay fields gone). Build only (with 010039d's gen/).

- 2026-10-18: series / best-of: TF.EX's SeriesScreen (all 36 methods), HandleSeriesLobby and Series.SideOfSeat generated by the
  translator (ws/tfex-update edabf78). No hand wiring needed: TF.EX's generated MainMenu_CallStateFunc (NetplayScreens' hook)
  dispatches MenuState.SeriesLobby (61) to HandleSeriesLobby, which makes the screen in the menu (its class made on first use,
  outside rollback); the server's SeriesLobby message runs TF.EX's ReturnToMenu(SeriesLobby) through the generated handlers
  (B2's). Build-checked with edabf78.

- 2026-10-18: evaluation follow-up, Wave 1 (recommendation 4), lobby side: the hw.txt keys genmm / genmenus (they only logged
  "ignored"; an old line is now skipped like any unknown one), MatchmakingService's dead members NotifyMatchEnded (the hand
  MatchEnded: TF.EX's generated HUDPatch sends it), RematchChoice, ArcherSelectChoice, RequestStart, IsCheckingConnections,
  ResetLobby, and ResetPeer's hand fallback. Kept: SwitchPingMeasurementUrl (the services' ping edge calls it) and the forwarders'
  null guards (TF.EX's object is made on the first menu). Build only.

### Checks for the next test day (no testing for now, 31badab)
- Roll-call hooks generated (c.f. the commit after e308b3d): smoke lobby, join, team, three, private, quickplay: seats join, ready
  and draw their archers; an open remote seat shows WAITING FOR PLAYERS; a creating lobby (Loader) takes no input; Back on our
  unjoined seat leaves to the lobby browser. Log: "[tfexgen] hook RollcallElement.NotJoinedUpdate call 1..8" (prefix/postfix on).

### Bugs seen in play
- 2026-10-17 12:15, Ryujinx slot subclass, smoke lobby: guest crash in the match at tick 572, in the runtime (PC +0x47320 <- +0x483cc
  <- +0x7fe6c <- the main loop +0x841ac), invalid access at 0x10; not reproduced on the rerun. Logs /tmp/xplay-runs-eb755020-subclass/runs/121536.
- 2026-10-17 10:30-10:50, Ryujinx vs the switch-rtc page **after it moved to TF.EX v0.21.1** (0f31636): a Switch-hosted REMATCH never
  connects match 2 (tools/smoke.sh rematch, 3 runs, also with master's C++). Both sides start their second session in the same room
  with the same peer ids; the Switch's room socket gets no signal (no NewPeer/offer); the page logs TF.EX's "[MatchmakingService] Error
  while listening to server message: TaskCanceledException". Before the page's v0.21.1 (phase 3b, 01:15) rematches passed. Open:
  page-vs-page rematch on v0.21.1, and what v0.21.1 changed in the rematch reconnect. Logs: /tmp/xplay-runs-eb755020/runs/103946.

## Inventory (2026-10-17)

Already adopted (hand files are thin wrappers over the generated unit; nothing to do here): Notification (tfex/Notification.cpp),
VersusSelectBanner (tfex/VersusSelectBanner.cpp), LobbyPanel + LobbyInfos (tfex/LobbyInfos.cpp), ConnectionIcon and
MainMenuPatch.RenderBannerWithDots (tfex/RollcallPings.cpp). Test-only units: Demo, BannerTest, StaticBannerTest, PanelTest.
Auto_* units are dependencies pulled in by the others.

Units with `hooks = true` (translate.toml), each off unless hw.txt `genpatch <Class>`. All four are fully translated (coverage
v0.21.0: ScreenTitlePatch 2/2, VersusCoinButtonPatch 1/1, ReadyBannerPatch 1/1; GameplayLayerPatch: the 3 render methods of 8).
All four ran together in two Ryujinx lobby matches (ws-tfex-update.md phase 4: 0 of 599 differing, twice).

## Ranked candidates (safest first)

### 1. ScreenTitlePatch -> replaces NetplayScreens.cpp ScreenTitleChangeStateHook + ScreenTitleCtorHook
- Generated: ctor prefix (a state the Switch's enum doesn't define -> VersusOptions, Switch value 8); ChangeState prefix
  (TF.EX's netplay states 58-65 -> targetTexture = menuTitles/fight, original skipped; Changelog 66 -> null).
- Hand port: passes Rollcall (5) to the original for 59/60/62/63/64/QuickPlaySearch, in both the ctor and ChangeState.
- Same behaviour: the PC's dictionary maps both Rollcall and VersusOptions to menuTitles/fight; the Switch's ctor fills key 8
  (disasm of +0xb3ee20: set_Item with 2,3,5,6,8,7,9..15,1,4), so the remapped ctor can't throw (the 2026-10-16 QUIT crash).
  Small differences, all TF.EX's: ChangeState doesn't run the original for netplay states (no title tween bookkeeping; TF.EX's
  own behaviour); Rollcall would pick trials/quest/darkWorld titles from RollcallMode, VersusOptions never does (netplay sets
  RollcallMode = Versus anyway). No TFEX-DIFF left in the hand port worth keeping.
- Tested: menu walk (gen-demo.sh, FIGHT! on NetplaySelect/LobbyBrowser), the two lobby matches above.
  Not tested: the ctor path with the generated hook (QUIT from the end-of-match menu = `new MainMenu(NetplaySelect)`: hw.txt
  `lobbyvote 3`). That run is the switch-over test.
- Switch-over: `genpatch ScreenTitlePatch` default on in xplay.cpp; drop the two hand hooks (keep `IsCustom`, used elsewhere).
- **Done (2026-10-17).** Tested in Ryujinx (shared, build of this branch on master fd273bd):
  - QUIT path (`tools/tfex/translator/test/adopt-lobby.sh`: lobbyhost 1, lobbyscript 1, lobbylength 0, lobbyvote 3, npdigest 1):
    the match played to its results (frame 2558), `[lobby] test: QUIT`, `back to the lobby screen (MainMenu 62)`, then
    `[tfexgen] hook ScreenTitle.ctor (state 8)` (62 remapped by TF.EX's prefix), ChangeState(8) original runs, `[menu] state 62`,
    the game kept running (menu polls 4320-4680). The compare of that run is void: the Switch's vs-log-3m.txt was not rewritten
    (file time 02:37, before the run; its content was an older vsstop-600 run's). A harness/writer issue, reported.
  - Normal lobby match (lobby-vs-browser.sh, LVB_PREKEYS=c, digest on both sides, vsstop 600): **0 differing ticks of 592**,
    tick log from that run (03:01), no desync event on the page.
  - Menu walk (gen-demo.sh, `lobbyui 3`, no genpatch line: the default): ChangeState 62 (NetplaySelect) and 59 (LobbyBrowser)
    "original skipped" (the generated prefix sets menuTitles/fight), vanilla states 1-7 "original runs".
  - Not checked: the title on screen. Screenshots came back black (the Mac's display was locked at 03:00); the FIGHT! title
    with the generated prefix was seen on screen before (ws-tfex-update/stage4-genpatch-screentitle.png), not with this build.

### 2. VersusCoinButtonPatch -> no hand port (new TF.EX behaviour)
- Generated: Render prefix, skipped while netplay (`tfex::IsNetplay() || svc::InSession()`).
- Nothing in the hand code hooks VersusCoinButton. In the runs so far its hook was installed and never called: the Switch's
  netplay screens don't show a vanilla VersusCoinButton (the lobby builder is NetplayScreens' own). So turning it on is almost
  certainly invisible today, and becomes right once a TF.EX screen that contains one is used.
- Risk: a render-only skip. Switch-over = the default line only.
- **Done (2026-10-17), default on** (xplay.cpp's Enable list). Tested in Ryujinx: lobby match (LVB_PREKEYS=c, digest both sides,
  vsstop 600) **0 differing ticks of 599**, tick log from that run, no desync event; the hook was installed and never called
  (no `[tfexgen] hook VersusCoinButton.Render` line). Offline menu walks (test-scripts/menu-to-versus.txt, and the same with a
  second pad) stopped at the roll call, so "original runs" offline was not seen either. Not tested: any screen with a coin button,
  online or offline. Where it matters: TF.EX hides the vanilla VersusCoinButton while netplay; no Switch netplay screen shows one
  today, so this is parity for later screens, with no visible change now.

### 3. GameplayLayerPatch -> no hand port (new: TF.EX's in-match ping line)
- Generated: BatchedRender postfix (RenderPings / UpdatePingAlpha / DrawPingLine) over the services' GGRS stats.
- Tested: 0 of 599 twice; "P2 : 429 MS" drawn in P2's color with quality color and relay/direct icon.
- Open before default: the console (value vs the browser's own TF.EX overlay, per-frame string allocation cost on the console's
  GC, the alpha fade, 3-4 players). Ranked below 1-2 because it runs every in-match frame and has no hardware run yet.
- **Done (2026-10-17), default on.** Ryujinx: lobby match with forced rollbacks (page netdelay=40, netjitter=20; digest both
  sides; vsstop 600): **0 differing ticks of 599**, 8 rollback lines in the guest log, the hook ran every frame, no desync event.
  Note: it allocates managed strings (Concat/FromInt) every rendered frame, between ticks; in sync here, but the console's GC and
  rollback heap under a full match are unproven. Not seen on screen in this build (screenshots black: display locked).

### 4. ReadyBannerPatch -> replaces MainMenuPatch.cpp GetReadyStateHook (needs a decision)
- Generated (TF.EX): postfix; only when the original says ready, VersusMatchSettings exists, netplay, own lobby non-empty:
  `__result = IsLobbyReady() || CanHostStart()`.
- Hand port: ignores the original in the lobby; `CanStart(mm) || IsLobbyReady()`, where CanStart = CanHostStart **and
  playerCount <= 2 and no local guest seat** (a TFEX-DIFF: the hand host-start path in JoinedUpdate uses the same CanStart;
  reason not written down, presumably the Switch's local-guest / 3+-player start limits).
- Two differences: (a) TF.EX needs the vanilla ready state first (depends on how the hand RollcallElement hooks drive remote
  seats: unverified); (b) in a 3-4 player lobby TF.EX's banner says ready while the Switch's host start refuses (CanStart).
  The generated hook was never called in the harness (the scripted host never shows the roll call's banner).
- Recommendation: keep the hand hook until the CanStart limit is either removed or kept as a ledger entry (a translate.toml
  `[[replace]]` for tfex-translator), and until one hand-driven lobby shows the banner with the generated hook.

- **Done (2026-10-17), default on**, after the coordinator's decision (follow TF.EX). MainMenuPatch.cpp: the hand GetReadyStateHook is
  gone; `CanStart` = `CanHostStart` without the stale `playerCount <= 2`. **TFEX-DIFF kept:** no host start while controller 2 holds a
  local roll-call seat or LocalGuest is in the lobby (Switch-only code TF.EX doesn't have; the match start carries one local player per
  device until the couch-online plan lands). In that case the banner (TF.EX's rule) can say ready while the start is refused.
  Test driver: `lobbyui 1` now presses start only once the lobby is full (`lobbymax`), as `lobbyhost 1` already did; without that it
  started a 3-seat lobby with 2 players, which TF.EX allows.
  Tested in Ryujinx (master 1a200ef + this):
  - The Switch hosts through its own UI (`lobbyhost 0, lobbyui 1, lobbymax 3`), two pages join (LVB_PAGES=2), host start:
    3-player match, **0 differing ticks of 599 against both pages** (digest on, vsstop 600; tick log from that run). Repeated with the
    trace below: 0 of 593 against both.
  - Banner, log-only (a temporary, uncommitted hook over the generated one logged its final value on change):
    ready 0 (1 player) -> ready 1 (2 players, CanHostStart) -> 0 (3rd joined, not ready) -> 1 (3 ready) -> A: StartLobbyChoice ->
    0 (start requested) -> 1 (IsLobbyReady). The vanilla value under the postfix was true in each case.
  - One run before these crashed at the main menu (state 2, null read at +0xa61a40 from +0x93d570 / +0xa9f6b0, 15 s after boot,
    before the UI driver pressed anything or any lobby code ran). Not reproduced in 3 later runs on the same path. Kept:
    /tmp/xplay-runs-eb755020/runs/042750/ryujinx.log.
  Not tested: the banner on screen (display locked); a 4-player lobby; controller 2's local seat with the new CanStart.

### 5. WhiteArcherPrisonPatch -> replaces rnd.cpp WhiteArcherSpawnHook (unit from tfex-translator, 2026-10-17)
- Generated (TF.EX): prefix on WhiteArcherPrison.Spawn; in a deterministic session `__result = new WhiteArcherPrisonBroken(xml.Position())`,
  original skipped. Deterministic = VersusMatchSettings set and (`tfex::IsNetplay()` or a GGRS session), or the services' netplay mode
  is Test (replay mode: false here).
- Hand port (3397e5d): made Unlocks.WhiteArcherRevealed read true around the original in pcgameplay's online mode, so vanilla returned
  the broken prison. Same result; only one of the two may exist, so the hand hook is gone.
- Difference: the hand one keyed on pcgameplay::On(), the generated on netplay mode / a GGRS session. The lobby and xnet paths start the
  session before the level is built, so they're covered; only the old non-GGRS lockstep path (g_npOn off) now gets the vanilla prison.
- **Done (2026-10-17), default on.** Ryujinx: lobby map 9, seed 3 (first level Moonstone 05), digest both sides, vsstop 600:
  `[tfexgen] hook WhiteArcherPrison.Spawn call 1: ... original skipped`, **0 differing ticks of 592**. This save has the white archer
  revealed, so vanilla would also have built the broken prison: the log line, not the compare, shows the generated path ran.
  Not tested: a save without the white archer revealed (the case that desynced before 3397e5d); WiderSet's Moonstone 12.

### 6. FightButtonPatch -> replaces MainMenuPatch.cpp FightActionHook
- Generated (TF.EX): postfix (Priority.Last) on FightButton.MenuAction: `WiderSetMenu.IsNetplayRequested = false`, ClearNetplayMode.
- Hand port: a prefix clearing its own `g_netplayRequested` (no ClearNetplayMode).
- Not equivalent as generated alone: the generated `IsNetplayRequested` is the unit's own static (tfexgen_Auto_WiderSetMenu), not the
  hand flag, so turning on genpatch with the hand flag kept would never clear what the hand code reads. Fix in the hand code:
  `g_netplayRequested` is now a reference to the generated static (one flag). Prefix -> postfix: nothing reads the flag inside
  MenuAction (its readers are NetplaySelect's Create and the STANDARD/WIDE buttons, both later). ClearNetplayMode on FIGHT is new
  but changes nothing in practice (the main menu already left netplay mode).
- **Done (2026-10-17), default on.** Ryujinx: offline walk (test-scripts/menu-to-versus.txt): the FightButton is on screen 3,
  `[tfexgen] hook FightButton.MenuAction call 1 ... original runs`, then 7 (STANDARD/WIDE) -> 5 (local roll call). Lobby match through
  NETPLAY -> STANDARD (lobbyhost 0, lobbyui 1; the flag set and read): state 62, host start, **0 differing ticks of 592** (digest).
  Not tested: NETPLAY -> back -> VERSUS -> STANDARD by hand (the case the flag exists for: it must land on the local roll call).

### 7. RollcallLayout -> replaces the offset copies in RollcallPings.cpp and MainMenuPatch.cpp (static helper, no hooks)
- Generated: PING (0, -66) / WIDE_PING (-16, -40), NAME, TEAM (0, 114) / WIDE_TEAM (0, -20); PingAt/NameAt/TeamAt(element) = position +
  offset, wide from the services' IsWide (= MainMenuPatch::IsWide, the flag the hand caller passed).
- Hand: RollcallPings kept its own PING/WIDE_PING (same values); the team label always used (0, 114). **TFEX-DIFF fixed:** in a wide team
  lobby TF.EX puts the team label at WIDE_TEAM (0, -20). Now RollcallPings uses PingAt and the team label TeamAt (minus the element's
  position: it is a component offset).
- Couch: the ping line's "not my seat" test is `!svc::IsLocalSeat(seat)` now (was `seat != GetLocalSeat()`; the RoomPeerId test next to
  it already left out every seat of this device, so no change today).
- **Done (2026-10-17).** Ryujinx: wide team-deathmatch lobby hosted by the Switch through its UI (lobbyhost 0, lobbyui 1, lobbywide 1,
  lobbymode 5, lobbyteam 0; page WiderSet + Red): team labels BLUE / RED built, host start, **0 differing ticks of 599** (digest).
  Not seen on screen (display locked): the label and ping positions.
- Found on the way: unit 4's `lobbyui 1` driver change waited for the lobby's MaxPlayers, which wide and team lobbies raise, so
  Switch-hosted wide/team test runs never started; fixed in its own commit (the driver waits for hw.txt lobbymax; the LobbyBuilder no
  longer overwrites that value).

### 8. LobbyBorderButton (unit on master 63fb775): nothing to switch alone
- TF.EX's abstract base (a BorderButton with an icon wiggler, its UI-camera tween on selection, `ownLobby`) of the LobbyBuilder's five
  option buttons: LobbyVersusModeButton, LobbyVersusMapButton, LobbyVersusPlayerCountButton, LobbyVersusCoinButton,
  LobbyVersusSeriesButton. The hand LobbyBuilder (NetplayScreens.cpp, "CreateLobbyBuilder") has no counterpart: it uses vanilla
  OptionsButtons as stand-ins (title + left/right values, its own mode/map/size/length logic; no series button).
- The five subclasses are 6/6, 7/7, 7/7, 7/7, 9/9 in the current coverage report (tools/tfex/reports/translator-coverage.md), so the
  real adoption is the base plus those five replacing the stand-ins as one piece. Needs units for the five (tfex-translator); the
  builder's screen code (MainMenuPatch.CreateLobbyBuilder, START) stays hand-written for now.
- What the stand-ins do that would change: TF.EX's buttons draw their own icons and tween the UI camera; the size button's
  min rules (Team Deathmatch >= 3, editing >= the lobby's players) are hand-coded in SizeMin() and must match TF.EX's.
- Series button: TF.EX always adds it (MainMenu.cs CreateLobbyBuilder: LobbyVersusSeriesButton at (160, 250), the coin button's
  DownItem; it has no "series unavailable" state, only "OFF" / "OFF (SET AT CREATION ONLY)"). The Switch has no series yet
  (IsSeriesPicker / PickSeriesMap todo), and a button that leads nowhere is not shown: **TFEX-DIFF planned** for the switch-over:
  no series button; the coin button is the last item (DownItem null), BestOf stays 0.

### 10. LobbyBuilder buttons -> replace NetplayScreens' OptionsButton stand-ins (2026-10-17, master 9051807)
- TF.EX's LobbyVersusModeButton / MapButton / PlayerCountButton / CoinButton (generated, LobbyBorderButton : BorderButton) at TF.EX's
  positions (MainMenu.cs CreateLobbyBuilder), while the runtime-made classes are on (notif 1, as the banner and the toasts); the
  OptionsButton stand-ins stay when they're off. vb_BorderButton checked in the binary: BorderButton's vtable is 0xb8 bytes (it adds no
  virtual slots), so the vtable-size bug of MainModeButton doesn't apply.
- Own lobby: the buttons edit it through `ownLobby` (services' live copy, written back to MatchmakingService). Creating: seeded with the
  builder's defaults (`WriteOwnLobby`, no RoomId: still IsEmpty to the hand service). Editing: TF.EX's BeginEdit snapshot (MaxPlayers,
  GameData), given back on Back (CancelEdit). START reads `GetOwnLobby()` (flushes the edits) into HostFromBuilder / UpdateLobbySettings.
  LobbyBuilderContext.IsEditing (a generated static) set from the hand flag.
- TFEX-DIFFs: no series button (its Right sets BestOf 1/3/5/7; the Switch has no series), the coin button is the last item; no variants
  list or LobbyVarianText (v1, as before).
- **Needs gen/ regenerated on master:** the writable own lobby is in tools/tfex/translator/runtime/tfexgen_services.*, but master's
  tfex/gen copy is still the old one (GetOwnLobby reconverts every call: every edit lost). Tested here with those two files copied into
  gen/ locally (as translate.sh does; not committed).
- Tested (Ryujinx, notif 1, lobbyhost 0, lobbyui 1, digest both sides, vsstop 600): create with lobbymode 4 / lobbymap 3 / lobbylength 0:
  `LobbyBuilder START: mode 4, map 3, length 0, size 2`, tower 3, goal 1, **0 of 599**. Edit (lobbyedit 1): Right on the generated mode
  button -> `generated edits written back (3): mode 4` -> `LobbyBuilder (editing): mode 4` -> the match in mode 4, **0 of 599**.
  Not tested: Back while editing (the snapshot), the map/size/length buttons by input, a private lobby's builder, the look on screen.

### 9. Blocked (2026-10-17, master 84e6ecd)
- **LobbyBuilder buttons** (unblocked: see 10) (LobbyVersusMode/Map/PlayerCount/Coin/SeriesButton): TF.EX changes the lobby by writing through `ownLobby`;
  the generated `get_ownLobby` -> `svc::GetOwnLobby()` rebuilds a copy from the hand lobby on every call (tfexgen_services.cpp), so
  every write is lost. Needs a writable own lobby with a write-back (services). Series button: its Update does set BestOf > 0 (Right
  cycles 1/3/5/7), so it stays out (TFEX-DIFF) while the Switch has no series.
- **NetplayMenuButton, NetplayButton, CompactCoOpButton** (MainModeButton subclasses): the generated class crashes at its first update
  (InstructionAbort). `vb_MainModeButton = 0xa8` in the binding (smaller than `vb_MenuItem = 0xb8`), so MakeClass copies only 0xa8 bytes
  of the base vtable, and MainModeButton's non-overridden slots (Rotate 0xe8, AddYMult 0xf0, BaseTextScale 0xf8, PlaySound 0x100, ...)
  stay null; a real subclass's vtable (the boot log's FightButton dump) runs to 0x110+. The NetplayScreens switch-over is parked as a
  patch (side-session notes, tfex-adopt-netplaymenubutton-wip.patch): gen::NetplayMenuButton::New per button, onConfirm =
  brute::NewNativeDelegate(t_Action, button, OnConfirm) -> NetplayScreens::MenuAction; ArchivesButton stand-in kept while notif is off.

### Later (need a new unit from tfex-translator; fully translatable at v0.21.0, hand port exists)
| TF.EX class | coverage | hand port | note |
|---|---|---|---|
| WhiteArcherPrisonPatch | 1/1 | rnd.cpp | lockstep (A): a unit + one match on a prison level |
| FightButtonPatch | 1/1 | MainMenuPatch.cpp | menu |
| RollcallLayout | 4/4 | MainMenuPatch.cpp, RollcallPings.cpp | constants |
| LobbyBorderButton | 3/3 | NetplayScreens.cpp | a MenuItem subclass |
| NetplayMenuButton | 10/10 | NetplayScreens.cpp / MainMenuPatch.cpp (an ArchivesButton stand-in) | a MainModeButton subclass |
| CompactCoOpButton | 16/18 | MainMenuPatch.cpp | 2 methods block |
| Models (stage 1) | 50/50 JSON byte-identical | tfex/Models.cpp + Json.cpp | protocol-wide: last |

## Single-local-seat spots (docs/research-couch-online.md section 5: several local players per device are planned)
Don't bake "the local seat" as a scalar into a replacement. Found along the candidates:
- ScreenTitlePatch, VersusCoinButtonPatch: none.
- GameplayLayerPatch: its services adapter `GetNetworkStatsPerSeat` (tfexgen_services.cpp, tfex-services' file) skips one seat
  (`i == seat` from xplay::NpSessionForStats): with several local seats it would draw ping lines for our own extra archers. Needs
  `IsLocalSeat(i)` there when the mask exists (reported, not mine to change).
- ReadyBannerPatch: the hand `CanStart` (MainMenuPatch.cpp) gates on `g_localGuestSeat < 0 && !LocalGuest::Active()` and
  `playerCount <= 2`; the couch plan's step 4 replaces exactly that ("CanStart for 3-4"). Once it does, TF.EX's own
  `CanHostStart` (the generated postfix) is the right banner rule, and the hand hook can go.

## Hardware checklist (one line per adopted unit)
- ScreenTitlePatch (generated, default): NETPLAY menu, lobby browser, private/quick play screens show the FIGHT! title; after a
  lobby match, QUIT from the end-of-match menu lands on the NETPLAY screen with the FIGHT! title, no crash (the 2026-10-16 case).
- VersusCoinButtonPatch (generated, default): offline VERSUS -> options screen still shows the coin (match length) button;
  nothing to see online today (no netplay screen has one). Watch only for a crash on the versus options screen.
- GameplayLayerPatch (generated, default): in a lobby match, "P2 : <n> MS" top left in the archer's color, quality color and
  relay/direct icon; value close to the browser player's own TF.EX overlay; fades when an archer/arrow is under it; a FULL match
  plus rematch with no crash or slowdown (per-frame string allocation on the console's GC).
- ReadyBannerPatch (generated, default): hosting a 3-player lobby, the roll call's ready banner shows only when every joined player
  is ready, the host's A starts the match with 3 players (the old 2-player limit is gone); with a second controller holding a local
  seat, A is refused (TFEX-DIFF).
- WhiteArcherPrisonPatch (generated, default): a lobby match on Moonstone 05 (lobby map 9; the level with the white archer's prison) shows
  the broken prison on both sides and stays in sync; best with a save where the white archer is NOT yet revealed.
- FightButtonPatch (generated, default): NETPLAY, back (B) to the main menu, then VERSUS -> FIGHT -> STANDARD: the local roll call, not
  the netplay screens; and NETPLAY -> STANDARD still opens the netplay menu.
- RollcallLayout (generated helper): in a lobby, the ping line above each remote archer's portrait; in a WIDE team lobby the BLUE/RED
  label sits above the portrait area (TF.EX's wide offset), not under it.

- RollCallElementPatch (generated, boot): host a lobby, the browser joins: its portrait appears with its archer, ready/unready follow
  the browser, B on your own unjoined seat leaves to the lobby browser; in a team lobby the BLUE/RED label under each portrait; no crash
  entering or leaving the roll call several times (the old Layer.Render crash).
- MatchmakingService (generated, on by default since batch 11; hw.txt genmm 0 = the hand fallback): host a lobby, join one from the browser, QUICK PLAY, a private code; the
  browser joins and leaves (toasts, portraits), START starts the match; after a match REMATCH! and QUIT still work; the lobby list
  refreshes; "[tfex] TF.EX's MatchmakingService (generated) made" in the boot log; no crash when 2+ players are in a lobby.
- LobbyBuilder buttons (generated, with notif 1): LOBBIES -> START: TF.EX's GAME MODE / MAP (tower icon) / ARCHERS / MATCH LENGTH (coin)
  buttons; left/right change them; START hosts with those settings; on the lobby screen, START (edit), change the mode, START: the lobby
  shows the new mode; edit again and press B: nothing changes. No SERIES button (TFEX-DIFF).
- Batch 1 (lobby parity): as a joined player, the host edits the lobby: your portrait drops and you ready again; the host leaves:
  back to the NETPLAY menu; a lobby with Return As Ghosts / Dark Portals / Bottomless Treasure shows CAN'T JOIN - MISSING VARIANTS;
  in a lobby: "<name> joined" / "Player left" toasts and the platform notice.
- Wide layout (the commit after d783136 with "curtains"): NETPLAY -> WIDE: no side art on NetplaySelect, the lobby browser / builder,
  quick play, the roll call and the map screen (as TF.EX + WiderSet on the PC: the 420-wide menus); log "[menu] the Switch's wide
  screen layout (SixPlayer 1, RollcallMode 1)". Back to the main menu: the side art returns ("standard screen layout"); local VERSUS
  STANDARD / WIDE still work as before. Watch the roll call in wide: RollcallMode 1 is the Switch's 6-seat roll call (TF.EX lobbies
  have <= 4 seats: seats 5-6 should stay empty and unlabeled); if it misbehaves, drop the RollcallMode write and keep SixPlayer.
- Settings store (the commit after 16a090f): first boot without sd:/xplay/TF.EX.settings.json: log "no ...: the first-run settings
  from netplay.txt", then "settings saved to ..." and "settings applied (TF.EX's NetplaySettings.Apply): name <NAME>, server <url>,
  input delay <ms> ms, player id local:<guid>"; the file exists (1024 bytes, JSON + spaces). Second boot: "settings loaded from",
  the same player id (no save unless something changed). The name shows upper-case in lobbies (TF.EX's Apply); the server connects
  (the Switch's server, not TF.EX's official one); lobbies show replays off for us.
- Series (translator edabf78): host a lobby with the builder's SERIES option (best of 3), play a game: between games the series
  screen (tally, tower pick / veto, archer change, READY with the countdown) instead of the roll call; the next game starts when all
  are ready; at the end the result screen, then back to the lobby. Log: TF.EX's "server message SeriesLobby" and "[menu] state 61".
  Match side (B2): returning from the level to MainMenu state 61 through ReturnToMenu.
- Wave 1 lobby deletions: nothing should change; boot to the main menu, NETPLAY, host and join a lobby, play a match: the end of match
  still reports (TF.EX's "[tfex] Send ... MatchEnded" from HUDPatch), rematch / archer select still vote.
