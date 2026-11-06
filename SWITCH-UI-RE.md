# Switch (1.4.1.6) menu internals for the online UI

Goal (user, 2026-10-15): the Switch's online UI is TF.EX's, as identical as possible: TF.EX's NETPLAY icon on the main menu, its
netplay selection screen, the party/lobby path, the lobby screen, the end-of-match choices. TF.EX's screens: TFEX-UI-SPEC.md; its
lobby protocol: LOBBY-PROTOCOL.md. This file collects what the Switch build offers to rebuild them (found at run time with the probe's
`[menu]` debug hooks, and in `tools/re/main.flat`).

## Main menu (vanilla Switch)

`MainMenu.CreateMain` +0xaa0bd0. It builds `List<MenuItem>` (+0x971a50, `Add` +0x971b20), then each button with a factory
`obj = F(nullptr, Vector2 position (s0,s1), Vector2 tweenFrom (s2,s3))`:

| button | factory | position | tweenFrom | title / players text |
|---|---|---|---|---|
| FightButton | +0xa428f0 | (100, 140) | (-160, 120) | VERSUS |
| CoOpButton | +0x9d5f50 | (220, 140) | (480, 120) | CO-OP / 1-4 ARCHERS |
| ArchivesButton | +0x97c8e0 | (270, 210) | (270, 300) | ARCHIVES |
| TrialsButton | +0xb885f0 | (200, 210) | (200, 300) | TRIALS |

then BladeButtons `+0x9a42c0(nullptr, string text, Action action, float y)`; the Action is built by `+0x17a7d0(nullptr, target, method)`
with the method from `+0x2f20(...)`. Every main-mode button runs the base constructor `MainModeButton(pos, tweenFrom, text, playersText)`
+0xaa6700 (the factories run in table order, which is how they map to the classes).

Inside ArchivesButton's factory: `TFGame.MenuAtlas` getter +0xb747f0, `Atlas[string]` +0x904b10 -> Subtexture, `new Image(Subtexture)`
+0x935cc0, `Image.CenterOrigin` +0x932b20, `Entity.Add(Component)` +0x92f9d0. A new menu-atlas image (the NETPLAY icon) can be added to
the romfs `Content/Atlas/menuAtlas.{png,xml}` with the roster mod's tools (`tools/inject_sprites.py` blits in place;
`additional-characters/.../build_unit.py` repacks for new names).

TF.EX's change (PC): NetplayButton at (220, 140), the Co-Op button replaced by its CompactCoOpButton at (290, 180), the buttons of the
y = 210 row moved down 10 (tweenTo too). Details: TFEX-UI-SPEC.md.

## Object layouts (Switch)

| field | offset |
|---|---|
| Entity.Active/Visible/Collidable | +0x10/+0x11/+0x12 |
| Entity.Position X, Y | +0x14, +0x18 |
| MenuItem DownItem, UpItem, RightItem, LeftItem | +0x78, +0x80, +0x88, +0x90 |
| MainModeButton title, players (OutlineText, set in Added) | +0xa0, +0xa8 |
| MainModeButton sine | +0xb0 |
| MainModeButton text, playersText (string) | +0xc8, +0xd0 |
| MainModeButton pressed | +0xd8 |
| MainModeButton tweenFrom, tweenTo (Vector2) | +0xdc, +0xe4 |
| ArchivesButton image, glow (Image) | +0xf0, +0xf8 |
| FightButton leftArrow, rightArrow | +0xf0, +0xf8 |

Strings: class pointer, 8 zero bytes, int length at +0x10, UTF-16 characters from +0x14.

Virtual slots (resolver; check against a live vtable before hooking): MainModeButton.OnConfirm 0xa0, MenuAction 0xb0
(ArchivesButton/TrialsButton overrides), Added 0x30, Render 0x50 (ArchivesButton). MainMenu: Update 0x30, Render 0x40; its state is the
int at MainMenu+0xc0 (+0xa9ed20 is that field's plain setter: the state change happens in MainMenu.Update).
