/* The end of a lobby match: results, the vote menu (REMATCH! / ARCHER SELECT / QUIT), rematch and archer select (EndOfMatch.cpp).
 * Public hooks for xplay (OnLobbyResultsFinished, MainMenuPatch::InEndgame / SetAutoVote) keep their names. */
#pragma once

namespace tfex::EndOfMatch {
void Install();          /* the vanilla VersusMatchEnd pause-menu hooks (once, from MainMenuPatch::Install) */
void Frame(void* menu);  /* every frame (MainMenuPatch::Update): test votes, the Switch's teardown after TF.EX's QUIT / ArcherSelectLobby */
bool AwaitingVote();     /* the match ended; waiting for the server's ArcherSelectLobby / RematchLobby */
void RematchMapScene();  /* MapScenePatch: TF.EX's RematchLobby MapScene begins: the Switch's native lobby start */
void* GenNetplayManager();   /* TF.EX's generated NetplayManager object (made at the first TFGame.Update: MakeNetplayManager) */
/* TF.EX's NetplayManager, made once as TF.EX's ServiceCollections does at load: at the first TFGame.Update, before TF.EX's own code runs
 * (xplay.cpp FrameShell), so every INetplayManager call reaches it (the translator's dispatch, no null guards) */
void MakeNetplayManager();
void DescribePauseMenu(char* out, int cap);   /* TEST ONLY: the last updated PauseMenu's items and index */
bool MatchStarted();     /* NetplayManager started the lobby's match (StartLobby) */
void SetMatchStarted();
unsigned long TestButtons();   /* hw.txt lobbyvote: controller 1's buttons this frame (Npad bits), for xplay's controller hook */
int Matches();           /* lobby matches finished since boot (the debug driver leaves after the first) */
/* The match start is TF.EX's generated code: its NetplayRoundLogic (xplay.cpp GetRoundLogicHook) calls NetplayManager.Init at the first round's
 * OnLevelLoadFinish, and TF.EX's TFGame.Update prefix (generated) calls EstablishSessionIfSynchronized before GGRS's first advance, i.e. before the rollback log
 * starts: OnSessionEstablished -> SpawnFirstRound (NetplayRoundLogic.SpawnRoundPlayers). */
bool GenStart();                 /* TF.EX's NetplayManager made */
/* xplay (BeforeTfexUpdate), before TF.EX's prefix and so before EstablishSessionIfSynchronized: xplay's input delay from TF.EX's GGRSConfig, once per match */
void SyncInputDelay();
}
