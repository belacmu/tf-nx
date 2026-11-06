/* A .NET-compatible System.Random on the Switch (VIABILITY-TESTS test 4), and the route to TF.State's gameplay stream.
 *
 * TFEX-DIFF S-native (the console's runtime): the Switch's System.Random is JKISS32, not .NET's, so its method bodies are replaced by .NET's
 * algorithm below. TF.State's gameplay RNG (StateContext's DeterministicRandom, RngService, CalcPatch: the Calc.Random swap) is TF.State's own
 * code, generated (tfex/gen Auto_DeterministicRandom, Auto_CalcPatch ...). TFEX-DIFF S-native: the AOT build calls Random.Next / Sample
 * directly (devirtualized), so a DeterministicRandom's overrides are never reached through its vtable; the hooked bodies here route an
 * object whose runtime type is DeterministicRandom to its generated methods (and count / trace those draws).
 *
 * The Switch's System.Random is JKISS32 (fields x y z c, a 0x20-byte object); .NET's seeded Random is
 * Knuth's subtractive generator (dnrandom.hpp, tested against the real runtime). Seeded sequences
 * matter: level loading (Calc.Random = new Random(Level.LoadSeed), PushRandom(seed)) and TF.EX's own
 * gameplay path (PushRandom(Calc.Random.Next())) must give the browser's numbers.
 *
 * Every method body of the class is replaced (bodies found in the flat image, VIABILITY-TESTS):
 *   .ctor() +0x271d00, .ctor(int) +0x271e30, Next(min,max) +0x271f40, Next(max) +0x272040,
 *   Next() +0x272130, NextBytes +0x272230, Sample +0x272580 (NextDouble just calls Sample).
 * .NET's state (a 56-int array and two indices, 228 bytes) doesn't fit the 16 bytes the object has, so
 * it lives in a table here, found through an index stored in the object's `y` field (+0x14); the
 * object's `x` (+0x10) still holds the seed. Slots are reused round-robin (kSlots newest Randoms).
 * TO DO when rollback exists: g_slots must be part of the snapshot (an object restored to an older
 * state would otherwise find its stream further along).
 *
 * Invalid arguments (negative maxValue, minValue > maxValue, a null array) are left to the original
 * body, which throws the managed exception. */
#include "lib.hpp"
#include "lib/hook/nx64/impl.hpp"
#include "pools.hpp"
#include "loggers.hpp"
#include "patch/code_patcher.hpp"
#include <lib/util/sys/modules.hpp>
#include <common.hpp>

#include <cstdint>
#include <cstring>

#include "dnrandom.hpp"
#include "hwlog.hpp"
#include "rnd.hpp"
#include "tfex/gen/tfexgen_Auto_DeterministicRandom.hpp"
#include "tfex/gen/tfexgen_Auto_RngService.hpp"
#include "tfex/gen/tfexgen_Auto_ServiceCollections.hpp"
#include "tfex/runtime/tfexgen_state.hpp"

namespace pcgameplay { bool Wide(); bool On(); }
namespace rnd {

static uintptr_t Base() { return exl::util::GetMainModuleInfo().m_Total.m_Start; }

namespace off {
    constexpr uintptr_t CalcRandomSlot = 0x47919d0;  /* a relocated slot holding the address of the static Monocle.Calc.Random */
    /* The gameplay RNG's registrations (the whitelist of GAMEPLAY-RNG-PORT.md) are TF.State's patches, generated (tfexgen_glue
     * InstallState: batch 1, batch 2 parts 1-3); none is hand-hooked or checked here any more (a check reads the first instruction,
     * which a generated hook replaces when it goes in first). What stays: System.Random's own entry points below. */
    constexpr uintptr_t Ctor0 = 0x271d00, CtorSeed = 0x271e30, NextMinMax = 0x271f40, NextMax = 0x272040, Next = 0x272130,
                        NextBytes = 0x272230, Sample = 0x272580, ArrayLength = 0x1885e0;
    constexpr uint32_t kCtor0 = 0xa9bd57f6, kCtorSeed = 0xa9be4ff4, kNextMinMax = 0xf81d0ff5, kNextMax = 0xa9be4ff4,
                       kNext = 0xa9bc5ff8, kNextBytes = 0xf81a0ffb, kSample = 0xf81e0ff3;
    constexpr uint32_t SeedField = 0x10, SlotField = 0x14;
}

struct Slot { uintptr_t obj; int32_t seed; int traced; uint32_t serial; dnrandom::State st; };
constexpr int kSlots = 1024;
static Slot g_slots[kSlots];
static uint32_t g_nextSlot = 0, g_serial = 0;
static int g_lost = 0, g_traceLines = 0;
static bool g_trace = false; /* the first 6000 Random calls logged for tools/check-random-trace.py: slow in Ryujinx, so off by default */
static uint8_t* Bytes(void* obj) { return reinterpret_cast<uint8_t*>(obj); }

/* ---- TF.State's gameplay stream: draws on a DeterministicRandom ------------------------------------- */
static bool g_capture = false;
namespace det = tfex::gen::Auto_DeterministicRandom;
static uintptr_t* CalcRandomField() { return *reinterpret_cast<uintptr_t**>(Base() + off::CalcRandomSlot); }
static uint64_t g_draws = 0;   /* gameplay draws so far (the tick log's and DumpDraws' count) */
static int g_gameTrace = 0, g_traceFrame = -1, g_traceFrom = -1000000, g_traced = 0;   /* hw.txt "rnggametrace N [F]": log the first N gameplay draws
                                                                                      * (call site, frame) from frame F on, as the browser's rngtrace (rngfrom=F) */
void SetGameTrace(int n, int from) { g_gameTrace = n; g_traceFrom = from; hw::Log("[rnd] gameplay draw trace: %d draws from frame %d (draws so far %llu)", n, from, (unsigned long long)g_draws); }
static inline bool TraceThis() { return g_gameTrace && g_traceFrame >= g_traceFrom && g_traced < g_gameTrace && ++g_traced; }
void SetTraceFrame(int f) { g_traceFrame = f; }
/* Always on: the last 256 gameplay draws (call site, frame), written to the vs log at vsstop (DumpDraws): a run that diverges carries the
 * draw sites with it, without tracing (which can change timing). */
struct DrawRec { uint32_t site; int32_t frame; uint32_t n; };
static DrawRec g_drawRing[256];
static uint32_t g_drawRingN = 0;
static inline void RecordDraw(uintptr_t site) { g_drawRing[g_drawRingN++ & 255] = { (uint32_t)(site - Base()), g_traceFrame, (uint32_t)g_draws }; }
void DumpDraws(void (*out)(const char*)) {
    uint32_t from = g_drawRingN > 256 ? g_drawRingN - 256 : 0;
    char l[96];
    for (uint32_t i = from; i < g_drawRingN; i++) {
        const DrawRec& r = g_drawRing[i & 255];
        snprintf(l, sizeof l, "[vs] draw %u frame %d site +%#x", r.n, r.frame, r.site);
        out(l);
    }
}
__attribute__((noinline)) static void TraceGameDraw(uintptr_t site) {
    hw::Log("[rnd] gdraw %llu frame %d site +%#lx", (unsigned long long)g_draws, g_traceFrame, (unsigned long)(site - Base()));
}
__attribute__((noinline)) static void FirstDraw(void* self) {
    hw::Log("[rnd] the first gameplay draw: on TF.State's DeterministicRandom %p (generated), Calc.Random %s it", self,
            *CalcRandomField() == reinterpret_cast<uintptr_t>(self) ? "is" : "is not");
}
/* An object of TF.State's DeterministicRandom (the gameplay stream: StateContext's, which CalcPatch puts in Calc.Random and TreasureSpawnerPatch
 * gives the spawner): its draws go to its generated methods, and are counted. Its runtime type is exact (the class is sealed). */
static inline __attribute__((always_inline)) bool Det(void* self) {
    void* t = det::RuntimeType();
    if (!t || tfex::gen::RuntimeTypeOf(self) != reinterpret_cast<uintptr_t>(t)) return false;
    if (++g_draws == 1) FirstDraw(self);
    RecordDraw(reinterpret_cast<uintptr_t>(__builtin_return_address(0))); if (TraceThis()) TraceGameDraw(reinterpret_cast<uintptr_t>(__builtin_return_address(0)));
    return true;
}

static Slot* Create(void* obj, int32_t seed) {
    uint32_t idx = g_nextSlot++ % kSlots;
    Slot& s = g_slots[idx];
    s.obj = reinterpret_cast<uintptr_t>(obj);
    s.seed = seed;
    s.traced = 0;
    s.serial = ++g_serial;
    dnrandom::Init(&s.st, seed);
    *reinterpret_cast<uint32_t*>(Bytes(obj) + off::SlotField) = idx;
    return &s;
}

static Slot* Get(void* obj) {
    uint32_t idx = *reinterpret_cast<uint32_t*>(Bytes(obj) + off::SlotField);
    if (idx < kSlots && g_slots[idx].obj == reinterpret_cast<uintptr_t>(obj)) return &g_slots[idx];
    /* Not made by a hooked constructor, or its slot was reused: start again from the seed field. */
    if (g_lost++ < 5) hw::Log("[rnd] Random %p has no state (slot %u): re-seeding from its seed field", obj, idx);
    return Create(obj, *reinterpret_cast<int32_t*>(Bytes(obj) + off::SeedField));
}

void* CalcRandomObject() { uintptr_t* f = CalcRandomField(); return f ? reinterpret_cast<void*>(*f) : nullptr; }

/* The first few calls on the first few instances, to check against a reference offline
 * (tools/check-random-trace.py reads these lines from the guest log). */
static int g_bytesLines = 0;
static void Trace(Slot* s, const char* kind, int a, int b, long long result) {
    bool bytes = kind[4] == 'B'; /* "NextBytes": the game makes 16-byte ones constantly; a few are enough */
    if (!g_trace || g_traceLines >= 6000) return;
    if (bytes ? (g_bytesLines >= 40 || s->traced >= 3) : s->traced >= 2000) return;
    s->traced++;
    g_traceLines++;
    if (bytes) g_bytesLines++;
    Logging.Log("[rnd] trace #%u seed %d %s %d %d -> %lld", s->serial, s->seed, kind, a, b, result);
}

HOOK_DEFINE_TRAMPOLINE(Ctor0Hook) {
    static void* Callback(void* self) {
        void* obj = Orig(self);
        if (!obj) return obj;
        Create(obj, *reinterpret_cast<int32_t*>(Bytes(obj) + off::SeedField)); /* the original set x = Environment.TickCount */
        return obj;
    }
};
HOOK_DEFINE_TRAMPOLINE(CtorSeedHook) {
    static void* Callback(void* self, int32_t seed) {
        void* obj = Orig(self, seed);
        if (obj) Create(obj, seed);
        return obj;
    }
};
HOOK_DEFINE_TRAMPOLINE(NextHook) {
    static int32_t Callback(void* self) {
        if (Det(self)) return det::Next(self);
        Slot* s = Get(self);
        int32_t r = dnrandom::Next(&s->st);
        Trace(s, "Next", 0, 0, r);
        return r;
    }
};
HOOK_DEFINE_TRAMPOLINE(NextMaxHook) {
    static int32_t Callback(void* self, int32_t maxValue) {
        if (maxValue < 0) return Orig(self, maxValue); /* the original throws */
        if (Det(self)) return det::Next(self, maxValue);
        Slot* s = Get(self);
        int32_t r = dnrandom::Next(&s->st, maxValue);
        Trace(s, "NextMax", maxValue, 0, r);
        return r;
    }
};
HOOK_DEFINE_TRAMPOLINE(NextMinMaxHook) {
    static int32_t Callback(void* self, int32_t minValue, int32_t maxValue) {
        if (minValue > maxValue) return Orig(self, minValue, maxValue); /* the original throws */
        if (Det(self)) return det::Next(self, minValue, maxValue);
        Slot* s = Get(self);
        int32_t r = dnrandom::Next(&s->st, minValue, maxValue);
        Trace(s, "NextMinMax", minValue, maxValue, r);
        return r;
    }
};
HOOK_DEFINE_TRAMPOLINE(SampleHook) {
    static double Callback(void* self) {
        if (Det(self)) return det::Sample(self);
        Slot* s = Get(self);
        double d = dnrandom::Sample(&s->st);
        uint64_t bits;
        std::memcpy(&bits, &d, 8);
        Trace(s, "Sample", 0, 0, (long long)bits);
        return d;
    }
};
HOOK_DEFINE_TRAMPOLINE(NextBytesHook) {
    static void Callback(void* self, void* array) {
        if (!array) return Orig(self, array); /* the original throws */
        using LenFn = int32_t (*)(void*);
        int32_t n = reinterpret_cast<LenFn>(Base() + off::ArrayLength)(array);
        uint8_t* data = *reinterpret_cast<uint8_t**>(Bytes(array) + 0x10);
        if (Det(self)) { det::NextBytes(self, tfex::gen::Arr<uint8_t>{data, n, array}); return; }
        if (n <= 0 || !data) return;
        Slot* s = Get(self);
        dnrandom::NextBytes(&s->st, data, n);
        Trace(s, "NextBytes", n, 0, data[0]);
    }
};

/* TF.State's CalcPushRandomPatch (generated, batch 2 part 3): PushRandom() under the gameplay registration pushes Calc.Random.Next(),
 * which the Next hook draws from the gameplay stream; the hand port here is gone. */

void SetCapture(bool on) { g_capture = on; }
bool Capture() { return g_capture; }

/* The lobby match's seed (lobbymatch.cpp; hw.txt rngseed): TF.EX's own SetSeed gave the stream its seed (MainMenu's create / join,
 * MatchmakingService.HandleLobbyUpdate when the lobby's changes; TF.State reseeds from it at StartGame / StartSession). A check only:
 * the two must agree, else the stream isn't the one the other side draws from. */
void SetLobbySeed(int32_t seed) {
    int32_t gseed = tfex::gen::Auto_RngService::GetSeed(tfex::gen::Auto_ServiceCollections::ResolveRngService());
    if (seed != gseed) hw::Log("[rnd] ERROR: the lobby match's seed %d but TF.State's stream seed %d (TF.EX's SetSeed not reached?)", seed, gseed);
    else hw::Log("[rnd] the lobby match's seed %d = TF.State's stream seed", seed);
}
uint64_t DrawCount() { return g_draws; }
uint64_t StateFold() { return tfex::gen::statert::RngFold(); }

/* TF.State's ArrowCushionPatch, PlayerStatePatch.LoseHat / DropArrow, the hat / Chain / Lava / LavaControl RNG patches run as TF.EX's own code
 * (generated, tfex/gen State_*: tfexgen_glue installs them at boot); their hand ports here are gone (2026-10-19). */
/* TF.State's ArrowPatch (Drop / EnterFallMode), OrbLogicPatch, OrbPatch and SpikeballPatch run as TF.EX's own code (generated, batch 2
 * part 3, 2026-10-21): their registrations bracket the game's own bodies as Harmony's do on the PC (EnterFallMode: Arrow's body only,
 * which the arrows' overrides call through base); DoSpaceOrb is TF.State's replacement, 320 wide as on the PC with or without the
 * wide set. The hand hooks here (and the EnterFallMode vtable pool) are gone. */
/* TF.State's TreasureSpawnerPatch, OrbPickupPatch, UnlockDataPatch, LocalSettingsGuard and LevelLoaderXMLStatePatch run as TF.EX's own code
 * (generated, tfex/gen State_*; batch 2 part 1, 2026-10-21): the spawner's Random is TF.State's IRngService.Gameplay object (statert::
 * GameplayRandom, since 2026-10-26 RngService.Gameplay: StateContext's DeterministicRandom); their hand ports here are gone. */
/* ---- Virtual methods: found from a live object --------------------------------------------------
 * Every managed object starts with a class descriptor: word 1 is its vtable; word 0 leads to a record that
 * holds the type's name (UTF-16, words 4 to 6) and its namespace. So when the first arrow of a type
 * appears, its body for a virtual method is vtable[slot] (debug helpers: the logs name types and bodies). */
const char* TypeName(void* obj, char* out, int cap) {
    out[0] = 0;
    /* (a debug helper also called on words that only look like pointers: a class descriptor lives in the main module, else no name) */
    uintptr_t desc = obj && !(reinterpret_cast<uintptr_t>(obj) & 7) ? *reinterpret_cast<uintptr_t*>(obj) : 0;
    if (desc < Base() || desc >= Base() + 0x4900000 || (desc & 7)) return out;
    uintptr_t klass = *reinterpret_cast<uintptr_t*>(desc);
    if (klass < 0x10000 || (klass & 7)) return out;
    uintptr_t name = reinterpret_cast<uintptr_t*>(klass)[4];
    if (name >= Base() + 0x2863000 && name < Base() + 0x450d168) {
        const uint16_t* u = reinterpret_cast<const uint16_t*>(name);
        int k = 0;
        while (k < cap - 1 && u[k] >= 0x20 && u[k] < 0x7f) { out[k] = (char)u[k]; k++; }
        out[k] = 0;
    }
    return out;
}
uintptr_t VtableBody(void* obj, uint32_t slot) {
    uintptr_t desc = *reinterpret_cast<uintptr_t*>(obj);
    uintptr_t* vt = *reinterpret_cast<uintptr_t**>(desc + 8);
    return vt[slot / 8] & 0x7fffffffffffffffull;   /* the top bits tag the entry */
}

/* Crown, SteelHat and WoodenHat each override Update and TF.EX registers around each (their collision
 * callbacks draw while the hat tumbles); DefaultHat is not patched. Found when Hat.CreateHat returns one. */



/* TF.State's RoundLogicPatch (player spawns: FFA and teams, TopUpSpawns), VersusLevelSystemPatch (the level order) and
 * SessionStatePatch.Session_StartGame (the stream's reset, RandomLevelSeed) run as TF.EX's own code (generated, tfex/gen State_*; batch 2
 * part 2, 2026-10-21): their hand ports here (SpawnFFAHook, the SpawnPlayersTeams inline hooks, TopUpTeams, GenLevelsHook, StartGameHook)
 * are gone. */
/* TF.EX's end-of-tick entity-list flush (LevelPatch.UpdateLayersEntityList) is TF.EX's own code: the generated LevelPatch hook. */

/* B14 (no 12-arrow limit while capturing): TF.State's own ArrowPatch.Arrow_EnforceLimit (generated). */

static bool Check(const char* what, uintptr_t offset, uint32_t expected) {
    uint32_t w = *reinterpret_cast<uint32_t*>(Base() + offset);
    if (w != expected) { hw::Log("[rnd] %s isn't as expected (%08x, wanted %08x): Random NOT replaced", what, w, expected); return false; }
    return true;
}

/* A fake 0x20-byte object: the original constructors accept a non-null `this` and the object
 * constructor they call does nothing for it, so the hooked bodies can be called through the game's own
 * addresses. */
static bool SelfTest() {
    alignas(16) static uint8_t fake[0x40];
    std::memset(fake, 0, sizeof fake);
    using CtorInt = void* (*)(void*, int32_t);
    using NextF = int32_t (*)(void*);
    using NextMaxF = int32_t (*)(void*, int32_t);
    using NextMinMaxF = int32_t (*)(void*, int32_t, int32_t);
    using SampleF = double (*)(void*);
    auto ctor = reinterpret_cast<CtorInt>(Base() + off::CtorSeed);
    auto next = reinterpret_cast<NextF>(Base() + off::Next);
    auto nextMax = reinterpret_cast<NextMaxF>(Base() + off::NextMax);
    auto nextMinMax = reinterpret_cast<NextMinMaxF>(Base() + off::NextMinMax);
    auto sample = reinterpret_cast<SampleF>(Base() + off::Sample);
    bool g_save = g_trace;
    g_trace = false;
    /* Expected values: .NET, seed 42 (tests/dnrandom-ref.txt). */
    ctor(fake, 42);
    int32_t a = next(fake), b = next(fake);
    int32_t c = nextMax(fake, 100), d = nextMinMax(fake, 5, 10);
    double e = sample(fake);
    g_trace = g_save;
    dnrandom::State ref; dnrandom::Init(&ref, 42);
    int32_t ra = dnrandom::Next(&ref), rb = dnrandom::Next(&ref), rc = dnrandom::Next(&ref, 100), rd = dnrandom::Next(&ref, 5, 10);
    double re = dnrandom::Sample(&ref);
    bool ok = a == ra && b == rb && c == rc && d == rd && e == re;
    hw::Log("[rnd] self-test (seed 42 through the game's own function addresses): Next %d %d, Next(100) %d, Next(5,10) %d, Sample %.6f: %s",
            a, b, c, d, e, ok ? "matches .NET" : "DOES NOT MATCH");

    return ok;
}

void Install() {
    bool ok = Check(".ctor()", off::Ctor0, off::kCtor0) & Check(".ctor(int)", off::CtorSeed, off::kCtorSeed) &
              Check("Next(min,max)", off::NextMinMax, off::kNextMinMax) & Check("Next(max)", off::NextMax, off::kNextMax) &
              Check("Next()", off::Next, off::kNext) & Check("NextBytes", off::NextBytes, off::kNextBytes) & Check("Sample", off::Sample, off::kSample);
    if (!ok) return;
    Ctor0Hook::InstallAtOffset(off::Ctor0);
    CtorSeedHook::InstallAtOffset(off::CtorSeed);
    NextHook::InstallAtOffset(off::Next);
    NextMaxHook::InstallAtOffset(off::NextMax);
    NextMinMaxHook::InstallAtOffset(off::NextMinMax);
    SampleHook::InstallAtOffset(off::Sample);
    NextBytesHook::InstallAtOffset(off::NextBytes);
    hw::Log("[rnd] System.Random replaced by .NET's seeded algorithm (%d state slots)", kSlots);
    SelfTest();
}

} // namespace rnd

void rnd_main() { rnd::Install(); }

