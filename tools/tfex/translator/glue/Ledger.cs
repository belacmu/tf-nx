// The Switch's intentional differences (TFEX-BASE.md section 3) that replace a TF.EX member's body (translate.toml [[replace]]).
using System;
using System.Collections.Generic;
using System.Linq;
using Monocle;
using TowerFall;

namespace TfexGlue
{
    public static class Ledger
    {
        // TFEX-DIFF: S-rollback. Notification's `new Canvas(length * 10, 14)` per toast: a render target (native, outside the undo log) made
        // inside a simulated tick, so each resimulation made another. A Canvas shared per size, made in the menu (tfex/Notification.cpp).
        // Weak until tfex/Notification.cpp has it (ws/tfex-subclass): without it, the creation itself ($new).
        [Cpp("(tfex::gen::ledger::HasSharedCanvas() ? tfex::Notification::SharedCanvas($0, $1) : $new)", Include = "tfexgen_ledger.hpp")]
        public static extern Monocle.Canvas SharedCanvas(int width, int height);

        // TFEX-DIFF: S-wide / S-hud. Notification.CenterX asks WiderSet's API (DI) for UIXOffset: (420 - 320) / 2 on a wide level. On the
        // Switch both of its layers draw in the console's 420-wide canvas: a Level's HUD layer (4) in either mode (the 320-wide level sits
        // at x 50..370, tfex/SwitchHud.hpp kCanvasX) and the MainMenu's layer 0 (the Switch's menus are 420 wide), so the centre is 210
        // for both (2026-11-01: level toasts 50 px left of centre; 2026-11-07: menu toasts, the mods warning and lobby notices, too).
        public static float NotificationCenterX(int layerIndex) => 160f + 50f;

        // TFEX-DIFF: S-native. FortRise's custom archers (ArcherRegistryApi) and custom variants (MatchVariants.CustomVariants, a FortRise
        // addition to TowerFall) don't exist on the AOT build: TF.EX's own bodies with an empty registry.
        public static string NoCustomArcherId(int archerIndex, int altIndex) => "";
        public static int NoCustomArcher(string customArcherId) => -1;
        public static List<string> NoArcherMods(Func<string, string> versionLookup) => new List<string>();
    }
}
