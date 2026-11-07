/* The hw.txt settings parser (sd:/xplay/hw.txt) (split out of xplay.cpp, code unchanged; shared state: xplay_internal.hpp) */
#include "xplay_internal.hpp"
#include "megamod/megamod.hpp"
#include "tfex/runtime/tfexgen_scenario_rt.hpp"
#include "tfex/runtime/tfexgen_glue.hpp"
#include "tfex/AiSlotButton.hpp"

void GfxSetFlushAt(int n);   /* gfxmem.cpp (tests) */
namespace xplay {
void LoadHwConfig() {
    nn::fs::FileHandle h;
    if (nn::fs::OpenFile(&h, "sd:/xplay/hw.txt", nn::fs::OpenMode_Read) != 0) {
        HWLOG("[hw] no sd:/xplay/hw.txt: timing of the game's update only");
        return;
    }
    long size = 0;
    nn::fs::GetFileSize(&size, h);
    static char buf[8192];   /* (was 1 KB, cut silently: smoke.sh adds lines, so the last keys could vanish) */
    if (size > (long)sizeof(buf) - 1) {   /* an over-long file is an error, not a silent cut (the pools rule) */
        pools::Exhausted("hw.txt", (int)sizeof(buf) - 1, "sd:/xplay/hw.txt is longer than the settings buffer: the lines past it are NOT read");
        size = sizeof(buf) - 1;
    }
    nn::fs::ReadFile(h, 0, buf, size);
    nn::fs::CloseFile(h);
    buf[size] = 0;
    if (size == (long)sizeof(buf) - 1) { char* nl = std::strrchr(buf, '\n'); if (nl) nl[1] = 0; }   /* (no half line read as a key) */
    for (char* line = buf; line && *line;) {
        char* nl = std::strchr(line, '\n');
        if (nl) *nl = 0;
        int a = 0, b = 0;
        if (std::sscanf(line, "rngseed %d", &a) == 1) { g_hwSeed = a; g_hwSeedSet = true; }   /* a scripted native match's seed (NativeSessionSetup) */
        else if (std::sscanf(line, "vstart %d", &a) == 1) g_vstart = a;
        else if (std::sscanf(line, "localvs %d %d", &a, &b) == 2) megamod::SetLocalTest(a, b);   /* tests: a local match (megamod/localtest.cpp) */
        else if (std::sscanf(line, "localarcher %d", &a) == 1) megamod::SetLocalArcher(a);
        else if (!std::strncmp(line, "localmode ", 10)) megamod::SetLocalModeName(line + 10);   /* a custom mode by name (megamod/localtest.cpp) */
        else if (!std::strncmp(line, "localvariant ", 13)) megamod::SetLocalVariant(line + 13);   /* a FortRise custom variant on (megamod/localtest.cpp) */
        else if (!std::strncmp(line, "localtower ", 11)) megamod::SetLocalTower(std::atoi(line + 11));   /* Right n times on the tower select */
        else if (!std::strncmp(line, "localteamsteps ", 15)) megamod::SetLocalTeamSteps(line + 15);   /* Teams+'s team screen: the right stick (megamod/localtest.cpp) */
        else if (!std::strncmp(line, "localaisteps ", 13)) megamod::SetLocalAiSteps(line + 13);   /* steps on archer select: L r l u d c (megamod/localtest.cpp) */
        else if (!std::strncmp(line, "localainext", 11)) megamod::SetLocalAiNext();   /* the last AI: the next AI type (megamod/localtest.cpp) */
        else if (!std::strncmp(line, "localnarrow", 11)) megamod::SetLocalNarrow();   /* the normal set, not WIDE (megamod/localtest.cpp) */
        else if (!std::strncmp(line, "aiseat ", 7)) tfex::aislot::SetAiSeat(std::atoi(line + 7));   /* TEST ONLY: LoaderAI's AI plays the online seats (tfex/AiSlotButton.cpp) */
        else if (!std::strncmp(line, "localai ", 8)) megamod::SetLocalAi(std::atoi(line + 8));   /* L n times on archer select: AI players (tfex/AiSlotButton.cpp) */
        else if (!std::strncmp(line, "localroundend", 12)) megamod::SetLocalRoundEnd();
        else if (!std::strncmp(line, "localkill", 9)) megamod::SetLocalKill();
        else if (!std::strncmp(line, "localvictory", 12)) megamod::SetLocalVictory();
        else if (std::sscanf(line, "gfxflushat %d", &a) == 1) ::GfxSetFlushAt(a);   /* tests: the mid-frame flush at N chunks (gfxmem.cpp) */
        else if (std::sscanf(line, "lobbypred %d", &a) == 1) g_lobbyPred = a;
        else if (std::sscanf(line, "lobbyscript %d", &a) == 1) g_lobbyScript = a;
        else if (std::sscanf(line, "lobbylength %d", &a) == 1) { tfex::MainMenuPatch::SetLobbyLength(a); tfex::NetplayScreens::SetBuilderPreset('l', a); }
        else if (std::sscanf(line, "lobbymap %d", &a) == 1) {
            /* TF.EX's lobby builder offers MapId -1 (RANDOM) .. 15 (LobbyVersusMapButton: NETPLAY_SAFE_MAP's 16 towers); a wide match
             * rolls its tower through RANDOM (docs/ws-wide.md). A value outside that is no lobby TF.EX makes (MapId 29 crashed the
             * builder indexing the tower list, 2026-10-26): refused */
            if (a < -1 || a > 15) HWLOG("[hw] lobbymap %d refused: TF.EX's lobby MapId is -1 (RANDOM) .. 15", a);
            else { tfex::MainMenuPatch::SetLobbyMap(a); tfex::NetplayScreens::SetBuilderPreset('p', a); }
        }
        else if (!std::strncmp(line, "lobbyvariants ", 14)) tfex::NetplayScreens::SetBuilderVariants(line + 14);
        else if (!std::strncmp(line, "lobbydelaybutton ", 17)) tfex::MainMenuPatch::SetDelayButton(line + 17);   /* tests: L or ZL alone */
        else if (std::sscanf(line, "optionsdrive %d", &a) == 1) tfex::MainMenuPatch::SetOptionsDrive(a);   /* tests: OPTIONS -> TF.EX's entries */
        else if (!std::strncmp(line, "optionstext", 11) && line[11] >= '1' && line[11] <= '4' && line[12] == ' ')   /* tests: optionstext1..4 <text>, */
            tfex::NetplayOptions::AddTestText(line + 13);                                                             /* the keyboard's texts in order */
        else if (std::sscanf(line, "lobbydelaysteps %d", &a) == 1) tfex::MainMenuPatch::SetDelaySteps(a);   /* tests: the input-delay advisor's gesture */
        else if (std::sscanf(line, "lobbyseries %d", &a) == 1) tfex::NetplayScreens::SetBuilderPreset('s', a);   /* tests: the builder's SERIES (best of a) */
        else if (std::sscanf(line, "lobbymode %d", &a) == 1) { tfex::MainMenuPatch::SetLobbyMode(a); tfex::NetplayScreens::SetBuilderPreset('m', a); }
        else if (std::sscanf(line, "lobbyteam %d", &a) == 1) tfex::MainMenuPatch::SetLobbyTeam(a);
        else if (std::sscanf(line, "lobbyui %d", &a) == 1) tfex::MainMenuPatch::SetAutoUi(a);
        else if (std::sscanf(line, "lobbyarcher %d", &a) == 1) tfex::MainMenuPatch::SetLobbyArcher(a);
        else if (std::sscanf(line, "notif %d", &a) == 1) tfex::Notification::SetEnabled(a != 0);   /* TF.EX's Notification (off by default) */
        else if (tfex::gen::HwLine(line)) {}   /* generated TF.EX code (tfex/gen, tools/tfex/translate.sh) */
        else if (std::sscanf(line, "lobbyprivate %d", &a) == 1) tfex::MainMenuPatch::SetPrivateUi(a);
        else if (std::sscanf(line, "lobbyquickwait %d", &a) == 1) tfex::MainMenuPatch::SetQuickWait(a);
        else if (std::sscanf(line, "lobbyquick %d", &a) == 1) tfex::MainMenuPatch::SetQuickUi(a);
        else if (std::sscanf(line, "lobbyedit %d", &a) == 1) tfex::MainMenuPatch::SetEditUi(a);
        else if (char jc[8] = {}; std::sscanf(line, "joincode %7s", jc) == 1) tfex::NetplayScreens::SetTestJoinCode(jc);   /* tests: the code instead of the keyboard */
        else if (std::sscanf(line, "lobbymax %d", &a) == 1) tfex::MainMenuPatch::SetLobbyMax(a);
        else if (std::sscanf(line, "matchpads %d", &a) == 1) xplay::g_matchPads = a;   /* tests: in a match controller 1 holds Right, controller 2 Left */
        else if (std::sscanf(line, "lobbyback %d", &a) == 1) tfex::MainMenuPatch::SetBackOut(a);   /* tests: the host backs out of the ready lobby */
        else if (std::sscanf(line, "lobbylocal2 %d", &a) == 1) tfex::MainMenuPatch::SetLocal2(a);   /* tests: a second local player in the lobby (controller 2); 2: plugged in after the lobby opened */
        else if (std::sscanf(line, "lobbylocal %d", &a) == 1) tfex::MainMenuPatch::SetLocalPlayers(a);   /* (after lobbylocal2: "lobbylocal %d" reads lobbylocal2 as 2) tests: n local players in the lobby (controllers 2..n) */
        else if (std::sscanf(line, "lobbyvote %d", &a) == 1) tfex::MainMenuPatch::SetAutoVote(a);
        else if (int r = std::sscanf(line, "lobbypause %d %d", &a, &b); r >= 1) tfex_MatchPauseSetTest(a, r == 2 ? b : 0);   /* the test console opens TF.EX's pause overlay at that frame and QUITs (1 after it: RESUMEs) */
        else if (std::sscanf(line, "lobbywide %d", &a) == 1) tfex::MainMenuPatch::SetWide(a);
        else if (std::sscanf(line, "lobbyjoin %d", &a) == 1) tfex::MainMenuPatch::SetJoinMode(a);
        else if (std::sscanf(line, "np %d", &a) == 1) g_npOn = a != 0;
        else if (std::sscanf(line, "npseat %d", &a) == 1) g_npSeat = a;
        else if (std::sscanf(line, "nppred %d", &a) == 1) g_npPred = a;
        else if (std::sscanf(line, "hwloghead %d", &a) == 1) hw::SetHeadLimit((size_t)a);
        else if (std::strncmp(line, "stategenpatch ", 14) == 0) {   /* TF.State plan (TEST ONLY): several generated patch classes on one line ("genpatch <Class>" each); smoke.sh keeps one line per first word */
            char buf[256]; std::snprintf(buf, sizeof buf, "%s", line + 14);
            for (char* tok = std::strtok(buf, " \t\r\n"); tok; tok = std::strtok(nullptr, " \t\r\n")) { char one[96]; std::snprintf(one, sizeof one, "genpatch %s", tok); tfex::gen::HwLine(one); }
        }
        else if (std::strncmp(line, "statescenario ", 14) == 0) { char sn[48] = ""; if (std::sscanf(line + 14, "%47s", sn) == 1) tfex::gen::scen::ConfigureScenario(sn); }   /* TF.State plan: a TF.EX scenario's map in lobby matches (TEST ONLY) */
        else if (std::sscanf(line, "statedump %d", &a) == 1) tfex::gen::statert::ConfigureDump(a);   /* TF.State plan step E (TEST ONLY) */
        else if (std::sscanf(line, "staterawdump %d", &a) == 1) tfex::gen::statert::ConfigureRaw(a);   /* TF.State plan step B (TEST ONLY) */
        else if (std::sscanf(line, "statespawn %d", &a) == 1) tfex::gen::statert::ConfigureSpawn(a);   /* TF.State plan step A (TEST ONLY) */
        else if (std::sscanf(line, "statelayout %d", &a) == 1) tfex::gen::statert::ConfigureLayout(a);   /* TF.State plan phase 1: slot maps */
        else if (int r = std::sscanf(line, "statemeasure %d %d", &a, &b); r >= 1) tfex::gen::statert::Configure(a, r == 2 ? b : 0);   /* TF.State's GetState / serialize / LoadState timed (tfexgen_state.hpp) */
        else if (std::sscanf(line, "gcdivisor %d", &a) == 1) tfex::gen::statert::GcDivisor(a);   /* Boehm's GC_free_space_divisor (tfexgen_state.hpp GcWatch) */
        else if (std::sscanf(line, "gcmin %d", &a) == 1) tfex::gen::statert::GcMin(a);   /* the least a GC collection waits for, in MB allocated (tfexgen_state.hpp GcWatch) */
        else if (std::sscanf(line, "gccensus %d", &a) == 1) tfex::gen::statert::GcCensusOn(a != 0);   /* the GC's live objects by type, on by default (tfexgen_state.hpp) */
        else if (std::sscanf(line, "allocwatch %d", &a) == 1) AllocWatch(a != 0);   /* MEASUREMENT: the managed allocations of TF.State's saves / loads (netplay_gate.cpp) */
        else if (std::sscanf(line, "rumblefix %d", &a) == 1) rumble::SetEnabled(a != 0);
        else if (std::sscanf(line, "nppad %d", &a) == 1) g_npPad = a;
        else if (std::sscanf(line, "vsshift %d %d", &a, &b) == 2) { g_shiftFrom = a; g_shiftN = b; }
        else if (std::sscanf(line, "vsstop %d", &a) == 1) g_vsStop = a;
        else if (std::sscanf(line, "rngcapture %d", &a) == 1) { g_captureForced = a != 0; rnd::SetCapture(a != 0); }
        else if (std::sscanf(line, "rnggametrace %d %d", &a, &b) == 2) rnd::SetGameTrace(a, b);
        else if (std::sscanf(line, "rnggametrace %d", &a) == 1) rnd::SetGameTrace(a, -1000000);
        line = nl ? nl + 1 : nullptr;
    }
}
} // namespace xplay
