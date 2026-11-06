/* TF.EX's map screen at a lobby match start (TF.EX.Patchs/Scene/MapScene.cs MapScenePatch, the MapScene parts of the input patches
 * XGamePadInput.cs / KeyboardInput.cs, and RollCallElement.cs StartVersus_Postfix's fade), on the Switch's vanilla MapScene.
 * docs/ws-map-select.md. */
#pragma once

namespace tfex::MapScenePatch {
/* exlaunch hooks (once, from xplay_main). */
void Install();
/* The lobby match's map screen: from the lobby screen (a MainMenu) RollCallElement.StartVersus_Postfix's MainMenu.State = Fade with
 * FadeAction = GotoVersusLevelSelect; from the finished match's Level (RematchLobby) Engine.Scene = the MapScene at once. tower /
 * wide: the tower TF.EX plays (tfex::Tower::Select); random: the screen starts on RANDOM and rolls to it, else on its button. */
void Enter(int tower, bool wide, bool random);
/* The map screen's StartSession has run, or the match was dropped before it (NetplayManager.Reset): no more map screen patches. */
void Reset();
/* The map screen this StartSession came from was TF.EX's generated one (a standard netplay lobby): its tower pick stands. */
bool StartedWithGen();
bool Active();
}
