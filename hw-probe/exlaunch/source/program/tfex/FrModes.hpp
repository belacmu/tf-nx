/* FrModes: the Switch's inlined MatchSettings / Session team code under FortRise's custom modes (FrModes.cpp) */
#pragma once
#include <cstdint>

namespace tfex::frmodes {
void Install();
bool ModeButtonSelected();   /* the GAME MODE button selected at its last update (the local-play test driver) */
/* while alive: Session code that inlined GetScoreIndex (TeamMode ? GetPlayerAllegiance(i) : i) gets the real GetScoreIndex for its
 * GetPlayerAllegiance call (Session.EndRound's: xplay.cpp's shell opens one) */
/* a mod's native form of an IL edit that replaces one call of MatchSettings.GetPlayerAllegiance (Teams+'s OnPlayerDeath transpiler):
 * that call site, by its return address (module offset), answered by fn; false when the table (4) is full (logged) */
using AllegianceAt = int32_t (*)(void* settings, int32_t playerIndex);
bool RedirectAllegianceCall(uintptr_t returnOffset, AllegianceAt fn, const char* what);
struct ScoreIndexScope {
    void* prevSession;
    explicit ScoreIndexScope(void* session);
    ~ScoreIndexScope();
};
}
