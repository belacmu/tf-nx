/* FrVersusModes: FortRise's IVersusGameMode called on a registered mode object, dispatched to the class that implements it (FrVersusModes.cpp) */
#pragma once
#include "runtime/tfexgen_code_rt.hpp"

namespace tfex::frvmodes {
void* Name(void* mode);
gen::Color NameColor(void* mode);
void* Icon(void* mode);
bool IsTeamMode(void* mode);
void OnStartGame(void* mode, void* session);
void* OnCreateRoundLogic(void* mode, void* session);
int32_t GetMinimumPlayers(void* mode, void* matchSettings);
int32_t GetMinimumTeamPlayers(void* mode, void* matchSettings);
}
