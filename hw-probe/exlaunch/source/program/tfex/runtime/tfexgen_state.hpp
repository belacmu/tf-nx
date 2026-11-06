/* TF.State's state path on the Switch (the rollback measurement, docs/tfstate-rollback-measurement.md), OUR OWN CODE: the glue's [Cpp]
 * externs (translator/glue/State.cs, StateMsgPack.cs) and the measurement hook. Nothing here runs unless hw.txt "statemeasure" asks. */
#pragma once
#include "tfexgen_code_rt.hpp"

namespace tfex::gen::statert {
/* Delegate.Method.Name for TF.State's Player scheduler (ActionExtensions.ToNames / PlayerExtensions.ToActions): the name of the method a
 * delegate runs. The Switch's own bodies by address (its Player constructor's shield / invisible lambdas under TF.State's names, PlayerWings.Gain,
 * Player.DodgeCooldown / FinishAutoMove); a delegate the generated code made over a TF.EX method registers its name (NameDelegate) */
void* NameDelegate(void* d, const char* name);
void* ActionName(void* d);   /* a managed string */
/* per-seat arrays (RoundLogic.Kills, Session.Scores / OldScores outside a team match): the game's own seat count on the Switch
 * (TFGame.Players, read: 6 today, 8 with the 8-player mod), the PC's in the state (4, 8 with WiderSet); any other length (a team
 * match's 2 scores) is kept as is. A load gives the game its own (S-native) */
gen::Arr<int32_t> SeatsToPc(const gen::Arr<int32_t>& game);
gen::Arr<int32_t> SeatsToGame(const gen::Arr<int32_t>& state);
gen::Arr<bool> SeatsToPc(const gen::Arr<bool>& game);       /* (bool[]: CrownChangeEvent.HasCrown) */
gen::Arr<bool> SeatsToGame(const gen::Arr<bool>& state);
int32_t GameSeats();
int32_t PcSeats();
void SeatsShort(int32_t game, int32_t pc);
/* a per-seat array of game structs read into the state (TF.State v0.27's Session.MatchStats; GetState reads it, LoadState never writes it
 * back): the first PcSeats() seats, a view (no copy). With fewer seats than the PC's (the Switch's 6 in a wide match, WiderSet's 8) the
 * extra seats are default structs, as the PC's unused seats are: a copy in a static buffer (game thread only), valid until the next call
 * (read at once by the caller; the game's own array still holds what its structs point to). More than kMaxSeats: logged (SeatsShort). */
template <class T> gen::Arr<T> SeatsToPcView(gen::Arr<T> game) {
    constexpr int32_t kMaxSeats = 8;
    int32_t g = GameSeats(), pc = PcSeats();
    if (game.len == g && pc < g) game.len = pc;
    else if (game.len == g && pc > g) {
        if (pc > kMaxSeats) { SeatsShort(g, pc); return game; }
        static T padded[kMaxSeats];
        for (int32_t i = 0; i < pc; i++) padded[i] = i < g ? game.data[i] : T{};
        return gen::Arr<T>{padded, pc};
    }
    return game;
}
/* TF.State's gameplay stream (StateContext's DeterministicRandom, generated): its four state words folded into one (the tick log's rng=) */
uint64_t RngFold();

/* MessagePack-CSharp 2.5's Lz4BlockArray (MessagePackSerializer.ToLZ4BinaryCore): under 64 bytes the bytes as they are; else
 * [array(blocks + 1), ext 98 (the blocks' lengths), bin32 per block] with its own LZ4 codec (MessagePack.LZ4.LZ4Codec, the 64-bit
 * encoder, ported: lz4net's, not today's lz4). A block is one 32 KB segment of its pooled Sequence; past 32 KB the split is
 * approximated (logged). */
gen::Arr<uint8_t> Lz4BlockArray(const gen::Arr<uint8_t>& raw);
/* system ticks of the last Lz4BlockArray's entry / exit and Lz4Unwrap's exit (0 until first used; payloads under 64 bytes skip them): the rollback seam's
 * save / load phase split (GetState + encode | LZ4 | rest; decode | LoadState) */
extern uint64_t g_stampLz4In, g_stampLz4Out, g_stampUnwrapOut;
/* the profiling regeneration's (TFEX_PROF_UNITS) per-method costs: ProfBegin before a save / load, ProfEnd(bucket) adds what it cost to that bucket (0 save,
 * 1 load), ProfDump logs a bucket's top methods by self time (per call of the save / load: `n` of them) and clears it. No-ops in a normal build. */
void ProfBegin();
void ProfEnd(int bucket);
void ProfDump(int bucket, const char* tag, int n);
/* TF.State's LevelExtensions entity helpers, TFEX-DIFF S-performance (translate.toml [[replace]]): GetAll<T>, Get<T>, DeleteAll<T>, Delete<T> with the
 * same results in the same order, over a per-layer index of the entity lists (rebuilt when a list's _items / _size / _version change) instead of a
 * full scan with a type test per entity per type, and without the predicate delegate TF.State allocates per layer for toAdd's RemoveAll when
 * toAdd is empty (the usual case). A save / load runs ~70 of these; on the console they were a third of a save (2026-10-24 profile). */
gen::Snap<void*> EntGetAll(void* level, void* type);
void* EntGet(void* level, void* type);
void EntDeleteAll(void* level, void* type);
void EntDelete(void* level, void* type);
/* The game's Boehm GC, watched through its GC_on_collection_event hook (no code patched; a callback it already had is still called): every
 * collection counted and timed, whole (GC_EVENT_START to GC_EVENT_END) and by phase (stopping the world, mark, starting it, finalize +
 * reclaim). GcWatch installs it (idempotent); GcSummary writes the counts, times and heap sizes since the last call into buf. */
void GcWatch();
void GcDivisor(int d);   /* hw.txt gcdivisor <d>: Boehm's GC_free_space_divisor (the game sets 2; the Switch's default 1); applied by GcWatch */
void GcMin(int mb);       /* hw.txt gcmin <MB>: the least a collection waits for (default 40; 0 = Boehm's own threshold); applied by GcWatch */
void GcSummary(char* buf, int cap);
void GcCensusOn(bool on);   /* hw.txt gccensus 0 turns it off: GcCensus logs the GC's live objects by kind and type (the 1200th, 4800th save) */
void GcCensus();
/* the reverse: raw MessagePack bytes (an uncompressed payload passes through) */
gen::Arr<uint8_t> Lz4Unwrap(const gen::Arr<uint8_t>& bytes);

/* the measurement (hw.txt "statemeasure <every> [loadEvery]"): every <every>th first-time simulated tick of a lobby match, TF.State's
 * GetState + serialize and deserialize timed; every <loadEvery>th of those also LoadState and the round trip (GetState again, the
 * same bytes?). Lines "[state] ..." (hwlog). */
void Configure(int every, int loadEvery);
void InstallThrowLog();   /* every C++ (managed) throw logged first-chance, bounded (tfexgen_state.cpp) */
bool On();
void BeforeMatch();                        /* in the map screen (no rollback window): TF.State's classes made */
void MeasureTick(void* level, int frame);  /* after a tick's simulation (not a resimulated one), before Draw */
void LogSummary(const char* why);
void ConfigureLayout(int every);   /* hw.txt "statelayout <every>": slot maps only (phase 1 of the plan) */
void ConfigureDump(int every);      /* hw.txt "statedump <every>": step E, serialized states to the log for statediff.py */
void DumpSaved(int frame, const gen::Arr<uint8_t>& bytes);   /* the rollback's saved state of a frame, when statedump asks for it (a re-save after a rollback logs again: the last is what GGRS kept) */
void ConfigureRaw(int n);          /* hw.txt "staterawdump <n>": step B, raw object memory to sd:/xplay/raw (with statelayout) */
void ConfigureSpawn(int tick);     /* hw.txt "statespawn <tick>": the spawn scene, TEST ONLY (glue/StateSpawn.cs) */
}
