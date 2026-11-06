/* FortRise content mods: the boot call (OUR OWN CODE). Where FortRise loads them: in the game's load sequence, right after
 * GameData.Load (FortRise's TFGame.Load patch calls RiseCore.Initialize there), once. */
#pragma once

namespace tfex::frmods {
void Install();   /* hooks GameData.Load (main.cpp, after exl::hook::Initialize; installs only) */
}
