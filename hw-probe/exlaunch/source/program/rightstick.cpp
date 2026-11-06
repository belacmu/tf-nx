/* The controllers' right stick, for TF.EX's XGamePadInputExtensions.GetRightStick (its right-stick shot, the input's aim_right_axis to GGRS).
 *
 * TFEX-DIFF S-input: the PC reads the right stick of its gamepad (XGamepadData.GetRightStick). The Switch's game never reads one: its
 * JoyConInput takes only the left stick. Every frame the game polls nn::hid::GetNpadState for each controller with the state type of
 * that controller's style (Pro: FullKey, a pair: JoyDual, handheld: Handheld, a single Joy-Con: JoyLeft / JoyRight); each of those states
 * carries both sticks. These hooks on the game's import slots keep each npad id's last right stick; a JoyConInput (its index at +0x20)
 * reads the freshest state with a right stick polled for that index (handheld: npad id 0x20, controller 1's). The game polls one npad id
 * in more than one style in the same frame (console logs: a pair's id 0 as JoyDual, then as JoyLeft / JoyRight), so a single Joy-Con
 * poll doesn't hide a right stick polled for that id within the last 100 ms (the AI controls on archer select were dead on a pair).
 * A single Joy-Con alone has one stick, its move stick: no right stick, so the right-stick shot is off there, as on a PC pad without
 * one. Values as Monocle's: -1..1, Y down. */
#include "lib.hpp"
#include "hwlog.hpp"
#include "tfex/runtime/tfexgen_code_rt.hpp"
#include <cstdint>


namespace rightstick {
namespace {
struct NpadState { int64_t samplingNumber; uint64_t buttons; int32_t lx, ly, rx, ry; uint32_t attributes; uint32_t reserved; };
using GetFn = void (*)(NpadState*, const uint32_t&);
struct Last { uint64_t tick; int32_t rx, ry; uint64_t buttons; bool connected; };
Last g_right[0x21];        /* per npad id (0..7: No1..No8, 0x20: Handheld): its last poll in a style with a right stick */
int g_seen[5];             /* polls per state type, for the log */
int g_connLogged[5][2];    /* the connected attribute per state type, logged once each way (console evidence) */
uint64_t Now() { uint64_t t; __asm__ __volatile__("mrs %0, cntpct_el0" : "=r"(t)); return t; }   /* nn::os::GetSystemTick's counter */
uint64_t Fresh() { return 19200000 / 10; }   /* 100 ms of the Switch's 19.2 MHz system tick */
const char* const kNames[] = { "Pro (FullKey)", "pair (JoyDual)", "handheld", "single Joy-Con L", "single Joy-Con R" };

void Record(uint32_t id, const NpadState* s, bool hasRight, int kind) {
    if (id > 0x20 || (id > 7 && id != 0x20)) return;
    bool connected = (s->attributes & 1) != 0;   /* nn::hid::NpadAttribute::IsConnected */
    if (g_seen[kind]++ == 0)
        hw::Log("[input] right stick: the game polls %s states (npad id %u)%s", kNames[kind], (unsigned)id, hasRight ? "" : ": no right stick there");
    if (!g_connLogged[kind][connected]++)
        hw::Log("[input] right stick: a %s poll %s (npad id %u)", kNames[kind], connected ? "connected" : "not connected", (unsigned)id);
    if (!hasRight) return;   /* a single Joy-Con style: it doesn't replace a right stick polled for this id */
    if (!connected && g_right[id].connected && Now() - g_right[id].tick < Fresh()) return;   /* a connected one this frame wins */
    g_right[id] = Last{ Now(), s->rx, s->ry, s->buttons, connected };
}

/* the game's import slots (main.flat's jump-slot relocations; FullKey is xplay.cpp's own hook, which calls RecordFullKey) */
constexpr uintptr_t kSlotJoyDual = 0x4722818, kSlotHandheld = 0x4722828, kSlotJoyLeft = 0x4722838, kSlotJoyRight = 0x4722850;
GetFn g_joyDual, g_handheld, g_joyLeft, g_joyRight;
void JoyDualHook(NpadState* out, const uint32_t& id) { g_joyDual(out, id); Record(id, out, true, 1); }
void HandheldHook(NpadState* out, const uint32_t& id) { g_handheld(out, id); Record(id, out, true, 2); }
void JoyLeftHook(NpadState* out, const uint32_t& id) { g_joyLeft(out, id); Record(id, out, false, 3); }
void JoyRightHook(NpadState* out, const uint32_t& id) { g_joyRight(out, id); Record(id, out, false, 4); }

uintptr_t (*g_find)(const char*) = nullptr;
bool Patch(uintptr_t slot, const char* sym, GetFn& orig, GetFn hook) {
    orig = reinterpret_cast<GetFn>(g_find(sym));
    if (!orig) return false;
    *reinterpret_cast<volatile uintptr_t*>(exl::util::GetMainModuleInfo().m_Total.m_Start + slot) = reinterpret_cast<uintptr_t>(hook);
    return true;
}
} // namespace

void RecordFullKey(const void* state, uint32_t id) { Record(id, static_cast<const NpadState*>(state), true, 0); }

void Install(uintptr_t (*findExport)(const char*)) {
    g_find = findExport;
    int n = Patch(kSlotJoyDual, "_ZN2nn3hid12GetNpadStateEPNS0_16NpadJoyDualStateERKj", g_joyDual, &JoyDualHook)
          + Patch(kSlotHandheld, "_ZN2nn3hid12GetNpadStateEPNS0_17NpadHandheldStateERKj", g_handheld, &HandheldHook)
          + Patch(kSlotJoyLeft, "_ZN2nn3hid12GetNpadStateEPNS0_16NpadJoyLeftStateERKj", g_joyLeft, &JoyLeftHook)
          + Patch(kSlotJoyRight, "_ZN2nn3hid12GetNpadStateEPNS0_17NpadJoyRightStateERKj", g_joyRight, &JoyRightHook);
    hw::Log("[input] right stick: %d of 4 npad state imports hooked (FullKey: xplay.cpp's)%s", n, n == 4 ? "" : " (ERROR: no right stick on those)");
}

namespace {
/* the freshest state with a right stick polled for this input's controller within 100 ms, else none (a single Joy-Con) */
const Last* LastOf(void* joyConInput) {
    if (!joyConInput) return nullptr;
    int32_t index = *reinterpret_cast<int32_t*>(reinterpret_cast<uintptr_t>(joyConInput) + 0x20);
    if (index < 0 || index > 7) return nullptr;
    uint64_t now = Now(), fresh = Fresh();
    auto ok = [&](const Last& l) { return l.tick && now - l.tick < fresh; };
    const Last* best = ok(g_right[index]) ? &g_right[index] : nullptr;
    if (index == 0 && ok(g_right[0x20])) {   /* handheld: controller 1; the connected one of the two, else the fresher */
        const Last& h = g_right[0x20];
        if (!best || (h.connected && !best->connected) || (h.connected == best->connected && h.tick > best->tick)) best = &h;
    }
    return best;
}
}
/* the right stick of a JoyConInput: -1..1 each axis, Y down (Monocle's XGamepadData.GetRightStick) */
tfex::gen::Vector2 Of(void* joyConInput) {
    const Last* best = LastOf(joyConInput);
    if (!best) return {0.0f, 0.0f};
    return { (float)best->rx / 32767.0f, -(float)best->ry / 32767.0f };
}
/* a full controller (Pro, a Joy-Con pair, handheld): one with a right stick; a single Joy-Con has none */
bool Full(void* joyConInput) { return LastOf(joyConInput) != nullptr; }
/* its npad buttons (nn::hid::NpadButton bits: 5 = the right stick's click) */
uint64_t Buttons(void* joyConInput) { const Last* best = LastOf(joyConInput); return best ? best->buttons : 0; }
} // namespace rightstick
