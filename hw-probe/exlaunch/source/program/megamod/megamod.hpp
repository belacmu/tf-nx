#pragma once
/* The megamod (formerly ../combined-mod, docs/PLAN-MEGAMOD-2026-10-30.md): TowerBall (mode 6), 8 players in the 2-8 lobby and three
 * teams in Team Deathmatch, for local play. Offline is the megamod's, online is TF.EX's: every megamod hook calls straight through
 * while TF.EX's online mode is on (pcgameplay::On: NETPLAY's ApplyNetplayMode .. ClearNetplayMode, or a lobby / test match), and every
 * instruction or data word it changes goes back to the game's while that mode is on (SetOnline). Except the 8-seat substrate (eightplayer.hpp:
 * the player arrays, loop bounds and Session counts, MatchTeams' seats 6-7, the roll call's and results' 7-8 pieces), which a wide TF.EX
 * lobby past 6 seats needs as WiderSet's on the PC: all of it inert with 6 seats or fewer. Its hooks install after
 * switch-online's, so they are the outermost of each chain (exlaunch: the later install wraps the earlier). */
#include <cstdint>
#include "../hwlog.hpp"

namespace pcgameplay { bool On(); }

#ifndef XPLAY_EXTRAS
#define XPLAY_EXTRAS 1   /* (hw-probe/exlaunch/config.mk sets it for every file) */
#endif

namespace megamod {
inline bool Online() { return pcgameplay::On(); }
#if XPLAY_EXTRAS
/* A word the megamod changes for local play (an instruction, a cap table entry, a UTF-16 label), checked against `expect`: written now
 * (local play at boot) and put back while online. A word that is neither is left alone and logged. Table: kMaxPatches (log at install). */
void Patch(uintptr_t off, uint32_t expect, uint32_t repl, const char* what);
void SetOnline(bool on);   /* pcgameplay::Set */
void SetOnlineWord(uintptr_t off, bool local);   /* online: a Patch word the 8-seat substrate needs, the megamod's (local) or the game's value */
void Install();            /* main.cpp: the patch-word table's pool (the mods themselves: mods/<name>/src) */
/* Tests (megamod/localtest.cpp): hw.txt "localvs <players> <mode>" */
void SetLocalTest(int players, int mode);
void SetLocalArcher(int steps);   /* hw.txt "localarcher <steps>": controller 1 steps Right before confirming */
void SetLocalAlt();
void SetLocalDump(int hide);               /* hw.txt "localdump": player 1's graphics in the log */               /* hw.txt "localalt": player 1 plays its archer's alt */
void SetLocalModeName(const char* name);
void SetLocalVariant(const char* name);
void SetLocalNarrow();
void SetLocalAiSteps(const char* steps);   /* localaisteps: archer-select steps after the AIs (L r l u d c) */
void SetLocalAiNext();   /* localainext: the last AI switched to the next AI type */   /* localnarrow: the normal set on VERSUS, not WIDE */
void SetLocalTeamSteps(const char* steps);   /* localteamsteps: controller 1's right stick on Teams+'s team screen (r l u d) */
void SetLocalAi(int n);   /* localai <n>: n AI players put in with the right stick on archer select */
void LocalTestStick(uint32_t id, int32_t* rx, int32_t* ry);   /* the test's right stick for controller id */
void SetLocalTower(int rights);   /* localtower <n>: Right n times on the tower select */   /* localvariant <mod>/<id>: a FortRise custom variant on before START (Move or Die) */   /* localmode <name>: a custom mode by its GAME MODE name (Teams+'s TEAMS+) */
void SetLocalKill();
void SetLocalRoundEnd();   /* localroundend: everyone but player 1 dies 15 s in (a round's end, the results board) */   /* hw.txt "localkill": every player dies 5 s into the level */
void SetLocalVictory();   /* hw.txt "localvictory": player 1's victory music 5 s into the level */
bool LocalTestOn();
unsigned long LocalTestButtons(uint32_t id);   /* controller id's buttons this poll (id 0 advances the driver) */
#else
/* "TF.EX Switch" (XPLAY_EXTRAS 0): no megamod in the module (megamod.cpp and localtest.cpp compile to nothing); the game's local play */
inline void SetOnline(bool) {}
inline void Install() { ::hw::Log("[megamod] not in this build (TF.EX Switch, no extras): the game's local play"); }
inline void SetLocalTest(int, int) { ::hw::Log("[megamod] localvs: not in this build (no extras)"); }
inline void SetLocalKill() {}
inline void SetLocalRoundEnd() {}
inline void SetLocalModeName(const char*) {}
inline void SetLocalVictory() {}
inline void SetLocalArcher(int) {}
inline void SetLocalAlt() {}
inline void SetLocalDump(int) {}
inline bool LocalTestOn() { return false; }
inline unsigned long LocalTestButtons(uint32_t) { return 0; }
#endif
}

#define MMLOG(...) ::hw::Log(__VA_ARGS__)
/* First line of every megamod hook: online, the call goes on down the chain untouched. */
#define MM_ONLINE_PASS(call) do { if (::megamod::Online()) return call; } while (0)
