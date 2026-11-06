/* TF.EX's online menu on the Switch: TF.EX.Patchs.Scene.MainMenuPatch, TF.EX.Patchs.Entity.MenuItem.RollCallElementPatch /
 * ReadyBannerPatch and the custom components (NetplayButton, CompactCoOpButton), mirrored on the vanilla Switch objects
 * (switch-online/TFEX-UI-SPEC.md sections 2-3, docs/ws-lobby-ui.md). Same names as TF.EX where there is a TF.EX counterpart. */
#pragma once

namespace tfex {
namespace models { struct Lobby; }

/* TF.EX's VersusModeExtensions: the versus settings are in netplay mode (entered with NETPLAY, left on the main menu). */
bool IsNetplay();
void ApplyNetplayMode();
void ClearNetplayMode();

namespace MainMenuPatch {
/* exlaunch hooks (once, from xplay_main). */
void Install();
/* Every TFGame.Update (the frame hook), before the game's update: TF.EX's MainMenu.Update prefix work for the lobby screen while the
 * scene is a MainMenu, and the lobby's server events (start, end of match -> back to the lobby) in any scene. */
void Update();
/* Debug driver for hidden test runs (hw.txt "lobbyui <n>"): presses the menu buttons a player would (NETPLAY, ready, start, ...). The
 * buttons it holds this frame, as nn::hid Npad bits, for the frame hook's controller hook. */
void InstallLate();   /* hooks found on live objects (each frame until done) */
void SetLobbyLength(int matchLength);   /* hw.txt "lobbylength": the hosted lobby's MatchLength (TF.EX default 2) */
void SetLobbyMap(int mapId);            /* hw.txt "lobbymap": the hosted lobby's MapId (TF.EX default -1 = random) */
void SetLobbyMode(int mode);           /* hw.txt "lobbymode 3|4|5": the hosted lobby's GameData.Mode (until LobbyBuilder) */
void SetLobbyTeam(int team);           /* hw.txt "lobbyteam 0|1": this player's first team in a team lobby */
bool InEndgame();          /* a lobby match's results screen runs locally (after TF.EX's NetplayManager.Reset) */
void SetAutoVote(int v);    /* hw.txt "lobbyvote 1|2": test console votes REMATCH! / ARCHER SELECT 3 s after the results */
void SetEditUi(int on);      /* hw.txt "lobbyedit 1": the auto-UI edits the lobby (START on its seat) before readying */
void SetQuickUi(int on);     /* hw.txt "lobbyquick 1": the auto-UI picks QUICK PLAY */
void SetPrivateUi(int on);   /* hw.txt "lobbyprivate 1": the auto-UI hosts through PRIVATE -> CREATE */
void SetJoinMode(int mode);   /* hw.txt "lobbyjoin 1": NETPLAY joins the first joinable lobby instead of hosting (until LobbyBrowser exists) */
void SetAutoUi(int mode);
void SetLobbyArcher(int archer);
void SetQuickWait(int seconds);   /* hw.txt "lobbyquickwait S" */   /* hw.txt "lobbyarcher N": the auto-UI picks archer N before readying */
void SetLobbyMax(int n);   /* hw.txt "lobbymax": MaxPlayers of the hosted lobby (2..4; v1 default 2) */
void SetDelaySteps(int n);
void SetOptionsDrive(int on);   /* tests: hw.txt optionsdrive 1 (the OPTIONS screen's TF.EX entries) */
void SetDelayButton(const char* b);   /* tests: hw.txt lobbydelaybutton L|ZL */   /* tests: hw.txt lobbydelaysteps N (the roll call's input-delay gesture, N taps) */
void SetWide(int on);      /* hw.txt "lobbywide 1": WiderSet's wide set (hosted lobbies IsWide True, joins wide lobbies only, quick play wide) */
bool IsWide();
/* around TF.EX's InputDelayAdvisor.Render from its MainMenu.Render postfix (a [[rewrite]], glue Rewrites.InputDelayAdvisorRender). TFEX-DIFF
 * S-wide: in the wide layout's roll call the Switch shows its own six-seat roll call across the 420-wide screen, and its guides (EDIT
 * LOBBY) at the screen's right; TF.EX anchors the advisor to a 320 screen's right edge (318), so it is drawn UIXOffset (50) right, on
 * the same right edge; in its left layout (beside COPY CODE) the guides' margin moves them onto the screen's left edge (AdvisorLeftMargin).
 * The series lobby and the standard layout as TF.EX. */
bool AdvisorShiftBegin();   /* true: shifted until AdvisorShiftEnd */
void AdvisorShiftEnd();
float AdvisorLeftMargin();   /* InputDelayAdvisor.GuideRightMargin's [service] rule */
const void* LiveString();   /* a managed string to copy the string class from (MakeString), once the main menu was built; else nullptr */
unsigned long AutoUiButtons();
void SetBackOut(int on);   /* hw.txt lobbyback 1: the host backs out of the ready lobby (tests) */
unsigned long AutoUiLocalButtons(int id);   /* hw.txt lobbylocal <n>: controller id+1's presses (local players 2..n) */
int TestLocalPlayers();
void SetLocal2(int on);
void SetLocalPlayers(int n);
}

} // namespace tfex
