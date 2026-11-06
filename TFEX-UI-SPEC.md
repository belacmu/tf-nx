# TF.EX online UI: v1 flow spec and code structure to mirror

Source: TF.EX v0.19.2. The decompile this was written from (a /tmp scratchpad) is gone; the real source is now the
`vendor/tfex/` submodule (same class names; `TF.EX.Patchs.Scene/X.cs` there is `src/network/TF.EX.Patchs/Scene/X.cs`).
See TFEX-BASE.md for how the port relates to TF.EX. Written 2026-10-15.

Emphasis (user, 2026-10-15): for v1 the **functional flow** and **TF.EX's code structure** matter; visual exactness does
not. The long-term hope is to run something very close to TF.EX itself on the Switch, so the C++ port should mirror TF.EX's
classes, services, methods and Harmony patch points 1:1 (section 3). Pixel detail is kept only where it was free.

Path abbreviations used in citations (all under the source root above):

| abbr | path |
|---|---|
| `MMP` | `TF.EX.Patchs/TF.EX.Patchs.Scene/MainMenuPatch.cs` |
| `MI/` | `TF.EX.Patchs/TF.EX.Patchs.Entity.MenuItem/` |
| `CC/` | `TF.EX.Domain/TF.EX.Domain.CustomComponent/` |
| `MS` | `TF.EX.Domain/TF.EX.Domain.Services/MatchmakingService.cs` |
| `NM` | `TF.EX.Domain/TF.EX.Domain.Services/NetplayManager.cs` |
| `TGP` | `TF.EX.Patchs/TF.EX.Patchs.Engine/TFGamePatch.cs` |
| `XGP` | `TF.EX.Patchs/TF.EX.Patchs.PlayerInput/XGamePadInputPatch.cs` |

Vanilla menu buttons used below: Confirm = A, Back = B, Start = START (+ on Switch), **Alt = RB/RT**, **Alt2 = LB/LT**
(`docs/reference/tf8-decompile/TowerFall/XGamepadInput.cs:68-80`). `ButtonModes`: 1 Confirm, 2 Back, 3 Alt, 4 Alt2, 5 Start
(`.../MenuButtonGuide.cs:8`).

---

## 1. States

TF.EX extends `MainMenu.MenuState` with values past vanilla's 0..15 (`TF.EX.Domain/TF.EX.Domain.Models/MenuState.cs:3-30`,
mapping `TF.EX.Domain/TF.EX.Domain.Extensions/MenuStateExtensions.cs:9-56`, identity casts):

| value | state | in v1? |
|---|---|---|
| 3 | Main (vanilla) | yes |
| 4 | Fade (vanilla) | yes (start) |
| 5 | Rollcall (vanilla) = **the lobby screen** | yes |
| 59 | LobbyBrowser | yes |
| 60 | LobbyBuilder (create / edit lobby settings) | yes |
| 61 | SeriesLobby | no (series) |
| 62 | NetplaySelect | yes |
| 63 | PrivateSelect | yes |
| 64 | PrivateJoinCode | yes |
| 65 | QuickPlaySearch | no (quick play) |
| 66 | Changelog | no |

How the states are driven: `MainMenu.CallStateFunc(name, state)` prefix (`MMP:140-192`) intercepts `"Create"`/`"Destroy"` for
the custom states and returns false (vanilla never sees them); for `Main` "Create" it calls `DropServerConnectionIfAny`
(`MMP:180-185`, `MMP:948-957`). A postfix on the same method (`MMP:374-385`) adds the lobby screen's extra button guides when
state 5 is created. `ScreenTitle.ChangeState` prefix shows `menuTitles/fight` as the title for every netplay state
(`TF.EX.Patchs/TF.EX.Patchs.Entity/ScreenTitlePatch.cs:22-45`); its ctor prefix maps unknown states to 7 (`:12-20`).

**Switch note.** The Switch build is AOT; whether its `MainMenu` dispatches states through a `CallStateFunc(string, state)` (name
lookup) or a compiled switch decides where our hook sits. The custom state numbers only need to be values the Switch's
`MainMenu.Update`/`ScreenTitle` tolerate.

### State graph (v1)

```
Main(3) --NETPLAY--> [version check] --> NetplaySelect(62)
NetplaySelect --LOBBIES--> LobbyBrowser(59) --START--> LobbyBuilder(60) --START (CreateLobby ok)--> Rollcall(5)
                          LobbyBrowser --A on a lobby (JoinLobby ok)--> Rollcall(5)
NetplaySelect --PRIVATE--> PrivateSelect(63) --CREATE--> LobbyBuilder(60, private) --START--> Rollcall(5)
                           PrivateSelect --JOIN--> PrivateJoinCode(64) --A (JoinPrivate ok)--> Rollcall(5)
Rollcall(5) (host, own seat empty) --START--> LobbyBuilder(60, editing) --START (UpdateLobbySettings)--> Rollcall(5)
Rollcall(5) all ready --host START--> server StartLobby --> Fade(4) --> MapScene --> Level (match)
Level end: votes --RematchLobby--> new MapScene ; --ArcherSelectLobby--> new MainMenu(Rollcall 5) ; QUIT --> new MainMenu(NetplaySelect 62)
Back: NetplaySelect->Main(3) ; LobbyBrowser->NetplaySelect ; PrivateSelect->NetplaySelect ; PrivateJoinCode->PrivateSelect ;
      LobbyBuilder->LobbyBrowser|PrivateSelect (create) or Rollcall (edit) ; Rollcall (own seat not joined) -> LeaveLobby -> LobbyBrowser
```

---

## 2. Screens

### 2.1 Main menu (state 3), changes

Hook: `MainMenu.CreateMain` **postfix** `MainMenu_CreateMain` (`MMP:231-249`) -> `AddNetplayButtonToMenu` (`MMP:651-727`).
Also clears `WiderSetMenu.IsNetplayRequested` and turns WiderSet's wide mode off if the versus settings are still in netplay
mode or a lobby is held (`MMP:237-248`).

`AddNetplayButtonToMenu` works on the to-be-spawned list (`Scene.GetToBeSpawned<T>`, a TF.EX extension over the scene's
pending-add list):

1. Needs `FightButton` and `CoOpButton`, else does nothing (`MMP:660-668`).
2. **Removes the vanilla `CoOpButton`** (`MMP:669`).
3. Every `MainModeButton` whose `Position.Y` is within 1 of **210** is moved **down 10**, and its private `tweenTo` is set to the
   new position (`MMP:670-677`). On Switch that is TrialsButton (200,210) and ArchivesButton (270,210) -> y 220
   (`SWITCH-UI-RE.md` table; Switch `MainModeButton.tweenTo` at +0xe4).
4. Adds `NetplayButton(position (220,140), tweenFrom (480,120), onConfirm)` (`MMP:678-687`) and
   `CompactCoOpButton(position (290,180), tweenFrom (480,180))` (`MMP:688`). On Switch (220,140) is exactly where vanilla
   CoOpButton was.
5. Navigation (`MMP:689-711`): Fight.Right = Netplay; Netplay.Left = Fight; Netplay.Right = CompactCoOp; CompactCoOp.Left =
   Netplay; CompactCoOp.Up = Netplay; Trials.Up = Fight; Archives.Up = Netplay and Netplay.Down = Archives; if a
   WorkshopButton exists: Workshop.Up = CompactCoOp, CompactCoOp.Down = Workshop; else CompactCoOp.Down = Archives
   (the Switch has no Workshop, so the else branch applies). Links that other vanilla items had to the removed CoOpButton
   are **not** rewritten (see open questions).
6. If `ToStartSelected` was the CoOpButton (returning from co-op), it becomes the CompactCoOpButton (`MMP:712-715`).

**NetplayButton** (`CC/NetplayButton.cs`), subclass of vanilla `MainModeButton`, title `"NETPLAY"`, players text `"ONLINE MATCHS"`
(sic) (`:54-55`). Draws one `Image(MenuIcons.Online())`, centered origin, `BaseScale = 60 / icon.Width` (`:10,16`),
rendered with `DrawOutline(1)` then base render (`:70-74`); the ImageScale/Rotation/Y overrides feed MainModeButton's
standard select wobble. Confirm -> `MenuAction` (`:63-68`): `MainMenu.CurrentMatchSettings = MainMenu.VersusMatchSettings`,
`MainMenu.RollcallMode = 0` (Versus), then the callback: `TFGamePatch.RequestNetplayEntry(menu, enter)` (`MMP:680-686`).

**Online icon**: `MenuIcons.Online()` returns the registered subtexture, falling back to `TFGame.Atlas["versus/introArrow"]`
(`CC/MenuIcons.cs:16-26`). It is configured in `TFEXModModule.RegisterAndLoad`:
`InternetIcon = RegisterTexture(content.Root.GetRelativePath("imgs/netplay.png"))`; `MenuIcons.ConfigureOnline(() =>
InternetIcon.Subtexture)` (`TF.EX.Core/TF.EX/TFEXModModule.cs:119-120`). The same icon is the netplay versus mode's icon
(`TF.EX.Core/TF.EX.Core.RoundLogic/NetplayVersusMode.cs:27`). File, copied from the v0.19.2 zip:
`imgs/netplay.png` (58x58 RGBA; zip path `DShad.TF.EX/imgs/netplay.png`); since 2026-10-28 TF.EX's own module boot loads it from sd:/xplay/tfex/imgs/ (vendor/tfex's copy, tools/devserve.list). The zip also has a
48x48 `Content/Atlas/menuAtlas/gamemodes/netplay.png` (atlas entry `gameModes/netplay`), copied as
`assets/tfex-gamemode-netplay-icon.png`; nothing in the decompiled code references it.

**CompactCoOpButton** (`CC/CompactCoOpButton.cs`), subclass of `MainModeButton`, title `"CO-OP"`, no players text. Uses vanilla
`TFGame.MenuSpriteData.GetSpriteInt("CoopIcon")` at scale 0.65 plus four 19x19 `coopSkulls` subtextures orbiting it
(radius 19.5 + 3.25*sine; `:80-115`); on confirm the skulls spin in (`:126-152`). `MenuAction` sets `MainMenu.State = 6`
(CoOp) (`:154-157`). `Rotate=false`, `PlaySound=false`, text scale 1 (`:31-39`).

**RequestNetplayEntry** (`TGP:419-465`): on the official server, if the update status is not fresh, adds Loader
`"CHECKING VERSION"` (with fade), disables controllers, runs `IAutoUpdater.CheckForUpdate()`; when done, `HandleAutoUpdate`
(`TGP:385-417`) calls `ResolveNetplayEntry`: update available -> auto-update dialog or notification `"Online play requires
version X (current version : Y)"`; status unknown -> `"Could not verify TF.EX version, check your connection and retry"`;
otherwise warnings (`"NON-COSMETIC MODS CAN DESYNC NETPLAY"`, `"YOUR GAME FILES ARE MODIFIED - NETPLAY MAY DESYNC"`) and
`enter()`, which sets `WiderSetMenu.IsNetplayRequested = true` and `State = WiderSetMenu.SelectionState ?? NetplaySelect`
(`MMP:682-685`; WiderSet's standard/wide picker comes first only if WiderSet is installed). For the Switch v1 the version
check can be skipped (non-official server) or kept as a server call.

Related vanilla patches: `FightButton.MenuAction` postfix clears netplay mode (`MI/FightButtonPatch.cs:11-18`). The browser adds
the same for Trials and the co-op submenu button (`Towerfall Browser/web/Netplay/TfexPatches.cs:52-99`, a TF.EX bug fix).

### 2.2 NetplaySelect (62)

Create: `CreateNetplaySelect` (`MMP:729-783`).
- `MainMenu.VersusMatchSettings.ApplyNetplayMode()` (switches versus mode to TF.EX's registered "Netplay" mode),
  `MatchVariantsPatchs.DisableUnauthorized(variants)`, `matchmaking.ResetLobby()`, `ResetPeer()`,
  `inputService.EnableAllControllers()`, `LobbyBuilderContext.IsPrivate = false`.
- `BackState = MenuReturn.NetplayEntry ?? Main` (the state NETPLAY was entered from: Main or WiderSet's picker; `MMP:748-750`,
  `MMP:1068-1079`).
- Three `NetplayMenuButton`s (all `MainModeButton` subclasses, `BaseScale 1`, one `Image` drawn with `DrawOutline(1)`;
  `CC/NetplayMenuButton.cs:52-70`):

| button | pos / tweenFrom | title / subtitle | icon (MenuAtlas) | confirm |
|---|---|---|---|---|
| quickPlay | (65,90) / (-160,120) | `QUICK PLAY` / `2-4 ARCHERS` | `GoldenArrow` | State = QuickPlaySearch |
| lobbyList | (175,90) / (160,-80) | `LOBBIES` / `BROWSE GAMES` | `browseWorkshopButton` | State = LobbyBrowser |
| privateMatch | (265,90) / (560,120) | `PRIVATE` / `INVITE ONLY` | `map/towerLock` | State = PrivateSelect |

- Links: quick.Right=lobbies, lobbies.Left=quick, lobbies.Right=private, private.Left=lobbies (no up/down). First selected:
  quickPlay (`MMP:781`). For a v1 without quick play, open question whether to keep the button.
- `VersusSelectBanner` at (160,185), depth -1000 (`MMP:1081-1087`), tracking the selected button and showing its title +
  two lines (`MMP:90-98`): QUICK PLAY "JUMP INTO A RANDOM MATCH." / "CLOSEST OPPONENTS FIRST."; LOBBIES "BROWSE PUBLIC LOBBIES" /
  "OR CREATE YOUR OWN."; PRIVATE "LOCKED LOBBY, JOINABLE" / "ONLY WITH A CODE.".
- `TweenBGCameraToY(1)`.

`VersusSelectBanner` (`CC/VersusSelectBanner.cs`): a `MenuItem` that is never selectable; per entry alpha approaches 1/0 at
0.08 per frame; draws `Draw.TextureBannerV(MenuAtlas["questResults/tipBanner"], pos, (100,37), ...)` (animated waving),
title via `Draw.OutlineTextCentered` in `#FFDC6B` at y-25, lines in white every 8 px (`:57-74`).

No Destroy handler: the vanilla MainMenu removes menu items on state change.

### 2.3 LobbyBrowser (59)

Create: `HandleLobbyBrowser` -> `TFGameExtensions.ResetVersusChoices()`, `CreateLobbyBrowser` (`MMP:1230-1265`, `MMP:1290-1330`):
`ApplyNetplayMode`, `DisableUnauthorized`, builds the variant menu once (`MatchVariants.BuildMenu`, kept in a static list for
LobbyBuilder), Loader `"FINDING LOBBIES..."`, `BackState = NetplaySelect`, `ResetLobby`, `DisableAllControllers`,
`ResetLobbies`, then **`IMatchmakingService.GetLobbies(OnRetrieveSuccess, onFail)`**. Fail: remove loader, notification
`"Failed to retrieve lobbies..."`, enable controllers.

`OnRetrieveSuccess` (`MMP:1409-1557`): button guides A `JOIN`, B `RETURN`, Start `CREATE`, Alt `REFRESH` (guide slots A-D,
`MMP:1426-1429`), plus an extra guide entity `SPECTATE` on Alt2 (`MMP:1430-1437`).
- For each lobby: compute `CanJoin` (WiderSet compatibility; `"WIDERSET MOD ON OFF REQUIRED"`; missing custom variants ->
  `"MISSING CUSTOM VARIANTS"`, `MMP:1460-1493`), create `LobbyInfos(self, (5, 50 + 35*i), lobby, confirmAction, lobbyPanel)`,
  link Up/Down in list order, select the first (`MMP:1524-1537`). One shared `LobbyPanel(225,110)`. Added to the UI layer;
  `MaxUICameraY` = list bottom.
- No lobbies: `Text("NO LOBBIES FOUND")` at (100,100) (`MMP:1550-1556`).
- Later refreshes with the same count only update each `LobbyInfos.Lobby` in place (`MMP:1545-1549`).

Inputs while in this state (`MainMenu.Update` prefix, `MMP:507-573`):
- **Start** -> `State = LobbyBuilder` (create a public lobby).
- **Alt** (3 s cooldown) -> refresh: Loader `"FINDING LOBBIES..."`, remove list + panel, `ResetLobbies`, `GetLobbies(...)`.
- **Alt2** -> spectate selected lobby (out of v1 scope): `JoinLobby(roomId, isPlayer:false, ...)`, loader
  `"JOINING LOBBY AS SPECTATOR"`; nothing selected -> notification `"You must select a lobby to spectate!"`.
- **Confirm on a LobbyInfos** (`MMP:1494-1523`): in game/series -> `"MATCH IN PROGRESS, SPECTATE ONLY"` /
  `"SERIES IN PROGRESS, SPECTATE ONLY"`; !CanJoin -> `"CAN'T JOIN - MISSING VARIANTS: A, B +N MORE"` (fitted to 290 px,
  `MMP:1267-1288`) or `"CAN'T JOIN - <reason>"`; else disable controllers, Loader `"JOINING LOBBY"`,
  **`JoinLobby(roomId, isPlayer:true, OnJoinSuccess, OnFailedToJoinLobby)`**.
- Back: vanilla, to BackState NetplaySelect.

`OnJoinSuccess` (`MMP:1637-1692`): remove loader; `NetplayManager.SetRoomAndServerMode(Server + "/room/" + roomId + "?peer=" +
roomPeerId, isHost:false)`, `UpdatePlayer2Name(host name)`; `UpdateOwnLobby(lobby)`; `EnableAllControllers` +
`DisableAllControllerExceptLocal`; publish own `CustomVariants`/`UseInstantReplay` via `UpdatePlayer`; `StateApi.SetSeed(seed)`,
`Variants.ApplyVariants(lobby variants)`, `MatchLength`; `State = Rollcall (5)`; `NotifyOnVersionMismatch` (notification
`"<MOD> v (HOST v) - MAY NOT WORK"`).
`OnFailedToJoinLobby` (`MMP:1710-1728`): notification `"Failed to join lobby: either full, gone, or already started"` (15/450),
remove the list (user must refresh).

Destroy (`MMP:1232-1259`): remove all LobbyInfos, panel, "no lobbies" text, spectate guide.

**LobbyInfos** (`CC/LobbyInfos.cs`), subclass of vanilla `MenuItem`: one `Image(MenuAtlas["ascension/slabTop"])` scaled (0.4,0.6),
origin y centered, tint white (or `#B49B3C` if in game/series), lerping to `#FFDA9B` when selected (`:67-71`, `:98-134`); text
= lobby name upper-cased up to the first '.', `Draw.TextRight` at Position + (100,0) in WhiteSmoke (`:136-145`). Tween in from
x-100 (20 frames CubeOut); selected slides +15 x. `OnSelect` scrolls the UI camera to `Y-200`, moves the panel along, and calls
`panel.UpdateInfo(Lobby)` (`:191-245`). `OnConfirm` (once) plays ui_click and runs the confirm action (`:181-189`).

**LobbyPanel** (`CC/LobbyPanel.cs`), plain `Entity` (layer -1), drawn only in LobbyBrowser (`:56-73`): map block+icon via vanilla
`VersusGraphics.GetVersusGraphics(mapId)` or `MapButton.InitRandomVersusGraphics()` at y-60; map name `OutlineText` scale 1.4 at
y-38 (`"RANDOM"` or tower theme name); mode text at y-30 (`LAST MAN STANDING` / `HEADHUNTERS` / `TEAM DEATHMATCH`, colours
`QuestDifficultySelect.Legendary/Hardcore/NormalColor`); variant icons in rows of 5, 20 px apart from (-40,0); match length
name (`INSTANT/QUICK/STANDARD/EPIC MATCH`, `#3CBCFC`) at y-16; red can't-join reason (scale 1.3); yellow
`"LIVE, SPECTATE ONLY"`; gold `"BEST OF N SERIES"` (`:120-331`).

### 2.4 PrivateSelect (63)

Create: `CreatePrivateSelect` (`MMP:785-818`): `BackState = NetplaySelect`; two NetplayMenuButtons:

| button | pos / tweenFrom | title / subtitle | icon | confirm |
|---|---|---|---|---|
| create | (100,90) / (-160,120) | `CREATE` / `HOST A LOBBY` | `editorQuill` | `LobbyBuilderContext.IsPrivate = true`; State = LobbyBuilder |
| join | (220,90) / (560,120) | `JOIN` / `ENTER A CODE` | `controls/keyboard/icon` | State = PrivateJoinCode |

Links create.Right=join, join.Left=create; first = create. Banner lines: CREATE "HOST A PRIVATE LOBBY AND" / "SHARE ITS CODE WITH
FRIENDS."; JOIN "ENTER A CODE TO JOIN" / "A FRIEND'S PRIVATE LOBBY." (`MMP:96-98`).

### 2.5 PrivateJoinCode (64)

Create: `CreatePrivateJoinCode` (`MMP:905-946`): `BackState = PrivateSelect`; `JoinCodeEntry((160,105), onSubmit, ReadClipboard)`
selected first; a `StaticOptionsButton("JOIN AS")` (TF.EX nested subclass of vanilla `OptionsButton` that is always Selected
and only wiggles on select, `MMP:47-64`) at y 155 toggling `PLAYER` / `SPECT` with Left/Right; guides A `JOIN`, B `RETURN`,
Alt `PASTE`.

**JoinCodeEntry** (`CC/JoinCodeEntry.cs`), subclass of `MenuItem`: 5-char code, alphabet A-Z minus I and O, digits 2-9
(`:151-162`). Input is **keyboard only** (`Keyboard.GetState()` new presses: letters, digits, numpad, Backspace, Ctrl+V;
`:32-124`) or **Alt = paste from clipboard** (`:53-56`, `:137-149`). Confirm -> `Submit` (needs 5 chars, else ui_invalid).
Draws `"ENTER CODE"` (#FFDC6B) at y-30, five slots `*` (entered, white) or `_` (gray) at scale 2, 16 px apart, and `"TYPE OR
PASTE THE CODE"` (gray) at y+30 (`:181-202`).

Submit -> `SubmitJoinCode` (`MMP:971-1012`): 3 s cooldown; disable controllers; Loader `"JOINING LOBBY"` (or `"... AS
SPECTATOR"`); **`JoinPrivate(code, asPlayer, onSuccess(lobby), onFail)`**. Success -> `EvaluateLobbyCompatibility`
(`MMP:1014-1041`); compatible -> `OnJoinSuccess` (as 2.3); incompatible -> `LeaveLobby`, notification with the reason. Fail ->
`"Invalid or expired code"`.

**Gamepad-only:** there is no way to type a code; only Alt pastes whatever is on the system clipboard. A Switch port needs
its own entry method (software keyboard applet or an on-screen character picker). Desktop-only.

### 2.6 LobbyBuilder (60): create / edit lobby settings

Create: `HandleLobbyBuilder` -> `CreateLobbyBuilder` (`MMP:1089-1228`).
- `BackState`: editing -> Rollcall (5); private create -> PrivateSelect; public create -> LobbyBrowser (`MMP:1172`).
- Writes straight into `matchmaking.GetOwnLobby()` (`LobbyBorderButton.ownLobby`, `CC/LobbyBorderButton.cs:15`).
- Items (all `LobbyBorderButton` = vanilla `BorderButton` + an icon `Wiggler` + "scroll the UI camera to Y-200 when selected";
  `CC/LobbyBorderButton.cs:9-37`). Each draws its own text over `BorderButton.Render` and toggles `DrawLeft/DrawRight` arrows;
  Left/Right change the value with ui_move2 and call `BorderButton.OnConfirm` (the wiggle).

| item (pos / tweenFrom, size) | value / text | rule |
|---|---|---|
| `LobbyVersusModeButton` (160,90)/(-100,90), 200x30 | mode icon `VersusModeButton.GetModeIcon` at y-20, `"GAME MODE:"` at y-5, `VersusModeButton.GetModeName` scale 2 at y+4 | modes array is a static initialised blob (likely LMS 3, HH 4, TDM 5); series lobby forces one mode; picking TDM raises MaxPlayers to >= 3 (`CC/LobbyVersusModeButton.cs:27-113`) |
| `LobbyVersusMapButton` (160,130)/(-100,210), 200x30 | map block+icon (VersusGraphics), name `OutlineText` scale 2 at y+20 | MapId -1 (random) .. count(`NETPLAY_SAFE_MAP`)-1; plays `MapButton.PlayTowerSound`; series forces -1 (`CC/LobbyVersusMapButton.cs:30-153`) |
| `LobbyVersusPlayerCountButton` (160,190)/(420,105), 168x34 | `"LOBBY SIZE"` scale 2, `"N ARCHERS"` | 2..4, min 3 for TDM, min = current players when editing (`CC/LobbyVersusPlayerCountButton.cs:15-96`) |
| `LobbyVersusCoinButton` (160,220)/(420,135), 168x34 | length name scale 2, `"PLAY TO: <coin> x N"` | MatchLength 0..3; N = goal for mode/players x {0.01,0.5,1,2}, ceil (`CC/LobbyVersusCoinButton.cs:28-135`) |
| `LobbyVersusSeriesButton` (160,250)/(420,165), 168x34 | `"SERIES"`, `"OFF"` / `"BEST OF N - 1V1/2V2"` / `"OFF (SET AT CREATION ONLY)"` | BestOf {0,1,3,5,7}; locked when editing; `EnforceSeriesRules` (`CC/LobbyVersusSeriesButton.cs:16-112`) |
| `LobbyVarianText` (160,280)/(-120,165) | `"VARIANTS"` scale 2, `#3CBCFC` | label only (`CC/LobbyVarianText.cs:56-62`) |
| vanilla variant items from `MatchVariants.BuildMenu`, each moved +260 y | vanilla variant toggles | `MaxUICameraY = variantsMaxY + 290` |

- Links: mode<->map<->size<->coin<->series vertically; series.Down = variants[0]; variants[0..3].Up = series (`MMP:1198-1210`).
  First selected: mode button. `TweenBGCameraToY(3)`. Plays `ui_startGame`.
- Guides: A `VARIANT SELECT`, B `CANCEL`, plus a guide entity on slot 4 with Start = `CREATE LOBBY` / `CREATE PRIVATE LOBBY` /
  `UPDATE LOBBY` (`MMP:1212-1226`).

**Start** (`MainMenu.Update` prefix, `MMP:574-648`):
- Editing (`LobbyBuilderContext.IsEditing`): if changed, set variants + mods, then
  **`UpdateLobbySettings(maxPlayers, gameData, mods)`**; `EndEdit`; `State = 5`, `BackState = NetplaySelect`,
  `RequestRollcallReconcile()`.
- Creating: `RoomId = Guid`, `Name = NetplayPreferences.Name`, `Kind = Private|Standard`, add own `Player{Name, IsHost=true,
  CustomVariants, UseInstantReplay, HaveModifiedGameFiles, Platform}`, WiderSet mod entry; Loader `"CREATING LOBBY..."`; disable controllers; **`CreateLobby(onSuccess, onFail)`**. Success: enable controllers +
  `DisableAllControllerExceptLocal`, `State = 5`, `BackState = NetplaySelect`, **`NetplayManager.SetRoomAndServerMode(Server +
  "/room/" + roomId + "?peer=" + peerId, isHost:true)`**, `StateApi.SetSeed(lobby seed)`, MatchLength. Fail: notification
  `"Failed to create lobby"`, State back to PrivateSelect or LobbyBrowser.
- Destroy (`MMP:1091-1144`): when editing, `CancelEdit` + reapply lobby variants + `RequestRollcallReconcile`; remove all items.

### 2.7 Lobby screen = vanilla Rollcall (5) in a lobby

Same vanilla archer-select screen (`RollcallElement` per seat, `ReadyBanner`), steered by patches. "In a lobby" = versus mode is
netplay and `GetOwnLobby()` is not empty. Global effects: `MainMenu.NoGamepadUpdates = true` while a lobby is held
(`MMP:475-487`); `MainMenu.Update` prefix also calls `ReconcileRollcallIfPending()` and `ShowPendingSpectatorNoticeIfAny()`
every frame (`MMP:493-501`); `InputDelayAdvisor.Update` (`MMP:502`).

Seat ownership. Local seat = own player's `Seat` (`MS:1567-1570`). Other seats' input is suppressed (`XGP` `IsForeignSeat`,
`:113-125`; all Menu* getters return false for foreign seats).

Per `RollcallElement` (`MI/RollCallElementPatch.cs`):
- `Update` prefix (`:329-355`): forces `input = TFGame.PlayerInputs[seat]`; local seat -> team pick with Up (BLUE) / Down (RED)
  in team mode -> `UpdatePlayer` (`:448-489`); remote seats -> controller icon set to `controls/xb360/player{n}`, A/RT buttons
  (`:588-602`), portrait refreshed from lobby archer (`:357-420`); open seats (seat < MaxPlayers, nobody there) show a vanilla
  `ChaosOrb` sprite in place of the controller icon (`:422-446`).
- `NotJoinedUpdate` prefix (`:140-184`): a seat taken by someone else stays not-joined (result 0); local Confirm is refused with
  notification `"MISSING <variant> +N MORE"` if the lobby uses variants we lack; local Back is swallowed (result 0).
  Postfix (`:289-327`): local Back while not joined -> **`LeaveLobby`** (sync), then `State = LobbyBrowser` (fail -> 7). This is
  the lobby screen's "leave" (also for private lobbies).
- `EnterJoined` postfix (`:236-287`): local -> `player.Ready = true`, ArcherIndex/AltIndex/custom archer id, variants, instant
  replay, archer mods; `ArcherService.AddArcher(seat, player)`; custom skin publish; disable controllers ->
  **`UpdatePlayer(player, enable, fail "Failed to notify server")`**. Remote -> `UpdatePlayerUI` (portrait join, `Players[seat]=true`).
- `LeaveJoined` postfix (`:87-138`): local Back while joined -> `Ready = false`, `RemoveArcher`, `UpdatePlayer`; remote ->
  portrait leave.
- `JoinedUpdate` prefix/postfix (`:186-220`): remote seats stay joined (result 1).
- `ForceStart` prefix: disabled while in a lobby (`:491-496`).
- `StartVersus` prefix clears the console; postfix (`:506-526`): `AddSpectators`, `UpdateNumPlayers(players)`,
  `MainMenu.FadeAction = MainMenu.GotoVersusLevelSelect`, `State = Fade (4)`.
- `Render` postfix (`:27-85`): `"SPECTATORS : N"` at (40,20); player name under the portrait for remote seats (or always with
  WiderSet) at Position+(0,105), archer ColorA (ColorB when joined); team label `BLUE`/`RED`/`UP/DOWN` at +(0,114)
  (`MI/RollcallLayout.cs`).

Remote seat state is driven by the server: every `LobbyUpdate` -> `HandleLobbyUpdate` on the game thread (`MS:646-663`,
`MS:817-932`) -> `pendingRollcallLobby` -> `ReconcileRollcall` (`MS:1188-1276`): joins/leaves each remote seat's
RollcallElement (state 1/0, portrait Join/Leave, `TFGame.Characters/AltSelect`, `ArcherService.Add/RemoveArcher`),
notifications `"<name> joined"`, `"Player left"`, modified-files / other-platform warnings. `UnreadyOwnRollcall`
(`MS:1114-1143`) drops our own join if the server un-readied us. Host gone -> State = NetplaySelect (`MS:856-873`).
`PublishSettingsChanges` (`MS:995-1063`) pushes `LobbyUpdateFeed` rows (`"LOBBY SETTINGS UPDATED"`, map, `"LOBBY SIZE N"`,
length, mode, variants on/off) to joiners.

Ready / start:
- `ReadyBanner.GetReadyState` postfix: true only if `IsLobbyReady() || CanHostStart()` (`MI/ReadyBannerPatch.cs:12-25`).
  `CanHostStart` = not quick play, all players (>= 2) ready, teams playable, start not yet sent, and we are host
  (`MS:1614-1678`).
- Host presses **Confirm or Start** while joined and `CanHostStart` -> **`RequestStart()`** (cooldown) -> `SendStartLobbyChoice`
  (`XGP:362-366`, `XGP:420-428`, `MS:1646-1654`). Server broadcasts `StartLobby` -> `hasHostStartedMatch = true` (`MS:702-705`)
  -> `IsLobbyReady()` true -> Confirm/Start pass through (`XGP:343-346`) -> vanilla rollcall start -> `StartVersus` (above).
- Joiners while all ready and waiting: `"WAITING FOR HOST"` on `portraits/readyBanner` at (160,225) (`MMP:1390-1407`).
- Host with own seat **not joined** presses Start -> `TryOpenLobbyEditor` -> LobbyBuilder in edit mode (`XGP:415-418`,
  `MMP:429-448`); guide `"EDIT LOBBY"` (Start, slot 1) is visible only then (`MMP:387-427`).
- Private lobby: guide `"COPY CODE"` (Alt2, slot 0) (`MMP:1043-1066`); Alt2 tap (not hold) -> `ClipboardService.SetText(JoinCode)`,
  notification `"CODE COPIED"` (`MMP:503-506`, `MMP:1368-1388`). **The join code is never drawn on screen**: on a Switch
  (no clipboard) it must be displayed. Desktop-only.
- Overlays drawn by `MainMenu.Render` postfix in state 5 (`MMP:258-372`): ping `"N MS"` / `"... MS"` coloured by connection
  quality above each remote portrait (Position+(0,-66)); `InputDelayAdvisor.Render` (input-delay slider top right, Alt2 hold +
  Left/Right adjusts, hold 1.5 s resets, mouse drag on desktop; `TF.EX.Patchs/TF.EX.Patchs/InputDelayAdvisor.cs`).
- `LobbyUpdateFeed` (`CC/LobbyUpdateFeed.cs`): left-edge bubbles (`variants/bubbleEdge`/`bubbleMiddle`, tinted) from y 44,
  18 px rows, slide in, 6 s life, max 10; only drawn in state 5.

Versus options patches that matter only if the vanilla VersusOptions screen is reachable in netplay: `VersusModeButton.Update`
pre/postfix keeps the mode from leaving netplay (`MI/VersusModeButtonPatch.cs`), coin/variants buttons hidden
(`MI/VersusCoinButtonPatch.cs`, `MI/VersusVariantButtonPatch.cs`), `VersusBeginButton.OnConfirm` -> NetplaySelect and its ctor
leaves any lobby (`MI/VersusBeginButtonPatch.cs`), `MenuItem.Update` up/down skip (`MI/MenuItemPatch.cs`). Not on the v1 path.

### 2.8 Waiting, errors, notifications

- **Loader**: `Scene.AddLoader(msg, withFade)` adds one vanilla `Loader(true)` (depth -1000) and sets the static
  `Loader.Message`; `RemoveLoader` removes it and any `Fader` (`TF.EX.Domain/TF.EX.Domain.Extensions/TowerFallSceneExtensions.cs:67-106`).
  In netplay the `Loader.TweenIn` prefix slides it from (160,280) to (160,120) over 12 frames (`MI/LoaderPatch.cs:13-35`).
  Messages: `CHECKING VERSION`, `FINDING LOBBIES...`, `CREATING LOBBY...`, `JOINING LOBBY`, `JOINING LOBBY AS SPECTATOR`.
- **Fader** (`CC/Fader.cs`): full-screen black rect, alpha 0 -> 0.4 over 50 frames.
- **Notification** (`CC/Notification.cs`): text upper-cased; a bubble of `variants/bubbleEdge` + n x `bubbleMiddle` (10 px) +
  flipped edge, black `Draw.TextCentered` text; slides in from the left to centre x 160, y 10 + UI camera Y, stacks 16 px per
  live notification, stays `stayingDuration` frames (default 20 in / 250 stay) then slides out; same text replaces the old one;
  layer -1 in MainMenu, HUD layer 4 in a Level (`:130-154`). Sticky variant used for `"Waiting for other players..."`.
- Server connection failure in any request: `ConnectToServerAndListen` catch -> `State = NetplaySelect` (`MS:138-170`);
  `Update(...)` runs `onFail` when `EnsureConnection()` fails (`MS:1387-1391`).

### 2.9 Start -> match

`Fade(4)` -> `MainMenu.GotoVersusLevelSelect` -> vanilla `MapScene`:
- `MapScene.Begin` postfix selects the lobby's MapId (or VersusRandomSelect) and scrolls to it
  (`TF.EX.Patchs/TF.EX.Patchs.Scene/MapScenePatch.cs:63-90`); MenuConfirm is forced true in MapScene while in a lobby
  (`XGP:289-292`), Back false (`XGP:470-477`) -> the map is confirmed immediately.
- `GetRandomVersusTower` postfix: shuffle netplay-safe towers with `new Random(StateApi.GetSeed())` (`MapScenePatch.cs:92-134`).
- `StartSession` prefix (`:20-44`): `NetplayOptions.Apply`, `RestoreArchersFromLobbyIfNeeded`, `NetplayManager.UpdatePlayers`,
  `UpdateNumPlayers`, `ArcherService.CompactSeatsToHandles` + `ApplyToGame`, `ApplyTeamsToMatchSettings`. Postfix (`:46-61`):
  `StateApi.ResetRng`, `InputService.EnsureRemoteController(max(2,players))`, `EnsureEveryControllerSlot`, `StateApi.PurgeCache`.
- In the Level: waiting for sync, the Level does not update (`LevelPatch.Level_Update__Prefix`, `IsAwaitingSynchronization`);
  20 s without sync -> abort -> `GoToNetplayEntryMenu` + notification `"Connection to the other players failed"` (`NM:262-288`,
  `TGP:205-226`). Later unrecoverable errors -> `"CONNECTION LOST"` / `"NETPLAY ERROR"` back to NetplaySelect (`NM:523-554`).
- In-match Start opens TF.EX's `LightPauseMenu` (`RESUME`/`QUIT`, "GAME KEEPS RUNNING"; QUIT leaves the lobby and goes to
  NetplaySelect) instead of the vanilla pause (`LevelPatch.cs:147-170`, `CC/LightPauseMenu.cs`).

### 2.10 End of match

- `Session.GotoNextRound` prefix: when the match has a winner, hook round results to match results, freeze, play victory music,
  skip (`TF.EX.Patchs/TF.EX.Patchs/SessionPatch.cs:20-50`); `Session.CreateResults` prefix reuses an existing
  `VersusMatchResults` (`:52-67`).
- `HUD.Update` postfix (`TF.EX.Patchs/TF.EX.Patchs.Entity.HUD/HUDPatch.cs:19-65`): when `VersusMatchResults` is finished (once):
  `PlayerInput.AssignInputs`, `MenuInput.UpdateInputs`, `InputService.RebindLocalInput`, **`NotifyMatchEnded(winnerSeat,
  seatScores)`** (sends `MatchEnded` once), then `MatchEndChoices.Create(level)`. (In test mode it also exports the replay and
  resets the NetplayManager here.)
- Vanilla `PauseMenu` of type VersusMatchEnd (items `REMATCH!`, `MATCH SETTINGS`, `ARCHER SELECT`, `QUIT`;
  `docs/reference/tf8-decompile/TowerFall/PauseMenu.cs:94-99`) with patches (`TF.EX.Patchs/TF.EX.Patchs.Entity/PauseMenuPatch.cs`):
  - `AddItem` prefix: drop `MATCH SETTINGS`; series: drop `ARCHER SELECT`, rename `REMATCH!` -> `CONTINUE`; lobby empty: drop
    REMATCH/ARCHER SELECT (`:161-202`). `Update` postfix: spectators / empty lobby keep only the last item (QUIT) (`:35-69`).
  - `VersusRematch` prefix: vote once -> disable controllers -> **`RematchChoice()`** (series: `SeriesContinueChoice()`) ->
    sticky notification `"Waiting for other players..."` (`:118-159`). Empty lobby -> `"All players left..."`.
  - `VersusArcherSelect` prefix: vote once -> **`ArcherSelectChoice()`** -> same notification (`:83-116`).
  - `Quit` / `VersusMatchSettingsAndSave` prefix: `QuitToNetplayEntry` = `LeaveLobby` (async) + `level.GoToNetplayEntryMenu()`
    (new `MainMenu(NetplaySelect)`) + enable controllers + `RebindLocalInput` (`:71-81`, `:204-234`).
  - After voting, all menu inputs are locked (`ResultsVoteLock` -> `PauseMenuPatch.IsVotePending`, used in `XGP`).
- **MatchEndChoices** (`CC/MatchEndChoices.cs`), an `Entity` on HUD layer 4, depth -2000000: `"PICK IN N"` (30 s countdown,
  red at <= 5) at (2,32); one line per player `"P<seat+1> REMATCH|READY|ARCHERS|..."` in `ArcherData.GetColorA(seat)` every
  10 px; spectators get `"YOU CAN KEEP"` / `"SPECTATING THE PLAYERS"`. Text is `Draw.TextJustify` drawn 8 times in black around
  for the outline (`:50-129`). Data: `GetEndGameStatus()` = players by seat + their vote from `lobby.EndGameChoice`
  (`MS:1779-1792`). The countdown is local and cosmetic; the server decides.
- Server outcome (`MS:680-738`): `RematchLobby` -> (reset NetplayManager, export replay) `Engine.Scene = new MapScene(Versus)`
  (same archers, straight to map / match); `ArcherSelectLobby` -> `ReturnToMenu(Rollcall)` = reset + `new MainMenu(5)` +
  `ArcherService.Reset()` (`MS:741-761`); `SeriesLobby` -> SeriesLobby state. All players left during a level ->
  `"All players left..."`, lobby dropped (`MS:843-855`); host left -> `"Host left the game..."` (`MS:865-871`).
- `TFGame.OnSceneTransition` postfix, Level -> MainMenu (`TGP:83-138`): `StateApi.ResetRngOverride`, clear versus levels,
  `PurgeCache`, `NetplayManager.Reset()`; unless we land in Rollcall/SeriesLobby with a lobby: `ResetMode()` +
  `RemoveFakeControllers()` + `NetplayOptions.Restore()`; `ResetVersusChoices()`.

---

## 3. Code structure to mirror

TF.EX is three layers; the Switch port should keep the same names and seams so TF.EX code can later be dropped in.

| TF.EX assembly | role | Switch equivalent |
|---|---|---|
| `TF.EX.Core` | mod entry (`TFEXModModule`: registers the icon, the `NetplayVersusMode` game mode, services, patches), `NetplayRoundLogic`, console commands | exlaunch init + our "mode 7" registration |
| `TF.EX.Domain` | services (`MatchmakingService`, `NetplayManager`, `ArcherService`, `InputService`, `ReplayService`), models (`Lobby`, `Player`, `GameData`, `GGRSConfig`, `MenuState`), custom components (section 2), `ServiceCollections` (static DI: `ResolveMatchmakingService()` etc.) | C++ singletons with the same method names |
| `TF.EX.Patchs` | Harmony patches on vanilla classes | exlaunch hooks on the same vanilla methods |
| `TF.State` (separate mod, `StateApi.Current`) | rollback state capture/restore, RNG, SFX sync | `switch-online/` state work (ROLLBACK.md, GAMEPLAY-RNG-PORT.md) |
| native `ggrs_ffi` (`GGRSFFI.netplay_*`) | GGRS session over the server's WebRTC/WS relay | `switch-online/ggrs-switch` |

### 3.1 Services and the methods the v1 flow uses

**IMatchmakingService** (`TF.EX.Domain/TF.EX.Domain.Ports/IMatchmakingService.cs`; impl `MS`). WebSocket to
`NetplayPreferences.Server + "/ws"` (`MS:123-125`), messages are MessagePack-as-JSON with the tag as the first key
(`MS:502-505`). Callbacks are queued and run on the game thread (`RunOnGameThread` / `DrainGameThreadActions`, drained at the
start of `TFGame.Update`, `TGP:166`).
- Connection: `ConnectToServerAndListen`, `IsConnectedToServer`, `DisconnectFromServer`, `ResetPeer`, `GetRoomPeerId`.
- Browse: `GetLobbies(onSuccess,onFail)` -> `GetLobbiesResponse`; `GetLobbies()`; `ResetLobbies`.
- Own lobby: `GetOwnLobby`, `UpdateOwnLobby`, `ResetLobby`, `GetLocalSeat`, `IsSpectator`, `CanEditLobbySettings`.
- Create / join / leave: `CreateLobby` -> `CreateLobbyResponse`; `JoinLobby(roomId,isPlayer)` -> `JoinLobbyResponse`;
  `JoinPrivate(code,asPlayer)` -> `PrivateJoinResult`; `LeaveLobby` -> `LeaveLobbyResponse`.
- In lobby: `UpdatePlayer` (ready/archer/team) -> answered by `LobbyUpdate`; `UpdateLobbySettings(maxPlayers, gameData, mods)`;
  `PublishInputDelay`; `PublishCustomSkin`; `RequestRollcallReconcile` / `ReconcileRollcallIfPending`;
  `IsLobbyReady`, `CanHostStart`, `IsWaitingForHostStart`, `RequestStart` -> server `StartLobby`;
  `GetPingTo`, `GetConnectionQualityTo`, `StartOrStopPingMeasurementIfNeeded`.
- Match: `RestoreArchersFromLobbyIfNeeded`, `ApplyTeamsToMatchSettings`, `NotifyMatchEnded`, `GetEndGameStatus`,
  `RematchChoice`, `ArcherSelectChoice`, `SeriesContinueChoice` -> server `RematchLobby` / `ArcherSelectLobby` / `SeriesLobby`.
- Server push handled in `HandleMessageContents` (`MS:507-739`): `KeepAlive` (echo), `LobbyUpdate`, `StartLobby`,
  `RematchLobby`, `ArcherSelectLobby`, `SeriesLobby`, `SpectatorJoined`, `SkinChunk`, quick-play messages.
- Wire details: `switch-online/LOBBY-PROTOCOL.md` if/when written; the senders are `MS:242-449`.

**INetplayManager** (`TF.EX.Domain/TF.EX.Domain.Ports/INetplayManager.cs`; impl `NM`):
- Setup from the menu: `SetRoomAndServerMode(roomUrl + "?peer=" + peerId, isHost)` (builds `GGRSConfig.DefaultServer`),
  `UpdatePlayer2Name`, `SetSpectatorMode`, `UpdatePlayers`, `UpdateNumPlayers`, `AddSpectators`.
- Session: `Init(roundLogic)` (`NM:180-206`: frame driver "TF.EX", `GGRSConfig.Name/InputDelay/MaxInputDelay`, then
  `Connect` on a task -> `NativeInit` = `GGRSFFI.netplay_init(json(GGRSConfig))`); `IsInit`, `IsSynchronized`,
  `EstablishSessionIfSynchronized` -> `OnSessionEstablished` (`ArcherService.ApplyToGame`, spawn first round);
  `AbortIfSynchronizationFailed` (20 s), `HasFailedInitialConnection`, `ConsumeAbortToVersusOptions`, `IsDisconnected`.
- Per frame: `Poll`, `AdvanceFrame(input)`, `UpdateNetplayRequests`, `HaveRequestToHandle`, `ConsumeNetplayRequest`
  (SaveGameState / LoadGameState / AdvanceFrame), `SaveGameState(bytes)`, `LoadGameState()`, `AdvanceGameState`,
  `CanAdvanceFrame`, `IsFramesAhead`, `SetIsRollbackFrame`, `SetIsUpdating`, `GetSessionFps` (240).
- Teardown: `Reset` (`GGRSFFI.netplay_reset`, `StateApi.ResetSession/ResetSfx`; `NM:758-791`), `ResetMode`.

**GGRSConfig** (`TF.EX.Domain/TF.EX.Domain.Models/GGRSConfig.cs`): `InputDelay`, `MaxInputDelay`, `Fps = 240`, `Name`,
`Netplay { ServerConf { RoomUrl, IsHost } | LocalConf | NumPlayers }`, `Test`; passed to native as JSON.

**StateApi.Current** (`TF.EX.Domain/TF.EX.Domain.Interop/ITfStateApi.cs`, provided by TF.State): used in the flow:
`SetSeed/GetSeed`, `ResetRng`, `ResetRngOverride`, `PurgeCache`, `CaptureGameState`, `RestoreGameStateBytes`,
`SetCurrentFrame`, `SetFrameDriver`, `SynchronizeSfx`, `ResetSession`, `SetSessionRoundStarted`, `SetInstantReplay`,
`SetVersusLevels`.

Also: `IArcherService` (`AddArcher/RemoveArcher/Reset/ApplyToGame/CompactSeatsToHandles/GetArchers`), `IInputService`
(`EnableAllControllers/DisableAllControllers/DisableAllControllerExceptLocal/EnsureRemoteController(s)/EnsureEveryControllerSlot/
EnsureLocalControllerSeat/RebindLocalInput/GetPolledInput/UpdatePolledInput/GetCurrentInput/GetInputIndex/GetLocalPlayerInputIndex/
ObserveLocalDevice/RemoveFakeControllers`), `IReplayService` (`Initialize/AddRecord/Export/Reset`), `IAutoUpdater` (version check),
statics `LobbyBuilderContext` (`IsPrivate`, `IsEditing`, `BeginEdit/EndEdit/CancelEdit/HasChanges`), `NetplayPreferences`
(`Server`, `Name`, `IsOfficialServer`), `NetplayOptions` (`Apply/Restore`), `MenuReturn.NetplayEntry`,
`VersusModeExtensions` (`ApplyNetplayMode`, `ClearNetplayMode`, `IsNetplay`), `NetplayVersusMode` / `NetplayRoundLogic`
(game mode registered through FortRise; `OnLevelLoadFinish` calls `NetplayManager.Init`, `NetplayRoundLogic.cs:53-75`).

### 3.2 Harmony patch points (vanilla class.method -> kind -> effect), v1 flow

| vanilla target | kind | TF.EX patch | effect |
|---|---|---|---|
| `MainMenu.CreateMain` | postfix | `MMP:231` | NETPLAY + compact co-op buttons, row shift, links |
| `MainMenu.CallStateFunc` | prefix (+postfix) | `MMP:140`, `MMP:374` | Create/Destroy of custom states; lobby guides |
| `MainMenu.set_State` | prefix | `MMP:194` | PressStart->Main detour to Changelog (not v1) |
| `MainMenu.Update` | prefix | `MMP:463` | NoGamepadUpdates, reconcile, LobbyBrowser Start/Alt/Alt2, LobbyBuilder Start, copy code |
| `MainMenu.Render` | postfix | `MMP:258` | pings, WAITING FOR HOST, input delay, quick-play counters |
| `MainMenu.CreateVersusOptions` | postfix | `MMP:450` | select mode button in netplay (not v1) |
| `MainMenu..ctor` | postfix | `MMP:251` | console enabled |
| `ScreenTitle..ctor` / `ChangeState` | prefix | `ScreenTitlePatch.cs:12,22` | title for custom states |
| `Loader.TweenIn` | prefix | `MI/LoaderPatch.cs:13` | loader slide position |
| `FightButton.MenuAction` | postfix | `MI/FightButtonPatch.cs:11` | leave netplay mode |
| `RollcallElement.Update` | prefix | `MI/RollCallElementPatch.cs:329` | input binding, team pick, remote icons, open-seat orb |
| `RollcallElement.NotJoinedUpdate` | prefix + postfix | `:140`, `:289` | seat ownership, missing variants, Back = LeaveLobby |
| `RollcallElement.JoinedUpdate` | prefix + postfix | `:186`, `:204` | remote seats stay joined |
| `RollcallElement.EnterJoined` / `LeaveJoined` | postfix | `:236`, `:87` | ready/unready -> UpdatePlayer |
| `RollcallElement.ForceStart` | prefix | `:491` | disabled in lobby |
| `RollcallElement.StartVersus` | prefix + postfix | `:498`, `:506` | spectators, num players, Fade -> level select |
| `RollcallElement.Render` | postfix | `:27` | names, teams, spectator count |
| `RollcallElement..ctor` | prefix + finalizer | `:222`, `:229` | `SkinSlot` scope (custom skins) |
| `ReadyBanner.GetReadyState` | postfix | `MI/ReadyBannerPatch.cs:12` | ready only when lobby ready / host can start |
| `XGamepadInput.get_Menu*`, `GetState`, `get_Name` | postfix | `XGP` (also `KeyboardInputPatch.cs`) | foreign-seat lock, vote lock, host start, MapScene auto-confirm, netplay inputs in Level |
| `PlayerInput.AssignInputs` | prefix | `PlayerInputPatch.cs:15` | keep netplay input mapping |
| `MapScene.Begin` | postfix | `MapScenePatch.cs:63` | preselect lobby map |
| `MapScene.StartSession` | prefix + postfix | `:20`, `:46` | archers/teams/players from lobby; RNG/controllers |
| `MapScene.GetRandomVersusTower` | postfix | `:92` | seeded safe-tower pick |
| `Session.StartGame` / `StartRound` | prefix / postfix | `SessionPatch.cs:93`, `:108` | instant replay flag, round started |
| `Session.GotoNextRound` / `CreateResults` | prefix | `:20`, `:52` | end-of-match freeze/results |
| `Level..ctor` | postfix | `LevelPatch.cs:50` | drop vanilla ReplayRecorder |
| `Level.Update` | prefix + postfix | `:69`, `:110` | hold until synchronized; TimeMult for 240 Hz; SFX sync; skip loader |
| `Level.HandlePausing` | prefix (x2) | `:39`, `:147` | controller slots; LightPauseMenu |
| `TFGame.Update` | prefix (+reverse patch of original) | `TGP:155` | the netplay frame loop (3.3) |
| `TFGame.OnSceneTransition` | postfix | `TGP:83` | Level->MainMenu cleanup |
| `TFGame.Load` / `OnExiting` | postfix / prefix | `TGP:140`, `TGP:71` | clock reset; replay export |
| `HUD.Update` | postfix | `HUDPatch.cs:19` | NotifyMatchEnded + MatchEndChoices |
| `PauseMenu.AddItem/Update/VersusRematch/VersusArcherSelect/Quit/VersusMatchSettingsAndSave` | prefix/postfix | `PauseMenuPatch.cs` | end-of-match votes and quit |
| `MatchVariants..ctor` / `BuildMenu`, `VariantToggle.*`, `MatchSettings.PlayerGoals/CleanSettingsVersus` | various | `TF.EX.Patchs/TF.EX.Patchs/*` | allowed variants, goals |

(TF.State adds ~90 more patches for rollback state; out of this spec.)

### 3.3 Call graph: NETPLAY confirmed -> match -> back to menu

```
NetplayButton.MenuAction
  -> TFGamePatch.RequestNetplayEntry(menu, enter)      [IAutoUpdater.CheckForUpdate if official server]
     -> ResolveNetplayEntry -> enter(): MainMenu.State = NetplaySelect(62)
MainMenu.CallStateFunc("Create", 62) -> MainMenuPatch.CreateNetplaySelect
  VersusMatchSettings.ApplyNetplayMode; Matchmaking.ResetLobby/ResetPeer; InputService.EnableAllControllers
LOBBIES -> State 59 -> CreateLobbyBrowser -> Matchmaking.GetLobbies -> [ws GetLobbies / GetLobbiesResponse] -> OnRetrieveSuccess
  A on LobbyInfos -> Matchmaking.JoinLobby(roomId,true) -> [JoinLobby / JoinLobbyResponse] -> OnJoinSuccess
     NetplayManager.SetRoomAndServerMode(url,isHost:false); Matchmaking.UpdateOwnLobby; UpdatePlayer; StateApi.SetSeed; State=5
  START -> State 60 -> CreateLobbyBuilder ; START -> Matchmaking.CreateLobby -> [CreateLobby / CreateLobbyResponse]
     NetplayManager.SetRoomAndServerMode(url,isHost:true); StateApi.SetSeed; State=5
Rollcall(5): A on own seat -> RollcallElement.EnterJoined postfix -> ArcherService.AddArcher; Matchmaking.UpdatePlayer(Ready)
  [server LobbyUpdate] -> HandleLobbyUpdate -> pendingRollcallLobby -> ReconcileRollcallIfPending (MainMenu.Update prefix)
  host A/START with CanHostStart -> Matchmaking.RequestStart -> [StartLobby] -> hasHostStartedMatch
  -> ReadyBanner ready, Confirm passes -> vanilla rollcall start -> RollcallElement.StartVersus postfix
     NetplayManager.AddSpectators/UpdateNumPlayers; FadeAction = GotoVersusLevelSelect; State = Fade(4)
MapScene.Begin postfix (select lobby map) ; forced Confirm -> MapScene.StartSession
  prefix: Matchmaking.RestoreArchersFromLobbyIfNeeded; NetplayManager.UpdatePlayers/UpdateNumPlayers;
          ArcherService.CompactSeatsToHandles/ApplyToGame; Matchmaking.ApplyTeamsToMatchSettings
  postfix: StateApi.ResetRng; InputService.EnsureRemoteController/EnsureEveryControllerSlot; StateApi.PurgeCache
  Session.StartGame prefix (instant replay) -> LevelLoaderXML -> Level
NetplayRoundLogic.OnLevelLoadFinish -> NetplayManager.Init(roundLogic) -> Connect -> GGRSFFI.netplay_init(GGRSConfig)
TFGame.Update prefix, every frame:
  Matchmaking.DrainGameThreadActions; ManageTimeStep (fixed step, 240 Hz target)
  NetplayManager.AbortIfSynchronizationFailed / HasFailedInitialConnection -> Level.GoToNetplayEntryMenu + notification
  NetplayManager.Poll; accumulator loop at 1/240 s (x1.1 when frames ahead):
    EstablishSessionIfSynchronized -> OnSessionEstablished -> ArcherService.ApplyToGame; NetplayRoundLogic.SpawnRoundPlayers
    NetplayLogic: NetplayManager.AdvanceFrame(InputService.GetPolledInput()) -> UpdateNetplayRequests ->
      SaveGameState: StateApi.CaptureGameState -> NetplayManager.SaveGameState ; ReplayService.AddRecord
      LoadGameState: NetplayManager.LoadGameState -> StateApi.RestoreGameStateBytes
      AdvanceFrame: NetplayManager.AdvanceGameState; MInput.Update; Level.Update (resimulation)
    then AdvanceGameState; scene FrameCounter = netplay_current_frame; original TFGame.Update (the live frame)
  Level.Update prefix/postfix: wait for sync; TimeMult; StateApi.SynchronizeSfx
Match end: Session.GotoNextRound/CreateResults prefixes -> VersusMatchResults
  HUD.Update postfix -> Matchmaking.NotifyMatchEnded -> [MatchEnded]; MatchEndChoices.Create
  PauseMenu(VersusMatchEnd): REMATCH! -> Matchmaking.RematchChoice | ARCHER SELECT -> ArcherSelectChoice | QUIT -> LeaveLobby + GoToNetplayEntryMenu
  [server LobbyUpdate with EndGameChoice] -> MatchEndChoices lines
  [RematchLobby]       -> NetplayManager.Reset; ReplayService.Export; Engine.Scene = new MapScene(Versus)   (loop to StartSession)
  [ArcherSelectLobby]  -> ReturnToMenu(5): NetplayManager.Reset; Engine.Scene = new MainMenu(Rollcall); ArcherService.Reset
TFGame.OnSceneTransition postfix (Level -> MainMenu): StateApi.ResetRngOverride/PurgeCache; NetplayManager.Reset;
  if not back in a lobby: NetplayManager.ResetMode; InputService.RemoveFakeControllers; NetplayOptions.Restore
Back out of the lobby: Rollcall Back on own unjoined seat -> Matchmaking.LeaveLobby -> State = LobbyBrowser
  ... Back -> NetplaySelect -> Back -> Main ; MainMenu Create(Main) -> DropServerConnectionIfAny (DisconnectFromServer)
```

---

## 4. Browser build differences (Towerfall Browser `web/Netplay/TfexPatches.cs`)

No screen layout changes. It: answers the update check from GitHub's API and refuses self-update (`:11-15`, `:167-200`);
copies lobby codes to the page clipboard as well (`:16`, `:185-191`); points TF.EX's OFFICIAL server at the browser's server and
labels it BROWSER in options (`:17-20`, `:101-146`); makes Trials and the co-op submenu leave netplay mode, as the Fight button
does (`:52-99`); exposes `EnterNetplay(mainMenu)` = what the NETPLAY button does, for `?mode=online` deep links (`:150-165`).

---

## 5. Desktop-only features and gamepad behaviour

| feature | TF.EX on desktop | gamepad-only | Switch v1 needs |
|---|---|---|---|
| typing a join code (`JoinCodeEntry`) | keyboard letters/digits, Backspace, Ctrl+V | only Alt = paste clipboard; no typing | software keyboard or on-screen picker |
| sharing a private code | Alt2 tap = copy to clipboard (`"CODE COPIED"`) | same, but clipboard of the PC | **draw the code on the lobby screen** (TF.EX never does) |
| player name (`NetplayPreferences.Name`) | typed in TF.EX options | defaults | a default (e.g. Switch user name) |
| input-delay slider | mouse drag or Alt2 hold + Left/Right | Alt2 hold + Left/Right | keep the gamepad path |
| console (`~`), F1-F5 replay keys, clipboard into console | keyboard | n/a | none |

---

## 6. Open questions

1. Quick play and series are outside v1: keep the QUICK PLAY button on NetplaySelect (disabled? hidden?) and the SERIES row in
   LobbyBuilder, or drop them? Dropping changes button positions vs TF.EX.
2. Join-code entry on the Switch: system software keyboard (swkbd applet, needs the applet from a game process) or a TF.EX-styled
   5-slot character picker driven by Up/Down/Left/Right?
3. Where the Switch shows the host's join code (TF.EX relies on the clipboard).
4. Main menu links: vanilla items that pointed at the removed CoOpButton (any BladeButton RightItem, Trials/Archives Left/Right) keep
   pointing at it in TF.EX; on Switch check the vanilla links and decide whether to repoint them (TF.EX does not).
5. Does the Switch `MainMenu` have a name-based `CallStateFunc` to hook, and will its `ScreenTitle`/`Update` tolerate state values
   59..66? If not, use spare values and keep a mapping table like `MenuStateExtensions`.
6. The `LobbyVersusModeButton.Modes` array is a compiled data blob the decompiler could not show; assumed LMS(3)/HH(4)/TDM(5) from
   `LobbyPanel` and `MatchmakingService.ModeName`; confirm against IL if mode order matters.
7. Version check (`RequestNetplayEntry`): Switch talks to our own server; skip it, or answer it so TF.EX's official-server rule
   ("only the latest TF.EX plays online") still holds?
8. WiderSet interop (wide lobbies, `"WIDERSET MOD ON OFF REQUIRED"`): ignore on Switch v1 (always standard)?
9. Spectating (Alt2 in LobbyBrowser, JOIN AS SPECT) is reachable from the v1 screens; in or out?
