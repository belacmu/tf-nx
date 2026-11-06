/* Version gap: hot coals bounce players on the PC (TF.EX's build), not on the Switch (1.4.1.6).
 *
 * The PC: Player.Update sets onHotCoals (= OnGround && CollideCheck(HotCoals, Position + UnitY)); NormalUpdate, DuckingUpdate and
 * DodgingUpdate then call HotCoalsBounce() (Speed.Y = -2.24 / -2.88, Fire.Start(), ...). The Switch keeps onHotCoals (+0x188), the
 * no-ducking check and HotCoalsBounce (0xaf4d90, same body), but nothing calls HotCoalsBounce: Player.Update sets the player on fire
 * instead (that call is nopped in xplay.cpp's version-gap table). This puts the PC's three call sites back, exactly:
 *
 *   NormalUpdate   (IL_0881)  if (onHotCoals) HotCoalsBounce(); else if (JumpPressed || jumpBuffer) { jumps }      -> ShootLock ...
 *   DuckingUpdate  (IL_0225)  if (onHotCoals) { HotCoalsBounce(); return 0; } else if (JumpPressed && jumpGrace) ...
 *   DodgingUpdate  (IL_019f)  if (onHotCoals && !(DodgeSliding && !CanUnduck())) { HotCoalsBounce(); return 0; }   -> hat pickup ...
 *
 * Each Switch site (where both of the PC's paths meet the Switch's code) gets 4 instructions replaced by `ldr x17, #8; br x17; .quad
 * stub`. The stub calls the C check (x0 = the player, x19 in all three methods); if it bounced, it continues at the method's
 * equivalent of the PC's target (0 = Normal for the two returns), else it runs the 4 replaced instructions and continues after them.
 * Caller-saved registers are dead at all three sites (each follows a call or a possible call), x30 is saved by the prologues. */
#include "lib.hpp"
#include "patch/code_patcher.hpp"
#include <lib/util/sys/modules.hpp>
#include <common.hpp>
#include <cstdint>
#include "hwlog.hpp"

static uintptr_t Base() { return exl::util::GetMainModuleInfo().m_Total.m_Start; }

namespace {
constexpr uintptr_t kHotCoalsBounce = 0xaf4d90, kCanUnduck = 0xaf26a0;
constexpr uintptr_t kCounterImplicit = 0x9228c0, kDoHatPickupCheck = 0xaf2770;
constexpr int kOnHotCoals = 0x188, kDodgeSliding = 0x2c4;
inline bool B(void* p, int off) { return *(reinterpret_cast<uint8_t*>(p) + off) != 0; }
inline void Bounce(void* p) { reinterpret_cast<void (*)(void*)>(Base() + kHotCoalsBounce)(p); }
}

/* Absolute addresses for the stubs (filled by Install). */
extern "C" uintptr_t g_hcAddr[10];
uintptr_t g_hcAddr[10];
enum { A_Implicit, A_Hat, N_Cont, N_Taken, N_Bounce, D_Cont, D_Taken, D_Bounce, G_Cont, G_Bounce };

extern "C" int HcNormal(void* p) { if (!B(p, kOnHotCoals)) return 0; Bounce(p); return 1; }
extern "C" int HcDucking(void* p) { if (!B(p, kOnHotCoals)) return 0; Bounce(p); return 1; }
extern "C" int HcDodging(void* p) {
    if (!B(p, kOnHotCoals)) return 0;
    if (B(p, kDodgeSliding) && !reinterpret_cast<bool (*)(void*)>(Base() + kCanUnduck)(p)) return 0;
    Bounce(p); return 1;
}
extern "C" void HcNormalStub(), HcDuckingStub(), HcDodgingStub();

#define HC_JMP(slot) "adrp x17, g_hcAddr\n add x17, x17, :lo12:g_hcAddr\n ldr x17, [x17, #" #slot "*8]\n br x17\n"
#define HC_CALL(slot) "adrp x17, g_hcAddr\n add x17, x17, :lo12:g_hcAddr\n ldr x17, [x17, #" #slot "*8]\n blr x17\n"
asm(R"(
.text
.balign 4
.global HcNormalStub
HcNormalStub:
    mov x0, x19
    bl HcNormal
    cbnz w0, 9f
    ldrb w8, [x19, #0x149]          /* input.JumpPressed */
    cbnz w8, 8f
    ldr x0, [x19, #0x1a8]           /* jumpBufferCounter */
)" HC_CALL(0) HC_JMP(2) R"(
8:
)" HC_JMP(3) R"(
9:
)" HC_JMP(4) R"(
.global HcDuckingStub
HcDuckingStub:
    mov x0, x19
    bl HcDucking
    cbnz w0, 9f
    ldrb w8, [x19, #0x149]          /* input.JumpPressed */
    cbz w8, 8f
    ldr x0, [x19, #0x1a0]           /* jumpGraceCounter */
)" HC_CALL(0) HC_JMP(5) R"(
8:
)" HC_JMP(6) R"(
9:
)" HC_JMP(7) R"(
.global HcDodgingStub
HcDodgingStub:
    mov x0, x19
    bl HcDodging
    cbnz w0, 9f
    ldrb w8, [x19, #0x2c4]          /* DodgeSliding */
    cbz w8, 8f
    mov x0, x19
)" HC_CALL(1) R"(
8:
)" HC_JMP(8) R"(
9:
)" HC_JMP(9));

namespace hotcoals {
/* pcgameplay.cpp: on entering online mode. */
static bool done = false;
static uint32_t g_orig[3][4];
void Install() {
    if (done) return;
    uintptr_t b = Base();
    struct Site { uintptr_t off; uint32_t w[4]; void (*stub)(); const char* what; } sites[] = {
        { 0xae8b14, { 0x39452668u, 0x35000088u, 0xf940d660u, 0x97f8e768u }, HcNormalStub, "NormalUpdate" },
        { 0xaea8d0, { 0x39452668u, 0x340001a8u, 0xf940d260u, 0x97f8dff9u }, HcDuckingStub, "DuckingUpdate" },
        { 0xae9610, { 0x394b1268u, 0x34000068u, 0xaa1303e0u, 0x94002455u }, HcDodgingStub, "DodgingUpdate" },
    };
    for (auto& s : sites) {
        auto* w = reinterpret_cast<uint32_t*>(b + s.off);
        if (w[0] != s.w[0] || w[1] != s.w[1] || w[2] != s.w[2] || w[3] != s.w[3]) {
            hw::Log("[vs] version gap: hot-coals bounce in %s: insns %08x %08x %08x %08x, not patched", s.what, w[0], w[1], w[2], w[3]);
            return;   /* all or nothing */
        }
    }
    g_hcAddr[A_Implicit] = b + kCounterImplicit;   g_hcAddr[A_Hat] = b + kDoHatPickupCheck;
    g_hcAddr[N_Cont] = b + 0xae8b24; g_hcAddr[N_Taken] = b + 0xae8b28; g_hcAddr[N_Bounce] = b + 0xae8bdc;
    g_hcAddr[D_Cont] = b + 0xaea8e0; g_hcAddr[D_Taken] = b + 0xaea908; g_hcAddr[D_Bounce] = b + 0xaea900;
    g_hcAddr[G_Cont] = b + 0xae9620; g_hcAddr[G_Bounce] = b + 0xae9e7c;
    for (int k = 0; k < 3; k++) for (int j = 0; j < 4; j++) g_orig[k][j] = sites[k].w[j];
    for (auto& s : sites) {
        exl::patch::CodePatcher p(s.off);
        p.Write<uint32_t>(0x58000051u);   /* ldr x17, #8 */
        p.Write<uint32_t>(0xd61f0220u);   /* br x17 */
        p.Write<uint64_t>(reinterpret_cast<uintptr_t>(s.stub));
    }
    done = true;
    hw::Log("[vs] version gap: hot coals bounce players in NormalUpdate, DuckingUpdate, DodgingUpdate (as PC)");
}
/* Back to the Switch's own instructions (pcgameplay.cpp, leaving online mode). */
void Uninstall() {
    if (!done) return;
    const uintptr_t offs[3] = { 0xae8b14, 0xaea8d0, 0xae9610 };
    for (int k = 0; k < 3; k++) {
        exl::patch::CodePatcher p(offs[k]);
        for (int j = 0; j < 4; j++) p.Write<uint32_t>(g_orig[k][j]);
    }
    done = false;
}
}
