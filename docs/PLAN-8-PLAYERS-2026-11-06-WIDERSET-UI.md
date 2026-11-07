# WiderSet's look for the 8-player archer select (2026-11-06)

The user asked for WiderSet's archer select on the 8-slot WIDE roll call and the "8" labels, and nothing more ("don't do more than is
needed": the full WiderSet translation, 7,355 lines with 130 IL transpilers, was dropped). Done in the stand-in
(mods/Teuria.WiderSet/src/eightplayer.hpp), each piece named after the WiderSet 0.9.0 hook it stands in for:
- GetPosition / GetTweenSource prefixes: two rows of four, x 55 + 70 * col, y 75 / 165; the top row slides in from the left, the
  bottom from the right (was a 4+4 staggered layout of full-height portraits that overlapped).
- Render prefix: "P1".."P8" over each slot in the player's colour.
- ArcherPortrait transpilers: the portrait (and flip side, join flash) cropped to the top 60 x 60 of its art from y 10 (archer 6
  not joined: y 60); ShowTitle off; the gem at +30 (InitGem); the archer arrows at Y 30 (ctor).
- HandleControlIcons / Update: ControlIconPos (-15, -45) (WiderSet's -30 left half of each icon under the portrait: the Switch draws
  them first); the Switch-only costume prompt (R / ?) static (22, 45) -> (22, 15).
- The labels: "2-8 ARCHERS", "8-PLAYER", "SUPPORTS UP TO 8 ARCHERS": the game makes its managed literal strings at startup, so the
  '6' is changed in those (the old main.flat word patches changed nothing on screen).
Left as they are: the join flash overflows for a moment on a slot that just joined; the AI cursor label on a top-row slot crosses the
FIGHT! title; each input's name line under its icon touches the portrait's top edge.

Found on the way (the console's archer-select crash): a portrait switching to a mod archer with its own menu gem (@Gem..._MENU: 13 of
the 18 SSB archers) threw KeyNotFoundException: FortRise's translated SpriteEntry.CreateProperty<T> wrote value.ToString() as a direct
Object.ToString call ("System.String" as every mod sprite's Texture; boxed numbers as "System.Int32"). The translator now emits a string's
ToString as the string and object's as the virtual call (gen::ObjToString).
