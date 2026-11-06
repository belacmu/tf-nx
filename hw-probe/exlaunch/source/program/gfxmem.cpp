/* More command memory for the game's NVN command buffer when a frame needs it (the 240 Hz abort, STATUS.md "240 Hz blocker").
 *
 * The game's graphics layer (FNA3D over NVN, device object D) records every draw into one command buffer, D+0x51a8. At init it gives
 * it 256 KiB of command memory (pool D+0x5190, offset D+0x5198) and 256 KiB of control memory (D+0x51a0), and after each flush (queue
 * submit, then a wait on the sync D+0x5278 with no timeout: +0x1f300 mid-frame, +0x20d60 at present) it gives the same memory back.
 * It never asks how much is free (nvnCommandBufferGetCommandMemoryFree has no caller) and registers no memory callback
 * (nvnCommandBufferSetMemoryCallback is only loaded), so a frame that records more than 256 KiB makes the driver abort inside
 * whichever command comes next: DrawElementsBaseVertex from the SpriteBatcher (+0x21b18, console 2026-10-29 07:57, a 240 Hz match),
 * SetRenderTargets (+0x1f298, smoke team), ClearColor (+0x1fdf8, smoke spectate at 240 Hz). At 60 Hz one game update runs per
 * present; at 240 Hz four do, and a rollback resimulates more, and the game and TF.EX draw in updates (LightingLayer.Update,
 * PlayerPatch.RefreshSprites), so a frame's commands can pass the 256 KiB.
 *
 * The fix is NVN's own mechanism: a memory callback on that command buffer that adds a chunk from a fixed pool when the driver runs
 * out, and the pool starts over at each of the game's flushes (the GPU is done with it then: the game waited on its sync). Chunks: kN
 * of command memory from the game's own GPU memory allocator (+0x1e030, the flags it uses for its own command memory) and kN of
 * control memory. Running out of chunks is a pools error, logged before the driver aborts.
 *
 * A frame's commands have no bound, though: TF.EX's accumulator (its TFGame.Update prefix) runs every advance a stall owes in one frame,
 * and each advance draws (PlayerPatch.RefreshSprites), so after a stall a frame needed more than 16 chunks (smoke team 080603: the
 * driver aborted mid-match). On the PC the driver grows its memory. Here FlushIfLow, after each Level.Update (no draw batch open), runs
 * the game's own mid-frame flush (+0x1f6e0, which the game calls around its texture reads: submit, wait on the sync, the command buffer's
 * own memory back, the render state marked for re-applying) once half the chunks are in use: the pool then covers one update, not a
 * frame. +0x1f6e0 reads only the device at its argument's +0x238 (then D everywhere), so it gets a stand-in holding D.
 *
 * Switch 1.4.1.6 (main): NVN function pointers loaded by +0x1bxxx into globals reached through these GOT slots (the loader's
 * nvnDeviceGetProcAddress names): SetMemoryCallback 0x47239c0, SetMemoryCallbackData 0x47239c8, AddCommandMemory 0x47239d8,
 * AddControlMemory 0x47239e0, GetCommandMemoryFree 0x47239f8, GetControlMemoryFree 0x4723a10. Hook sites are the `mov w2, #0x40000`
 * before each AddControlMemory: +0x1ec58 (init: x22 = the command buffer), +0x1f7cc and +0x20fbc (the two flushes). */
#include "lib.hpp"
#include "hwlog.hpp"
#include "pools.hpp"
#include <cstdint>
#include <cstdlib>
#include <lib/util/sys/modules.hpp>

namespace gfxmem {
namespace {

uintptr_t Base() { return exl::util::GetMainModuleInfo().m_Total.m_Start; }
template<typename F> F Nvn(uintptr_t got) { return *reinterpret_cast<F*>(*reinterpret_cast<uintptr_t*>(Base() + got)); }

using MemoryCallback = void (*)(void* cmdBuf, int event, size_t minSize, void* data);
using SetCallbackFn = void (*)(void* cmdBuf, MemoryCallback cb);
using SetDataFn = void (*)(void* cmdBuf, void* data);
using AddCommandFn = void (*)(void* cmdBuf, const void* pool, ptrdiff_t offset, size_t size);
using AddControlFn = void (*)(void* cmdBuf, void* memory, size_t size);
using GetFreeFn = size_t (*)(const void* cmdBuf);
using GpuAllocFn = void (*)(size_t size, size_t align, int flags, void** pool, ptrdiff_t* offset);

constexpr uintptr_t kSetCallback = 0x47239c0, kSetData = 0x47239c8, kAddCommand = 0x47239d8, kAddControl = 0x47239e0,
                    kCommandFree = 0x47239f8, kControlFree = 0x4723a10;
constexpr uintptr_t kGpuAlloc = 0x1e030;
constexpr uintptr_t kInitSite = 0x1ec58, kFlushSite = 0x1f7cc, kPresentSite = 0x20fbc;
constexpr uint32_t kMovW2_40000 = 0x320e03e2;   /* mov w2, #0x40000 (orr w2, wzr, #0x40000) */
constexpr int kGameCommandFlags = 0x22;         /* what +0x1ec14 passes for the game's own command memory */

constexpr int kN = 16;                          /* chunks of each kind per frame (between two flushes) */
constexpr size_t kCommandChunk = 0x40000;       /* 256 KiB, the game's own size */
constexpr size_t kControlChunk = 0x20000;

void* g_pool = nullptr;                         /* the NVN memory pool of the command chunks */
ptrdiff_t g_poolOffset = 0;
uint8_t* g_control = nullptr;
int g_cmdUsed = 0, g_ctlUsed = 0;               /* chunks given in this frame */
int g_cmdPeak = 0, g_ctlPeak = 0;
uint64_t g_adds = 0;                            /* chunks given in all */
void* g_device = nullptr;                       /* D, the graphics device (FlushIfLow) */
alignas(8) uint8_t g_flushArg[0x240];           /* the stand-in argument of the game's flush: D at +0x238 */
uint64_t g_flushes = 0;
bool g_flushOk = false;
int g_flushAt = kN / 2;                         /* chunks in use that make FlushIfLow flush (hw.txt gfxflushat: tests) */

void OnOutOfMemory(void* cmdBuf, int event, size_t minSize, void*) {
    /* Which memory ran out from the free counts, not the event's value (0 command, 1 control in NVN's headers; logged to confirm). */
    size_t cmdFree = Nvn<GetFreeFn>(kCommandFree)(cmdBuf), ctlFree = Nvn<GetFreeFn>(kControlFree)(cmdBuf);
    bool cmd = cmdFree < minSize ? true : ctlFree < minSize ? false : event == 0;
    if (cmd ? minSize > kCommandChunk : minSize > kControlChunk) {
        pools::Exhausted(cmd ? "gfx command chunks" : "gfx control chunks", kN, "a request bigger than a chunk: the driver aborts next");
        return;
    }
    if (cmd) {
        if (g_cmdUsed == kN || !g_pool) { pools::Exhausted("gfx command chunks", kN, "no chunk left in this frame: the driver aborts next"); return; }
        Nvn<AddCommandFn>(kAddCommand)(cmdBuf, g_pool, g_poolOffset + (ptrdiff_t)(g_cmdUsed * kCommandChunk), kCommandChunk);
        if (++g_cmdUsed > g_cmdPeak) g_cmdPeak = g_cmdUsed;
    } else {
        if (g_ctlUsed == kN || !g_control) { pools::Exhausted("gfx control chunks", kN, "no chunk left in this frame: the driver aborts next"); return; }
        Nvn<AddControlFn>(kAddControl)(cmdBuf, g_control + g_ctlUsed * kControlChunk, kControlChunk);
        if (++g_ctlUsed > g_ctlPeak) g_ctlPeak = g_ctlUsed;
    }
    if (++g_adds <= 8)
        hw::Log("[gfx] the frame's %s memory ran out (event %d, wants %zu, free %zu / %zu): chunk %d of %d added (%llu so far; first 8 logged)",
                cmd ? "command" : "control", event, minSize, cmdFree, ctlFree, cmd ? g_cmdUsed : g_ctlUsed, kN, (unsigned long long)g_adds);
}

HOOK_DEFINE_INLINE(InitHook) {
    static void Callback(exl::hook::InlineCtx* ctx) {
        void* cmdBuf = reinterpret_cast<void*>(ctx->X[22]);
        g_device = reinterpret_cast<void*>(ctx->X[19]);   /* (D: the init stores D+0x51a0 through x19 just before) */
        *reinterpret_cast<void**>(g_flushArg + 0x238) = g_device;
        reinterpret_cast<GpuAllocFn>(Base() + kGpuAlloc)(kN * kCommandChunk, 0x1000, kGameCommandFlags, &g_pool, &g_poolOffset);
        g_control = static_cast<uint8_t*>(std::aligned_alloc(0x1000, kN * kControlChunk));
        Nvn<SetCallbackFn>(kSetCallback)(cmdBuf, &OnOutOfMemory);
        Nvn<SetDataFn>(kSetData)(cmdBuf, nullptr);
        hw::Log("[gfx] command buffer memory callback set: up to %d extra chunks a frame of %zu KiB command (pool %s) and %zu KiB control (%s)",
                kN, kCommandChunk / 1024, g_pool ? "ok" : "NOT ALLOCATED", kControlChunk / 1024, g_control ? "ok" : "NOT ALLOCATED");
    }
};

/* The game has waited on its sync and given the command buffer its own memory again: every chunk is free. */
HOOK_DEFINE_INLINE(FlushHook) {
    static void Callback(exl::hook::InlineCtx*) { g_cmdUsed = g_ctlUsed = 0; }
};

constexpr uintptr_t kGameFlush = 0x1f6e0;
} // namespace

/* after a Level.Update: the game's own flush once half the chunks are in use (above) */
void FlushIfLow() {
    if (!g_flushOk || !g_device || (g_cmdUsed < g_flushAt && g_ctlUsed < g_flushAt)) return;
    const int cmd = g_cmdUsed, ctl = g_ctlUsed;
    reinterpret_cast<int32_t (*)(void*)>(Base() + kGameFlush)(g_flushArg);   /* (its flush site resets the chunk counts: FlushHook) */
    if (++g_flushes <= 4 || g_flushes % 100 == 0)
        hw::Log("[gfx] mid-frame flush %llu (the game's +0x1f6e0) after a Level.Update with %d command / %d control chunks in use (of %d)%s",
                (unsigned long long)g_flushes, cmd, ctl, kN, g_flushes == 4 ? "; from now on every 100th logged" : "");
}

void Install() {
    const uintptr_t sites[] = { kInitSite, kFlushSite, kPresentSite };
    for (uintptr_t s : sites) {
        if (*reinterpret_cast<uint32_t*>(Base() + s) != kMovW2_40000) {
            hw::Log("[gfx] the command buffer's memory setup isn't as expected (+%#lx): no memory callback (a busy frame aborts)", (unsigned long)s);
            return;
        }
    }
    if (*reinterpret_cast<uint32_t*>(Base() + kGameFlush) != 0xf81c0ff7u) {   /* str x23, [sp, #-0x40]! */
        hw::Log("[gfx] the game's flush (+%#lx) isn't as expected: no mid-frame flush (a long catch-up frame aborts)", (unsigned long)kGameFlush);
    } else g_flushOk = true;
    InitHook::InstallAtOffset(kInitSite);
    FlushHook::InstallAtOffset(kFlushSite);
    FlushHook::InstallAtOffset(kPresentSite);
    pools::Register("gfx command chunks", [](pools::Use* u) { u->used = g_cmdUsed; u->cap = kN; u->peak = g_cmdPeak; });
    pools::Register("gfx control chunks", [](pools::Use* u) { u->used = g_ctlUsed; u->cap = kN; u->peak = g_ctlPeak; });
    hw::Log("[gfx] command buffer memory: hooks installed (callback set at the game's graphics init)");
}


} // namespace gfxmem

void GfxSetFlushAt(int n) { if (n >= 1 && n <= gfxmem::kN) { gfxmem::g_flushAt = n; hw::Log("[gfx] hw.txt gfxflushat %d: the mid-frame flush at %d chunks in use", n, n); } }
