/* Controller rumble during a lobby match (docs/ws-rumble.md).
 *
 * The game rumbles `TFGame.PlayerInputs[seat].Rumble(strength, frames)` for a player's own events (hit, death, catch, chest...) and
 * MenuInput.RumbleAll / RumblePlayers for everyone. On the Switch every PlayerInput is a TowerFall.JoyConInput, whose Rumble is the
 * thunk `this.JoyCon.Rumble(strength, frames)` (Monocle.MInput+JoyConData: strength *= the vibration setting; if it is stronger
 * than lastRumble +0x84: lastRumble = it, rumbleCounter +0x88 (Monocle.Counter, value float +0x24) = frames, GamePad.SetVibration
 * now; equal: the counter SetMax; weaker: nothing). JoyConData.Update (MInput.Update, once per engine frame, outside Level.Update)
 * counts the counter down and stops the motor when it reaches 0. Three things went wrong in a lobby match:
 *   1. The lobby start puts the console's controller object in every empty seat (xplay.cpp, TF.EX's EnsureRemoteController), so
 *      the remote archers' events rumbled this controller.
 *   2. Rollback: resimulated ticks rumbled again (a late hit felt twice, or felt long after it happened), and a rumble whose event a
 *      rollback undid kept going.
 *   3. The JoyConData object is in the GC heap, which a rollback restores: the restored rumble timer was the one of the frame rolled
 *      back to (0 while the motor runs: nothing ever stops it, the pad buzzes until the next rumble ends), and its input state too
 *      (the next frame's "pressed" compares against a button state several frames old).
 * The fix: the local seat keeps the controller object; every other seat that held it gets a controller object of its own, made by
 * the game's own constructor in the GC heap (`new JoyConInput(index)` with the same JoyCon index, as TFGame makes them: the same
 * JoyConData, so reads behave exactly as before), whose rumble goes nowhere. (The first version put a byte copy of the object in
 * the module's .bss there; the console hung once right after a remote archer's death was dropped, so nothing outside the heap now.) JoyConInput.Rumble / StopRumble are replaced:
 * offline they do what the thunks did; in a match only the local object reaches the motor, and inside Level.Update a rumble is
 * remembered per frame (label, strength, frames): a resimulated tick's repeat is dropped, a new one (an event only the rollback
 * found) starts with the frames already gone subtracted, and one that the resimulation did not repeat is cancelled at the end of the
 * real frame (Synchronize).
 *
 * Switch 1.4.1.6: JoyConInput.Rumble +0xa62390 / StopRumble +0xa623a0 (vtable +0x470b830 slots 0x28 / 0x30; `ldr x0, [x0, #0x18];
 * b JoyConData.X`, 16 bytes each), JoyConData.Rumble +0x93e9b0 / StopRumble +0x93e840 (no other game callers), JoyConInput size
 * 0xb8 (JoyCon +0x18, JoyConIndex +0x20), .ctor(this, index) +0xa61690 (this null: allocates and returns it), JoyConData size 0x90, Counter.value +0x24 (Set +0x922780), TFGame.PlayerInputs slot +0x4791fa8. */
#include "lib.hpp"
#include <lib/util/sys/modules.hpp>

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "hwlog.hpp"
#include "rumble.hpp"

namespace rumble {

static uintptr_t Base() { return exl::util::GetMainModuleInfo().m_Total.m_Start; }

constexpr uintptr_t kJciCtor = 0xa61690, kJciRumble = 0xa62390, kJciStop = 0xa623a0, kJcdRumble = 0x93e9b0, kJcdStop = 0x93e840, kPlayerInputs = 0x4791fa8;
constexpr int kJciJoyCon = 0x18, kJciIndex = 0x20, kJcdCounter = 0x88, kCounterValue = 0x24;
constexpr int kSeats = 8;

static void JcdRumble(void* d, float s, int n) { reinterpret_cast<void (*)(void*, float, int)>(Base() + kJcdRumble)(d, s, n); }
static void JcdStop(void* d) { reinterpret_cast<void (*)(void*)>(Base() + kJcdStop)(d); }
static void* JoyCon(void* jci) { return *reinterpret_cast<void**>(reinterpret_cast<uintptr_t>(jci) + kJciJoyCon); }

static void* g_other[kSeats];     /* the other seats' controller objects MatchStart made (referenced by PlayerInputs: GC-safe) */
static void* g_pad = nullptr;     /* the console's controller object, in the local seat */
static int g_seat = -1;
static uint32_t g_copied = 0;     /* seats whose slot holds g_other[seat] (it held g_pad before) */
static bool g_on = false;
static bool g_enabled = true;     /* hw.txt "rumblefix 0": the old behaviour (every seat rumbles this controller, rollback restores it) */

/* Simulation context and the rumbles of the recent frames */
static bool g_inLevel = false, g_resim = false;
static int g_label = 0, g_latest = 0;   /* the Level.Update running; the last real (first-time) frame's label */
struct Entry { int f; float s; int n; bool stale; };
constexpr int kEntries = 32, kKeepFrames = 64;
static Entry g_ent[kEntries];
static int g_nEnt = 0, g_logged = 0;
static uint32_t g_fired, g_dups, g_remote, g_late, g_cancelled;


#define RLOG(...) do { if (g_logged < 300) { g_logged++; hw::Log(__VA_ARGS__); } } while (0)

static uintptr_t* Slots(int* n) {
    uintptr_t fi = *reinterpret_cast<uintptr_t*>(Base() + kPlayerInputs);
    uintptr_t ai = fi ? *reinterpret_cast<uintptr_t*>(fi) : 0;
    *n = ai ? **reinterpret_cast<int32_t**>(ai + 0x20) : 0;
    return ai ? *reinterpret_cast<uintptr_t**>(ai + 0x10) : nullptr;
}
static int SeatOf(void* self) {
    if (self == g_pad) return g_seat;
    for (int k = 0; k < kSeats; k++) if (g_other[k] && self == g_other[k]) return k;
    int n = 0;
    uintptr_t* di = Slots(&n);   /* another controller of this console in a remote seat (Ryujinx connects every pad) */
    for (int k = 0; di && k < n; k++) if (di[k] == reinterpret_cast<uintptr_t>(self)) return k;
    return -1;
}

void MatchStart(int localSeat, int players) {
    g_on = false; g_pad = nullptr; g_copied = 0; g_nEnt = 0; g_logged = 0; g_latest = 0;
    g_fired = g_dups = g_remote = g_late = g_cancelled = 0;
    for (auto& o : g_other) o = nullptr;
    if (!g_enabled) { hw::Log("[rumble] match start: rumblefix 0: rumble untouched"); return; }
    int n = 0;
    uintptr_t* di = Slots(&n);
    if (!di || localSeat < 0 || localSeat >= n || !di[localSeat]) { hw::Log("[rumble] match start: no controller in seat %d: rumble untouched", localSeat); return; }
    g_pad = reinterpret_cast<void*>(di[localSeat]);
    g_seat = localSeat;
    for (int k = 0; k < n && k < players && k < kSeats; k++) {
        if (k == localSeat || di[k] != reinterpret_cast<uintptr_t>(g_pad)) continue;
        int index = *reinterpret_cast<int32_t*>(reinterpret_cast<uintptr_t>(g_pad) + kJciIndex);
        void* o = reinterpret_cast<void* (*)(void*, int)>(Base() + kJciCtor)(nullptr, index);   /* new JoyConInput(index) */
        if (!o || JoyCon(o) != JoyCon(g_pad) || *reinterpret_cast<uintptr_t*>(o) != *reinterpret_cast<uintptr_t*>(g_pad)) {
            hw::Log("[rumble] match start: new JoyConInput(%d) for seat %d came out wrong (%p): the seat keeps the controller", index, k, o);
            continue;
        }
        g_other[k] = o;
        di[k] = reinterpret_cast<uintptr_t>(o);
        g_copied |= 1u << k;
    }
    g_on = true;
    hw::Log("[rumble] match start: seat %d keeps the controller %p, seats %#x get a controller object of their own without rumble", localSeat, g_pad, g_copied);
}

void MatchEnd() {
    if (!g_pad) return;
    JcdStop(JoyCon(g_pad));
    int n = 0;
    uintptr_t* di = Slots(&n);
    for (int k = 0; di && k < n && k < kSeats; k++)
        if ((g_copied & (1u << k)) && di[k] == reinterpret_cast<uintptr_t>(g_other[k])) di[k] = reinterpret_cast<uintptr_t>(g_pad);
    hw::Log("[rumble] match end: stopped; fired %u, resimulated repeats dropped %u, other seats dropped %u, started late %u, cancelled %u",
            g_fired, g_dups, g_remote, g_late, g_cancelled);
    g_on = false; g_pad = nullptr; g_copied = 0; g_nEnt = 0; g_inLevel = false;
    for (auto& o : g_other) o = nullptr;
}

void LevelUpdateBegin(int label, bool resim) {
    if (!g_on) return;
    g_inLevel = true; g_resim = resim; g_label = label;
    if (!resim && label > g_latest) {
        g_latest = label;
        int w = 0;
        for (int i = 0; i < g_nEnt; i++) if (g_ent[i].f > label - kKeepFrames) g_ent[w++] = g_ent[i];
        g_nEnt = w;
    }
}
void LevelUpdateEnd() { g_inLevel = false; }

void OnLoad(int to) {
    if (!g_on) return;
    for (int i = 0; i < g_nEnt; i++) if (g_ent[i].f > to) g_ent[i].stale = true;   /* frame f's update (label f + 1) runs again */
}

void Synchronize(int label) {
    if (!g_on || !g_pad) return;
    bool stopRunning = false;
    int w = 0;
    for (int i = 0; i < g_nEnt; i++) {
        const Entry& e = g_ent[i];
        if (!e.stale) { g_ent[w++] = e; continue; }
        g_cancelled++;
        bool running = e.f + e.n > label;
        RLOG("[rumble] seat %d frame %d strength %.2f frames %d: cancelled by a rollback%s", g_seat, e.f, e.s, e.n, running ? " (stopped)" : "");
        if (running) stopRunning = true;
    }
    g_nEnt = w;
    if (!stopRunning) return;
    void* d = JoyCon(g_pad);
    JcdStop(d);
    const Entry* best = nullptr;   /* what should still be running */
    for (int i = 0; i < g_nEnt; i++) {
        const Entry& e = g_ent[i];
        if (e.f + e.n > label && (!best || e.s > best->s)) best = &e;
    }
    if (best) JcdRumble(d, best->s, best->f + best->n - label);
}

static void Request(void* self, float s, int n) {
    int seat = SeatOf(self);
    if (self != g_pad) {
        g_remote++;
        RLOG("[rumble] seat %d frame %d%s strength %.2f frames %d: not this console's seat, dropped", seat, g_label, g_resim ? " (resim)" : "", s, n);
        return;
    }
    void* d = JoyCon(self);
    if (!g_inLevel) { g_fired++; JcdRumble(d, s, n); return; }   /* outside the simulation (menus): as offline */
    for (int i = 0; i < g_nEnt; i++) {
        Entry& e = g_ent[i];
        if (e.f == g_label && e.s == s && e.n == n) {
            if (e.stale) {   /* the resimulation confirms a rumble already felt */
                e.stale = false; g_dups++;
                RLOG("[rumble] seat %d frame %d (resim) strength %.2f frames %d: already felt, dropped", seat, g_label, s, n);
                return;
            }
            return;                                            /* the same rumble twice in one frame: the motor takes the max anyway */
        }
    }
    int left = g_resim ? n - (g_latest + 1 - g_label) : n;
    if (g_nEnt < kEntries) g_ent[g_nEnt++] = Entry{ g_label, s, n, false };
    RLOG("[rumble] seat %d frame %d%s strength %.2f frames %d%s", seat, g_label, g_resim ? " (resim)" : "", s, n,
         left <= 0 ? ": already over, skipped" : g_resim ? ": new after a rollback, started late" : "");
    if (left <= 0) return;
    if (g_resim) g_late++;
    g_fired++;
    JcdRumble(d, s, left);
}

HOOK_DEFINE_REPLACE(JciRumble) {
    static void Callback(void* self, float strength, int frames) {
        if (!g_on) { JcdRumble(JoyCon(self), strength, frames); return; }
        Request(self, strength, frames);
    }
};
HOOK_DEFINE_REPLACE(JciStopRumble) {
    static void Callback(void* self) {
        if (g_on && (self != g_pad || (g_inLevel && g_resim))) return;   /* another seat's stop would stop this console's rumble */
        JcdStop(JoyCon(self));
    }
};


void SetEnabled(bool on) { g_enabled = on; }

static bool IsThunk(uintptr_t at, uintptr_t target) {
    const uint32_t* w = reinterpret_cast<const uint32_t*>(Base() + at);
    uint32_t b = 0x14000000u | (static_cast<uint32_t>((static_cast<int64_t>(target) - static_cast<int64_t>(at + 4)) >> 2) & 0x03ffffffu);
    return w[0] == 0xf9400c00u && w[1] == b;   /* ldr x0, [x0, #0x18]; b target */
}
void Install() {
    if (!IsThunk(kJciRumble, kJcdRumble) || !IsThunk(kJciStop, kJcdStop)) { hw::Log("[rumble] JoyConInput.Rumble isn't as expected: rumble hooks NOT installed"); return; }
    JciRumble::InstallAtOffset(kJciRumble);
    JciStopRumble::InstallAtOffset(kJciStop);
}

}
