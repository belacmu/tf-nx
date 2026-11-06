# Workstream C (browser & server): findings, 2026-10-15

Browser repo branch **`ws/browser`** (from `switch-lobby`, worktree
`~/Code/Towerfall Browser/.claude/worktrees/quickplay-mode-deep-link-cd26a5`). Head **`67afc8b`**, which is `switch-lobby` (`4d55007`)
plus one commit. Nothing pushed or deployed. The old `switch-lobby` worktree (`.../scratchpad/wt-lobby`) is untouched.

## 1. Round 1's `StartRound` RNG difference (BROWSER-LOBBY-RELAY.md section 4, "Round logic" item 3): explained

**Cause: match variants.** A lobby created by a browser with the builder left as is has **`"Variants":["NO AUTOBALANCE","SYMMETRICAL TREASURE"]`**.
These are TowerFall's 1v1 *tournament rules*. The xnet harness and the sync-test reference use **no variants**. Nothing else differs:
not the mode (11 vs 3), not `NetplayRoundLogic`, not TF.State's treasure patches, not the test-mode flags.

### Where the variants come from (TF.EX 0.19.2 code)
- On entering the online menu (`CreateNetplaySelect`, `CreateLobbyBrowser`), `MatchSettings.ApplyNetplayMode()` runs and calls `Variants.ApplyNetplayVariantRules()`.
  That function is `NormalizeForNetplay()` + vanilla `MatchVariants.TournamentRules()`, which sets every variant to its `[TournamentRule1v1]` flag.
  Only two variants have that flag: `NoAutobalance` and `SymmetricalTreasure`.
- The lobby builder's toggles show those values. On create, `lobby.GameData.Variants` = the titles of the toggles that are on (`MainMenuPatch` create path).
- A joiner of a normal lobby runs `ApplyVariants(lobby.GameData.Variants)`: DisableAll, then enable by `Title`.
- **Quick play** runs `ApplyNetplayVariantRules()` on both sides, so it always uses the tournament rules, whatever `GameData.Variants` says.
- A **Switch-hosted** lobby sends `Variants: []`. The browser joiner then disables all variants, which is the same as xnet. This is why the 2026-10-15 Switch-hosted
  rehearsal matched for 2,994 ticks through round 1.

### Measured (this branch; one headless Chrome; `xplay2` sync test, idle, seed 1288949485, new option `variants=`)

| run | variants | after `StartRound` round 0 | after `StartRound` round 1 (G 3063) |
|---|---|---|---|
| `xplay2 - - 1288949485 3300` | none | `049846d5eecd1f4e` | `27f1292e1d738907` (= xnet) |
| `... variants=NO_AUTOBALANCE,SYMMETRICAL_TREASURE` | the lobby's two | `049846d5eecd1f4e` | **`f7491cf69323b379` (= lobby-idle)** |

- `cmp.py lobby-idle-A.txt <tournament sync test>`: T −264..2882 are identical (3,146 of 3,147 compared). This includes round 1 after `StartRound` (T ≥ 2797).
  - The only T difference is the known G=1 line (T −265: players not yet in the level in the lobby flow).
  - T 2630 is present only in the lobby log. It is the sync test's skipped tick at the round change.
  - R, U and A lines are all identical.
- One page against a local `local.mjs`, builder untouched → `GetLobbiesResponse` shows `"Variants":["NO AUTOBALANCE","SYMMETRICAL TREASURE"]`.
  The screenshot of the builder shows these two highlighted.
- Earlier note "TreasureRates identical in both setup dumps": that was `MatchSettings.TreasureRates` (null in both), not the spawner's.
  The lobby's setup dump also threw before it reached the variants, so lobby logs never showed them. That is fixed on this branch (variants are printed first).

### When the two variants change the match (vanilla 1.3 code; the Switch's game has the same variants)
- **SYMMETRICAL TREASURE** (`TreasureSpawner.GetChestSpawnsForLevel`): when a chest is placed at x ≠ 160 and the mirrored position (`WrapMath.Opposite`) is also a chest spot, that spot gets the
  same treasure and timer and is removed from the list. The result is fewer loop iterations, so fewer RNG draws and a different chest layout.
  (Same for bomb chests.) Sacred Ground `00.oel` has no `TreasureChest` spots (round 0's only draw is the big-chest `Chance`), so **round 0 on map 0 never shows it**.
  Any later level with mirrored chest spots does, which is nearly every level.
- **NO AUTOBALANCE** (`Session.GetSpawnArrows` / `GetSpawnShield`):
  - Without it, the crown holder (sole highest score, above the lowest) spawns with `max(2, 3-1)` = **2** arrows (4 instead of 6 with Max Arrows).
    With it, the crown holder keeps 3.
  - Without it, a player trailing by ≥ `AutobalanceLosingAmount` (3 for 2 players) spawns with a shield. With it, there is no shield.
  - So it shows from the first round after someone scores. It changes positions and arrows, not the RNG at `StartRound`.
- Idle runs never score, so only the treasure difference showed. With the scripts, Instant ended after round 0, so neither showed. That is why it looked seed- or input-dependent.

### What the Switch must do to match a LOBBY match
1. **Apply the lobby's `GameData.Variants` exactly as TF.EX does.** For a normal lobby: disable all, then enable each listed title on `MatchSettings.Variants`.
   For a quick-play lobby: tournament rules (NoAutobalance + SymmetricalTreasure on, all else off), whatever the list says.
   - The vanilla variant code then does the rest. Keep the chest draws on the gameplay RNG (`RegisterRng` around `GetChestSpawnsForLevel`, as already ported).
   - If v1 only supports `Variants: []`, the Switch must **not join** browser lobbies whose list isn't empty. That means most browser-hosted lobbies, unless the host turns both toggles off.
   - Supporting exactly these two titles covers the default browser lobby and quick play.
2. As host, keep sending `Variants: []` (or list exactly what it applies).
3. **Reference logs:** a lobby log is the reference only for a lobby with the same variants.
   - For no variants, xnet / the sync test is equivalent (plus the G=1 and goal differences in BROWSER-LOBBY-RELAY section 4).
   - For a default browser lobby, run `xplay2 ... variants=NO_AUTOBALANCE,SYMMETRICAL_TREASURE` or `xnet ... variants=NO_AUTOBALANCE,SYMMETRICAL_TREASURE` (new on this branch).
   - The goal still differs: test mode forces 2.

## 2. End of the match and GoalScore (confirmed in code; frames measured in lobby-inst2, BROWSER-LOBBY-RELAY section 4)

**GoalScore** (FortRise `patch_MatchSettings.get_GoalScore`, used because mode 11 `IsCustom`):
`ceil(PlayerGoals(5, 8, 10) * GoalMultiplier[MatchLength])`, with `GoalMultiplier = {0.01, 0.5, 1, 2}`. A `MatchLength` ≥ 4 (Custom) gives `CustomGoal`.
- Note the **HeadHunters table (5, 8, 10)**, not LMS's (5, 4, 3). It is the same for 2 players and differs for 3 or 4.
- 2 players: **Instant 1, Quick 3, Standard 5, Epic 10**. Instant and Quick were logged; Standard and Epic come from the formula.

**`VersusMatchResultsInputGate`** (`TF.EX.Domain.Context`; used in `TFGamePatch.NetplayLogic`):
- `input = (LightPauseMenu.IsOpen || gate) ? default(Input) : polled`, with `gate = !IsReplayMode && !IsTestMode && ShouldFeedNeutralInput(level)`.
- `ShouldFeedNeutralInput`: `!_isRollbacking && a VersusMatchResults entity is in the level && _shownFrames > 60`.
- `OnFrameAdvanced` runs after each `AdvanceFrame` call:
  - when the results are shown and there is no rollback, `_shownFrames++`;
  - otherwise it calls `Reset()`, which sets the count to 0 and clears the rollback flag. A `LoadGameState` request sets `_isRollbacking`, so any rollback restarts the count.
- Measured: results first in the level at G 2319; the input handed to `AdvanceFrame` is neutral from c = 2380 on (61 counted frames).
  With input delay D the first neutral *applied* frame is c + D + 1 (2383 for D = 2). These are 60 frames, not scaled to 240 Hz.
- **xnet never does this** (`IsTestMode` is forced true there). A lobby match does. The Switch must mirror the lobby behaviour.
  The light pause menu also forces neutral input in a lobby match.

**GGRS reset / relay close** (`HUDPatch.HUD_Update` postfix on `VersusMatchResults`): when `finished && !HasReset`, it calls `replayService.Export()`, then `NetplayManager.Reset()`.
That closes the relay socket, so the Switch sees the WebSocket close. Then `HasReset = true`. Measured G 2558, about 239 frames after the results first appear.

**`MatchEnded`** is sent in the same `HUD.Update`, right after the reset. Conditions: mode is netplay, not replay or test mode, and `!IsInit`.
- Once per match (`isMatchEndReported`). Contents:
  - `{"MatchEnded":{"winner_seat": Session.GetWinner() (= handle; team mode: first seat of the winning team), "frame": ExFlags.CurrentFrame, "checksum": SHA-256 hex of the last state or null, "scores": [score per seat 0..min(players, PlayerAmount)-1]}}`
  - Measured: `winner_seat:0, scores:[1,0]`.
- Then `MatchEndChoices` (REMATCH / ARCHER SELECT).
- Server (`matchmaker.js MatchEnded`): only from a player of an InGame lobby. It starts the 33 s vote (`VOTE_MS`).
  **The first report decides** `seriesResult` (later reports are ignored), so a Switch report must agree with the browser's.

## 3. Branch state, tests, what deploys would change

- `ws/browser` = `origin/main` `150ca29` + 10 commits (the 9 of `switch-lobby` + `67afc8b`). It fast-forwards from `origin/main` with no conflicts.
  - Caveat: `git fetch` was refused in this session, so `origin/main` is as of the last fetch (2026-10-13 18:09). Re-fetch and rebase before merging.
  - The local `main` = `origin/main` (`150ca29`).
- `cd cloudflare/tfex-server && node test.mjs`: **all 31 passed**. `tools/build.sh`: builds (used for the runs above).

**`67afc8b` (new on top of `switch-lobby`):**
1. **WiderSet fix (site).**
   - Before: the `TowerFallBrowser.Relay` lobby mod made a browser lobby's `Mods` non-empty. A TF.EX joiner checks WiderSet only when `Mods` is empty
     (`EvaluateLobbyCompatibility` / lobby list: "WIDERSET MOD ON OFF REQUIRED"), so a desktop player with WiderSet in wide mode could have joined a browser lobby.
   - Now: a lobby without a `Teuria.WiderSet` entry also gets `{"Name":"Teuria.WiderSet","Data":{"IsWide":"False"}}`.
     `CanUseWiderSet` then refuses a wide joiner ("WIDERSET MOD NOT WIDE REQUIRED") and lets everyone else in. There is no `Version` key, so no version notice.
   - Verified: the created lobby has `Mods: [Teuria.WiderSet {IsWide:False}, TowerFallBrowser.Relay {Version:1, Fps:60}]`. The refusal itself was read in the code, not tried with a wide desktop client.
   - The Switch already accepts `IsWide` `"False"` (LOBBY-PROTOCOL 4.4).
2. `xplay2` / `xnet`: `variants=` option.
3. The setup dump prints the variants first, so a lobby match shows them.

**A server deploy** (`tools/deploy-tfex-server.sh --wait`) from `ws/browser`:
- The server code is identical to `switch-lobby`'s (`67afc8b` touches no server file).
- The live server's `/` already lists `/relay/<id>` (checked 2026-10-15). If it runs `switch-lobby`'s `d7f51e6`, a deploy changes **nothing** functionally; it only restarts the Durable Objects, which disconnects lobbies.
- Against `origin/main` it would add:
  - `/relay/<RoomId>?peer=<uuid>` (players of an InGame lobby only; 2 peers, 2048 B/msg, 500 msg/s; HTTP 409 when the room is full or the peer id is held by a live socket elsewhere);
  - the `RelayRoomObject` Durable Object (binding `RELAYS`, migration `v3`);
  - lobby-free `xnet-*` rooms;
  - `CreateLobby` refusing `xnet-*` RoomIds;
  - Switch reports (`POST /report` without Origin, `kind:"switch"` only, 64 KB, 600/h per address).

**A site deploy** from `ws/browser`, compared with the current site (`origin/main`).

For everyone:
- `TfexFps` getter patches are installed when TF.EX loads. They return TF.EX's own values (240 Hz) unless a session rate is set.
- `SwitchLobby` patches are installed. They act only in a 2-player lobby with exactly one `Platform:"Switch"` player: relay, 60 Hz, no spectators.
- **Every lobby a browser creates or edits** now carries the two `Mods` entries above. These are visible to all clients; desktop TF.EX ignores the Relay entry.
- `tfnet.js` has the relay transport. It is chosen only for `/relay/` room URLs; WebRTC is unchanged.

Debug only:
- `?tfexfps=`, `?tfexplatform=`;
- the `xplay2` / `xnet` / `xlobby` commands (via `?command=`; their Harmony patches install only when run).

The browser half of Switch lobbies only works after this deploy and with the server above.
