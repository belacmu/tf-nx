/* TF.EX's netplay settings on the Switch's OPTIONS screen (NetplayOptions.cpp): TF.EX's NetplaySettings.Create (generated) adds its entries
 * there (FortRise's module settings page on the PC; TFEX-DIFF S-native), FortRise's text input is the software keyboard. */
#pragma once

namespace tfex::NetplayOptions {
void Install();
void AddEntries(void* buttons);   /* TF.EX's settings into OPTIONS' list (FortRise's InitModOptions, glue SwitchMods) */
void* ReadKeyboard(void* initialValue);
/* tests: hw.txt "optionstext1..4 <text>" (queued, in order): the keyboard's text instead of opening it (a hidden emulator can't type),
 * and the OPTIONS screen's buttons for the auto-UI (hw.txt optionsdrive) */
void AddTestText(const char* text);
int FirstEntry();      /* the index of TF.EX's first entry in the OPTIONS screen's list, -1 before it is made */
int SelectedIndex();   /* the selected button's index there, -1 if none */   /* glue SettingsUi.KeyboardText: the typed text (a managed string), null when cancelled */
}
