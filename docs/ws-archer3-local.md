# Mixed lobby: a second LOCAL player at archer select (branch `claude/ws-archer3-local-cd75f5`)

**Scope (updated by the user):** Switch side first, local only; the network side is only noted (see "What the network side needs"), and
TF.EX compatibility must not change: default (2-player) lobbies behave exactly as before.

Original scope: archer select only. The Switch hosts a lobby, an online player joins (seat 1), then controller 2 on the Switch joins as a
local player (seat 2) with its own archer, visible to the online player. No match start, no GGRS change (starting a lobby with
more than 2 players is refused on the Switch for now: `CanStart`).

## Answers to the open questions

**(a) One connection, two seats? No: the extra local player is a separate connection and peer; this works with the server as it is.**
tfex-server (`cloudflare/tfex-server/matchmaker.js`) keys everything on `conn.state.peer` (one RoomPeerId per websocket):
`playerOf`, `UpdatePlayer` (`findIndex(p => p.RoomPeerId === conn.state.peer)`), `leave`, `join` (`this.leave(conn)` first: a second
JoinLobby on the same connection *moves* the connection to a new member, it doesn't add one). Seats are server-assigned
(`freeSeat`: lowest free below `MaxPlayers`). So a lobby member = a websocket. A second websocket from the Switch that does
`Identify` + `JoinLobby(is_player)` gets seat 2 and its own RoomPeerId, like any other joiner; nothing in the server needs to change
for the lobby/archer-select part. (Identify's `player_id` is only stored; it is not checked for duplicates.)
What a *match* would still need (not done, only described): the relay side sees two peers from one machine (`/relay/<room>?peer=<uuid>`
per peer; the Switch would need one relay socket per seat or a multiplexed one), `StartLobby`/GGRS handles for 3 seats, and the
browser mode's own 3-4 seat handling. A cleaner long-term server option is a "local seats" message (`AddLocalPlayer` on the existing
connection creating extra members owned by that connection), but the second-connection approach needs none of it.

**(b) Roll call when controller 2 appears.** The roll call has four `RollcallElement`s (seat = `playerIndex`), and the mod already
drives them from the lobby (foreign seats via `JoinRemote`/`LeaveRemote`, the host seat via the vanilla element with the first local
controller). `NoGamepadUpdates` is set while a lobby is held, so vanilla discovery never joins/un-joins anything when a pad
connects: the element for a seat only acts on the `PlayerInput` object in its `input` field. Design used:
- Controller 2 = the second non-null `TFGame.PlayerInputs` slot (`GuestPad()`; the first is the host seat's).
- Its A in the lobby screen (`GuestUpdate`, every frame in the Rollcall state) claims a seat: second connection, `JoinLobby`.
- The element of the seat the server assigns (found by the guest's peer id, `GuestSeat`) then runs the vanilla `NotJoinedUpdate`
  with `input` = controller 2: left/right picks the archer, A joins; the mod publishes `UpdatePlayer(Ready, archer, alt)` on the
  guest's connection (`PublishGuest`) exactly as it does for seat 0 on the main connection. B on the unjoined guest seat leaves the
  lobby (guest connection closes, seat freed). Joined seat: the vanilla `JoinedUpdate` with controller 2, except Confirm/Start
  (only the host's seat starts), and an archer change is published.
- Ryujinx reports every pad as connected, so "connect" can't be simulated; pressing A on pad 2 is the trigger instead (also what a
  Joy-Con user does). hw.txt `lobbypad2 1` injects pad 2 (npad id 1): Right (another archer), A, once the lobby has a second player.

**(c) MaxPlayers.** The Switch's `CreateLobby` hardcoded 2. New hw.txt `lobbymax <2..4>` (default 2, so v1 behaviour is unchanged).
The server accepts 2..4 (`MAX_PLAYERS = 4`). Note the Switch's own *joiner* check (`WhyNotJoinable`) still wants 2-player lobbies.

## Code
- `tfex/LocalGuest.cpp/.hpp`: the second lobby client (own net thread, own stack; Identify, JoinLobby, UpdatePlayer, LeaveLobby;
  the lobby itself is read from the main connection). Name is `<name>2`.
- `tfex/MainMenuPatch.cpp`: `GuestPad/GuestSeat/PublishGuest/GuestUpdate`, guest branches in `NotJoinedUpdateHook` / `JoinedUpdateHook`,
  `CanStart` (no start with >2 players), `lobbymax`, `lobbypad2`, `lobbyui 2` (the debug driver stops before starting).
- `xplay.cpp`: hw.txt keys, pad 2 injection in the npad hook.
- `tools/archer3-test.sh` + `tools/lobby-observer.mjs`: Ryujinx hosts (lobbyui 2, lobbymax 3, lobbypad2 1) on the local tfex-server; the
  observer is a Node websocket client standing in for the browser page (joins as seat 1, ready, archer 1, prints every roster
  change, never starts). The real browser page can be used instead with `MANUAL=1 tools/lobby-vs-browser.sh` (same hw.txt lines
  plus `lobbyhost 0`); it was not needed for this step.

## Modes
- **Local (default, `lobbyguestnet 0`)**: controller 2's A claims the lowest free roll-call seat (not ours, not held by a lobby member,
  below `MaxPlayers`). Only in lobbies with `MaxPlayers > 2` (so v1 2-player lobbies are untouched). No server involvement. A start with a
  local second seat is refused (`CanStart`).
- **Network (`lobbyguestnet 1`)**: the same, but the guest also joins the lobby on its own connection (LocalGuest); answers (a)-(c) above.

## Roll call findings (Ryujinx)
- The archer is picked on the **unjoined** seat (Right/Left cycle `TFGame.Characters_`), A joins; on a joined seat `JoinedUpdate` does not
  cycle archers (Right had no effect). So the mod publishes at join time only (as the host seat does).
- Elements take input from their `input` field; pointing seat 2's element at controller 2's `PlayerInput` (second non-null
  `TFGame.PlayerInputs` slot) is enough. `NoGamepadUpdates` (set while a lobby is held) keeps vanilla discovery from touching seats.
- `TFGame.Players` becomes `1110` after the guest joins (seat 0 host, seat 1 online player shown from the lobby, seat 2 local).
- Ryujinx reports all pads connected, so "controller 2 connects" is modelled as controller 2's first A (also what a Joy-Con user does);
  hw.txt `lobbypad2 1` injects Right then A on npad id 1.

## What the network side needs later (not implemented)
- Lobby: a member per local seat. Today the server has one peer per websocket, so either one connection per local seat (works now; used by
  `lobbyguestnet 1`) or a server message that adds extra members owned by one connection (cleaner, needs tfex-server + TF.EX client
  agreement). Seats are server-assigned (`freeSeat`), so a local seat chosen before the server answers can collide with a later online
  joiner: the local mode must reserve seats through the lobby (the guest must join first, or the host picks seat numbers).
- Match: GGRS with >2 handles, per-seat local/remote table, relay sockets per peer, `StartLobby` for 3-4 seats, browser parity
  (see docs/ws-single-controller.md roadmap).

## Result
Ryujinx, `tools/archer3-test.sh` (2026-10-16), all at archer select, no match started:
- Local mode, online player on seat 1 (`lobbymax 3`): controller 2 A -> seat 2, Right -> archer 3, A -> joined; `Players 1110`.
- Local mode, no online player (`OBSERVER=0`): controller 2 takes seat 1, archer 2; `Players 1100`; "can start 0" (start refused).
- Network mode (`GUESTNET=1`): the observer's roster showed seat 2 `RYUJINX2` appear and then READY with archer 3; `Players 1110`, 3 lobby members.
Not covered: a real second Joy-Con connecting (Ryujinx can't); an online player joining *after* the local seat took seat 1 (seat collision,
see above); starting a match with 3 seats; the real browser page (a Node client stood in).
