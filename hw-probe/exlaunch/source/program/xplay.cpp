/*
 * TowerFall Switch — cross-play module (exlaunch)
 * -----------------------------------------------
 * The hooks, the netplay gate (GGRS) and the glue of the lobby match: native lobby-match start, the real rollback
 * (TF.State's state through netplay_gate.cpp's seam), the per-tick "[vs]" log the harness compares, timing statistics.
 *
 * At boot:
 *   - Fixes for cross-play: the game's pow import -> musl's pow; Math.Round(double) ->
 *     ties-to-even (BRUTE's is floor(x + 0.5); .NET rounds half to even); fixed timestep; MonoGame's user buffers.
 * Scripted input (test harness):
 *   - sd:/xplay/script.txt drives player 1's / 2's menus: "M <poll> <buttons hex> <stick x> <stick y>" (Pro Controller
 *     input, polls counted from boot) and "N ..." (player 2). sd:/xplay/vs0.txt..vs3.txt ("frame:flags:moveX:moveY,..."
 *     per seat, hw.txt "lobbyscript 1") are the in-level input of a match.
 *
 * Output: the guest log ("[xplay] ..." lines in Ryujinx's log; read it with grep -a).
 * Offsets: TowerFall v1.4.1.6 main, flat-image (module-base-relative).
 */

#include "tfex/gen/tfexgen_Tp_Teams3.hpp"
#include "xplay_internal.hpp"
#include "megamod/megamod.hpp"
#include "tfex/FrModes.hpp"   /* ScoreIndexScope: EndRound's inlined GetScoreIndex */
#include "ggrsffi.hpp"
#include "tfex/gen/tfexgen_hooks.hpp"
#include "tfex/ModsMenu.hpp"
#include "tfex/gen/tfexgen_NetplayEntry.hpp"
#include "tfex/InputLock.hpp"
#include "tfex/gen/tfexgen_NetplayManager.hpp"
#include "tfex/runtime/tfexgen_services.hpp"
#include "tfex/AiSlotButton.hpp"
#include "tfex/gen/tfexgen_InputService.hpp"
#include "tfex/gen/tfexgen_GameContext.hpp"   /* the test script into TF.EX's polled input (ScriptFeed) */

/* musl's pow as the browser runs it (source/program/mathlib: Emscripten's musl, no FMA path,
 * -ffp-contract=off). The SDK's pow differs from the browser's; this is the fix. */
extern "C" double xplay_musl_pow(double, double);

namespace rightstick { void Install(uintptr_t (*findExport)(const char*)); void RecordFullKey(const void* state, uint32_t id); }
namespace hangwatch { void Install(uintptr_t (*)(const char*)); void Beat(); void UpdateBegin(); void UpdateEnd(); void SetProfile(bool on); void ProfileReport(bool log); }
namespace gfxmem { void FlushIfLow(); }   /* gfxmem.cpp */
namespace xplay {

namespace off {
    constexpr uintptr_t PowImportSlot        = 0x47225b0;
    constexpr uintptr_t NpadFullKeyStateSlot = 0x4722830; /* nn::hid::GetNpadState(NpadFullKeyState*, const uint&) */
    constexpr uintptr_t MathRoundDouble      = 0xaca0;    /* fmov d1,#0.5; fadd d0,d0,d1; frintm d0,d0; ret */
    constexpr uintptr_t Player_NormalUpdate  = 0xae81d0;  /* int (Player*) */
    constexpr uintptr_t EngineUpdate_FixedStepBranch = 0x92d15c; /* after `bl Game.get_IsFixedTimeStep` */
    constexpr uint32_t  kFixedStepBranchInsn = 0x360001c0;      /* tbz w0, #0, +0x38 */
    /* Player.Update (+0xaef280), right after `input = PlayerInputs[i].GetState()` (slot 0x20,
     * InputState returned on the stack) has loaded the 28 bytes into x8/x9/x10/w11 and before they're
     * stored to Player.input (+0x148): `str w11, [x19, #0x160]`. x19 = the Player. */
    constexpr uintptr_t Player_Update_InputStore = 0xaef934;
    /* TFGame.Update(GameTime): the input update, then Engine.Update (+0x92d0b0). */
    constexpr uintptr_t TFGame_Update = 0xb75c60;
    constexpr uint32_t  kTFGameUpdateInsn = 0xa9be4ff4; /* stp x20, x19, [sp, #-0x20]! */
    /* The graphics layer's per-frame buffer allocator: x0 = a native ring of transient GPU buffers
     * (0x5600 bytes, inside a managed object) that tracks GPU frames (+0x55b8); rolling one back
     * trips an assertion. */
    constexpr uintptr_t GfxRingAlloc = 0x204d0;
    constexpr uint32_t  kInputStoreInsn = 0xb901626b;
}


/* The address of an exported (defined) function in any loaded module, from its dynamic symbol
 * table. At exl_main time the game's own import slots aren't bound yet (calling through them
 * crashed in rtld's lazy binder), so functions are looked up where they're defined (nnSdk). */
struct Elf64Sym { uint32_t name; uint8_t info, other; uint16_t shndx; uint64_t value, size; };
static uintptr_t FindExport(const char* wanted) {
    for (int i = (int)exl::util::ModuleIndex::Start; i < (int)exl::util::ModuleIndex::End; i++) {
        if (!exl::util::impl::mem_layout::s_ModuleBitset[i]) continue;
        uintptr_t base = exl::util::GetModuleInfo((exl::util::ModuleIndex)i).m_Total.m_Start;
        uint32_t mod0 = *reinterpret_cast<uint32_t*>(base + 4);
        if (std::memcmp(reinterpret_cast<void*>(base + mod0), "MOD0", 4) != 0) continue;
        uintptr_t dyn = base + mod0 + *reinterpret_cast<int32_t*>(base + mod0 + 4);
        uintptr_t symtab = 0, strtab = 0;
        for (auto* d = reinterpret_cast<int64_t*>(dyn); d[0] != 0; d += 2) {
            if (d[0] == 6) symtab = base + d[1];
            if (d[0] == 5) strtab = base + d[1];
        }
        if (!symtab || !strtab || strtab <= symtab) continue;
        /* The string table follows the symbol table in these modules. */
        for (auto* s = reinterpret_cast<Elf64Sym*>(symtab); reinterpret_cast<uintptr_t>(s + 1) <= strtab; s++) {
            if (s->shndx != 0 && s->value != 0 && std::strcmp(reinterpret_cast<const char*>(strtab + s->name), wanted) == 0)
                return base + s->value;
        }
    }
    return 0;
}

/* ---- Cross-play fixes ------------------------------------------------------------------------ */

static void ApplyFixes() {
    /* pow: lazily bound import slots are ordinary writable data (CodePatcher's code mirror doesn't
     * cover them). Written before the game's first call, so lazy binding never replaces it. */
    *reinterpret_cast<volatile uintptr_t*>(Base() + off::PowImportSlot) = reinterpret_cast<uintptr_t>(&xplay_musl_pow);

    /* Math.Round(double): frintn (ties to even), like .NET. */
    auto* code = reinterpret_cast<uint32_t*>(Base() + off::MathRoundDouble);
    if (code[0] == 0x1e6c1001 && code[1] == 0x1e612800 && code[2] == 0x1e654000 && code[3] == 0xd65f03c0) {
        exl::patch::CodePatcher p(off::MathRoundDouble);
        p.Write<uint32_t>(0x1e644000); /* frintn d0, d0 */
        p.Write<uint32_t>(0xd65f03c0); /* ret */
        HWLOG("[xplay] fixes: pow -> musl, Math.Round -> ties to even");
    } else {
        HWLOG("[xplay] Math.Round isn't as expected (%08x %08x): NOT patched", code[0], code[1]);
    }

    /* Fixed timestep: Engine.Update takes the IsFixedTimeStep branch always (TimeMult = TimeRate,
     * DeltaTime = TimeRate/60) instead of measuring real frame time, which jitters. The browser does
     * the same. `tbz w0, #0, <variable branch>` after the IsFixedTimeStep call -> nop. */
    auto* branch = reinterpret_cast<uint32_t*>(Base() + off::EngineUpdate_FixedStepBranch);
    if (*branch == off::kFixedStepBranchInsn) {
        exl::patch::CodePatcher p(off::EngineUpdate_FixedStepBranch);
        p.Write<uint32_t>(0xd503201f); /* nop */
        HWLOG("[xplay] fixes: fixed timestep");
    } else {
        HWLOG("[xplay] Engine.Update isn't as expected (%08x): fixed timestep NOT applied", *branch);
    }

    /* MonoGame's user buffers (GraphicsDevice.DrawUserIndexedPrimitives +0xc9xxx, the SpriteBatcher's draw): the index buffers (device
     * +0x260 16-bit, +0x268 32-bit; +0xd6a40) are made with max(count, 6000) entries and the vertex buffers (a dictionary at +0x258,
     * +0xd6860) with max(count, 2000), and each is replaced by a bigger one when a draw needs more. A wide level's 1008 tiles pass
     * 6000 indices: in a 4-player wide match a buffer was replaced mid-match, rollback keeps the device object out of its restores, and
     * a rollback to before that frame left the device pointing at a buffer the restored heap no longer had (crash in the next
     * LightingLayer.RenderLightCanvas, 2026-10-16). Made at the SpriteBatcher's cap from the start (Math.Min(n, 5461) sprites per draw,
     * +0xc8ca4: 32766 indices, 21844 vertices; ~0.7 MB), so they never grow. */
    struct MinSize { uintptr_t off; uint32_t was, now; const char* what; };
    static const MinSize kMin[] = { { 0xd6ac8, 0x5282ee01, 0x528fffc1, "user index buffers 6000 -> 32766" },     /* mov w1, #0x1770 -> #0x7ffe */
                                    { 0xd68fc, 0x5280fa01, 0x528aaa81, "user vertex buffers 2000 -> 21844" } };   /* mov w1, #0x7d0 -> #0x5554 */
    for (const MinSize& m : kMin) {
        auto* w = reinterpret_cast<uint32_t*>(Base() + m.off);
        if (*w == m.was) { exl::patch::CodePatcher p(m.off); p.Write<uint32_t>(m.now); HWLOG("[xplay] fixes: %s", m.what); }
        else HWLOG("[xplay] %s: unexpected %08x at +%#lx, NOT applied", m.what, *w, (unsigned long)m.off);
    }

    /* SoundEffectInstance.set_Volume (+0x47050): the Switch's throws ArgumentOutOfRange outside 0..1 (+0x470dc); the PC's FNA takes any
     * volume. TF.EX plays some at more (MatchmakingService's "Connection dropped": ui_clickSpecial.Play(160, 5)), and the throw from a
     * server message's game-thread action aborted the game (2026-10-29 console, idle in a lobby). The check becomes a clamp to 0..1. */
    static const uint32_t kVolumeCheck[] = { 0x1e202108, 0x540003cb, 0x1e2e1000, 0x1e202100, 0x54000368 };   /* fcmp s8,#0; b.lt; fmov s0,#1; fcmp s8,s0; b.hi */
    static const uint32_t kVolumeClamp[] = { 0x1e2e1000, 0x1e207908, 0x1e2703e0, 0x1e206908, 0xd503201f };   /* fmov s0,#1; fminnm s8,s8,s0; fmov s0,wzr; fmaxnm s8,s8,s0; nop */
    if (std::memcmp(reinterpret_cast<void*>(Base() + 0x470dc), kVolumeCheck, sizeof kVolumeCheck) == 0) {
        exl::patch::CodePatcher p(0x470dc);
        for (uint32_t insn : kVolumeClamp) p.Write<uint32_t>(insn);
        HWLOG("[xplay] fixes: SoundEffectInstance volume clamped to 0..1 (was a throw)");
    } else {
        HWLOG("[xplay] SoundEffectInstance.set_Volume isn't as expected at +0x470dc: volume clamp NOT applied");
    }
}

/* ---- Hardware probe configuration: sd:/xplay/hw.txt (the keys are parsed in LoadHwConfig) ----------
 * No file: only the timing of the game's own update is recorded. */
int32_t g_hwSeed = 0; bool g_hwSeedSet = false;
bool g_captureForced = false;   /* hw.txt "rngcapture 1": the gameplay stream on outside lobby matches too (scripted tests) */
bool g_npOn = false;              /* hw.txt "np 1": the match is driven by GGRS (this peer is lockstep by default) */
int g_npWaitSkip = 0;
int g_npSeat = 0, g_npDelay = 2 /* the session's input delay (ticks): TF.EX's GGRSConfig's, set at NetplayManager.Init (EndOfMatch.cpp) */, g_npPred = 0, g_npPad = 0;   /* npseat / npdelay / nppred / nppad (1 = the real controller instead of the script) */   /* npseat / npdelay / nppred */
int g_shiftFrom = 0, g_shiftN = 0; /* hw.txt "vsshift <fromT> <n>": from tick T >= fromT on, the script lookup and the log label use T - n (isolates a known intro-length difference) */
int g_vsStop = 0; /* hw.txt "vsstop <tick>": at that Level.Update, flush the log and park the game thread (so a match never reaches its end, which writes the save data) */
int g_vstart = 0; /* hw.txt "vstart 1": once two players have joined the rollcall, start the match natively */
/* The frame's cost split (2026-10-17, the case for 240 Hz on the Switch): per in-level frame, the game's own first-pass Level ticks, the
 * rollback bookkeeping (EndTick / BeginTick / PendingScan / the restore), GGRS (poll, events, advance), resimulation (its ticks with
 * their bookkeeping, and how many), then Draw and the idle wait until the next frame. Counters and svcGetSystemTick only;
 * ReportStats writes them every 600 in-level frames ("[hw] timing split"). */
FrameCost g_fc;
bool g_lobbyStart = false;  /* a lobby's StartLobby asked for the match (StartLobbyMatch): no rollcall wait, the lobby's archers */
int g_lobbyPlayers = 2;
int g_lobbyPred = 8;         /* hw.txt "lobbypred <n>": GGRS prediction for lobby matches (0 = lockstep; >0 also turns rollback on) */
int g_lobbyScript = 0;       /* hw.txt "lobbyscript 1": the scripted input (vs0/vs1.txt) instead of the controller, for log comparisons */
int g_resultsFrame = -1;     /* the GGRS frame whose Level.Update ran Session.CreateResults */
uint64_t g_syncWaitSince = 0; /* system tick of the first frame a lobby match waited for its first synchronization (NetplayGate) */
void* g_session = nullptr;   /* the Session that made the results (Session.GetWinner +0xb453b0, Scores int[] at +0x30) */
bool g_matchFinished = false;
uint64_t g_interruptedSince = 0;
bool g_remoteGone = false;      /* GGRS reported the remote interrupted/disconnected after the results appeared */   /* a lobby match is over (results shown, the remote closed the relay): the Level is frozen */
static bool g_freezeLevel = false;     /* no Level.Update until the scene changes (back to the lobby screen) */
bool g_vsLogDone = false;       /* the per-tick log covers the first match only (the comparison's reference) */
int g_goal = 2;              /* MatchSettings.GoalScore while the match runs: TF.EX test mode's 2 (xnet), or the lobby's */

/* ---- Determinism test: scripted input, per-frame state -------------------------------------- */

struct Step { char phase; int at; uint64_t buttons; int32_t x, y; };
static Step g_steps[512];
static int g_stepCount = 0;
static bool g_scriptLoaded = false;

static void LoadScript() {
    g_scriptLoaded = true;
    if (!hw::MountSd()) {
        HWLOG("[xplay] script: can't mount the SD card");
        return;
    }
    LoadHwConfig();
    nn::fs::FileHandle h;
    if (nn::fs::OpenFile(&h, "sd:/xplay/script.txt", nn::fs::OpenMode_Read) != 0) {
        HWLOG("[xplay] script: no sd:/xplay/script.txt (no scripted input; timing only)");
        return;
    }
    long size = 0;
    nn::fs::GetFileSize(&size, h);
    static char buf[32768];
    if (size > (long)sizeof(buf) - 1) size = sizeof(buf) - 1;
    nn::fs::ReadFile(h, 0, buf, size);
    nn::fs::CloseFile(h);
    buf[size] = 0;
    for (char* line = buf; line && *line && g_stepCount < 512;) {
        char* nl = std::strchr(line, '\n');
        if (nl) *nl = 0;
        if (line[0] == 'M' || line[0] == 'T' || line[0] == 'N' || (line[0] >= '2' && line[0] <= '7')) {   /* '2'..'7': controllers 3-8 */
            Step s{};
            s.phase = line[0];
            char* p = line + 1;
            s.at = (int)std::strtol(p, &p, 10);
            s.buttons = std::strtoull(p, &p, 16);
            s.x = (int32_t)std::strtol(p, &p, 10);
            s.y = (int32_t)std::strtol(p, &p, 10);
            g_steps[g_stepCount++] = s;
        }
        line = nl ? nl + 1 : nullptr;
    }
    HWLOG("[xplay] script: %d steps%s", g_stepCount, g_stepCount == 512 ? " (ERROR: the table is full, 512: the rest is left out)" : "");
}

/* The script's input at a point of a phase: the last step at or before it. */
static const Step* StepAt(char phase, int at) {
    const Step* found = nullptr;
    for (int i = 0; i < g_stepCount; i++)
        if (g_steps[i].phase == phase && g_steps[i].at <= at) found = &g_steps[i];
    return found;
}

static int g_polls = 0;                   /* player 1 polls since boot */
static int g_nuCount = 0;                 /* NormalUpdate calls: advances only while player 1 is alive in a level */

HOOK_DEFINE_TRAMPOLINE(NormalUpdateHook) {
    static int Callback(void* self) {
        g_nuCount++;
        return Orig(self);
    }
};

struct NpadFullKeyState {
    int64_t samplingNumber;
    uint64_t buttons;
    int32_t lx, ly, rx, ry;
    uint32_t attributes;
    uint32_t reserved;
};
using GetFullKeyFn = void (*)(NpadFullKeyState*, const uint32_t&);
static GetFullKeyFn g_getFullKey = nullptr;


/* Menu progress, for scripted runs that can't be watched: TFGame.Players / Characters (static field
 * slots +0x4793890 / +0x4792458, each pointing at the field, which holds the array: data at +0x10)
 * and whether MainMenu.CurrentMatchSettings (+0x4793818) is set, and its Mode (+0x30). */
void* volatile g_game = nullptr; /* the TFGame (Engine) instance, from TFGame.Update */
bool g_vsStarted = false;
int g_matchPads = 0;   /* hw.txt matchpads 1 (tests): in a match controller 1 holds Right, controller 2 Left (which seat follows which controller) */
static void LogMenuState() {
    auto arr = [](uintptr_t slot) -> uint8_t* {
        uintptr_t f = *reinterpret_cast<uintptr_t*>(Base() + slot);
        uintptr_t a = f ? *reinterpret_cast<uintptr_t*>(f) : 0;
        return a ? *reinterpret_cast<uint8_t**>(a + 0x10) : nullptr;
    };
    uint8_t* pl = arr(0x4793890);
    int32_t* ch = reinterpret_cast<int32_t*>(arr(0x4792458));
    uintptr_t f = *reinterpret_cast<uintptr_t*>(Base() + 0x4793818);
    uintptr_t ms = f ? *reinterpret_cast<uintptr_t*>(f) : 0;
    if (!pl || !ch) return;
    char tn[64] = "?";
    uintptr_t scene = g_game ? *reinterpret_cast<uintptr_t*>(reinterpret_cast<uintptr_t>(g_game) + 0xe8) : 0;
    if (scene) rnd::TypeName(reinterpret_cast<void*>(scene), tn, sizeof tn);
    static bool dumpedLevel = false;
    if (scene && !dumpedLevel && !std::strcmp(tn, "Level")) {
        dumpedLevel = true;
        for (uint32_t slot = 0x10; slot <= 0x90; slot += 8)
            HWLOG("[xplay]   Level vtable slot %#x -> +%#lx", slot, (unsigned long)(rnd::VtableBody(reinterpret_cast<void*>(scene), slot) - Base()));
    }
    if (ms && g_polls % 1200 == 0) {
        for (int o = 0x10; o < 0x80; o += 8) {
            uintptr_t v = *reinterpret_cast<uintptr_t*>(ms + o);
            char tn2[64] = "";
            if (!(v & 7) && (v >> 32) == (ms >> 32)) rnd::TypeName(reinterpret_cast<void*>(v), tn2, sizeof tn2);
            HWLOG("[xplay]   settings+%#x = %#lx %s", o, (unsigned long)v, tn2);
        }
    }
    HWLOG("[xplay] menu poll %d: scene %s state %d: players %d%d%d%d chars %d %d %d %d settings %#lx mode %d", g_polls, tn,
          scene ? *reinterpret_cast<int32_t*>(scene + 0xb4) : -1, pl[0], pl[1], pl[2], pl[3], ch[0], ch[1],
          ch[2], ch[3], (unsigned long)ms, ms ? *reinterpret_cast<int32_t*>(ms + 0x30) : -1);
}

bool g_vsOn = false, g_vsPending = false;
int g_lvTick = 0;
static void GetFullKeyHook(NpadFullKeyState* out, const uint32_t& id) {
    g_getFullKey(out, id);
    rightstick::RecordFullKey(out, id);   /* the right stick, before the tests' presses (rightstick.cpp) */
    {   /* which controller ids does the game poll, and which does the system say are connected? */
        static unsigned polls[16], connected[16];
        static int total = 0;
        if (id < 16) { polls[id]++; if (out->attributes & 1) connected[id]++; }
        if (++total == 3000) {
            char b[400]; int n = 0;
            for (int i = 0; i < 16; i++) if (polls[i]) n += snprintf(b + n, sizeof b - n, " id%d:%u(%u connected)", i, polls[i], connected[i]);
            HWLOG("[xplay] npad polls in the first 3000:%s", b);
        }
    }
    if (id == 0) if (unsigned long mp = tfex_MatchPauseTestButtons()) { out->buttons |= mp; out->attributes |= 1; }   /* hw.txt "lobbypause": the pause overlay test */
    if (g_vsStarted && g_matchPads && id <= 1) { out->buttons |= id == 0 ? (1u << 14) : (1u << 12); out->attributes |= 1; return; }   /* matchpads: controller 1 Right, controller 2 Left */
    if (g_vsStarted) return; /* the versus match is running: no more menu input */
    if (megamod::LocalTestOn()) {   /* hw.txt "localvs": controllers 1-8 walk to a local match (megamod/localtest.cpp) */
        if (id < 8) {
            out->attributes |= 1; out->buttons = megamod::LocalTestButtons(id);
            megamod::LocalTestStick(id, &out->rx, &out->ry);
            rightstick::RecordFullKey(out, id);   /* (the test's right stick and click, as the AI controls read them) */
        }
        return;
    }
    if (id >= 1 && (int)id < tfex::MainMenuPatch::TestLocalPlayers())   /* hw.txt lobbylocal <n>: controllers 2..n, connected while they press */
        if (unsigned long l = tfex::MainMenuPatch::AutoUiLocalButtons((int)id)) { out->buttons |= l; out->attributes |= 1; return; }
    if (id == 1) { /* player 2: "N <player 1 poll> ..." menu steps */
        if (!g_scriptLoaded) LoadScript();
        const Step* n = StepAt('N', g_polls);
        if (n) { out->buttons = n->buttons; out->lx = n->x; out->ly = n->y; out->attributes |= 1; }
        return;
    }
    if (id >= 2 && id <= 7) { /* controllers 3-8: "<id> <player 1 poll> ..." steps (the megamod's 8-player local tests) */
        if (!g_scriptLoaded) LoadScript();
        const Step* n = StepAt((char)('0' + id), g_polls);
        if (n) { out->buttons = n->buttons; out->lx = n->x; out->ly = n->y; out->attributes |= 1; }
        return;
    }
    if (id != 0) return; /* player 1 */
    if (unsigned long ui = tfex::MainMenuPatch::AutoUiButtons()) { out->buttons |= ui; out->attributes |= 1; }   /* hw.txt "lobbyui": the debug driver's presses */
    if (unsigned long ev = tfex::EndOfMatch::TestButtons()) { out->buttons |= ev; out->attributes |= 1; }   /* hw.txt "lobbyvote": the end-of-match menu */
    if (!g_scriptLoaded) LoadScript();
    g_polls++;
    if (g_polls % 120 == 0 && !g_vsStarted && !tfex::MainMenuPatch::InEndgame()) LogMenuState();   /* (not on a lobby match's results screen: still a Level) */
    const Step* s = StepAt('M', g_polls);
    if (s) {
        out->buttons = s->buttons;
        out->lx = s->x;
        out->ly = s->y;
        out->attributes |= 1; /* connected */
    }
}


/* ---- Test 5: the scripted 2-player versus match (TEST5-MATCH-SETUP.md) ----------------------------
 * Started natively (MaybeNativeStart), then, once per Level.Update (LevelUpdateShell around TF.EX's generated LevelPatch, which sets
 * Engine.TimeMult / DeltaTime / DeltaTicks from the session's step and drops the ReplayRecorder in its Level ctor postfix):
 *   - Scene.FrameCounter = (float)tick (ticks are 1-based and not reset per round);
 *   - a log line "[vs] <tick> rng=<fold> p0 x y sx sy alive=<0|1> p1 ..." (float bits) before the tick runs.
 * Input: at the GetState call site of Player.Update, seats 0 and 1 get the script's InputState on every
 * tick T >= 0, T = tick - origin, origin = the first tick whose Player.Update reaches GetState with
 * State != Frozen. The scripts (sd:/xplay/vs0.txt, vs1.txt) use the browser's format
 * "frame:flags:moveX:moveY,..." (flags in hex), each held until the next step. */
struct VsStep { int frame, flags, mx, my; };
static VsStep g_vsSteps[kNpSeats][600];
static int g_vsN[kNpSeats] = {};
int g_vsOrigin = -1;
void* g_seatPlayer[kNpSeats];
static int g_seatTick[kNpSeats] = { -9, -9, -9, -9, -9, -9, -9, -9 };

void LoadVsScripts() {
    static char buf[16384];
    for (int s = 0; s < kNpSeats; s++) {
        char path[32];
        snprintf(path, sizeof path, "sd:/xplay/vs%d.txt", s);
        g_vsN[s] = 0;
        nn::fs::FileHandle h;
        if (nn::fs::OpenFile(&h, path, nn::fs::OpenMode_Read) != 0) { HWLOG("[vs] no %s", path); continue; }
        long size = 0;
        nn::fs::GetFileSize(&size, h);
        if (size > (long)sizeof(buf) - 1) size = sizeof(buf) - 1;
        nn::fs::ReadFile(h, 0, buf, size);
        nn::fs::CloseFile(h);
        buf[size] = 0;
        char* p = buf;
        while (*p && g_vsN[s] < 600) {
            VsStep st{};
            char* e;
            st.frame = (int)std::strtol(p, &e, 10);
            if (*e != ':') break;
            st.flags = (int)std::strtol(e + 1, &e, 16);
            st.mx = (int)std::strtol(e + 1, &e, 10);
            st.my = (int)std::strtol(e + 1, &e, 10);
            g_vsSteps[s][g_vsN[s]++] = st;
            p = e;
            if (*p == ',') p++;
            while (*p == '\n' || *p == '\r' || *p == ' ') p++;
        }
        HWLOG("[vs] script %d: %d steps", s, g_vsN[s]);
    }
}
/* The step in force at tick T (the last with frame <= T), or null. */
static const VsStep* VsAt(int seat, int T) {
    const VsStep* f = nullptr;
    for (int i = 0; i < g_vsN[seat] && g_vsSteps[seat][i].frame <= T; i++) f = &g_vsSteps[seat][i];
    return f;
}


/* The per-tick lines are kept in memory and written to sd:/xplay/vs-log-3m.txt every 600 ticks: a
 * debug-log call per tick made the emulated run about 8 ticks/s. */
constexpr size_t kVsLogMax = 3 * 1024 * 1024; /* files on the SD card cannot grow: change the file name in VsFlush with this size */
static char g_vsBuf[kVsLogMax];
static size_t g_vsLen = 0;
static void VsAppend(const char* line) {
    size_t n = std::strlen(line);
    if (g_vsLen + n + 2 > kVsLogMax) return;
    std::memcpy(g_vsBuf + g_vsLen, line, n);
    g_vsLen += n;
    g_vsBuf[g_vsLen++] = '\n';
}
/* The tick log to the SD card. On the writer thread (hw::WriteFixedAsync, only what was appended): synchronously it was a 3 MB
 * rewrite on the game thread, ~1.7 s on the console, at every death (VsEvent) and every 300 ticks: the console's lag spikes. */
void VsFlush() { hw::WriteFixedAsync("sd:/xplay/vs-log-3m.txt", g_vsBuf, g_vsLen, kVsLogMax); }
static void VsFlushNow() { hw::WriteFixedFile("sd:/xplay/vs-log-3m.txt", g_vsBuf, g_vsLen, kVsLogMax); }   /* the test's end: on disk before Ryujinx stops */


/* TFEX-DIFF S-native: the Switch's SFX.Play / SFXInstanced.Play take panX = 1e8 (their default argument, rodata +0x38e2860) for "the
 * screen's centre" and turn it into (int)screen width / 2 (the static int behind +0x4792130, the width CalculatePan divides by) inside
 * their body (+0x94a1a8, +0x94af50); the PC's default argument is the centre itself (160). TF.State's SFXPatch / SFXInstancedPatch prefixes
 * run before that body and pass panX on (AddToPlayedList, CalculatePan -> SoundEffectInstance.set_Pan, which throws outside [-1, 1]): the
 * same centre here first, then TF.State's prefix and the original as Harmony runs them. */
static float SfxPanX(float panX) {
    float centre; std::memcpy(&centre, reinterpret_cast<void*>(Base() + 0x38e2860), 4);
    if (panX != centre) return panX;
    int32_t* w = *reinterpret_cast<int32_t**>(Base() + 0x4792130);
    return w && *w > 0 ? (float)(*w / 2) : 160.f;
}
static void SfxPlayShell(tfex::gen::hooks::SFX_Play& c) { c.panX = SfxPanX(c.panX); c(); }
static void SfxInstancedPlayShell(tfex::gen::hooks::SFXInstanced_Play& c) { c.panX = SfxPanX(c.panX); c(); }
/* Level.HandlePausing (first call of Level.Update): TF.EX's LevelPatch prefixes (generated hooks: EnsureEveryControllerSlot; in a netplay
 * lobby's match the LightPauseMenu overlay instead of the game's pause, tfex/MatchPause.cpp). TFEX-DIFF S-tools: the scripted local test
 * (g_vsOn outside a lobby match) never pauses: the game pauses on focus loss (Engine.IsActive), which the hidden Ryujinx window flaps. */
static void HandlePausingShell(tfex::gen::hooks::Level_HandlePausing& c) {
    if (g_vsOn && !g_lobbyStart) return;
    c();
    if (g_lobbyStart) tfex_MatchPauseLogChange();   /* the overlay's openings and closings, with the frame (the pause log) */
}


/* ---- Netplay (lockstep GGRS, ../ggrs-switch) -------------------------------------------------------------------- */
void* g_npSession = nullptr;
int g_npFrame = 0;                /* GGRS frame of the tick being run (1-based, like TF.EX's Scene.FrameCounter source) */
int g_npN = 2;                    /* players in the session (the lobby's) */
int64_t SyncWaitMs() { return g_syncWaitSince ? (int64_t)((svcGetSystemTick() - g_syncWaitSince) / 19200ull) : -1; }   /* tfex/gen services: TF.EX's _synchronizationTimer */
void* NpSessionForStats(int* seat, int* n) { *seat = g_npSeat; *n = g_npN; return g_npSession; }   /* tfex/gen services: GetNetworkStats(PerSeat) */
/* the inputs of the frame being simulated, for the Switch's diagnostics (desync records, remote changes, resimulation records): TF.EX's
 * GameContext.GetCurrentInput per seat, as its GetState postfix reads them (C2 step 3) */
void NpCurrentInputs(TfxInput* out) {
    void* gc = tfex::gen::svc::GameContextObj();
    for (int i = 0; i < kNpSeats; i++) {
        tfex::gen::st::Input in = gc ? tfex::gen::GameContext::GetCurrentInput(gc, i) : tfex::gen::st::Input{};
        static_assert(sizeof in == sizeof(TfxInput), "TfxInput is TF.EX's Input"); std::memcpy(&out[i], &in, sizeof in);
    }
}
static int g_lvDepth = 0;         /* Level.Update calls running: 1 inside TF.EX's nested first update of the next Level (SkipLevelLoaderIfNeeded, same GGRS frame) */
bool g_resim = false;            /* a rollback resimulation: no log lines */
int g_npEvents[8];
static int CurFrame() { return g_npOn ? g_npFrame : g_lvTick; }
bool ReSimulatingForServices() { return g_resim; }   /* tfex/gen services: HasFramesToReSimulate */
int SimFrameForServices(bool* resim, bool* inMatch) { *resim = g_resim; *inMatch = g_lobbyStart && g_vsStarted; return CurFrame(); }   /* tfex/MatchPause.cpp */

void LogPlayersArray(const char* when);
void VsEvent(const char* what);
/* Calls virtual method `slot` (a byte offset into the vtable) of a managed object, with the game's `this` adjustment
 * (entry bits 59.. & 0x10, as in `lsr x9, x8, #0x3b; and x9, x9, #0x10; add x0, x8, x9`). */
void CallVirt0(void* obj, uint32_t slot) {
    uintptr_t desc = *reinterpret_cast<uintptr_t*>(obj);
    uintptr_t* vt = *reinterpret_cast<uintptr_t**>(desc + 8);
    uintptr_t entry = vt[slot / 8];
    auto fn = reinterpret_cast<void (*)(void*)>(entry & 0x7fffffffffffffffull);
    fn(reinterpret_cast<uint8_t*>(obj) + ((entry >> 0x3b) & 0x10));
}

/* One line per Level.Update (not the nested one): the gameplay RNG fold and both archers (float bits), then the arrows. */
/* With real rollback (nppred > 0) a frame can be simulated several times: the lines are kept per frame and written once the
 * frame's inputs are confirmed (the last simulation is the one that counts). */
bool g_rbReal = false;
int g_rbLoads = 0, g_rbMaxDepth = 0, g_rbResimTicks = 0;
/* Per lobby match (2026-10-16 21:16: a rematch ran 1574 frames against a human at 283 ms ping with no rollback at all, and the console
 * never saw the kill that ended it on the page): how often a remote's input as simulated changed, and the rollbacks, at the match's end. */
int g_remoteChanges = 0, g_rbLoadsAtStart = 0, g_matchFirstTicks = 0, g_remotesGone = 0;
void NpStateForServices(bool* resim, int* inputDelay, int* remotesGone, bool* remoteGone) {   /* tfex/gen services (read-only): IsRollbackFrame, */
    /* TF.EX's own flag (NetplayManager._isRollbackFrame: set at a rollback's load, cleared by LevelPatch's Update postfix) */
    *resim = g_npSession && tfex::gen::NetplayManager::IsRollbackFrame(g_npSession);
    *inputDelay = g_npDelay; *remotesGone = g_remotesGone; *remoteGone = g_remoteGone;   /* GetEffectiveInputDelay, IsDisconnected */
}
TfxInput g_lastRemote[kNpSeats];
struct PendLine { int label; char a[300]; char r[400]; };
static PendLine g_pend[64];
int g_commitNext = -1;
static void LogOut(int label, const char* a, const char* r) {
    if (!g_rbReal) { if (!g_resim) { VsAppend(a); VsAppend(r); } return; }
    PendLine& p = g_pend[label & 63];
    p.label = label;
    snprintf(p.a, sizeof p.a, "%s", a);
    snprintf(p.r, sizeof p.r, "%s", r);
    if (g_commitNext < 0) g_commitNext = label;
}
void LogCommit(int upTo) {
    for (; g_commitNext >= 0 && g_commitNext <= upTo; g_commitNext++) {
        PendLine& p = g_pend[g_commitNext & 63];
        if (p.label != g_commitNext) continue;
        VsAppend(p.a); VsAppend(p.r);
    }
}
static void VsLogTick(void* level, int label) {
    char line[300];
    int n = snprintf(line, sizeof line, "[vs] %d rng=%016llx", label, (unsigned long long)rnd::StateFold());
    for (int s = 0; s < 2; s++) {
        uintptr_t pl = reinterpret_cast<uintptr_t>(g_seatPlayer[s]);
        if (!pl || *reinterpret_cast<uintptr_t*>(pl + 0x20) != reinterpret_cast<uintptr_t>(level)) {
            /* the round change: the first update of the new Level (TF.EX's nested one) comes before any of its players updated, so the seat
             * still points at last round's archer: take this Level's player of that seat (Level[GameTags.Player], tag 0), as the browser logs */
            using TagFn0 = uintptr_t (*)(void*, int);
            uintptr_t lst = reinterpret_cast<TagFn0>(Base() + 0x943f10)(level, 0);
            int cnt = lst ? *reinterpret_cast<int32_t*>(lst + 0x18) : 0;
            uintptr_t arr = lst ? *reinterpret_cast<uintptr_t*>(lst + 0x10) : 0;
            uintptr_t* d = arr ? *reinterpret_cast<uintptr_t**>(arr + 0x10) : nullptr;
            for (int k = 0; d && k < cnt; k++)
                if (*reinterpret_cast<int32_t*>(d[k] + fld::Player_PlayerIndex) == s) { pl = d[k]; break; }
        }
        bool alive = pl && *reinterpret_cast<uintptr_t*>(pl + 0x20) == reinterpret_cast<uintptr_t>(level) && !*reinterpret_cast<uint8_t*>(pl + 0x108); /* in this Level (Entity.Scene +0x20), not Dead */
        if (alive)
            n += snprintf(line + n, sizeof line - n, " p%d %08x %08x %08x %08x alive=1", s, *reinterpret_cast<uint32_t*>(pl + fld::Entity_PositionX),
                          *reinterpret_cast<uint32_t*>(pl + fld::Entity_PositionY), *reinterpret_cast<uint32_t*>(pl + fld::Player_SpeedX),
                          *reinterpret_cast<uint32_t*>(pl + fld::Player_SpeedY));
        else
            n += snprintf(line + n, sizeof line - n, " p%d 00000000 00000000 00000000 00000000 alive=0", s);
    }
    const uint8_t* lv = reinterpret_cast<const uint8_t*>(level);
    snprintf(line + n, sizeof line - n, " | fz=%d%d end=%d%d", lv[0xf8], lv[0xf9], lv[0x88], lv[0x89]);
    /* arrows: Level[GameTags.Arrow] (tag 7): count and the position bit patterns sorted by (x, y), like the browser's R lines */
    using TagFn = uintptr_t (*)(void*, int);
    uintptr_t al = reinterpret_cast<TagFn>(Base() + 0x943f10)(level, 7);
    int ac = al ? *reinterpret_cast<int32_t*>(al + 0x18) : 0;
    uintptr_t aa = al ? *reinterpret_cast<uintptr_t*>(al + 0x10) : 0;
    uintptr_t* ad = aa ? *reinterpret_cast<uintptr_t**>(aa + 0x10) : nullptr;
    uint64_t pos[24];
    int np = 0;
    for (int k = 0; ad && k < ac && np < 24; k++)
        pos[np++] = ((uint64_t)*reinterpret_cast<uint32_t*>(ad[k] + fld::Entity_PositionX) << 32) | *reinterpret_cast<uint32_t*>(ad[k] + fld::Entity_PositionY);
    std::sort(pos, pos + np);
    char r[400];
    int m = snprintf(r, sizeof r, "[vs] R %d arrows=%d list=", label, ac);
    for (int k = 0; k < np && m < 380; k++) m += snprintf(r + m, sizeof r - m, "%s%08x:%08x", k ? ";" : "", (unsigned)(pos[k] >> 32), (unsigned)pos[k]);
    LogOut(label, line, r);
}

/* TF.EX's sound reconciliation (TF.State's SFXPatch / SFXService, generated) seen from outside, for the log: what each Synchronize started,
 * and what it stopped (a rollback undid it) or found finished, from StateContext's playing list before and after the call. The first 16
 * starts and the first 16 stops as lines, then the counts every 1200 synchronized frames and at vsstop. */
static void* g_sfxBefore[128];
static bool g_sfxWasPlaying[128];
static void* g_sfxBeforeName[128];   /* (read before the call: a removed entry may be collected during it; the names are SFXPatch's file names) */
static int32_t g_sfxBeforeFrame[128];
static int g_sfxNBefore = 0, g_sfxStartLeft = 16, g_sfxStopLeft = 16;
static uint32_t g_sfxSyncs = 0, g_sfxStarted = 0, g_sfxStopped = 0, g_sfxFinished = 0, g_sfxOver = 0;
static void* SfxCurrent() {
    void* ctx = tfex::gen::Auto_ServiceCollections::ResolveStateContext();
    return ctx ? tfex::gen::Auto_StateContext::GetCurrentSfxs(ctx) : nullptr;
}
static int SfxCount(void* l) { return l ? tfex::gen::Fn<int32_t (*)(void*)>(tfex::gen::bind::m_List_1_f983_get_Count)(l) : 0; }
static void* SfxAt(void* l, int i) { return tfex::gen::Fn<void* (*)(void*, int32_t)>(tfex::gen::bind::m_List_1_f983_get_Item_e2bf)(l, i); }
static bool SfxPlaying(void* p) {   /* SoundEffectInstance.State == Playing */
    void* inst = tfex::gen::Auto_SoundEffectPlaying::get_SoundEffectInstance(p);
    return inst && tfex::gen::Fn<int32_t (*)(void*)>(tfex::gen::bind::m_SoundEffectInstance_get_State)(inst) == 0;
}
static const char* SfxName(void* str, char* out, int cap) {   /* a managed System.String (length +0x10, UTF-16 +0x14) as ASCII */
    if (!str) return "(no name)";
    int32_t len = *reinterpret_cast<int32_t*>(reinterpret_cast<uintptr_t>(str) + 0x10);
    const uint16_t* c = reinterpret_cast<const uint16_t*>(reinterpret_cast<uintptr_t>(str) + 0x14);
    int i = 0;
    for (; i < len && i < cap - 1; i++) out[i] = c[i] < 0x80 ? (char)c[i] : '?';
    out[i] = 0;
    return out;
}
static void SfxLogStats(const char* when) {
    void* l = SfxCurrent();
    HWLOG("[sfx] %s: TF.EX's Synchronize ran %u times: started %u, stopped %u (rolled back), finished %u; playing now %d%s", when, g_sfxSyncs,
          g_sfxStarted, g_sfxStopped, g_sfxFinished, SfxCount(l), g_sfxOver ? " (log list overflowed)" : "");
}
static void SfxLogBefore() {
    void* l = SfxCurrent();
    int n = SfxCount(l);
    if (n > 128) { g_sfxOver++; n = 128; }
    for (int i = 0; i < n; i++) {
        void* p = g_sfxBefore[i] = SfxAt(l, i);
        g_sfxWasPlaying[i] = SfxPlaying(p);
        g_sfxBeforeName[i] = tfex::gen::Auto_SoundEffectPlaying::get_Name(p);
        g_sfxBeforeFrame[i] = tfex::gen::Auto_SoundEffectPlaying::get_Frame(p);
    }
    g_sfxNBefore = n;
}
static void SfxLogAfter(int frame) {
    void* l = SfxCurrent();
    int n = SfxCount(l);
    char nm[64];
    for (int i = 0; i < n; i++) {   /* new in the list: started */
        void* p = SfxAt(l, i);
        bool seen = false;
        for (int k = 0; k < g_sfxNBefore && !seen; k++) seen = g_sfxBefore[k] == p;
        if (seen) continue;
        g_sfxStarted++;
        if (g_sfxStartLeft > 0) { g_sfxStartLeft--; HWLOG("[sfx] F %d TF.EX's Synchronize started %s@%d", frame, SfxName(tfex::gen::Auto_SoundEffectPlaying::get_Name(p), nm, sizeof nm), tfex::gen::Auto_SoundEffectPlaying::get_Frame(p)); }
    }
    for (int k = 0; k < g_sfxNBefore; k++) {   /* gone from the list: stopped while playing, or finished */
        bool still = false;
        for (int i = 0; i < n && !still; i++) still = SfxAt(l, i) == g_sfxBefore[k];
        if (still) continue;
        if (g_sfxWasPlaying[k]) g_sfxStopped++; else g_sfxFinished++;
        if (g_sfxWasPlaying[k] && g_sfxStopLeft > 0) { g_sfxStopLeft--; HWLOG("[sfx] F %d TF.EX's Synchronize stopped %s@%d (a rollback undid it)", frame, SfxName(g_sfxBeforeName[k], nm, sizeof nm), g_sfxBeforeFrame[k]); }
    }
    if (++g_sfxSyncs % 1200 == 0) SfxLogStats("periodic");
}

/* [perf]'s counters (LpFrame, by the Draw hook, explains them) */
struct LocalPerf { int frames = 0, updates = 0, owed = 0, dropped = 0, outlines = 0; double upd = 0, lv = 0, pre = 0, outline = 0, draw = 0, worst = 0; uint64_t t0 = 0, last = 0; };
static LocalPerf g_lp;
static void* g_lpLevel = nullptr;
static int g_lpWindows = 0;
static uint64_t g_lpPre0 = 0;
static bool g_lpLocal = false;   /* this frame's scene is a Level outside a session (FrameShell) */
static void LpBeforeOrig(tfex::gen::hooks::Level_Update&) { if (g_lpPre0) { g_lp.pre += Ms(svcGetSystemTick() - g_lpPre0); g_lpPre0 = 0; } }

/* Level.Update: TF.EX's LevelPatch (generated hooks, on with every TF.EX patch: the prefix skips the update while awaiting
 * synchronization, sets Engine.TimeMult / DeltaTime / DeltaTicks from Game.TargetElapsedTime (xplay's SessionTimeStep: the session's
 * step), the occasional player indicator; the postfix: deferred notifications, the waiting notification and the match-start warnings,
 * the spectators' tally, SetIsRollbackFrame(false), TF.State's owned controllers, the entity lists, the sprites, TF.State's sound
 * reconciliation and the nested level load, SkipLevelLoaderIfNeeded) runs inside c(). Around it only the Switch's tooling (S-tools):
 * the match freeze, the tick log (VsLogTick, vsstop, VsFlush), rumble per simulated frame, the [sfx] log, statemeasure, the cost timer.
 * TFEX-DIFF S-rollback: Scene.FrameCounter = the GGRS frame, set here as TF.EX's TFGame.Update / NetplayLogic do before each advance,
 * until those are generated (docs/ws-netplay-loop.md steps 2-3). The gate (netplay_gate.cpp) runs the frame's update through TFGame.Update
 * and each resimulation by calling Scene.Update itself; both come here. */
static void LevelUpdateShell(tfex::gen::hooks::Level_Update& c) {
    if (g_freezeLevel) return;
    void* level = c.self;
    const bool nested = g_lvDepth > 0;
    /* in TF.EX's netplay frame loop (its TFGame.Update prefix: synchronized, or a sync test; not the unsynchronized or disconnected update):
     * a resimulation while TF.EX's NetplayManager has frames to resimulate (NetplayLogic's AdvanceFrame requests), else the frame's own update */
    void* nm = g_npSession;
    const bool inLoop = !nested && nm && g_npOn && g_vsStarted && !tfex::gen::NetplayEntry::IsRunningUnsynchronizedUpdate
                        && (tfex::gen::NetplayManager::IsSynchronized(nm) || tfex::gen::NetplayManager::IsTestMode(nm)) && !tfex::gen::NetplayManager::IsDisconnected(nm);
    const int resimFrame = inLoop && tfex::gen::NetplayManager::HaveFramesToReSimulate(nm) ? ResimBegin() : -1;
    if (inLoop && resimFrame < 0) FrameOwnBegin();
    int label = -1;
    if (g_vsPending) { g_vsPending = false; g_vsOn = true; }
    if (g_vsOn) {
        g_lvTick++;
        int frame = CurFrame();
        label = frame; if (!g_npOn && g_shiftN && g_vsOrigin >= 0 && g_lvTick - g_vsOrigin >= g_shiftFrom) label -= g_shiftN;   /* vsshift: the browser's numbering (scripted run only) */
        *reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(level) + 0x30) = (float)label;   /* Scene.FrameCounter = (float)ggrsFrame */
        rnd::SetTraceFrame(label);
        rumble::LevelUpdateBegin(label, g_resim);                                                /* rumble per frame (rumble.cpp) */
        if (nested && !g_vsLogDone) VsLogTick(level, label);   /* the frame's last Level.Update (TF.EX's nested first update of the next Level) is its line, as in the browser */
        if (!nested) {
            if (!g_vsLogDone) VsLogTick(level, label);
            if (g_lvTick % 300 == 0) VsFlush();
            if (g_vsStop && g_lvTick >= g_vsStop && !g_resim) {
                /* (no commit here: RbGate commits a line once its frame is final, i.e. confirmed before the previous advance, which corrected it.
                 * Committing up to the current confirmed frame + 1 here wrote the frames confirmed in this tick's poll before the next advance
                 * could correct them: the harness's tick compare then counted a few predicted lines as differing, with the digest clean) */
                {
                    char b[200];
                    snprintf(b, sizeof b, "[rb] rollbacks %d (deepest %d frames), resimulated ticks %d", g_rbLoads, g_rbMaxDepth, g_rbResimTicks);
                    VsAppend(b); HWLOG("%s", b);
                }
                SfxLogStats("vsstop");
                rnd::DumpDraws(VsAppend);
                VsEvent("vsstop: parking the game thread");
                VsFlushNow();   /* on the card before the harness sees the next line and stops Ryujinx */
                HWLOG("[vs] vsstop: parking the game thread at tick %d", g_lvTick);
                if (devupdate::Enabled()) {   /* the test's logs to the dev server (tools/devserver.py) */
                    VsFlush();
                    devupdate::Upload("vs-log.txt", g_vsBuf, (int)g_vsLen);
                    devupdate::UploadFile("hw-log.txt", "sd:/xplay/hw-log.txt");
                }
                for (;;) svcSleepThread(1000000000ll);
            }
        }
    }
    /* the [sfx] log: TF.State's playing list before and after the update (only TF.EX's SynchronizeSfx changes it, in the postfix and the
     * nested update's), on a frame's real update; a level load (ClearSfx) isn't counted */
    const bool sfxLog = label >= 0 && !nested && !g_resim && tfex::gen::svc::IsCaptureActive();
    if (sfxLog) SfxLogBefore();
    g_lvDepth++;
    {
        double ignored = 0;
        CostTimer ct(g_resim || nested ? &ignored : &g_fc.tick);   /* (resimulated ticks: in ResimEnd's total; the nested one: in this one) */
        if (!g_npSession && !nested) {   /* [perf]: a local match's Level.Update, its prefixes apart */
            const uint64_t a = svcGetSystemTick();
            g_lpPre0 = a; c.beforeOrig_ = &LpBeforeOrig;
            c();
            g_lpPre0 = 0; g_lp.lv += Ms(svcGetSystemTick() - a); g_lp.updates++;
        } else c();
    }
    g_lvDepth--;
    if (!nested) ::gfxmem::FlushIfLow();   /* a long catch-up frame flushes before the extra command memory runs out (gfxmem.cpp) */
    if (label >= 0 && !nested && !g_resim) {   /* (once, the first synchronized frames: TF.EX's step is the session's) */
        static int stepLogs = 0;
        if (c.ran_ && stepLogs < 3 && tfex::gen::svc::IsCaptureActive()) {
            stepLogs++;
            int64_t te = *reinterpret_cast<int64_t*>(reinterpret_cast<uintptr_t>(g_game) + 0x58);
            int64_t dt = *reinterpret_cast<int64_t*>(Slot(0x4791e18));
            HWLOG("[vs] Level.Update frame %d: TF.EX's LevelPatch step: Engine.DeltaTicks %lld, TimeMult %.7f (the session's step %lld: %s; the game's own TargetElapsedTime %lld)",
                  label, (long long)dt, *reinterpret_cast<float*>(Slot(0x4791df8)), (long long)SessionStepTicks(), dt == SessionStepTicks() ? "same" : "DIFFERENT", (long long)te);
        }
        if (!c.ran_) { static int skipped = 0; if (skipped++ < 4) HWLOG("[vs] Level.Update frame %d skipped by TF.EX's LevelPatch (awaiting synchronization)", label); }
    }
    rumble::LevelUpdateEnd();
    if (sfxLog) {
        if (*reinterpret_cast<void**>(reinterpret_cast<uintptr_t>(g_game) + 0xe8) == level) SfxLogAfter(label);
        else SfxLogBefore();   /* a new Level: the list starts over (ClearSfx) */
    }
    if (label >= 0 && !nested && !g_resim) rumble::Synchronize(label);   /* rumbles a rollback undid stop */
    if (g_vsOn && g_npOn && !nested && !g_resim && tfex::gen::statert::On()) tfex::gen::statert::MeasureTick(level, CurFrame());   /* hw.txt statemeasure */
    if (resimFrame >= 0) ResimEnd(resimFrame);
}


/* Round flow markers for the tick log (compare with the browser's `A ... round_ending` lines). */
void VsEvent(const char* what) {
    char l[160];
    snprintf(l, sizeof l, "[vs] event %s at tick %d", what, g_lvTick);
    VsAppend(l);
    VsFlush();
}
/* Player.Die(DeathCause cause, int killerIndex, bool brambled, bool laser) +0xaee3d0 (every death goes through it; Die(Arrow) +0xaf3450
 * calls it with cause 0): "[vs] event death p<i> cause <c> killer <k> at (x,y)". Diagnostics for per-tower desyncs. */
void DesyncRemoteDeath(void* pl, int cause, int killer, bool brambled, bool laser);   /* below, with the input ring */
HOOK_DEFINE_TRAMPOLINE(PlayerDieHook) {
    static void* Callback(void* pl, int cause, int killer, bool brambled, bool laser) {
        if (pl) DesyncRemoteDeath(pl, cause, killer, brambled, laser);
        if (g_vsOn && pl) {
            char w[96];
            snprintf(w, sizeof w, "death p%d cause %d killer %d at (%.2f,%.2f)%s", *reinterpret_cast<int32_t*>(reinterpret_cast<uintptr_t>(pl) + fld::Player_PlayerIndex),
                     cause, killer, *reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(pl) + 0x14), *reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(pl) + 0x18), g_rbReal ? " (resim)" : "");
            VsEvent(w);
        }
        return Orig(pl, cause, killer, brambled, laser);
    }
};
/* TF.EX's SessionPatch (generated hooks, on with every TF.EX patch) on Session.EndRound / GotoNextRound / CreateResults; the Switch only
 * wraps them for the scripted versus test's events and the match log (S-tools). */
static void EndRoundShell(tfex::gen::hooks::Session_EndRound& c) {
    if (g_vsOn) VsEvent("Session.EndRound");
    tfex::frmodes::ScoreIndexScope s(c.self);   /* EndRound inlines GetScoreIndex (tfex/FrModes.cpp) */
    c();
}
/* Session.GotoNextRound: TF.EX skips it once a netplay match has a winner (links the round results to the match results, freezes the
 * level, plays the winner's victory music). */
static void GotoNextRoundShell(tfex::gen::hooks::Session_GotoNextRound& c) {
    c();
    if (!c.ran_) HWLOG("[vs] Session.GotoNextRound skipped at frame %d (TF.EX: the match has a winner)", CurFrame());
}
/* Session.CreateResults runs at every round end; the match's results (TF.EX: a VersusMatchResults is shown) are the ones made when the
 * session has a winner (Session.GetWinner +0xb453b0: the unique top score at or above the goal, else -1). */
static void CreateResultsShell(tfex::gen::hooks::Session_CreateResults& c) {
    if (g_vsOn) VsEvent("Session.CreateResults");
    c();
    if (!c.ran_) { HWLOG("[vs] Session.CreateResults at frame %d: the VersusMatchResults is shown again (TF.EX's SessionPatch)", CurFrame()); return; }
    if (!g_vsOn) return;
    int winner = reinterpret_cast<int (*)(void*)>(Base() + 0xb453b0)(c.self);
    if (winner >= 0 && g_resultsFrame < 0) { g_resultsFrame = CurFrame(); g_session = c.self; }
    HWLOG("[vs] Session.CreateResults at frame %d (tick %d): %s", CurFrame(), g_lvTick, winner >= 0 ? "match results" : "round results");
}

/* TFEX-DIFF S-tools: a scripted native match (tools/run-vs.sh: no TF.EX session) presses Confirm on every call, so the title card's and the
 * round results' "wait for a press" fall through: MenuInput.Confirm here, PlayerInput.MenuConfirm (vtable slot 0x38; the round results wait on
 * MenuInput.ConfirmOrStart = each input's MenuConfirm || MenuStart) in tfex/InputLock.cpp's gate, which asks ForcesMenuConfirm. In a TF.EX
 * session (a lobby match, the sync test) TF.EX's own XGamePadInputPatch.InterceptConfirm does it: true while netplay is initialised, the
 * real button while the level is paused. Forcing it there too (until 2026-10-28) made the game's pause menu take Confirm on RESUME the frame
 * it opened: after the browser quit mid-match, the Switch couldn't leave the level (console 2026-10-28). */
bool ForcesMenuConfirm() { return g_vsOn && !g_npOn; }
HOOK_DEFINE_TRAMPOLINE(MenuConfirmHook) {
    static bool Callback() { return ForcesMenuConfirm() ? true : Orig(); }
};

/* MatchSettings.GoalScore: TF.EX's test mode forces PlayerGoals to 2 (GoalScore = ceil(2 * 1.0)); a lobby match uses FortRise's value for
 * TF.EX's mode 11 (Instant 1, Quick 3, Standard 5 with 2 players). */
HOOK_DEFINE_TRAMPOLINE(GoalScoreHook) {
    static int Callback(void* self) { return (g_vsOn || g_vsPending) ? g_goal : Orig(self); }
};

HOOK_DEFINE_INLINE(InputStoreHook) {
    static void Callback(exl::hook::InlineCtx* ctx) {
        void* player = reinterpret_cast<void*>(ctx->X[19]);
        if (g_vsOn) {
            uintptr_t pl = reinterpret_cast<uintptr_t>(player);
            int seat = *reinterpret_cast<int32_t*>(pl + fld::Player_PlayerIndex);
            if (seat < 0 || seat > 3) return;
            g_seatPlayer[seat] = player;
            g_seatTick[seat] = g_lvTick;
            uintptr_t sm = *reinterpret_cast<uintptr_t*>(pl + 0x140);
            int state = sm ? *reinterpret_cast<int32_t*>(sm + 0x24) : 0;
            if (g_vsOrigin < 0 && state != 5) { g_vsOrigin = CurFrame(); HWLOG("[vs] origin: tick %d (seat %d state %d)", CurFrame(), seat, state); char ol[64]; snprintf(ol, sizeof ol, "[vs] origin: tick %d", CurFrame()); VsAppend(ol); }
            if (g_npOn) return;   /* the seat's GGRS input comes from GetState itself (TF.EX's postfix / FakeController) */
            if (g_vsOrigin < 0 || seat > 3) return;
            int T = g_lvTick - g_vsOrigin;
            if (g_shiftN && T >= g_shiftFrom) T -= g_shiftN;
            const VsStep* cur = VsAt(seat, T);
            if (!cur) return;
            const VsStep* prev = T > 0 ? VsAt(seat, T - 1) : nullptr;
            uint32_t f = (uint32_t)cur->flags, pf = prev ? (uint32_t)prev->flags : 0;
            auto held = [&](uint32_t m) { return (f & m) != 0; };
            auto pressed = [&](uint32_t m) { return (f & m) != 0 && (pf & m) == 0; };
            uint8_t b[28] = {};
            b[0] = held(1);  b[1] = pressed(1);
            b[2] = held(2);  b[3] = pressed(2);
            b[4] = held(8);  b[5] = pressed(8);
            b[6] = held(4);  b[7] = pressed(4);
            b[8] = held(0x10);
            int32_t mx = cur->mx, my = cur->my;
            float ax = (float)mx, ay = (float)my;
            if (mx != 0 && my != 0) { ax *= 0.70710677f; ay *= 0.70710677f; }
            std::memcpy(b + 12, &mx, 4);
            std::memcpy(b + 16, &my, 4);
            std::memcpy(b + 20, &ax, 4);
            std::memcpy(b + 24, &ay, 4);
            uint64_t q[3]; uint32_t w;
            std::memcpy(q, b, 24);
            std::memcpy(&w, b + 24, 4);
            ctx->X[8] = q[0]; ctx->X[9] = q[1]; ctx->X[10] = q[2]; ctx->W[11] = w;
            return;
        }
    }
};




/* ---- Hardware probe: update timing ---------------------------
 * Timing: the game's own update (TFGame.Update: the input update and Engine.Update, no drawing) is
 * timed on every frame in which player 1 is alive in a level (NormalUpdate ran), and the frame period
 * (update + drawing + waiting for the display: 16.7 ms is a steady 60 fps). Every kStatN such frames:
 * p50/p90/p99/max of both. */
constexpr int kStatN = 600;
static float g_updMs[kStatN], g_perMs[kStatN], g_sortBuf[kStatN];
static float g_drawMs[kStatN], g_idleMs[kStatN], g_tickMs[kStatN], g_rbMs[kStatN], g_ggrsMs[kStatN], g_resimMs[kStatN], g_resimN[kStatN], g_gateMs[kStatN];
static float g_saveMs[kStatN], g_ticksN[kStatN];   /* per drawn frame: rollback saves (TF.State GetState), game ticks */
static FrameCost g_fd;                             /* the drawn frame so far: the sum of its updates' FrameCosts */
static int g_fdTicks = 0; static double g_fdUpd = 0;
static double g_worstFrameMs = 0; static int g_worstFrameAt = -1, g_worstFrameTicks = 0;   /* the window's worst drawn frame (updates + draw) */
static int g_drawN = 0, g_idleN = 0, g_splitN = 0;
static double g_lastUpdMs = 0, g_lastDrawMs = 0;   /* the previous frame's, for its idle time */
static int g_updN = 0, g_perN = 0, g_inLevelFrames = 0;
static bool g_lastInLevel = false;
static uint64_t g_lastFrameStart = 0;
static double g_worstUpd = 0;

static void Pct(const float* v, int n, float out[4]) {
    if (n <= 0) { out[0] = out[1] = out[2] = out[3] = 0; return; }
    std::memcpy(g_sortBuf, v, (size_t)n * sizeof(float));
    std::sort(g_sortBuf, g_sortBuf + n);
    auto at = [&](double q) { int k = (int)(q * n); return g_sortBuf[k < n ? k : n - 1]; };
    out[0] = at(0.50); out[1] = at(0.90); out[2] = at(0.99); out[3] = g_sortBuf[n - 1];
}

static void ReportStats() {
    float u[4], p[4];
    Pct(g_updMs, g_updN, u);
    Pct(g_perMs, g_perN, p);
    HWLOG("[hw] timing, drawn frames %d..%d at %d Hz: update (one tick) ms p50 %.2f p90 %.2f p99 %.2f max %.2f (worst so far %.2f); frame period ms p50 %.2f p90 %.2f p99 %.2f max %.2f",
          g_inLevelFrames - g_splitN + 1, g_inLevelFrames, SessionHz(), u[0], u[1], u[2], u[3], g_worstUpd, p[0], p[1], p[2], p[3]);
    g_updN = g_perN = 0;
    if (g_splitN) {
        float d[4], id[4], t[4], r[4], g[4], rs[4], rn[4], gt[4], sv[4], tk[4];
        Pct(g_drawMs, g_drawN, d); Pct(g_idleMs, g_idleN, id); Pct(g_tickMs, g_splitN, t); Pct(g_rbMs, g_splitN, r); Pct(g_ggrsMs, g_splitN, g); Pct(g_gateMs, g_splitN, gt);
        Pct(g_saveMs, g_splitN, sv); Pct(g_ticksN, g_splitN, tk);
        double resimMs = 0, resimTicks = 0, ticks = 0; float rmax = 0;
        for (int i = 0; i < g_splitN; i++) { resimMs += g_resimMs[i]; resimTicks += g_resimN[i]; ticks += g_ticksN[i]; if (g_resimN[i] > rmax) rmax = g_resimN[i]; }
        Pct(g_resimMs, g_splitN, rs); Pct(g_resimN, g_splitN, rn);
        HWLOG("[hw] timing split, %d drawn frames at %d Hz, game ticks per drawn frame avg %.2f p50 %.0f max %.0f; ms per drawn frame p50/p90/p99/max: draw %.2f/%.2f/%.2f/%.2f | "
              "idle %.2f/%.2f/%.2f/%.2f (the headroom; its min on the next line) | level ticks %.2f/%.2f/%.2f/%.2f | rollback save %.2f/%.2f/%.2f/%.2f | "
              "rollback load %.2f/%.2f/%.2f/%.2f | ggrs %.2f/%.2f/%.2f/%.2f | resim %.2f/%.2f/%.2f/%.2f, %.2f ms per resimulated tick, resimulated ticks per drawn frame "
              "avg %.2f p99 %.0f max %.0f | netplay gate (ggrs + rollback + resim) %.2f/%.2f/%.2f/%.2f",
              g_splitN, SessionHz(), ticks / g_splitN, tk[0], tk[3], d[0], d[1], d[2], d[3], id[0], id[1], id[2], id[3], t[0], t[1], t[2], t[3],
              sv[0], sv[1], sv[2], sv[3], r[0], r[1], r[2], r[3], g[0], g[1], g[2], g[3],
              rs[0], rs[1], rs[2], rs[3], resimTicks > 0 ? resimMs / resimTicks : 0.0, resimTicks / g_splitN, rn[2], rmax, gt[0], gt[1], gt[2], gt[3]);
        float imin = 1e9f; for (int i = 0; i < g_idleN; i++) if (g_idleMs[i] < imin) imin = g_idleMs[i];
        if (g_idleN) HWLOG("[hw] timing idle: min %.2f ms of the frame period (the smallest headroom this window); worst drawn frame %.2f ms (%d ticks) at frame %d%s",
                           imin, g_worstFrameMs, g_worstFrameTicks, g_worstFrameAt, g_testSession ? "" : "");
        if (g_testSession) HWLOG("[test] sync-test mismatches so far: %d (frame %d)", g_testMismatches, NpFrame());
        g_drawN = g_idleN = g_splitN = 0;
        g_worstFrameMs = 0; g_worstFrameAt = -1; g_worstFrameTicks = 0;
    }
    if (rnd::DrawCount()) HWLOG("[hw] gameplay rng: %llu draws so far, state fold %016llx", (unsigned long long)rnd::DrawCount(), (unsigned long long)rnd::StateFold());
}


/* Native versus start (test 5): the rollcall has filled TFGame.Players/PlayerInputs/Characters and
 * MainMenu.CurrentMatchSettings (VersusLevelSystem for tower 0 at +0x28, Mode at +0x30). Make it
 * Last Man Standing and call MapScene.StartSession (+0xab54e0; it ignores `this`). */
void LogPlayersArray(const char* when) {
    HWLOG("[vs] Coroutine.OneFrameDelay %s: %d", when, *reinterpret_cast<uint8_t*>(*reinterpret_cast<uintptr_t*>(Base() + 0x4791c30)));
    uintptr_t f = *reinterpret_cast<uintptr_t*>(Base() + 0x4793890);
    uintptr_t a = f ? *reinterpret_cast<uintptr_t*>(f) : 0;
    uint8_t* pl = a ? *reinterpret_cast<uint8_t**>(a + 0x10) : nullptr;
    if (pl) HWLOG("[vs] TFGame.Players %s: %d%d%d%d", when, pl[0], pl[1], pl[2], pl[3]);
}
static int g_vsPlayersWatch = 0;
static uint32_t g_vsInputsLast = 0xffffffffu;
static uint32_t g_vsPlayersLast = 0xffffffffu;

/* Game.Draw's time per in-level frame (the timing split). The body is found from the game object's vtable. */
static bool g_drawHooked = false;
/* One drawn frame of a Level (Draw's hook): the game updates since the last one (FrameShell's g_fd: hz/60 at a session's rate above 60), their
 * costs, the draw and the idle rest of the frame period, into the timing window; the frames that hitch logged (bounded) */
static void DrawnFrame(uint64_t t0, double drawMs) {
    if (!g_fdTicks) { g_lastFrameStart = t0; return; }
    const double work = g_fdUpd + drawMs;
    if (g_lastFrameStart && g_lastInLevel) {
        const double per = Ms(t0 - g_lastFrameStart);
        if (g_perN < kStatN) g_perMs[g_perN++] = (float)per;
        if (g_idleN < kStatN) g_idleMs[g_idleN++] = (float)(per - work);   /* (frames over 250 ms count too: they are the stalls) */
        static int spikes = 0;   /* the frames that hitch, with what took the time (online: the console's lag spikes) */
        if (g_npOn && (per > 25 || work > 20) && spikes < 400) {
            spikes++;
            HWLOG("[spike] lvTick %d ggrs %d confirmed %d: frame period %.1f ms, %d ticks %.1f ms (rollback save %.1f, load %.1f, %d resimulated %.1f), draw %.1f ms, wait skip %d",
                  g_lvTick, g_npSession ? NpFrame() : -1, g_npSession ? NpConfirmed() : -1, per, g_fdTicks, g_fdUpd, g_fd.save, g_fd.rb, g_fd.resimN, g_fd.resim, drawMs, g_npWaitSkip);
        }
    }
    if (work > g_worstFrameMs) { g_worstFrameMs = work; g_worstFrameAt = g_npSession ? NpFrame() : g_lvTick; g_worstFrameTicks = g_fdTicks; }
    if (g_splitN < kStatN) {
        int i = g_splitN++;
        g_tickMs[i] = (float)g_fd.tick; g_rbMs[i] = (float)g_fd.rb; g_ggrsMs[i] = (float)g_fd.ggrs; g_resimMs[i] = (float)g_fd.resim; g_resimN[i] = (float)g_fd.resimN;
        g_gateMs[i] = (float)g_fd.gate; g_saveMs[i] = (float)g_fd.save; g_ticksN[i] = (float)g_fdTicks;
        if (g_drawN < kStatN) g_drawMs[g_drawN++] = (float)drawMs;
    }
    g_inLevelFrames++;
    g_fd = FrameCost{}; g_fdTicks = 0; g_fdUpd = 0;
    g_lastFrameStart = t0;
    if (g_splitN >= kStatN) ReportStats();
}
/* [perf]: a local match's cost (a Level outside a netplay session; the user's 8-player Teams+ match with 7 AIs slowed down, 2026-11-07,
 * and the timing split above counts only while player 1 is alive). Per window of 600 drawn frames: real time, the game updates run and
 * owed (FixedStepUpdates: TF.EX's 240 Hz step, at most 8 a frame, the rest dropped: the game then runs slower than real time), and the
 * frame's time by part: TFGame.Update, of it Level.Update, of that its prefixes (LoaderAI's and AIJimmy's AgentUpdate: every AI's
 * decision each update) and the team outlines, then Draw. The first 6 windows of a level, then every 10th. */
static int LpPlayers() {
    uintptr_t f = *reinterpret_cast<uintptr_t*>(Base() + 0x4793890);   /* TFGame.Players */
    uintptr_t a = f ? *reinterpret_cast<uintptr_t*>(f) : 0;
    if (!a) return 0;
    int n = **reinterpret_cast<int32_t**>(a + 0x20), c = 0;
    uint8_t* pl = *reinterpret_cast<uint8_t**>(a + 0x10);
    for (int i = 0; i < n && i < 8; i++) c += pl[i] ? 1 : 0;
    return c;
}
static void LpFrame(void* level, double drawMs) {
    const uint64_t now = svcGetSystemTick();
    if (level != g_lpLevel) { g_lpLevel = level; g_lpWindows = 0; g_lp = LocalPerf{}; }
    if (!g_lp.t0) { g_lp.t0 = now; g_lp.last = now; return; }
    const double per = Ms(now - g_lp.last); g_lp.last = now;
    if (per > g_lp.worst) g_lp.worst = per;
    g_lp.draw += drawMs;
    if (++g_lp.frames < 600) return;
    const double wall = Ms(now - g_lp.t0) / 1000.0, f = g_lp.frames;
    const int64_t step = g_game ? *reinterpret_cast<int64_t*>(reinterpret_cast<uintptr_t>(g_game) + 0x58) : 0;
    const double gameSecs = step > 0 ? g_lp.updates * (double)step / 1e7 : 0;
    g_lpWindows++;
    if (g_lpWindows <= 6 || g_lpWindows % 10 == 0)
        HWLOG("[perf] local match, %d players, window %d: %d drawn frames in %.1f s (%.1f fps); game updates %d (%.2f a frame; owed %d, dropped %d): the game ran at %.0f%% of real time | ms a frame: TFGame.Update %.2f, of it Level.Update %.2f (its prefixes, the AIs' decisions: %.2f; team outlines %.2f, %.2f draws a frame), draw %.2f; worst frame period %.1f ms",
              LpPlayers(), g_lpWindows, g_lp.frames, wall, f / wall, g_lp.updates, g_lp.updates / f, g_lp.owed, g_lp.dropped, wall > 0 ? 100.0 * gameSecs / wall : 0.0,
              g_lp.upd / f, g_lp.lv / f, g_lp.pre / f, g_lp.outline / f, g_lp.outlines / f, g_lp.draw / f, g_lp.worst);
    hangwatch::ProfileReport(g_lpWindows <= 6 || g_lpWindows % 10 == 0);   /* the window's most sampled stacks (reset either way) */
    g_lp = LocalPerf{}; g_lp.t0 = now; g_lp.last = now;
}
/* TFEX-DIFF S-native: the Switch game's own TeamOutlinesLayer (a team match's layer -1, made by the Switch's Level ctor when
 * MatchSettings.TeamMode; the PC has none) draws each team's players into its two outline render targets from the last call of the
 * Switch's Level.Update (+0xb68c40). Drawing only (render targets, no game state): once a frame at the game's own 60 Hz, but at TF.EX's
 * 240 Hz four times a frame plus once per resimulated tick, ~1.5 ms a tick of a 4-player team match, past 240 Hz's 4.17 ms budget (STATUS:
 * the road to eight players). It runs once per drawn frame, just before Draw, as at 60 Hz: in a session, and since 2026-11-07 in a local
 * match too (TF.EX's 240 Hz step there as well: 4 updates a frame, 8 players' outlines each time). */
static bool g_outlinesFromDraw = false;
static int g_outlinesSkipped = 0, g_outlinesDrawn = 0;
HOOK_DEFINE_TRAMPOLINE(TeamOutlinesHook) {
    static void Callback(void* layer) {
        if (!g_outlinesFromDraw) { g_outlinesSkipped++; return; }
        const uint64_t t0 = svcGetSystemTick();
        Orig(layer);
        if (!g_npSession) { g_lp.outline += Ms(svcGetSystemTick() - t0); g_lp.outlines++; }
    }
};
static void TeamOutlinesBeforeDraw(void* game) {
    uintptr_t sc = *reinterpret_cast<uintptr_t*>(reinterpret_cast<uintptr_t>(game) + 0xe8);   /* Engine.scene */
    if (!sc) return;
    char tn[32] = ""; rnd::TypeName(reinterpret_cast<void*>(sc), tn, sizeof tn);
    void* layer = std::strcmp(tn, "Level") ? nullptr : *reinterpret_cast<void**>(sc + 0xc8);   /* Level's TeamOutlinesLayer (team mode only) */
    if (!layer) return;
    /* TFEX-DIFF S-native: none in a Teams+ match. The PC has no outline layer, and Teams+ marks its groups with recoloured sheets only
     * (vendor/teamplus STATUS.md "Marking a group": outlines dropped by request). The Switch's layer knows only its two native teams: in
     * the user's 8-player Teams+ match (7 lone wolves, the user yellow) it drew every archer blue, and it cost the most of any part of a
     * frame in the emulator (12 ms of ~16, 2026-11-07) */
    if (tfex::gen::Tp_Teams3::get_Live()) {
        static int said = 0;
        if (!said++) HWLOG("[np] a Teams+ match: the Switch's team outlines not drawn (Teams+ marks groups by their colours, as on the PC)");
        return;
    }
    if (!g_outlinesDrawn++) HWLOG("[np] the team outlines (the Switch's TeamOutlinesLayer) drawn once per drawn frame, not in each update's Level.Update (online and local)");
    g_outlinesFromDraw = true;
    TeamOutlinesHook::Callback(layer);
    g_outlinesFromDraw = false;
}
HOOK_DEFINE_TRAMPOLINE(DrawSkipHook) {
    static void Callback(void* game, void* gameTime) {
        TeamOutlinesBeforeDraw(game);
        uint64_t t0 = svcGetSystemTick();
        Orig(game, gameTime);
        g_lastDrawMs = Ms(svcGetSystemTick() - t0);
        DrawnFrame(t0, g_lastDrawMs);
        if (g_lpLocal) LpFrame(*reinterpret_cast<void**>(reinterpret_cast<uintptr_t>(game) + 0xe8), g_lastDrawMs);
    }
};

/* This peer's scripted input for GGRS frame F (= netplay_current_frame before the advance): the script step at T = F - 266, with
 * `pressed` derived from T and T-1 (TEST5-MATCH-SETUP.md 9.2); neutral before tick 0. */
/* The real controller: PlayerInputs[seat].GetState() (vtable slot 0x20, InputState returned through x8: 9 bools, MoveX, MoveY,
 * AimAxis), like TF.EX's polled input. */
struct InputStateRaw { uint8_t b[12]; int32_t mx, my; float ax, ay; };
/* TEST ONLY (hw.txt script, nppad 0): before TF.EX's NetplayLogic and after each of its ResetPolledInput ([after] rule), each local seat's
 * scripted input for the frame GGRS advances next, into TF.EX's GameContext polled input (as the controller's GetState postfix would) */
/* the frame the polled input goes to next: NpFrame() before TF.EX's NetplayLogic each update (ScriptFeedFrame), one more after each of its
 * ResetPolledInput (an update with two advances: the second reads the next frame's script, not the first's again) */
static int g_scriptFrame = 0;
void ScriptFeedSeat(int seat) {   /* (also after each UpdatePolledInput: the idle controller's GetState postfix doesn't replace the script) */
    if (g_npPad || !g_lobbyStart || !g_npSession || seat < 0 || seat >= kNpSeats || !tfex::gen::svc::IsLocalInputIndex(seat)) return;
    void* gc = tfex::gen::svc::GameContextObj();
    if (!gc) return;
    if (tfex::aislot::AiSeatOn()) {   /* TEST ONLY (hw.txt aiseat): LoaderAI's AI instead of the script, through TF.EX's UpdatePolledInput */
        static bool inside = false;   /* (TF.EX's UpdatePolledInput ends in this function: [after] rule) */
        tfex::gen::GameVal<28> st;
        if (inside || g_resultsFrame >= 0 || !tfex::aislot::AiSeatState(seat, g_scriptFrame, &st)) return;
        inside = true;
        tfex::gen::InputService::UpdatePolledInput(tfex::gen::svc::Service(), seat, st, tfex::gen::st::EX_RightStick{});
        inside = false;
        return;
    }
    TfxInput li = NpLocalInput(seat, g_scriptFrame);
    tfex::gen::st::Input in; static_assert(sizeof in == sizeof li, "TfxInput is TF.EX's Input"); std::memcpy(&in, &li, sizeof in);
    tfex::gen::GameContext::UpdatePolledInput(gc, seat, in);
}
static void FeedAll() { for (int seat = 0; seat < kNpSeats; seat++) ScriptFeedSeat(seat); }
void ScriptFeedFrame() { g_scriptFrame = NpFrame(); FeedAll(); }   /* before TF.EX's NetplayLogic (FrameShell) */
void ScriptFeed() { g_scriptFrame++; FeedAll(); }                  /* [after] ResetPolledInput: the next advance's frame */
/* TEST ONLY (hw.txt script, nppad 0): a scripted seat's input for frame F. The controller's own input is TF.EX's: its GetState postfix
 * fills GameContext's polled input, which its NetplayLogic sends and clears (C2 step 2). */
TfxInput NpLocalInput(int seat, int F) {
    TfxInput o{};
    /* the test script plays the match only: from the match results on its seat is idle, as a player who has stopped (the script runs
     * ~2,500 ticks past a short match's end; under lag TF.EX's results input gate never goes neutral and those presses held the results) */
    if (g_resultsFrame >= 0) {
        static int logged = 0;
        if (logged < 2) { logged++; HWLOG("[vs] the test script's seat %d is idle from the match results (frame %d)", seat, g_resultsFrame); }
        return o;
    }
    int T = F + g_npDelay + 1 - 266;   /* added at frame index F (0-based), applied by the tick of 1-based frame F + delay + 1 = script tick T */
    const VsStep* cur = T >= 0 ? VsAt(seat, T) : nullptr;
    if (!cur) return o;
    const VsStep* prev = T > 0 ? VsAt(seat, T - 1) : nullptr;
    uint32_t f = (uint32_t)cur->flags, pf = prev ? (uint32_t)prev->flags : 0;
    auto held = [&](uint32_t m) { return (int)((f & m) != 0); };
    auto pressed = [&](uint32_t m) { return (int)((f & m) != 0 && (pf & m) == 0); };
    o.jc = held(1); o.jp = pressed(1); o.sc = held(2); o.sp = pressed(2);
    o.ac = held(8); o.ap = pressed(8); o.dc = held(4); o.dp = pressed(4); o.arrow = held(0x10);
    o.mx = cur->mx; o.my = cur->my;
    float ax = (float)o.mx, ay = (float)o.my;
    if (o.mx != 0 && o.my != 0) { ax *= 0.70710677f; ay *= 0.70710677f; }
    o.ax = ax; o.ay = ay;
    return o;
}


/* ---- Rollback glue: the frame loop's own bookkeeping per frame (not game state: TF.State's state has the game's, its RNG included) ---- */
void SideSave(SideAll* s) {
    for (int i = 0; i < kNpSeats; i++) { s->seatPlayer[i] = g_seatPlayer[i]; s->seatTick[i] = g_seatTick[i]; }
    s->vsOrigin = g_vsOrigin; s->lvTick = g_lvTick;
}
void SideLoad(const SideAll* s) {
    for (int i = 0; i < kNpSeats; i++) { g_seatPlayer[i] = s->seatPlayer[i]; g_seatTick[i] = s->seatTick[i]; }
    g_vsOrigin = s->vsOrigin; g_lvTick = s->lvTick;
}
int g_stateFrame = -1;          /* the GGRS frame whose start the game state is */

/* TF.EX's HUD.Update postfix: once VersusMatchResults is finished (TweenIn's 120-frame alarm sets +0xa1), NetplayManager.Reset, then
 * NotifyMatchEnded(winner seat, seat scores) and the end-of-match vote (the vanilla VersusMatchEnd pause menu, tfex/MainMenuPatch.cpp).
 * The alarm runs inside the simulated frame: the reset waits for the next frame's gate. */
volatile bool g_resultsDone = false;
HOOK_DEFINE_TRAMPOLINE(ResultsFinishedHook) {   /* VersusMatchResults.<TweenIn>b__12_1: focused = finished = true; tweening = false */
    static void Callback(void* self) {
        Orig(self);
        if ((g_lobbyStart || g_testSession) && g_npOn && !g_resultsDone && !g_resim) { g_resultsDone = true; HWLOG("[vs] match results finished at frame %d: the session ends (TF.EX NetplayManager.Reset)", g_npFrame); }
    }
};

int SessionHz() { void* nm = tfex::gen::svc::GenNM(); return nm ? tfex::gen::NetplayManager::GetSessionFps(nm) : 240; }   /* TF.EX's GetSessionFps (GGRSConfig.Fps) */
/* TFEX-DIFF: the session's time step: what Game.TargetElapsedTime holds in TF.EX's session on the PC. At 240 Hz (TF.EX's NETPLAY_FPS)
 * ManageTimeStep leaves FortRise's EnableFixedTimeStep(true) value, TimeSpan.FromSeconds(1.0f / 240f) = 41666 ticks (truncated; 41667
 * desynced a 240 Hz lobby, 2026-10-28); at another rate it sets round(1e7 / hz) (166667 at 60). The Switch game's own TargetElapsedTime is
 * 170000 (17 ms): read at 60 Hz it made every tick's TimeMult 1.02 (a lobby match desynced from tick 1) and the netplay gate step at 58.8 Hz. */
int64_t SessionStepTicks() { return SessionHz() == 240 ? 41666 : (10000000 + SessionHz() / 2) / SessionHz(); }
/* TFEX-DIFF S-native: FortRise's EnableFixedTimeStep(true) (TF.EX's ManageTimeStep) sets Game.TargetElapsedTime to 240 Hz's step and TF.EX's
 * RestoreTimeStep only restores the fixed-step flag: on the PC the game's own step is FNA's 1/60 s anyway. The Switch game's own is 170000 (17 ms): put back on the main menu. Game: TargetElapsedTime +0x58 (TimeSpan ticks). */
static int64_t g_gameStep = 0;
static void GameStepRestore(void* game) {
    int64_t& target = *reinterpret_cast<int64_t*>(reinterpret_cast<uintptr_t>(game) + 0x58);
    if (!g_gameStep) g_gameStep = target;
    if (target == g_gameStep) return;
    uintptr_t sc = *reinterpret_cast<uintptr_t*>(reinterpret_cast<uintptr_t>(game) + 0xe8);
    char tn[32] = ""; if (sc) rnd::TypeName(reinterpret_cast<void*>(sc), tn, sizeof tn);
    if (std::strcmp(tn, "MainMenu")) return;
    HWLOG("[np] back to the game's own step on the main menu (TargetElapsedTime %lld -> %lld ticks)", (long long)target, (long long)g_gameStep);
    target = g_gameStep;
}
/* TFEX-DIFF S-native: FNA's fixed time step for a Level outside a session. TF.EX keeps a Level at the session's step (240 Hz: FortRise's
 * EnableFixedTimeStep(true), TF.EX's ManageTimeStep) after the match too, and on the PC FNA's Game.Tick runs as many updates as real time
 * needs. The Switch game's loop runs one update per present (TF.EX's accumulator does the catching up only while its session runs), so the
 * finished match's level (results, awards, the end-of-match menu) ran at a quarter speed or less: TimeMult 0.25 at ~20 updates a second,
 * the awards outlasted the server's 30 s vote window (smoke rematch, 2026-10-30). The updates this frame owes, as FNA counts them: real
 * time since the last frame in steps of TargetElapsedTime, at most 8 (FNA's catch-up bound; the rest is dropped). 1 otherwise. */
static int FixedStepUpdates(void* game) {
    static uint64_t last = 0; static int64_t acc = 0; static int logged = 0;
    const uint64_t now = svcGetSystemTick();
    const int64_t step = *reinterpret_cast<int64_t*>(reinterpret_cast<uintptr_t>(game) + 0x58);   /* Game.TargetElapsedTime (ticks) */
    uintptr_t sc = *reinterpret_cast<uintptr_t*>(reinterpret_cast<uintptr_t>(game) + 0xe8);
    char tn[32] = ""; if (sc) rnd::TypeName(reinterpret_cast<void*>(sc), tn, sizeof tn);
    const bool owed = !g_npSession && g_gameStep && step > 0 && step < g_gameStep && !std::strcmp(tn, "Level");
    const uint64_t prev = last; last = now;
    if (!owed || !prev) { acc = 0; return 1; }
    acc += (int64_t)((now - prev) * 10000000 / 19200000);   /* (19.2 MHz system ticks to TimeSpan ticks) */
    int n = (int)(acc / step);
    g_lp.owed += n;
    if (n > 8) { g_lp.dropped += n - 8; n = 8; acc = 0; } else acc -= (int64_t)n * step;
    if (logged < 3 && n != 1) { logged++; HWLOG("[np] a Level outside a session at TF.EX's step (%lld ticks): %d updates this frame (FNA's fixed time step)", (long long)step, n); }
    return n;
}
/* TFGame.Update: TF.EX's prefix (TFGamePatch.TFGameUpdate_Patch, generated: DrainGameThreadActions, ManageTimeStep, the failed-connection
 * and disconnected branches, the unsynchronized update, Poll, the accumulator at the session's rate, NetplayLogic, the frame's own advance and
 * the original update) with the Switch's own steps around it: the ggrs_ffi pump (TFEX-DIFF: TF.EX's PC threads), the late hook installs, the
 * lobby screen and match start / end, the dev-update notice, the frame cost statistics. */
static void FrameShell(tfex::gen::hooks::TFGame_Update& c) {
    hangwatch::Beat();
    {
        void* game = c.self;
        g_game = game;
        g_fc = FrameCost{};   /* (at the start: the netplay gate below adds its GGRS, rollback and resimulation time) */
        if (!g_drawHooked) {   /* (always: Draw's time) */
            g_drawHooked = true;
            uintptr_t body = rnd::VtableBody(game, 0x68);
            HWLOG("[vs] game Draw body +%#lx", (unsigned long)(body - Base()));
            if (body > Base() && body < Base() + 0x2863000) DrawSkipHook::InstallAtOffset(body - Base());
        }
        if (!g_scriptLoaded) {   /* sd:/xplay/hw.txt and the script, also without the Pro Controller input hook (Joy-Con never call it) */
            static int frames = 0;
            if (++frames == 120 && hw::MountSd()) { LoadScript(); HWLOG("[xplay] settings loaded from the frame hook: vstart %d np %d", (int)g_vstart, (int)g_npOn); }
        }
        tfex::EndOfMatch::MakeNetplayManager();   /* TF.EX's NetplayManager before TF.EX's code first runs (once; at boot, outside any rollback) */
        ggrsffi::Frame();                          /* TF.EX's ggrs_ffi pump (TFEX-DIFF: its PC threads, ggrsffi.cpp) */
        tfex::MainMenuPatch::InstallLate();
        { void tfex_InputLockInstallLate(); tfex_InputLockInstallLate(); }   /* TF.EX's input lock gates on JoyConInput's menu getters (tfex/InputLock.cpp) */
        tfex_MatchPauseFrame();
        { static int frames = 0; if (++frames == 900) pools::Report("15 s after boot (late installs done)"); }
        { static int f = 0; if (++f % 60 == 0) hw::SetLive(g_lobbyStart || !tfex::gen::Auto_Lobby::get_IsEmpty(tfex::gen::svc::GetOwnLobby())); }   /* devupdate's periodic log upload (lobby / match) */
        tfex::MainMenuPatch::Update();             /* the lobby screen, the lobby's start and end of match */
        devupdate::Frame();                        /* the dev-update notice on the main menu */
        if (g_freezeLevel) {   /* frozen until the scene is no longer the finished match's Level */
            uintptr_t sc = *reinterpret_cast<uintptr_t*>(reinterpret_cast<uintptr_t>(game) + 0xe8);
            char tn[32] = ""; if (sc) rnd::TypeName(reinterpret_cast<void*>(sc), tn, sizeof tn);
            if (std::strcmp(tn, "Level")) { g_freezeLevel = false; HWLOG("[vs] left the finished match's Level (scene %s)", tn); }
        }
        ClearFilledSlots();
        MaybeNativeStart();
        GameStepRestore(game);
        if (MatchEndFrame()) return;   /* the finished match's level, frozen until left */
        BeforeTfexUpdate();
        if (g_vsStarted && g_vsPlayersWatch < 20) {   /* who turns seats 2 and 3 on? */
            uintptr_t f = *reinterpret_cast<uintptr_t*>(Base() + 0x4793890);
            uintptr_t a = f ? *reinterpret_cast<uintptr_t*>(f) : 0;
            uint8_t* pl = a ? *reinterpret_cast<uint8_t**>(a + 0x10) : nullptr;
            uint32_t now = pl ? (pl[0] | pl[1] << 8 | pl[2] << 16 | (uint32_t)pl[3] << 24) : 0;
            uintptr_t fi = *reinterpret_cast<uintptr_t*>(Base() + 0x4791fa8);
            uintptr_t ai = fi ? *reinterpret_cast<uintptr_t*>(fi) : 0;
            uintptr_t* di = ai ? *reinterpret_cast<uintptr_t**>(ai + 0x10) : nullptr;
            uint32_t inmask = 0;
            for (int i = 0; di && i < 6; i++) inmask |= (di[i] ? 1u : 0u) << i;
            if (inmask != g_vsInputsLast) { HWLOG("[vs] PlayerInputs non-null mask now %02x (Level.Update tick %d)", inmask, g_lvTick); g_vsInputsLast = inmask; }
            if (now != g_vsPlayersLast) {
                char tn[64] = "?";
                uintptr_t scene = *reinterpret_cast<uintptr_t*>(reinterpret_cast<uintptr_t>(game) + 0xe8);
                if (scene) rnd::TypeName(reinterpret_cast<void*>(scene), tn, sizeof tn);
                HWLOG("[vs] TFGame.Players now %d%d%d%d (scene %s, Level.Update tick %d)", pl[0], pl[1], pl[2], pl[3], tn, g_lvTick);
                g_vsPlayersLast = now; g_vsPlayersWatch++;
            }
        }
        int nu0 = g_nuCount;
        uint64_t u0 = svcGetSystemTick();
        const bool test = g_npSession && tfex::gen::NetplayManager::IsTestMode(g_npSession);
        const int own0 = g_matchFirstTicks, resim0 = g_rbResimTicks;
        /* TF.EX's prefix: the session's ticks of this update (or the plain update outside a session). TFEX-DIFF S-native: until TF.EX's services
         * exist (made on the first menu frame: the game's GC isn't up at boot, the 171f0de crash; the PC's DI makes them at load) the game's
         * own update runs */
        tfex::MatchmakingService::Instance().PumpTransport();   /* S-net: the server's messages into TF.EX's HandleMessageContents (whose
                                                                 * game-thread actions TF.EX's prefix drains next, as on the PC); it makes
                                                                 * TF.EX's MatchmakingService on the first menu frame */
        ScriptFeedFrame();   /* tests: the scripted seats' polled input for this update's first advance */
        {
            uintptr_t sc = *reinterpret_cast<uintptr_t*>(reinterpret_cast<uintptr_t>(game) + 0xe8);
            char tn[16] = ""; if (sc && !g_npSession) rnd::TypeName(reinterpret_cast<void*>(sc), tn, sizeof tn);
            g_lpLocal = !g_npSession && !std::strcmp(tn, "Level");
        }
        hangwatch::SetProfile(g_lpLocal);   /* [perf]: a local match's updates sampled (hangwatch.cpp) */
        if (g_npSession || g_lpLocal) hangwatch::UpdateBegin();   /* a match's TFGame.Update past 100 ms: sampled and logged (hangwatch.cpp) */
        const uint64_t lpU0 = svcGetSystemTick();
        if (tfex::gen::svc::GenNM() && tfex::gen::svc::GenMM()) { for (int k = FixedStepUpdates(game); k > 0; k--) c(); }
        else c.Orig();
        if (g_lpLocal) g_lp.upd += Ms(svcGetSystemTick() - lpU0);
        hangwatch::UpdateEnd();
        if (g_npSession) {   /* TF.EX's accumulator: the frame's own advances and resimulations this TFGame.Update ran, every 600th call */
            static int calls = 0; static int own = 0, resims = 0; static double ms = 0;
            const int dOwn = g_matchFirstTicks - own0;   /* (negative across a match start, which resets the count: the new match's ticks) */
            calls++; own += dOwn < 0 ? g_matchFirstTicks : dOwn; resims += g_rbResimTicks - resim0; ms += Ms(svcGetSystemTick() - u0);
            if (calls % 600 == 0) { HWLOG("[np] TF.EX's TFGame.Update over the last 600 calls: %.2f own advances and %.2f resimulations per call, %.2f ms per call (session %d Hz)", own / 600.0, resims / 600.0, ms / 600, SessionHz()); own = resims = 0; ms = 0; }
            /* the same per simulated tick, every 2400 of the session's own ticks (10 s at 240 Hz): the per-tick cost, which a hidden Ryujinx window
             * (few updates, each catching up many ticks) never reached by calls; the players count for the cost by players (STATUS, 8 players) */
            static int tOwn = 0, tResim = 0; static double tMs = 0;
            tOwn += dOwn < 0 ? g_matchFirstTicks : dOwn; tResim += g_rbResimTicks - resim0; tMs += Ms(svcGetSystemTick() - u0);
            if (tOwn >= 2400) {
                HWLOG("[np] per tick over the last %d own ticks (+%d resimulated): %.3f ms (TFGame.Update's time / all ticks), %d players, session %d Hz",
                      tOwn, tResim, tMs / (tOwn + tResim), g_npN, SessionHz());
                if (g_outlinesSkipped) HWLOG("[np] team outlines: %d of Level.Update's calls skipped, %d drawn before Draw (session so far)", g_outlinesSkipped, g_outlinesDrawn);
                tOwn = tResim = 0; tMs = 0;
            }
        }
        {   /* after a lobby match's results (the level runs on with the end-of-match menu): the state TF.EX's prefix branched on, every 20th
             * frame of the first 300 (bounded per match) */
            static int afterFor = -1, after = 0;
            if (g_vsLogDone && !g_npSession && afterFor != g_lobbyMatchNo) { afterFor = g_lobbyMatchNo; after = 0; }
            if (afterFor == g_lobbyMatchNo && after < 300) {
                if (after % 20 == 0) {
                    namespace NM = tfex::gen::NetplayManager;
                    void* nm = tfex::gen::svc::GenNM();
                    char tn[32] = ""; uintptr_t sc = *reinterpret_cast<uintptr_t*>(reinterpret_cast<uintptr_t>(game) + 0xe8); if (sc) rnd::TypeName(reinterpret_cast<void*>(sc), tn, sizeof tn);
                    HWLOG("[np] after the match (frame +%d): scene %s, init %d synchronized %d disconnected %d mode %d unsync-update %d, freeze %d, Level updates %d",
                          after, tn, nm ? (int)NM::get__isInit(nm) : -1, nm ? (int)NM::IsSynchronized(nm) : -1, nm ? (int)NM::IsDisconnected(nm) : -1,
                          (nm ? (int)tfex::gen::NetplayManager::GetNetplayMode(nm) : -1), (int)tfex::gen::NetplayEntry::IsRunningUnsynchronizedUpdate, (int)g_freezeLevel, g_nuCount);
                }
                after++;
            }
        }
        if (g_npSession) {   /* the state TF.EX's prefix branched on, the first frames of each session (bounded) */
            static int logged = 0, forMatch = -1;
            if (forMatch != g_lobbyMatchNo) { forMatch = g_lobbyMatchNo; logged = 0; }
            namespace NM = tfex::gen::NetplayManager;
            void* nm = g_npSession;
            if (logged < 12 || (logged < 40 && logged % 4 == 0)) {
                char tn[32] = ""; uintptr_t sc = *reinterpret_cast<uintptr_t*>(reinterpret_cast<uintptr_t>(game) + 0xe8); if (sc) rnd::TypeName(reinterpret_cast<void*>(sc), tn, sizeof tn);
                HWLOG("[np] TF.EX's TFGame.Update: scene %s, init %d synchronized %d mode %d disconnected %d failed %d unsync-update %d, requests %d, GGRS frame %d",
                      tn, (int)NM::get__isInit(nm), (int)NM::IsSynchronized(nm), (nm ? (int)tfex::gen::NetplayManager::GetNetplayMode(nm) : -1), (int)NM::IsDisconnected(nm),
                      (int)NM::HasFailedInitialConnection(nm), (int)tfex::gen::NetplayEntry::IsRunningUnsynchronizedUpdate,
                      NM::S(nm)->_netplayRequests ? NM::S(nm)->_netplayRequests->Count() : -1, NpFrame());
            }
            logged++;
        }
        uint64_t u1 = svcGetSystemTick();
        if (test && !tfex::gen::NetplayManager::IsTestMode(g_npSession)) {   /* TF.EX's AdvanceFrame on a sync-test checksum mismatch: logs it,
                                                                                * Reset, ResetMode (the test ends) */
            g_testMismatches++;
            HWLOG("[test] sync-test mismatch: TF.EX's AdvanceFrame reset the test session at frame %d", NpFrame());
        }

        bool inLevel = g_nuCount != nu0;
        if (inLevel) {   /* one game update (hz/60 of them per drawn frame at a session's rate above 60): its costs go into the drawn frame's */
            double upd = Ms(u1 - u0);
            if (upd > g_worstUpd) g_worstUpd = upd;
            if (g_updN < kStatN) g_updMs[g_updN++] = (float)upd;
            g_fd.tick += g_fc.tick; g_fd.rb += g_fc.rb; g_fd.ggrs += g_fc.ggrs; g_fd.resim += g_fc.resim; g_fd.gate += g_fc.gate; g_fd.save += g_fc.save;
            g_fd.resimN += g_fc.resimN; g_fdTicks++; g_fdUpd += upd;
        }
        g_lastInLevel = inLevel;
    }
}

} // namespace xplay

void xplay_main() {
    Logging.Log("[xplay] cross-play probe injected");
    xplay::ApplyFixes();
    xplay::g_getFullKey = reinterpret_cast<xplay::GetFullKeyFn>(
        xplay::FindExport("_ZN2nn3hid12GetNpadStateEPNS0_16NpadFullKeyStateERKj"));
    if (xplay::g_getFullKey) {
        *reinterpret_cast<volatile uintptr_t*>(xplay::Base() + xplay::off::NpadFullKeyStateSlot) =
            reinterpret_cast<uintptr_t>(&xplay::GetFullKeyHook);
    }
    rightstick::Install(&xplay::FindExport);   /* the other npad state imports: the right stick for TF.EX's GetRightStick */
    hangwatch::Install(&xplay::FindExport);   /* a frozen game's main thread, logged (hangwatch.cpp) */
    xplay::NormalUpdateHook::InstallAtOffset(xplay::off::Player_NormalUpdate);
    if (*reinterpret_cast<uint32_t*>(xplay::Base() + xplay::off::TFGame_Update) == xplay::off::kTFGameUpdateInsn) {
        tfex::gen::hooks::TFGame_Update::Wrap(xplay::FrameShell);   /* TF.EX's TFGame.Update prefix with the Switch's steps around it */
        xplay::ResultsFinishedHook::InstallAtOffset(0xbb0b60);
    }
    else
        Logging.Log("[xplay] TFGame.Update isn't as expected: the frame hook is off");
    if (*reinterpret_cast<uint32_t*>(xplay::Base() + xplay::off::Player_Update_InputStore) == xplay::off::kInputStoreInsn)
        xplay::InputStoreHook::InstallAtOffset(xplay::off::Player_Update_InputStore);
    else
        Logging.Log("[xplay] Player.Update isn't as expected: InputState hook NOT installed");
    xplay::MenuConfirmHook::InstallAtOffset(0xac6a00);
    xplay::GoalScoreHook::InstallAtOffset(0xabae50);
    rumble::Install();
    tfex::MainMenuPatch::Install();
    tfex::NetplayOptions::Install();   /* TF.EX's netplay settings on the OPTIONS screen (tfex/NetplayOptions.cpp) */
    tfex::ModsMenu::Install();   /* FortRise's MODS menu: its OPTIONS filter (tfex/ModsMenu.cpp) */
    tfex::MapScenePatch::Install();
    tfex::gen::Install();
    tfex::gen::hooks::Level_Update::Wrap(xplay::LevelUpdateShell);               /* the Switch's tooling around TF.EX's LevelPatch */
    tfex::gen::hooks::Level_HandlePausing::Wrap(xplay::HandlePausingShell);
    tfex::gen::hooks::SFX_Play::Wrap(xplay::SfxPlayShell);                       /* the Switch's centre panX before TF.State's SFXPatch */
    tfex::gen::hooks::SFXInstanced_Play::Wrap(xplay::SfxInstancedPlayShell);
    xplay::InstallTfStateHooks();
    tfex::gen::hooks::Session_EndRound::Wrap(xplay::EndRoundShell);
    tfex::gen::hooks::Session_GotoNextRound::Wrap(xplay::GotoNextRoundShell);
    xplay::PlayerDieHook::InstallAtOffset(0xaee3d0);
    xplay::TeamOutlinesHook::InstallAtOffset(0xb68c40);   /* the Switch's TeamOutlinesLayer: once per drawn frame in a session */
    tfex::gen::hooks::Session_CreateResults::Wrap(xplay::CreateResultsShell);
    Logging.Log("[xplay] input hook %s, NormalUpdate hook installed", xplay::g_getFullKey ? "set" : "NOT set (export missing)");
    pools::Register("exlaunch trampolines", [](pools::Use* u) { u->used = exl::hook::nx64::TrampolinesUsed(); u->cap = exl::hook::nx64::TrampolinesMax(); });
    pools::Register("exlaunch inline hooks", [](pools::Use* u) { u->used = exl::hook::nx64::InlineHooksUsed(); u->cap = exl::hook::nx64::InlineHooksMax(); });
    pools::Report("boot installs done");
}

