# TF.EX PRs A and B: more players in wide lobbies

The upstream half of 8-player wide lobbies (docs/PLAN-8-PLAYERS-2026-11-02.md), split so each PR is easy to approve. Not opened:
the author has #312, #314, #316 (PR C) and server #7 to review first. This doc holds the PRs' text, their proof and its state, so
they are ready the day we send them.

| PR | TF.EX (belacmu/TF.EX) | Server (belacmu/tf-ex-matchmaking-server) | What it changes |
|---|---|---|---|
| C | `wide-spawn-centre`: **Fcornaire/TF.EX#316** (open) | none | TF.State's spawn centre on wide maps (any player count) |
| A | `player-limits-v2` (d616ac1, on main 3d98d2e) | `player-limits-v2` (2967701, on main 0ea22a3) | every player limit in one place, all still 4, and a lobby's devices capped at 4: nothing changes for players |
| B | `wide-lobby-seats-v2` (7b12335, on A) | `wide-lobby-seats-v2` (e69f4e1, on A) | `WIDE_MAX_PLAYERS = 8` on each side: one line each |

The first split (2026-11-03: `player-limits` / `wide-lobby-seats`, pushed) carried the panel layout, the roll-call icon, the
`Seats` entry and the server's seat cap in B. v2 moves all of that into A behind `WIDE_MAX_PLAYERS = 4`, so B is the one number. With
WiderSet the game has 8 seats, so v2's A+B is the code the 8-player scenarios passed with (`git diff wide-lobby-seats
wide-lobby-seats-v2`: comments dropped, one `public` made `private`, `Math.Min(WIDE_MAX_PLAYERS, TFGame.Players.Length)`), plus
two fixes the screenshots found (2026-11-04), both in A and inert with its limits:
- **The local players panel with more than 3 players** overlapped the menu's description banner (its extra player and hint columns
  grew into the middle). Now up to 4 players a column; a second column (P5-P8) goes right of P1-P4, the block moving left into the
  wide screen's margin; past 2 guests the QUIT hints are one line (`P2-P7 QUIT`, each device's back button, as the JOIN line shows
  each device's start button). Up to 3 players it draws as main does.
- **A remote seat past 4 in the roll call** fell back to `controls/gamepad`, which in the PC game's atlas is the controls screen's
  big controller diagram (drawn over the roll call). Now such a seat shows no control icon: the game has `controls/xb360/player1-4`
  only (main shows them for seats 1-4 as before).

**The device limit (2026-11-05).** The author kept online play at 4 players because every device connects to every other one,
and more connections are a heavy load. Local players share their device's connection, so A caps a lobby's devices instead:
`MAX_DEVICES = 4` on both sides. The server refuses a 5th device's join (`Lobby … is full`, the client's existing "full" message)
and leaves a lobby with 4 devices out of the lobby list, as it does a full one. Players see it only above 4 players: LOBBY SIZE reads
`6 ARCHERS - MAX 4 DEVICES`, and when the 4th device joins with seats still free, the lobby's update feed says
`4 DEVICES - MORE ARCHERS JOIN ON THESE DEVICES`. Under A's limits a lobby can't pass 4 players, so none of this shows; with B the
links stay where they are today (at most 6, 3 per device) while a lobby holds 8 players.

## Making them easy to approve
- B is one line on each side, after A.
- A changes nothing a player sees, and says exactly where it differs from main: only with WiderSet loaded (`TFGame.Players.Length`
  is 8 there), where loops also visit the empty seats 4-7 and the arrays they fill grow (a replay records 8 team entries, which
  main reads too: it loops `seat < 4` over a longer array).
- Proof for each claim (below), screenshots before/after (docs/SCREENSHOTS.md, tools/pageshot), the server's tests.
- No comments where the code says it; the why goes in the PR text.
- Compatibility and deploy order stated: the server's A/B before (or with) the client's; an old client in a wide lobby sends no
  `Seats`, so it gets 4 seats, as today.
- Commits: one per PR, on current main. The AI checkbox "I wrote the git history myself" needs the user to write the commits.
- Optional, before B: an issue proposing the shape (the author prefers agreeing on a design first).

## Proof plan
| Claim | Proof | State |
|---|---|---|
| A: the game plays the same | TF.EX's own scenarios (its sync tests) through the code A changes, on main and on A, WiderSet loaded (`TFGame.Players.Length 8`): arrows-basic, team-reviver, team-arrow-owner, sudden-death, start-with-shields, start-with-wings, miasma-round-carry, instant-replay, superbomb-replay-restore | **done** (2026-11-04): 9/9 PASS on main and on A (2962577), each scenario ending on the same frame (`tools/pageshot/shot.sh sweep-a player-limits-v2 "scenario <names> 120"`, patch seat-count-log). The whole catalog (82) takes ~22 min a run: these 9 cover A's spawn, team, variant, scoring and replay changes |
| A: lobbies the same | screenshots main vs A (wide, 3 local players): the panel and LOBBY SIZE at its largest (4) | **done**: `docs/img/tfex-pr-ab/a-*.png` (`tools/pageshot/lobby.sh` with NO_JOIN=1 LOBBY_WIDE=1 HOST_GUESTS=2; main on upstream's server, A on server A, each with only the ALLOWED_ORIGINS test commit) |
| A server: same limits | 60 tests pass; new `a_lobby_takes_up_to_max_devices`, `join_needs_room_for_another_device` (2026-11-05); new `a_standard_lobby_has_max_players_seats`, `a_wide_lobby_has_its_seats_up_to_wide_max_players` (4 under A), `a_wide_lobby_without_seats_has_max_players_seats` | **done** (2026-11-04) |
| B: wide lobbies up to 8 | LOBBY SIZE at its largest: 8 in a wide lobby, 4 in a standard one | **done**: `b-wide-size.png`, `b-standard-size.png` |
| B: one device up to 7 | the local players panel with 7 players | **done**: `b-panel-7.png` |
| B: 8 players play | two pages: the host with 7 (6 guests on virtual pads), the other with 1; both roll calls, the match | **done**: `b-lobby-host.png`, `b-lobby-join.png`, `b-match-8.png`; GGRS synchronized, no desync or exception on either page (`lobby.sh` LOBBY_WIDE=1 HOST_GUESTS=6). Not yet: the results screen (the Switch's wide8end showed it on the first split) |
| B server | the same tests: 8 seats in a wide lobby; a 5th device refused with seats free | **done** (60 pass; `join_needs_room_for_another_device` tests the device cap itself under B) |
| B: the device limit shows | LOBBY SIZE at 6 in a wide lobby; the lobby feed when the last device joins | **done** (2026-11-05): `b-size-6-devices.png`; `b-feed-device-limit.png` with a test build whose limit is 2 (patch device-limit-2: two pages, no 4-page run), so it reads `2 DEVICES`; the server's tests prove the real 4 |

## Drafts

### A: `refactor: player limits in one place`

> ## Summary
>
> TF.EX's player limits are spread over literal `4`s and a few `MAX_PLAYERS` constants. This puts them in `Constants`:
> `MAX_PLAYERS` (a lobby's most players), `WIDE_MAX_PLAYERS` (a wide lobby's, also 4), `LobbyMaxPlayers` (the current lobby's) and
> `LobbyMaxLocalPlayers` (one device's: the lobby's minus one, so 3 as today), and `MAX_DEVICES` (4: every device connects to every
> other one, so a lobby takes at most 4 devices; more players than that share a device). Loops and arrays over seats use the game's seats
> (`TFGame.Players.Length`) instead of 4. Every limit stays 4: nothing changes for players.
>
> Where it differs from main: only with WiderSet loaded, where the game has 8 seats. Loops then also visit seats 4-7 (empty, so
> they change nothing) and the arrays they fill have 8 entries (a replay records 8 team entries; main reads those too).
>
> For more players than today, also: the local players panel lays out 4 players a column (a second column to the right, the block
> moving into the wide screen's margin; past 2 guests one `QUIT` line for them all), the roll call shows no control icon for a
> remote seat past the game's 4 (`controls/xb360/player1-4`), and a wide lobby sends its seat count to the server (`Seats` in
> WiderSet's lobby data). With these limits none of that shows; it is there so raising `WIDE_MAX_PLAYERS` is all a change of limit
> needs.
>
> With more than 4 players a lobby says so: LOBBY SIZE reads `6 ARCHERS - MAX 4 DEVICES`, and the lobby's update feed says when the
> last device joined while seats are free. The server refuses a 5th device and leaves such a lobby out of the list, as a full one.
>
> Server: belacmu/tf-ex-matchmaking-server `player-limits-v2` (a lobby's seats in one place, the same limits, at most 4 devices).
>
> ## How it was tested
>
> TF.EX's scenarios through the changed code (spawns, teams, per-player variants, round scoring, replays), with WiderSet loaded so
> the game has 8 seats: arrows-basic, team-reviver, team-arrow-owner, sudden-death, start-with-shields, start-with-wings,
> miasma-round-carry, instant-replay, superbomb-replay-restore. 9/9 pass on main and on this branch, each ending on the same frame.
>
> A wide lobby with 3 local players, main and this branch: the same panel, LOBBY SIZE stops at 4.
>
> | | main | this PR |
> |---|---|---|
> | local players | ![](./a-panel-main.png) | ![](./a-panel-a.png) |
> | LOBBY SIZE (wide) | ![](./a-wide-size-main.png) | ![](./a-wide-size-a.png) |

### B: `feat: wide lobbies up to 8 players`

> ## Summary
>
> WiderSet gives the game 8 seats. This raises `WIDE_MAX_PLAYERS` from 4 to 8: a wide lobby can have up to 8 players and one device
> up to 7. Standard lobbies stay at 4, and a lobby still takes at most 4 devices (A), so no one has more connections than today:
> the extra players are local ones. Needs the server's change (Fcornaire/tf-ex-matchmaking-server#..., the same constant) deployed
> first; a client without this keeps sending 4 seats.
>
> ## How it was tested
>
> Two browser builds of TowerFall with TF.EX and WiderSet (the PC code), one lobby on a local server built from the server change:
> one device with 7 players (6 on gamepads), the other with 1. Both roll calls, the match synchronized, no desync.
>
> ![LOBBY SIZE in a wide lobby: 8](./b-wide-size.png) ![LOBBY SIZE in a standard lobby: still 4](./b-standard-size.png)
> ![LOBBY SIZE 6: at most 4 devices](./b-size-6-devices.png)
> ![The lobby feed when the last device joins (a test build with the limit at 2)](./b-feed-device-limit.png)
> ![7 local players](./b-panel-7.png)
> ![The host's roll call with 8 players](./b-lobby-host.png) ![The other device's roll call](./b-lobby-join.png)
> ![An 8-player match](./b-match-8.png)
