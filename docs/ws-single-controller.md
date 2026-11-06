# Single-controller online play (Switch)

Goal: one physical controller (the local player's); every remote player is driven by GGRS, no extra controllers connected.
TF.EX does this on PC by replacing the input objects; here we feed the game's `InputState` instead.

## Findings
- The lobby start already forces `Players = 110000` and the local seat reads the first local controller (`NpPadInput`,
  rollcall `XGamePadInputPatch`), so no second controller is needed to *start* a match.
- **The gap:** `Player.Update` (+0xaef280) does `if (PlayerInputs[i] != null) input = PlayerInputs[i].GetState()` (`cbz x8` at
  +0xaef900 jumps to +0xaef940). `InputStoreHook` (+0xaef934) lives on the non-null path, so with one controller the remote
  seat (no PlayerInput object) never received its GGRS input: it stood still.
- **Fix:** `InputJoinHook` at +0xaef940 (where both paths rejoin; `ldr s0,[x19,#0x154]`, insn 0xbd415660). When the seat's
  `PlayerInputs` slot is null (or out of range) and a netplay match runs, it writes the seat's GGRS input (shared helper
  `NpSeatInputBytes`) into `Player.input` (+0x148, 28 bytes). Logs `[vs] seat N has no controller object` once per match.

## Result (Ryujinx, 2026-10-16)
Ryujinx reports every pad connected, so all six `PlayerInputs` slots are filled (mask 3f) and the null path never ran. New debug
switch `onepad 1` (hw.txt) empties `PlayerInputs[1..5]` at the lobby match start (mask 01 = one controller). Run (`tools/lobby-auto.sh`
`lobbyui 1, lobbyscript 0, padtest 1, onepad 1, lobbypred 0, lobbylength 0`): `seat 1 has no controller object` logged, **0 differing
ticks of 1499** against the browser. The same run without `onepad`: 0 differing of 2444. Baseline (`nojoinhook 1` = the hook is a no-op, `onepad 1`, vsstop 600):
**334 differing ticks of 599 from T 1**: the browser's p1 moves, the Switch's p1 stands at its spawn with speed 0. So the fix is what makes it work. Use `vsstop` below the match length (~600), else the run idles to its timeout.

## Status (before that run)
- Builds (`tools/build-probe.sh`). NOT yet run: needs a Ryujinx/hardware run with only one controller connected (Ryujinx's idle
  second controller makes slot 1 non-null, so test with it disconnected, or on the console with one Joy-Con/Pro Controller).
- Open: 78 other readers of `TFGame.PlayerInputs` (rumble, aim, pause, results) are not audited for null slots; the 2-seat limit
  (`seat > 1`) is unchanged, so 3-4 player lobbies need the GGRS side widened first.

## Long-term goal: any mix of local and online players (2-4 seats)
**Not to be implemented yet:** the server and the browser mode don't support it, so this is only a roadmap. Current scope is the
single-controller fix above (2-player lobbies, one local seat, one remote seat).

Model: a seat is *local* (a real controller drives it) or *remote* (GGRS drives it). The game only ever sees a 28-byte InputState per
seat, so the target design is "every seat's input comes from one array `g_npIn[seat]`", filled per frame from: the seat's own
controller (local seats) or GGRS (remote seats). Steps:
1. **Seat table**: `seat -> {local controller slot | remote handle}` built from the lobby (Players by Seat; local = ours) plus the
   locally joined rollcall seats. A local-only match is the degenerate case (no relay).
2. **Local couch seats in a lobby**: each extra local player joins the lobby as its own `Player` (own seat, same RoomPeerId as us,
   or the server's per-peer multi-seat if it allows it; check LOBBY-PROTOCOL.md / the tfex-server first) and its seat reads its own
   `PlayerInputs[slot]` (`NpPadInput(seat)` already takes a slot index; today it falls back to the first controller).
3. **Widen the 2-seat limits**: `g_npIn[2]`, `seat > 1` checks, `TfxInput` hist buffers (`g_inHist`), `for (i < 2) pl[i]`,
   `ch[0..1]`, scripts `vs0/vs1`, GGRS session player count (`tfx_session_new`), rollback input history, `MaxPlayers` 2..4 and
   `CompactSeatsToHandles` (handle = rank of seat). GGRS local players need `add_local_player` per local seat (several local
   handles per peer; GGRS supports it, ggrs-switch's wrapper does not expose it yet).
4. **Gate**: local seats' input fed to GGRS via the seat's own controller; the `InputJoinHook` path then stays only for seats
   with no controller object (remote), and local seats go through `InputStoreHook` as vanilla.
5. Audit the 78 `PlayerInputs` readers for null slots and rumble/aim per seat.
Order: (3) widen with 2 remote seats still working -> (1)/(4) seat table -> (2) couch seats in lobbies -> 3-4 player browser parity.

## Joiner with one controller (browser hosts, Switch is seat 1 / player two)
Run (2026-10-16, `MANUAL=1 tools/lobby-vs-browser.sh`, page driven by hand: C, Right, C = LOBBIES, Enter = create, Down+Right = Sacred
Ground, Enter, wait until RYUJINX shows in seat 2, host C = ready, host C again on the "FIGHT!" banner; hw.txt `lobbyhost 0, lobbyui 1,
lobbyjoin 1, lobbyscript 0, padtest 1, onepad 1, npdelay 2, lobbypred 0, vsstop 600`): handle 1, `PlayerInputs` mask 01, seat 1 logged
`no controller object` (the Switch's own seat goes through `InputJoinHook` too), **0 differing ticks of 599**. The host must ready only
after the Switch's seat is shown ready, else the "FIGHT!" start banner doesn't come up (the first attempt waited forever).
