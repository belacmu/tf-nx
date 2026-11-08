/* Test driver for the megamod's local play (hw.txt "localvs <players> <mode>": 2..8 controllers, MatchSettings.Mode 3 LAST MAN
 * STANDING, 4 HEADHUNTERS, 5 TEAM DEATHMATCH, 6 TOWERBALL). Controllers 1..N walk the menus as people would: PRESS
 * START, FIGHT, WIDE (the 2-8 roll call), each joins and confirms an archer, START; on the screens after the roll call the driver sets
 * the mode the way the mode button would (VersusMatchSettings.Mode) and presses START until the tower select, then A until the level.
 * Each screen it sees and each press goes to the log ("[mmtest]"); in the level, one line with the players and mode. Called from
 * xplay.cpp's GetFullKeyHook (the npad hook), per controller id. */
#include <cstdint>
#include <cstring>
#include "megamod.hpp"
#include "../tfex/gen/tfexgen_Fr_VersusModeButton.hpp"
#include "../tfex/gen/tfexgen_GlueFrMods.hpp"
namespace tfex::frmodes { bool ModeButtonSelected(); }   /* tfex/FrModes.cpp */
#include "../tfex/runtime/tfexgen_code_rt.hpp"
#include <cstdio>
#if XPLAY_EXTRAS   /* (megamod.hpp: "TF.EX Switch" builds leave the megamod out) */
#include "../tfex/Game.hpp"
#include "../rnd.hpp"
#include "../xplay_internal.hpp"
#include "../tfex/gen/tfexgen_binding.hpp"
#include "../tfex/runtime/tfexgen_code_rt.hpp"
#include "../tfex/runtime/tfexgen_state.hpp"

namespace megamod {
namespace {
using namespace tfex::game;
int g_players = 0, g_mode = 0, g_steps = 0;
bool g_roundEnd = false;   /* hw.txt localroundend: everyone but player 1 dies 15 s into the level (after the start overlay, when the game ends
                            * rounds; localkill's 5 s is still in it): one group stands, the round ends, the results board shows. Killed
                            * by player 1, but player 8 by player 7 (in an 8-player Teams+ match: a lone wolf kills a lone wolf) */
bool g_kill = false;   /* hw.txt localkill: every player dies 5 s into the level (a corpse, for the corpse tests) */
bool g_victory = false;   /* hw.txt localvictory: player 1's archer's victory music 5 s into the level (VersusRoundResults' call) */   /* g_steps: controller 1's Right presses on the roll call (hw.txt localarcher) */
int g_frame = 0, g_lastScene = -1, g_lastState = -1, g_since = 0, g_wait = 0, g_presses = 0;   /* g_since: on this screen; g_wait: since the last press */
int g_phase = 0;   /* 0 title .. 3 roll call: joins, 4 confirms, 5 start; 6 after the roll call; 7 tower select; 8 level */
unsigned long g_bits[8];
int32_t g_rx[8], g_ry[8];   /* the right stick (localai: the AI controls, tfex/AiSlotButton.cpp), held with the buttons */
int g_hold[8];
constexpr unsigned long kA = 1u << 0, kStickR = 1u << 5, kLeft = 1u << 12, kUp = 1u << 13, kRight = 1u << 14, kPlus = 1u << 10, kL = 1u << 6;
int g_towerRights = 0, g_towerMoved = 0;   /* localtower <n>: Right n times on the tower select (Soccer's check: Twilight Spire, no cobwebs) */
char g_variantName[48] = "";   /* localvariant <mod>/<id>: a FortRise custom variant on before START (Move or Die's MoveOrDie/MoveOrDie) */
bool g_variantDone = false, g_firstDeath = false;
char g_modeName[32] = "";   /* localmode <name>: a custom mode (FortRise's GAME MODE button, scrolled to as a player does) */
bool g_modeDone = false; int g_modePresses = 0;
bool g_narrow = false, g_aiNext = false;   /* localainext: the last AI's next AI type (a second click) */   /* localnarrow: the normal set (four slots), not WIDE: the Switch's every slot has a controller object there */
char g_teamSteps[32] = "";   /* localteamsteps <s>: controller 1's right stick on the team screen (Teams+'s, or the game's after the humans' picks), one step / 20 frames from 60: r l u d (tfex/AiSlotButton.cpp teams) */
char g_aiSteps[64] = "";   /* localaisteps <s>: controller 1 on archer select after the AIs, one step / 30 frames: L (the L button), r l u d (the right stick), c (its click) */
int g_ai = 0;   /* localai <n>: n AI players put in with the right stick while the humans still choose (tfex/AiSlotButton.cpp) */
float g_startX[8] = {}, g_startY[8] = {}, g_path[8] = {};
constexpr int kTeamSelect = 10;   /* MainMenu's team select (TowerBall's: 8-player's cursors) */

void Press(int pad, unsigned long bits, const char* what, int32_t rx = 0, int32_t ry = 0) {
    g_bits[pad] = bits; g_rx[pad] = rx; g_ry[pad] = ry; g_hold[pad] = 8; g_wait = 0;
    MMLOG("[mmtest] controller %d: %s", pad + 1, what);
}

int SceneKind(char* tn, int cap) {   /* 0 MainMenu, 1 MapScene, 2 Level, 3 other */
    void* game = xplay::g_game;
    uintptr_t scene = game ? *reinterpret_cast<uintptr_t*>(reinterpret_cast<uintptr_t>(game) + 0xe8) : 0;
    std::strcpy(tn, "?");
    if (!scene) return 3;
    rnd::TypeName(reinterpret_cast<void*>(scene), tn, cap);
    if (!std::strcmp(tn, "MainMenu")) return 0;
    if (!std::strcmp(tn, "MapScene")) return 1;
    if (!std::strcmp(tn, "Level")) return 2;
    return 3;
}

void Step() {   /* once per frame (controller 1's poll) */
    g_frame++; g_since++; g_wait++;
    char tn[64];
    int kind = SceneKind(tn, sizeof tn);
    void* game = xplay::g_game;
    uintptr_t scene = game ? *reinterpret_cast<uintptr_t*>(reinterpret_cast<uintptr_t>(game) + 0xe8) : 0;
    int state = kind == 0 && scene ? *reinterpret_cast<int32_t*>(scene + MainMenu::state) : -1;
    if (kind != g_lastScene || state != g_lastState) {
        MMLOG("[mmtest] screen: %s state %d (frame %d, phase %d)", tn, state, g_frame, g_phase);
        g_lastScene = kind; g_lastState = state; g_since = 0; g_presses = 0;
    }
    for (int i = 0; i < 8; i++) if (g_hold[i] > 0 && --g_hold[i] == 0) { g_bits[i] = 0; g_rx[i] = g_ry[i] = 0; }
    if (kind == 2) {
        if (g_phase != 8) {
            g_phase = 8;
            uint8_t* pl = TFGame::Players_();
            int32_t* ch = TFGame::Characters_();
            void* ms = Static<void*>(MainMenu::VersusMatchSettings);
            char who[96]; int n = 0, k = 0;
            for (int i = 0; i < 8 && pl; i++) if (pl[i]) { k++; n += std::snprintf(who + n, sizeof who - n, " P%d:%d", i + 1, ch ? ch[i] : -1); }
            MMLOG("[mmtest] level: %d players (archers%s), mode %d, roll call mode %d", k, who,
                  ms ? *reinterpret_cast<int32_t*>(reinterpret_cast<uintptr_t>(ms) + 0x30) : -1, Static<int32_t>(MainMenu::RollcallMode));
        }
        /* VersusStart's two cards at the first round, each waiting `while (!MenuInput.Confirm)` (on the Switch the controls card first,
         * then the tower's): A at 2 s, 6 s and 9 s, as a player (a press during a card's fly-in is ignored, hence the third) */
        if (g_since == 120 || g_since == 360 || g_since == 540) Press(0, kA, "A (the round's intro cards)");
        if (g_roundEnd && g_since == 900) {
            auto players = tfex::gen::statert::EntGetAll(reinterpret_cast<void*>(scene), tfex::gen::TypeOf(tfex::gen::bind::t_Player));
            int died = 0;
            for (int pass = 0; pass < 2; pass++)   /* (player 8 first, while player 7 still stands) */
                for (int32_t i = 0; i < players.Count(); i++) {
                    int idx = tfex::gen::Fn<int32_t (*)(void*)>(tfex::gen::bind::m_Player_get_PlayerIndex)(players.items[i]);
                    if (idx == 0 || (pass == 0) != (idx == 7)) continue;
                    int killer = idx == 7 ? 6 : 0;
                    if (tfex::gen::Fn<void* (*)(void*, int32_t, int32_t, bool, bool)>(tfex::gen::bind::m_Player_Die_1d72)(players.items[i], 0, killer, false, false)) died++;
                }
            MMLOG("[mmtest] localroundend: %d of %d players died (player 1 stands)", died, (int)players.Count());
        }
        if (g_kill && g_since == 300) {   /* every player dies: each leaves a corpse */
            auto players = tfex::gen::statert::EntGetAll(reinterpret_cast<void*>(scene), tfex::gen::TypeOf(tfex::gen::bind::t_Player));
            int made = 0;
            for (int32_t i = 0; i < players.Count(); i++)
                if (tfex::gen::Fn<void* (*)(void*, int32_t, int32_t, bool, bool)>(tfex::gen::bind::m_Player_Die_1d72)(players.items[i], 0, -1, false, false)) made++;
            MMLOG("[mmtest] localkill: %d players died, %d corpses", (int)players.Count(), made);
        }
        if (g_victory && g_since == 300) {
            void* p = tfex::gen::statert::EntGet(reinterpret_cast<void*>(scene), tfex::gen::TypeOf(tfex::gen::bind::t_Player));
            void* archer = p ? tfex::gen::Fn<void* (*)(void*)>(tfex::gen::bind::m_Player_get_ArcherData)(p) : nullptr;
            MMLOG("[mmtest] localvictory: %s", archer ? "a player's archer's victory music" : "no player found");
            if (archer) tfex::gen::Fn<void (*)(void*)>(tfex::gen::bind::m_ArcherData_PlayVictoryMusic)(archer);
        }
        if (!g_firstDeath && g_since % 15 == 0) {   /* the level's first death (an idle player dies only to a rule: Move or Die's): a dead player's
                                                     * entity is removed and a PlayerCorpse added, so the first corpse */
            auto corpses = tfex::gen::statert::EntGetAll(reinterpret_cast<void*>(scene), tfex::gen::TypeOf(tfex::gen::bind::t_PlayerCorpse));
            if (corpses.Count() > 0) { g_firstDeath = true; MMLOG("[mmtest] first death: %d corpse(s), %d frames into the level", (int)corpses.Count(), g_since); }
        }
        if (g_ai && g_since >= 600 && g_since <= 1500 && g_since % 30 == 0) {   /* each player's path from 10 s to 25 s, sampled every half second
                                                                                  * (an AI moves; it may come back where it was) */
            auto players = tfex::gen::statert::EntGetAll(reinterpret_cast<void*>(scene), tfex::gen::TypeOf(tfex::gen::bind::t_Player));
            char line[160]; int n = 0;
            for (int32_t i = 0; i < players.Count(); i++) {
                int idx = tfex::gen::Fn<int32_t (*)(void*)>(tfex::gen::bind::m_Player_get_PlayerIndex)(players.items[i]);
                if (idx < 0 || idx > 7) continue;
                float x = tfex::gen::Fld<float>(players.items[i], tfex::gen::bind::f_Entity_Position), y = tfex::gen::Fld<float>(players.items[i], tfex::gen::bind::f_Entity_Position + 4);
                if (g_since > 600) { float dx = x - g_startX[idx], dy = y - g_startY[idx]; g_path[idx] += __builtin_sqrtf(dx * dx + dy * dy); }
                g_startX[idx] = x; g_startY[idx] = y;
                if (g_since == 1500) {
                    n += std::snprintf(line + n, sizeof line - n, " P%d %.0f", idx + 1, g_path[idx]);
                    if (idx >= g_players && g_path[idx] > 16) MMLOG("[mmtest] an AI player moved: P%d", idx + 1);   /* (the humans' controllers stay idle) */
                }
            }
            if (g_since == 1500) MMLOG("[mmtest] players' paths from 10 s to 25 s (pixels):%s", n ? line : " none alive");
        }
        if (g_since == 1800) MMLOG("[mmtest] level ran 30 s");   /* tools/megamod/localrun.sh ends the run here */
        return;
    }
    if (kind == 1) {   /* the tower select: A (the tower), A again if it asks */
        g_phase = 7;
        if (g_towerRights > 0 && g_since > 90 && g_since % 20 == 0 && g_towerMoved < g_towerRights) { g_towerMoved++; Press(0, kRight, "Right (tower select: localtower)"); return; }
        if (g_since > 150 + 20 * g_towerRights && g_wait > 120 && g_presses < 4) { g_presses++; Press(0, kA, "A (tower select)"); }
        return;
    }
    if (kind != 0) return;
    switch (state) {
    case MainMenu::PressStart: if (g_since > 120 && g_wait > 120 && g_presses < 3) { g_presses++; Press(0, kA, "A (press start)"); } return;
    /* TF.EX's first-run screens after PRESS START (as tfex/MainMenuPatch.cpp's AutoUi): the tutorial (72, A = NEXT on each slide) and the
     * changelog panel (66, A = CONTINUE) */
    case 72: if (g_since > 90 && g_wait > 60 && g_presses < 4) { g_presses++; Press(0, kA, "A (tutorial: NEXT)"); } return;
    case 66: if (g_since > 120 && g_wait > 60 && g_presses < 1) { g_presses++; Press(0, kA, "A (changelog: CONTINUE)"); } return;
    case MainMenu::Main: if (g_since > 150 && g_wait > 120 && g_presses < 2) { g_presses++; Press(0, kA, "A (FIGHT)"); } return;
    case MainMenu::VersusSub:
        if (g_phase >= 3) break;   /* (7 is also a later screen: below) */
        if (g_since == 60 && !g_narrow) Press(0, kRight, "Right (WIDE)");
        if (g_since == 100) { Press(0, kA, g_narrow ? "A (the normal set: localnarrow)" : "A (WIDE)"); g_phase = 2; }
        return;
    case MainMenu::Rollcall: {
        if (g_phase < 3) g_phase = 3;
        /* controller 1's Right presses first (localarcher: it is joined already, from FIGHT, and its first A readies it), then each
         * controller A to join at 30-frame steps, A again to confirm, then START */
        int t = g_since - 90;
        if (t < 0 || t % 30) return;
        /* the AIs go in while the humans are still choosing their archers (as on the console: nobody had confirmed), then the humans
         * confirm */
        int s = g_steps, k = t / 30 - s, n = g_players;
        int steps = (int)std::strlen(g_aiSteps);
        int a = k - n, ai = (g_ai ? 2 * g_ai + 1 + (g_aiNext ? 1 : 0) : 0) + steps, c = a - ai;
        if (k < 0) Press(0, kRight, "Right (the next archer)");
        else if (k < n) { if (k > 0 || (!g_ai && !g_aiSteps[0])) Press(k, kA, "A (join)"); }   /* (localai: controller 1, in from FIGHT, stays choosing) */
        else if (a < 2 * g_ai) {   /* each AI: the right stick right (past the first), then its click (tfex/AiSlotButton.cpp) */
            if (a % 2 == 0) { if (a > 0) Press(0, 0, "right stick right (the AI cursor)", 30000, 0); }
            else Press(0, kStickR, "right stick click (an AI on)");
        }
        else if (g_ai && a == 2 * g_ai) Press(0, 0, "right stick down (that AI's next archer)", 0, -30000);
        else if (g_ai && g_aiNext && a == 2 * g_ai + 1) Press(0, kStickR, "right stick click (that AI: the next AI type)");
        else if (a >= ai - steps && a < ai) {
            switch (g_aiSteps[a - (ai - steps)]) {
            case 'L': Press(0, kL, "L (the AI shortcut)"); break;
            case 'r': Press(0, 0, "right stick right", 30000, 0); break;
            case 'l': Press(0, 0, "right stick left", -30000, 0); break;
            case 'u': Press(0, 0, "right stick up", 0, 30000); break;
            case 'd': Press(0, 0, "right stick down", 0, -30000); break;
            case 'c': Press(0, kStickR, "right stick click"); break;
            default: break;
            }
        }
        else if (c < n) { Press(c, kA, "A (confirm the archer)"); g_phase = 4; }
        else if (c == n + 3 || c == n + 8) { Press(0, kPlus, "START (roll call)"); g_phase = 5; }
        return;
    }
    default: break;
    }
    /* the screens after the roll call (match settings, teams): the mode, then START */
    if (g_phase >= 3) {   /* (all ready: the roll call may go on before the driver's START) */
        g_phase = 6;
        void* ms = Static<void*>(MainMenu::VersusMatchSettings);
        int32_t* mode = ms ? reinterpret_cast<int32_t*>(reinterpret_cast<uintptr_t>(ms) + 0x30) : nullptr;
        if (g_variantName[0] && !g_variantDone && ms) {
            g_variantDone = true;
            void* variants = *reinterpret_cast<void**>(reinterpret_cast<uintptr_t>(ms) + tfex::gen::bind::f_MatchSettings_Variants);
            bool ok = tfex::gen::GlueFrMods::SetCustomVariant(variants, tfex::gen::ManagedUtf8(g_variantName, (int)std::strlen(g_variantName)), true);
            MMLOG("[mmtest] custom variant %s %s", g_variantName, ok ? "on" : "NOT FOUND (no such registered variant)");
        }
        if (g_modeName[0]) {   /* a custom mode by name: Right on GAME MODE (FortRise's button) every 20 frames until it shows the name */
            if (!g_modeDone && mode && g_since >= 30 && g_since % 20 == 10) {
                char cur[32] = "";
                if (*mode >= 7) if (void* n = tfex::gen::Fr_VersusModeButton::GetModeName(*mode)) tfex::gen::Utf8Of(n, cur, sizeof cur);
                if (!std::strcmp(cur, g_modeName)) { g_modeDone = true; MMLOG("[mmtest] GAME MODE: %s (mode %d)", cur, (int)*mode); }
                else if (!tfex::frmodes::ModeButtonSelected() && g_modePresses < 10) { g_modePresses++; Press(0, kUp, "Up (to GAME MODE)"); }
                else if (g_modePresses++ < 26) {
                    auto* list = tfex::gen::GlueFrMods::get_VersusModeSelect();
                    MMLOG("[mmtest] GAME MODE now mode %d (FortRise's index %d of %d): Right", (int)*mode, (int)tfex::gen::Fr_VersusModeButton::currentIndex, list ? list->Count() : -1);
                    Press(0, kRight, "Right (GAME MODE)");
                }
                else if (g_modePresses == 27) MMLOG("[mmtest] ERROR: GAME MODE never showed %s (last mode %d)", g_modeName, (int)*mode);
            }
        } else if (mode && *mode != g_mode && g_since == 30) { MMLOG("[mmtest] mode %d -> %d (as the mode button)", *mode, g_mode); *mode = g_mode; }
        /* a custom team mode's select (Teams+'s columns SOLO | BLUE | YELLOW | RED, the cursors starting at SOLO): controller k goes
         * (k % 3) + 1 columns right (BLUE, YELLOW, RED in turn); from the seventh on they stay SOLO (lone wolves) */
        const bool columns = !std::strcmp(g_modeName, "TEAMS+");   /* Teams+'s own select; any other team mode the game's BLUE | RED */
        /* a team mode's teams (TEAM DEATHMATCH, TOWERBALL, a mod's team mode: SOCCER): odd controllers BLUE (Left), even RED (Right), one
         * each 20 frames from 60 (until 2026-10-31 the hand-written three-team code handed out TDM's teams itself) */
        if (state == kTeamSelect && !columns && (g_modeName[0] || (g_mode != 3 && g_mode != 4)) && g_since >= 60 && g_since < 60 + 20 * g_players) {
            if ((g_since - 60) % 20 == 0) { int k = (g_since - 60) / 20; Press(k, k % 2 ? kRight : kLeft, k % 2 ? "Right (RED)" : "Left (BLUE)"); }
            return;
        }
        if (state == kTeamSelect && g_teamSteps[0]) {   /* (localteamsteps: the AIs' teams with the right stick, after the humans' on the game's) */
            const int from = columns ? 60 : 60 + 20 * g_players + 20;
            int t = (g_since - from) / 20, n = (int)std::strlen(g_teamSteps);
            if (g_since >= from && (g_since - from) % 20 == 0 && t < n) {
                switch (g_teamSteps[t]) {
                case 'r': Press(0, 0, "right stick right (team screen)", 30000, 0); break;
                case 'l': Press(0, 0, "right stick left (team screen)", -30000, 0); break;
                case 'u': Press(0, 0, "right stick up (team screen)", 0, 30000); break;
                case 'd': Press(0, 0, "right stick down (team screen)", 0, -30000); break;
                default: break;
                }
            }
            if (g_since < from + 20 * n + 30) return;
            if (g_since % 90 == 0 && g_presses++ < 6) Press(0, kPlus, "START");
            return;
        }
        if (state == kTeamSelect && columns && g_since >= 60 && g_since < 60 + 20 * 3 * g_players) {
            int t = (g_since - 60) / 20, k = t / 3, r = t % 3;
            if ((g_since - 60) % 20 == 0 && k < 6 && r <= k % 3) Press(k, kRight, "Right (the next column)");
            return;
        }
        if (g_modeName[0] && !g_modeDone) return;   /* (START once GAME MODE shows the custom mode) */
        if (g_since > 120 + (state == kTeamSelect ? 20 * (columns ? 3 : 1) * g_players : 0) && g_since % 90 == 0 && g_presses++ < 6) Press(0, kPlus, "START");
    }
}
}  // namespace

void SetLocalTest(int players, int mode) {
    g_players = players < 2 ? 2 : players > 8 ? 8 : players; g_mode = mode;
    MMLOG("[mmtest] local play test: %d controllers, mode %d", g_players, g_mode);
}
void SetLocalModeName(const char* name) {
    std::snprintf(g_modeName, sizeof g_modeName, "%s", name ? name : "");
    MMLOG("[mmtest] local play test: the custom mode %s (GAME MODE scrolled to it)", g_modeName);
}
void SetLocalVariant(const char* name) { std::snprintf(g_variantName, sizeof g_variantName, "%s", name ? name : ""); }
void SetLocalNarrow() { g_narrow = true; }
void SetLocalAiNext() { g_aiNext = true; }
void SetLocalAiSteps(const char* st) { int k = 0; for (; st[k] && st[k] != '\r' && st[k] != '\n' && k < (int)sizeof g_aiSteps - 1; k++) g_aiSteps[k] = st[k]; g_aiSteps[k] = 0; }
void SetLocalTeamSteps(const char* st) { int k = 0; for (; st[k] && st[k] != '\r' && st[k] != '\n' && k < (int)sizeof g_teamSteps - 1; k++) g_teamSteps[k] = st[k]; g_teamSteps[k] = 0; }
void SetLocalAi(int n) { g_ai = n < 0 ? 0 : n > 7 ? 7 : n; }
void SetLocalTower(int rights) { g_towerRights = rights < 0 ? 0 : rights > 20 ? 20 : rights; }
void SetLocalKill() { g_kill = true; }
void SetLocalRoundEnd() { g_roundEnd = true; }
void SetLocalVictory() { g_victory = true; }
void SetLocalArcher(int steps) { g_steps = steps < 0 ? 0 : steps > 40 ? 40 : steps; }
bool LocalTestOn() { return g_players > 0; }
void LocalTestStick(uint32_t id, int32_t* rx, int32_t* ry) { *rx = id < 8 ? g_rx[id] : 0; *ry = id < 8 ? g_ry[id] : 0; }
unsigned long LocalTestButtons(uint32_t id) {
    if (!g_players || id >= 8) return 0;
    if (id == 0) Step();
    return g_bits[id];
}
}  // namespace megamod
#endif   // XPLAY_EXTRAS
