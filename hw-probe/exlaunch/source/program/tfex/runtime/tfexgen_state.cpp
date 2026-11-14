#include "tfexgen_state.hpp"
#include "lib.hpp"
#include "../../rnd.hpp"
#include "../../hwlog.hpp"
#include "tfexgen_binding.hpp"
#include "tfexgen_State_Level.hpp"
#include "tfexgen_State_Api.hpp"
#include "tfexgen_GlueStateMsgPack.hpp"
#include "tfexgen_GlueStateSpawn.hpp"
#include "tfexgen_skins.hpp"
#include "tfexgen_Auto_ServiceCollections.hpp"
#include "tfexgen_Auto_StateContext.hpp"
#include "tfexgen_Auto_DeterministicRandom.hpp"
#include <cstring>
#include <nn/fs.hpp>
#include <common.hpp>
#include <algorithm>

extern "C" uint64_t svcGetSystemTick();
namespace tfex::gen { int EnsureStateClasses(); extern bool g_traceOnly, g_traceNow; }
namespace pcgameplay { bool Wide(); }   /* (pcgameplay.cpp: a wide match) */

namespace tfex::gen::statert {
uint64_t RngFold() {
    auto* r = tfex::gen::Auto_DeterministicRandom::S(tfex::gen::Auto_StateContext::GetGameplayRandom(tfex::gen::Auto_ServiceCollections::ResolveStateContext()));
    return r ? r->_s0 ^ r->_s1 ^ r->_s2 ^ r->_s3 : 0;
}

/* ---- MessagePack-CSharp's LZ4 codec (MessagePack.LZ4.LZ4Codec, MIT; lz4net's port of LZ4), the 64-bit encoder for inputs under
 * 65547 bytes (LZ4_compress64kCtx_64), statement for statement: its hash table is 2048 entries (HASH64K_LOG 11, today's lz4 has 4096),
 * cleared before each block as its HashTablePool does. */
namespace {
inline uint32_t R32(const uint8_t* p) { uint32_t v; std::memcpy(&v, p, 4); return v; }
inline uint16_t R16(const uint8_t* p) { uint16_t v; std::memcpy(&v, p, 2); return v; }
inline uint64_t R64(const uint8_t* p) { uint64_t v; std::memcpy(&v, p, 8); return v; }
inline uint32_t Hash64k(const uint8_t* p) { return (R32(p) * 2654435761u) >> 21; }

int Compress64k(const uint8_t* src, uint8_t* dst, int srcLen, int dstMax) {
    static uint16_t ht[2048];
    std::memset(ht, 0, sizeof ht);
    const uint8_t* ip = src;
    const uint8_t* anchor = ip;
    const uint8_t* base = ip;
    const uint8_t* iend = ip + srcLen;
    const uint8_t* mflimit = iend - 12;
    uint8_t* op = dst;
    uint8_t* oend = op + dstMax;
    const uint8_t* matchlimit = iend - 5;
    const uint8_t* ml1 = matchlimit - 1;
    const uint8_t* ml3 = matchlimit - 3;
    const uint8_t* ml7 = matchlimit - 7;
    uint8_t* oend6 = oend - 6;
    uint8_t* oend8 = oend - 8;
    if (srcLen >= 13) {
        ip++;
        uint32_t forwardH = Hash64k(ip);
        for (;;) {
            int attempts = 67;
            const uint8_t* forwardIp = ip;
            const uint8_t* ref;
            uint8_t* token;
            for (;;) {   /* find a match */
                uint32_t h = forwardH;
                int step = attempts++ >> 6;
                ip = forwardIp;
                forwardIp = ip + step;
                if (forwardIp > mflimit) goto lastLiterals;
                forwardH = Hash64k(forwardIp);
                ref = base + ht[h];
                ht[h] = (uint16_t)(ip - base);
                if (R32(ref) == R32(ip)) break;
            }
            while (ip > anchor && ref > src && ip[-1] == ref[-1]) { ip--; ref--; }   /* catch up */
            {
                int length = (int)(ip - anchor);
                token = op++;
                if (op + length + (length >> 8) > oend8) return 0;
                if (length >= 15) {
                    int len = length - 15;
                    *token = 240;
                    if (len > 254) {
                        do { *op++ = 255; len -= 255; } while (len > 254);
                        *op++ = (uint8_t)len;
                        std::memcpy(op, anchor, (size_t)length);   /* (BlockCopy64) */
                        op += length;
                        goto nextMatch;
                    }
                    *op++ = (uint8_t)len;
                } else *token = (uint8_t)(length << 4);
                uint8_t* e = op + length;   /* its wild copy, 8 bytes at a time (within oend - 8: checked above) */
                do { std::memcpy(op, anchor, 8); op += 8; anchor += 8; } while (op < e);
                op = e;
            }
        nextMatch:
            for (;;) {
                uint16_t off = (uint16_t)(ip - ref);
                std::memcpy(op, &off, 2); op += 2;
                ip += 4; ref += 4;
                anchor = ip;
                for (;;) {   /* count the match */
                    if (ip < ml7) {
                        uint64_t diff = R64(ref) ^ R64(ip);
                        if (diff == 0) { ip += 8; ref += 8; continue; }
                        ip += __builtin_ctzll(diff) >> 3;   /* (its DEBRUIJN_TABLE_64) */
                        break;
                    }
                    if (ip < ml3 && R32(ref) == R32(ip)) { ip += 4; ref += 4; }
                    if (ip < ml1 && R16(ref) == R16(ip)) { ip += 2; ref += 2; }
                    if (ip < matchlimit && *ref == *ip) ip++;
                    break;
                }
                int len = (int)(ip - anchor);
                if (op + (len >> 8) > oend6) return 0;
                if (len >= 15) {
                    *token += 15;
                    for (len -= 15; len > 509; len -= 510) { *op++ = 255; *op++ = 255; }
                    if (len > 254) { len -= 255; *op++ = 255; }
                    *op++ = (uint8_t)len;
                } else *token += (uint8_t)len;
                if (ip > mflimit) { anchor = ip; goto lastLiterals; }
                ht[Hash64k(ip - 2)] = (uint16_t)(ip - 2 - base);
                uint32_t h = Hash64k(ip);
                ref = base + ht[h];
                ht[h] = (uint16_t)(ip - base);
                if (R32(ref) == R32(ip)) { token = op++; *token = 0; continue; }
                break;
            }
            anchor = ip++;
            forwardH = Hash64k(ip);
        }
    }
lastLiterals:
    {
        int lastRun = (int)(iend - anchor);
        if (op + lastRun + 1 + (lastRun - 15 + 255) / 255 > oend) return 0;
        if (lastRun >= 15) {
            *op++ = 240;
            for (lastRun -= 15; lastRun > 254; lastRun -= 255) *op++ = 255;
            *op++ = (uint8_t)lastRun;
        } else *op++ = (uint8_t)(lastRun << 4);
        std::memcpy(op, anchor, (size_t)(iend - anchor));
        op += iend - anchor;
        return (int)(op - dst);
    }
}

/* an LZ4 block (the format is LZ4's: any decoder reads it): the bytes written, -1 if invalid */
int Decompress(const uint8_t* src, int srcLen, uint8_t* dst, int dstLen) {
    const uint8_t* ip = src; const uint8_t* iend = src + srcLen;
    uint8_t* op = dst; uint8_t* oend = dst + dstLen;
    while (ip < iend) {
        uint8_t token = *ip++;
        int lit = token >> 4;
        if (lit == 15) { uint8_t b; do { if (ip >= iend) return -1; b = *ip++; lit += b; } while (b == 255); }
        if (ip + lit > iend || op + lit > oend) return -1;
        std::memcpy(op, ip, (size_t)lit); op += lit; ip += lit;
        if (ip >= iend) break;   /* the last sequence has literals only */
        if (ip + 2 > iend) return -1;
        int off = ip[0] | (ip[1] << 8); ip += 2;
        if (off == 0 || op - dst < off) return -1;
        int ml = (token & 15);
        if (ml == 15) { uint8_t b; do { if (ip >= iend) return -1; b = *ip++; ml += b; } while (b == 255); }
        ml += 4;
        if (op + ml > oend) return -1;
        const uint8_t* m = op - off;
        for (int i = 0; i < ml; i++) op[i] = m[i];   /* (overlapping copies byte by byte) */
        op += ml;
    }
    return (int)(op - dst);
}

int UIntSize(uint32_t v) { return v <= 127 ? 1 : v <= 255 ? 2 : v <= 65535 ? 3 : 5; }
void PutUInt(uint8_t*& p, uint32_t v) {   /* MessagePackWriter.Write(int) for v >= 0 */
    if (v <= 127) *p++ = (uint8_t)v;
    else if (v <= 255) { *p++ = 0xcc; *p++ = (uint8_t)v; }
    else if (v <= 65535) { *p++ = 0xcd; *p++ = (uint8_t)(v >> 8); *p++ = (uint8_t)v; }
    else { *p++ = 0xce; *p++ = (uint8_t)(v >> 24); *p++ = (uint8_t)(v >> 16); *p++ = (uint8_t)(v >> 8); *p++ = (uint8_t)v; }
}
gen::Arr<uint8_t> NewBytes(int32_t n) { return gen::NewArr<uint8_t>(bind::t_Byte_ef68, n); }   /* System.Byte[] */
uint8_t g_scratch[160 * 1024];   /* the compressed blocks before they're copied into the managed byte[] */
double g_lastLz4Ms = 0, g_lastUnwrapMs = 0;
int32_t g_lastRaw = 0;
double Ms(uint64_t t) { return (double)t / 19200.0; }   /* the system tick: 19.2 MHz */
}

}   /* namespace tfex::gen::statert (reopened below) */
namespace tfex::gen {
ProfScope* g_profTop = nullptr;
namespace { ProfSlot* g_profSlots[2048]; int g_profN = 0; }
static inline uint64_t ProfNow() { uint64_t t; asm volatile("mrs %0, cntpct_el0" : "=r"(t)); return t; }
ProfScope::ProfScope(ProfSlot* slot) : s(slot), t0(ProfNow()), parent(g_profTop) {
    if (!slot->idx && g_profN < 2047) { g_profSlots[g_profN] = slot; slot->idx = (uint32_t)++g_profN; }   /* (idx = index + 1) */
    g_profTop = this;
}
ProfScope::~ProfScope() {
    uint64_t d = ProfNow() - t0;
    s->incl += d; s->self += d - child; s->calls++;
    if (parent) parent->child += d;
    g_profTop = parent;
}
namespace statert {
namespace { struct Bucket { uint64_t self[2048], incl[2048]; uint32_t calls[2048]; }; Bucket g_prof[2]; int g_profRuns[2]; }
void ProfBegin() { for (int i = 0; i < g_profN; i++) { auto* s = g_profSlots[i]; s->incl = s->self = 0; s->calls = 0; } }
void ProfEnd(int b) {
    for (int i = 0; i < g_profN; i++) { auto* s = g_profSlots[i]; g_prof[b].self[i] += s->self; g_prof[b].incl[i] += s->incl; g_prof[b].calls[i] += s->calls; }
    g_profRuns[b]++;
}
void ProfDump(int b, const char* tag, int n) {
    if (!g_profN || !g_profRuns[b]) return;
    int runs = g_profRuns[b]; uint64_t tot = 0;
    for (int i = 0; i < g_profN; i++) tot += g_prof[b].self[i];
    hw::Log("[prof] %s over %d runs: %.3f ms self per run in profiled methods (cntpct 19.2 MHz); top by self (ms/run, calls/run, incl ms/run):", tag, runs, tot / 19200.0 / runs);
    bool taken[2048] = {};
    for (int k = 0; k < n; k++) {
        int best = -1; for (int i = 0; i < g_profN; i++) if (!taken[i] && (best < 0 || g_prof[b].self[i] > g_prof[b].self[best])) best = i;
        if (best < 0 || !g_prof[b].self[best]) break;
        taken[best] = true;
        hw::Log("[prof]  %-48s self %.3f  calls %.1f  incl %.3f", g_profSlots[best]->name, g_prof[b].self[best] / 19200.0 / runs, (double)g_prof[b].calls[best] / runs, g_prof[b].incl[best] / 19200.0 / runs);
    }
    std::memset(&g_prof[b], 0, sizeof g_prof[b]); g_profRuns[b] = 0;
}

/* ---- TF.State's entity helpers over a per-layer index (tfexgen_state.hpp EntGetAll ...) */
namespace {
/* the game's (TowerFall 1.4.1.6) members these use, as tfexgen_binding.hpp has them (kept here: a regeneration drops bindings nothing generated uses) */
constexpr uintptr_t kSceneGetLayers = 0x943c40, kLayerGetEntities = 0x936b00, kLayerGetIndex = 0x936ae0, kEntityGetLayerIndex = 0x92dd20,
                    kListEntityRemove = 0x1c5a140;
constexpr uint32_t kLayerToAdd = 0x60, kVirtRemoved = 0x38;
constexpr uint32_t kDictLink = 0x18, kDictKeys = 0x20, kDictVals = 0x28, kDictTouched = 0x40;   /* Dictionary<int, Layer> */
constexpr uint32_t kListItems = 0x10, kListSize = 0x18, kListVersion = 0x1c;                     /* List<Entity>: _items, _size, _version */
constexpr int kIdxEnts = 1024, kIdxTypes = 64, kIdxLayers = 32;
struct LayerIdx {
    void* list; void* items; int32_t size, version; bool ok;
    int32_t nTypes; uintptr_t types[kIdxTypes]; void* reps[kIdxTypes];   /* the distinct runtime types, one entity of each */
    uint8_t typeOf[kIdxEnts]; void* ents[kIdxEnts];
};
LayerIdx g_idx[kIdxLayers]; int g_idxNext = 0; int g_idxRebuilds = 0, g_idxFallbacks = 0;
template <class F> void ForLayers(void* level, F&& f) {   /* level.Layers.Values in the dictionary's order (gen::FromDict, as the generated code walks it) */
    gen::FromDict<int32_t, void*>(gen::Fn<void* (*)(void*)>(kSceneGetLayers)(level), kDictLink, kDictKeys, kDictVals, kDictTouched)([&](gen::KV<int32_t, void*> kv) { f(kv.Value); return true; });
}
void* EntitiesOf(void* layer) { return gen::Fn<void* (*)(void*)>(kLayerGetEntities)(layer); }
/* the list's index, or null when it can't be indexed (more than kIdxEnts entities / kIdxTypes types: the caller scans as TF.State does) */
LayerIdx* IndexOf(void* list) {
    if (!list) return nullptr;
    void* items = gen::Fld<void*>(list, kListItems);
    int32_t size = gen::Fld<int32_t>(list, kListSize), version = gen::Fld<int32_t>(list, kListVersion);
    LayerIdx* c = nullptr;
    for (auto& e : g_idx) if (e.list == list) { c = &e; break; }
    if (c && c->items == items && c->size == size && c->version == version) return c->ok ? c : nullptr;
    if (!c) { c = &g_idx[g_idxNext]; g_idxNext = (g_idxNext + 1) % kIdxLayers; }
    c->list = list; c->items = items; c->size = size; c->version = version; c->ok = false; c->nTypes = 0;
    g_idxRebuilds++;
    if (size < 0 || size > kIdxEnts) { if (g_idxFallbacks++ < 3) hw::Log("[state] entity index: a layer with %d entities (index holds %d): scanned without it", size, kIdxEnts); return nullptr; }
    void** data = gen::ArrData<void*>(items);
    for (int32_t i = 0; i < size; i++) {
        void* e = data[i];
        uintptr_t rt = e ? **reinterpret_cast<uintptr_t* const*>(e) : 0;
        int t = 0;
        while (t < c->nTypes && c->types[t] != rt) t++;
        if (t == c->nTypes) {
            if (t == kIdxTypes) { if (g_idxFallbacks++ < 3) hw::Log("[state] entity index: a layer with more than %d entity types: scanned without it", kIdxTypes); return nullptr; }
            c->types[t] = rt; c->reps[t] = e; c->nTypes++;
        }
        c->typeOf[i] = (uint8_t)t; c->ents[i] = e;
    }
    c->ok = true;
    return c;
}
uint64_t MaskOf(const LayerIdx* c, void* type) {   /* the index's types that are T (an entity of each, through gen::As: C#'s `is T`) */
    uint64_t m = 0;
    for (int t = 0; t < c->nTypes; t++) if (gen::As(c->reps[t], type)) m |= 1ull << t;
    return m;
}
/* f(entity) for each entity of the list that is T, in the list's order; f returns false to stop. Returns false if stopped. */
template <class F> bool EachOfType(void* list, void* type, F&& f) {
    if (LayerIdx* c = IndexOf(list)) {
        uint64_t m = MaskOf(c, type);
        if (!m) return true;
        for (int32_t i = 0; i < c->size; i++) if ((m >> c->typeOf[i]) & 1) if (!f(c->ents[i])) return false;
        return true;
    }
    if (!list) return true;
    int32_t size = gen::Fld<int32_t>(list, kListSize);
    void** data = gen::ArrData<void*>(gen::Fld<void*>(list, kListItems));
    for (int32_t i = 0; i < size; i++) if (gen::As(data[i], type)) if (!f(data[i])) return false;
    return true;
}
/* DynamicData.For(layer).Get<List<Entity>>("toAdd")?.RemoveAll(entity => entity is T): the same contents afterwards (each match removed, the
 * others in order), through List.Remove per match; nothing to do (and no delegate made) when toAdd is empty */
void RemoveFromToAdd(void* layer, void* type, void* where = nullptr) {
    void* toAdd = gen::Fld<void*>(layer, kLayerToAdd);
    if (!toAdd || gen::Fld<int32_t>(toAdd, kListSize) <= 0) return;
    void* matches[kIdxEnts]; int n = 0;
    int32_t size = gen::Fld<int32_t>(toAdd, kListSize);
    void** data = gen::ArrData<void*>(gen::Fld<void*>(toAdd, kListItems));
    for (int32_t i = 0; i < size; i++) if (gen::As(data[i], type) && (!where || gen::DlgInvoke<bool, void*>(where, data[i]))) { if (n < kIdxEnts) matches[n++] = data[i]; else gen::PoolFull("entity index: toAdd matches", kIdxEnts); }
    for (int i = 0; i < n; i++) gen::Fn<bool (*)(void*, void*)>(kListEntityRemove)(toAdd, matches[i]);
}
}   // namespace

/* level.Layers.SelectMany(layer => layer.Value.Entities).Where(ent => ent is T).Select(ent => ent as T), materialized as the generated code does */
gen::Snap<void*> EntGetAll(void* level, void* type) {
    gen::Snap<void*> r;
    if (!level || !type) return r;
    ForLayers(level, [&](void* layer) { EachOfType(EntitiesOf(layer), type, [&](void* e) { r.Add(e); return true; }); });
    return r;
}
/* level.Layers.SelectMany(layer => layer.Value.Entities).FirstOrDefault(ent => ent is T) as T */
void* EntGet(void* level, void* type) {
    void* found = nullptr;
    if (!level || !type) return nullptr;
    bool stop = false;
    ForLayers(level, [&](void* layer) { if (!stop) EachOfType(EntitiesOf(layer), type, [&](void* e) { found = e; stop = true; return false; }); });
    return found;
}
/* foreach layer: toAdd's T removed; then each T of its entities (a snapshot) removed from the list and Removed(); with `where` (TF.State
 * v0.30.0's filter) only the T it accepts */
void EntDeleteAll(void* level, void* type, void* where) {
    if (!level || !type) return;
    ForLayers(level, [&](void* layer) {
        RemoveFromToAdd(layer, type, where);
        void* list = EntitiesOf(layer);
        void* snap[kIdxEnts]; int n = 0;
        EachOfType(list, type, [&](void* e) { if (where && !gen::DlgInvoke<bool, void*>(where, e)) return true; if (n < kIdxEnts) snap[n++] = e; else gen::PoolFull("entity index: DeleteAll snapshot", kIdxEnts); return true; });
        for (int i = 0; i < n; i++) {
            gen::Fn<bool (*)(void*, void*)>(kListEntityRemove)(EntitiesOf(layer), snap[i]);
            gen::Virt<void>(snap[i], kVirtRemoved);
        }
    });
}
/* foreach layer: toAdd's T removed; then the first T of all layers removed from the first layer whose Index is its LayerIndex, and Removed() */
void EntDelete(void* level, void* type) {
    if (!level || !type) return;
    ForLayers(level, [&](void* layer) { RemoveFromToAdd(layer, type); });
    void* entity = EntGet(level, type);
    if (!entity) return;
    int32_t li = gen::Fn<int32_t (*)(void*)>(kEntityGetLayerIndex)(entity);
    void* target = nullptr;
    ForLayers(level, [&](void* layer) { if (!target && gen::Fn<int32_t (*)(void*)>(kLayerGetIndex)(layer) == li) target = layer; });
    gen::Fn<bool (*)(void*, void*)>(kListEntityRemove)(target ? EntitiesOf(target) : nullptr, entity);
    gen::Virt<void>(entity, kVirtRemoved);
}

/* ---- the GC's collections (GcWatch / GcSummary). GC_on_collection_event (+0x48e25b0, a void (*)(GC_EventType)) is called by
 * GC_try_to_collect_inner (+0x28570f0) with GC_EVENT_START (0) and GC_EVENT_END (5); by GC_stopped_mark (its calls +0x2857574..+0x28576e0) with PRE_STOP_WORLD (6),
 * POST_STOP_WORLD (7), MARK_START (1), MARK_END (2), PRE_START_WORLD (8), POST_START_WORLD (9); by GC_finish_collection (+0x285781c) with
 * RECLAIM_START (3). Each collection is timed whole (0 -> 5: what the game waits) and by phase: stopping the world (6 -> 7), the mark
 * (1 -> 2), starting it (8 -> 9) and the finish (3 -> 5: finalization, the disappearing links, the reclaim). The callback runs with the
 * world stopped for most of them: only counters here. */
namespace {
constexpr uintptr_t kGcOnCollectionEvent = 0x48e25b0;
constexpr uintptr_t kGcFreeSpaceDivisor = 0x4721f68;   /* GC_word GC_free_space_divisor (3): min_bytes_allocd = scanned heap / it (+0x2857044) */
/* The game runs Boehm with GC_free_space_divisor 2 (a collection after allocating ~half the scanned heap). TF.State's rollback allocates ~21 KB a
 * save and ~30 KB a load (a save every tick at 240 Hz), so the Switch sets 1: ~half as many collections, each ~15 ms world-stopped on the
 * console (Ryujinx sync test: 7 -> 3-4 per 600 saves; console 240 Hz 2026-10-24: 2-3). The heap grows by the extra free space. */
int g_gcDivisor = 1;
using GcEventFn = void (*)(int);
GcEventFn g_gcPrev = nullptr;
enum GcPhase { kGcWhole, kGcStop, kGcMark, kGcStart, kGcFinish, kGcPhases };
constexpr int kGcPhaseBegin[kGcPhases] = {0, 6, 1, 8, 3}, kGcPhaseEnd[kGcPhases] = {5, 7, 2, 9, 5};
volatile uint64_t g_gcAt[kGcPhases], g_gcTicks[kGcPhases], g_gcMax = 0; volatile int32_t g_gcN = 0;
uint64_t g_gcSeen[kGcPhases]; int32_t g_gcSeenN = 0;
/* hw.txt gcmin <MB> (default 40, 0 = Boehm's own): the least a collection waits for. Boehm collects once the bytes allocated since the last
 * one pass min_bytes_allocd = (2 x pointer-bearing in use + pointer-free in use / 4 + roots + stacks) / GC_free_space_divisor: ~13 MB in the
 * emulator, ~21.5 MB on the console. TF.EX's rollback at 240 Hz allocates ~22.5 MB per 600 saves (TF.State's GetState models every save), so
 * a collection came every ~1.7 s, each a 15-30 ms world-stopped mark of the live heap (its length set by the live heap, not by the
 * threshold). The heap is already ~60 MB with ~14 MB live, so a 40 MB floor fits in it: ~2.5x fewer pauses. GC_should_collect (+0x2856fa0)
 * caches the threshold at +0x48e25a0, recomputed once per collection (keyed by GC_gc_no at +0x48e25a8); the hook raises the cached value
 * and asks again. Where it says no, Boehm grows the heap instead (GC_collect_or_expand). */
int g_gcMinMb = 40;
constexpr uintptr_t kGcShouldCollect = 0x2856fa0, kGcLastMinBytesAllocd = 0x48e25a0;
HOOK_DEFINE_TRAMPOLINE(GcShouldCollect) {
    static bool Callback() {
        bool r = Orig();
        auto* lim = reinterpret_cast<uint64_t*>(gen::Base() + kGcLastMinBytesAllocd);
        uint64_t want = (uint64_t)g_gcMinMb << 20;
        if (r && *lim < want) { *lim = want; r = Orig(); }
        return r;
    }
};
void OnGcEvent(int ev) {
    uint64_t now = svcGetSystemTick();
    if (ev == 0) g_gcN = g_gcN + 1;
    for (int p = 0; p < kGcPhases; p++) {
        if (ev == kGcPhaseBegin[p]) g_gcAt[p] = now;
        else if (ev == kGcPhaseEnd[p] && g_gcAt[p]) {
            uint64_t d = now - g_gcAt[p];
            g_gcTicks[p] = g_gcTicks[p] + d; g_gcAt[p] = 0;
            if (p == kGcWhole && d > g_gcMax) g_gcMax = d;
        }
    }
    if (g_gcPrev) g_gcPrev(ev);
}
}
void GcWatch() {
    auto* slot = reinterpret_cast<GcEventFn*>(gen::Base() + kGcOnCollectionEvent);
    auto* div = reinterpret_cast<uint64_t*>(gen::Base() + kGcFreeSpaceDivisor);
    if (g_gcDivisor > 0 && *div != (uint64_t)g_gcDivisor) { hw::Log("[gc] GC_free_space_divisor %llu -> %d (hw.txt gcdivisor, else the Switch's default 1)", (unsigned long long)*div, g_gcDivisor); *div = (uint64_t)g_gcDivisor; }
    if (*slot == &OnGcEvent) return;
    if (g_gcMinMb > 0) { GcShouldCollect::InstallAtOffset(kGcShouldCollect); hw::Log("[gc] a collection waits for at least %d MB allocated (hw.txt gcmin, 0 = Boehm's own threshold)", g_gcMinMb); }
    g_gcPrev = *slot;
    *slot = &OnGcEvent;
    hw::Log("[gc] watching the GC's collections (GC_on_collection_event%s)", g_gcPrev ? ", chained to the game's own callback" : "");
}
void GcDivisor(int d) { g_gcDivisor = d; }
void GcMin(int mb) { g_gcMinMb = mb; }
void GcSummary(char* buf, int cap) {
    int32_t n = g_gcN - g_gcSeenN;
    double ms[kGcPhases];
    for (int p = 0; p < kGcPhases; p++) { ms[p] = (g_gcTicks[p] - g_gcSeen[p]) / 19200.0; g_gcSeen[p] = g_gcTicks[p]; }
    uint64_t used = 0; svcGetInfo(&used, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0);
    /* what a mark scans (Boehm's min_bytes_allocd, +0x2856fd0: 2 x pointer-bearing in use + pointer-free in use / 4 + the roots + the stacks):
     * GC_arrays (+0x48e2af0) heapsize +0, composite_in_use +0x90, atomic_in_use +0x98; GC_root_size +0x48e2a88 */
    auto word = [](uintptr_t off) { return *reinterpret_cast<const uint64_t*>(gen::Base() + off) / 1048576.0; };
    std::snprintf(buf, (size_t)cap, "%d GC collections, %.1f ms in total, max %.1f ms (stopping the world %.1f, mark %.1f, starting it %.1f, "
                  "finalize + reclaim %.1f); heap %.1f MB (in use: %.1f MB with pointers, %.1f MB without), roots %.1f MB, a collection every %.1f MB "
                  "allocated; process memory %.1f MB", n, ms[kGcWhole], g_gcMax / 19200.0, ms[kGcStop], ms[kGcMark], ms[kGcStart], ms[kGcFinish],
                  word(0x48e2af0), word(0x48e2b80), word(0x48e2b88), word(0x48e2a88), std::max(word(kGcLastMinBytesAllocd), (double)g_gcMinMb), used / 1048576.0);
    g_gcSeenN = g_gcN; g_gcMax = 0;
}

uint64_t g_stampLz4In = 0, g_stampLz4Out = 0, g_stampUnwrapOut = 0;

gen::Arr<uint8_t> Lz4BlockArray(const gen::Arr<uint8_t>& raw) {
    uint64_t t0 = svcGetSystemTick();
    g_stampLz4In = t0;
    g_lastRaw = raw.len;
    if (!raw.data || raw.len < 64) { g_lastLz4Ms = 0; return raw; }
    /* the blocks: MessagePack-CSharp's buffer segments of this write (skins::MpSegments: 4096 bytes, then 32 KB, cut where a write's size
     * hint doesn't fit), one LZ4 block each, as its Lz4BlockArray */
    int32_t ends[64];
    int32_t ne = skins::MpSegments(ends, 63);
    int32_t starts[65], lens[65];
    int nb = 0;
    for (int32_t i = 0, at = 0; i <= ne; i++) {
        int32_t e = i < ne ? ends[i] : raw.len;
        if (e > raw.len) e = raw.len;
        if (e > at) { starts[nb] = at; lens[nb] = e - at; nb++; at = e; }
    }
    int lensSize = 0;
    for (int b = 0; b < nb; b++) lensSize += UIntSize((uint32_t)lens[b]);
    uint8_t* p = g_scratch;
    uint8_t* end = g_scratch + sizeof g_scratch;
    /* array header (blocks + 1) */
    if (nb + 1 <= 15) *p++ = (uint8_t)(0x90 | (nb + 1)); else { *p++ = 0xdc; *p++ = (uint8_t)((nb + 1) >> 8); *p++ = (uint8_t)(nb + 1); }
    /* ext header, type 98 */
    switch (lensSize) {
        case 1: *p++ = 0xd4; break; case 2: *p++ = 0xd5; break; case 4: *p++ = 0xd6; break; case 8: *p++ = 0xd7; break; case 16: *p++ = 0xd8; break;
        default: *p++ = 0xc7; *p++ = (uint8_t)lensSize; break;
    }
    *p++ = 98;
    for (int b = 0; b < nb; b++) PutUInt(p, (uint32_t)lens[b]);
    for (int b = 0; b < nb; b++) {
        int n = lens[b];
        int cap = n + n / 255 + 16;   /* LZ4Codec.MaximumOutputLength */
        if (p + 5 + cap > end) { hw::Log("[state] LZ4: %d bytes don't fit the scratch buffer", raw.len); return raw; }
        int c = Compress64k(raw.data + starts[b], p + 5, n, cap);
        p[0] = 0xc6; p[1] = (uint8_t)(c >> 24); p[2] = (uint8_t)(c >> 16); p[3] = (uint8_t)(c >> 8); p[4] = (uint8_t)c;   /* bin32, always */
        p += 5 + c;
    }
    auto out = NewBytes((int32_t)(p - g_scratch));
    if (out.data) std::memcpy(out.data, g_scratch, (size_t)(p - g_scratch));
    g_stampLz4Out = svcGetSystemTick();
    g_lastLz4Ms = Ms(g_stampLz4Out - t0);
    return out;
}

gen::Arr<uint8_t> Lz4Unwrap(const gen::Arr<uint8_t>& bytes) {
    uint64_t t0 = svcGetSystemTick();
    if (!bytes.data || bytes.len < 4) return bytes;
    const uint8_t* p = bytes.data; const uint8_t* end = p + bytes.len;
    int n;   /* array(blocks + 1) followed by ext 98, else the bytes are raw MessagePack */
    if ((p[0] & 0xf0) == 0x90) { n = p[0] & 15; p++; } else if (p[0] == 0xdc) { n = (p[1] << 8) | p[2]; p += 3; } else return bytes;
    int extLen;
    switch (*p) {
        case 0xd4: extLen = 1; p++; break; case 0xd5: extLen = 2; p++; break; case 0xd6: extLen = 4; p++; break; case 0xd7: extLen = 8; p++; break; case 0xd8: extLen = 16; p++; break;
        case 0xc7: extLen = p[1]; p += 2; break;
        default: return bytes;
    }
    if (*p++ != 98 || n < 2) return bytes;
    const uint8_t* lens = p; p += extLen;
    int lensOf[64]; int nb = n - 1, total = 0;
    if (nb > 64) return gen::Arr<uint8_t>{nullptr, 0, nullptr};
    for (int b = 0; b < nb; b++) {   /* MessagePack ints */
        uint32_t v; uint8_t t = *lens++;
        if (t <= 0x7f) v = t; else if (t == 0xcc) v = *lens++; else if (t == 0xcd) { v = (lens[0] << 8) | lens[1]; lens += 2; }
        else if (t == 0xce) { v = ((uint32_t)lens[0] << 24) | (lens[1] << 16) | (lens[2] << 8) | lens[3]; lens += 4; } else return gen::Arr<uint8_t>{nullptr, 0, nullptr};
        lensOf[b] = (int)v; total += (int)v;
    }
    auto out = NewBytes(total);
    if (!out.data) return out;
    int at = 0;
    for (int b = 0; b < nb; b++) {
        if (p + 5 > end || p[0] != 0xc6) return gen::Arr<uint8_t>{nullptr, 0, nullptr};
        int c = (int)(((uint32_t)p[1] << 24) | (p[2] << 16) | (p[3] << 8) | p[4]); p += 5;
        if (p + c > end || Decompress(p, c, out.data + at, lensOf[b]) != lensOf[b]) { hw::Log("[state] LZ4: block %d doesn't decode", b); return gen::Arr<uint8_t>{nullptr, 0, nullptr}; }
        p += c; at += lensOf[b];
    }
    g_stampUnwrapOut = svcGetSystemTick();
    g_lastUnwrapMs = Ms(g_stampUnwrapOut - t0);
    return out;
}

/* ---- the measurement */
namespace {
int g_every = 0, g_loadEvery = 0;
/* step E (hw.txt "statedump <every>"): every <every>th measured frame's serialized state, as statediff.py reads it:
 * "[state] dump frame <N> part <k>/<n> <base64>" (hwlog cuts long lines; the parts are joined there) */
int g_dumpEvery = 0;
void DumpState(int frame, const gen::Arr<uint8_t>& bytes, const char* tag = "dump") {
    static const char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    constexpr int kChunk = 384;   /* base64 characters per line: a multiple of 4 */
    int n = bytes.len, total = (n + 2) / 3 * 4, parts = (total + kChunk - 1) / kChunk;
    char line[kChunk + 1]; int at = 0, part = 1;
    for (int i = 0; i < n; i += 3) {
        uint32_t v = (uint32_t)bytes.data[i] << 16 | (i + 1 < n ? (uint32_t)bytes.data[i + 1] << 8 : 0) | (i + 2 < n ? bytes.data[i + 2] : 0);
        line[at++] = kB64[v >> 18 & 63]; line[at++] = kB64[v >> 12 & 63];
        line[at++] = i + 1 < n ? kB64[v >> 6 & 63] : '='; line[at++] = i + 2 < n ? kB64[v & 63] : '=';
        if (at == kChunk || i + 3 >= n) { line[at] = 0; hw::Log("[state] %s frame %d part %d/%d %s", tag, frame, part++, parts, line); at = 0; }
    }
}
/* a dumped frame's GGRS save (statediff.py takes a frame's last dump: what GGRS kept after rollbacks), skipped when its bytes are those
 * of that frame's last dump: a stalled session re-saves the same frame unchanged on every update (wide5 215719: 26986 dumps of frame
 * 60, the log head full). The skipped count is logged with the next dump. */
int g_lastDumpFrame = -1, g_dumpSkipped = 0;
uint64_t g_lastDumpHash = 0;
void DumpSavedFrame(int frame, const gen::Arr<uint8_t>& bytes) {
    uint64_t h = 1469598103934665603ull;
    for (int i = 0; i < bytes.len; i++) h = (h ^ bytes.data[i]) * 1099511628211ull;
    if (frame == g_lastDumpFrame && h == g_lastDumpHash) { g_dumpSkipped++; return; }
    if (g_dumpSkipped) hw::Log("[state] dump frame %d: %d more saves with the same bytes, not logged", g_lastDumpFrame, g_dumpSkipped);
    g_lastDumpFrame = frame; g_lastDumpHash = h; g_dumpSkipped = 0;
    DumpState(frame, bytes, "dump");
}
int g_ticks = 0, g_measured = 0, g_loads = 0, g_roundTripBad = 0;
bool g_ready = false;
constexpr int kN = 4096;
struct Series { float v[kN]; int n = 0; void Add(double x) { if (n < kN) v[n++] = (float)x; } };
Series g_get, g_ser, g_lz4, g_deser, g_unwrap, g_load, g_bytes, g_raw;
void Pct(Series& s, float* out) {
    static float tmp[kN];
    if (s.n == 0) { out[0] = out[1] = out[2] = 0; return; }
    std::memcpy(tmp, s.v, sizeof(float) * (size_t)s.n);
    std::sort(tmp, tmp + s.n);
    out[0] = tmp[s.n / 2]; out[1] = tmp[(s.n * 9) / 10]; out[2] = tmp[s.n - 1];
}
uint16_t Fletcher16Bincode(const gen::Arr<uint8_t>& b) {   /* ggrs_ffi: fletcher16(bincode::serialize(&Vec<u8>)): u64 LE length, the bytes */
    uint32_t s1 = 0, s2 = 0;
    uint64_t n = (uint64_t)b.len;
    for (int i = 0; i < 8; i++) { s1 = (s1 + (uint8_t)(n >> (8 * i))) % 255; s2 = (s2 + s1) % 255; }
    for (int i = 0; i < b.len; i++) { s1 = (s1 + b.data[i]) % 255; s2 = (s2 + s1) % 255; }
    return (uint16_t)((s2 << 8) | s1);
}
}

/* the pinned fields (tools/tfex/state-fieldcheck.py), checked on live objects every 30th measured tick, each class's fields once: a
 * reference must hold an object of the PC field's type or a subclass (or null), a number a plausible value (kind 1 bool 0/1, 2 float32
 * finite and |x| < 1e7, 3 enum -16..4095, 4 Vector2). Only what isn't as expected is logged, and a summary per check. */
struct FieldCheck { const char* cls; const char* field; uint32_t off; const char* expected; bool confirmed; int kind; };
const FieldCheck kFieldChecks[] = {
#include "tfexgen_state_fieldcheck.inc"
};
/* mapped and readable (svcQueryMemory; the last region cached): a word that only looks like a pointer is never followed */
bool Readable(uintptr_t p, uintptr_t len) {
    static uintptr_t lo = 0, hi = 0;
    if (p >= lo && p + len <= hi) return true;
    MemoryInfo mi{}; u32 page = 0;
    if (R_FAILED(svcQueryMemory(&mi, &page, p))) return false;
    if (!(mi.perm & Perm_R) || mi.type == 0) return false;
    lo = mi.addr; hi = mi.addr + mi.size;
    return p + len <= hi;
}
bool LooksLikeObject(uintptr_t p) { return p && !(p & 7) && p >= 0x1000000000ull && p < 0x8000000000ull && Readable(p, 16); }
/* the object's runtime type or one of its bases is named `want` (type record: +0x20 the name, +0x88 the base type's getter) */
bool IsA(uintptr_t obj, const char* want) {
    uintptr_t desc = *reinterpret_cast<uintptr_t*>(obj);
    if (desc < Base() || desc >= Base() + 0x4900000 || (desc & 7) || !Readable(desc, 8)) return false;
    uintptr_t rec = *reinterpret_cast<uintptr_t*>(desc);
    for (int depth = 0; depth < 24 && rec >= 0x10000 && !(rec & 7) && Readable(rec, 0x90); depth++) {
        uintptr_t name = reinterpret_cast<uintptr_t*>(rec)[4];
        if (name >= Base() + 0x2863000 && name < Base() + 0x450d168 && Readable(name, 2)) {
            const uint16_t* u = reinterpret_cast<const uint16_t*>(name);
            int k = 0;
            while (want[k] && Readable(name + 2 * k, 2) && u[k] == (uint8_t)want[k]) k++;
            if (!want[k] && Readable(name + 2 * k, 2) && (u[k] < 0x20 || u[k] >= 0x7f)) return true;
        }
        uintptr_t getter = *reinterpret_cast<uintptr_t*>(rec + 0x88);
        if (getter < Base() || getter >= Base() + 0x4900000) break;
        rec = reinterpret_cast<uintptr_t (*)()>(getter)();
    }
    return false;
}
/* 1 as expected, 0 null (a reference), -1 not as expected (logged) */
int CheckOne(void* obj, const FieldCheck& c) {
    uintptr_t at = reinterpret_cast<uintptr_t>(obj) + c.off;
    if (!Readable(at, 8)) { hw::Log("[state] field check %s.%s +%#x: past the object's readable memory", c.cls, c.field, c.off); return -1; }
    if (c.kind == 0) {
        uintptr_t v = *reinterpret_cast<uintptr_t*>(at);
        if (!v) return 0;
        bool module = v >= Base() && v < Base() + 0x4900000 && !std::strcmp(c.expected, "String");   /* a string literal */
        if (module || (LooksLikeObject(v) && IsA(v, c.expected))) return 1;
        char got[64] = "";
        if (LooksLikeObject(v)) rnd::TypeName(reinterpret_cast<void*>(v), got, sizeof got);
        hw::Log("[state] field check %s.%s +%#x: holds %s (%#lx), the PC field is a %s%s", c.cls, c.field, c.off, got[0] ? got : "no object",
                (unsigned long)v, c.expected, c.confirmed ? " (a confirmed pin)" : "");
        return -1;
    }
    bool ok = true; float f[2]; int32_t e;
    switch (c.kind) {
    case 1: ok = *reinterpret_cast<uint8_t*>(at) <= 1; break;
    case 2: case 4:
        std::memcpy(f, reinterpret_cast<void*>(at), c.kind == 4 ? 8 : 4);
        for (int i = 0; i < (c.kind == 4 ? 2 : 1); i++) ok = ok && f[i] == f[i] && f[i] < 1e7f && f[i] > -1e7f;
        break;
    case 3: std::memcpy(&e, reinterpret_cast<void*>(at), 4); ok = e >= -16 && e < 4096; break;
    }
    if (!ok) hw::Log("[state] field check %s.%s +%#x: implausible for a %s (bytes %08x %08x)%s", c.cls, c.field, c.off, c.expected,
                     reinterpret_cast<uint32_t*>(at)[0], reinterpret_cast<uint32_t*>(at)[1], c.confirmed ? " (a confirmed pin)" : "");
    return ok ? 1 : -1;
}
void CheckFields(void* level) {
    static int calls = 0;
    if (calls++ % 30) return;   /* (every 30th measured tick: entities that appear later, each class's fields once) */
    constexpr int kN = sizeof kFieldChecks / sizeof kFieldChecks[0];
    static uint8_t seen[kN];
    int ok = 0, bad = 0, nul = 0, before = 0;
    for (int k = 0; k < kN; k++) before += seen[k];
    auto check = [&](void* obj, const char* cls) {
        for (int k = 0; k < kN; k++) {
            const FieldCheck& c = kFieldChecks[k];
            if (seen[k] || std::strcmp(c.cls, cls)) continue;
            seen[k] = 1;
            int r = CheckOne(obj, c);
            if (r > 0) ok++; else if (r == 0) nul++; else bad++;
        }
    };
    void* layers = gen::Fn<void* (*)(void*)>(bind::m_Scene_get_Layers)(level);
    gen::FromDict<int32_t, void*>(layers, bind::f_Dictionary_2_d0c1_linkSlots, bind::f_Dictionary_2_d0c1_keySlots, bind::f_Dictionary_2_d0c1_valueSlots,
                                  bind::f_Dictionary_2_d0c1_touchedSlots)([&](gen::KV<int32_t, void*> kv) {
        void* ents = gen::Fn<void* (*)(void*)>(bind::m_Layer_get_Entities)(kv.Value);
        int32_t n = ents ? gen::Fn<int32_t (*)(void*)>(bind::m_List_1_a639_get_Count)(ents) : 0;
        for (int32_t i = 0; i < n; i++) {
            void* e = gen::Fn<void* (*)(void*, int32_t)>(bind::m_List_1_a639_get_Item_e2bf)(ents, i);
            char cls[64]; rnd::TypeName(e, cls, sizeof cls);
            check(e, cls);
        }
        return true;
    });
    /* objects that aren't entities: the round logic, the level (Scene's and Level's fields) */
    if (void* rl = gen::Fn<void* (*)(void*)>(bind::m_Session_get_RoundLogic)(gen::Fn<void* (*)(void*)>(bind::m_Level_get_Session)(level))) {
        check(rl, "RoundLogic");
        char cls[64]; rnd::TypeName(rl, cls, sizeof cls); check(rl, cls);
    }
    check(level, "Scene"); check(level, "Level");
    if (ok + bad + nul) hw::Log("[state] field check (%d fields checked before): %d as expected, %d not (a wrong offset), %d null", before, ok, bad, nul);
}

/* ---- the layout dump (hw.txt "statelayout <every>"): no GetState, so nothing of TF.State's runs. Every <every>th tick, the slot map of
 * objects reachable from the level, its round logic and its entities (two references deep): each 8-byte slot holding an object, with
 * its type ("[state] slots <Class> <size>: <off>=<Type> ..."), at most 4 maps per class a match. tools/re/layoutdb.py matches them to
 * the PC fields (docs/tfstate-rollback-measurement.md, phase 1). */
int g_layoutEvery = 0;
struct ClassDumps { char name[48]; int n; int raw; int rawTick; int rawThisTick; };
ClassDumps g_dumped[512]; int g_nDumped = 0;
int DumpsOf(const char* cls) {
    for (int i = 0; i < g_nDumped; i++) if (!std::strcmp(g_dumped[i].name, cls)) return i;
    if (g_nDumped >= 512) return -1;
    std::snprintf(g_dumped[g_nDumped].name, 48, "%s", cls); g_dumped[g_nDumped].n = 0; g_dumped[g_nDumped].raw = 0; g_dumped[g_nDumped].rawTick = -1; g_dumped[g_nDumped].rawThisTick = 0;
    return g_nDumped++;
}
/* rnd::TypeName with every read checked first (a slot can lead into runtime structures whose words only look like pointers) */
const char* SafeTypeName(uintptr_t obj, char* out, int cap) {
    out[0] = 0;
    if (!LooksLikeObject(obj)) return out;
    uintptr_t desc = *reinterpret_cast<uintptr_t*>(obj);
    if (desc < Base() || desc >= Base() + 0x4900000 || (desc & 7) || !Readable(desc, 8)) return out;
    uintptr_t klass = *reinterpret_cast<uintptr_t*>(desc);
    if (klass < 0x10000 || (klass & 7) || !Readable(klass, 0x28)) return out;
    uintptr_t name = reinterpret_cast<uintptr_t*>(klass)[4];
    if (name < Base() + 0x2863000 || name >= Base() + 0x450d168 || !Readable(name, 2)) return out;
    const uint16_t* u = reinterpret_cast<const uint16_t*>(name);
    int k = 0;
    while (k < cap - 1 && Readable(name + 2 * k, 2) && u[k] >= 0x20 && u[k] < 0x7f) { out[k] = (char)u[k]; k++; }
    out[k] = 0;
    return out;
}
/* runtime / collection types: their slots aren't game fields (and lead into the runtime) */
bool NotGame(const char* c) {
    static const char* const kSkip[] = { "Mono", "Action", "Func", "Delegate", "Method", "Type", "String", "List`", "Dictionary", "HashSet",
                                         "Stack`", "Queue`", "Enumerator", "ArrayList", "Object", "Hashtable", "Comparer", "EqualityComparer" };
    for (const char* p : kSkip) if (!std::strncmp(c, p, std::strlen(p)) || std::strstr(c, "Enumerator")) return true;
    return false;
}
/* step B, the raw dump (hw.txt "staterawdump <n>"): each object the slot-map walk visits, its whole instance (the size from its type
 * record, +0x00) appended to sd:/xplay/raw/<Class>.bin as a record: [u32 'TFRW', u32 tick, u32 size, u32 the address's low word] then
 * the bytes. Up to <n> records per class, at most 2 per tick, its ticks 60+ apart (the same entities over the match). tools/re/rawcheck.py checks them. */
int g_rawMax = 0, g_rawTick = 0, g_rawFiles = 0, g_rawRecords = 0, g_rawFailed = 0;
uint32_t InstanceSize(uintptr_t obj) {
    uintptr_t desc = *reinterpret_cast<uintptr_t*>(obj);
    if (desc < Base() || desc >= Base() + 0x4900000 || (desc & 7) || !Readable(desc, 8)) return 0;
    uintptr_t rec = *reinterpret_cast<uintptr_t*>(desc);
    if (rec < 0x10000 || (rec & 7) || !Readable(rec, 4)) return 0;
    uint32_t size = *reinterpret_cast<uint32_t*>(rec);
    return size >= 0x10 && size <= 0x4000 && Readable(obj, size) ? size : 0;
}
void RawDump(void* obj, const char* cls, int k) {
    ClassDumps& d = g_dumped[k];
    if (d.rawTick != g_rawTick) {   /* a new tick for this class: at least 60 ticks after its last one (snapshots spread over the match) */
        if (d.raw > 0 && g_rawTick < d.rawTick + 60) return;
        d.rawTick = g_rawTick; d.rawThisTick = 0;
    }
    if (d.raw >= g_rawMax || d.rawThisTick >= 2) return;
    uint32_t size = InstanceSize(reinterpret_cast<uintptr_t>(obj));
    if (!size || !hw::MountSd()) { g_rawFailed++; return; }
    char path[96]; int at = std::snprintf(path, sizeof path, "sd:/xplay/raw/");
    for (const char* c = cls; *c && at < 90; c++) path[at++] = (std::isalnum((unsigned char)*c) || *c == '_') ? *c : '_';
    std::snprintf(path + at, sizeof path - at, ".bin");
    if (d.raw == 0) { nn::fs::CreateFile(path, 0); g_rawFiles++; }   /* (the folder is emptied at ConfigureRaw) */
    nn::fs::FileHandle h;
    if (nn::fs::OpenFile(&h, path, nn::fs::OpenMode_Write | nn::fs::OpenMode_Append) != 0) { g_rawFailed++; return; }
    long end = 0; nn::fs::GetFileSize(&end, h);
    uint32_t head[4] = { 0x57524654u /* "TFRW" */, (uint32_t)g_rawTick, size, (uint32_t)reinterpret_cast<uintptr_t>(obj) };
    bool ok = nn::fs::WriteFile(h, end, head, sizeof head, nn::fs::WriteOption::CreateOption(0)) == 0 &&
              nn::fs::WriteFile(h, end + (long)sizeof head, obj, size, nn::fs::WriteOption::CreateOption(0)) == 0;
    if (ok) nn::fs::FlushFile(h);
    nn::fs::CloseFile(h);
    if (ok) { d.raw++; d.rawThisTick++; g_rawRecords++; } else g_rawFailed++;
}
void DumpSlots(void* obj, int depth, int& lines) {
    if (!obj || lines > 300) return;
    char cls[48]; SafeTypeName(reinterpret_cast<uintptr_t>(obj), cls, sizeof cls);
    if (NotGame(cls)) return;
    if (!cls[0] || std::strchr(cls, '[')) return;   /* (arrays: their elements aren't fields) */
    int k = DumpsOf(cls);
    if (k >= 0 && g_rawMax > 0) RawDump(obj, cls, k);
    if (k < 0 || g_dumped[k].n >= 4) return;
    g_dumped[k].n++; lines++;
    /* (lines of at most ~400 characters: hwlog cuts longer ones; a continuation is "[state] slots+ <Class>:") */
    char line[480]; int at = std::snprintf(line, sizeof line, "[state] slots %s:", cls);
    void* kids[160]; int nk = 0;
    for (uint32_t off = 0x10; off < 0x800; off += 8) {
        if (at > 400) { hw::Log("%s", line); at = std::snprintf(line, sizeof line, "[state] slots+ %s:", cls); }
        if (!Readable(reinterpret_cast<uintptr_t>(obj) + off, 8)) break;
        uintptr_t v = *reinterpret_cast<uintptr_t*>(reinterpret_cast<uintptr_t>(obj) + off);
        if (!LooksLikeObject(v)) continue;
        char tn[40]; SafeTypeName(v, tn, sizeof tn);
        if (!tn[0]) continue;
        at += std::snprintf(line + at, sizeof line - at, " %x=%s", off, tn);
        if (nk < 160) kids[nk++] = reinterpret_cast<void*>(v);
    }
    hw::Log("%s", line);
    if (depth > 0) for (int i = 0; i < nk; i++) DumpSlots(kids[i], depth - 1, lines);
}
int g_spawnAt = -1;
void LayoutTick(void* level, int frame) {
    if (frame == g_spawnAt) {   /* the spawn scene (TEST ONLY: glue/StateSpawn.cs) */
        int n = tfex::gen::GlueStateSpawn::Arrows(level);
        int m = tfex::gen::GlueStateSpawn::Entities(level);
        hw::Log("[state] spawn at tick %d: %d arrows, %d other entities", frame, n, m);
    }
    if (g_spawnAt >= 0 && frame == g_spawnAt + 30) hw::Log("[state] spawn at tick %d: %d team revivers", frame, tfex::gen::GlueStateSpawn::Revivers(level));
    if (g_layoutEvery <= 0 || frame % g_layoutEvery) return;
    int lines = 0;
    g_rawTick = frame;
    if (g_rawMax > 0 && frame % (g_layoutEvery * 30) == 0)
        hw::Log("[state] raw dump at tick %d: %d class files, %d records, %d failed", frame, g_rawFiles, g_rawRecords, g_rawFailed);
    DumpSlots(level, 2, lines);
    DumpSlots(gen::Fn<void* (*)(void*)>(bind::m_Session_get_RoundLogic)(gen::Fn<void* (*)(void*)>(bind::m_Level_get_Session)(level)), 2, lines);
    void* layers = gen::Fn<void* (*)(void*)>(bind::m_Scene_get_Layers)(level);
    gen::FromDict<int32_t, void*>(layers, bind::f_Dictionary_2_d0c1_linkSlots, bind::f_Dictionary_2_d0c1_keySlots, bind::f_Dictionary_2_d0c1_valueSlots,
                                  bind::f_Dictionary_2_d0c1_touchedSlots)([&](gen::KV<int32_t, void*> kv) {
        void* ents = gen::Fn<void* (*)(void*)>(bind::m_Layer_get_Entities)(kv.Value);
        int32_t n = ents ? gen::Fn<int32_t (*)(void*)>(bind::m_List_1_a639_get_Count)(ents) : 0;
        for (int32_t i = 0; i < n; i++) DumpSlots(gen::Fn<void* (*)(void*, int32_t)>(bind::m_List_1_a639_get_Item_e2bf)(ents, i), 2, lines);
        return true;
    });
}
void ConfigureSpawn(int tick) { g_spawnAt = tick; hw::Log("[state] spawn scene at tick %d (TEST ONLY)", tick); }
void ConfigureRaw(int n) {
    g_rawMax = n;
    if (n > 0 && hw::MountSd()) { nn::fs::DeleteDirectoryRecursively("sd:/xplay/raw"); nn::fs::CreateDirectory("sd:/xplay/raw"); }
    hw::Log("[state] raw dump %s: up to %d records per class (with statelayout's walk)", n > 0 ? "on" : "off", n);
}
void ConfigureLayout(int every) { g_layoutEvery = every; hw::Log("[state] layout dump %s: every %d ticks", every > 0 ? "on" : "off", every); }

void DumpSaved(int frame, const gen::Arr<uint8_t>& bytes) { if (g_dumpEvery > 0 && frame % g_dumpEvery == 0) DumpSavedFrame(frame, bytes); }
void ConfigureDump(int every) { g_dumpEvery = every; hw::Log("[state] state dump %s: every %d measured frames (statediff.py)", every > 0 ? "on" : "off", every); }
/* Every C++ throw (the game's managed exceptions are C++ throws) logged first-chance, with the throwing call's return address in the
 * game (symbolize with tools/re callmap) and the thrown object's managed type when it is one: the game rethrows from cleanup blocks
 * (RollcallElement.EnterJoined's foreach, Scene.Update), so a crash report alone loses where it came from (the console's archer-select
 * crash, 2026-11-06). On from boot (InstallThrowLog). Log bounds: the first 8 throws, then each new throwing site once (a table of 64
 * sites; full: one line), with how many throws there were. The game's PLT slot for the SDK's __cxa_throw (main +0x47223c0, its stub
 * +0x2861ac0) points at ThrowShim: it logs, pops its own frame and jumps to the real one, so the unwinder never sees this module's
 * frames (built without unwind tables). A trampoline hook on the SDK function crashed the boot. */
extern "C" uintptr_t g_realThrow;
uintptr_t g_realThrow = 0;
extern "C" void ThrowLog(void* obj, uintptr_t lr) {
    static int n = 0, sitesUsed = 0;
    static uintptr_t sites[64];
    static bool fullSaid = false;
    n++;
    bool fresh = n <= 8;
    if (!fresh) {
        int k = 0;
        while (k < sitesUsed && sites[k] != lr) k++;
        if (k < sitesUsed) return;   /* a site seen before */
        if (sitesUsed == 64) {
            if (!fullSaid) { fullSaid = true; hw::Log("[pools] ERROR throw log: its site table is full (64): new throwing sites not logged"); }
            return;
        }
        fresh = true;
    }
    if (sitesUsed < 64) { int k = 0; while (k < sitesUsed && sites[k] != lr) k++; if (k == sitesUsed) sites[sitesUsed++] = lr; }
    uintptr_t b = Base();
    char tn[64] = "";
    void* managed = obj && Readable(reinterpret_cast<uintptr_t>(obj), 8) ? *reinterpret_cast<void**>(obj) : nullptr;
    if (managed && LooksLikeObject(reinterpret_cast<uintptr_t>(managed))) SafeTypeName(reinterpret_cast<uintptr_t>(managed), tn, sizeof tn);
    bool inMain = lr >= b && lr < b + 0x4900000;
    hw::Log("[throw] #%d from %s+%#lx: %s (sites %d/64)", n, inMain ? "main" : "elsewhere", (unsigned long)(inMain ? lr - b : lr),
            tn[0] ? tn : "(not a managed object)", sitesUsed);
}
extern "C" __attribute__((naked)) void ThrowShim() {
    asm volatile(
        "stp x29, x30, [sp, #-48]!\n"
        "stp x0, x1, [sp, #16]\n"
        "str x2, [sp, #32]\n"
        "mov x1, x30\n"
        "bl ThrowLog\n"
        "ldp x0, x1, [sp, #16]\n"
        "ldr x2, [sp, #32]\n"
        "ldp x29, x30, [sp], #48\n"
        "adrp x16, g_realThrow\n"
        "ldr x16, [x16, :lo12:g_realThrow]\n"
        "br x16\n");
}
void InstallThrowLog() {
    static bool hooked = false;
    if (hooked) return;
    uintptr_t slot = Base() + 0x47223c0;
    MemoryInfo mi{}; u32 page = 0;
    bool writable = R_SUCCEEDED(svcQueryMemory(&mi, &page, slot)) && (mi.perm & Perm_W);
    if (writable && *reinterpret_cast<uintptr_t*>(slot)) {
        g_realThrow = *reinterpret_cast<uintptr_t*>(slot);
        *reinterpret_cast<uintptr_t*>(slot) = reinterpret_cast<uintptr_t>(&ThrowShim);
        hooked = true;
        hw::Log("[throw] first-chance throw log on: the __cxa_throw slot goes through ThrowShim (the first 8, then each new site)");
    } else hw::Log("[throw] ERROR first-chance throw log not installed: the __cxa_throw slot isn't writable (perm %#x)", (unsigned)mi.perm);
}
void Configure(int every, int loadEvery) {
    g_every = every; g_loadEvery = loadEvery;
    if (loadEvery > 0) InstallThrowLog();
    hw::Log("[state] measurement %s: every %d ticks, LoadState + round trip every %d measured ticks", every > 0 ? "on" : "off", every, loadEvery);
}
bool On() { return g_every > 0 || g_layoutEvery > 0; }

/* Before every lobby match (TF.State's patches are on by default): its classes and statics made outside the rollback window (a lazy one
 * made inside it is undone by a restore under the module's pointer), its context reset as a fresh TF.EX's (TF.EX resets them at the
 * previous match's end: NetplayManager.Reset -> ResetSession, GameContext.Reset -> ResetMatch; the Switch has no GameContext.Reset), and
 * the DynamicData side table emptied (the last match's entries). */
/* TF.State's IRngService.Gameplay, a System.Random rnd.cpp draws from the gameplay stream: one object for the Switch's whole run, made before
 * the first lobby match (outside the rollback window: a restore would free one made inside it under the module's pointer), kept alive
 * in scanned uncollectable memory */
namespace {
struct DlgName { void* fn; const char* name; };
DlgName g_dlgNames[16];
int g_nDlgNames = 0;
void* DlgFnOf(void* d) { return d ? *reinterpret_cast<void**>(reinterpret_cast<uintptr_t>(d) + 0x10) : nullptr; }   /* game::brute::DlgFn */
}
void* NameDelegate(void* d, const char* name) {
    void* fn = DlgFnOf(d);
    if (!fn) return d;
    for (int i = 0; i < g_nDlgNames; i++) if (g_dlgNames[i].fn == fn) return d;
    if (g_nDlgNames < (int)(sizeof g_dlgNames / sizeof g_dlgNames[0])) g_dlgNames[g_nDlgNames++] = DlgName{ fn, name };
    else gen::PoolFull("delegate method names (statert::NameDelegate)", (int)(sizeof g_dlgNames / sizeof g_dlgNames[0]));
    return d;
}
void* ActionName(void* d) {
    static const struct { uintptr_t off; const char* name; } kGame[] = {
        { 0xaec0a0, "ShieldDelegate" },      /* Player..ctor's shield lambda (the PC's <.ctor>b__250_2, TF.State's INVENTORY_SHIELD_DELEGATE) */
        { 0xaec110, "InvisibleDelegate" },   /* its invisible lambda (<.ctor>b__250_3: Invisible = true) */
        { 0xb06fc0, "Gain" },                /* PlayerWings.Gain (the constructor schedules it on the wings) */
        { 0xaf2760, "DodgeCooldown" },       /* Player.DodgeCooldown */
        { 0xaf3380, "FinishAutoMove" },      /* Player.FinishAutoMove */
    };
    uintptr_t fn = reinterpret_cast<uintptr_t>(DlgFnOf(d));
    const char* name = nullptr;
    for (auto& k : kGame) if (fn == gen::Base() + k.off) name = k.name;
    for (int i = 0; !name && i < g_nDlgNames; i++) if (reinterpret_cast<uintptr_t>(g_dlgNames[i].fn) == fn) name = g_dlgNames[i].name;
    if (!name) {
        static int logged = 0;
        if (logged++ < 4) hw::Log("[state] ERROR: a Player scheduler action of unknown method (body +%#lx): TF.State's LoadState can't rebuild it", (unsigned long)(fn - gen::Base()));
        name = "<unknown>";
    }
    return gen::ManagedUtf8(name, (int)std::strlen(name));
}
/* Per-seat arrays (RoundLogic.Kills, Session.Scores / OldScores outside a team match, CrownChangeEvent.HasCrown) have one entry per seat:
 * the game's own seat count on the Switch (TFGame.Players: 6 today, 8 with the 8-player mod), the PC's in the state (4, 8 with WiderSet).
 * A team array (Session.Scores / OldScores in a team match: `TeamMode ? 2 : seats` on both) is no seat array and keeps its length both
 * ways (a team match's scores were saved as seats: the parity sweep, 2026-10-24). No player count is assumed: both counts are read. */
int32_t GameSeats() {
    return gen::GameArr<bool>((gen::Cctor(gen::bind::c_TFGame), gen::Static<void*>(gen::bind::f_TFGame_Players)), gen::bind::m_Array_get_Length).len;
}
int32_t PcSeats() { return ::pcgameplay::Wide() ? 8 : 4; }   /* TF.EX's MatchSettings seats: 4, WiderSet's 8 */
void SeatsShort(int32_t game, int32_t pc) {
    static int n = 0;
    if (n++ < 3) hw::Log("[state] ERROR: a per-seat struct array (Session.MatchStats) has the game's %d seats, the PC's state %d: more than the padding holds (the checksum differs)", game, pc);
}
template <class T> gen::Arr<T> Resized(const gen::Arr<T>& src, int32_t n, uintptr_t type) {
    auto a = gen::NewArr<T>(type, n);
    for (int32_t i = 0; a.data && i < n; i++) a.data[i] = i < src.len ? src.data[i] : T{};
    return a;
}
gen::Arr<int32_t> SeatsToPc(const gen::Arr<int32_t>& game) { return Resized(game, game.len == GameSeats() ? PcSeats() : game.len, gen::bind::t_Int32_90aa); }
gen::Arr<int32_t> SeatsToGame(const gen::Arr<int32_t>& state) {
    int32_t g = GameSeats();
    return Resized(state, state.len == PcSeats() && g > 0 ? g : state.len, gen::bind::t_Int32_90aa);
}
gen::Arr<bool> SeatsToPc(const gen::Arr<bool>& game) { return Resized(game, game.len == GameSeats() ? PcSeats() : game.len, gen::bind::t_Boolean_4da6); }
gen::Arr<bool> SeatsToGame(const gen::Arr<bool>& state) {
    int32_t g = GameSeats();
    return Resized(state, state.len == PcSeats() && g > 0 ? g : state.len, gen::bind::t_Boolean_4da6);
}
void BeforeMatch() {
    uint64_t t0 = svcGetSystemTick();
    tfex::gen::SideReport();
    int n = tfex::gen::EnsureStateClasses();
    /* TF.State's StateContext (its DeterministicRandom: IRngService.Gameplay) made here, outside any rollback window, if nothing has yet */
    void* rng = tfex::gen::Auto_StateContext::GetGameplayRandom(tfex::gen::Auto_ServiceCollections::ResolveStateContext());
    if (!rng) hw::Log("[state] ERROR: TF.State's StateContext has no DeterministicRandom (its class not made?)");
    tfex::gen::State_Api::ResetMatch(nullptr);
    tfex::gen::State_Api::ResetSession(nullptr);
    g_ready = true;
    hw::Log("[state] TF.State's classes made before the match: %d, its context reset (%.2f ms)", n, Ms(svcGetSystemTick() - t0));
}

/* TEST ONLY (statemeasure with LoadState): each stuck arrow, its StuckTo's actualDepth, what TF.State's GetEntityByDepth finds for that
 * depth (and for the arrow's own), before and after a load: why StuckToActualDepth came back 0 after a round trip */
void ArrowProbe(void* level, const char* when) {
    static int lines = 0;
    if (lines >= 40) return;
    void* layer = tfex::gen::State_Level::GetGameplayLayer(level);
    void* ents = layer ? gen::Fn<void* (*)(void*)>(bind::m_Layer_get_Entities)(layer) : nullptr;
    int32_t n = ents ? gen::Fn<int32_t (*)(void*)>(bind::m_List_1_a639_get_Count)(ents) : 0;
    for (int32_t i = 0; i < n && lines < 40; i++) {
        void* e = gen::Fn<void* (*)(void*, int32_t)>(bind::m_List_1_a639_get_Item_e2bf)(ents, i);
        if (!gen::As(e, gen::TypeOf(bind::t_Arrow))) continue;
        int32_t st = gen::Fn<int32_t (*)(void*)>(bind::m_Arrow_get_State)(e);
        void* to = gen::Fn<void* (*)(void*)>(bind::m_Arrow_get_StuckTo)(e);
        if (st != 2 && !to) continue;   /* (ArrowStates: Shooting 0, Gravity 1, Stuck 2, ...) */
        double d = to ? gen::Fld<double>(to, bind::f_Entity_actualDepth) : 0, own = gen::Fld<double>(e, bind::f_Entity_actualDepth);
        void* found = to ? tfex::gen::State_Level::GetEntityByDepth(level, d) : nullptr;
        void* self = tfex::gen::State_Level::GetEntityByDepth(level, own);
        char tn[48] = "-", fn[48] = "-";
        if (to) SafeTypeName(reinterpret_cast<uintptr_t>(to), tn, sizeof tn);
        if (found) SafeTypeName(reinterpret_cast<uintptr_t>(found), fn, sizeof fn);
        lines++;
        hw::Log("[state] arrow probe %s: state %d, own depth %.4f (found by depth: %s), stuck to %s depth %.4f -> by depth: %s", when, st, own,
                self == e ? "itself" : self ? "another" : "nothing", tn, d, fn);
    }
}
void MeasureTick(void* level, int frame) {
    if (level) LayoutTick(level, frame);
    if (g_every <= 0 || !g_ready || !level) return;
    if (g_ticks++ % g_every) return;
    tfex::gen::g_traceOnly = true; tfex::gen::g_traceNow = g_measured < 2;   /* (TFEX_TRACE_UNITS builds: the first ticks' calls) */
    CheckFields(level);
    namespace L = tfex::gen::State_Level;
    namespace M = tfex::gen::GlueStateMsgPack;
    tfex::gen::State_Api::SetCurrentFrame(nullptr, frame);
    uint64_t t0 = svcGetSystemTick();
    void* gs = L::GetState(level);
    uint64_t t1 = svcGetSystemTick();
    gen::Arr<uint8_t> bytes = M::Serialize(gs);
    if (g_dumpEvery > 0 && frame % g_dumpEvery == 0) DumpSavedFrame(frame, bytes);
    uint64_t t2 = svcGetSystemTick();
    double lz4 = g_lastLz4Ms;
    void* back = M::Deserialize(bytes);
    uint64_t t3 = svcGetSystemTick();
    double unwrap = g_lastUnwrapMs;
    g_get.Add(Ms(t1 - t0)); g_ser.Add(Ms(t2 - t1)); g_lz4.Add(lz4); g_deser.Add(Ms(t3 - t2)); g_unwrap.Add(unwrap);
    g_bytes.Add(bytes.len); g_raw.Add(g_lastRaw);
    g_measured++;
    /* the reader against the writer, without a load: what Deserialize read must serialize to the same bytes (a reader that reads one
     * field more or less than the writer wrote shifts every value after it: Session.Miasma read null, a crash in LoadState) */
    if (back) {
        gen::Arr<uint8_t> re = M::Serialize(back);
        if (!(re.len == bytes.len && std::memcmp(re.data, bytes.data, (size_t)bytes.len) == 0)) {
            static int bad = 0;
            int at = 0; while (at < re.len && at < bytes.len && re.data[at] == bytes.data[at]) at++;
            if (bad++ < 4) {
                hw::Log("[state] reader check at frame %d: DIFFERENT (%d -> %d bytes, first difference at byte %d)", frame, bytes.len, re.len, at);
                DumpState(frame, bytes, "rdump"); DumpState(frame, re, "rdump2");   /* statediff.py --tags rdump,rdump2 */
            }
        }
    }
    if (g_measured <= 5 || g_measured % 60 == 0)
        hw::Log("[state] frame %d: %d bytes (%d before LZ4), fletcher16 %04x (bincode), GetState %.2f ms, serialize %.2f ms (LZ4 %.2f), deserialize %.2f ms (unLZ4 %.2f)%s",
                frame, bytes.len, g_lastRaw, Fletcher16Bincode(bytes), Ms(t1 - t0), Ms(t2 - t1), lz4, Ms(t3 - t2), unwrap, back ? "" : ", DESERIALIZE FAILED");
    if (g_loadEvery > 0 && back && g_measured % g_loadEvery == 0) {
        tfex::gen::g_traceNow = g_loads == 0;   /* (TFEX_TRACE_UNITS builds: the first LoadState's calls) */
        uint64_t l0 = svcGetSystemTick();
        ArrowProbe(level, "before");
        tfex::gen::State_Api::LoadGameStateBytes(nullptr, bytes);   /* TF.State's own entry (as TF.EX's NetplayLogic): StateFlags.IsRestoring around the load (LevelScreenShakePatch and others read it) */
        uint64_t l1 = svcGetSystemTick();
        ArrowProbe(level, "after");
        g_load.Add(Ms(l1 - l0)); g_loads++;
        gen::Arr<uint8_t> again = M::Serialize(L::GetState(level));
        bool same = again.len == bytes.len && std::memcmp(again.data, bytes.data, (size_t)bytes.len) == 0;
        if (!same) {
            int at = 0; while (at < again.len && at < bytes.len && again.data[at] == bytes.data[at]) at++;
            if (g_roundTripBad++ < 8) {
                hw::Log("[state] round trip at frame %d: DIFFERENT (%d -> %d bytes, first difference at byte %d)", frame, bytes.len, again.len, at);
                if (g_dumpEvery > 0) { DumpState(frame, bytes, "dump"); DumpState(frame, again, "dump2"); }   /* statediff.py --tags dump dump2 */
            }
        } else if (g_loads <= 3) hw::Log("[state] round trip at frame %d: identical (%d bytes), LoadState %.2f ms", frame, bytes.len, Ms(l1 - l0));
    }
    tfex::gen::g_traceNow = false;
    if (g_measured % 300 == 0) LogSummary("periodic");
}

void LogSummary(const char* why) {
    if (!On() || g_measured == 0) return;
    float a[3], b[3], c[3], d[3], e[3], f[3], g[3], r[3];
    Pct(g_get, a); Pct(g_ser, b); Pct(g_lz4, c); Pct(g_deser, d); Pct(g_unwrap, e); Pct(g_load, f); Pct(g_bytes, g); Pct(g_raw, r);
    hw::Log("[state] summary (%s, %d ticks measured, %d loads, %d round trips different), ms p50/p90/max: GetState %.2f/%.2f/%.2f; serialize %.2f/%.2f/%.2f "
            "(LZ4 %.2f/%.2f/%.2f); deserialize %.2f/%.2f/%.2f (unLZ4 %.2f/%.2f/%.2f); LoadState %.2f/%.2f/%.2f; bytes %.0f/%.0f/%.0f (raw %.0f/%.0f/%.0f)",
            why, g_measured, g_loads, g_roundTripBad, a[0], a[1], a[2], b[0], b[1], b[2], c[0], c[1], c[2], d[0], d[1], d[2], e[0], e[1], e[2], f[0], f[1], f[2], g[0], g[1], g[2], r[0], r[1], r[2]);
}

/* ---- the GC census (on by default, hw.txt gccensus 0 turns it off; the 1200th and 4800th save of a session): what the GC's mark goes through. Boehm keeps the mark bits of its last collection until the next one,
 * so the objects marked now are what that collection found live. The blocks are walked as GC_apply_to_all_blocks (+0x285bd80) does:
 * GC_all_bottom_indices (+0x48e2a38), a bottom index = 1024 header pointers, asc_link +0x2000, key +0x2010 (a value below 0x1000 is a
 * forwarding count or empty); a block header (GC_clear_hdr_marks +0x285cdf0): kind +0x18 (0 pointer-free, 1 normal, 2 / 3 uncollectable),
 * flags +0x19 (4: free), object size +0x20, mark bits from +0x40 (a bit per 16-byte granule; a large object's bit 0). Live objects with
 * pointers are counted by runtime type (the class descriptor at the object's first word, named as rnd::TypeName does); reads only, each
 * region checked readable first (it runs while other threads may allocate). */
bool g_gcCensus = true;
void GcCensusOn(bool on) { g_gcCensus = on; }
void GcCensus() {
    if (!g_gcCensus) return;
    uint64_t t0 = svcGetSystemTick();
    struct Row { uintptr_t desc; uint64_t n, bytes; };
    static Row rows[2048];
    std::memset(rows, 0, sizeof rows);
    uint64_t kindN[8] = {}, kindB[8] = {}, blocks = 0, unnamed = 0, dropped = 0;
    auto add = [&](uintptr_t desc, uint64_t sz) {
        uint32_t h = (uint32_t)(desc >> 3) & 2047;
        for (int k = 0; k < 2048; k++, h = (h + 1) & 2047)
            if (rows[h].desc == desc || !rows[h].desc) { rows[h].desc = desc; rows[h].n++; rows[h].bytes += sz; return; }
        dropped += sz;
    };
    uintptr_t lo = gen::Base(), hi = gen::Base() + 0x4900000;
    /* readable, with its own two regions remembered (the headers and the blocks are in different ones: Readable's single one would ask
     * the kernel for every block) */
    struct Region { uintptr_t lo, hi; } seen[2] = {};
    int next = 0;
    auto readable = [&](uintptr_t p, uintptr_t len) {
        for (auto& r : seen) if (p >= r.lo && p + len <= r.hi) return true;
        MemoryInfo mi{}; u32 page = 0;
        if (R_FAILED(svcQueryMemory(&mi, &page, p)) || !(mi.perm & Perm_R) || mi.type == 0) return false;
        seen[next] = Region{mi.addr, mi.addr + mi.size}; next ^= 1;
        return p + len <= mi.addr + mi.size;
    };
    for (uintptr_t bi = *reinterpret_cast<uintptr_t*>(gen::Base() + 0x48e2a38); bi && readable(bi, 0x2018); bi = *reinterpret_cast<uintptr_t*>(bi + 0x2000)) {
        uint64_t key = *reinterpret_cast<uint64_t*>(bi + 0x2010);
        for (int64_t j = 1023; j >= 0;) {
            uintptr_t hdr = reinterpret_cast<uintptr_t*>(bi)[j];
            if (hdr < 0x1000) { j -= hdr ? (int64_t)hdr : 1; continue; }
            j--;
            if (!readable(hdr, 0x68) || (*reinterpret_cast<uint8_t*>(hdr + 0x19) & 4)) continue;
            blocks++;
            uintptr_t block = ((key << 10) + (uint64_t)(j + 1)) << 12;
            int kind = *reinterpret_cast<uint8_t*>(hdr + 0x18) & 7;
            uint64_t sz = *reinterpret_cast<uint64_t*>(hdr + 0x20);
            const uint64_t* marks = reinterpret_cast<const uint64_t*>(hdr + 0x40);
            if (!sz || !readable(block, 0x1000)) continue;
            for (uint64_t off = 0; off == 0 || (sz <= 0x800 && off + sz <= 0x1000); off += sz) {
                uint64_t bit = off >> 4;
                if (!(marks[bit >> 6] >> (bit & 63) & 1)) { if (sz > 0x800) break; continue; }
                kindN[kind]++; kindB[kind] += sz;
                if (kind == 0 || kind == 3) { if (sz > 0x800) break; continue; }
                uintptr_t desc = *reinterpret_cast<uintptr_t*>(block + off);
                if (desc < lo || desc >= hi || (desc & 7)) { unnamed += sz; desc = 1; }
                add(desc, sz);
                if (sz > 0x800) break;
            }
        }
    }
    hw::Log("[gc] census (gccensus): %llu blocks; live: pointer-free %.2f MB (%llu), normal %.2f MB (%llu), uncollectable %.2f MB (%llu), "
            "uncollectable pointer-free %.2f MB (%llu), other kinds %.2f MB; %.1f ms", (unsigned long long)blocks, kindB[0] / 1048576.0,
            (unsigned long long)kindN[0], kindB[1] / 1048576.0, (unsigned long long)kindN[1], kindB[2] / 1048576.0, (unsigned long long)kindN[2],
            kindB[3] / 1048576.0, (unsigned long long)kindN[3], (kindB[4] + kindB[5] + kindB[6] + kindB[7]) / 1048576.0, Ms(svcGetSystemTick() - t0));
    bool taken[2048] = {};
    for (int k = 0; k < 40; k++) {
        int best = -1;
        for (int i = 0; i < 2048; i++) if (rows[i].desc && !taken[i] && (best < 0 || rows[i].bytes > rows[best].bytes)) best = i;
        if (best < 0) break;
        taken[best] = true;
        char nm[96] = "";
        uintptr_t desc = rows[best].desc;
        if (desc == 1) std::snprintf(nm, sizeof nm, "(no class descriptor: native or runtime data)");
        else if (Readable(desc, 8)) {
            uintptr_t klass = *reinterpret_cast<uintptr_t*>(desc);
            uintptr_t name = klass >= 0x10000 && !(klass & 7) && Readable(klass, 0x28) ? reinterpret_cast<uintptr_t*>(klass)[4] : 0;
            if (name >= gen::Base() + 0x2863000 && name < gen::Base() + 0x450d168) {
                const uint16_t* u = reinterpret_cast<const uint16_t*>(name);
                int c = 0; while (c < (int)sizeof nm - 1 && u[c] >= 0x20 && u[c] < 0x7f) { nm[c] = (char)u[c]; c++; } nm[c] = 0;
            }
        }
        if (!nm[0]) std::snprintf(nm, sizeof nm, "(descriptor +%#lx)", (unsigned long)(desc - gen::Base()));
        hw::Log("[gc]   %-72s %7llu objects %8.1f KB", nm, (unsigned long long)rows[best].n, rows[best].bytes / 1024.0);
    }
    if (dropped) hw::Log("[gc]   (%.1f KB in types past the census's 2048 rows)", dropped / 1024.0);
}
}
}   /* namespace tfex::gen */
