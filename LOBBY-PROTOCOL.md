# TF.EX lobby protocol for a non-.NET client (the Switch)

Status 2026-10-15: **spec only, nothing implemented or run.** Written from the browser project's
TF.EX-compatible server and a decompiled TF.EX 0.19.x. Every claim cites a source line. Where the
code leaves room or wasn't read, the text says **UNVERIFIED**.

Goal: the Switch (native C++, TLS WebSocket client, GGRS speaking TF.EX's wire format) creates a
lobby or joins an open public one, picks an archer, readies, starts, plays one match and ends it,
against the browser build of TF.EX, on the browser project's server.

**Read section 5 first.** A browser lobby match today runs GGRS at **240 Hz over WebRTC**. The
Switch does **60 Hz over a WebSocket relay**. The lobby protocol (sections 1 to 4) works
unchanged. The match only works after a small browser change, proposed in section 5.

Path prefixes used below:

| prefix | path |
|---|---|
| `SRV/` | `~/Code/Towerfall Browser/cloudflare/tfex-server/` |
| `BRW/` | `~/Code/Towerfall Browser/` (branch `perf/ahead-threshold`) |
| `WT/` | browser repo, branch `exp/xplay-versus` (worktree `.../a86873af-.../scratchpad/wt`) |
| `TFEX/` | decompiled TF.EX: `/private/tmp/claude-501/-Users-crab-Code-Towerfall-Browser--claude-worktrees-quickplay-mode-deep-link-cd26a5/ae09f7f5-282e-4d06-a0ec-362d8366135e/scratchpad/tfex-analysis/src/` |
| `MM` | `SRV/matchmaker.js` |
| `MS` | `TFEX/TF.EX.Domain/TF.EX.Domain.Services/MatchmakingService.cs` |
| `MMP` | `TFEX/TF.EX.Patchs/TF.EX.Patchs.Scene/MainMenuPatch.cs` |
| `NM` | `TFEX/TF.EX.Domain/TF.EX.Domain.Services/NetplayManager.cs` |
| `RELAY` | `switch-online/BROWSER-RELAY.md` (this repo) |

---

## 1. Connection and framing

| item | value | source |
|---|---|---|
| URL | `wss://<your-server>/ws` (the site's server) | `BRW/web/wwwroot/main.js:770`; route `SRV/routes.js:25` |
| Origin header | **Send none.** No Origin is always let in, which is the desktop TF.EX case. | `SRV/routes.js:3-11` |
| Per address | at most 16 connections, or close 4029 | `MM:38,74-77` |
| Frames from client | text or binary; the server decodes binary as UTF-8. TF.EX sends **binary** frames containing UTF-8 JSON. | `SRV/worker.js:155,205-211`; `MS:437-449` |
| Frames from server | always **text** frames | `MM:146-148`; `SRV/worker.js:124-127` |
| Message shape | a JSON object with **one** key, the message name. Client messages are matched by that key. Lobby objects use PascalCase field names; message bodies use snake_case. | `MM:6-9,106-109` |
| Integers / lists | must stay JSON integers. Never send `null` for a list, since TF.EX parses through MessagePack. | `MM:8-9` |
| Server keep-alive | `{"KeepAlive":{}}` at connect, then every 30 s | `MM:15,73-81,114-123` |
| Idle drop | no message from the client for 100 s closes it with **4008 "no keep-alive"**. TF.EX echoes every KeepAlive text back unchanged. | `MM:17,118-120`; `MS:509-513` |
| Rate limit | bucket of 100 messages, refilled at 20/s. Running dry closes with 4029. | `MM:39-41,93-98` |
| Max frame | 320 KiB + 16 KiB (both directions). Server→client `SkinChunk` frames can be this big (section 6). | `MM:31,35`; `SRV/worker.js:207-209` |
| Unknown / bad JSON | ignored silently | `MM:101-109` |

**Parsing advice for C++:** parse the JSON and take the first key. TF.EX itself matches
`{"Name"` as a prefix (`MS:502-505`). Never prefix-match `StartLobby`, because `StartLobbyChoice`
also starts with it. The server never sends `StartLobbyChoice`.

## 2. Data model (server's view; TF.EX's classes)

The server rebuilds every lobby and player from known fields (`MM:235-256` player, `:262-273`
game data, `:275-282` mods, `:630-644` lobby). Field order below is the order the server emits.

### Lobby (`TFEX/TF.EX.Domain/TF.EX.Domain.Models.WebSocket/Lobby.cs:9-95`)

| field | type | who sets it | notes |
|---|---|---|---|
| `Name` | string ≤10 | host (CreateLobby) | defaults to the host's name (`MM:631`) |
| `RoomId` | string `[A-Za-z0-9_-]{1,64}` | **host picks it** (TF.EX: `Guid.NewGuid().ToString()`, lowercase with hyphens) | must be unused (`MM:619-621`; `MMP:602,608`). It is also the signaling or relay room name. |
| `MaxPlayers` | int | host | clamped to 2..4 (3..4 for team deathmatch) (`MM:633`). **v1: 2.** |
| `Players` | Player[] | server | host is at seat 0 (`MM:627`); joiners take the lowest free seat (`MM:297-300,330`) |
| `Spectators` | Player[] | server | Seat -1 |
| `GameData` | GameData | host | see below |
| `EndGameChoice` | `{RoomPeerId, Choice}`[] | server | votes: `"Rematch"`, `"ArcherSelect"` or `"Continue"` (`MM:363-374`) |
| `Mods` | `{Name, Data:{string:string}}`[] | host | ≤64 entries. Values are cleaned to strings (`MM:275-282`). Only `Teuria.WiderSet` is enforced by clients (section 4.4). |
| `InGame` | bool | server | true from StartLobbyChoice until the vote resolves (`MM:348-361,382-406`) |
| `Kind` | `"Standard"` / `"Private"` / `"QuickPlay"` | host (`"Private"`, otherwise Standard) | `MM:623` |
| `JoinCode` | string | server | 5 characters for Private lobbies (`MM:25-26,284-291,641`) |
| `Series` | object or null | server | null unless `BestOf > 0`. **v1: always null.** |
| `MissingVariants` | string[] | client-side only | server always sends `[]` (`MM:643`) |

### GameData (`TFEX/.../Models.WebSocket/GameData.cs`; server `MM:262-273`)

| field | type | values | notes |
|---|---|---|---|
| `MapId` | int | -1 = random, 0..15 = one of TF.EX's 16 netplay towers | 0 SACRED GROUND, 1 TWILIGHT SPIRE, 2 BACKFIRE, 3 FLIGHT, 4 MIRAGE, 5 THORNWOOD, 6 FROSTFANG KEEP, 7 KING'S COURT, 8 SUNKEN CITY, 9 MOONSTONE, 10 TOWERFORGE, 11 ASCENSION, 12 THE AMARANTH, 13 DREADWOOD, 14 DARKFANG, 15 CATACLYSM (`TFEX/TF.EX.Domain/TF.EX.Domain.Models/Constants.cs:19-23`; `MM:29`). TF.EX selects the button whose `Data.ID.X == MapId` (`TFEX/TF.EX.Patchs/TF.EX.Patchs.Scene/MapScenePatch.cs:78-79`). **UNVERIFIED:** that PC versus tower index = Switch versus tower index. |
| `Mode` | int | 3 last man standing, 4 headhunters, 5 team deathmatch (anything else becomes 3) | `MM:267`; names `MS:1072-1078`. **v1: 3.** |
| `MatchLength` | int 0..3 | TowerFall `MatchLengths` (default 2) | `MM:268` |
| `Variants` | string[] | `Variant.Title` strings of enabled variants | joiners apply them by title (`TFEX/.../Extensions/MatchVariantsExtensions.cs:12-32`). **v1: `[]`.** |
| `Seed` | int | 0..2^31-2 | **server-owned**: new at create, at every start and after every vote (`MM:270,293-295,352,388`) |
| `BestOf` | int | 0,1,3,5,7 | >0 = series. **v1: 0.** |

### Player (`TFEX/.../Models.WebSocket/Player.cs`; server `MM:235-256`)

The server keeps `Name`, `RoomPeerId`, `IsHost` and `Seat` itself. **Every `UpdatePlayer` replaces
all the other fields.** A field left out resets to its default. So always send the full player,
or the `Platform` flag (section 5) would be lost.

| field | type | Switch value | notes |
|---|---|---|---|
| `Name` | string ≤10 | its name | fixed by the server from Identify/Join/Create |
| `ArcherIndex` | int | 0..8 (vanilla) | resolved by `ResolveArchers` (section 4.3) |
| `ArcherAltIndex` | int | 0 normal, 1 alt, 2 secret | |
| `Ready` | bool | false → true once picked | |
| `RoomPeerId` | uuid | (server) | **the GGRS peer id** (section 4.3). `""` in GetLobbies listings (`MM:603-611`). |
| `IsHost` | bool | (server) | |
| `Seat` | int | (server) | 0..3 for players, -1 for spectators |
| `Team` | int | -1 | 0 blue / 1 red (team deathmatch only) (`MM:238,247`) |
| `CustomArcherId` | string | `""` | non-empty = modded archer (`TFEX/.../Extensions/ArcherDataExtensions.cs:24-45`) |
| `ArcherMods` | string[] | `[]` | `"<mod>@<version>"` (`ArcherDataExtensions.cs:66-87`) |
| `CustomVariants` | string[] | `[]` | informational |
| `UseInstantReplay` | bool | **false** | instant replay runs only if **all** players have it true (`TFEX/TF.EX.Patchs/TF.EX.Patchs/SessionPatch.cs:103`). False turns it off for the match. |
| `HaveModifiedGameFiles` | bool | false | true makes others show "HAS MODIFIED GAME FILES" (`MS:1190-1194`) |
| `Platform` | string ≤32 | **`"Switch"`** | TF.EX sends `"Windows"`, `"Linux"`, `"macOS"` or `"Unknown"` (`TFEX/TF.EX.Domain/TF.EX.Domain.Utils/Platform.cs:7`). A different value shows "X IS PLAYING ON SWITCH - NETPLAY MAY DESYNC" once (`MS:1195-1199`). It's harmless, and it is the proposed relay flag (section 5). |
| `InputDelay` | int or null | its delay in session frames | informational, except in series lobbies where the host's value is everyone's delay (`Lobby.cs` `SeriesInputDelay`; `NM:191`) |

## 3. Messages

### 3.1 Client → server

| message | body | server reaction | source |
|---|---|---|---|
| `KeepAlive` | `{}` | none (counts as activity). TF.EX echoes the server's text. | `MM:596`; `MS:509-513` |
| `Identify` | `{"player_id": str≤128, "name": str≤10}` | stores the name; **no reply** | `MM:598-601`; `MS:296-313` |
| `GetLobbies` | `{}` | `GetLobbiesResponse` | `MM:605-612` |
| `CreateLobby` | `{"lobby": Lobby}` | `CreateLobbyResponse` (no LobbyUpdate) | `MM:614-652` |
| `JoinLobby` | `{"room_id", "name", "is_player": bool}` | `JoinLobbyResponse`, then `LobbyUpdate` to all | `MM:654-662` |
| `JoinPrivate` | `{"code", "name", "is_player"}` | `PrivateJoinResult`, then `LobbyUpdate` to all | `MM:664-675` |
| `UpdatePlayer` | `{"player": Player}` | always a `LobbyUpdate` (to all; only to the sender if not a player or the lobby got too big) | `MM:683-698` |
| `UpdateLobbySettings` | `{"max_players", "game_data", "mods"}` | host only, not in game; **un-readies everyone**; LobbyUpdate | `MM:700-717` |
| `StartLobbyChoice` | `{}` | host only, all ready: LobbyUpdate (InGame, **new Seed**), then `StartLobby` | `MM:719-729,348-361` |
| `MatchEnded` | `{"winner_seat": int, "frame": int, "checksum": str or null, "scores": int[]}` | starts the 33 s vote clock; LobbyUpdate on the first report | `MM:731-740`; `MS:372-387` |
| `RematchLobbyChoice` / `ArcherSelectChoice` / `SeriesContinueChoice` | `{}` | records a vote; resolves when all have voted or after 33 s | `MM:742-753,363-406` |
| `LeaveLobby` | `{}` | `LeaveLobbyResponse`; LobbyUpdate to the rest (or closes the lobby) | `MM:677-680,203-233` |
| `SeriesPickMap`, `EnterQuickPlay`, `ExitQuickPlay`, `SkinChunk` | | not needed for v1 | `MM:755-792` |

### 3.2 Server → client

| message | body | source |
|---|---|---|
| `KeepAlive` | `{}` | `MM:80,121` |
| `GetLobbiesResponse` | `{"lobbies": Lobby[]}`. Only `Kind == "Standard"`, **including running ones**, with every `RoomPeerId` blanked. | `MM:605-612` |
| `CreateLobbyResponse` | `{"success", "message", "lobby": Lobby or null}`. Failures: `NO LOBBY`, `BAD ROOM ID`, `ROOM ID IN USE`, `LOBBY TOO BIG`. | `MM:614-652` |
| `JoinLobbyResponse` / `PrivateJoinResult` | `{"success", "message", "room_peer_id", "lobby"}`. The lobby already contains you, with peer ids. Failures: `LOBBY NOT FOUND`, `MATCH ALREADY STARTED`, `LOBBY FULL` (private also: `TOO MANY TRIES`). | `MM:317-324,654-675` |
| `LobbyUpdate` | `{"lobby": Lobby}`. Sent after **every** change. A lobby **without any `IsHost` player** means the host left and the lobby is gone. | `MM:161-165,209-216` |
| `StartLobby` | `{}` | `MM:359` |
| `ArcherSelectLobby` | `{}`, followed by a LobbyUpdate with everyone `Ready: false`, `InGame: false` | `MM:400-404` |
| `RematchLobby` | `{}`, **preceded** by a LobbyUpdate with a new Seed | `MM:396-399` |
| `SeriesLobby` | `{}` (series only) | `MM:390-395` |
| `LeaveLobbyResponse` | `{"success": true, "message": "" or "NOT IN A LOBBY"}` | `MM:677-680` |
| `SpectatorJoined` | `{"room_peer_id"}`, sent to everyone while InGame | `MM:337-343` |
| `SkinChunk` | `{"chunk": {"bundle_id", "custom_archer_id", "chunk_index", "chunk_count", "data": base64}, "from": peer}` (up to ~330 KB) | `MM:786-792` |
| `QuickPlayStatus` / `QuickPlayMatchFound` | quick play only | `MM:552-557,586` |

### 3.3 Example JSON (exact shapes)

```jsonc
// S->C on connect
{"KeepAlive":{}}
// C->S, echo it back unchanged (any message would do)
{"KeepAlive":{}}

{"Identify":{"player_id":"5d0c8a5e-2b6f-4e8e-9d55-0b7e1f3c9a10","name":"SWITCHY"}}

// host: create (Kind "Private" for a code lobby; RoomPeerId/Seed/JoinCode are ignored/overwritten)
{"CreateLobby":{"lobby":{"Name":"SWITCHY","RoomId":"6f1c2d3e-4a5b-4c6d-8e9f-0a1b2c3d4e5f","MaxPlayers":2,
 "Players":[{"Name":"SWITCHY","ArcherIndex":0,"ArcherAltIndex":0,"Ready":false,"RoomPeerId":"","IsHost":true,
   "Seat":0,"Team":-1,"CustomArcherId":"","ArcherMods":[],"CustomVariants":[],"UseInstantReplay":false,
   "HaveModifiedGameFiles":false,"Platform":"Switch","InputDelay":3}],
 "Spectators":[],"GameData":{"MapId":0,"Mode":3,"MatchLength":2,"Variants":[],"Seed":0,"BestOf":0},
 "EndGameChoice":[],"Mods":[{"Name":"TowerFall.Switch","Data":{"Version":"1"}}],"InGame":false,
 "Kind":"Standard","JoinCode":"","Series":null,"MissingVariants":[]}}}

{"CreateLobbyResponse":{"success":true,"message":"","lobby":{"Name":"SWITCHY",
 "RoomId":"6f1c2d3e-4a5b-4c6d-8e9f-0a1b2c3d4e5f","MaxPlayers":2,"Players":[{"Name":"SWITCHY","ArcherIndex":0,
 "ArcherAltIndex":0,"Ready":false,"RoomPeerId":"a3c9e0d2-77b1-4f3e-a1d4-5e6f7a8b9c01","IsHost":true,"Seat":0,
 "Team":-1,"CustomArcherId":"","ArcherMods":[],"CustomVariants":[],"UseInstantReplay":false,
 "HaveModifiedGameFiles":false,"Platform":"Switch","InputDelay":3}],"Spectators":[],
 "GameData":{"MapId":0,"Mode":3,"MatchLength":2,"Variants":[],"Seed":1187405923,"BestOf":0},
 "EndGameChoice":[],"Mods":[{"Name":"TowerFall.Switch","Data":{"Version":"1"}}],"InGame":false,
 "Kind":"Standard","JoinCode":"","Series":null,"MissingVariants":[]}}}

{"GetLobbies":{}}
{"GetLobbiesResponse":{"lobbies":[{"Name":"BROWSERB","RoomId":"0e8f…","MaxPlayers":2,"Players":[{…,"RoomPeerId":"",…}], …}]}}

{"JoinLobby":{"room_id":"0e8f2a41-9c3d-4b6e-8f10-2233445566aa","name":"SWITCHY","is_player":true}}
{"JoinLobbyResponse":{"success":true,"message":"","room_peer_id":"c1d2e3f4-0000-4111-8222-333344445555","lobby":{…}}}
{"JoinPrivate":{"code":"K7M2Q","name":"SWITCHY","is_player":true}}            // -> PrivateJoinResult (same body)

// always the FULL player (fields you omit reset to defaults)
{"UpdatePlayer":{"player":{"Name":"SWITCHY","ArcherIndex":2,"ArcherAltIndex":0,"Ready":true,"RoomPeerId":"c1d2…",
 "IsHost":false,"Seat":1,"Team":-1,"CustomArcherId":"","ArcherMods":[],"CustomVariants":[],"UseInstantReplay":false,
 "HaveModifiedGameFiles":false,"Platform":"Switch","InputDelay":3}}}
{"LobbyUpdate":{"lobby":{…}}}

{"StartLobbyChoice":{}}
{"LobbyUpdate":{"lobby":{…,"GameData":{…,"Seed":905531177,…},…,"InGame":true,…}}}   // the match seed
{"StartLobby":{}}

{"MatchEnded":{"winner_seat":0,"frame":4282,"checksum":null,"scores":[3,1]}}
{"ArcherSelectChoice":{}}         {"RematchLobbyChoice":{}}
{"ArcherSelectLobby":{}}          {"RematchLobby":{}}
{"LeaveLobby":{}}                 {"LeaveLobbyResponse":{"success":true,"message":""}}
```

## 4. Sequences

### 4.1 Host (Switch creates, a browser joins)

| # | dir | message | what the Switch does |
|---|---|---|---|
| 1 | C→S | connect `/ws` | |
| 2 | S→C | `KeepAlive` | echo it (and every later one) |
| 3 | C→S | `Identify` | |
| 4 | C→S | `CreateLobby` (MaxPlayers 2, Mode 3, MapId 0..15, Variants [], BestOf 0, host player with `Platform:"Switch"`) | |
| 5 | S→C | `CreateLobbyResponse` | keep `RoomPeerId` (own peer id) and `GameData.Seed`. If Private, show `JoinCode`. |
| 6 | C→S | `UpdatePlayer` (archer, `Ready:true`) | TF.EX's host does this when it picks an archer (`TFEX/TF.EX.Patchs/TF.EX.Patchs.Entity.MenuItem/RollCallElementPatch.cs:237-281`) |
| 7 | S→C | `LobbyUpdate` | |
| 8 | S→C | `LobbyUpdate` (browser joined, seat 1, `Ready:false`, `Platform` still `""`) | `MM:654-662,326-343` |
| 9 | S→C | `LobbyUpdate` (browser's first UpdatePlayer: its Platform, CustomVariants, InputDelay) | browser sends this right after joining (`MMP:1664-1676`) |
| 10 | S→C | `LobbyUpdate` (browser `Ready:true`, archer) | |
| 11 | C→S | `StartLobbyChoice`, once **≥2 players and all Ready** (TF.EX `AreAllPlayersReady`, `MS:1656-1678`; server `MM:306-313,719-729`) | TF.EX rate-limits it to 1/s (`MS:1646-1654`) |
| 12 | S→C | `LobbyUpdate` (`InGame:true`, **new Seed**) then `StartLobby` | start the match (section 4.3) |

### 4.2 Joiner (Switch joins a browser's public lobby)

| # | dir | message | what the Switch does |
|---|---|---|---|
| 1-3 | | connect, KeepAlive echo, Identify | as above |
| 4 | C→S / S→C | `GetLobbies` → `GetLobbiesResponse` | filter (section 4.4); peer ids are blank here |
| 5 | C→S | `JoinLobby {room_id, name, is_player:true}` | |
| 6 | S→C | `JoinLobbyResponse` (lobby with peer ids) then `LobbyUpdate` | keep `room_peer_id`. Re-check the lobby, since it may have changed. |
| 7 | C→S | `UpdatePlayer` (full, `Ready:false`, `Platform:"Switch"`) | TF.EX joiners send this at once (`MMP:1664-1676`). **The Switch must**, because it sets the relay flag. |
| 8 | C→S | `UpdatePlayer` (archer, `Ready:true`) | each answered by a LobbyUpdate |
| 9 | S→C | any `LobbyUpdate` where the host changed settings | the server un-readies everyone (`MM:714-716`). Re-validate (4.4). Re-ready, or `LeaveLobby`. |
| 10 | S→C | `LobbyUpdate` (`InGame:true`, new Seed) + `StartLobby` | start the match |

Browser side for reference: `StartLobby` sets `hasHostStartedMatch` (`MS:702-705`). The input
patches then confirm for the player (`TFEX/.../PlayerInput/XGamePadInputPatch.cs:343-364`), the
roll call starts versus (`RollCallElementPatch.cs:507-526`), and the MapScene auto-selects
`MapId` (`MapScenePatch.cs:65-90`) and starts the session.

### 4.3 Lobby → match: what TF.EX does, and what the Switch must mirror

| aspect | TF.EX behaviour | Switch must |
|---|---|---|
| Mode | the session runs TF.EX's custom versus mode (`Modes` 11, `TFEX/.../Extensions/VersusModeExtensions.cs:11`), whose round logic is `NetplayRoundLogic` with `mode = lobby.GameData.Mode` (`TFEX/TF.EX.Core/TF.EX.Core.RoundLogic/NetplayVersusMode.cs:48-70`) | LMS rules (Mode 3) |
| Tower | `MapId` ≥ 0: that tower. `-1`: netplay-safe versus buttons (locked ones dropped without Dark World; `NoRandom` dropped), shuffled with **.NET `System.Random(seed)`** via `Calc.Shuffle`, first one (`MapScenePatch.cs:93-134`). | v1: only `MapId` 0..15. Random needs a port of `System.Random` + the button-list filter. |
| Variants | `ApplyVariants(lobby.GameData.Variants)`: DisableAll, then enable by title (`MS:974-983`; `MatchVariantsExtensions.cs:12-32`) | v1: `[]` only |
| Match length / goal | `MatchSettings.MatchLength = GameData.MatchLength` (`MS:981`; `MMP:636,1680`). Goal = TowerFall's own formula. Only test mode forces `PlayerGoals = 2` (`TFEX/TF.EX.Patchs/TF.EX.Patchs/MatchSettingsPatch.cs:16-22`), which the xnet harness used (`RELAY:159`). | use the **lobby's** MatchLength. **UNVERIFIED:** which GoalScore formula applies to the custom mode 11 (FortRise). Log `Session.MatchSettings.GoalScore` in a browser lobby match. |
| Seed | every LobbyUpdate with a different seed calls `StateApi.SetSeed(seed)` (`MS:839-842`), also at create/join (`MMP:635,1678`). `SetSeed` = state seed + `Calc.Random = new Random(seed)` (`TFEX/TF.State.Domain/TF.State.Domain.Services/RngService.cs:30-34`). `Session.StartGame` sets `RandomLevelSeed = rng seed` (`TFEX/TF.State.Patchs/TF.State.Patchs/SessionStatePatch.cs:26-29`). `StartSession` postfix calls `ResetRng()` (`MapScenePatch.cs:56`). | Use the Seed from the **LobbyUpdate right before `StartLobby`** (`MM:352,358-359`) exactly where xnet used 42 (gameplay RNG + `RandomLevelSeed`, same code path as test mode). |
| Seats → GGRS handles | `GGRSConfig.Netplay.Players` = RoomPeerIds **ordered by Seat**. `LocalPeerId` = own. `NumPlayers` = player count. Spectators listed separately (`NM:1033-1058`, called at `MapScenePatch.cs:35-36`). `CompactSeatsToHandles` renumbers seats to 0..n-1 in seat order (`TFEX/TF.EX.Domain/TF.EX.Domain.Services/ArcherService.cs:37-47`), so **PlayerIndex = handle = rank of Seat**. The server reads `winner_seat` the same way (`MM:427-432`). | handle = index of own RoomPeerId among Players sorted by Seat (host = 0 in a 2-player lobby). The GGRS remote address = the other RoomPeerId. |
| Archers | `ResolveArchers(lobby)` (`ArcherDataExtensions.cs:96-129`): (1) vanilla `ArcherIndex` 0..8, alt 0..2, `CustomArcherId == ""`, and the archer exists → kept, **duplicates allowed**. (2) A custom archer → kept only if every player's `ArcherMods` has its mod tag. (3) Otherwise → `PickUnusedVanilla(alt, used)`: lowest index 0..8 not taken by step 1, requested alt if it exists, else 0 (`:144-160`). Then `TFGame.Characters[handle]`, `AltSelect[handle]` (`ArcherService.cs:49-67`). | implement steps 1 and 3 (Switch sends `ArcherMods: []`, so any browser custom archer falls to step 3). **UNVERIFIED:** that `ArcherData.Get(i, alt)` existence matches between PC and Switch. |
| Teams | team deathmatch only (`MS:1168-1181`) | n/a in v1 |
| Signaling room | `<server>/room/<RoomId>?peer=<own RoomPeerId>`, set at create/join (`MMP:603,634,1651-1655`), used by `netplay_init` (`NM:219-231`) | replaced by the relay (section 5) |
| Ping measurement | while in the lobby menu, `ggrs_ffi.ping_measurement_start("<server>/ping_measurement/<RoomId>?peer=<id>")`: matchbox signaling + WebRTC to every lobby member, stats only (`MS:1348-1374`; called every frame from `TFEX/.../Engine/TFGamePatch.cs:167`) | **don't connect.** Missing samples only mean no ping is shown (`MS:1316-1346`). Joining the signaling room without WebRTC would make browsers send offers that never complete. |
| GGRS session | `InputDelay = SeriesInputDelay ?? own delay`, `MaxInputDelay = ToFrames(332)` (`NM:191-192`), `Fps = 240` (`TFEX/TF.EX.Domain/TF.EX.Domain.Models/GGRSConfig.cs:9`; `Constants.cs:11`), `ToFrames(ms) = (ms*240+999)/1000` (`TFEX/.../Models/NetplayPreferences.cs:43-46`). Builder values (prediction 15·fps/60, desync checks every 120·fps/60, 15 s disconnect, …) as in `RELAY:92-108`. | the same as the xnet work at **60 Hz** (`RELAY` sections 2-3), 2 players |
| Round 0 start | `NetplayRoundLogic.OnLevelLoadFinish` calls `NetplayManager.Init` and **does not spawn** (`NetplayRoundLogic.cs:63-70`). The level doesn't update until GGRS is synchronized (`TFGamePatch.cs:232-241,317`). The first synchronized frame calls `EstablishSessionIfSynchronized` → `SpawnFirstRound` (adds `VersusStart` + spawns players) **before** that frame's `Level.Update` (`TFGamePatch.cs:283-286`; `NM:208-217,290-320`). Rounds ≥1 spawn in `OnLevelLoadFinish` (`NetplayRoundLogic.cs:71-74,77-96`). | GGRS frame 1 = first Level.Update with players and VersusStart present. **UNVERIFIED:** that this equals the xnet harness (plain `LastManStanding`, spawn at load, `RELAY:155-162`). `RELAY:185` already lists "lobby path not compared". |
| Round logic | LMS update and death handling (`NetplayRoundLogic.cs:111-131,175-195,297-328`) read like vanilla `LastManStandingRoundLogic` (score, crown event, `FinalKill` at goal-1, `CancelFinalKill`). `OnRoundStart` adds `SpawnTreasureChestsVersus` (`:111-115`). | **UNVERIFIED** line-by-line against vanilla. Diff it before relying on it. |
| Match end | `GotoNextRound` is skipped once there is a winner; round results are re-linked to match results; level frozen (`TFEX/TF.EX.Patchs/TF.EX.Patchs/SessionPatch.cs:22-48`). At match results TF.EX feeds **neutral input** into GGRS (`TFGamePatch.cs:541-542`). | match the frame where inputs go neutral, or the tail of the match diverges (cosmetic only after the winner is fixed: **UNVERIFIED**) |
| Options forced | `NetplayOptions.Apply` at StartSession: `ReplayMode = 0`, `CanSkipReplays`, no tips (`TFEX/TF.EX.Domain/TF.EX.Domain/NetplayOptions.cs:34-44,77-86`). The kill-cam `ReplayRecorder` is dropped for netplay levels (`TFEX/.../Scene/LevelPatch.cs:63`). | same as the xnet harness (it drops the recorder: `RELAY:160`) |
| Instant replay | on only if all players `UseInstantReplay` (`SessionPatch.cs:103`). `InstantReplayFootage.StepPause` can skip ticks (`TFGamePatch.cs:279`). | send `UseInstantReplay:false` |

### 4.4 Compatibility checks a joiner faces, and what the Switch claims

TF.EX checks very little. Lobby list (`MMP:1460-1522`) and private join (`MMP:1014-1041`):

1. **WiderSet:** a `Mods` entry `Teuria.WiderSet` with `IsWide`. A wide lobby needs a wide joiner;
   a narrow one is refused by a wide joiner. With no entry, a wide joiner is refused.
   (`TFEX/TF.EX.Common/TF.EX.Common.Interop/WiderSetModApiData.cs:9-48`; `MMP:1462-1479`).
2. **Variants:** every title in `GameData.Variants` must exist locally, or "MISSING CUSTOM
   VARIANTS" (`MMP:1481-1493,1035-1039`).
3. **Running lobbies:** a lobby that is InGame or mid-series can only be spectated (`MMP:1496-1499`).
4. **Versions:** only a *notification* after joining, for `Mods` entries whose `Data["Version"]`
   differs from a locally installed mod of the same name (`MMP:1603-1625,1691`). No TF.EX version
   gate. The update check only runs against the official server (`BRW/docs/MULTIPLAYER.md:119-122`).

So **a Switch-hosted lobby with `Mods: []` or a private marker entry, and `Variants: []`, is
joinable by any narrow TF.EX client.** Unknown mod names are ignored, because the version lookup
returns null.

What the Switch accepts when joining (v1): `Kind == "Standard"`, `!InGame`, `Series == null` and
`BestOf == 0`, `MaxPlayers == 2`, `Players.Count == 1`, `Mode == 3`, `Variants == []`,
`MapId` in 0..15, WiderSet `IsWide` absent or `"False"` (C# `bool.ToString()`;
`bool.TryParse` ignores case). It also needs the relay-capable-host marker from section 5.

## 5. Match transport: relay instead of WebRTC (proposal; nothing changed)

### 5.1 The two blockers

1. **Tick rate.** TF.EX runs its session at 240 Hz (`Constants.cs:11`; `GGRSConfig.cs:9`;
   `TFEX_LESSONS.md` item 1). The only 60 Hz path is the page-load experiment `?tfexfps=60`
   (`WT/web/Netplay/TfexFps.cs:10-66`; `WT/web/wwwroot/main.js:801-803`). It exists **only on
   branch `exp/xplay-versus`** (commit `7f4d734`), not on the browser's main line. It patches the
   `GGRSConfig.Fps` getter, `NetplayPreferences.ToFrames` and `InputDelayAdvisor`. At 240 Hz the
   relay also can't keep up: about 4 × 125 messages/s per peer (`RELAY:46`).
2. **Transport.** `netplay_init` opens `GGRSConfig.Netplay.ServerConf.RoomUrl` through
   `tfnet_open`. In `tfnet.js`, a URL ending in `/relay/<room>` goes to `TFNet.openRelay`, and
   anything else to matchbox + WebRTC (`WT/netplay/tfnet.js:162-163,375-499`). That is also
   **only on `exp/xplay-versus`** (commit `a355c15`). `BRW/netplay/tfnet.js` has no
   `isRelayUrl`. The relay URL needs `?peer=<own uuid>&remote=<other uuid>`
   (`WT/netplay/tfnet.js:401-415`).

### 5.2 Smallest browser change (one new C# file + merging the two exp commits)

The lobby signal is the **Switch player's `Platform == "Switch"`**. It is per player, the server
keeps it (`MM:252`), TF.EX ignores it except for one notification, and it is present whether the
Switch hosts or joins (after its first UpdatePlayer). Optionally, the browser host advertises
that it is relay-capable, so the Switch can filter the lobby list.

Proposed `web/Netplay/SwitchLobby.cs`, hooked from `TfexPatches.Apply` (`BRW/web/Netplay/TfexPatches.cs:26-50`):

| hook | change |
|---|---|
| postfix `TF.EX.Domain.Services.NetplayManager.UpdatePlayers` (runs in `MapScene.StartSession` prefix, `MapScenePatch.cs:35`, **before** `Init` at level load) | If the lobby has exactly 2 players and one has `Platform == "Switch"`: set `GGRSConfig.Netplay.ServerConf.RoomUrl = <server>/relay/<RoomId>?peer=<LocalPeerId>&remote=<other player's RoomPeerId>`, clear `GGRSConfig.Netplay.Spectators`, and switch `TfexFps` to 60 **for this session**. `GGRSConfig.InputDelay` is computed later in `Init` (`NM:191`), so `ToFrames` already sees 60. |
| `TfexFps` | make `Fps` settable at run time (today it is read once from `TOWERFALL_TFEX_FPS`, `TfexFps.cs:38-39`). Reset it when the scene goes back to `MainMenu` (`TFGamePatch.OnSceneTransition`, `TFGamePatch.cs:84-140`). **UNVERIFIED:** switching rate inside a running page (only tested as a page-load option). |
| prefix `NetplayManager.AddLateSpectator` / `AddSpectators` (`NM:981-1031`) | no-op in relay mode (a relay room carries exactly 2 peers; see 5.4) |
| postfix `MainMenuPatch.AddWiderSetLobbyMod` (`MMP:1559-1580`; called after `CatalogLobbyMods` clears `Mods`, `MMP:588-589,606,619`) | add `{"Name":"TowerFallBrowser.Relay","Data":{"Version":"1","Fps":"60"}}` so a Switch can see that the host is relay-capable. Unpatched clients ignore unknown mod names (4.4). |
| merge `a355c15` (tfnet.js relay) and `7f4d734` (TfexFps) | `tfnet.js` needs nothing else: it already dispatches on the URL. |

The joiner side is the same hook: a browser joiner rewrites its own URL with `remote` = the
Switch host's peer id.

### 5.3 Hosting `/relay/<room>` on tfex-server (proposal)

Today `parseRoute` knows only `/ws`, `/room/<id>`, `/ping_measurement/<id>` (`SRV/routes.js:21-31`).
The separate `relay-worker/` (`switch-online/relay-worker/worker.js:11-14,34-43`) is a temporary
probe. It allows 4 peers, 1 KB/message and 150 messages/s, has no membership check, and is open
to anyone.

Proposal:
- `routes.js`: accept `/relay/<RoomId>?peer=<uuid>` as kind `relay`.
- `worker.js`: route it to a new Durable Object class `RelayRoomObject` (one per room, `idFromName("relay:"+id)`), plus migration `v3` (`SRV/wrangler.toml` style). Before accepting the WebSocket, ask the matchmaker that `peer` is a **player** of lobby `RoomId` and the lobby is InGame. `/member` already does player-or-spectator (`SRV/worker.js:189-192`; `MM:514-518`). This matters: a third peer's frames would be fed to ggrs-ffi as the other player, and a malformed one panics the wasm (`RELAY:28-29`).
- 2 sockets per room. The same peer reconnecting replaces its old socket, as `SRV/signal.js:52-77` does. Forward **binary** frames to the other socket unchanged and drop text. Cap 2 KB/frame (average 27-54 B, `RELAY:46-48`). Rate cap ≥ 400/s (60 Hz: ~125/s, `RELAY:46`).
- Same origin rule as the rest (`SRV/routes.js:9-12`); the Switch sends no Origin.
- `local.mjs` gets the same route, for `node local.mjs` testing (`SRV/local.mjs`).
- **Cost (free plan):** incoming WebSocket messages count 1/20 request, and the cap is 100,000 requests/day (`BRW/docs/MULTIPLAYER.md:157-160`). A 10-minute match = 2 × 125 × 600 = 150,000 messages = 7,500 requests. That is **about 13 ten-minute relay matches a day**, shared with everything else. The object stays awake for the match (~75 GB-s / 10 min, same as signaling, `MULTIPLAYER.md:162-165`). The paid Workers plan lifts this.

Relay URLs: the Switch uses `wss://<server>/relay/<RoomId>?peer=<own RoomPeerId>`. The browser
uses the same plus `&remote=<Switch RoomPeerId>`. Wire: one `bincode(ggrs::Message)` per binary
frame (`RELAY` sections 1-2).

### 5.4 Who should not end up in a Switch lobby

Spectators: the browser host would put them into its GGRS session (`NM:1024-1031`;
`RollCallElementPatch.cs:518-521`; late ones via `SpectatorJoined`, `MS:706-719`). A relay room
can't carry them. **UNVERIFIED:** whether ggrs-ffi's start waits for spectators to connect, which
would time out the match. Hence the spectator no-op above.

Unpatched clients: desktop TF.EX on a CUSTOM server, or a stale cached page, joining a
Switch-hosted public lobby would play 240 Hz WebRTC and give up after 20 s ("Connection to the
other players failed", `NM:262-271`; `TFGamePatch.cs:205-226`). The simplest gate is for the
Switch to host **Private** lobbies (`Kind:"Private"`, join by code; never listed, `MM:609`).

## 6. Timeouts and things the Switch must answer

| what | limit | consequence | source |
|---|---|---|---|
| server KeepAlive | echo within 100 s (sent every 30 s) | closed 4008, removed from the lobby; host → lobby closes | `MM:15-17,114-123,203-216` |
| message rate | ≤20/s sustained, burst 100 | closed 4029 | `MM:39-41,93-98` |
| `SkinChunk` frames | text frames up to ~330 KB may arrive (a browser player with a custom archer: `archerIndex ≥ 9`, `MS:451-500`) | the WS client must read and discard them without failing | `MM:31,35,786-792` |
| `UpdatePlayer` | always answered by a LobbyUpdate; no client timeout on the server side | TF.EX locks its own controls until then (`MS:646-651`) | `MM:683-698` |
| ready / archer select | **no** server timeout in Standard/Private lobbies (quick play only: 1.5 s auto-start) | never kicked for idling | `MM:22-23,695` |
| host's start | `StartLobbyChoice` ignored unless all ready; TF.EX ≥1 s between tries | | `MM:719-729`; `MS:1646-1654` |
| GGRS sync (browser) | **20 s** from the browser's successful `netplay_init` (at its level load) | browser aborts: back to the online menu, **disconnects from the server** (so leaves the lobby) | `NM:53,262-271`; `TFGamePatch.cs:205-226` |
| ggrs-ffi waiting for the peer | 30 s; with the relay, the peer counts as connected when the browser's own socket opens | | `RELAY:34-36,105` |
| GGRS during the match | 500 ms silence = `NetworkInterrupted`; 15 s = disconnect. Without a winner, the browser then leaves the lobby. | | `RELAY:38,90`; `NM:378-397` |
| end-of-match vote | 33 s from the first `MatchEnded`, vote or mid-match leave | undecided = archer select | `MM:19,363-406,731-740,227` |
| ping measurement | not required | no ping shown | `MS:1316-1374` |

Nothing else needs an answer: no handshake, token or version reply.

## 7. End of the match

What the browser does (`TFEX/TF.EX.Patchs/TF.EX.Patchs.Entity.HUD/HUDPatch.cs:21-90`):
1. When `VersusMatchResults` is `finished`, it exports the replay and **resets GGRS** (`:26-42`),
   which closes its relay socket.
2. It then sends **one** `MatchEnded {winner_seat: Session.GetWinner() (a handle), frame,
   checksum: SHA-256 hex of the last state or null, scores: per handle}` (`:59,67-90`;
   `MS:372-387,1757-1767`) and shows REMATCH / ARCHER SELECT (`:62`; `PauseMenuPatch.cs:83-159`).
3. Server: the first report opens the 33 s vote (`MM:731-740`).
   - Every player votes Rematch → LobbyUpdate (new seed) + `RematchLobby`. The browser
     immediately builds a new MapScene and a **new GGRS session** (`MS:680-701`; `MM:396-399`).
   - Otherwise (any ArcherSelect, or timeout) → `ArcherSelectLobby` + LobbyUpdate (everyone
     un-readied). The browser returns to the roll call (`MS:720-728`; `MM:400-404`).
4. If a player leaves: a joiner leaving while the browser is in the level makes it show "All
   players left...", send `LeaveLobby` and drop the lobby (`MS:843-855`), so the lobby ends. The
   host leaving closes the lobby; the joiner sees a hostless LobbyUpdate (`MM:209-216`;
   `MS:856-873`).

**Minimal valid v1 for the Switch:**
- Keep the relay open and keep stepping GGRS until its own results screen is done. A close after
  the winner is decided is harmless to the browser (`NM:385-397` only leaves when there is no
  winner).
- Optionally send `MatchEnded` with `winner_seat` = winning handle, `scores` = per handle,
  `checksum: null`.
- Send `ArcherSelectChoice` (never `RematchLobbyChoice`: a rematch needs a fresh GGRS session
  and a new seed handled mid-level). Wait for `ArcherSelectLobby` (≤33 s), then either re-ready
  for another match (back to 4.1 step 6 or 4.2 step 8) or send `LeaveLobby`.
- Simplest fallback: `LeaveLobby` right after the results. It is valid, but it ends the lobby for
  the browser too.

## 8. Open questions / UNVERIFIED

1. **60 Hz in a lobby match.** It needs `TfexFps` made per-session and merged. `TfexFps.cs:20`
   itself notes untouched 240-based constants (spectator UI, `InstantReplayFootage`). Never run
   from a lobby.
2. **Goal score** for TF.EX's custom mode 11 with a given `MatchLength` (the xnet reference used
   test mode's forced 2).
3. **`NetplayRoundLogic` vs the xnet reference:** first-round spawn timing (4.3) and whether its
   LMS code equals vanilla line by line. Run a browser-vs-browser **lobby** match at 60 Hz with
   the `xplay2` logging, and compare it with `browser-reference-log-arrows.txt` before trying
   the Switch.
4. **Spectators** with a relay-mode host (does ggrs-ffi wait for them?).
5. **Random tower** (`MapId -1`): needs .NET `System.Random(int)` plus TowerFall's `Calc.Shuffle`
   and the exact button filter (`MapScenePatch.cs:103-133`). v1 avoids it.
6. Tower index and archer `(index, alt)` existence parity between PC and Switch.
7. **Relay hosting:** a new Durable Object on tfex-server (needs a deploy and free-plan budget,
   5.3) or a hardened `relay-worker`. Owner's call.
8. Seat compaction in TF.EX happens client-side (`NormaliseSeats`, `MS:805-815`) only if seats
   collide; the server never sends colliding seats, so the Switch can trust `Seat`.
9. The match-results neutral-input gate (`VersusMatchResultsInputGate`, not read): which frame
   inputs become neutral at the results screen.
10. Desktop TF.EX can't play a Switch lobby at all (240 Hz, no relay transport). The plan covers
    browser ↔ Switch only.

## 9. C++ implementation checklist

- [ ] WSS `/ws`, no Origin. Read text frames up to 400 KB and discard unknown names.
- [ ] Echo `{"KeepAlive":{}}` on receipt (from the network thread, also during the match).
- [ ] `Identify` once, then `GetLobbies` / `CreateLobby` / `JoinLobby` / `JoinPrivate`.
- [ ] Generate `RoomId` = random UUID v4 (lowercase) when hosting.
- [ ] Keep `own_peer_id` (from CreateLobbyResponse host player / `room_peer_id`) and the latest
      `Lobby` from every LobbyUpdate. A lobby with no `IsHost` player means it is gone.
- [ ] `UpdatePlayer` always with the full player: `Platform:"Switch"`, `UseInstantReplay:false`,
      `CustomArcherId:""`, `ArcherMods:[]`, `Team:-1`. Send it once right after joining.
- [ ] Host: `StartLobbyChoice` when ≥2 players and all are `Ready`.
- [ ] On `StartLobby`: seed = last LobbyUpdate's `GameData.Seed`. Handles = Players by Seat.
      Archers via ResolveArchers steps 1 and 3. Tower = MapId, LMS, the lobby's MatchLength.
      Open `wss://<server>/relay/<RoomId>?peer=<own>`. GGRS 2 players, 60 Hz. Sync within 20 s.
- [ ] After the results: optional `MatchEnded`, then `ArcherSelectChoice` (or `LeaveLobby`).
      Handle `ArcherSelectLobby` → un-readied lobby.
- [ ] Ignore: `SkinChunk`, `SpectatorJoined`, `QuickPlay*`, `SeriesLobby`. Treat `RematchLobby`
      as unexpected in v1 and leave.
