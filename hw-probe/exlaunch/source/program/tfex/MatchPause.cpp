/* TF.EX's mid-match netplay pause (v0.21.1): LevelPatch.Level_HandlePausing -> LightPauseMenu.HandleStartPress, a RESUME / QUIT overlay on
 * the HUD layer while the game keeps running (TF.EX.Domain/CustomComponent/LightPauseMenu.cs, TF.EX.Patchs/Scene/Level.cs). Both generated
 * (tfex/gen/tfexgen_LightPauseMenu.*; LevelPatch's two HandlePausing prefixes are generated hooks, tfexgen_LevelPatch.*, which xplay.cpp's
 * HandlePausingShell wraps), this file is the Switch glue:
 *  - LightPauseMenu.Pressed(MenuButton) ([service] svc::LightPausePressed): the local JoyConInputs' menu getters (InputLock::MenuPressed).
 *    TF.EX's overlay lives in the Level and is driven from inside the simulated tick (Level.Update -> HandlePausing, the entity's Update),
 *    and TF.State's load doesn't restore it (IsOpen and the entity stay as they are, as on the PC). A press belongs to the first
 *    simulation of the frame it came in: a resimulated frame presses nothing (TF.EX reads the live pad, whose edge is gone by the time a
 *    later frame rolls back; replaying it reopened and closed the overlay: the self-closing pause menu, 2026-10-22). TFEX-DIFF S-input:
 *    a press seen on a real frame where the game didn't tick is latched (Frame) for the next simulated frame (2026-10-17: a test Start
 *    was lost that way).
 *  - the local player's input to GGRS is neutral while the overlay is open: TF.EX's own NetplayLogic (TFGame.cs:679, generated);
 *  - ForceClose when the match ends: TF.EX's own TFGame.OnSceneTransition postfix (generated, NetplayEntry);
 *  - QUIT: the generated Quit (LeaveLobby, Level.GoToNetplayEntryMenu, EnableAllControllers); the Switch's teardown follows in
 *    EndOfMatch.cpp's QuitTeardownFrame (the match's Level gave way to a MainMenu).
 *  - hw.txt "lobbypause N": the test console presses Start at simulated frame N of a lobby match, then Down, Confirm (QUIT);
 *    "lobbypause N 1": Start, then Confirm on RESUME, and the match goes on (the overlay's open frames go through rollbacks);
 *    "lobbypause -1": the same QUIT 2 s after TF.EX's IsDisconnected (every remote gone) (the level runs on locally: TF.EX's way out of it). */
#include "InputLock.hpp"
#include "BetweenTicks.hpp"
#include "gen/tfexgen_LightPauseMenu.hpp"
#include "gen/tfexgen_binding.hpp"
#include "runtime/tfexgen_code_rt.hpp"
#include "Game.hpp"
#include "EndOfMatch.hpp"
#include "gen/tfexgen_NetplayManager.hpp"
#include "../hwlog.hpp"
#include <cstdint>

namespace xplay { int SimFrameForServices(bool* resim, bool* inMatch); }   /* xplay.cpp: the frame being simulated, a resimulation, a lobby match runs */

namespace tfex::MatchPause {
namespace {
uint8_t g_latch = 0;         /* menu presses seen on real frames (Frame) that no simulated frame has taken yet */
bool g_recorded = false;     /* a simulated frame's first pass read the buttons since the last Frame() */
int g_testAt = 0;            /* hw.txt lobbypause: the simulated frame to press Start at (0: off) */
bool g_testResume = false;   /* hw.txt lobbypause N 1: RESUME instead of QUIT */
unsigned long g_testBits = 0; int g_testHold = 0, g_testStep = 0, g_testWait = 0;
}

bool Pressed(int button) {
    if (button < 0 || button > 4) return false;
    bool resim = false, inMatch = false;
    int f = xplay::SimFrameForServices(&resim, &inMatch);
    if (!inMatch) return InputLock::MenuPressed(button);
    if (resim) return false;
    static int frame = -1; static uint32_t bits = 0;
    if (frame != f) {   /* the frame's first read: its buttons, with the latched ones (a press lasts one real frame) */
        frame = f; bits = g_latch; g_latch = 0; g_recorded = true;
        for (int b = 0; b < 5; b++) if (InputLock::MenuPressed(b)) bits |= 1u << b;
        {   /* a change only, the first 30; then a count every 600 (a held button repeated this line on every frame: 85% of a console log) */
            static uint32_t lastBits = 0; static int logged = 0, more = 0;
            if (bits != lastBits) {
                if (logged < 30) { logged++; hw::Log("[pause] frame %d: menu buttons %#x (Confirm 1, Back 2, Up 4, Down 8, Start 16)", f, bits); }
                else if (++more % 600 == 0) hw::Log("[pause] %d more menu-button changes (not logged one by one)", more);
                lastBits = bits;
            }
        }
    }
    return bits >> button & 1;
}
bool IsOpen() { return gen::LightPauseMenu::get_IsOpen(); }

/* after TF.EX's HandlePausing prefixes (xplay's HandlePausingShell, in a lobby match): each opening and closing of the overlay, with the
 * frame (a resimulation must repeat its first pass) */
void LogChange() {
    static bool was = false;
    if (bool now = IsOpen(); now != was) {
        bool resim = false, inMatch = false;
        int f = xplay::SimFrameForServices(&resim, &inMatch);
        static int n = 0;
        if (n++ < 40) hw::Log("[pause] frame %d%s: the overlay %s", f, resim ? " (resimulated)" : "", now ? "opened" : "closed");
        was = now;
    }
}

/* the test driver: once per real frame (xplay's FrameShell) */
void SetTest(int frame, bool resume) { g_testAt = frame; g_testResume = resume; }
void Frame() {
    {   /* the latch: a menu button's press is an edge of one real frame; the simulation may not tick on that frame (GGRS waits, catch-up),
         * so it waits here for the next simulated frame's first pass (Pressed). This runs before the game's Update (xplay's FrameShell),
         * so the edges read here are the previous real frame's: latched only when no simulated frame read them then. */
        bool resim = false, inMatch = false;
        xplay::SimFrameForServices(&resim, &inMatch);
        if (!inMatch) g_latch = 0;
        else if (!g_recorded) for (int b = 0; b < 5; b++) if (InputLock::MenuPressed(b)) g_latch |= 1u << b;
        g_recorded = false;
    }
    if (g_testHold > 0) g_testHold--;
    if (!g_testAt) return;
    bool resim = false, inMatch = false;
    int f = xplay::SimFrameForServices(&resim, &inMatch);
    if (!inMatch) { g_testStep = 0; return; }
    constexpr unsigned long kStart = 1u << 10, kDown = 1u << 15, kConfirm = 1u << 0;
    auto press = [](unsigned long b) { g_testBits = b; g_testHold = 8; g_testWait = 0; };
    g_testWait++;
    switch (g_testStep) {
    case 0:
        if (g_testAt < 0) {   /* lobbypause -1: once every remote is gone (counted in real frames: GGRS's frame stops there) */
            void* nm = EndOfMatch::GenNetplayManager();   /* TF.EX's own flag: IsDisconnected (GetEventAndUpdate's Disconnected) */
            static int goneFor = 0;
            goneFor = nm && gen::NetplayManager::IsDisconnected(nm) ? goneFor + 1 : 0;
            if (goneFor == 120) { hw::Log("[pause] test: TF.EX's IsDisconnected for 2 s (frame %d): Start, then Down, Confirm (QUIT)", f); press(kStart); g_testStep = 1; }
            break;
        }
        if (f >= g_testAt) { hw::Log("[pause] test: Start at frame %d (hw.txt lobbypause), then %s", f, g_testResume ? "Confirm (RESUME)" : "Down, Confirm (QUIT)"); press(kStart); g_testStep = 1; } break;
    case 1:
        if (g_testWait <= 40) break;
        if (g_testAt < 0) { char d[200]; EndOfMatch::DescribePauseMenu(d, sizeof d); hw::Log("[pause] test: after Start: the overlay %s, Level.Paused %d, %s", IsOpen() ? "open" : "NOT open", game::SceneIs("Level") && gen::Fn<bool (*)(void*)>(gen::bind::m_Level_get_Paused)(game::CurrentScene()), d); }
        if (g_testResume) { hw::Log("[pause] test: overlay %s at frame %d; RESUME in 4 s", IsOpen() ? "open" : "NOT open", f); g_testWait = 0; g_testStep = 3; break; }
        hw::Log("[pause] test: overlay %s; Down", IsOpen() ? "open" : "NOT open"); press(kDown); g_testStep = 2; break;
    case 3: if (g_testWait > 240) { hw::Log("[pause] test: overlay %s at frame %d; Confirm (RESUME)", IsOpen() ? "open" : "NOT open", f); press(kConfirm); g_testStep = 4; } break;   /* (the open frames go through the page's rollbacks) */
    case 4: if (g_testWait > 40) { hw::Log("[pause] test: after RESUME the overlay is %s at frame %d", IsOpen() ? "STILL OPEN" : "closed", f); g_testStep = 5; } break;
    case 2: if (g_testWait > 40) { char d[200]; EndOfMatch::DescribePauseMenu(d, sizeof d); hw::Log("[pause] test: Confirm (QUIT); %s", d); press(kConfirm); g_testStep = 9; } break;
    case 9: if (g_testAt < 0 && g_testWait == 60) { char d[200]; EndOfMatch::DescribePauseMenu(d, sizeof d); hw::Log("[pause] test: 1 s after Confirm: scene %s, %s", game::SceneIs("Level") ? "Level" : "not Level", d); } break;
    default: break;
    }
}
unsigned long TestButtons() { return g_testHold > 0 ? g_testBits : 0; }
} // namespace tfex::MatchPause

/* xplay.cpp's hooks (inside namespace xplay) declare these. */
namespace xplay {
void tfex_MatchPauseLogChange() { tfex::MatchPause::LogChange(); }
bool tfex_MatchPauseIsOpen() { return tfex::MatchPause::IsOpen(); }
unsigned long tfex_MatchPauseTestButtons() { return tfex::MatchPause::TestButtons(); }
void tfex_MatchPauseSetTest(int frame, int resume) { tfex::MatchPause::SetTest(frame, resume != 0); }
void tfex_MatchPauseFrame() { tfex::MatchPause::Frame(); }
}
