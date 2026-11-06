# Several players on one device in online play (research, 2026-10-17)

Read-only research for the coordinator. Nothing was built or run. Ryujinx was not used.

**Question (the user):** we want browser ↔ Switch online play with several players on the same device, for example two players on
the Switch and one in the browser, or the other way round. Everything is being built on top of TF.EX. How could we best do this, what
would it take, and what would the consequences be?

**Short answer.**
- **TF.EX has no support for this.** Every layer of TF.EX v0.21.0 assumes one local player per peer: lobby model, protocol, FFI,
  input capture and roll call. Upstream has no open work on it. Issue #182, "Can't play with multiple local players", was closed in
  2026-09 with no linked change, and the code still has a single local seat.
- **GGRS supports it fully.** Its own example is "2 local + 1 remote". The wire format puts all of one peer's local inputs into one
  message.
- **One existing piece of TF.EX already almost works.** `ggrs-ffi` builds its GGRS players from a per-seat list of RoomPeerIds and
  groups seats with the same peer id into one endpoint. If two seats share a RoomPeerId in the lobby, the *other* peers, including an
  unmodified PC TF.EX peer, would set the session up correctly at the GGRS level. Only the device with the two local players needs new
  code.
- **Recommendation: option (c).** Build a thin extension layer, with the same design on the browser and the Switch, around one
  contract: *extra local players are extra `Player` records that share the device's `RoomPeerId`*. Make that contract the one we offer
  upstream. Before the translation work goes further, decide on a "set of local seats" instead of "the local seat" (section 5).
- **The current second-connection prototype is a dead end for matches** (`LocalGuest`, docs/ws-archer3-local.md). It is fine for
  archer select, but no GGRS layout can use it (section 3.4).

Sources: `vendor/tfex` (TF.EX v0.21.0); the browser repo at `~/Code/Towerfall Browser` (main, plus `switch-rtc` where noted). Its
`vendor/netplay-src/ggrs` is GGRS 0.13.0 from the Fcornaire fork, and its `vendor/netplay-src/ggrs-ffi` is
github.com/Fcornaire/ggrs-ffi, the same native library PC TF.EX ships. Also this repo's Switch code, `ggrs-switch/` (which builds
against the browser's GGRS checkout, `ggrs-switch/Cargo.toml:11`), and the web sources listed at the end. Abbreviations:
`NET` = `vendor/tfex/src/network`, `P` = `hw-probe/exlaunch/source/program`, `BR` = `~/Code/Towerfall Browser`,
`FFI` = `BR/vendor/netplay-src/ggrs-ffi/core/src`.

---

## 1. TF.EX: one local player per peer, everywhere

Nothing in TF.EX's code or docs mentions couch play, split screen or two players on one PC. The README roadmap item
"Support 4 players (FFA and 2V2 teams)" means four peers. The only related TODO is `PlayerDraw` "migrate to seat ?"
(`NET/TF.EX.Domain/Models/PlayerDraw.cs:3-9`).

| Layer | What TF.EX does | Evidence |
|---|---|---|
| Lobby model | `Player` has one `RoomPeerId` and one `Seat`. It has no local index, no `IsLocal` and no controller slot. `SessionPlayers` = `MatchPlayers` if set, else `Players`. | `NET/TF.EX.Domain/Models/WebSocket/Lobby.cs:45,163-213` |
| Matchmaking | There is one `peerId` per websocket. `GetLocalSeat()` returns the seat of the first Player with that peer id. `UpdatePlayer` and `PublishInputDelay` always send `Players.First(RoomPeerId == peerId)`. `ReconcileRollcall` treats every Player of another peer as remote. CreateLobby adds exactly one Player. | `NET/.../MatchmakingService.cs:39,1660,1794-1799,1818,1351-1353`; `NET/TF.EX.Patchs/Scene/MainMenu.cs:546-554` |
| GGRS config | `NetplayConfig { NumPlayers, Players (peer id per seat, by Seat), LocalPeerId (one string) }`. Handle = rank of Seat (`CompactSeatsToHandles`). | `NET/TF.EX.Domain/Models/GGRSConfig.cs:69-78`; `NET/.../NetplayManager.cs:1138-1161`; `ArcherService.cs:33-46` |
| FFI | `netplay_advance_frame(Input)` takes **one** input and `netplay_local_player_handle()` returns **one** int. Only the sync test takes `Input[]`. | `NET/TF.EX.Domain/Externals/GGRSFFI.cs:33,36,72` |
| Native lib (ggrs-ffi) | `local_player_handle: Option<usize>`. The local seat is the *first* seat whose id equals `local_peer_id`. Every other seat becomes `Remote(Address::Peer(id))`, **grouped by peer id** by GGRS. `advance_frame` adds one local input. The library is a process-wide singleton. | `FFI/neplay.rs:42,470-478,506-531,693-697`; `FFI/lib.rs:25` |
| Input capture | `ObserveLocalDevice` keeps one device. `DisableAllControllerExceptLocal` nulls every `PlayerInputs` slot except `localSeat`. Remote seats get placeholder `KeyboardInput`/`FakeController` objects. The `GetState` postfixes poll only `seat == GetLocalPlayerInputIndex()`. There is one `PolledInput` and one `_localPlayerIndex`. | `NET/.../TF/InputService.cs:28-40,71-140,234-256,294-329`; `NET/TF.EX.Patchs/PlayerInput/XGamePadInput.cs:100-114,191-194`; `KeyboardInput.cs:123-137,225-230`; `NET/TF.EX.Domain/Context/GameContext.cs:40-45,96-115`; `NET/TF.EX.Patchs/Engine/TFGame.cs:614-618` |
| Input format | `Input` is a 64-byte `#[repr(C)]` struct: 11 ints, 2 `Vector2f` and `disconnected`. The size is fixed per player. | `NET/TF.EX.Domain/Models/Input.cs:10-55`; `FFI/model/input.rs:4-21` |
| Roll call / UI | `IsForeignSeat` zeroes the menu buttons of every device not on the local seat. `AssignInputs` is blocked in a netplay lobby. A non-local occupied seat is network-driven (`NotJoinedUpdate` returns 0, `JoinedUpdate` is forced to state 1). Only the local seat sends `UpdatePlayer`, handles team selection, leaves the lobby and opens the lobby editor. | `NET/TF.EX.Patchs/Entity/MenuItem/RollCallElement.cs:89-117,136-165,207-217,250-326,349-352,466-513`; `NET/TF.EX.Patchs/PlayerInput/PlayerInput.cs:12-29`; `MainMenu.cs:247-249,349-353` |
| Spectators | Spectators are a list of peer ids, plus `netplay_add_spectator(peerId)` for late joiners. A spectator is a whole peer. | `NetplayManager.cs:1117-1136` |
| Disconnect | A GGRS `Disconnected` event ends an unfinished team match ("A PLAYER LEFT"). The per-input `disconnected` flag kills the archer of that handle. | `NetplayManager.cs:420-448`; `NET/TF.EX.Patchs/Entity/LevelEntity/Player.cs:41-45` |
| Desync | The native checksum on the TF.State blob plus a "DESYNC DETECTED" compare. Checksums cover the whole state and know nothing about peers or players. | `NetplayManager.cs:450-454,510-556,668-682` |
| Replays | Inputs are stored for every seat (fine). The metadata holds one `LocalSeat`, and the standalone recorder hard-codes seat 0. | `NET/.../ReplayService.cs:93-95,122,185-189`; `vendor/tfex/src/replay/TF.Replay.Domain/Models/Replay.cs:24` |

**What already works per seat (up to 4, or 8 with WiderSet):** the advanced inputs from GGRS (`GameContext.CurrentInputs`,
`MAX_PLAYERS = 4`); archer, team and series data; per-seat ping stats (`netplay_remote_player_handle_count/_at`); the replay input
format; the `disconnected` kill.

**Upstream status.** Issue Fcornaire/TF.EX#182 "Can't play with multiple local players" (opened 2024-08). The owner said it was
"probably related to #159". It was closed as completed on 2026-09-09 with no linked commit, and v0.21.0 still has a single local seat.
Most likely it was closed for offline local play breaking while the mod is on (the #159 "Coop Freeze" thread), not for online couch
play. There is no roadmap item, and GitHub discussions are disabled. The Discord was not searched. So there is nothing to wait for,
and nothing to port.

## 2. GGRS, ggrs-ffi and ggrs-switch

**GGRS (0.13.0, the Fcornaire fork, used by both the browser and ggrs-switch):**
- Several `PlayerType::Local` handles per `P2PSession` are supported, with no limit. The upstream example's README describes a
  "three-player game with two players playing in the same client", and the remote side lists the same address twice. Remote handles
  are grouped per address into one endpoint (`BR/vendor/netplay-src/ggrs/src/sessions/builder.rs:97-116,375-395`).
- `add_local_input(handle)` works for any local handle. `advance_frame` refuses to run until every local handle has input
  (`p2p_session.rs:272-292,379-388`).
- **Wire:** one input message per frame per remote endpoint, carrying every local handle's input in ascending handle order,
  delta-encoded against the last acked input. The receiver splits the buffer evenly by its handle count for that endpoint
  (`network/protocol.rs:96-122`). Both sides must therefore agree on how many handles sit behind each address. That is the
  compatibility rule everything below depends on.
- Fixes for several local players: 0.9.1 "fixed multiple local players"; 0.13.0 "Input delay changes now work when several local
  players share one endpoint" and added per-handle `set_input_delay`. The local player set is fixed at session start (no hot-join;
  upstream gschup/ggrs#94 is open about that).

**Cost scaling:**
- Rollbacks come only from mispredicted *remote* inputs. A second local player adds no rollbacks and only 64 bytes per message
  before delta compression.
- A rollback costs depth × (one game update + save/restore) and does not depend on the number of inputs.
- More archers make the update a little heavier: more entities, more pages written per tick.
- Fewer *remote endpoints* means fewer mispredictions. A 3-player match on 2 devices rolls back less than the same match on 3 devices.

**On the console this matters for frame cost, not input count.**
- The console is already over budget with one remote player: EndTick p50 13.4 ms / p99 about 21 ms, about 35 ms per projected
  rollback, frame period p50 17 ms (docs/ws-perf.md:159-167). Ryujinx runs about 4.8× faster than the console.
- 4-player wide TDM ran at about 3-7 ticks/s in Ryujinx (ROLLBACK.md:184).
- Couch play adds archers without adding remote peers, so it costs the console about as much as a normal 3-4 player match. It is not a
  new cost class, but it does need the same perf work (`rbwrite`/`rbdemote`, docs/ws-perf.md:174-179) that 3-4 player matches need.

**ggrs-ffi (TF.EX's native lib, used by the browser and PC):** one local handle, `advance_frame(Input)` (`FFI/ffi.rs:123,258`).
It needs:
- `local_player_handles: Vec<usize>` holding every seat whose id equals `local_peer_id`;
- own seats excluded from `required` (today a second own seat lands in `required` and `wait_for_players` times out,
  `FFI/neplay.rs:506-514`);
- a new export, for example `netplay_advance_frame_multi(const Input*, len)` or `netplay_add_local_input(handle, Input)`, plus
  `netplay_local_player_handle_count/_at`.

The remote side needs nothing: a list `[A, A, B]` already gives peer B one endpoint `A` with two handles (`FFI/neplay.rs:519-531`).
`disconnect_all` only disconnects the first remote (`FFI/session.rs:136-145`). That bug matters for 3-4 player matches in general.

**ggrs-switch (`ggrs-switch/src/lib.rs`):**
- Several local handles already work: `kinds[]` can hold several 0s, and `tfx_add_local_input(seat)` takes any seat (`:117-170,236`).
  docs/ws-single-controller.md:43-44 says otherwise, and that is out of date.
- **The blocker is addressing.** `type Address = u32; // the peer index (the seat of the remote player)` (`:56`), and every remote
  seat gets `Remote(seat)` (`:165`). Two remote seats on one device would become two endpoints, each expecting one input per message,
  while that device sends two. That breaks the GGRS rule above. The address has to become a *peer* index.
- `Disconnected` drops the address (`:197`, `kind: 3, a: 0`), so C++ can't tell which device (and which seats) left.
- `TfxRequest` holds at most 4 inputs (`:307-308,360`), which is fine for 2-4 players. There is no spectator API.

## 3. What each layer would need

### 3.1 Lobby server (tfex-server, `BR/cloudflare/tfex-server/matchmaker.js`)
**Today:**
- One websocket = one member = one seat. `attach` sets one `conn.state.peer` (`:179-183`), and `join` makes one member and one seat
  (`:327-335`).
- `playerOf`/`isHost` find the one Player with that peer (`:171-177`).
- `UpdatePlayer` targets `findIndex(p => p.RoomPeerId === conn.state.peer)` (`:687`).
- `leave`/`removeMember` filter by peer (`:203-231`). That already removes *every* seat of a departing device, which is what we want.
- Votes are keyed by peer (`:363-386`), which is fine (one vote per device). Series are keyed by Seat (`:412-432`), which is fine.
- `StartLobby` is `{}` and nothing sends `MatchPlayers` (`:347-361`).
- Quick play pairs two connections, one seat each (`:559-590`).
- `signal.js` refuses a duplicate peer id on a second socket (`:53-66`). That is fine, because a device with several seats still has
  one signaling socket.

**Needed (additive, so old clients are unaffected):**
- `AddLocalPlayer {name}` → a new `Player` with the connection's `RoomPeerId` and a `freeSeat()` seat, and `RemoveLocalPlayer {seat}`.
- `UpdatePlayer` gains an optional `seat`. Without it the server keeps today's behaviour: the connection's first Player.
- `joinRefusal` / "LOBBY FULL" counts seats. `MaxPlayers` stays at most 4.
- Quick play needs a per-connection seat count; leave this for later.
- Host checks stay per peer.

Seats remain server-assigned. This also fixes the seat collision noted in docs/ws-archer3-local.md:69-71.

### 3.2 Lobby protocol (LOBBY-PROTOCOL.md)
- Document the rule *"a RoomPeerId may own several Players; seats stay unique; handle = rank of Seat"*, plus the two new messages and
  `UpdatePlayer.seat`.
- LOBBY-PROTOCOL.md:245 ("handle = index of own RoomPeerId") becomes "handles = ranks of every seat with own RoomPeerId".
- **PC TF.EX compatibility:** a PC peer that only sees such a lobby should cope:
  - its `GetLocalSeat` uses `FirstOrDefault` on *its own* id;
  - foreign seats are network-driven per seat;
  - ggrs-ffi groups the shared id into one endpoint.
  
  This is **inferred from the code, not tested**. The risky spots are anything on the PC side that looks a Player up by RoomPeerId
  and expects one result (`ReconcileRollcall`, `PeerMeasured`, the ping labels). This only matters where PC TF.EX shares our lobby
  server at all.

### 3.3 Roll call / lobby UI (TF.EX-style, both sides)
- A second local device pressing A on a free seat (one that is not ours, not held by a member, and below `MaxPlayers`) →
  `AddLocalPlayer`. The server's `LobbyUpdate` gives the seat, and the element for that seat then runs vanilla `NotJoinedUpdate`/
  `JoinedUpdate` with *that* device's input. The Switch's `GuestUpdate`/`GuestSeat` code (`P/tfex/MainMenuPatch.cpp:497-566,640-652,
  878-893`) already does the UI half. Only its transport changes, from a second connection to `AddLocalPlayer`.
- **TF.EX logic to widen** (browser via Harmony, Switch in the port):
  - `IsForeignSeat`: foreign = not one of *our* seats;
  - `EnterJoined`/`LeaveJoined`/`HandleTeamSelection` publish the Player *of that seat*;
  - leaving on a guest seat sends `RemoveLocalPlayer`, not `LeaveLobby`;
  - only the host's main seat can Start or open the lobby editor;
  - ping labels are hidden for all own seats (`P/tfex/RollcallPings.cpp:28` hides only ours);
  - the guest's name, for example `<name> 2`.
- **Team selection** is per seat (up/down), the same as for the main seat.
- `CanStart` on the Switch refuses any start with more than 2 players or a guest (`P/tfex/MainMenuPatch.cpp:535`). That has to go
  anyway for 3-4 player Switch hosting.
- Small existing bug: `MainMenuPatch.cpp:651` is `g_guestNet && ch != … || alt != …`, so precedence publishes an alt change in local
  mode.

### 3.4 Why the second-connection prototype can't carry a match (option d)
`LocalGuest` gives couch player 2 its own websocket and RoomPeerId (`P/tfex/LocalGuest.hpp:1-4`). Every other peer then treats that
id as a separate endpoint, expecting its own matchbox connection, its own GGRS sync handshake and one input per message.
- **The Switch can't provide this.** It has one signaling identity (`P/net.cpp:1510`) and one GGRS session. A single session with two
  local handles sends both inputs in one message from one address.
- **What would work:** making the console impersonate two full GGRS peers. That means two protocol endpoints driving one simulation,
  which is not something GGRS supports. Running two sessions means two rollback timelines for one game.
- **The browser can't do it at all:** ggrs-ffi is a singleton (`FFI/lib.rs:25`).

So keep LocalGuest for archer select only and replace its transport. Don't build a match on it.

### 3.5 Input mapping on the Switch
**Today:**
- A local seat's pad is "the `PlayerInputs` slot whose index is the seat, else the first pad" (`P/xplay.cpp:2406-2407`).
- `FillControllers` fills empty seats with the *first* pad (`:2249-2251`).
- The guest is "the second non-null slot" (`MainMenuPatch.cpp:497-501`).

**Needed:**
- An explicit **seat → pad table**, built when a seat is claimed in the lobby (seat, `PlayerInput` object, npad id) and used by
  `NpPadInput`, `FillControllers` (never hand a claimed pad to another seat) and rumble.
- Joy-Con pairs or singles: today Ryujinx reports all pads as connected (docs/ws-archer3-local.md:62-64), so a real second Joy-Con
  joining has to be tried on the console. The Switch's controller-applet (pair Joy-Cons) is not wired up.
- **Rumble** (`P/rumble.cpp`) is built around one `g_pad`/`g_seat`. It drops every other object's rumble and stop (`:169-173,206`) and
  keeps only one pad's JoyConData out of rollback restores (`:211-233`). With two local pads the second gets no rumble, and its
  JoyConData (rumble timer, button state) is rolled back, which risks a stuck motor or stale buttons. It needs a set of local pads in
  both places.
- docs/ws-single-controller.md:26,47: the 78 unaudited `PlayerInputs` readers matter more once two slots are real pads.

### 3.6 Input in the browser
- SDL3/FNA handles gamepads (`BR/web/BrowserHost.cs:66-68`, `FNA_GAMEPAD_NUM_GAMEPADS=8`). Vanilla `AssignInputs` already gives
  keyboard + pad, or two pads, as separate players offline (`BR/reference/decompile/TowerFall/PlayerInput.cs:116-158`).
- The touch pad postfix removes the keyboard while touch controls show (`BR/web/TouchGamepad.cs:16-22`).
- Online, TF.EX's `DisableAllControllerExceptLocal` and the `IsForeignSeat` gates throw the second device away (section 1). Needed: a
  Harmony layer in `BR/web/Netplay/` (alongside `SwitchLobby.cs`) that keeps a seat → device map, polls N inputs and calls the new
  multi-input FFI.
- `tfnet.js` needs **no change**: one socket per tab, one data channel per remote peer, and GGRS puts all of a peer's seats in one
  packet (`BR/netplay/tfnet.js:156-183,464-505`).

### 3.7 GGRS session setup
- **Browser:** the ggrs-ffi patch from section 2. The config needs nothing new: `Players` already lists the shared peer id once per
  seat (`NetplayManager.cs:1144-1148`), and `LocalPeerId` selects every matching seat.
- **Switch:**
  - `LobbyMatch.handle` (`P/lobbymatch.hpp:4`) becomes a local-seat mask; `NetplayManager.cpp:384-389` collects every seat with our
    RoomPeerId.
  - `NpStart` sets `kinds[i] = 0` for every own seat (`P/xplay.cpp:2448`).
  - `g_npSeat` (`:289`) becomes a mask, and `tfx_add_local_input` loops over it (`:2895-2896`, `:3096-3097`).
  - ggrs-switch: remote `Address` = peer index (section 2).
  - net.cpp maps peer ↔ peer index instead of seat: `g_seatPeer/SeatOf/g_ownSeat/want` (`P/net.cpp:744-766,796-797,845,1469-1488`).
    Today `SeatOf` returns the first matching seat, and `want` would wait forever for our own second seat.
- **Relay:** `np_set_remote_seat(1 - g_npSeat)` (`xplay.cpp:2449`) and relay.js `MAX_RELAY_PEERS = 2` are per *peer*. A "2 Switch +
  1 browser" match is a 2-peer match, so the 2-peer relay works as soon as its checks count peers instead of players.

### 3.8 Spectators
- They are unaffected: spectators receive every handle's input from the host's session. They are whole peers, and couch seats are
  players.
- The Switch has no spectator API (ggrs-switch); that limitation is unrelated to couch play.
- A device with couch players can't be "half spectator". `IsSpectator` is per peer, which is correct.

### 3.9 Disconnect handling
- One device leaving drops all its seats at once. That is correct at every layer once addresses are per peer: GGRS disconnects the
  endpoint's handles, `disconnected` kills each of those archers (`Player.cs:41-45`, `P/tfex/PlayerPatch.cpp:29-35`), and the server
  removes the members by peer (`matchmaker.js:203-231`).
- **Switch fixes:**
  - `g_remotesGone >= g_npN - 1` (`P/xplay.cpp:3030`) counts Disconnected events (one per *endpoint*) against players − 1. It should
    compare against remote *peers*, or check "no remote seat left".
  - Telemetry `tfx_ping_ms(…, g_npSeat == 0 ? 1 : 0)` (`:3321`) assumes 2 players.
  - ggrs-switch's `Disconnected` should carry the address.
- **Rules question** (decide later): in FFA, the couch pair leaving can leave a 1-player "match". That is the same as today's "every
  remote gone" rule.

### 3.10 Digest / desync check
- **Unaffected.** The Switch digest is per PlayerIndex 0..3 (`P/npdigest.cpp:47-50`). TF.EX's checksum is the whole state.
  `MatchEnded.checksum` / `winner_seat` are per seat.
- New risk: input *routing* bugs. For example, the second pad reads the first pad's input locally but GGRS sends the right one, or the
  other way round. These show up as a desync on the first tick that player moves, so the existing digest catches them at once.

## 4. Options

| | (a) Wait for / contribute upstream TF.EX | (b) Fork TF.EX (browser) + port the fork (Switch) | (c) Extension layer, one contract, both sides | (d) One connection per local player |
|---|---|---|---|---|
| **What** | Get multi-local into TF.EX + ggrs-ffi upstream, then port it | Change TF.EX's own sources (InputService, GameContext, RollCallElement, MatchmakingService, NetplayManager, ggrs-ffi); build the browser from the fork; translate the fork | Leave TF.EX's sources alone. Browser: Harmony patches in `web/Netplay/` + the ggrs-ffi patch (`netplay/ggrs-ffi.patch` already exists). Switch: `S-*` hand-ported units in TF.EX's shape. Server: additive messages | A second websocket + RoomPeerId per couch player (today's `LocalGuest`) |
| **Browser work** | none until upstream lands, then a version bump | medium-high: own TF.EX build pipeline (today the browser runs the release DLL) | medium: about 6-8 Harmony patches + about 60 lines of Rust in ggrs-ffi | impossible (ggrs-ffi singleton) |
| **Switch work** | the same as (c), later | the same as (c), translated from fork sources | medium-high: sections 3.3, 3.5, 3.7, 3.9 (seat mask, peer-indexed addresses, pad table, rumble) | very high (two GGRS peers in one process); not feasible |
| **Server work** | upstream's server (we run our own anyway) | AddLocalPlayer etc. | AddLocalPlayer / RemoveLocalPlayer / `UpdatePlayer.seat` | none for the lobby |
| **PC TF.EX players** | full, once released | they can play *against* couch devices (GGRS groups shared ids); PC couch needs the fork | the same as (b): they can play against couch devices (inferred, test it), but not host couch players themselves | broken: the other peers expect a peer the device can't run |
| **TF.EX-first conflict** | none | high: every TF.EX update has to be merged into the fork and re-translated | low: TF.EX stays the reference. The layer is small, named, and lives where the Switch-only code already lives | none in theory; in practice unusable |
| **Risks** | no timeline (#182 is closed, no roadmap); the owner may choose another design | merge pain; translator inputs diverge from upstream | patches on private TF.EX members can break on TF.EX updates (the same risk `SwitchLobby.cs` already carries); the PC-peer behaviour is untested | desync/handshake failure at match start |
| **Maintenance on a TF.EX update** | none | rebase the fork, re-translate | re-check about 8 patch targets + the ggrs-ffi patch; keep the Switch's `S-*` units in step with the translated originals | – |

**Ranking: (c) > (a) > (b) > (d).** Run (a) *alongside* (c): offer the contract (shared RoomPeerId, `AddLocalPlayer`,
`advance_frame_multi`) upstream as a proposal or PR once it works for us. If TF.EX takes it, (c)'s patches collapse into the version
bump. If upstream picks a different shape, the extension stays isolated.

## 5. Recommendation

**Contract (decide now and write it into LOBBY-PROTOCOL.md):**
1. A device may own several lobby `Player`s, all with its own `RoomPeerId` and with distinct server-assigned seats.
2. GGRS handles = the ranks of the seats (unchanged). A device's local handles = every seat with its RoomPeerId.
3. GGRS remote address = **peer**, never seat.
4. One signaling socket per device. A device leaving removes all its seats.

**Decide now, so the current TF.EX translation work doesn't make this harder:**
- **Never translate or hand-port "the local seat" as a scalar in new code.** Use one helper, `IsLocalSeat(seat)` /
  `LocalSeatMask()`, and keep `GetLocalSeat()` only as "our first/main seat" (host Start, lobby editor, replay `LocalSeat`). The
  concrete spots:
  - `tools/tfex/overrides.toml:380` (`GetLocalPlayerInputIndex` → `GetLocalSeat()`): keep the override, but route the `seat ==
    GetLocalPlayerInputIndex()` *comparisons* in XGamePadInput/KeyboardInput/RollCallElement ports through `IsLocalSeat`.
  - `tools/tfex/overrides.toml:387` (`DisableAllControllerExceptLocal`, still todo): port it as "except the local seats".
  - `tools/tfex/manifest.toml:300-301` (`NetplayManager.LocalSeat` → `g_npSeat`): when `g_npSeat` is next touched, add the mask
    beside it instead of spreading more `== g_npSeat`.
- **ggrs-switch addressing** (`lib.rs:56,165`): change it to peer index *before* more 3-4 player work lands on top of seat
  addressing. It is the same change 3-4 player matches benefit from (one endpoint per device). It is invisible while every device has
  one player, so it can land first and be checked with today's 2-4 player tests.
- **Don't extend LocalGuest's second connection** toward a match (section 3.4).
- Mark docs/ws-single-controller.md:43-44 as out of date: ggrs-switch already takes several local handles.

**Order of work** (each step is testable on its own and changes nothing for 1-player-per-device matches):
1. **Switch plumbing, no behaviour change:** peer-indexed GGRS addresses (ggrs-switch + net.cpp), `Disconnected` with its address,
   a local-seat mask in `LobbyMatch`/`NpStart`/the input loop, and endpoint-based disconnect counting. Verify with the existing 2- and
   3-4-player lobby tests (0 differing ticks).
2. **Server:** `AddLocalPlayer` / `RemoveLocalPlayer` / `UpdatePlayer.seat` in tfex-server, with tests in `test.mjs`. Deploying it
   is the coordinator's call.
3. **Browser:** the ggrs-ffi patch (multi local, own seats out of `required`; fix `disconnect_all` while there), plus Harmony
   patches for InputService/GameContext/the gates/roll call. First test browser-only: two pads in one tab vs. a second tab. This also
   checks the "unmodified peer" claim, because the second tab plays the role of the PC peer.
4. **Switch lobby:** move LocalGuest's UI onto `AddLocalPlayer`; seat → pad table; `CanStart` for 3-4; per-seat publishing.
5. **Switch match:** first 2 Switch + 1 browser, then 1 Switch + 2 browser. The 2-device case keeps the 2-peer relay usable.
6. **Rumble for several pads**, then the console checklist for the coordinator: real second Joy-Con/Pro Controller, Joy-Con pairing
   applet, frame time with 3 archers.
7. **Upstream proposal** to Fcornaire/TF.EX + ggrs-ffi with the contract and the patches as a reference.

**Consequences to accept:**
- The console's frame cost is that of a 3-4 player match; it needs the pending rollback perf work regardless.
- The browser carries Harmony patches on TF.EX internals that a TF.EX update can break, the same as `SwitchLobby.cs` today.
- PC TF.EX players can only *join* couch lobbies, and only if they use our server; that path is untested.
- Quick play stays one player per device until the server matches by seat count.

---
Web sources: github.com/gschup/ggrs (README + examples/README.md, CHANGELOG, `src/network/protocol.rs`, issues #54 and #94);
github.com/Fcornaire/TF.EX (README roadmap, issues #182 and #159); github.com/pond3r/ggpo (`p2p.cpp`, DeveloperGuide: vanilla GGPO
maps one endpoint to one player, so no help as a model); github.com/johanhelsing/matchbox (`ggrs_socket.rs`: per PeerId,
local-player-agnostic).
