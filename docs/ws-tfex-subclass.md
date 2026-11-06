# Workstream tfex-subclass: can TF.EX's components be real game subclasses on the Switch?

Branch `ws/tfex-subclass` (from `ws/tfex-base`), 2026-10-16. TFEX-BASE.md section 8 steps 1-2, section 9.

## Answer: yes

TF.EX's `Notification : Entity` (vendor/tfex/src/network/TF.EX.Domain/CustomComponent/Notification.cs, v0.19.2) runs on the
Switch as a **real Monocle.Entity subclass**: an object of its own runtime type `TF.EX.Domain.CustomComponent.Notification`,
allocated by the game's allocator, added with the game's `Layer.Add`, updated and drawn by the game's own layer loops through
its vtable, removed by the game's `UpdateEntityList` (which calls our `Removed` override). The port is line by line
(`hw-probe/exlaunch/source/program/tfex/Notification.cpp`, the C# quoted above each statement).

Evidence (Ryujinx, private slot `subclass`):
- **Main menu** (`tools/re/notif-test.sh`, hw.txt `notiftest 200 0`): the toast slides in (x -130 -> 95 = TF.EX's
  `restingX`), stays 250 frames, slides out, is removed (`_lives` 1 -> 0 through our `Removed`), and a second toast follows.
  `is Entity` (the game's IsInst +0x950) is true for it, `is MainMenu` false. A full `GC_gcollect` while it lives leaves its
  fields intact (its description string survives: the GC scans our fields). Screenshot: `docs/ws-tfex-subclass/menu-toast.png`.
- **The real call site** (`notif-test.sh -f`: the auto-UI hosts a lobby on a server that refuses): MatchmakingService's
  CreateLobby fails, `MainMenuPatch` calls `Notification::Create(menu, "Failed to create lobby", 10, 120)` exactly as TF.EX's
  `MainMenu.cs:578` does: `docs/ws-tfex-subclass/create-failed-toast.png`. It looks like TF.EX's (the `variants/bubbleEdge` /
  `bubbleMiddle` bubble from the menu atlas, black centered text, TF.EX's geometry).
- **In a lobby match with rollback on** (`tools/lobby-auto.sh` with `LVB_URLEXTRA=netdelay=40&netjitter=20`, `lobbypred 8`,
  `notiftest 200 1`): the toast is created in the Level (layer 4, TF.EX's Level path: `Scene` set, added to `layer.Entities`),
  its Tween then its Alarm run (FramesLeft 230 -> 20 across the log), it leaves at frame ~475 and is removed; 24 rollbacks
  (deepest 25 frames) happened while it lived and after, the resimulated ticks ran our Update/Render and delegates, and the match
  compared **0 differing ticks of 1098** with the browser (the toast is not gameplay). Creation and removal across a rollback:
  "Rollback" below.

## What a BRUTE class is (and what ours needs)

Found in the binary (tools/re/rtypes.py documents each field; tfex/Game.hpp `brute::` has the layout):

| piece | game's | ours |
|---|---|---|
| object header | `obj[0]` = descriptor, `obj[1]` = sync word, fields from +0x10 | the same: allocated by the game's `NewObject` +0x610 from our runtime type |
| descriptor (.data, 3 words) | runtime type, vtable, interface table | in our module: {our type, our vtable, Entity's interface table} |
| runtime type (0x168 bytes) | built lazily by each type's getter through `MakeType` +0x2460, allocated uncollectable (+0xe50 = GC_malloc_uncollectable, kind 2) | a copy of Entity's record (same allocator) with our size, hash, names (UTF-16), descriptor, base getter, reflection counts 0 |
| instance size, GC | +0x00 size; +0x08 flags bit 3 = "holds references" -> `GC_malloc` (Boehm scans the whole object **conservatively**; there is no per-type GC bitmap); otherwise GC_malloc_atomic + memset | size 0xa8 (Entity 0x68 + our fields), flags copied (bit 3): our reference fields are scanned with no extra work |
| `is` / casts | `IsInst` +0x950: same type, else walk `+0x88` base-type getters (+0x2580); interfaces via the type's interface getters (+0xd8/+0xe0) | `+0x88` = Entity's own getter (+0x6f1470), as `MakeType`'s x7 is for a game subclass; Entity's IComponentHolder stays |
| vtable | 18 slots for Entity (Object's 4, SceneBegin..HandleGraphicsDispose, IComponentHolder Add/Remove twice), entries may carry a `this`-adjust flag in bits 59.. | Entity's copied from its live descriptor, slots 0x28 SceneEnd, 0x38 Removed, 0x40 Update, 0x50 Render -> our C++; checked at run time against the bodies tools/re found |
| interface dispatch | descriptor[2] -> {interface hash (low 32) / first vtable slot (high), 0} | Entity's, unchanged (same slot indices) |
| class constructor | runtime type +0x70 = a runner (`if (!flag) RunCctor(flag, .cctor)`); **BRUTE checks beforefieldinit at the call sites** | our statics are made in `EnsureClass`; before calling a game type's static (Alarm.Create, Tween.Create, Ease.CubeOut, TFGame.Font) we run that type's runner (Alarm.Create crashed on a null `Alarm.cached` until we did) |
| delegates | Action / Action`1: +0x10 function, +0x20 target, +0x28 method record (0x60 bytes: +0x08 declaring type, +0x18 body), +0x58 previous (multicast); Invoke = vtable slot 0x58, calls `fn(target, args...)` | `NewDelegate`: the game's delegate type, our C++ function, target = the Notification; a method record per lambda naming our type |
| statics | the game's live in its module's .bss (scanned by Boehm, inside the rollback's globals) | one uncollectable GC block per class (scanned, and inside the rolled-back heap); the C++ side keeps only its constant address |

So the deciding question has a clean answer: **a new class needs no compiler support, only these five records, all made at run
time from the base class's own** (`MakeSubclass`, 40 lines). The same works for any base whose runtime type, descriptor and
vtable size we know (all from rtypes.py) - MenuItem, Arrow, Scene subclasses are the same exercise.

Limits found:
- The class must be made **outside a rollback window** (in a menu): its type record and statics are GC allocations; made inside
  the window, a rollback past them would free them under our constant pointers. `EnsureClass` runs on the first menu frames.
- **Generic instantiations over a new type don't exist in the binary** (`List<Notification>`, `Scene.Add<Notification>`): we
  use the base type's instantiation (`List<Entity>`, same layout for any reference type) and re-express a generic *method*'s
  body (`Scene.Add<T>` = `Layers[e.LayerIndex].Add(e, false)`, which BRUTE inlines everywhere).
- **Private fields aren't in BRUTE's reflection** (only public/internal fields have records): `Layer.toAdd` (TF.EX's
  `DynamicData.Get("toAdd")`) was found from `Layer.Add`'s body (`ldr x0,[x0,#0x60]; b List.Add`).

## Rollback (ROLLBACK.md)

- **The class itself** (runtime type, descriptor, vtable) is immutable after `EnsureClass` and outside the restored memory:
  nothing to do. The objects, their components, delegates, the `_lives` list and the statics block are ordinary heap memory: the
  undo log covers them like the game's own (the 24 rollbacks above, 0 differing ticks).
- **Creation outside the simulated tick.** TF.EX creates in-match notifications from network callbacks, between ticks. Our test
  does the same (from the frame hook). A rollback whose target is before that moment restores memory from before the toast
  existed: the toast is erased and nothing recreates it (on PC TF.EX it survives, because TF.State's snapshot doesn't include
  UI entities). See the stress run below. This is a property of the raw-memory rollback, not of subclassing; the fix belongs in the
  rollback glue (follow-up): run such non-simulation creations as recorded events at the start of the next simulated tick, so a
  resimulation replays them (as inputs are), or re-apply the ones made after a rollback's target once it has restored.
- `new Canvas(w, 14)` (kept: TF.EX makes one per toast and never disposes it) creates a GPU render target, native memory the undo
  log doesn't cover: a toast created inside a resimulated tick would leak one small render target per resimulation (none here:
  the toasts are made outside the tick).

Stress run (same lobby harness, a toast made from the frame hook every 37 Level frames from frame 200; the test counts creations
in GC memory and in our module, so a rollback that erases one shows as a difference): **18 toasts made, 6 erased by a rollback
whose target was before their creation**, 21 rollbacks (deepest 20), no crash, the layer and `_lives` always agree (1-4 toasts
at once, stacked by TF.EX's slot logic), toasts that had already been removed come back with a restore and leave again in the
resimulation, **0 differing ticks of 1094** with the browser. So restores across creation and removal are memory-safe; what
is missing is only TF.EX's semantics for non-simulation UI (the follow-up above).

## Field walker and coverage (step 1)

New tools in `tools/re/` (README section "Types, fields, TF.EX coverage"):
- `rtypes.py`: every managed type from its `MakeType` call (size, flags, descriptor, vtable, base type, class-constructor
  runner, getter) and its field records (+0x00 name, +0x08 the field type's getter, +0x10 offset, +0x20 attributes; statics give
  their storage address). 14,662 types (4,484 with field records, 17,927 fields). `rtypes.py Monocle.Entity`.
- `fieldcheck.py`: against offsets the port already uses: **38 of 38 resolvable known offsets match, 0 wrong**; 6 more are
  private fields (MainMenu.state/switchTo, Engine.scene/nextScene, MainModeButton.text, RollcallElement.playerIndex): not in
  BRUTE's reflection. Auto-properties (Entity.Scene, MenuItem.Selected, Player.Dead...) resolve through their getter's body
  (`ldr [x0,#off]; ret`).
- `sigs.py`: a class's methods with their PC signatures paired with the Switch bodies by declaration order (overloads).
- `tfexrefs.py`: the TowerFall/Monocle members referenced by TF.EX's compiled network DLLs (IL of TF.EX.API/Common/Core/Domain/
  Patchs from the v0.19.2 release zip), resolved on the Switch:

| | distinct members | resolve automatically | by use |
|---|---|---|---|
| fields (ldfld/stfld/ldsfld...) | 182 | **171 (94 %)** | 1164 / 1190 |
| methods (call/callvirt/newobj, one per overload) | 338 | **305 (90 %)** | 1917 / 2007 |
| `DynamicData` names (private members by string) | 55 | 6 in field records | the rest need code analysis (like `toAdd`) |
| Notification.cs alone | 31 | 30 (Scene.Add<T> is generic, inlined) | 71 / 72 |

  Of the misses: 11 members of types the Switch doesn't have (XGamepadInput, GamepadConfig, MInput.XGamepadData: S-input);
  PlayerInput's abstract getters (no body: vtable slots, resolvable by slot); generic methods (Scene.Add<T>, Calc.Shuffle<T>);
  members 1.4.1.6 lacks (MatchSettings.IsCustom, MatchVariants.CustomVariants: FortRise/TF.EX additions). 48 overloaded
  methods are picked by their IL signature, 2 not (generic signatures).
- `genheader.py`: Game.hpp-style accessors from the above (`genheader.py --tfex --class Notification` emits every offset
  Notification.cpp uses; tfex/Game.hpp's new section was checked against it).

## C# constructs in Notification.cs with no mechanical mapping (step 4)

Mechanical (a translator does these by rule, no knowledge of the class): field reads/writes at the walker's offsets; static
field storage; constants; default parameters; game method calls by body (overloads by signature); property getters/setters;
`base.X()` = the base body; reference `==`; arithmetic, casts, `Math.Ceiling`/`Math.Max` (C math, same IEEE ops); enum
literals; integer/float mixing; virtual calls through `Virt`.

Each `MAP:` in Notification.cpp is one of these:

| # | C# construct | what we did | what a translator needs |
|---|---|---|---|
| 1 | `class Notification : Entity` + `override` Update/Render/Removed/SceneEnd | `MakeSubclass` + vtable slot overrides | the base's runtime type getter, descriptor, vtable size and the slot of each overridden method: all from rtypes (slot = the vtable entry equal to the base's body from sigs) |
| 2 | instance fields of the new class | a C++ struct after Entity's 0x68 bytes | our own layout; nothing else (no GC bitmap: Boehm is conservative) |
| 3 | `new Notification(...) : base(layer)` | `NewObject(ourType)`, then Entity's ctor body with `this` | BRUTE ctors take `this` (null = allocate the declaring type): allocate with ours, call the base ctor body |
| 4 | static fields with initializers (`List`, `Queue`, `bool`) | one uncollectable GC block, filled by `EnsureClass` | a statics struct per class, the static initializers as its class constructor, run outside any rollback window |
| 5 | `List<Notification>` (generic over the new type) | `List<Entity>` | map a generic instance over a new reference type to the nearest base type's existing instance |
| 6 | `mainMenu.Add(notification)` = `Scene.Add<T>` | its body: `Layers[e.LayerIndex].Add(e, false)` | generic *methods* over a new type have no code: translate the generic method's PC body, or use an existing instantiation |
| 7 | lambdas capturing `this` (5) and a method group (`RemoveSelf`) | `NewDelegate`: game delegate type, C++ function `(this, args)`, target this; the method group uses the game's Entity.RemoveSelf body | lambda -> function with the target first; a closure capturing locals needs a synthesized display class (an Object subclass, as `MakeSubclass`) - none here |
| 8 | calls to game statics (`Tween.Create`, `Alarm.Create`, `Ease.CubeOut`, `TFGame.Font`) | `Cctor(runner)` first | BRUTE puts beforefieldinit checks at the call sites: emit the type's runner before any static access of a game type (rtypes "cctor") |
| 9 | string literals; `text` parameters | static `MString` objects (System.String's descriptor +0x45aefb8), outside the GC heap | a literal table; strings are immutable, so static objects are safe |
| 10 | `text.ToUpper()`, `string ==` (mscorlib) | System.String bodies by name; `op_Equality` | corlib (Mono) signatures for overloads: sigs.py only knows TowerFall's PC IL; add the Mono corlib's |
| 11 | `TFGame.Font.MeasureString` (FNA) | SpriteFont.MeasureString +0xd9590 | FNA's IL for signatures; tfexrefs counts only TowerFall/Monocle members |
| 12 | `Vector2` values, `Vector2.Lerp`, `Position.Y +=` | a C++ POD (HFA: passed in s0/s1, as AAPCS64), Lerp inlined | FNA value types as PODs with the same layout; their small methods inlined or called |
| 13 | `scene is MainMenu`, `scene is MainMenu menu`, `switch (scene) { case MainMenu: case Level: }`, `.OfType<Notification>()` | `brute::As` (IsInst) with the type getters | type tests -> IsInst with the target's getter; for our types, our runtime type |
| 14 | `scene.Layers.Single(l => l.Key == layerIndex).Value` | `Dictionary<int, Layer>[key]` | LINQ (673 calls in TF.EX) needs rewriting patterns or a small LINQ over game collections; `Single` = indexer here |
| 15 | `.Where(...).ToList()` then removing while iterating | a fixed-size snapshot array | LINQ materialization -> temporary arrays (a fixed cap of 32 here) |
| 16 | `DynamicData.For(layer).Get<List<Entity>>("toAdd")` | Layer.toAdd +0x60, found by hand | private fields are not in BRUTE's reflection: a resolver from code (accessors, method bodies), or a hand table (55 names in TF.EX) |
| 17 | `DynamicData.For(notification).Set("Scene", scene)` | `Entity.set_Scene` | by-name property access -> the setter's body (resolvable: records) |
| 18 | `ServiceCollections.ResolveWiderSetModApi()?.UIXOffset ?? 0` (DI) | `MainMenuPatch::IsWide() ? 50 : 0` (TFEX-DIFF: S-wide) | DI services -> the override table (section 3's ledger) |
| 19 | `_deferred.Enqueue(text)` with a literal | a GC copy of the text (ToUpper's result: Create upper-cases anyway) | a managed string from native text: a real string allocator (String.CreateString / FastAllocateString) |
| 20 | `foreach` over a List | index loop over get_Count / get_Item | the enumerator's version check (mutation during iteration throws in C#) is not reproduced |
| 21 | exceptions (`Single` with no match, a missing atlas key) | none: the game's own exception path (fatal) | the translator can't express try/catch (exlaunch builds -fno-exceptions); TF.EX's code here has none |

Not ported: nothing of Notification.cs's members is left out. `FlushDeferred`/`CreateOrDefer`/`ClearDeferred`/`Clear` are
ported but have no Switch caller yet.

## Files

- `hw-probe/exlaunch/source/program/tfex/Notification.{hpp,cpp}`: the class (+ `brute::MakeSubclass`, `NewDelegate`), the
  test (hw.txt `notiftest <frame> <scene: 0 menu, 1 Level>`).
- `tfex/Game.hpp`: `brute::` (the runtime's layout, type getters, descriptors, cctor runners) and the members Notification uses.
- `tfex/MainMenuPatch.cpp`: the one call site (create failed). `xplay.cpp`: two lines (hw.txt `notiftest`, the test's frame call)
  and the include.
- `tools/re/`: rtypes.py, fieldcheck.py, sigs.py, tfexrefs.py, genheader.py, notif-test.sh; classmeta.py: a crash fix (an
  `add` on an unknown register).

## Next

- The rollback glue for UI made outside the tick (above), then in-match notifications ("Synchronized!", "Player left",
  "DESYNC DETECTED": TFEX-BASE section 4).
- The remaining components the same way: MenuItem subclasses (NetplayButton, LobbyBorderButton...) need MenuItem's vtable map
  (its 0x80.. slots: TweenIn/TweenOut/OnConfirm...: Game.hpp already names three).
- A translator stage for CustomComponents: rows 1-9 and 13 are rule-based with today's tools; 14-16 (LINQ, private members) are
  the real work.

## Phase 2 (2026-10-16 evening): rollback-safe toasts, TF.EX's call sites

### UI made between ticks survives rollbacks (TFEX-DIFF: S-rollback)

TF.EX makes in-match toasts from network callbacks, between ticks; its TF.State snapshot doesn't hold them, so a rollback leaves
them alone. Our rollback restores all memory, so (phase 1) a rollback to a frame before such a toast erased it for good.

Keeping the toasts out of the restored memory isn't sound (their allocations come from Boehm's free lists, which are restored: the
memory would be handed out again). Instead the toast is made **inside the simulation**: in a Level, `Notification::Create` /
`Clear` called outside a simulated tick record a request (our module, not rolled back: scene, the Level's `FrameCounter`, the
arguments, a sequence number) and return. A hook on `Monocle.Scene.Update` (+0x944130, which `Level.Update` calls in every tick,
resimulated ones included; installed only once the class exists, so nothing changes while `notif` is off) carries out, at the start
of the tick, every request of that Level with a sequence number above `Statics.appliedSeq` and a frame at or before the counter.
`appliedSeq` lives in the class's GC statics, so a rollback past a request takes back both the toast and the record that it was
made, and the resimulation makes it again at the same tick, identically. A toast appears one tick after the call. In the menu (no
rollback) and from inside a tick, Create acts at once, as in TF.EX. Requests older than twice the rollback window are dropped.
`FlushDeferred` goes through the same path (the deferred texts become requests).

The mechanism is its own unit, `tfex/BetweenTicks.{hpp,cpp}` (`Run(scene, fn, payload, size)`, `InTick()`, `Enable()`), independent
of the class: Notification's static entry points (Create, Clear) wrap their bodies with it, and a generated class
(tfex/gen/tfexgen_Notification.cpp) can wrap its own the same way. Call sites go through the `tfex::Notification` API only.

Canvas: `new Canvas(length * 10, 14)` makes a GPU render target (native, outside the undo log); TF.EX only reads its Width. The
canvases are made once per length (1..47) when the class is made, in the menu, and shared; nothing native is made in a tick.

Stress run (lobby match, rollback, netdelay 40 / jitter 20, a toast requested from the frame hook every 37 Level frames; the test
checks every frame that the game's memory holds exactly the toasts requested before the current Level frame): **18 toasts, 27
rollbacks (deepest 18), at least 4 of them across a toast's tick, 0 mismatches, 0 differing ticks of 1099**. (Phase 1, without
the requests: 6 of 18 erased.)

### TF.EX's Notification call sites (v0.21.0; v0.19.2 has the same minus the 0.20/0.21 additions) and ours

All of ours are behind hw.txt `notif 1` (off: the stand-in text where there is one). Owner = who wires it.

| TF.EX | text (appear, stay) | ours | owner / state |
|---|---|---|---|
| Patchs/Scene/MainMenu.cs:586 (CreateLobby onFail) | Failed to create lobby (10, 120) | tfex/MainMenuPatch.cpp Update (g_createFailed) | subclass: done |
| MainMenu.cs:456, 1294 (GetLobbies onFail) | Failed to retrieve lobbies... | tfex/NetplayScreens.cpp OnBrowseFail | subclass: done (Status stand-in while off) |
| MainMenu.cs:1511 (LobbyInfos click, in game / series) | MATCH IN PROGRESS, SPECTATE ONLY / SERIES IN PROGRESS, SPECTATE ONLY | NetplayScreens.cpp BladeConfirmHook | subclass: done |
| MainMenu.cs:1520 (can't join) | CAN'T JOIN - <reason> / missing variants message | NetplayScreens.cpp BladeConfirmHook (reason = MainMenuPatch WhyNotJoinable) | subclass: done (our reasons, not TF.EX's CanNotJoinReason texts) |
| MainMenu.cs:1773 (JoinLobby onFail) | Failed to join lobby: either full, gone, or already started (15, 450) | NetplayScreens.cpp OnJoinFailed | subclass: done |
| MainMenu.cs:959 (JoinPrivate onFail) | Invalid or expired code (10, 300) | NetplayScreens.cpp OnPrivateJoinFailed | subclass: done |
| MainMenu.cs:949 (private lobby refused) | <reason> (10, 300) | no equivalent (JoinPrivate doesn't evaluate the lobby) | open |
| MainMenu.cs:803 (EnterQuickPlay refused) | <server message> (10, 300) | NetplayScreens.cpp OnQuickPlayFailed | subclass: done |
| MainMenu.cs:822 (OnQuickPlayMatched) | OPPONENT FOUND (10, 200) | NetplayScreens.cpp OnQuickPlayMatched | subclass: done |
| MainMenu.cs:1351 (copy code) | CODE COPIED | n/a: no clipboard (S-keyboard) | - |
| MainMenu.cs:1656 (mod version mismatch) | <mod> <ver> (HOST <ver>) - MAY NOT WORK | n/a: no mods besides WiderSet (built in) | - |
| MainMenu.cs:467 (spectate without a lobby) | You must select a lobby to spectate! | n/a: no spectating | - |
| Patchs/Entity/MenuItem/RollCallElement.cs:111, 282, 511 (UpdatePlayer onFail) | Failed to notify server | tfex/MainMenuPatch.cpp OnNotifyServerFail (PublishLocal, team selection) | subclass: done |
| RollCallElement.cs:160 (confirm with missing variants) | MISSING <variant> (+N MORE) (10, 400) | n/a today: lobbies with missing variants aren't joinable (WhyNotJoinable) | - |
| Patchs/Engine/TFGame.cs:238 (failed connection) | GetFailedConnectionMessage() (10, 450) | NetplayManager (failed connection messages) | lockstep |
| TFGame.cs:446, 510, 517 (auto-updater / version) | Update failed... / Online play requires version... / Could not verify TF.EX version... | n/a (S-update); the version check: none yet | - |
| TFGame.cs:470 | ONLINE PLAY REQUIRES THE DARK WORLD DLC | n/a (always owned on the Switch) | - |
| TFGame.cs:550, 551, 564 | NON-COSMETIC MODS CAN DESYNC NETPLAY / risky mods / YOUR GAME FILES ARE MODIFIED | n/a (no mods; game files not checked) | - |
| Domain/Services/MatchmakingService.cs:198 | Connection dropped | tfex/MatchmakingService.cpp (socket closed) | lockstep |
| MatchmakingService.cs:843 (KickedFromLobby) | <reason> - REMOVED FROM LOBBY (10, 500) | MatchmakingService.cpp (new message) | lockstep |
| MatchmakingService.cs:937 (alone in a Level) | All players left... (10, 500) | MatchmakingService.cpp lobby update | lockstep |
| MatchmakingService.cs:966 (host left, in a Level) | Host left the game... | MatchmakingService.cpp lobby update (g_lobbyClosed); in the menu TF.EX shows none (MainMenuPatch's lobby-closed path: right as is) | lockstep |
| MatchmakingService.cs:1112 | SERIES ABORTED: <reason> (12, 500) | n/a until series lobbies | - |
| MatchmakingService.cs:1122 (NotifyConnectionUpdate) | CONNECTION UPDATED (10, 300) | MatchmakingService.cpp (RelayedPairs) | lockstep |
| MatchmakingService.cs:1242 | MISSING <variant>, REMOVED FROM LOBBY (10, 450) | MatchmakingService.cpp | lockstep |
| MatchmakingService.cs:1342, 1348 | <name> HAS MODIFIED GAME FILES / IS PLAYING ON <platform> - NETPLAY MAY DESYNC (10, 500) | MatchmakingService.cpp (players' Platform / files) | lockstep |
| MatchmakingService.cs:1373, 1442 | <name> joined (10, 200) / Player left (10, 200) | MatchmakingService.cpp lobby update | lockstep |
| MatchmakingService.cs:1931, 1966 | <reason> - NETPLAY MAY DESYNC / spectator notice | MatchmakingService.cpp | lockstep (spectator: n/a) |
| Domain/Services/NetplayManager.cs:405, 411, 417, 422 (GGRS events) | Trying to re sync (10, 300, not sticky, no animation) / Clear(scene, 4) / Synchronized! (10, 150) / Clear(scene, 4) | the events are read in xplay.cpp's netplay gate (ev.kind 2 synchronized, 4/5 interrupted/resumed); NetplayManager.cpp is lockstep's | lockstep, with a line in xplay.cpp (coordinator) |
| NetplayManager.cs:553, 620 (desync, EndMatch) | DESYNC DETECTED - match ended (15, 450) / <message> (15, 450) | xplay.cpp ev.kind 7 (npdigest) | lockstep / coordinator |
| NetplayManager.cs:571 | A error occured while spectating | n/a | - |
| Patchs/Scene/LevelLoaderXML.cs:26 | Waiting for other players... (20, 0, no animation) | the level load in a lobby match (xplay.cpp / pcgameplay.cpp) | lockstep |
| Patchs/Scene/Level.cs:117, 211 | FlushDeferred(level) / Clear(level, HUD_LAYER) | xplay.cpp LevelUpdateHook (coordinator) or NetplayManager | lockstep |
| Patchs/Entity/PauseMenu.cs:91, 107, 129, 155 | All players left... / Waiting for other players... (10, 10, sticky) | n/a: pausing is off in netplay | - |
| Patchs/Entity/MenuItem/VersusBeginButton.cs:25 | Cannot start Netplay with more than 2 players | n/a (2-4 players supported) | - |
| Patchs/VariantButton.cs:42, VariantToggle.cs:42, 59 | LOCKED (SERIES ON) / <variant> DOES NOT SUPPORT NETPLAY / <player> DOESN'T HAVE <variant> | no variants screen online yet | - |
| Core/TFEXModModule.cs:55 (logger sink) | CreateOrDefer(scene, message) | n/a (FortRise logger) | - |
| Domain/Services/ReplayService.cs:162-177 | IsDeferedOn / ClearDeferred | n/a (no replays) | - |

For the lockstep session: call `tfex::Notification::Create(tfex::game::CurrentScene(), "<TF.EX's text>", appear, stay)` (and
`Clear(scene, 4)`) from the game thread, guarded by `tfex::Notification::Enabled()`; from network callbacks in a Level it is
rollback-safe as is (a request for the next tick). TF.EX's `$"...".ToUpperInvariant()` isn't needed: the toast upper-cases.

Stand-ins kept (they are other TF.EX components, not toasts): NetplayScreens' Status "FINDING LOBBIES..." / "JOINING LOBBY" and
the seat label "CREATING LOBBY..." are TF.EX's **Loader** (`AddLoader`); "NO LOBBIES FOUND" is a `Text` in the LobbyPanel; the
seat's "WAITING FOR PLAYERS..." stands for TF.EX's open-seat orb.

### Next component: VersusSelectBanner (not started; what it needs)

`CustomComponent/VersusSelectBanner.cs` (107 lines) is a `TowerFall.MenuItem` subclass. With today's tools:
- MenuItem: size 0x98, descriptor +0x4710490, getter +0x8b7f70, ctor(Vector2) +0xac9080, Update +0xac9390; its vtable is
  0xb8 bytes (`tools/re/vtable.py TowerFall.MenuItem`): the five abstract methods are **null slots** in the base vtable
  (0x80 TweenIn, 0x88 TweenOut, 0x90 OnSelect, 0x98 OnDeselect, 0xa0 OnConfirm; matches Game.hpp), so a subclass must fill all
  of them. `vtable.py` names every slot (bodies through classmeta + sigs; null slots = the class's abstract methods in IL order).
- Its nested `private sealed class Entry` is a second new class, deriving from System.Object (size 0x10, descriptor +0x4594608,
  getter +0x380): the same `MakeSubclass`. `List<Entry>` -> `List`1[System.Object]` (getter +0x8290b0). `Func<bool>` exists
  (`System.Func`1[System.Boolean]`, getter +0x844630): its lambdas are `NewDelegate`s.
- `params string[] lines` needs a managed array of the game's `System.String[]` (+0x8811b0): the array allocator isn't
  located yet (not +0xb10, which boxes a value type; +0x3690 is the string-literal table). That is the one open piece.
- Draw.TextureBannerV #1 +0x929c20, OutlineTextCentered #3 +0x924c60 (two colors), Calc.HexToColor +0x909510,
  Calc.Approach(float) +0x90a490; `Color * float` = FNA's operator (inline: each byte times the factor).

## Phase 3: the mod runs the translator's classes

### Integrating a generated class (the pattern; Notification is the first)

The translator writes `tfex/gen/tfexgen_<Class>.{hpp,cpp}` (never edited: regenerated): the class itself (runtime type made with
`brute::MakeSubclass` in its `EnsureClass`, vtable overrides, statics in a GC block) and TF.EX's static members as C++ functions on
managed values (`void* text` = a System.String). Around it, one hand-written Switch file per component, `tfex/<Class>.{hpp,cpp}`:

1. **API for our call sites**, in our types (ASCII texts, plain C++ values): `tfex::<Class>::Create(...)` etc. Call sites never
   include `tfex/gen/`; when the generated class changes or replaces a hand one, only this file changes.
2. **Switch on/off**: `Enabled()` (hw.txt, e.g. `notif 1`); `EnsureClass()` calls the generated `EnsureClass()` once, in a menu
   (no rollback window open: the type record and statics are GC allocations), then `BetweenTicks::Enable()`.
3. **Rollback** (TFEX-DIFF: S-rollback): every static entry point that changes game memory is called through
   `BetweenTicks::Run(scene, thunk, &args, sizeof args)`. The thunk turns the plain args into managed values (`gen::FromAscii` for
   strings: a GC string, since the generated code may keep it, e.g. CreateOrDefer's queue) and calls the generated function. In a
   Level between ticks it becomes a request carried out in the next tick and replayed by resimulations; elsewhere it runs at once.
   Args are plain data (no GC pointers: they wouldn't survive a rollback).
4. **Ledger hooks for the generated code**: what the class needs from the Switch (TFEX-DIFF rows) is a glue extern the translator
   routes to a function here (e.g. `SharedCanvas(w, h)` for Notification's `new Canvas`, requested from the translator), or to
   Ledger code (`NotificationCenterX`).
5. **Install**: nothing in xplay.cpp beyond the existing hw.txt line; the class is made lazily (first menu frames / first use).
   `brute::` runtime (MakeSubclass, NewDelegate) is `tfex/Brute.cpp`, shared by every generated class.
6. **Test**: the hw.txt test drives only the API and reads the game's layers (`As(entity, RuntimeType())`), so it runs on the
   generated class as it did on the hand port.

Notification now: `tfex/Notification.cpp` is that wrapper (~100 lines + the test); the hand-written class body is gone.

Tests of the generated Notification behind the wrapper (private slot, 2026-10-16 evening):
- menu (`notif-test.sh`): the generated class made, toast slides in to x 95, survives GC_gcollect, leaves, is removed;
- create-failed (`notif-test.sh -f`): "Failed to retrieve lobbies..." then "Failed to create lobby", stacked;
- lobby match with rollback (`lobby-auto.sh`, netdelay 40 / jitter 20, `notiftest 200 1`): 18 toasts requested between ticks,
  26 rollbacks, **0 mismatches, 0 differing ticks of 1096**. TF.EX's own in-match toasts now flow through the same path:
  tfex-lockstep's "Synchronized!" / "Trying to re sync" (NetplayManager, between ticks) were made, replayed after rollbacks and
  removed like the test's. Of four runs, one ended with the browser page stopping at tick 432 (8 differing ticks, all in its last
  ticks) and one stalled (the Switch slow, load average 40-60 on this Mac); the notif-off baseline right after and the last run
  were clean.
- SharedCanvas: the translator's [[rewrite]] (ws/tfex-update 43248ad, glue TfexGlue.Ledger.SharedCanvas, weak declaration in
  tfexgen_ledger.hpp) routes the generated ctor's `new Canvas` here. Stress run on a merge of ws/tfex-update + ws/tfex-subclass:
  the shared canvases used (no render target made in a tick), 18 toasts, 24 rollbacks, 0 mismatches, 0 differing ticks of 1093.
- `tfex::game::brute::NewArray` (+0x6110: arrayType, elementSize, lengths*, rank) found for `params string[]` (VersusSelectBanner).

### Rollcall pings (TF.EX MainMenu_Render), with tfex-lockstep

`tfex/MainMenuPatch.cpp` RollcallRenderHook (a RollcallElement.Render postfix, +0xb351a0; MainMenu.Render is tfex/gen glue's)
calls tfex-lockstep's `tfex::RollcallPings::RenderRollcallPing(element, playerIndex, IsWide())` per seat and
`RenderCheckingConnections()` once per frame, on the generated ConnectionIcon / RenderBannerWithDots; the hand copies
(tfex/ConnectionIcon.*, tfex/MainMenuBanner.*) are deleted on ws/tfex-lockstep. Compiled in when tfex/RollcallPings.hpp is
present (ws/tfex-lockstep 6c4ef65). Tested by tfex-lockstep on a merge of both branches: "306 MS" in red with the direct-connection
icon over the page's seat, then 0 differing ticks of 1199. Next: `RollcallPings::RenderQuickPlayStarting()` in NetplayScreens'
QuickPlaySearch once it exists.

### VersusSelectBanner: generated, in use

`tfex/VersusSelectBanner.{hpp,cpp}` wraps tfex/gen/tfexgen_VersusSelectBanner (ws/tfex-update b00f047, merged here): `New(x, y)`
(Depth -1000), `Track(banner, button, title, line1, line2)` with `() => button.Selected` as a real System.Func`1[System.Boolean]
made from plain C++ (`brute::NewNativeDelegate`, tfex/Brute.cpp: a method record naming Monocle.Entity, no class constructor) and
the lines as a managed String[] (NewArray). NetplayScreens' NetplaySelect and PrivateSelect build it as TF.EX does
(BuildVersusSelectBanner + Track per button, added with the menu's items so the menu tweens it); the three-OutlineText stand-in is
kept only while TF.EX's runtime-made classes are off (hw.txt `notif 1` turns them all on: Notification, VersusSelectBanner).
Ryujinx: `NT_WANT='screens\] NetplaySelect' tools/re/notif-test.sh "notif 1" "lobbyui 3"`: the waving questResults/tipBanner with
the gold title, TF.EX's lines, the cross-fade as the selection moves: `docs/ws-tfex-subclass/versusselectbanner.png`.

### LobbyPanel / LobbyInfos: generated, in use

`tfex/LobbyInfos.{hpp,cpp}` wraps tfex/gen's LobbyPanel and LobbyInfos (ws/tfex-update 0b1d3e7, merged here): `NewPanel(menu)`
(225, 110, on the UI layer), `NewEntry(menu, y, slot, lobby, canJoin, reason, onClick, target, panel)` (our models::Lobby turned into
the generated ws::Lobby through the JSON both read and write, kept per row slot as LobbyInfos keeps the pointer; onClick a real
System.Action from plain C++). NetplayScreens' OnRetrieveSuccess builds TF.EX's rows (5, 50 + 35 i), links Up/Down, selects the
first, sets MaxUICameraY and tweens the camera, as MainMenu.cs:1450-1561; JoinRow (the old BladeButton confirm) is the onClick.
First Ryujinx run: the classes made, LobbyInfos' ctor, OnSelect (LobbyPanel.UpdateInfo), Update and LobbyPanel.Render ran; then
LobbyInfos.Render jumped to address 0: the generated code calls `String.Split(char, StringSplitOptions)`, which the Switch's Mono
corlib doesn't have (binding unresolved = 0, compiled anyway). Reported to tfex-update with the Mono overloads (+0x3b1390
Split(char[], options)) and the ask that an unresolved member fail the build. Until it is regenerated, `kGeneratedReady = false` in
tfex/LobbyInfos.cpp keeps the BladeButton stand-in (with TF.EX's toasts: `docs/ws-tfex-subclass/browser-cantjoin-toast.png`).

Pitfall found on the way: exlaunch links with undefined symbols allowed (rtld resolves them at load), so a function declared in an
anonymous namespace and defined outside it builds and then jumps to 0 at run time ("[rtld] Unresolved symbol"). Check with
`aarch64-none-elf-nm -u -C tfx-hw-exlaunch.elf | grep "::" | grep -v "nn::\|std::"` (empty when clean).

Update (ws/tfex-update 91bceef merged: Split through gen:: helpers; unresolved bindings now fail the module build): `kGeneratedReady`
is true. Ryujinx (`tools/panel-check.sh [-n] "notif 1" "lobbyui 1" "lobbyjoin 1"`): a joinable PC lobby is joined from TF.EX's
LobbyInfos row (onClick -> JoinRow -> JoinLobby, seat 1); a wide one shows TF.EX's LobbyPanel (map block, mode in gold, length in
blue, the reason in red) and A gives TF.EX's "CAN'T JOIN - ..." toast: `docs/ws-tfex-subclass/lobbypanel-generated.png`. Open: the
map block shows an "X" icon for Sacred Ground (reported to tfex-update: the VersusGraphics glue's icon).
Pitfall: a delegate's target must be a managed object (or null): Action/Func.Invoke reads the target's type flags to unbox value
types; a native pointer as target gave a shifted `this`. The row's onClick target is a GC string with its RoomId.

### The notif 1 desync (2026-10-17) and the test matrix

Found by the managing session on ws/tfex-base 8bb4954 (lobby-vs-browser, seed 42, `notif 1`): 1834 of 1834 ticks differed from the
first one (the gameplay rng and the spawns). In that build the Notification class was made lazily, at the first toast: in a lobby
match that is tfex-lockstep's "Synchronized!", at sync, inside the match (around StartGame's reseed and SpawnPlayersFFA, with the
rollback's undo log starting). Making the class there puts GC allocations (type record, statics, BetweenTicks' cell, 47 canvases)
into the match. The exact mechanism of the rng difference was not pinned down (one failing run, logs since overwritten; my repro
attempts of the lazy path were clean), but the hazard is real: memory held by native pointers inside the rollback window.
**Fix (537efcf):** the class is made on the main menu only, eagerly once `notif` is on (MainMenuPatch::Update); a toast asked for
outside a menu before that is dropped; EnsureClass refuses outside a menu.

Runs on ws/tfex-subclass 998492a (fix + tfex-update 9957fbf), private slot, seed 42, netdelay 40 / jitter 20, lobbypred 8,
`notif 1`, vsstop 600 unless noted:
| case | result |
|---|---|
| the managing session's exact command (lobby-vs-browser.sh, LVB_PREKEYS=c) | 0 differing of 599 |
| 2-player lobby (lobby-auto.sh) | 0 of 599 |
| 3-player FFA (LVB_PAGES=2, lobbymax 3) | 0 of 593, both pages |
| wide lobby (WiderSet mods, lobbywide 1) | 0 of 596 |
| rematch (rematch-test.sh) | match #2 synchronized, 0 of 2554 |
| toast stress (notiftest 200 1: toasts between ticks every 37 frames) | 10 toasts, 8 rollbacks, 0 mismatches, 0 of 595 |
| lobby browser, 3 lobbies (tools/re/browser-test.sh) | TF.EX's LobbyInfos rows (selection slide), LobbyPanel, can't-join toast |
| joinable lobby from TF.EX's row (panel-check.sh -n) | joined (seat 1) |
| menu toast, create-failed toasts, VersusSelectBanner | as in the sections above |

Not tested (for the hardware checklist): everything on the console (the runtime-made classes, MakeSubclass/NewArray/NewNativeDelegate,
the BetweenTicks Scene.Update hook, the canvases' GPU memory); a toast created inside a rollback that crosses a GC collection; a
rollback crossing a toast's creation tick at a deep window (seen up to 25 frames, not 48); 4-player and team lobbies with toasts;
the browser with more than 8 lobbies (rows beyond 8 are not listed; the generated UpdateLobby's ring of 8 models) and with a lobby
list refresh while rows exist; PrivateSelect's banner (same code as NetplaySelect's, not screenshotted); the quick-play toasts
(OPPONENT FOUND, refused) and "Invalid or expired code"; "Failed to notify server".

### First-tick mismatch (open, 2026-10-17): analysis from the saved logs

The managing session's checkout (ws-archer3-local) failed from the first tick in every run with one extra hw.txt line ("notif 1",
"lobbywide 1" + WiderSet, three "genpatch" lines) and passed without; the same configs passed in other checkouts. From the saved
fail-genpatch logs (no new runs):
- compare-vs.py's alignment is right (origin 266 on both; the page stalled after 290 ticks); all 289 differences are real.
- The gameplay stream differs **before the spawn**: the browser's rng is 68393fba09e21e52 already at T -265 (players not alive), and
  a passing Switch run of the same config has that value too; the failing Switch has 950e4688d65551f4 from its first line and the
  p0/p1 spawns mirrored. So gameplay draws differ between Session.StartGame's reseed (logged, seed 42) and the first tick, during the
  MapScene -> Level transition / level load; not the spawn shuffle itself (its `new Random()` is marked as the gameplay stream in both).
- The match setup is identical (Switch logs from "lobby match requested" to "synchronized" equal line for line; PC level files
  byte-identical in the shared data folder and every slot).
- The always-on draw ring (rnd.cpp DumpDraws) is only written at vsstop; stalled runs lose it (0 "[vs] draw" lines).
Hypothesis (unconfirmed): with `rngcapture 1` capture is on in menus too; each extra line adds code that runs in the menu -> Level
transition frames (Notification's class creation, the generated patches' hooks, WiderSet); if any of it reaches the gameplay stream,
the draw count depends on the number of transition frames, which a busy machine changes.
Next steps (xplay.cpp / rnd.cpp): (1) dump the draw ring (or the draw count and last sites) at the first logged tick and on a stall,
not only at vsstop; (2) on the next failing config, `rnggametrace 400 -1000` on the Switch and `rngtrace=400` on the page (xlobby).
