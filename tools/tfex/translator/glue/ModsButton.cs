// FortRise's MODS entry on the Switch's main menu: OUR OWN CODE, translated. TFEX-DIFF S-native: FortRise's patch_MainMenu.CreateMain is
// a MonoMod replacement of the whole method (its own copy of the PC's layout); the Switch keeps the game's CreateMain (the console
// layout), and after it this adds what FortRise's adds: the MODS BladeButton above OPTIONS (18 higher, X -50: patch_BladeButton.SetX),
// its links, the selection when coming back from MODS, and the MODS title (patch_ScreenTitle's ctor: menuTitles/options). Runs before
// TF.EX's CreateMain postfix, as on the PC (tfex/MainMenuPatch.cpp CreateMainHook). The MODS screen: FortRise's UIModMenu (ModsMenu.cpp).
using System.Collections.Generic;
using HarmonyLib;
using Microsoft.Xna.Framework;
using Monocle;
using TF.EX.Domain.Extensions;
using TowerFall;

namespace TfexGlue
{
    public static class ModsButton
    {
        [Cpp("tfex::ModsMenu::kState", Include = "../ModsMenu.hpp")] private static extern int ModsState();
        [Cpp("tfex::ModsMenu::kToggleState", Include = "../ModsMenu.hpp")] private static extern int ToggleState();

        public static void AddToMain(MainMenu menu)
        {
            var modsState = (MainMenu.MenuState)ModsState();
            BladeButton options = null;   // the top blade button: OPTIONS
            foreach (var b in menu.GetAllToBeSpawned<BladeButton>())
            {
                if (options == null || b.Position.Y < options.Position.Y) options = b;
            }
            if (options == null) return;

            var mods = new BladeButton(options.Position.Y - 18f, "MODS", () => menu.State = modsState);
            SetX(mods, -50f);
            var fight = menu.GetToBeSpawned<FightButton>();
            if (fight != null && fight.DownItem == options) fight.DownItem = mods;
            mods.UpItem = options.UpItem;
            mods.DownItem = options;
            mods.RightItem = options.RightItem;
            options.UpItem = mods;
            if (menu.OldState == modsState) menu.ToStartSelected = mods;
            menu.Add(mods);

            var title = menu.Get<ScreenTitle>() ?? menu.GetToBeSpawned<ScreenTitle>();
            if (title != null)
            {
                var textures = Traverse.Create(title).Field<Dictionary<MainMenu.MenuState, Subtexture>>("textures").Value;
                textures[modsState] = TFGame.MenuAtlas["menuTitles/options"];
                textures[(MainMenu.MenuState)ToggleState()] = TFGame.MenuAtlas["menuTitles/options"];   // TOGGLE MODS (FortRise registers it with no title of its own)
            }
        }

        // FortRise's patch_MainMenu.CreateOptions when MODS opened a mod's settings (FilterMod): only that mod's entries (its InitModOptions:
        // TF.EX's NetplaySettings, the only settings on the Switch, which tfex/NetplayOptions.cpp's InitOptions hook adds), and B goes back
        // to MODS; the first entry is selected (list[1]: list[0] is the mod's header). False (the game's CreateOptions runs) with no mod picked.
        public static bool CreateFilteredOptions(MainMenu menu)
        {
            if (FilterMod() == null) return false;
            var list = new List<OptionsButton>();
            InitOptions(menu, list);
            if (list.Count > 1) menu.ToStartSelected = list[1];
            menu.BackState = (MainMenu.MenuState)ModsState();
            menu.TweenBGCameraToY(1);
            return true;
        }
        [Cpp("tfex::gen::GlueFrMods::GetFilterMod()", Include = "tfexgen_GlueFrMods.hpp")] private static extern object FilterMod();
        [Cpp("gen::Fn<void (*)(void*, void*)>(0xaa38d0)($0, $1)")] private static extern void InitOptions(MainMenu menu, List<OptionsButton> buttons);   // MainMenu.InitOptions (private; classmeta)

        // FortRise's patch_BladeButton.SetX: the button's X, and the tween / selected positions that follow it
        private static void SetX(BladeButton button, float x)
        {
            button.Position.X = x;
            Traverse.Create(button).Field("tweenTo").SetValue(button.Position);
            Traverse.Create(button).Field("tweenFrom").SetValue(button.Position + Vector2.UnitX * -(100f - x));
            Traverse.Create(button).Field("selected").SetValue(button.Position + new Vector2(x + (-x + 30), 0f));
        }
    }
}
