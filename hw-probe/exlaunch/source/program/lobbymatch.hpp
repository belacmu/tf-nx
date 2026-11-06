/* A lobby match's setup, from tfex/NetplayManager.cpp (StartLobby) to xplay's native start. */
#pragma once
namespace xplay {
/* The seat capacity of the lobby match's fixed arrays (the session's inputs, seats, teams; tfex/NetplayManager.cpp's players by seat).
 * Not the rule: how many play is the lobby's (TF.EX: TFGame.Players.Length in a wide lobby, 4 otherwise; the Switch's Players is 8,
 * megamod/eightplayer.hpp). A lobby with more players than this is refused with an ERROR line (a pools error): raise it with the game's seats. */
constexpr int kNpSeats = 8;
struct LobbyMatch { int seed, handle, players, goal; int tower; int mode; int team[kNpSeats]; bool wide; bool randomMap; };
/* tower: GameData.VersusTowers index (wide: the 6P towers' index, tfex::Tower::SelectWide), -1 = keep; wide: the lobby is WiderSet wide; mode: GameData.Mode (3 LMS, 4 Head Hunters, 5 Team Deathmatch); team: per handle (seat order),
 * the lobby players' Team (0 Blue, 1 Red, -1 none): TF.EX's ApplyTeamsToMatchSettings; randomMap: the map screen starts on RANDOM and
 * rolls to `tower` (MapId -1, or no button with the lobby's MapId), else it starts on `tower`'s button (tfex/MapScenePatch.cpp) */
void* LobbyLevelSystem(int tower, bool wide);   /* tfex: VersusTowers[tower] (wide: the 6P towers') .GetLevelSystem() */
void StartLobbyMatch(const LobbyMatch& m);
/* TF.EX's NetplayManager.Reset: the finished lobby match's session and settings (before going back to the lobby screen). */
void EndLobbyMatch();
/* The rest of the lobby match's start, in the map screen's MapScene.StartSession (tfex/MapScenePatch.cpp, TF.EX's StartSession prefix
 * and postfix): the match settings the MapScene doesn't make, the controller slots, the GGRS session. Then the game's StartSession. */
void LobbyStartSession();
}
