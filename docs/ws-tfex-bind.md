# Workstream tfex-bind (2026-10-17)

## Status at the hand-over (owner from now on: tfex-translator; last worked on by tfex-services)

**How the tools work.**
- `tools/tfex/bind.py <requests.json> <binding.json>`: the translator (Binder.cs) runs it for the game members a translation needs that
  binding.json lacks; it resolves them on the Switch binary with tools/re (rtypes, sigs, tfexrefs, classmeta, vtable, relocs) and writes
  binding.json in place. On every run it also repairs older entries: null type getters, abstract vtable slots without "proof"
  (`repair_abstract`), vtable sizes (`repair_vtbytes`). Since 2026-10-18 (evaluation rec. 3) any session regenerates tfex/gen and
  binding.json for its own rule edits (`tools/tfex/translate.sh all <tag>`, committed with the rule change); the translator session
  owns the translator itself. gen/ merges with merge=ours (.gitattributes); `tools/tfex/check-gen.sh` after a merge says whether it
  is the merged rules' output.
- Abstract vtable slots (`abstract_slot`): proven by the concrete subclasses (all agree, the owner's slot null), else an error, never a
  guess. Vtable sizes (`vtable_bytes`, bind::vb_*): up to the interface table the descriptor points at, or the descriptor; never smaller
  than the base's.
- `tools/tfex/abstract-slots.py [Type...]`: every abstract method's derived slot and every class vtable size, checked against
  binding.json (exit 1 on a mismatch or an unproven entry). Whole game ~9 min; a few types ~40 s.
- `translate.toml [pin]`: a member the binder can't pick (`off = 0x..` for a field, `body = 0x..` for a method), each with its evidence
  (`why`). Pins can't carry a vtable slot: fix vslots in bind.py. Verify an offset from the disassembly (`tools/re/disasm.py <addr> <n>`,
  `privfield.py` for leads, `vtable.py <Type>` for slots) before pinning: a guessed slot or offset crashes.
**Done:** batches 1-2 (22 pins, type getters, generic overloads), the abstract-slot proof (MainModeButton's 8 slots, PlayerInput
Alt2Icon/Name), the vtable sizes (MainModeButton 0xa8 -> 0x118, MapButton, PlayerInput, Arrow).
**Open:** 5 abstract methods stay unresolved by design (3 PlayerInput members no Switch subclass overrides, 2 owners without a vtable);
the run-time check of the generated NetplayMenuButton with the fixed sizes (tfex-adopt's switch-over).

Helper of the TF.EX translator (tools/tfex/translator, `ws/tfex-update`): the game bindings. Branch `ws/tfex-bind`. Touches only
`tools/tfex/bind.py`, the `[pin]` section of `tools/tfex/translate.toml` and `tools/re/**`; `binding.json` and `tfex/gen/` stay
uncommitted (the translator session regenerates them).

## Coverage

`tools/tfex/translate.sh coverage v0.21.0` in this worktree: **339 → 347 of 542 methods, 3608 → 3771 of 9326 lines**; compile-gen unchanged (61 of 128 files, 18 methods with
compile errors); nothing newly blocked. Unblocked: CompactCoOpButton..ctor, LevelPatch.AddPlayersIndicators /
RestorePauseMenuMusicVolume, MapScenePatch.InitVersusButtons_Prefix / _Postfix, PauseMenuPatch.QuitToNetplayEntry /
PauseMenu_VersusMatchSettingsAndSave, SessionPatch.Session_GotoNextRound, MatchVariantsExtensions.EnableForAll (outside the counted
trees). Module build (tools/build-probe.sh) OK with `translate.sh all` output (the configured units' gen/ is unchanged: they use
none of the new pins).

Most methods that used a new pin are still blocked, now only by translator constructs: PauseMenu_Update and four others by
IsNetplayEndgame (LINQ), RollcallElement_Render / NotJoinedUpdate by the PlayerInput getters below, IsHostSeatUnjoined (LINQ),
VersusPlayerMatchResults_ctor and SkipLevelLoaderIfNeeded (DynamicData), NormalizeForNetplay (property), UpdateClipped
(ClipboardService).

(The committed report says 325/542; the same tree here gave 339/542 before any change: the compile step finds fewer errors in a
fresh checkout. 339 is the baseline.)

## What was added

**Pins** (translate.toml `[pin]`, each with its evidence): private fields BRUTE's reflection doesn't list, and one renamed field.

| member | Switch | evidence |
|---|---|---|
| PauseMenu.menuType / level / optionIndex | +0x68 / +0x78 / +0x80 | ctor stores its arguments; Update's Down/Up handling |
| PauseMenu.optionNames / selectedOptionNames / optionActions | +0xa0 / +0xa8 / +0xb0 | AddItem: three Adds, the middle one of `"> " + name` |
| PauseMenu.oldMusicVolume | +0xc0 | ctor stores Music.MasterVolume |
| RollcallElement.state | +0xd0 | ctor: new StateMachine(2), SetCallbacks x3, Add |
| Counter.counter, PlayerIndicator.showCounter | +0x24, +0x48 | Counter.Set / get_Value; PlayerIndicator ctor's new Counter() |
| Commands.currentText | +0x58 | ctor and EnterCommand store "" there |
| Variant.playerValues / value | +0x60 / +0x58 | get_PerPlayer, get_Value |
| ArcherPortrait.joined / portrait | +0x64 / +0x68 | Leave; ctor's new Image(Portraits.NotJoined) |
| VersusPlayerMatchResults.portrait / gem | +0xb8 / +0xc8 | ctor (bg is +0xb0) |
| VersusMatchResults.roundResults | +0x90 | ctor: stp session, roundResults at +0x88 |
| Player.lastAimDirection | +0x2e8 | set_Aiming stores -1f; Update compares with -1 and AimDirection |
| UnlockData.Ascension | +0x13 | Switch name AscensionAndCataclysm; GetTowerUnlocked's jump table (towers 11 and 15) |
| Engine.scene / nextScene | +0xe8 / +0xf0 | tfex/Game.hpp (hand-verified) |

PauseMenu.menuType is compared by TF.EX with MenuType literals: the translator takes enum values from the Switch by name (kind `e`),
so the Switch's own MenuType numbering is used.

**bind.py**
- Type getters: a type no field or base type names had `getter: null` in binding.json (TowerFall.PauseMenu, KeyboardInput,
  Loader, FightButton, ArchivesButton, TrialsButton, VariantToggle, VersusBeginButton, VersusMapButton, VersusModeButton, Modes[],
  Action`1[Int32 / String / Action / IntPtr], Predicate`1...: 34 entries). The header then had "no value": coverage counted those
  uses as fine, but a module build of a unit that uses one (an `is PauseMenu`) doesn't compile. `rtypes.getter_of` finds the
  getter (the function that calls the type's builder and returns its record slot: same shape and same getter-pointer table as
  MainMenu's 0x8b62f0); bind.py uses it for new requests and repairs the null entries already in binding.json in place.
- Overloads of a game generic type's instance (`Monocle.Sprite`1[System.Int32].Play(T,bool)`): no PC IL of its own, so the
  binder couldn't pick. Now picked by the open definition's (`Monocle.Sprite`1<T>`) IL order, matching the parameters
  (`!T, bool`). Checked: Play(T,bool) = 0x95d3d0 (uses w1, w2), Play(T,int32,bool) = 0x1725de0 (uses w1-w3).

**tools/re**: `privfield.py` (private field offsets from the methods that use them: a lead, to confirm in the disassembly),
`rtypes.getter_of`.

## Not bindable here (needs the translator or a ledger rule)

1. **Virtual / abstract game properties** (translator, UnitEmitter.Expr.cs property path): `PlayerInput.MenuBack` etc. are
   abstract on the PC. The property path asks for the getter's body, then a backing field, and both fail; it should go through
   `Call()` (vslot) when the getter is virtual/abstract. bind.py's vslot already resolves them: MenuConfirm 0x38, MenuBack 0x48,
   MenuAlt2 0x68, MenuUp 0x70, MenuDown 0x78, MenuLeftCheck 0xa0, MenuRightCheck 0xa8, MenuAlt2Check 0xb8, Alt2Icon 0x100,
   Name 0x170 (the first four as in tfex/Game.hpp). Blocks InputDelayAdvisor.CapturesLeftRight (3 callers), UpdateGesture,
   RenderGuides, MainMenuPatch.Alt2Pressed, RollCallElementPatch Render / NotJoinedUpdate(_Prefix) / HandleTeamSelection / LeaveJoined.
2. **Generic methods with no instantiation on the Switch** (runtime or rewrite rules): `Array.IndexOf<T>(T[],T)` (the game never
   calls it; blocks LobbyVersusModeButton.IndexOfCurrentMode and LobbyVersusSeriesButton.IndexOfCurrent, 6 callers: an inline
   loop), `Scene.Add<T>(T[])` and `Scene.Add<T>(List<T>)` (like the existing Scene.Add<T>(T) rewrite; 4 MainMenuPatch methods),
   `Calc.Shuffle<T>`.
3. **Not in the Switch's (Mono) corlib**: Char.IsUpper (MatchVariantsPatchs.GetVariantTitle, alone), BitConverter
   SingleToInt32Bits / Int32BitsToSingle, Convert.ToHexString, SHA256.HashData, Path.GetRelativePath, Guid.NewGuid, TimeSpan.From*,
   Math.Clamp, OperatingSystem.Is*, Stream.ReadExactly, and the .NET Core string overloads (Split(char, options),
   Split(string, int, options), Split(ReadOnlySpan), Join(char, ...), StartsWith(char), Trim(ReadOnlySpan)).
4. **Generic instances the game never made**: Dictionary<Variant,bool>, Dictionary<Variant,bool[]>, HashSet<Variant>,
   HashSet<int>, List<List<int>>, List/Queue<TF.EX struct> (System.ValueType), Dictionary<int, byte[] / float / struct[]>,
   Func/Action/Predicate over TF.EX or new types, IGrouping. Need translator-side containers.
5. **The Switch's class differs**: Monocle.Screen has no Scale / ScaledHeight / Center / DrawRect (1.4.1.6: get_Width +0x949210,
   get_Height +0x949220, SetSize); Screen.Center = (Width, Height) * 0.5 as a rule (Text..ctor, alone). TowerFall.XGamepadInput and
   Monocle.MInput+XGamepadData don't exist (JoyConInput): ledger.
6. **PC-only, left unbound**: Keyboard.GetState, MouseState.X/Y, Game.Window / GameWindow.ClientBounds (mouse and keyboard),
   GraphicsDevice.Clear(Color) (replays), Console.WriteLine (debug), Marshal.Copy, reflection (MethodBase.Invoke,
   PropertyInfo.GetValue, MethodInfo.CreateDelegate, AccessTools, Traverse), SDL clipboard, DI / logging.
7. **FortRise** (not bindings; for the [refs] / TFEX-DIFF decision): `MatchVariants.CustomVariants` (MatchVariants_BuildMenu_Prefix:
   unsupported foreach) and `player.CustomVariants` copies, `FortRise.RiseCore.WillRestart` (TFGamePatch.HandleAutoUpdate:
   assignment target Invalid). None of the pins above is a FortRise member: all are vanilla 1.3.3.3 fields checked against the PC
   source.

## Batch 2 (after master 3de03f4): nothing left to pin

Every remaining "unbound" item in the coverage dump is one of the kinds above (interface methods such as ICollection`1.Add /
IReadOnlyCollection`1.get_Count are a fifth: the call needs interface dispatch, there is no body to bind). The other game-member
blockers are DynamicData:
- `DynamicData.Invoke` of a game method is not translated yet (UnitEmitter.Class.cs DynamicDataCall handles Get/Set of fields and
  properties). The methods bind by name already (bind.py, checked): MainMenu.TweenBGCameraToY(int) +0xaa0930 (6 places in
  MainMenuPatch), Layer.UpdateEntityList +0x9371e0 (LevelPatch.UpdateLayersEntityList), Layer.Add(Entity, bool) +0x938f40 (the
  CameraLayer.Add calls of OnRetrieveSuccess), ArcherPortrait.InitGem +0x96f0a0, VersusModeButton.UpdateSides +0xbb1b90.
- Members vanilla 1.3.3.3 doesn't have: Engine.FrameCounter (the PC's is Scene.FrameCounter), VersusModeButton.currentIndex
  (FortRise's mode registry?), MatchSettings.CustomVersusModeName (FortRise); and TF.EX's own DynamicData state on game objects
  (Player.isAimingRight / rightStickHeld, OutlineText.IsPlayerName): side tables, not bindings.

## Abstract vtable slots (2026-10-17, tfex-services): proven by the subclasses, not guessed

bind.py gave an abstract member (no body on the Switch) the null slot vtable.py named for it by guessing "the class's abstract methods
in IL order". For MainModeButton that guess was wrong: its own 8 abstract members sit at 0xa8..0xe0, and MenuItem's 5 abstract
methods are already overridden at 0x80..0xa0. So BaseScale/ImageScale/ImageRotation came out at 0xd0/0xd8/0xe0/0x120. Those really
are ImageY.get, ImageY.set, MenuAction, and a slot past the interface table. The same shift hit PlayerInput.get_Alt2Icon (0x100, really
0xf8) and get_Name (0x170, really 0x168). No shipped generated code used the wrong ones.

Now `abstract_slot` (bind.py): every concrete subclass that has a body for the member (classmeta record names, vtable.py) must have it
at the same offset, and the owner's own entry there must be null; else an error. `repair_abstract` re-resolves the old guessed
entries in binding.json in place (those with "abstract" and no "proof") every time bind.py runs.

| member | slot | proof |
|---|---|---|
| MainModeButton BaseScale.get / ImageScale.get, set / ImageRotation.get, set / ImageY.get, set / MenuAction | 0xa8 / 0xb0, 0xb8 / 0xc0, 0xc8 / 0xd0, 0xd8 / 0xe0 | 8 subclasses agree; FightButton disassembly: BaseScale = `fmov s0, #1.0`; ImageRotation reads +0x40 and sets it on image and imageOutline; ImageY.get tail-calls GraphicsComponent.get_Y; MenuAction is the full method |
| PlayerInput get_Alt2Icon / get_Name | 0xf8 / 0x168 | JoyConInput and KeyboardInput agree |
| MenuItem's 5, PlayerInput's menu getters (MenuConfirm 0x38 ...) | unchanged | 44-45 / 2 subclasses |

Coverage (a fresh run, master 3e4cd18): 353 -> 390 of 476 applicable (409 -> 446 of 542): CompactCoOpButton 0 -> 18/18,
NetplayMenuButton 0 -> 10/10, NetplayButton 0 -> 9/10. Module build with NetplayMenuButton and CompactCoOpButton as local-only units:
OK (their EnsureClass checks each slot is null in MainModeButton before overriding it). bind.py caches the subclass map and each
vtable, so a repairing run takes about a minute.

**Check:** `tools/tfex/abstract-slots.py [Type...]` lists every abstract method with its derived slot (or why it stays unresolved:
no subclass overrides it, or they disagree) and checks binding.json's abstract entries against it. Exit code 1 on a mismatch or an
unproven old guess. An abstract slot no subclass confirms is never emitted: bind.py returns an error for it (unresolved, not guessed).

## Vtable sizes (bind::vb_*, 2026-10-17)

A generated subclass copies vb_<Base> bytes of its base's vtable. bind.py counted slots up to the first null slot that vtable.py hadn't
labelled abstract (or, when vtable.py failed, the first null), so a class with abstract slots came out short: MainModeButton 0xa8 (below
MenuItem's 0xb8: its 8 abstract slots and Rotate .. PlaySound, 0x108/0x110 were not copied, and the generated NetplayMenuButton
crashed in its first Update; EnsureClass's "slot is null = abstract" check passed vacuously beyond the copy).
Now `vtable_bytes`: up to the interface table, which the descriptor's third word points at (desc[1] = the vtable), or up to the
descriptor when that table lies elsewhere; never smaller than the base's (else an error). Value types and other non-class vtables (the
descriptor isn't right after the vtable) keep the old count of relocated pointers and aren't checked. `repair_vtbytes` fixes binding.json
in place on every bind.py run.
| class | was | now | check |
|---|---|---|---|
| Monocle.Entity / TowerFall.MenuItem / BorderButton | 0x90 / 0xb8 / 0xb8 | same | tfex/Game.hpp EntityVtableBytes 0x90; BorderButton adds no virtuals |
| TowerFall.MainModeButton | 0xa8 | 0x118 | its 8 subclasses: 0x118 |
| TowerFall.MapButton | 0x80 | 0xd0 | base Entity 0x90; its 5 subclasses: 0xd0 |
| TowerFall.PlayerInput | 0x20 | 0x178 | its last three slots are abstract (null); JoyConInput, KeyboardInput: 0x178 |
| TowerFall.Arrow | 0x168 | 0x260 | base Actor 0x170 (was smaller than its base); its 11 subclasses: 0x260 |
All 14662 types: no class vtable smaller than its base's. `abstract-slots.py` now also lists every binding.json vtable size against the
derived one (exit 1 on a difference).
