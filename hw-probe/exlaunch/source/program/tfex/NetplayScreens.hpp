/* TF.EX's netplay screens (NetplayScreens.cpp): the custom MainMenu states and their items. */
#pragma once
#include "gen/tfexgen_enums.hpp"
namespace tfex::NetplayScreens {
/* the states: TF.EX's MenuState (generated from its source: NetplayScreens::LobbyBrowser, SeriesLobby, Changelog ...) */
using namespace ::tfex::gen::en::MenuState;
bool IsCustom(int state);
bool IsName(void* str, const char* s);   /* a managed string == an ASCII literal (CallStateFunc's "Create" / "Destroy") */
void Install();
int BrowserRows();               /* LobbyBrowser: lobbies listed (the auto-UI joins the first) */
void CodeEntryConfirm(void* entry);  /* TFEX-DIFF: JoinCodeEntry.OnConfirm: an empty code pastes (the keyboard) first, then TF.EX's Submit */
void SetBuilderVariants(const char* titles);   /* tests: hw.txt lobbyvariants TITLE|TITLE turns those builder variants on */
void SetBuilderPreset(char what, int value);   /* tests: hw.txt lobbymode 'm' / lobbymap 'p' / lobbylength 'l' / lobbymax 'x' (LOBBY SIZE) preset TF.EX's LobbyBuilder */
void* KeyboardCode();               /* TFEX-DIFF: TF.EX's clipboard paste (MainMenuPatch.ReadClipboard) = the Switch's software keyboard */
void Frame(void* menu);             /* each main-menu frame */
void SetTestJoinCode(const char* code);   /* hw.txt "joincode XXXXX": PrivateJoinCode uses it instead of the keyboard (tests) */
}
