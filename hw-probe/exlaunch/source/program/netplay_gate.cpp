/* The netplay gate: TF.EX's TFGame.Update / NetplayLogic on its generated NetplayManager over ggrs_ffi (Poll, the events' Switch side, the sync wait, the frame driver, the request loop RbGate), TF.State's GetState / LoadState through the rollback seam, the [desync] logging (shared state: xplay_internal.hpp) */
#include "xplay_internal.hpp"
#include "tfexgen_State_Api.hpp"
#include "tfexgen_InputService.hpp"
#include "tfexgen_services.hpp"
#include "tfexgen_NetplayEntry.hpp"
#include "lib.hpp"
#include "lib/hook/nx64/impl.hpp"
namespace tfex::gen { extern bool g_traceOnly, g_traceNow; }   /* (tfexgen_code_rt.cpp: TFEX_TRACE_UNITS builds) */

namespace xplay {
int g_testMismatches = 0;   /* TF.EX's test mode: AdvanceFrame errors (the sync test's mismatches), in the timing summary */
/* GGRS's request list: save f / load f = the state at the start of frame f (TF.State's GetState / LoadState, the seam below); advance
 * f = simulate f. Every advance but the last is a resimulation (the Level's Update called directly); the last one is this frame's normal
 * game update (returns false). */
static TfxInput g_rIn[kRbWindow][kNpSeats];   /* the inputs each frame was last simulated with ([desync] logging) */
static int g_rInFrame[kRbWindow];
static int32_t g_rSt[kRbWindow][kNpSeats];     /* GGRS's status of those inputs (0 confirmed, 1 predicted, 2 disconnected) */
/* ---- The recurring hardware desync (STATUS.md "The recurring desync": the Switch kills a REMOTE archer the browsers keep alive): at every
 * remote death in a lobby match, the frame, cause, killer, position, GGRS's confirmed frame and that seat's inputs (with their status) for
 * the frames before; any rollback across such a death and whether it happens again in the resimulation; the first remote inputs that carry
 * the disconnected flag (TF.EX's PlayerPatch kills that archer with Curse). Bounded per match. ---- */
static int g_dsMatch = -1, g_dsDeaths = 0, g_dsDisc = 0;
static int g_dsLastDeath = -1, g_dsLastSeat = -1;   /* the latest remote death's frame (first simulation) and seat */
static int g_dsRecheck = -1; static bool g_dsRedone = false;   /* a rollback crossed g_dsRecheck: did the death happen again? */
static void DsMatch() { if (g_dsMatch != g_lobbyMatchNo) { g_dsMatch = g_lobbyMatchNo; g_dsDeaths = g_dsDisc = 0; g_dsLastDeath = g_dsLastSeat = g_dsRecheck = -1; } }
static void DsInputLine(int f, int seat) {
    if (f < 0 || g_rInFrame[f % kRbWindow] != f) { HWLOG("[desync]   f%d: no inputs recorded", f); return; }
    const TfxInput& i = g_rIn[f % kRbWindow][seat];
    static const char st[] = "CPD?";
    int s = g_rSt[f % kRbWindow][seat];
    HWLOG("[desync]   f%d %c: j%d%d s%d%d a%d%d d%d%d arrow %d m %d,%d aim %.3f,%.3f right %.3f,%.3f disc %d", f, st[s >= 0 && s < 3 ? s : 3],
          i.jc, i.jp, i.sc, i.sp, i.ac, i.ap, i.dc, i.dp, i.arrow, i.mx, i.my, i.ax, i.ay, i.rx, i.ry, i.disc);
}
void DesyncRemoteDeath(void* pl, int cause, int killer, bool brambled, bool laser) {
    if (!g_lobbyStart || !g_npSession) return;
    int seat = *reinterpret_cast<int32_t*>(reinterpret_cast<uintptr_t>(pl) + fld::Player_PlayerIndex);
    if (seat == g_npSeat || seat < 0 || seat >= kNpSeats) return;
    DsMatch();
    int f = g_npFrame - 1;   /* the frame being simulated */
    if (g_resim && g_dsRecheck >= 0 && seat == g_dsLastSeat) g_dsRedone = true;
    if (g_dsDeaths >= 8) return;
    g_dsDeaths++;
    if (!g_resim) { g_dsLastDeath = f; g_dsLastSeat = seat; }
    HWLOG("[desync] remote p%d died at frame %d%s: cause %d killer %d brambled %d laser %d at (%.2f,%.2f); GGRS confirmed %d, current %d", seat, f,
          g_resim ? " (resimulation)" : "", cause, killer, (int)brambled, (int)laser, *reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(pl) + 0x14),
          *reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(pl) + 0x18), NpConfirmed(), NpFrame());
    for (int k = f - 8; k <= f; k++) DsInputLine(k, seat);
}
static void DsRecordInputs(int f, const TfxInput* in, const int32_t* status) {   /* each simulated frame (first time and resimulated) */
    for (int i = 0; i < kNpSeats; i++) g_rSt[f % kRbWindow][i] = status ? status[i] : 0;
    if (!g_lobbyStart) return;
    DsMatch();
    for (int i = 0; i < g_npN && i < kNpSeats; i++)
        if (i != g_npSeat && in[i].disc && g_dsDisc < 4) { g_dsDisc++; HWLOG("[desync] remote p%d's input at frame %d carries the disconnected flag (status %d%s): TF.EX's PlayerPatch kills that archer", i, f, status ? status[i] : -1, g_resim ? ", resimulation" : ""); }
}
static void DsRollback(int to, int from) {   /* a load to `to` from state frame `from` */
    if (!g_lobbyStart || g_dsLastDeath < 0 || g_dsLastDeath < to || g_dsLastDeath > from) return;
    static int n = 0;
    if (n++ >= 16) return;
    g_dsRecheck = g_dsLastDeath; g_dsRedone = false;
    HWLOG("[desync] rollback to %d (from %d) crosses remote p%d's death at frame %d: resimulated with GGRS's inputs", to, from, g_dsLastSeat, g_dsLastDeath);
}
static void DsAfterResim() {
    if (g_dsRecheck < 0) return;
    HWLOG("[desync] after the rollback: remote p%d %s at frame %d", g_dsLastSeat, g_dsRedone ? "died again in the resimulation" : "did NOT die again in the resimulation", g_dsRecheck);
    g_dsRecheck = -1;
}
/* A resimulation (TF.EX's NetplayLogic: an AdvanceFrame request -> AdvanceGameState, then level.Update()): the Switch's bookkeeping around
 * the Level.Update it runs, from LevelUpdateShell (xplay.cpp) when TF.EX's NetplayManager has frames to resimulate. The frame being
 * simulated is the state frame; TF.EX's AdvanceGameState put ggrs_ffi's inputs of it in GameContext's current inputs (NpCurrentInputs). */
static double g_resimT0 = 0;
int ResimBegin() {
    int f = g_stateFrame;
    g_fc.resimN++;
    TfxInput cur[kNpSeats]; NpCurrentInputs(cur);
    NpCopy(g_rIn[f % kRbWindow], cur); g_rInFrame[f % kRbWindow] = f;
    DsRecordInputs(f, cur, nullptr);
    g_npFrame = f + 1;
    g_resim = true;
    g_resimT0 = Ms(svcGetSystemTick());
    return f;
}
void ResimEnd(int f) {
    g_resim = false;
    g_fc.resim += Ms(svcGetSystemTick()) - g_resimT0;
    g_rbResimTicks++;
    g_stateFrame = f + 1;
}
struct RbCost { bool on; int depth; uint64_t t0; double loadMs; double resim0; int resimN0; };
static RbCost g_rbCost{};
/* TF.EX's NetplayLogic (TFGame.cs) on its generated NetplayManager over ggrs_ffi; a save and a load are TF.State's (the seam below), so the
 * NetplayManager and everything outside TF.State's state stay as they are across a load, as on the PC. */
/* the session's first steps of each match, logged: the first 12 polls before synchronization and the first 6 synchronized frames (where a
 * stall or a crash stopped; a bounded number of lines per match) */
static int g_stepMatch = -1, g_stepFrames = 0, g_stepLines = 0;
static bool StepLog() {
    if (g_stepMatch != g_lobbyMatchNo) { g_stepMatch = g_lobbyMatchNo; g_stepFrames = 0; g_stepLines = 0; }
    return g_stepFrames < 6 && g_stepLines < 60;
}
#define STEP(...) do { if (StepLog()) { g_stepLines++; HWLOG("[np] step: " __VA_ARGS__); } } while (0)
static int RequestFrame() { return g_stateFrame; }   /* ggrs_ffi's requests carry no frame: the state frame is the one each applies to */
/* ---- The rollback seam: ggrs_ffi's save and load requests (TF.EX's TFGame.NetplayLogic SaveGameState / LoadGameState) go through this
 * one pair: TF.State's TfStateApi.CaptureGameState / RestoreGameStateBytes, TF.EX's way. A load lands exactly on the bytes' frame. The frame
 * loop's own bookkeeping (seats, vsOrigin, lvTick: not game state) rides in a ring keyed by frame; the gameplay RNG is in TF.State's state
 * (glue/State.cs). ---- */
static SideAll g_sideRing[kRbWindow];
static int g_sideRingFrame[kRbWindow];
/* hw.txt "allocwatch 1" (MEASUREMENT): the game's managed allocations counted (objects through NewObject +0x610, arrays through NewArray
 * +0x6110, strings through String.InternalAllocateStr +0x11d10; counting trampolines, no change), and what TF.State's saves and loads
 * allocate logged with the [rb] save / load cost lines. What feeds the GC (the [gc] lines). */
namespace allocwatch {
bool g_on = false; bool g_installed = false;
uint64_t g_n = 0, g_bytes = 0;
/* by type, while a save / load runs (g_phase 1 / 2): objects by runtime type, arrays by array type, strings as one row */
int g_phase = 0;
struct Row { void* type; uint64_t n, bytes; };
Row g_rows[3][256];   /* save, load, and the rest of the frame (outside saves and loads) */
void CountRow(void* type, uint64_t bytes) {
    Row* t = g_rows[g_phase ? g_phase - 1 : 2];
    uint32_t h = (uint32_t)(reinterpret_cast<uintptr_t>(type) >> 4) & 255;
    for (int k = 0; k < 256; k++, h = (h + 1) & 255) {
        if (t[h].type == type || !t[h].type) { t[h].type = type; t[h].n++; t[h].bytes += bytes; return; }
    }
}
HOOK_DEFINE_TRAMPOLINE(NewObjectCount) {
    static void* Callback(void* type) { g_n++; uint64_t b = type ? *static_cast<uint32_t*>(type) : 0; g_bytes += b; CountRow(type, b); return Orig(type); }
};
HOOK_DEFINE_TRAMPOLINE(NewArrayCount) {
    static void* Callback(void* type, int32_t elem, const int32_t* lens, int32_t rank) {
        g_n++; uint64_t c = 1; for (int32_t i = 0; lens && i < rank; i++) c *= (uint64_t)(lens[i] > 0 ? lens[i] : 0); uint64_t b = c * (uint64_t)(elem > 0 ? elem : 0) + 0x28; g_bytes += b; CountRow(type, b);
        return Orig(type, elem, lens, rank);
    }
};
HOOK_DEFINE_TRAMPOLINE(NewStringCount) {
    static void* Callback(int32_t len) { g_n++; uint64_t b = (uint64_t)(len > 0 ? len : 0) * 2 + 0x18; g_bytes += b; CountRow(reinterpret_cast<void*>(1), b); return Orig(len); }
};
/* and Boehm's own GC_malloc (+0x285c4e0) / GC_malloc_atomic (+0x285c5c0), by caller: everything that feeds the collections, whatever
 * path it takes (the managed allocators above call these too; native code and our runtime call them directly). Counters only (they run
 * on any thread; a race costs a count, not memory) */
struct Caller { uintptr_t lr, up; uint64_t n, bytes; };   /* up: the next frame's return address (a trampoline's caller) */
Caller g_callers[512];
uint64_t g_gcBytes = 0;
void CountCaller(uintptr_t lr, uintptr_t up, uint64_t bytes) {
    g_gcBytes += bytes;
    uint32_t h = (uint32_t)(lr >> 2) & 511;
    for (int k = 0; k < 512; k++, h = (h + 1) & 511) {
        if (g_callers[h].lr == lr || !g_callers[h].lr) { g_callers[h].lr = lr; if (!g_callers[h].up) g_callers[h].up = up; g_callers[h].n++; g_callers[h].bytes += bytes; return; }
    }
}
uintptr_t UpLr() {   /* the return address one frame above the hooked function's caller (frame records: x29 chain) */
    auto* fp = static_cast<uintptr_t*>(__builtin_frame_address(1));
    return fp && !(reinterpret_cast<uintptr_t>(fp) & 7) ? fp[1] : 0;
}
HOOK_DEFINE_TRAMPOLINE(GcMallocCount) {
    static void* Callback(size_t bytes) { CountCaller(reinterpret_cast<uintptr_t>(__builtin_return_address(0)), UpLr(), bytes); return Orig(bytes); }
};
HOOK_DEFINE_TRAMPOLINE(GcMallocAtomicCount) {
    static void* Callback(size_t bytes) { CountCaller(reinterpret_cast<uintptr_t>(__builtin_return_address(0)), UpLr(), bytes); return Orig(bytes); }
};
void DumpCallers() {
    const auto* game = &exl::util::GetMainModuleInfo(); const auto* ours = &exl::util::GetSelfModuleInfo();
    HWLOG("[alloc] GC_malloc / GC_malloc_atomic over 600 saves: %.1f MB; top callers (game / ours / sdk +offset: allocations, bytes; ours: "
          "DumpCallers at +%#lx, for the ELF's addresses):", g_gcBytes / 1048576.0, (unsigned long)(reinterpret_cast<uintptr_t>(&DumpCallers) - ours->m_Total.m_Start));
    bool taken[512] = {};
    for (int k = 0; k < 16; k++) {
        int best = -1; for (int i = 0; i < 512; i++) if (g_callers[i].lr && !taken[i] && (best < 0 || g_callers[i].bytes > g_callers[best].bytes)) best = i;
        if (best < 0) break;
        taken[best] = true; uintptr_t lr = g_callers[best].lr;
        const auto* m = exl::util::TryGetModule(lr);
        uintptr_t up = g_callers[best].up; const auto* mu = exl::util::TryGetModule(up);
        HWLOG("[alloc]   %s +%#lx  %llu  %llu  (above: %s +%#lx)", m == game ? "game" : m == ours ? "ours" : m ? "sdk" : "?", (unsigned long)(lr - (m ? m->m_Total.m_Start : 0)),
              (unsigned long long)g_callers[best].n, (unsigned long long)g_callers[best].bytes, mu == game ? "game" : mu == ours ? "ours" : mu ? "sdk" : "?",
              (unsigned long)(up - (mu ? mu->m_Total.m_Start : 0)));
    }
    std::memset(g_callers, 0, sizeof g_callers); g_gcBytes = 0;
}
void Install() {
    if (!g_on || g_installed) return;
    g_installed = true;
    NewObjectCount::InstallAtOffset(0x610); NewArrayCount::InstallAtOffset(0x6110); NewStringCount::InstallAtOffset(0x11d10);
    GcMallocCount::InstallAtOffset(0x285c4e0); GcMallocAtomicCount::InstallAtOffset(0x285c5c0);
    HWLOG("[alloc] counting the game's managed allocations (allocwatch: NewObject, NewArray, String)");
}
struct Window { uint64_t n0, b0; static Window Begin() { return Window{ g_n, g_bytes }; } };
uint64_t g_saveN = 0, g_saveB = 0, g_saves = 0, g_loadN = 0, g_loadB = 0, g_loads = 0;
void TypeName(void* type, char* out, int cap) {   /* the runtime type's full name (UTF-16 at +0x30: a managed string, chars at +0x14?) as ASCII */
    out[0] = 0;
    if (type == reinterpret_cast<void*>(1)) { std::snprintf(out, (size_t)cap, "System.String"); return; }
    const char16_t* nm = type ? *reinterpret_cast<const char16_t* const*>(reinterpret_cast<uintptr_t>(type) + 0x30) : nullptr;
    int i = 0; for (; nm && nm[i] && i < cap - 1; i++) out[i] = nm[i] < 128 ? (char)nm[i] : '?'; out[i] = 0;
}
void DumpRows(int phase, const char* what, uint64_t runs) {
    Row* t = g_rows[phase]; bool taken[256] = {};
    HWLOG("[alloc] %s: top allocated types per %s (allocations, bytes) over %llu:", what, what, (unsigned long long)runs);
    for (int k = 0; k < 18 && runs; k++) {
        int best = -1; for (int i = 0; i < 256; i++) if (t[i].type && !taken[i] && (best < 0 || t[i].bytes > t[best].bytes)) best = i;
        if (best < 0) break;
        taken[best] = true; char nm[96]; TypeName(t[best].type, nm, sizeof nm);
        HWLOG("[alloc]   %-70s %6.1f %8.0f", nm, (double)t[best].n / runs, (double)t[best].bytes / runs);
    }
    std::memset(t, 0, sizeof g_rows[0]);
}
}
void AllocWatch(bool on) { allocwatch::g_on = on; }
/* TF.EX's NetplayLogic SaveGameState request: StateApi.Current.CaptureGameState() comes here (an ITfStateApi [service] rule, through
 * svc::CaptureForSeam): TF.State's CaptureGameState, with the Switch's bookkeeping (the frame loop's side ring) and cost logs around it. The
 * bytes stay valid until the next save (ggrs_ffi copies them in NetplayManager.SaveGameState). GGRS's checksum is ggrs_ffi's own, TF.EX's:
 * fletcher16 of these bytes (byte-identical to the PC's state) */
tfex::gen::Arr<uint8_t> SeamCapture() {
    CostTimer ct(&g_fc.save);
    static int match = -1;
    if (match != g_lobbyMatchNo) {   /* the match's first save: its frames start at ggrs_ffi's (a ring entry of an earlier match never matches) */
        match = g_lobbyMatchNo;
        g_stateFrame = NpFrame();
        for (int i = 0; i < kRbWindow; i++) g_sideRingFrame[i] = -1;
    }
    int f = RequestFrame();
    /* TF.EX labels a save with netplay_current_frame() (NetplayLogic: ExFlags.CurrentFrame, StateApi.SetCurrentFrame; GameState.Frame is in
     * the checksummed bytes). It must be this loop's state frame (set from it at the match's first save, advanced with every tick); a load
     * returns it */
    int label = NpFrame();
    { static int logged = 0, odd = 0; if (logged++ < 3 || (label != f && odd++ < 8)) HWLOG("[rb] TF.State save: state frame %d, label %d (netplay_current_frame)%s", f, label, label != f ? ": ERROR, they differ" : ""); }
    static tfex::gen::Arr<uint8_t> kept;
    uint64_t tSave0 = svcGetSystemTick();
    tfex::gen::statert::g_stampLz4In = tfex::gen::statert::g_stampLz4Out = 0;
    allocwatch::Install();
    auto aw = allocwatch::Window::Begin();
    allocwatch::g_phase = 1;
    tfex::gen::statert::ProfBegin();
    kept = tfex::gen::State_Api::CaptureGameState(nullptr);
    allocwatch::g_phase = 0;
    allocwatch::g_saveN += allocwatch::g_n - aw.n0; allocwatch::g_saveB += allocwatch::g_bytes - aw.b0; allocwatch::g_saves++;
    uint64_t tSave1 = svcGetSystemTick();
    tfex::gen::statert::ProfEnd(0);
    {   /* the console's cost split (a bounded log: the first 5, then every 600th save): GetState + MessagePack | LZ4 | the rest */
        static int n = 0; static double sEnc = 0, sLz4 = 0, sAll = 0;
        auto& st = tfex::gen::statert::g_stampLz4In; auto& so = tfex::gen::statert::g_stampLz4Out;
        double all = Ms(tSave1 - tSave0), lz = (st && so >= st) ? Ms(so - st) : 0, enc = st ? Ms(st - tSave0) : all;
        sEnc += enc; sLz4 += lz; sAll += all;
        if (n == 1200 || n == 4800) tfex::gen::statert::ProfDump(0, "save", 30);   /* (a TFEX_PROF_UNITS regeneration only) */
        if (n == 0) tfex::gen::statert::GcWatch();
        if (n > 0 && n % 600 == 0) {
            char g[400]; tfex::gen::statert::GcSummary(g, sizeof g); HWLOG("[gc] over the last 600 saves: %s", g);
            if (n == 1200 || n == 4800) tfex::gen::statert::GcCensus();
            using namespace allocwatch;
            if (g_installed && g_saves && g_loads) HWLOG("[alloc] per save %.1f allocations / %.0f bytes, per load %.1f / %.0f (%llu saves, %llu loads)", (double)g_saveN / g_saves, (double)g_saveB / g_saves, (double)g_loadN / g_loads, (double)g_loadB / g_loads, (unsigned long long)g_saves, (unsigned long long)g_loads);
            if (g_installed && (n == 1200 || n == 4800)) { DumpRows(0, "save", g_saves); DumpRows(1, "load", g_loads); DumpRows(2, "600 saves' ticks outside saves and loads", 1); DumpCallers(); }
            else { std::memset(g_rows, 0, sizeof g_rows); std::memset(g_callers, 0, sizeof g_callers); g_gcBytes = 0; }
            g_saveN = g_saveB = g_saves = g_loadN = g_loadB = g_loads = 0;
        }
        if (++n <= 5 || n % 600 == 0) { HWLOG("[rb] save cost #%d: %.2f ms (GetState+encode %.2f, LZ4 %.2f; %d bytes); avg over %d saves: %.2f (%.2f, %.2f)", n, all, enc, lz, kept.len, n, sAll / n, sEnc / n, sLz4 / n); }
    }
    tfex::gen::statert::DumpSaved(f, kept);   /* hw.txt statedump <N>: statediff.py against the page's (step E) */
    SideSave(&g_sideRing[f % kRbWindow]);
    g_sideRingFrame[f % kRbWindow] = f;
    return kept;
}
/* TF.EX's NetplayLogic LoadGameState request: StateApi.Current.RestoreGameStateBytes(bytes) comes here (svc::RestoreForSeam): TF.State's
 * RestoreGameStateBytes, then the Switch's bookkeeping (the side ring, the state frame) and logs. Returns the frame the game is now at the
 * start of (the bytes' frame), -1 if they can't be loaded. */
int SeamRestore(tfex::gen::Arr<uint8_t> h) {
    const uint8_t* bytes = h.data; int32_t len = h.len;
    uint64_t t0 = svcGetSystemTick();
    tfex::gen::statert::g_stampUnwrapOut = 0;
    tfex::gen::g_traceOnly = true; tfex::gen::g_traceNow = g_rbLoads == 0;   /* (translate.sh with TFEX_TRACE_UNITS: the first load's calls) */
    auto aw = allocwatch::Window::Begin();
    allocwatch::g_phase = 2;
    tfex::gen::statert::ProfBegin();
    int to = tfex::gen::State_Api::RestoreGameStateBytes(nullptr, tfex::gen::Arr<uint8_t>{ const_cast<uint8_t*>(bytes), len });   /* (the save's label: the state frame) */
    tfex::gen::g_traceNow = false;
    allocwatch::g_phase = 0;
    tfex::gen::statert::ProfEnd(1);
    allocwatch::g_loadN += allocwatch::g_n - aw.n0; allocwatch::g_loadB += allocwatch::g_bytes - aw.b0; allocwatch::g_loads++;
    {   /* the console's cost split (bounded: the first 5, then every 300th load): unwrap + deserialize | LoadState */
        static int n = 0; static double sDec = 0, sAll = 0;
        uint64_t tu = tfex::gen::statert::g_stampUnwrapOut, t1 = svcGetSystemTick();
        double all = Ms(t1 - t0), dec = tu >= t0 ? Ms(tu - t0) : 0;
        sDec += dec; sAll += all;
        if (n == 600 || n == 2400) tfex::gen::statert::ProfDump(1, "load", 30);
        if (++n <= 5 || n % 300 == 0) HWLOG("[rb] load cost #%d: %.2f ms (LZ4 unwrap %.2f, deserialize+LoadState %.2f); avg over %d loads: %.2f", n, all, dec, all - dec, n, sAll / n);
    }
    if (to < 0 || g_sideRingFrame[to % kRbWindow] != to) {
        HWLOG("[rb] FATAL: TF.State's RestoreGameStateBytes gave frame %d (%d bytes; bookkeeping ring has frame %d)", to, len, to < 0 ? -1 : g_sideRingFrame[to % kRbWindow]);
        return -1;
    }
    SideLoad(&g_sideRing[to % kRbWindow]);
    int depth = g_stateFrame - to;
    DsRollback(to, g_stateFrame);
    g_stateFrame = to;
    rumble::OnLoad(to);
    double loadMs = Ms(svcGetSystemTick() - t0);
    g_fc.rb += loadMs;
    g_rbCost = RbCost{ true, depth, t0, loadMs, g_fc.resim, g_fc.resimN };   /* logged once this tick's resimulations are done */
    g_rbLoads++;
    if (g_rbLoads <= 10 || g_rbLoads % 100 == 0) HWLOG("[rb] rollback #%d: back %d frames to %d (%d bytes, LoadState %.2f ms; confirmed %d, next line to write %d)", g_rbLoads, depth, to, len, loadMs, NpConfirmed(), g_commitNext);
    if (depth > g_rbMaxDepth) g_rbMaxDepth = depth;
    if (g_resultsFrame > to) { g_resultsFrame = -1; g_session = nullptr; }   /* the match's results come again if real (xplay's own match-end tracking;
                                                                              * TF.EX's VersusMatchResultsInputGate.NotifyRollback is its own) */
    return to;
}
/* The frame's own update (TF.EX's TFGame.Update prefix, after NetplayLogic: ConsumeNetplayRequest, UpdateFramesToReSimulate(0),
 * AdvanceGameState, the original update): the Switch's bookkeeping before its Level.Update, from LevelUpdateShell. The loads and
 * resimulations GGRS asked for are done: the state is the start of the state frame. */
void FrameOwnBegin() {
    /* Final lines: frames up to the confirmed frame as it was one advance ago (the advances since corrected any of them that were
     * mispredicted; a frame confirmed later can still be rolled back by the next advance). Label = frame + 1. */
    static int confPrev = -1;
    LogCommit(confPrev + 1 < g_stateFrame ? confPrev + 1 : g_stateFrame);
    confPrev = NpConfirmed();
    int frame = RequestFrame();
    STEP("the frame's own advance: frame %d", frame);
    if (StepLog()) g_stepFrames++;
    DsAfterResim();
    if (g_rbCost.on) {   /* one line per rollback (the 240 Hz case: what a rollback costs by depth) */
        g_rbCost.on = false;
        /* the first 200 in full, then a summary every 200 rollbacks (no boot-wide cap: the late matches keep their numbers) */
        static int lines = 0, n = 0, maxDepth = 0, resimTicks = 0; static double ms = 0, maxMs = 0, loadMs = 0;
        double all = Ms(svcGetSystemTick() - g_rbCost.t0);
        int rt = g_fc.resimN - g_rbCost.resimN0;
        if (lines < 200) { lines++; HWLOG("[rb] cost: depth %d, %.2f ms (bookkeeping and restore %.2f ms, %d resimulated ticks %.2f ms)", g_rbCost.depth,
                                          all, g_rbCost.loadMs, rt, g_fc.resim - g_rbCost.resim0); }
        else {
            n++; ms += all; loadMs += g_rbCost.loadMs; resimTicks += rt;
            if (all > maxMs) maxMs = all;
            if (g_rbCost.depth > maxDepth) maxDepth = g_rbCost.depth;
            if (n == 200) {
                HWLOG("[rb] cost summary of the last %d rollbacks: %.2f ms avg (restore %.2f), max %.2f ms, max depth %d, %.1f resimulated ticks avg", n, ms / n, loadMs / n, maxMs, maxDepth, (double)resimTicks / n);
                n = maxDepth = resimTicks = 0; ms = maxMs = loadMs = 0;
            }
        }
    }
    TfxInput cur[kNpSeats]; NpCurrentInputs(cur);   /* TF.EX's GameContext current inputs: this frame's, from its AdvanceGameState */
    for (int i = 0; i < g_npN && i < kNpSeats; i++)
        if (i != g_npSeat && std::memcmp(&cur[i], &g_lastRemote[i], sizeof(TfxInput))) { g_remoteChanges++; g_lastRemote[i] = cur[i]; }
    g_matchFirstTicks++;
    g_npFrame = frame + 1;
    DsRecordInputs(frame, cur, nullptr);
    NpCopy(g_rIn[frame % kRbWindow], cur); g_rInFrame[frame % kRbWindow] = frame;
    g_stateFrame = frame + 1;
}

/* Session.Scores in seat order (TF.EX HUDPatch.SeatScores: Scores[GetScoreIndex(seat)]; GetScoreIndex +0xb45230 maps a seat to its team in
 * team modes) and the winner's seat (WinnerSeat: in team mode the first seat of the winning team). */
static int SeatScores(void* session, int seats, int* out, int* winnerSeat) {
    *winnerSeat = -1;
    if (!session) return 0;
    void* arr = *reinterpret_cast<void**>(reinterpret_cast<uintptr_t>(session) + 0x30);
    if (!arr) return 0;
    int n = **reinterpret_cast<int**>(reinterpret_cast<uintptr_t>(arr) + 0x20);
    const int* d = *reinterpret_cast<int**>(reinterpret_cast<uintptr_t>(arr) + 0x10);
    auto scoreIndex = reinterpret_cast<int (*)(void*, int)>(Base() + 0xb45230);
    int winner = reinterpret_cast<int (*)(void*)>(Base() + 0xb453b0)(session);
    int k = 0;
    for (int seat = 0; seat < seats && seat < kNpSeats; seat++) {
        int i = g_lobbyMode == 5 ? scoreIndex(session, seat) : seat;
        out[k++] = i >= 0 && i < n ? d[i] : 0;
        if (*winnerSeat < 0 && winner >= 0 && i == winner) *winnerSeat = seat;
    }
    if (g_lobbyMode != 5) *winnerSeat = winner;
    return k;
}
/* The match's end, the Switch's side (before TF.EX's TFGame.Update prefix): once the results finished, the session ends (EndLobbyMatch:
 * TF.EX's NetplayManager.Reset and the native match state) and the lobby gets the results. Returns true when this frame's update is skipped
 * (the finished match's level, frozen until it is left). */
bool MatchEndFrame() {
    if (g_resultsDone) {
        g_resultsDone = false;
        int scores[kNpSeats] = {}, winner = -1;
        int n = SeatScores(g_session, g_lobbyPlayers, scores, &winner);
        VsEvent("match results finished"); VsFlush();
        g_vsLogDone = true;
        const bool test = g_testSession;
        EndLobbyMatch();   /* the level now runs locally: the results screen and its menu, with this console's controller */
        if (!test) tfex::OnLobbyResultsFinished(winner, scores, n);
        else HWLOG("[test] the test match is over (winner seat %d)", winner);
        return false;
    }
    return g_npOn && g_vsStarted && g_npSession && g_matchFinished;
}
/* TF.EX's Poll (INetplayManager.Poll, through svc::PollForGate): NativePoll, frames ahead, GetEventAndUpdate (the notifications, the
 * desync log, its Disconnected branch), the network stats. Then the Switch's per-event logs (bounded) and counters. */
void AfterPoll(void* nm) {
    namespace NM = tfex::gen::NetplayManager;
    static int polls = 0, pollMatch = -1;
    if (pollMatch != g_lobbyMatchNo) { pollMatch = g_lobbyMatchNo; polls = 0; }
    if (polls++ < 12) STEP("Poll done (synchronized %d), events %p", (int)NM::IsSynchronized(nm), NM::S(nm)->_events);
    tfex::gen::ffi::ForEachEvent(NM::S(nm)->_events, [&](const char* e) {
        if (!std::strncmp(e, "Synchronized with", 17)) {
            g_npEvents[2]++;
            HWLOG("[np] lobby match #%d synchronized (%s)", g_lobbyMatchNo, e);
        } else if (!std::strncmp(e, "Disconnected from", 17)) {
            g_npEvents[3]++;
            if (g_resultsFrame < 0) g_remotesGone++;
            HWLOG("[np] %s at frame %d (TF.EX's GetEventAndUpdate; the level runs on without GGRS once every remote is gone)", e, NpFrame());
        } else if (!std::strncmp(e, "NetworkInterrupted", 18)) {
            g_npEvents[4]++;
            HWLOG("[np] %s at frame %d", e, NpFrame());
        } else if (!std::strncmp(e, "NetworkResumed", 14)) {
            g_npEvents[5]++;
            HWLOG("[np] %s at frame %d", e, NpFrame());
        } else if (!std::strncmp(e, "DesyncDetected", 14)) {
            g_npEvents[7]++;
            static int nDesync = 0;   /* the first 5 in full, then one line every 10 (the states: hw.txt statedump, tools/tfex/statediff.py) */
            if (++nDesync <= 5) HWLOG("[np] DESYNC: %s", e);
            else if (nDesync % 10 == 0) HWLOG("[np] DESYNC: %s (%d so far)", e, nDesync);
            static int dumpedFor = -1;   /* the first one of a match: the last 256 gameplay draws, for the page's rngtrace */
            if (dumpedFor != g_lobbyMatchNo) { dumpedFor = g_lobbyMatchNo; HWLOG("[np] the last 256 gameplay draws (draw, frame, call site):"); rnd::DumpDraws([](const char* l) { hw::Log("%s", l); }); }
        } else if (!std::strncmp(e, "Synchronizing", 13)) {
            static int n = 0; if (n++ < 8) HWLOG("[np] %s", e);
        }
    });
}
/* Before TF.EX's TFGame.Update prefix, in a lobby match: the synchronization wait's start (TF.EX's _synchronizationTimer, a [service]
 * rule: AbortIfSynchronizationFailed reads it) and xplay's input delay in the session's ticks (SyncInputDelay: before TF.EX's
 * EstablishSessionIfSynchronized, inside the prefix) */
void BeforeTfexUpdate() {
    if (!g_npOn || !g_vsStarted || !g_npSession) return;
    namespace NM = tfex::gen::NetplayManager;
    void* nm = g_npSession;
    const bool synced = !tfex::gen::ffi::InInit() && NM::get__isInit(nm) && NM::IsSynchronized(nm);
    if (g_lobbyStart && g_resultsFrame < 0 && !synced) { if (!g_syncWaitSince) g_syncWaitSince = svcGetSystemTick(); }
    else g_syncWaitSince = 0;
    if (synced || g_testSession) tfex::EndOfMatch::SyncInputDelay();
}
} // namespace xplay
