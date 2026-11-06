/* FortRise's MODS menu on the Switch (ModsMenu.cpp). */
#pragma once
#include <cstdint>

namespace tfex::ModsMenu {
/* FortRise registers its menu states past the game's (CustomMenuStateRegistry); TF.EX's own end at Tutorial (72): MODS is the next */
constexpr int32_t kState = 73;
void Install();
void AfterCreateMain(void* menu);              /* the MODS button (FortRise's patch_MainMenu.CreateMain) */
bool CallStateFunc(void* menu, void* name, int32_t state);   /* Create / Destroy of the MODS state: true when handled */
}  // namespace tfex::ModsMenu
