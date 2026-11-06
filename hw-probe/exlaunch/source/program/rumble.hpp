/* Controller rumble during a lobby match (rumble.cpp, docs/ws-rumble.md): only this console's own seat, once per event, never from
 * resimulated ticks, cancelled when a rollback undoes its event, stopped at the match end. Offline play is unchanged. */
#pragma once

namespace rumble {
void Install();
/* hw.txt "rumblefix 0": MatchStart leaves everything alone (the hooks then do exactly what the game's code did). Default on. */
void SetEnabled(bool on);
/* After the lobby start filled the empty PlayerInputs slots: the local seat keeps the console's controller object, every other seat of
 * the match that held it gets a controller object of its own (the game's constructor, same JoyCon) whose rumble goes nowhere. */
void MatchStart(int localSeat, int players);
/* Stops the vibration and puts the controller object back in the slots MatchStart changed (before EndLobbyMatch empties them). */
void MatchEnd();
/* Around every Level.Update during the match (`label` = Scene.FrameCounter, the GGRS frame + 1). */
void LevelUpdateBegin(int label, bool resim);
void LevelUpdateEnd();
/* After a rollback to the start of frame `to`: the rumbles of the frames about to be resimulated must come again or are cancelled. */
void OnLoad(int to);
/* Once per real frame, after its last Level.Update: rumbles a rollback undid are stopped. */
void Synchronize(int label);
}
