/* hangwatch.cpp: a hang reporter. The game's frame loop (xplay.cpp FrameShell) beats every frame; a watchdog thread checks the beat each
 * second, and when it stops for 5 s (the game froze: no crash, nothing logged; 2026-10-30, TF.EX v0.28.0's first boot hung after the intro)
 * it suspends the main thread (nn::os::SuspendThread), reads its registers (svcGetThreadContext3 on its kernel handle, found in its
 * nn::os::ThreadType at boot: the word whose svcGetThreadId is the main thread's id), logs PC, LR and the frame-pointer chain by module, and
 * resumes it. Once, then every 30 s while the beat stays stopped. The SDK functions are bound by name at boot; without them it says so.
 * Another thread can be watched too (Watch: the FortRise mod loader runs on the game's loading thread, and a freeze in it shows on the
 * main thread only as a GC stop-the-world that waits for it): its stack is reported with the main thread's.
 * Slow updates (STATUS: the 23.7 s frame of 2026-11-01 20:05, no hang, everything ~50x slower): FrameShell brackets each TFGame.Update
 * (UpdateBegin/End); while one runs past 100 ms the watchdog samples the main thread's stack every 100 ms ([slow] sample, bounded), and the
 * update's end logs its wall time beside the main thread's own CPU time (svcGetInfo ThreadTickCount): close = the work is slow, far apart =
 * the thread waited or was preempted. */
#include "lib.hpp"
#include "hwlog.hpp"
#include "netws.hpp"
#include <cstdint>
#include <cstring>

namespace hangwatch {
namespace {
using FindFn = uintptr_t (*)(const char*);
void* (*osGetCurrentThread)() = nullptr;
int32_t (*osSuspendThread)(void*) = nullptr;
int32_t (*osResumeThread)(void*) = nullptr;
void* g_main = nullptr;
Handle g_mainHandle = 0;
void* g_other = nullptr;                 /* Watch's thread, while it is watched */
Handle g_otherHandle = 0;
const char* g_otherName = nullptr;
volatile uint32_t g_beat = 0;
alignas(0x1000) uint8_t g_stack[0x4000];   /* (the SDK wants a page-aligned stack) */
ThreadContext g_ctx;
constexpr uint64_t kSlowTicks = 1920000;   /* 100 ms of the 19.2 MHz system tick */
volatile uint64_t g_updAt = 0;             /* svcGetSystemTick at the running TFGame.Update's start; 0 between updates */
uint64_t g_lastSample = 0;
int g_samples = 0, g_unsampled = 0;        /* [slow] sample lines (at most kMaxSamples a session) and the ones not logged */
char g_lastLine[512];                      /* the last sample's stack: a repeat is counted, not logged (g_same) */
int g_same = 0;
void SameFlush() { if (g_same) hw::Log("[slow]   the same stack %d more time%s", g_same, g_same > 1 ? "s" : ""); g_same = 0; }
constexpr int kMaxSamples = 120;

void Where(const char* what, uint64_t a) {
    const auto* m = exl::util::TryGetModule(a);
    const char* name = !m ? "?" : m == &exl::util::GetMainModuleInfo() ? "game" : m == &exl::util::GetSelfModuleInfo() ? "ours" : "sdk";
    hw::Log("[hang]   %s %s +%#lx", what, name, (unsigned long)(a - (m ? m->m_Total.m_Start : 0)));
}

/* the kernel handle of a thread, from its ThreadType: the word whose svcGetThreadId is that thread's id (called on the thread itself) */
Handle HandleOfCurrent(void* thread) {
    u64 self = 0;
    if (!thread || svcGetThreadId(&self, CUR_THREAD_HANDLE) != 0) return 0;
    for (int o = 0; o < 0x1c0; o += 4) {
        Handle h = *reinterpret_cast<const uint32_t*>(static_cast<const uint8_t*>(thread) + o);
        u64 id = 0;
        if (h && h != CUR_THREAD_HANDLE && (h >> 15) && svcGetThreadId(&id, h) == 0 && id == self) return h;
    }
    return 0;
}

void Stack(const char* name, void* thread, Handle handle, uint32_t seconds);
void Report(uint32_t seconds) {
    if (!osSuspendThread || !osResumeThread || !g_main || !g_mainHandle) { hw::Log("[hang] the game froze %u s ago (no frame); its thread context isn't readable here", seconds); return; }
    Stack("the main thread", g_main, g_mainHandle, seconds);
    void* other = g_other; Handle oh = g_otherHandle;
    if (other && oh) Stack(g_otherName, other, oh, seconds);
}
void Stack(const char* name, void* thread, Handle handle, uint32_t seconds) {
    osSuspendThread(thread);
    std::memset(&g_ctx, 0, sizeof g_ctx);
    Result rc = svcGetThreadContext3(&g_ctx, handle);
    osResumeThread(thread);
    if (rc != 0) { hw::Log("[hang] the game froze %u s ago (no frame); %s: svcGetThreadContext3 failed (%#x)", seconds, name, rc); return; }
    hw::Log("[hang] the game froze %u s ago (no frame): %s", seconds, name);
    Where("pc", g_ctx.pc.x); Where("lr", g_ctx.lr);
    uint64_t fp = g_ctx.fp;
    for (int i = 0; i < 16 && fp && !(fp & 7) && fp - g_ctx.sp < 0x100000; i++) {   /* frame records within 1 MB above sp */
        const uint64_t* rec = reinterpret_cast<const uint64_t*>(fp);
        if (!rec[1]) break;
        char what[16]; std::snprintf(what, sizeof what, "frame %d", i);
        Where(what, rec[1]);
        if (rec[0] <= fp) break;
        fp = rec[0];
    }
}

/* [perf] profile: in a local match every watchdog tick (50 ms) inside a TFGame.Update samples the main thread's pc, lr and frame chain into
 * a fixed table of stacks (kProf; the rest counted as "other"); ProfileReport logs the most sampled ones per [perf] window */
constexpr int kProf = 64, kProfDepth = 20;
struct ProfEntry { uint64_t a[kProfDepth]; int n, count; };
ProfEntry g_prof[kProf]; int g_profN = 0, g_profTotal = 0, g_profOther = 0;
volatile bool g_profOn = false;
bool ReadMain() {
    osSuspendThread(g_main);
    std::memset(&g_ctx, 0, sizeof g_ctx);
    Result rc = svcGetThreadContext3(&g_ctx, g_mainHandle);
    osResumeThread(g_main);
    return rc == 0;
}
void ProfSample() {
    if (!ReadMain()) return;
    ProfEntry e{}; e.a[e.n++] = g_ctx.pc.x; e.a[e.n++] = g_ctx.lr;
    uint64_t fp = g_ctx.fp;
    for (int i = 0; e.n < kProfDepth && fp && !(fp & 7) && fp - g_ctx.sp < 0x100000; i++) {
        const uint64_t* rec = reinterpret_cast<const uint64_t*>(fp);
        if (!rec[1]) break;
        e.a[e.n++] = rec[1];
        if (rec[0] <= fp) break;
        fp = rec[0];
    }
    g_profTotal++;
    for (int i = 0; i < g_profN; i++)
        if (g_prof[i].n == e.n && !std::memcmp(g_prof[i].a + 1, e.a + 1, sizeof(uint64_t) * (e.n - 1))) { g_prof[i].count++; return; }   /* (by the calling chain: lr and the frames; the pc the first one's) */
    if (g_profN < kProf) { e.count = 1; g_prof[g_profN++] = e; } else g_profOther++;
}
/* a TFGame.Update past 100 ms: the main thread's pc, lr and frame chain on one line, every 100 ms while it runs */
void Sample() {
    const uint64_t at = g_updAt;
    if (at && g_profOn && osSuspendThread && osResumeThread && g_main && g_mainHandle) ProfSample();
    if (!at || !osSuspendThread || !osResumeThread || !g_main || !g_mainHandle) return;
    const uint64_t now = svcGetSystemTick();
    if (now - at < kSlowTicks || now - g_lastSample < kSlowTicks) return;
    g_lastSample = now;
    if (g_samples >= kMaxSamples) { g_unsampled++; return; }
    osSuspendThread(g_main);
    std::memset(&g_ctx, 0, sizeof g_ctx);
    Result rc = svcGetThreadContext3(&g_ctx, g_mainHandle);
    osResumeThread(g_main);
    if (rc != 0) return;
    char line[512]; int n = 0;
    auto add = [&](uint64_t a) {
        const auto* m = exl::util::TryGetModule(a);
        const char* name = !m ? "?" : m == &exl::util::GetMainModuleInfo() ? "game" : m == &exl::util::GetSelfModuleInfo() ? "ours" : "sdk";
        if (n < (int)sizeof line - 40) n += std::snprintf(line + n, sizeof line - n, " %s+%#lx", name, (unsigned long)(a - (m ? m->m_Total.m_Start : 0)));
    };
    add(g_ctx.pc.x); add(g_ctx.lr);
    uint64_t fp = g_ctx.fp;
    for (int i = 0; i < 12 && fp && !(fp & 7) && fp - g_ctx.sp < 0x100000; i++) {
        const uint64_t* rec = reinterpret_cast<const uint64_t*>(fp);
        if (!rec[1]) break;
        add(rec[1]);
        if (rec[0] <= fp) break;
        fp = rec[0];
    }
    if (!std::strcmp(line, g_lastLine)) { g_same++; return; }
    SameFlush();
    std::memcpy(g_lastLine, line, sizeof line);
    g_samples++;
    hw::Log("[slow] sample %d, TFGame.Update running %lu ms: pc, lr, frames:%s", g_samples, (unsigned long)((now - at) / 19200), line);
}

void Thread(void*) {
    uint32_t last = 0, still = 0, ms = 0;
    for (;;) {
        svcSleepThread(50000000LL);   /* 50 ms: the slow-update sampler; the hang check below once a second */
        Sample();
        if ((ms += 50) < 1000) continue;
        ms = 0;
        uint32_t b = g_beat;
        if (b != last || b == 0) { last = b; still = 0; continue; }
        still++;
        if (still == 5 || (still > 5 && still % 30 == 0)) Report(still);
    }
}

bool g_started = false;
void Start() {   /* at the first frame: netws::StartThread's SDK functions are bound by then (net_main runs after xplay_main) */
    g_started = true;
    bool ok = netws::StartThread(Thread, nullptr, g_stack, sizeof g_stack);
    hw::Log("[hang] watchdog %s (main thread %s, its handle %s; suspend %s)", ok ? "started" : "NOT started", g_main ? "known" : "unknown",
            g_mainHandle ? "found" : "not found", osSuspendThread && osResumeThread ? "yes" : "no");
}
}  // namespace

void Beat() { if (!g_started) Start(); g_beat++; }

namespace {
uint64_t g_cpuAt = 0;
bool g_cpuOk = true;
uint64_t CpuTicks() {   /* the calling thread's run time on all cores: InfoType 25 since 13.0.0, 0xF0000002 before */
    uint64_t v = 0;
    if (!g_cpuOk) return 0;
    if (svcGetInfo(&v, static_cast<InfoType>(25), CUR_THREAD_HANDLE, UINT64_MAX) == 0) return v;
    if (svcGetInfo(&v, static_cast<InfoType>(0xF0000002), CUR_THREAD_HANDLE, UINT64_MAX) == 0) return v;
    g_cpuOk = false;
    hw::Log("[slow] the thread's CPU time isn't readable (svcGetInfo ThreadTickCount): slow updates log their wall time only");
    return 0;
}
}  // namespace

/* FrameShell's TFGame.Update in a netplay session (menus and loading have their own long updates), bracketed: past 100 ms the watchdog samples it; its end logs wall vs the main thread's CPU time (bounded) */
void UpdateBegin() { g_cpuAt = CpuTicks(); g_updAt = svcGetSystemTick(); }
void UpdateEnd() {
    const uint64_t at = g_updAt, now = svcGetSystemTick();
    g_updAt = 0;
    if (!at || now - at < kSlowTicks) return;
    SameFlush();   /* (the watchdog may sample once more as this runs: a rare stray line, harmless) */
    const uint64_t cpu = g_cpuOk ? CpuTicks() - g_cpuAt : 0;
    static int slow = 0; static double wallSum = 0, cpuSum = 0;
    slow++; wallSum += (now - at) / 19200.0; cpuSum += cpu / 19200.0;
    if (slow <= 30 || slow % 50 == 0)
        hw::Log("[slow] TFGame.Update #%d over 100 ms: %.0f ms, the main thread ran %.0f ms of it%s (so far %d slow: %.0f ms, ran %.0f; samples logged %d, not logged %d)",
                slow, (now - at) / 19200.0, cpu / 19200.0, g_cpuOk ? "" : " (unknown)", slow, wallSum, cpuSum, g_samples, g_unsampled);
}

void SetProfile(bool on) { g_profOn = on; }
/* the most sampled stacks since the last report (at most 6 lines, each over 3% of the samples), then reset */
void ProfileReport(bool log) {
    const int total = g_profTotal;
    if (!log || total < 10) { g_profN = g_profTotal = g_profOther = 0; return; }
    for (int k = 0; k < 6; k++) {
        int best = -1;
        for (int i = 0; i < g_profN; i++) if (g_prof[i].count > 0 && (best < 0 || g_prof[i].count > g_prof[best].count)) best = i;
        if (best < 0 || g_prof[best].count * 100 < total * 3) break;
        char line[640]; int n = 0;
        for (int j = 0; j < g_prof[best].n; j++) {
            const uint64_t a = g_prof[best].a[j];
            const auto* m = exl::util::TryGetModule(a);
            const char* name = !m ? "?" : m == &exl::util::GetMainModuleInfo() ? "game" : m == &exl::util::GetSelfModuleInfo() ? "ours" : "sdk";
            if (n < (int)sizeof line - 40) n += std::snprintf(line + n, sizeof line - n, " %s+%#lx", name, (unsigned long)(a - (m ? m->m_Total.m_Start : 0)));
        }
        hw::Log("[perf]   %d%% of %d samples in updates:%s", g_prof[best].count * 100 / total, total, line);
        g_prof[best].count = -g_prof[best].count;   /* (taken) */
    }
    if (g_profOther) hw::Log("[perf]   (%d samples past the %d-stack table)", g_profOther, kProf);
    g_profN = g_profTotal = g_profOther = 0;
}

/* the calling thread is reported with the main thread while watched (Unwatch ends it); one at a time */
void Watch(const char* name) {
    if (!osGetCurrentThread) return;
    void* t = osGetCurrentThread();
    g_otherHandle = 0; g_otherName = name; g_other = t;
    g_otherHandle = HandleOfCurrent(t);
}
void Unwatch() { g_other = nullptr; g_otherHandle = 0; }

/* on the main thread at boot (xplay_main): it is the thread the watchdog reads */
void Install(FindFn find) {
    osGetCurrentThread = reinterpret_cast<void* (*)()>(find("_ZN2nn2os16GetCurrentThreadEv"));
    osSuspendThread = reinterpret_cast<int32_t (*)(void*)>(find("_ZN2nn2os13SuspendThreadEPNS0_10ThreadTypeE"));
    osResumeThread = reinterpret_cast<int32_t (*)(void*)>(find("_ZN2nn2os12ResumeThreadEPNS0_10ThreadTypeE"));
    /* each in the SDK's module, or not used (a name found elsewhere isn't the SDK's function) */
    auto sdk = [](const void* f) {
        const auto* m = exl::util::TryGetModule(reinterpret_cast<uintptr_t>(f));
        return f && m && m != &exl::util::GetMainModuleInfo() && m != &exl::util::GetSelfModuleInfo();
    };
    const void* fs[3] = { reinterpret_cast<const void*>(osGetCurrentThread), reinterpret_cast<const void*>(osSuspendThread),
                          reinterpret_cast<const void*>(osResumeThread) };
    for (int i = 0; i < 3; i++) if (fs[i] && !sdk(fs[i])) Where(i == 0 ? "GetCurrentThread (not the SDK's: unused)" : i == 1 ? "SuspendThread (not the SDK's: unused)" : "ResumeThread (not the SDK's: unused)", reinterpret_cast<uint64_t>(fs[i]));
    if (!sdk(reinterpret_cast<const void*>(osGetCurrentThread))) osGetCurrentThread = nullptr;
    if (!sdk(reinterpret_cast<const void*>(osSuspendThread)) || !sdk(reinterpret_cast<const void*>(osResumeThread))) osSuspendThread = nullptr, osResumeThread = nullptr;
    g_main = osGetCurrentThread ? osGetCurrentThread() : nullptr;
    g_mainHandle = HandleOfCurrent(g_main);
}
}  // namespace hangwatch
