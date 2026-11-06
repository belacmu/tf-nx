# WiderSet's own 8-player archer select on the Switch (planned 2026-11-06)

The user (2026-11-06): "our 8 player widerset mod should have taken the archer select screen / ui from Widerset", and chose to port
WiderSet's roll call UI in place of our hand-made one. The Switch's native wide towers and screen stay (wide-mode decision of
2026-10-18 holds for gameplay). Done means: WiderSet's code drives these screens ON by default, and the stand-in below is deleted.

## What the console shows now (stand-in, mods/Teuria.WiderSet/src/eightplayer.hpp)
- After FIGHT, the Switch's own SELECT LEVEL SET (STANDARD / 6-PLAYER). Its subtitle reads "SUPPORTS UP TO 6 ARCHERS" although
  OfferEight patches that UTF-16 literal (+0x37659f4, '6' -> '8') and the patch applies (no "left alone" line; one copy of the text
  in main.flat): the drawn string is not read from that word at draw time. Emulator shot 2026-11-06.
- The WIDE roll call: the Switch's 6 slots plus "DEV: rollcall slots 7+8 added (4+4 staggered)": portraits overlap, the top row is
  shifted, the FIGHT heading is cut (emulator shot 2026-11-06).

## WiderSet 0.9.0's UI (FortRise/ExampleFortRiseMod WiderSet/, origin/main 08c20ca "Push Version")
| WiderSet | what | Switch form |
|---|---|---|
| Core/StandardSelection.cs | its own menu state after FIGHT: STANDARD "2-4 ARCHERS" / WIDER "2-8 ARCHERS" buttons (MainModeButton), LevelSetDisplay, its tower sprites | translated (FortRise's MenuStates registry, sprites from its folder) |
| FightButtonHooks (postfix) | FIGHT goes to StandardSelection | translated |
| RollcallElementHooks GetPosition / GetTweenSource (prefixes), Render (prefix: "P1".."P8" over each slot) | two rows of 4: x 55 + 70 * (i % 4), y 75 / 165; rows slide in from opposite sides | translated |
| RollcallElementHooks transpilers: ctor (x 60 -> 30), NotJoinedUpdate (4 -> 8, MenuState), HandleControlIcons / Update (ControlIconPos - (15, 120)), Render (input null / DarkWorldDLC), get_MaxPlayers (4 -> 8, 2 -> 4) | the Switch's AOT code: native patches (`native =`, as Teams+'s IL edits) |
| ArcherPortraitHooks (6 transpilers: SetCharacter, StartJoined, Leave, InitGem, ctor, Render) | the smaller portraits for two rows | native patches |
| MainMenuHooks (Begin postfix, Update prefix: PadOffset tween; CreateRollcall / CreateTeamSelect / Render transpilers; GotoVersusLevelSelect postfix: its tower set) | the wide menu | prefixes / postfixes translated, transpilers native |
| MenuButtonsHooks, MenuInputHooks, TFGameHooks (4 -> 8 transpilers) | input and seat counts | native (most already widened by eightplayer.hpp's array / bound patches) |
| MatchTeamsHooks, VersusStartHooks | team select and the VERSUS start card for 8 | translated / native |

## Steps (one at a time, AGENTS.md)
1. vendor/widerset: a submodule at WiderSet 0.9.0's commit; translate.toml [code_mods] "widerset/" limited to the UI classes above
   (the rest of WiderSet stays the Switch's native wide mode); each transpiler a native patch with its one-line reason.
2. StandardSelection + FightButton: the STANDARD / WIDER screen; delete the SELECT LEVEL SET label patches.
3. The roll call: GetPosition / GetTweenSource / Render / transpilers; delete eightplayer.hpp's "4+4 staggered" layout and its slot 7+8
   adder. Check: localrun 8 3 with a screenshot of the roll call (two rows of 4, P1..P8), the AI controls' cursor label on it.
4. Portraits, team select, VERSUS start card.
5. Land on master, publish; console checklist in STATUS.md.
