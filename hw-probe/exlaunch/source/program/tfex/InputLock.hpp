/* TF.EX's input gates on the Switch's JoyConInput, and the Switch's controller refresh under TF.EX's InputService (InputLock.cpp).
 * The lock itself is TF.EX's InputService (generated: gen/tfexgen_InputService.hpp). */
#pragma once
#include <cstdint>

namespace tfex::InputLock {
void BeforeMenuUpdate();                 /* MainMenu.Update, before the original: the refresh waits while TF.EX holds the input */
void RefreshInTfexScreens(bool tfexScreen, int state);   /* and: the Switch's controller refresh in TF.EX's screens (59-65) too */
bool MenuPressed(int button);           /* LightPauseMenu.Pressed: Confirm 0, Back 1, Up 2, Down 3, Start 4 (local JoyConInputs) */
void InstallLate();                      /* every frame until the JoyConInput gates are installed */
}
