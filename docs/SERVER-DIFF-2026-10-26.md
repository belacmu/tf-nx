# TF.EX's matchmaking server vs ours (backlog item 7), 2026-10-26

Read-only comparison. Nothing was changed in either server.

- **Real:** [Fcornaire/tf-ex-matchmaking-server](https://github.com/Fcornaire/tf-ex-matchmaking-server) at `980e86c`
  ("fix: properly handle link lost rather than a plain disconnect"). Rust (axum + tokio), about 9,500 lines, GPL-3.0. It ships
  without ranking (`NoRanking`); ranked play, leaderboards and profiles plug in through a `Ranking` trait (docs/RANKING.md there).
- **Ours:** the browser repo's `cloudflare/tfex-server` at `origin/main` `ffd2b37`, about 2,000 lines of JS on Cloudflare
  Durable Objects (`matchmaker.js` is the matchmaking logic, then `signal.js`, `relay.js`, `match-reports.js`, `turn.js`, `worker.js`).
  It was written from the client's side while the real server was closed source (its header says so). The deployed worker may
  be a different commit: check with `tools/deploy-tfex-server.sh` before you change anything.
- TF.EX's client is the referee: the vendored v0.21.1 `MatchmakingService.cs`, `HUD.cs` and `Lobby.cs` were read where the two
  servers disagree about what a field means.

## The short version

The two servers speak the same message protocol, but they behave differently almost everywhere the client leaves room. These
differences matter most, from the most player-visible down:

1. **Quick play is a different design.** Real: lobbies of up to 4. A newcomer gets seated into the nearest open quick play lobby
   (also on a 1 s tick for players already waiting). The lobby starts by itself once it's full, or once everyone has been ready for
   5 s with no join or leave in those 5 s. If the host leaves, the lowest seat becomes host. Wide lobbies carry the `Teuria.WiderSet`
   mod (`IsWide: True`) and also hold at most 4. Ours: exactly two devices per lobby (MaxPlayers = their player count). It starts
   1.5 s after all are ready, closes as soon as anyone leaves before the match, adds no WiderSet mod, and holds up to 8 when wide.
2. **Rematch resets the lobby.** Real (`Lobby::reset` + `set_in_game(true)`): everyone un-ready, a new seed, MatchPlayers taken
   again. Ours keeps Ready and the old MatchPlayers.
3. **Match results: real waits for every player's MatchEnded** and compares winner, scores and checksum. It reads `winner_seat`
   as a lobby **seat**, which is what TF.EX sends (`HUD.WinnerSeat`: a TFGame player index, and TF.EX's game index is the lobby
   seat, `ArcherData`: `result[player.Seat]`). Ours takes the first report and reads `winner_seat` as an index into the players
   sorted by seat. **That's a bug in ours:** once a seat is empty (seats 0 and 2 after a leave and a rejoin), the series credits
   the wrong side.
4. **Connection check runs all the time in the lobby.** Real: every pair of players gets 10 s from the moment both are seated. One
   PeerMeasured in either direction settles the pair. `IsCheckingConnections` is on while any pair is unsettled, and the host's
   StartLobbyChoice is refused until then. Pairs that fail go to the relay; a player who can't reach the relay either gets
   `KickedFromLobby`. Ours checks only after StartLobbyChoice: it needs both directions, waits 8 s at most, and skips the check
   entirely when one client doesn't report measurements.
5. **Seating several local players per device (our #283 work) doesn't exist in the real server.** It keys every player by its
   connection: one Player per socket. `AddLocalPlayer` / `RemoveLocalPlayer` don't deserialize there and are dropped, and
   `EnterQuickPlay.local_players` is ignored. Two players on one peer id would also break its pair-based connection check.
   Fcornaire/TF.EX#283 needs a matching change in the real server.
6. **Browsers are refused** by the real server (`refuse_browsers`: any request with an `Origin` header gets 403, on every route).
   That's the one reason we must keep a server of our own (or a patched fork); see "What stays ours".
7. **No version gate on ours.** The real server refuses a `/ws` without `?version=` (426), and an older one with `UpdateRequired`
   once `MIN_CLIENT_VERSION` is set. If the variable is unset it requires 0.0.0, so only a missing version is refused. The Switch
   already sends `?version=` and a `local:<guid>` player id (MatchmakingService.cpp:319, :339), so it would pass. The comment in
   our matchmaker.js saying the Switch sends no version is out of date.

## What stays ours (needed, with the reason)

| What | Why the real server's way can't be used |
|---|---|
| Accepting browser pages (Origin allow-list, CORS) | The real server refuses every request with an Origin. The browser port can't connect to it at all. |
| `/turn` (Cloudflare TURN credentials for pages) | Browsers behind strict NATs need TURN. The real server is STUN-only and relays over UDP, which a page can't send. |
| `/relay/<room>?peer=` (binary WebSocket relay, 2 peers) | The Switch has no WebRTC. This is the Switch's match transport, not TF.EX's relay. |
| TF.EX's own relay (`/relay/room/`, `/relay/ping_measurement/`: WS control + UDP) missing | Workers can't take UDP. It only exists if the server runs on a host with UDP (option B below). |
| Performance reports (`POST /report` without `kind`, `GET /reports`) | Our diagnostics for pages and the Switch. They share the path with TF.EX's crash reports but carry no `kind`. |
| Lobby storage + hibernation, alarms instead of timers | Durable Objects: a lobby has to survive hibernation. Not behaviour the client sees. |
| Abuse limits (message rate, frame size, 16 connections per address, lobby JSON cap) | A server anyone can reach. Keep them, but loose enough that the client never hits one. |
| Local-player seating | Until the real server has it (point 5). It's our extension, so it should go upstream with #283 (noted only, see priority-crossplay-threads: upstream PRs are deferred). |

## Every difference, by area

"Move" says what moving to the real server's behaviour means. **Adopt**: do it the real way. **Keep**: see the table above.
**Decide**: the real way has a cost for us, so it needs the user's call.

### Connection, keep-alive, version

| Real | Ours | Move |
|---|---|---|
| Refuses a `/ws` with no `?version=` (426). An older version gets `UpdateRequired {version}`, then the socket closes. | Takes any version, never sends UpdateRequired. | Adopt, with a configurable minimum. |
| Server KeepAlive every 3 s; a client that misses 6 is dropped (about 18 s). | Every 30 s; dropped after 100 s of silence. | Decide: 3 s wakes the Durable Object 10× as often (cost on the free plan). At least shorten the drop time. |
| `Identify`: player_id must start `steam:` or `local:` and be ≤ 64 characters, otherwise it's ignored (logged). | Takes any value, cut to 128 characters. | Adopt (the Switch sends `local:<guid>`, the PC `steam:<id>`; check the browser's). |
| Names: no length cap anywhere. | 10 characters (NetplayPreferences.MaxNameLength) everywhere. | Keep our cap (the client caps at 10 anyway). |
| One socket per client address. A second socket from the same address+port is closed silently. | Up to 16 per IP. | Keep ours (behind NAT many players share an IP). |
| GeoIP location per client (MaxMind), used to rank quick play partners. | None. | Decide (needs a MaxMind key). Without one the real server treats every player as the same distance, which is our behaviour now. |
| Drain on shutdown: new lobbies, joins and quick play refused with "Server updated, try again"; the queue sent away; idle lobbies get `KickedFromLobby`; running matches get a `Notice` 5 min and 1 min before the deadline (44 min). | A redeploy drops everyone. The deploy script waits until no match is running. | Decide (a "draining" flag set through `/status` before the deploy would copy it). |

### Lobby list, create, join, leave

| Real | Ours | Move |
|---|---|---|
| `GetLobbies` lists lobbies that have a host: Standard ones that aren't full, or are in a match, or have a series running; plus QuickPlay lobbies in a match (to spectate). In-game lobbies come last. Peer ids are visible. | Every Standard lobby (full ones too), never QuickPlay, peer ids blanked. | Adopt the filter and the order. Blanking peer ids can stay (the list doesn't use them). |
| `CreateLobby`: the host is the first sent Player, whatever its IsHost says. MaxPlayers is clamped to 2–4 (no team-mode minimum). Room id taken as sent. Seed 0..100000. `Kind: Ranked` refused. JoinCode `null` unless Private. `IsReportEnabled` only when crash reports are configured. | The host is the IsHost player (else the first). MaxPlayers min 3 in team deathmatch. Room id checked (`[A-Za-z0-9_-]{1,64}`, no `xnet-`). Seed 0..2³¹. JoinCode `""`. IsReportEnabled always true. Fields validated (map, mode, length, BestOf). | Adopt the host and MaxPlayers rules, the seed range and `IsReportEnabled`. Keep the room id check (relay rooms). |
| `JoinLobby` by room id works for any kind except Ranked (so Private and in-game QuickPlay too). Refusals: `Lobby <id> not found / is full / already started / has a series running`. No spectator cap. "Player already in a lobby" if still seated (it first removes the client from every *other* lobby). | Standard only ("LOBBY NOT FOUND" otherwise). Messages `LOBBY FULL`, `MATCH ALREADY STARTED`. 8 spectators. Leaves its old lobby first, even the same one. | Adopt (joinable kinds, messages). Keep a spectator cap. |
| `JoinPrivate`: one try every 3 s per client ("Too many attempts, wait a moment"); a bad code is "Invalid or expired code"; the code is trimmed and uppercased. | 20 wrong codes per IP per 10 min ("TOO MANY TRIES"); "LOBBY NOT FOUND". | Adopt the 3 s cooldown and messages (the IP lock can stay as an extra guard). |
| Leaving or disconnecting: the host leaving a Standard/Private lobby: the others get the hostless lobby once, then it's purged (same as ours). In QuickPlay the lowest seat becomes host and the lobby goes on. A purge job every 10 s drops empty or hostless lobbies. | Same for Standard/Private. A QuickPlay lobby closes when anyone leaves before the match. | Adopt host promotion in quick play (part of the quick play rework). |
| `LeaveLobby {link_lost}`: the flag only matters to ranking. | The flag is ignored. | Nothing to do (no ranking). |
| `KickedFromLobby {reason}` sent for relay-unreachable players and on drain. | Never sent. | Adopt with the connection check and the drain. |

### Player updates and settings

| Real | Ours | Move |
|---|---|---|
| `UpdatePlayer` replaces the sender's Player with what was sent, except addr, peer id, IsHost and Seat. The name can change. CustomVariants keep their old value if the new list is empty. No validation. Afterwards: EndGameChoice is set to `[]` if the lobby is full, a LobbyUpdate goes out, a series game may start. | Picks the Player at the sent Seat among the device's own (local players), keeps Name, validates Team −1..1 and types, refuses an oversized lobby. Quick play: (re)arms the 1.5 s start. | Adopt (no name freeze, empty-CustomVariants rule). Keep the per-seat pick for local players. |
| `UpdateLobbySettings` (host, not in a match, no series running): MaxPlayers clamped to [max(players, 2), 4]; GameData replaced but the seed kept; Mods replaced; **Series cleared**; everyone un-ready. | Same checks, but: team-mode minimum 3; **BestOf can't change after the lobby is created** (`gameData(sent, keep)` keeps `keep.BestOf`); Series not cleared. | Adopt. Our BestOf lock means a host can't switch between a single match and a series. |

### Start and connection check

| Real | Ours | Move |
|---|---|---|
| Connectivity tick every second for every lobby that isn't in a match: each unordered pair has 10 s from when it appears; a PeerMeasured either way settles it. A failed pair goes into `RelayedPairs` if the relay is up; peers that can't reach the relay are removed and kicked. LobbyUpdate when the relayed pairs change, someone is kicked, or IsCheckingConnections flips while everyone is ready. | Check only after StartLobbyChoice: both directions, 8 s, `RelayedPairs` always empty (no UDP relay). Skipped when a client isn't 0.20+ and hasn't sent a PeerMeasured. | Adopt the continuous per-pair check. RelayedPairs stays empty on Workers (TF.EX then tries direct, as now). |
| `StartLobbyChoice`: host only, not in a match, not checking connections. **No all-ready or team check on the server** (the client's AreAllPlayersReady does that). A series lobby needs a 1v1 or a 2v2 team deathmatch. Sends LobbyUpdate only when a series starts, then StartLobby. The seed is **not** changed at start. | Requires all ready, two devices, two real teams in team deathmatch; a series needs Players == MaxPlayers. Always LobbyUpdate then StartLobby. New seed at every start. | Adopt. Check first that the Switch doesn't depend on the LobbyUpdate before StartLobby (it reads MatchPlayers, `SessionPlayers`, from the lobby). |
| MatchPlayers = Players when the match starts, cleared when it ends (same idea as ours). | Same. | Nothing to do. |

### End of match, votes, rematch

| Real | Ours | Move |
|---|---|---|
| `MatchEnded {winner_seat, frame, checksum, scores, stats, tower}`: the first one starts a **30 s** choice window. Results are collected per room until every seated player has reported (stale after 5 min), then compared. Disagreement is logged and aborts a series ("RESULT MISMATCH"). | The first MatchEnded is the result. **33 s** window. `frame`, `checksum`, `stats`, `tower` ignored. | Adopt (wait for all, compare, 30 s). |
| `winner_seat` is a lobby seat (what TF.EX sends). | Read as an index into the players sorted by seat. | **Adopt; bug in ours** (wrong side credited once seats have a gap). |
| Votes: Rematch / ArcherSelect / Continue, kept per player in `EndGameChoice` (`null` until the first start, then `[]`). It resolves when every player has voted, or when the 30 s run out. All voted Rematch (and at least one vote): rematch, otherwise archer select. While draining, never a rematch. | Same outcome rule, but needs more than one player to rematch, and ignores RematchLobbyChoice in a series lobby. | Adopt (one player left can rematch, as the real server lets them). |
| On rematch: everyone un-ready, a new seed, EndGameChoice `[]`, `set_in_game(true)` (MatchPlayers taken again, the match timer restarted). Then LobbyUpdate, then RematchLobby. | New seed and EndGameChoice `[]`, but Ready untouched and MatchPlayers kept. LobbyUpdate, then RematchLobby. | Adopt. |
| On archer select: everyone un-ready, a new seed, not in a match. LobbyUpdate, then ArcherSelectLobby. | Same, but the message goes out before the LobbyUpdate. | Adopt the order (update first). |
| A player leaving mid-match does **not** start or resolve the vote. Only MatchEnded starts the window. | A leave mid-match starts the 33 s window, and resolves it at once if everyone left has voted ("nobody sends MatchEnded"). | Adopt, but check first: the leave rule was added because a match that ends by a disconnect sent no MatchEnded. Confirm what the Switch and the page do on a peer's disconnect (TF.EX's LeaveLobby with link_lost). |

### Series (BestOf > 0)

| Real | Ours | Move |
|---|---|---|
| Only 1v1, or a 2v2 team deathmatch with two full teams ("A series needs a 1v1 or a 2v2 team deathmatch"). | Any free-for-all (each seat a side) or any team split; needs a full lobby. | Adopt. |
| First tower: a random unplayed one. After each game the side that didn't win picks. When a side's seats disagree, the pick is a draw among the distinct towers. | Same, but the draw isn't deduplicated (two votes for one tower weigh double). After a draw (no winner) the same side picks again. | Adopt the dedup. A draw doesn't exist in the real server: see the next row. |
| Abort reasons: `PLAYER LEFT` (any player leaves, series running), `RESULT MISMATCH`, `NO WINNER` (winner_seat < 0), `BAD RESULT` (a seat outside the series), `MISSING RESULT` (vote resolved before any result). After an abort the lobby goes to **archer select** (ArcherSelectLobby). | Only `PLAYER LEFT`. A draw records WinnerSide −1 and the series goes on. After anything: SeriesLobby. | Adopt. |
| Series screen: 60 s, then the missing pick is filled (a draw among picks, else a random unplayed tower) and everyone is set ready. A game starts when everyone is ready, the tower is known and connections aren't being checked. A finished series restarts the same way (1v1 / 2v2 again). | 62 s; starts when ready and the tower is known (no connection check); a finished series restarts only with a full lobby. | Adopt. |
| `Series.ForfeitSide`, `RankedSeats` on the lobby (ranking only). | Absent. | Add the fields with their empty values (the client may read them). |

### Quick play

| Real | Ours | Move |
|---|---|---|
| `EnterQuickPlay {name, is_wide}`. Already in a lobby: `QuickPlayStatus {queued: false, message: "Already in a lobby"}`. Else: seat into the nearest open QuickPlay lobby (same width, not in a match, not full); else pair with the nearest waiting player of the same width; else queue. | Leaves any lobby, then pairs the two oldest waiting devices whose player counts fit (`local_players`). | Adopt. Local-player counts become part of the upstream change (point 5). |
| A new QuickPlay lobby: named "QUICK PLAY", MaxPlayers 4, the waiting player is host at seat 0, `Mods: [Teuria.WiderSet {IsWide: True}]` when wide. | Named after the first player, MaxPlayers = player count, Mods empty, up to 8 when wide. | Adopt. The WiderSet mod in the lobby may matter for the wide crashes in the backlog. |
| A 1 s tick: seats waiting players into open lobbies, auto-starts (2+ players, and full, or everyone ready 5 s with no join/leave and no relay switch in 5 s), pushes `QuickPlayStatus` to everyone waiting. | Starts 1.5 s after all are ready (two devices), status only when the queue changes. | Adopt. |
| `ExitQuickPlay` answers `QuickPlayStatus {queued: false}`. | No answer. | Adopt. |
| `QuickPlayStatus` has no `local_players` field. | Sends `local_players` (our #283 field). | Keep while local players are ours. |

### Spectators, skins

| Real | Ours | Move |
|---|---|---|
| `SpectatorJoined` goes only to the host, only while a match runs. | To every member. | Adopt. |
| `SkinChunk` is typed (`bundle_id`, `custom_archer_id`, `chunk_index`, `chunk_count` ≤ 32, `data` ≤ 400,000) and goes to the whole lobby, **the sender included**. | Any object up to 320 KiB as JSON, to everyone but the sender. | Adopt (limits and echo). |

### Signaling (`/room/<id>`, `/ping_measurement/<id>`)

| Real | Ours | Move |
|---|---|---|
| The peer must be a member of that room in matchmaking (404 otherwise). The socket closes when the client leaves the room in matchmaking. `?next=N` (matchbox's room size) supported. | Any UUID may connect, 16 per room. Takeover rules for a reconnecting peer. No `?next`. | Adopt the membership check. Keep the takeover rule only if a client needs it (the real server has none). |

### TF.EX's crash reports (`POST /report?group=&name=&kind=`)

| Real | Ours | Move |
|---|---|---|
| Saved to `REPORTS_DIR/<group>/<name>_<kind>_<hhmmss>.zip`, up to 32 MB, kept 3 days, folder capped at 750 MB (507 when full), 201 on success, 404 when reports are off. Lobbies say `IsReportEnabled` only when reports are on. Pages refused (Origin). | R2 `match/<date>/...zip`, up to 16 MB, kept 60 days, 30 per address per hour, 204 on success. Pages allowed. IsReportEnabled always true. | Adopt the status codes, the 32 MB limit and `IsReportEnabled`. Keep R2 and the longer retention (we read them days later). |

### Ranked, leaderboard, profile

| Real (no ranking plugged in) | Ours | Move |
|---|---|---|
| `EnterRanked` → `RankedStatus {queued: false, message: <ranked unavailable>}`; `ExitRanked` → `RankedStatus {queued: false}`; `GetLeaderboard` → `Leaderboard` (empty); `GetPlayerProfile` → `PlayerProfile {profile: null}` (with cooldowns of 2 s and 250 ms). | No handlers: these are dropped silently. | Adopt the empty answers, so a newer client's menus don't wait forever. (v0.21.1 doesn't send them yet; later TF.EX does.) |

### Lobby JSON as sent

| Field | Real | Ours |
|---|---|---|
| `JoinCode` | `null` unless Private | `""` |
| `EndGameChoice` | `null` until the first start | always `[]` |
| `RankedSeats`, `Series.ForfeitSide` | present (empty) | absent |
| `MissingVariants` | absent (the client works it out) | sent as `[]` |
| `Player.Name` | as the client sent it | capped at 10 |

The client reads both forms today (it plays on both servers). Matching the real server exactly is the safe end state.

## How to move: two ways

**A. Port the behaviour into our worker** (keep Cloudflare): the tables above, one area at a time. Each step in `test.mjs` first,
then one browser-vs-browser run and one Switch smoke scenario (`lobby`, `quickplay`, `rematch`).
Suggested order (the Switch's flows first):
1. Results and rematch: winner_seat as a seat (the bug), wait for every MatchEnded and compare, 30 s window, the rematch reset,
   update-before-ArcherSelectLobby, no vote on leave (after checking the disconnect path).
2. Start: the continuous per-pair connection check; StartLobbyChoice refused while checking; no server-side ready check; no new seed at
   start; LobbyUpdate before StartLobby only for series (after checking the Switch).
3. Quick play rework (4-seat lobbies, seat into open ones, 5 s grace, host promotion, WiderSet mod, 1 s status, ExitQuickPlay reply,
   in-game quick play visible to spectators).
4. Lobby list/join/settings: list filter, joinable kinds, messages, UpdateLobbySettings (Series cleared, BestOf changeable, 2–4),
   private-join cooldown, SpectatorJoined to the host, skin chunk echo and limits, JSON field forms.
5. Series rules (1v1 / 2v2, abort reasons, archer select after an abort, dedup picks, restart without a full lobby).
6. Version gate, Identify check, the empty ranked/leaderboard/profile answers, signaling membership check, report status codes.
7. Decisions: keep-alive cadence, drain, GeoIP.

**B. Run the real server itself** (the normal way, and the same rule we follow for TF.EX's client code): host
tf-ex-matchmaking-server (Docker, `cargo run`) on a machine with a public UDP port. That brings TF.EX's own relay, GeoIP, drain and
every future server change for free. Then ~1,000 lines of matchmaker.js and signal.js go. Our differences become a small fork
patch set: an Origin allow-list instead of `refuse_browsers` (pages), and local-player seating (#283, the upstream candidate). `/turn`,
the Switch's `/relay/<id>` and the performance reports can stay on the Worker, which already does that well. Cost: a server to
run and pay for, and a migration of the Switch's and the page's server address. The tables above then become the checklist for
what changes for players.

Recommendation: B, if a small always-on host is acceptable. It removes our stand-in instead of making it imitate, and the server
keeps changing upstream (five commits on 2026-10-24/25 alone). Otherwise A in the order above. Either way, fix point 3
(winner_seat) first: it's a plain bug in ours and a small change in `seriesResult` (find the winner by `Seat`, and read
`scores` by seat too: TF.EX's `HUD.SeatScores` lists them by seat).

## Open questions

- Which commit does the deployed worker run? (Read `/status`, or the deploy log.)
- Does anything on the Switch or the page rely on our LobbyUpdate before every StartLobby, or on the vote starting when a player
  leaves mid-match? Both go away in the real server's flow.
- Does the browser's TF.EX send a `steam:` / `local:` player id? (The real server ignores an Identify with any other prefix.)
- Is the WiderSet mod in a wide lobby's `Mods` what makes TF.EX's client turn wide on? If so, our wide quick play lobbies (Mods empty)
  are part of the wide bugs in the backlog.

## Running the real server (option B, started 2026-10-26)

The user chose B. Our fork: `~/Code/Towerfall Switch Mods/tf-ex-matchmaking-server`, branch `switch-online` (local only, not pushed),
on upstream `980e86c`. Four commits on top:

| Commit | What | Why |
|---|---|---|
| `51e2599` | Browser pages may connect: `ALLOWED_ORIGINS` (comma-separated, `*` any), plus `https://belacmu.github.io` and `http://localhost` / `http://127.0.0.1` on any port by default. Other origins still 403. | The browser port runs TF.EX in a page, which always sends an Origin. |
| `c44a64b` | Info lines `[lobby] X created <Kind> lobby R (code C)`, `[lobby] X joined R as player`, `[quickplay] matched A vs B`, `[lobby] R started (A vs B)`. | Test harness only: the same words as local.mjs, which lobby-vs-browser.sh and lobby-auto.sh read. |
| `27df95e` | Local players (Fcornaire/TF.EX#283): `AddLocalPlayer {player}`, `RemoveLocalPlayer {seat}`, UpdatePlayer by seat; a device counts once in the connection check and for match reports; a refused request gets the lobby back. Test `local_players_share_their_device_and_count_once`. | Upstream candidate, with #283. Without it the real server drops those messages, and a device's two players would leave its pair check unsettled forever (the host could never start). |
| `7ae40bd` | `TFEX_FIXED_SEED`: every match gets that seed. | Test harness only (rematch/quit/archer need a level the scripted inputs finish). |

Build: `cargo build --release` in the fork (the rustup toolchain: `~/.rustup/toolchains/stable-aarch64-apple-darwin/bin`). `cargo test`: 46 pass.
Run: `PORT=3000 ./target/release/tf-ex-matchmaking-server` (env table in its README; `RELAY_PORT` is UDP, `METRICS_PORT` keep private).

**Harness:** `LVB_SERVER=real` in front of `tools/smoke.sh` / `tools/lobby-vs-browser.sh` starts the fork's binary on this checkout's
server port instead of local.mjs (`TFEX_REAL_SERVER=<path>` for another binary). LVB_SEED and LVB_REPORTS work the same way.

**What the Switch and the page need from it:** the Switch connects exactly as desktop TF.EX does: `/ws?version=`, `Identify` with a
`local:` id, signaling and ping measurement at `/room/` and `/ping_measurement/` (WebRTC, net.cpp RunRtc). It doesn't use our
`/relay/<room>` any more, so that route is only for the xnet tests. The page also asks the server for `/turn`, which the real server
doesn't have (404). The page then falls back to STUN only (tfnet.js `iceServersFor`), which is fine on one machine but not for players
behind strict NATs. For hosting, either add `/turn` to the fork (Cloudflare TURN credentials, as turn.js does) or point the page's ICE
request at the Worker.

**Checked 2026-10-26 (Ryujinx slot lockstep, hidden; the page from the harness worktree), all against the fork with `LVB_SERVER=real`:**
`lobby` PASS (0/587), `rematch` PASS (checksum clean), `localtwo` PASS (0/586, two local players on the Switch: the fork's #283
patch), `quickplay` PASS (0/296) after one Switch fix: its log kept only the first 40 server messages, and the real server's
once-a-second QuickPlayStatus used them up before QuickPlayMatchFound (now counted per message name, MatchmakingService.cpp).
Nothing else was run (the minimum: the flows where the real server behaves differently from local.mjs).

**Not done yet (needs decisions):** a host for it (a machine with a public TCP port and a UDP port for TF.EX's relay), `/turn` for
pages, `MIN_CLIENT_VERSION`, and switching the Switch's and the page's default server address to it (the page's `TFEX_SERVER`, the
Switch's TF.EX settings default). Upstream: the local-player patch goes with Fcornaire/TF.EX#283 (noted only).
