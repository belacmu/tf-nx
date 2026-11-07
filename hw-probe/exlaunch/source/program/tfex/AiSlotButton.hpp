/* AiSlotButton.cpp: the console's button for LoaderAI's AI slots on archer select (TFEX-DIFF S-input) */
#pragma once
#include <cstdint>

namespace tfex::aislot {
void Install();
/* LoaderAI's name for player i when that player is an AI ("JIMMY 3": a managed string), else null (glue AiNames) */
void* AiNameOf(int32_t playerIndex);
/* the round results board: `names` AI names put in, its rows laid out again (glue AiNames, its log line) */
void BoardLaidOut(int32_t names, int32_t labels);
/* online mode starts (pcgameplay::Set(true)): every AI archer out of the player slots; no AI archers in online games */
void DropAll();
/* TEST ONLY (hw.txt "aiseat <n>", n from 1: LoaderAI's AI types in registration order): the AI plays the Switch's local online seats */
void SetAiSeat(int n);
bool AiSeatOn();
}
namespace tfex::gen { template <int N> struct GameVal; }
namespace tfex::aislot {
/* the AI's InputState (28 bytes) for local seat `seat` at GGRS frame `frame`; false when there is no AI or no level */
bool AiSeatState(int32_t seat, int frame, tfex::gen::GameVal<28>* out);
}
