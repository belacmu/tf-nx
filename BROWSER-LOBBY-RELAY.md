# Browser side of a LOBBY match against the Switch: relay, 60 Hz, rehearsal results

Status 2026-10-15: **built and rehearsed browser-to-browser on a local server** (one page pretending to be the Switch).
Nothing pushed. The browser site is not deployed. The server code is in the branch (see section 2 for the deploy command).
Note: on 2026-10-15 ~07:20 the live server's `/` already listed `/relay/<id>` (someone else deployed a relay build).
Check which commit is live before relying on the `xnet-*` rooms or the Switch reports.

Read with: `LOBBY-PROTOCOL.md` (sections 4.3, 5, 6, 8) and `BROWSER-RELAY.md` (relay transport, `xnet`).

## 1. What changed (browser repo, branch `switch-lobby`)

Worktree: `/private/tmp/claude-501/-Users-crab-Code-Towerfall-Switch-Mods/a86873af-5865-44d3-bb29-d370464237eb/scratchpad/wt-lobby`.
The branch is based on `origin/main` (`150ca29`, which includes `6028684`, the deployed report tagging). The local `main`
is one merge behind. It does not touch the user's checkout (`perf/ahead-threshold`).

| commit | what |
|---|---|
| `f5d3d79` `894fcc6` `00088ba` `2fcd0d7` `bf79b6f` | cherry-picks from `exp/xplay-versus`: `7f4d734` (TfexFps), `c3dee1e` `88cdaf5` `8435d2d` (xplay2, which xnet needs), `a355c15` (tfnet.js relay transport + `xnet`) |
| `01cd09f` | tfex-server: `/relay/<RoomId>?peer=<uuid>` (relay.js, local.mjs, worker.js `RelayRoomObject`, wrangler.toml migration `v3`), tests |
| `d7f51e6` | tfex-server: Switch reports (`kind:"switch"`, no Origin); `xnet-*` relay rooms without a lobby; relay refusals as HTTP 409 before the upgrade |
| `2dd2757` | `web/Netplay/SwitchLobby.cs` (Switch lobbies: relay, 60 Hz, no spectators, lobby mod, `?tfexplatform=`), TfexFps settable per session |
| `4d55007` | `web/Netplay/XplayLobby.cs` (`xlobby` command: the xnet log and script for a real lobby match); `?tfexplatform=Switch` also reports `UseInstantReplay:false` |

### Browser (`web/`)
- **`Netplay/SwitchLobby.cs`**, hooked from `TfexPatches.Apply`. It runs as a postfix of `NetplayManager.UpdatePlayers`, which TF.EX's `MapScene.StartSession`
  prefix calls before the level loads and before `Init`. It acts only if the lobby has **exactly 2 players**, one of them with `Platform == "Switch"`,
  and this page is the other one. In that case it does three things:
  - It sets `ServerConf.RoomUrl = <NetplayPreferences.Server>/relay/<RoomId>?peer=<LocalPeerId>&remote=<other RoomPeerId>`. `tfnet.js` then picks the relay transport (no WebRTC).
  - It sets `GGRSConfig.Netplay.Spectators = []`. `AddLateSpectator` and `AddSpectators` become no-ops while the relay session is active.
  - It calls `TfexFps.SetSessionRate(60)`. A postfix on `TFGame.OnSceneTransition` sets the rate back to 0 (TF.EX's own 240) when the scene becomes `MainMenu`.

  Every other lobby is untouched: the rate stays 0, and the patches return TF.EX's values.
- **Lobby mod.** A postfix on `MainMenuPatch.AddWiderSetLobbyMod` adds `{"Name":"TowerFallBrowser.Relay","Data":{"Version":"1","Fps":"60"}}` to lobbies
  that this page creates or edits. The Switch can filter on it.
- **`?tfexplatform=Switch`** (debug; env `TOWERFALL_TFEX_PLATFORM`, set in `main.js`). `Platform.Current` returns that value (backing field + getter postfix),
  so the page's CreateLobby/UpdatePlayer say `"Switch"`. TF.EX's "PLAYER IS PLAYING ON SWITCH" notice shows on the other page. For `Switch`, the page
  also reports `UseInstantReplay:false`, as the Switch does, so the match has no instant replay.
- **`Netplay/TfexFps.cs`**: `Fps` is now `sessionFps ?: pageFps`. `SetSessionRate(n)` changes it at run time, and `?tfexfps=60` still forces it for the whole page.
  The patches are installed at TF.EX load. Patching later is risky because the interpreter may already have inlined the small getters.
  They return TF.EX's values when no rate is set. The `InputDelayAdvisor` constant now calls `FrameMs()` instead of being rewritten once.
- **`Netplay/XplayLobby.cs`**: the `xlobby` command (section 3).

### Server (`cloudflare/tfex-server`)
- **`relay.js`** (`RelayRoom`):
  - Each binary message goes to the room's other peer unchanged. Text messages are dropped, and the server never sends anything itself.
  - **2 peers** per room, at most **2048 B** per message, **500 msg/s** (burst 1000). Over a limit, the server closes with 4029.
  - The same peer id reconnecting replaces its old socket (old one closed 4001). This works from the same address, or once the old socket has been quiet for 3 s.
  - The server refuses with **HTTP 409** before the upgrade when the room is full or a live peer id is claimed from another address. Under workerd, a socket closed while being accepted never finished closing on the client.
- **Membership.** Only **players** (not spectators) of lobby `<RoomId>`, while it is **InGame**, get in: `Matchmaker.isPlayingIn`, and `/member?...&playing=1` on Cloudflare.
  Anyone else gets HTTP 403. A missing or non-UUID `peer` gets 400. The Origin rule is the same as for the other routes, and no Origin is always allowed.
- **`xnet-*` rooms** (`routes.js lobbyFreeRelay`): no lobby check, still 2 peers. This is for the first hardware test: Switch + browser `xnet`, no lobby.
  `CreateLobby` refuses RoomIds starting with `xnet-` ("BAD ROOM ID"), so no lobby can open its relay that way.
- **Reports**: `POST /report` **without Origin** accepts only `{"kind":"switch",...}`.
  - Limits: up to **64 KB**, **600/hour per address**. This count is separate from the pages' 60/hour.
  - Same store and `GET /reports` (STATUS_KEY) as the page reports.
  - The page rules are unchanged (Origin required, kinds `online`/`solo`, 24 KB). A page can't send `kind:"switch"`.
- `local.mjs` serves all of it (binary frames supported). `worker.js` adds `RelayRoomObject` (binding `RELAYS`). `wrangler.toml` has migration `v3` `new_sqlite_classes = ["RelayRoomObject"]`.
- Tests: `node test.mjs` gives **31/31 passed**, also with `--url` against `npx wrangler dev` (local workerd). Never deployed by me.

### Switch repo
- `browser-60hz/xnet-tools/xnet-run.sh`: new env options `ROOM=` (e.g. `xnet-test`, for the tfex-server relay) and `SEED=`.
- New `browser-60hz/xnet-tools/lobby-start.sh` and `lobby-keys.sh` (the lobby rehearsal, section 3).
- New `browser-60hz/lobby-evidence/` (logs, section 4).

## 2. Deploying the server (not done; needs the user's go)

```
cd /private/tmp/claude-501/-Users-crab-Code-Towerfall-Switch-Mods/a86873af-5865-44d3-bb29-d370464237eb/scratchpad/wt-lobby
tools/deploy-tfex-server.sh --wait        # waits until no match is running, then: npx -y wrangler@4 deploy --secrets-file ~/.config/towerfall-tfex.env
```

What changes on the live server:
- the `/relay/<id>` route;
- the new Durable Object class `RelayRoomObject` (migration `v3`);
- Switch reports;
- `xnet-*` rooms;
- the `BAD ROOM ID` rule for `xnet-*`.

Everything else matches `origin/main`, which includes the report tag `slowDownFromTicksAhead`. A deploy restarts the Durable Objects, which disconnects lobbies.

Cost: incoming messages count as 1/20 request. One 60 Hz relay match is about 2 × 125 msg/s, so a 10-minute match is about 7,500 requests (LOBBY-PROTOCOL 5.3).

The browser half (`web/`) only takes effect once the site is rebuilt and deployed. Until then, `xnet` from a local build works against the deployed relay with
`xnet wss://<your-server>/relay/xnet-<name> <seat> <own uuid> <other uuid> ...` (the page needs `?tfexfps=60`).

## 3. Running the local rehearsal

```
WT=<worktree above>;  cd $WT && tools/build.sh               # (vendor/ and gamefiles/ are symlinks to existing ones)
python3 tools/serve.py 18180 &                                 # (18080 was in use by another session's xnet page)
node cloudflare/tfex-server/local.mjs --port 13300 --verbose &
S=<switch-online>/test-scripts/versus; S0=$(tr -d '\n' < $S/script0.txt); S1=$(tr -d '\n' < $S/script1.txt)
BROWSER_WT=$WT OUT=/tmp/x browser-60hz/xnet-tools/lobby-start.sh run1 "script0=$S0" "script1=$S1" maxticks=4400 origin=266
```

Then drive the menus with `lobby-keys.sh A|B keys…`. Each call saves a screenshot.

Wait about 20 s after `[command] xlobby: armed` before pressing keys. Keys pressed while the online menu is still building land on QUICK PLAY.

| step | keys |
|---|---|
| A: LOBBIES | `ArrowRight`, then `c` |
| A: create | `Enter`. The builder opens on mode LMS. |
| A: tower | `ArrowDown`, `ArrowRight`. Random becomes Sacred Ground. |
| A: match length | `ArrowDown ArrowDown`, then `ArrowLeft` for Quick (3) or `ArrowLeft ArrowLeft` for Instant (1) |
| A: create the lobby | `Enter` |
| B: join | `ArrowRight`, `c` (list), `c` (join) |
| both: pick an archer | `c` on A and on B. The default archers are seat 0 green, seat 1 blue (= Characters 0,1, as in xnet). |
| A: start | `c` (FIGHT) |

Fresh profiles also show a "What's new" box first; press `c` once more for it.

Compare the logs:
- `python3 browser-60hz/xnet-tools/cmp.py A.txt B.txt`
- against xnet with the same seed: read `[xplay2setup] seed=` from the lobby log, run xnet with `seed=<it>`, then `cmp.py`.

Stop:
- `curl localhost:19400/quit; curl localhost:19500/quit`
- kill the two servers
- `rm -rf ~/.towerfall-browser/netplay-L{A,B}-profile`

## 4. Results

Runs: Mac, two headless Chromes, local `local.mjs`, lobby **Standard**, MaxPlayers 2, MapId 0 (Sacred Ground), Mode 3, Variants none.
Page B claims Switch. Evidence is in `browser-60hz/lobby-evidence/`.

| run | length | seed (lobby's) | input | result |
|---|---|---|---|---|
| `lobby-idle-A/B` | Quick | 1288949485 | none | 3000 ticks, both pages identical (T/R/U/A), 0 rollbacks. Round 0 ends with no score at T=2484 (both die with no input). Round 1 starts. |
| `lobby-inst2-A/B` | Instant | 598114087 | `script0/1.txt` (origin 266) | full match: seat 0 wins 1-0. Both pages identical to the end of the session (T -266..2292), 0 rollbacks. |
| `xnet-seed598114087-0/1` | xnet (test-mode rules, goal 2) | 598114087 | same scripts | comparison for `lobby-inst2` |
| `xnet-idleseed-0/1` | xnet | 1288949485 | none | comparison for `lobby-idle` |
| (not kept) `srv` | xnet via **tfex-server `local.mjs`**, room `xnet-srvtest` | 42 | reference scripts | 1500 ticks identical to `browser-reference-log-arrows.txt` (T 1766/1766, R 1501/1501) |

### Transport and session (both lobby runs, both pages)
- `[netplay] Switch lobby: this match runs at 60 Hz over the server's relay (ws://127.0.0.1:13300/relay/<RoomId>?peer=<own>&remote=<other>), no spectators`.
- `session init: fps=60 input_delay=2 max_input_delay=20 players=2 spectators=0 relay=True`. The handle is 0 for the host and 1 for the joiner (Players by Seat).
- The server log shows `[signal]` (ping-measurement room, WebRTC) in the lobby. At start it shows `[relay] <host> joined`, `[relay] <joiner> joined`.
  The match used **no WebRTC** (the tfnet relay transport).
- Synchronisation happened within about 0.1 s of level load. After the vote timed out (33 s, no vote cast), `ArcherSelectLobby` returned to the roll call.
  The log then shows `[netplay] TF.EX session rate: TF.EX's own (240 Hz)`.

### GoalScore and MatchLength (logged from `Session.MatchSettings` at the first spawn)

| MatchLength (lobby `GameData.MatchLength`) | GoalScore (2 players) | source |
|---|---|---|
| Instant (0) | **1** | logged |
| Quick (1) | **3** | logged |
| Standard (2), TF.EX's default | **5** | lobby builder's "PLAY TO ×5" (not logged in a match) |

- `MatchSettings.Mode = 11` (TF.EX's netplay mode), `IsCustom = True`, round logic `TF.EX.Core.RoundLogic.NetplayRoundLogic`, `ExFlags.IsTestMode = False`.
- `RandomLevelSeed` = the lobby's seed (`GameData.Seed` of the LobbyUpdate before `StartLobby`). Level order for 1288949485: round 0 `00.oel`, round 1 `03.oel`.
- The vanilla `orig_get_GoalScore` throws for mode 11; FortRise's getter gives the values above.
- The xnet reference used test mode's forced `PlayerGoals = 2`. The UI has no 2-player length with goal 2.

### First-round spawn relative to GGRS frames (G = `netplay_current_frame()`)
1. **Level load.** `Session.LevelLoadStart` and `OnLevelLoadFinish` run with no GGRS session yet (G = -1).
   `NetplayRoundLogic.OnLevelLoadFinish` calls `NetplayManager.Init` and **does not spawn**. This page's input delay is 2.
2. While waiting for sync, `TFGame.Update` still runs the original update. The logger sees one `Level.Update` at G=0 (logged as `T -266`).
   That update draws nothing, and the RNG equals the value after `OnLevelLoadFinish`.
3. **First synchronised loop iteration, G=0.** `EstablishSessionIfSynchronized` → `OnSessionEstablished` → `SpawnFirstRound` → `SpawnRoundPlayers`
   adds `VersusStart` and runs `SpawnPlayersFFA` (its RNG draws). This happens **before** the `AdvanceFrame` of GGRS frame 1.
   The first `[xlobby]` line logs it with `players in the level before: 0`.
4. **GGRS frame 1** (`T -265`). At the start of its `Level.Update` the players are **not yet in the level** (`alive=0`), but the RNG already includes the spawn draws.
   The players and VersusStart are added to the entity lists during this update.
5. **GGRS frame 2** (`T -264`). The players are present, frozen, at their spawn positions.
6. `Session.StartRound` (unfreeze, chests) runs at the end of G=265. **Tick 0 (first non-frozen update) = G 266**, the same `origin=266` as xnet.
   The scripted input at `origin=266` lined up: no shift warning.

**Difference from xnet (same seed and scripts):**
- In xnet (spawn at level load) the players are already present at the start of G=1.
- The lobby log has the extra pre-sync update `T -266`.
- **Every line from T -264 up to the first kill is identical** (inst2 vs xnet: T -264..1977 identical; idle: T -264..2797 identical).
- The RNG at G=1 is identical too (`d5f7b02c9365e761` in both). So the spawn draws happen before frame 1 in both paths.
- The Switch must spawn round 0 **between frame 0 and frame 1**: after sync, before the first `AdvanceFrame`'s `Level.Update`, entities added during that update.
  Rounds ≥ 1 spawn in `OnLevelLoadFinish` (vanilla timing).

### Round logic: what differs from the lobby-free xnet run
1. **G=1 presence** (above). It changes only the `T -265` line.
2. **Goal.** The xnet goal is 2; the lobby's is 1, 3 or 5.
   - With goal 1 the first kill is the **final kill**: speeds differ from T=1978 on (slow motion), and the arrow list from T=2153.
   - Scores, round end and the A lines up to T=2100 are identical.
   - The Switch must use the lobby's GoalScore, not 2.
3. **Round 1's `Session.StartRound` RNG differs.** This is **unexplained**.
   - Idle runs, same seed: after round 1's `OnLevelLoadFinish` both have `6d308be055be5a1c`. After `StartRound` (G=3063), lobby `f7491cf69323b379` vs xnet `27f1292e1d738907`.
   - All earlier ticks are identical, and `TreasureRates` are identical in both setup dumps.
   - The difference is inside `StartRound` (`OnRoundStart` → `SpawnTreasureChestsVersus` / `TreasureSpawner`). Likely cause: test-mode rules in xnet versus the normal rules in the lobby (TF.State's treasure patches?). Not traced.
   - **Consequence:** for rounds ≥ 1 the Switch's reference must be a **lobby** log (`lobby-idle-*`, `lobby-inst2-*`), not xnet or `browser-reference-log*.txt`.
     To trace it, run with `rngtrace` around round 1 (xnet only has it for the first N draws).

### End of the match (lobby-inst2, Instant, seat 0 wins 1-0; both pages print the same frames)

| GGRS frame G (T = G-266) | event |
|---|---|
| 2208 (T 1942) | `round_ending` (Level.Ending; only seat 0 alive) |
| 2319 (T 2053) | `VersusMatchResults` first seen in the level: `ShouldFeedNeutralInput` returns true when `netplay_current_frame()==2319` |
| **2380** (T 2114) | **local input fed to GGRS becomes neutral** (`VersusMatchResultsInputGate`: results shown for more than **60 AdvanceFrames**; 60 frames, not 240-scaled: 1 s at 60 Hz) |
| 2558 (T 2292) | `VersusMatchResults` finished. HUD exports the replay and **resets GGRS** (relay socket closed). `MatchEnded {winner_seat:0, scores:[1,0], checksum…}` sent right after. |
| +33 s | no votes, so the server sends `ArcherSelectLobby`, then the roll call. The session rate goes back to 240. |

How the neutral input works:
- The gate replaces the input **handed to `AdvanceFrame`** while `netplay_current_frame() >= 2380`, i.e. 61 frames after the results first appear.
- With input delay D, that input applies at G = c + D + 1. For this page (D = 2), the first neutral **applied** frame is 2383.
- The gate resets on rollback (`NotifyRollback`) and counts again.

The Switch must:
- start feeding neutral local input from the AdvanceFrame where its results screen has been shown for more than 60 frames;
- keep stepping and sending until the browser closes the relay (the browser's GGRS reset at its results-screen end, about 239 frames after results appear);
- then optionally send `MatchEnded`, then `ArcherSelectChoice`.

The remote's tail after the winner is fixed is cosmetic.

**Without** `UseInstantReplay:false` from the Switch side, TF.EX runs an instant replay at the match end. The first rehearsal showed it:
"[InstantReplay] rebuilt 210 frames", about 7 s of stall, 1080 repeated `Level.Update`s. The Switch must keep sending `UseInstantReplay:false`.

### Exactly what the Switch must reproduce (lobby match)
- Seed: the lobby's `GameData.Seed` (from the LobbyUpdate before `StartLobby`) is the gameplay RNG seed and `RandomLevelSeed`. Handle = rank of own Seat.
  Characters by `ResolveArchers` (here 0 and 1).
- **GoalScore from MatchLength**: Instant 1, Quick 3, Standard 5 (2 players). Mode 3 rules as `NetplayRoundLogic` (score at round end if exactly 1 player is left; crown event).
  Final kill at goal-1 as vanilla.
- **Round 0**: no spawn at level load. Spawn (VersusStart + SpawnPlayersFFA draws) after GGRS sync and before frame 1. Players present from frame 2.
  `StartRound` at the end of G=265, tick 0 = G 266.
- **Rounds ≥ 1**: spawn in `OnLevelLoadFinish` (no extra GGRS frame at the round change in a P2P run; see BROWSER-RELAY section 4).
- **Results**: neutral local input after 60 frames of results; keep the relay up until the browser closes it.
- 60 Hz, 2 players, input delay of the Switch's choice (the browser uses 2), relay `wss://<server>/relay/<RoomId>?peer=<own>`, no spectators,
  `UseInstantReplay:false`, `Platform:"Switch"`.
- Unknown: round 1's chest RNG difference (above). Use a lobby log as the reference past round 0.

## 5. Open items / caveats
- **The 20 s sync timeout may now be 30 s.** TF.EX 0.19.2's "What's new" says the wait for an opponent is now **30 s** (the decompile used for LOBBY-PROTOCOL says 20 s). Not measured.
- **Round 1 StartRound RNG** (section 4) is not explained.
- **Not exercised:**
  - spectators in a Switch lobby (blocked by design);
  - a Rematch vote (it would rebuild the session and call `UpdatePlayers` again, so relay + 60 Hz again; untested);
  - a browser *joining* a Switch-hosted lobby (only browser-hosted was run; the joiner path is the same hook);
  - a real Switch;
  - the Cloudflare Durable Object relay with real traffic (only `wrangler dev` with the tests).
- **`?tfexplatform` caveat.** It sets a static readonly backing field via reflection and patches the getter. It worked here (the notice showed, and the relay was chosen on both pages).
- **Port 18080 conflict.** Another session was running an `xnet` page on port 18080 (from the `wt` worktree, driver port 19700, against the deployed relay room `xnet-hw1`).
  The rehearsal therefore moved to 18180. I did not touch it. My earlier `xnet-run.sh` runs (18080) finished before that page started (07:18).
