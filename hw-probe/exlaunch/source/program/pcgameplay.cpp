/* The PC gameplay TF.EX plays, only while the game is in online mode: on when the player enters the online section (NETPLAY, TF.EX's
 * VersusModeExtensions.ApplyNetplayMode) or a test match starts natively, off (the Switch's original instructions and levels back) when
 * the player goes back to a non-online mode (ClearNetplayMode). Offline versus and the other modes keep the Switch's 1.4.1.6 behaviour.
 *
 *  - Version gaps (found by tools/re/parity.py): instruction patches, restored word for word;
 *  - hot coals: hotcoals.cpp's three HotCoalsBounce call sites (Install / Uninstall);
 *  - levels: the PC's versus levels sit in the romfs at Content/Levels/PCVersus/<tower>/<n>.oel (tools/pc-levels.sh; same file names
 *    as the Switch's, checked for all 16 towers), and Calc.LoadXML (+0x90c570) loads those instead of Content/Levels/Versus/... ;
 *    VersusTowerData.GetLevels (+0xbc2170) builds the list the way the PC does in netplay, where MatchSettings.Mode is FortRise's
 *    custom mode 11: the default branch, `PlayerSpawns >= TFGame.PlayerAmount`, with the PC files' spawn counts (a VersusLevelData
 *    (+0xbae420) made from the path while the redirect is on reads the PC file). The tower data the game built at boot stays as is,
 *    but for its level lists: online they hold the PC's NN.oel only (PcTowerLevels: the megamod's tb.oel out, back when offline).
 *  - wide (TF.EX with WiderSet's wide set, docs/ws-wide.md): SetWideMatch sets the lobby match's MatchSettings.SixPlayer (+0x20,
 *    read by Session.StartGame +0xb4451c: WrapMath width 420, screen 420x240) and remembers the value it replaced, given back when
 *    online mode is left; GetLevels then lists WiderSet's files for that tower (widelevels.hpp, from WiderSet's folder sd:/xplay/mods/WiderSet,
 *    tools/pc-levels.sh) in FortRise's order, with the same PlayerSpawns filter. The Switch's own 6P-Versus files stay for local play. */
#include "lib.hpp"
#include "tfex/runtime/tfexgen_scenario_rt.hpp"
#include "patch/code_patcher.hpp"
#include <lib/util/sys/modules.hpp>
#include <common.hpp>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include "hwlog.hpp"
#include "widelevels.hpp"
#include "tfex/AiSlotButton.hpp"

namespace hotcoals { void Install(); void Uninstall(); }
#include "megamod/megamod.hpp"
#include "modcatalog.hpp"
namespace pclevels { void* Doc(const char* rel); }   /* pclevels.cpp: a PC level from the Switch's own and its delta */
namespace tfex::gen::GlueFrMods { void ReskinOnline(bool online); }
namespace tfex::Tower { void ClearNoRandom(); }

namespace pcgameplay {
namespace {
uintptr_t Base() { return exl::util::GetMainModuleInfo().m_Total.m_Start; }

struct Gap { uint32_t off, sw, pc; const char* what; };
const Gap kGaps[] = {
    /* OrbLogic.DoSlowMoKill: Engine.TimeRate = 0.25f on the Switch, 0.5f on the PC (`mov w8, #0x3e800000` -> `#0x3f000000`) */
    { 0xadb45c, 0x52a7d008u, 0x52a7e008u, "final-kill slow motion 0.25 -> 0.5" },
    /* Player.Jump out of mud: the Switch's -2.2f arm (`movk w8, #0xc00c, lsl 16`) -> the -3.2f arm's (`#0xc04c`) */
    { 0xaf11ec, 0x72b80188u, 0x72b80988u, "jump out of mud -2.2 -> -3.2" },
    /* Player.Update: the Switch sets a player on hot coals on fire (Fire.Start); the PC only bounces (hotcoals.cpp): the call -> nop */
    { 0xaef8b0, 0x97fd4e10u, 0xd503201fu, "no Fire.Start on hot coals" },
    /* HotCoals.ctor: Hitbox(width, 6, 0, -1) on the Switch, Hitbox(width, 5, 0, 0) on the PC */
    { 0xa57a18, 0x1e231001u, 0x1e229001u, "hot coals hitbox height 6 -> 5" },
    { 0xa57a20, 0x1e3e1003u, 0x1e2703e3u, "hot coals hitbox y -1 -> 0" },
    /* RoundEndCounter: ghostWaitCounter 150 on the Switch, 90 on the PC (both resets: the packed {90f, 150f} -> {90f, 90f}; a version
     * difference: the mode's own tests, overtime and the TeamReviver, follow MatchSettings.Mode 11 now, docs/TEAM-MODES-RE.md section 7) */
    { 0xb37978, 0xf2e862c8u, 0xf2e85688u, "round end: ghost wait 150 -> 90 (constructor)" },
    { 0xb379e4, 0xf2e862c8u, 0xf2e85688u, "round end: ghost wait 150 -> 90 (reset)" },
    /* Spikeball (Dark World, TF.State's checksum: statediff on seed 1704231922, Darkfang): the ctor's Depth is -170 on the Switch, -160 on
     * the PC (`mov w1, #-170` -> `#-160`). Update's 1f / 180f stays the Switch's 0x3bb60b60: FortRise (every TF.EX player's game)
     * rewrites the PC's 0x3bb60b61 to 0.005555555f = 0x3bb60b60 (Patches/Spikeball.cs, "the same for v1.3.3.1 and v1.3.3.3"). Making it
     * the vanilla PC's (2026-10-24) desynced spike ball towers against the page (smoke lobby 220639, seed 5966; 2026-10-28). */
    { 0xb6132c, 0x12801521u, 0x128013e1u, "Spikeball.ctor: Depth -170 -> -160" },
    /* TeamReviver.ctor (TEAM REVIVE's reviver, TF.State's checksum: smoke team 172826, frame 1980): the Switch sets Depth = -99 after the
     * base ctor (`mov w1, #-99; bl Entity.set_Depth`), the PC never sets it (0): the call -> nop */
    { 0xb692dc, 0x97f711a5u, 0xd503201fu, "TeamReviver.ctor: no Depth -99 (the PC's 0)" },
    /* The same misrounding everywhere (tools/re/round7.py, against the PC game as TF.EX players run it: patched by FortRise,
     * tools/re/pc-il.sh: BRUTE wrote float literals with too few digits; the angles are one ULP low): rodata words the Switch shares
     * between every use of the literal, each word the PC's value, which no PC code has in the Switch's form, so every reader gets the
     * PC's (where FortRise's game does use the Switch's form, that reader is patched on its own: Arrow.get_SeekMaxAngle). Left out: Lava.DrawLight's 0.20000003 (not a
     * rounded literal; drawing only) and Mole.DrillUpdate's 0.6 degrees (34 readers for 1 PC use: shared with something else; quest) */
    { 0x38e29d8, 0x3f32b8c2u, 0x3f32b8c3u, "40 degrees (CrumbleBlock.Destroy ... 18 readers): the PC's rounding" },
    /* ... but Arrow.get_SeekMaxAngle keeps the Switch's 0x3f32b8c2: FortRise rewrites the PC's 0.69813174f to 0.6981317f (the same
     * v1.3.3.3 parity tweak as Spikeball's). The getter (+0x984da0: adrp, ldr of the shared word above, ret, padding) returns the
     * constant itself: mov / movk w8, fmov s0, ret */
    { 0x984da0, 0xd0017ae8u, 0x52971848u, "Arrow.get_SeekMaxAngle: mov w8, #0xb8c2 (FortRise's 0.6981317f)" },
    { 0x984da4, 0xbd49d900u, 0x72a7e648u, "Arrow.get_SeekMaxAngle: movk w8, #0x3f32, lsl #16" },
    { 0x984da8, 0xd65f03c0u, 0x1e270100u, "Arrow.get_SeekMaxAngle: fmov s0, w8" },
    { 0x984dac, 0x00000000u, 0xd65f03c0u, "Arrow.get_SeekMaxAngle: ret (was the padding after it)" },
    { 0x38e27c4, 0xbeb2b8c2u, 0xbeb2b8c3u, "-20 degrees (CrumbleBlock.Destroy ...): the PC's rounding" },
    { 0x38e2a68, 0x3eb2b8c2u, 0x3eb2b8c3u, "20 degrees (VariantButton, Saver ...): the PC's rounding" },
    { 0x38e25d8, 0xbdb2b8c2u, 0xbdb2b8c3u, "-5 degrees (FloatyPhysicsObject, Lantern ...): the PC's rounding" },
    { 0x38e2950, 0x3db2b8c2u, 0x3db2b8c3u, "5 degrees (FloatyPhysicsObject, Lantern ...): the PC's rounding" },
    { 0x38e268c, 0x3e32b8c2u, 0x3e32b8c3u, "10 degrees (PrismVanish, Worm ...): the PC's rounding" },
    { 0x38e28b4, 0xbe32b8c2u, 0xbe32b8c3u, "-10 degrees (EvilCrystal, KingReaper): the PC's rounding" },
    { 0x38e23c8, 0xbc64c388u, 0xbc64c389u, "-0.8 degrees (CataclysmMissile): the PC's rounding" },
    { 0x38e2a20, 0x3c64c388u, 0x3c64c389u, "0.8 degrees (CataclysmMissile): the PC's rounding" },
    { 0x38e26fc, 0x3ff5be0bu, 0x3ff5be0cu, "110 degrees (Skeleton.Update): the PC's rounding" },
    { 0x38e2454, 0x3ae4c388u, 0x3ae4c389u, "0.1 degrees (AmaranthBoss): the PC's rounding" },
    { 0x38e2a00, 0x3fc02037u, 0x3fc02038u, "86 degrees (AmaranthBoss): the PC's rounding" },
    /* Wide (docs/ws-wide.md section 3.2): the Switch reads the WrapMath width static [0x4792130] (420 when MatchSettings.SixPlayer)
     * where the PC keeps 320 with WiderSet on: methods WiderSet doesn't patch, TF.State's netplay replacements (fixed 160/320), and the
     * second test of MoveTowardsWrap (WiderSet patches only the first 320). The load becomes the constant 320, so these are no-ops at
     * width 320 (every standard online match) and the PC's numbers in a wide one. Each site checked: the scratch w8 is dead after it. */
    { 0xaef0ac, 0xb9400108u, 0x52802808u, "Player.SceneBegin: facing by X > width/2 (PC 160)" },   /* ldr w8, [x8] -> mov w8, #320 */
    { 0xaf6d8c, 0xb9400108u, 0x52802808u, "Player.SideBouncePlayer: |dx| >= width - 40, -= width (PC 280, 320)" },   /* ldr w8, [x8] -> mov w8, #320 */
    { 0x9d95ec, 0xbd400100u, 0x52802808u, "CrackedWall.ctor: width (PC 320)" },   /* ldr s0, [x8] -> mov w8, #320 */
    { 0x9d95f4, 0x5e21d809u, 0x1e220109u, "CrackedWall.ctor: width (PC 320)" },   /* scvtf s9, s0 -> scvtf s9, w8 */
    { 0x9daa70, 0xbd400100u, 0x52802808u, "CrumbleBlock.ctor: width (PC 320)" },   /* ldr s0, [x8] -> mov w8, #320 */
    { 0x9daa78, 0x5e21d809u, 0x1e220109u, "CrumbleBlock.ctor: width (PC 320)" },   /* scvtf s9, s0 -> scvtf s9, w8 */
    { 0x9dcaf0, 0xbd400100u, 0x52802808u, "CrumbleWall.ctor: width (PC 320)" },   /* ldr s0, [x8] -> mov w8, #320 */
    { 0x9dcaf8, 0x5e21d809u, 0x1e220109u, "CrumbleWall.ctor: width (PC 320)" },   /* scvtf s9, s0 -> scvtf s9, w8 */
    { 0xa411b4, 0xbd400101u, 0x52802808u, "FakeWall.SceneBegin: width (PC 320)" },   /* ldr s1, [x8] -> mov w8, #320 */
    { 0xa411b8, 0x5e21d821u, 0x1e220101u, "FakeWall.SceneBegin: width (PC 320)" },   /* scvtf s1, s1 -> scvtf s1, w8 */
    { 0xa41248, 0xbd400101u, 0x52802808u, "FakeWall.SceneBegin: width (PC 320)" },   /* ldr s1, [x8] -> mov w8, #320 */
    { 0xa4124c, 0x5e21d821u, 0x1e220101u, "FakeWall.SceneBegin: width (PC 320)" },   /* scvtf s1, s1 -> scvtf s1, w8 */
    { 0xa41338, 0xb9400319u, 0x52802819u, "FakeWall.SceneBegin: width (PC 320)" },   /* ldr w25, [x24] -> mov w25, #320 */
    { 0xa4136c, 0xb9400319u, 0x52802819u, "FakeWall.SceneBegin: width (PC 320)" },   /* ldr w25, [x24] -> mov w25, #320 */
    { 0xa57bd4, 0xbd400100u, 0x52802808u, "HotCoals.ctor: width (PC 320)" },   /* ldr s0, [x8] -> mov w8, #320 */
    { 0xa57bdc, 0x5e21d809u, 0x1e220109u, "HotCoals.ctor: width (PC 320)" },   /* scvtf s9, s0 -> scvtf s9, w8 */
    { 0xa5a3f8, 0xbd400100u, 0x52802808u, "Ice.BuildTiles: width (PC 320)" },   /* ldr s0, [x8] -> mov w8, #320 */
    { 0xa5a400, 0x5e21d809u, 0x1e220109u, "Ice.BuildTiles: width (PC 320)" },   /* scvtf s9, s0 -> scvtf s9, w8 */
    { 0xb65f1c, 0xbd400100u, 0x52802808u, "SwitchBlock.ctor: width (PC 320)" },   /* ldr s0, [x8] -> mov w8, #320 */
    { 0xb65f24, 0x5e21d809u, 0x1e220109u, "SwitchBlock.ctor: width (PC 320)" },   /* scvtf s9, s0 -> scvtf s9, w8 */
    { 0xb38ed4, 0xb9400108u, 0x52802808u, "RoundLogic.SpawnPlayersFFA: list[0].X == width/2 (TF.State 160)" },   /* ldr w8, [x8] -> mov w8, #320 */
    { 0xb39198, 0xb9400308u, 0x52802808u, "RoundLogic.SpawnPlayersTeams: TeamSpawn split at width/2 (TF.State 160)" },   /* ldr w8, [x24] -> mov w8, #320 */
    { 0xadb8e0, 0xb9400308u, 0x52802808u, "OrbLogic.DoOffsetWorldVariant: width/2 (TF.State 160)" },   /* ldr w8, [x24] -> mov w8, #320 */
    { 0xadb908, 0xb9400308u, 0x52802808u, "OrbLogic.DoOffsetWorldVariant: width/2 (TF.State 160)" },   /* ldr w8, [x24] -> mov w8, #320 */
    { 0xadb92c, 0xb9400308u, 0x52802808u, "OrbLogic.DoOffsetWorldVariant: width/2 (TF.State 160)" },   /* ldr w8, [x24] -> mov w8, #320 */
    { 0xadb958, 0xb9400308u, 0x52802808u, "OrbLogic.DoOffsetWorldVariant: width/2 (TF.State 160)" },   /* ldr w8, [x24] -> mov w8, #320 */
    { 0x95c850, 0xbd4002a0u, 0x52802808u, "Actor.MoveTowardsWrap: the second test (WiderSet remaps only the first 320)" },   /* ldr s0, [x21] -> mov w8, #320 */
    { 0x95c854, 0x5e21d80bu, 0x1e22010bu, "Actor.MoveTowardsWrap: the second test (WiderSet remaps only the first 320)" },   /* scvtf s11, s0 -> scvtf s11, w8 */
    /* LevelRandomTreasure.AddRandomTreasure (Cataclysm): the centre big-chest check `solids[15, i] || solids[16, i]` (and Collide) is
     * `half - 1`, `half` on the Switch; WiderSet remaps the PC's int 16s (-> 21) but not the 15s (its 15 rule matches floats only) */
    { 0xa96588, 0x51000517u, 0x528001f7u, "AddRandomTreasure: centre check column 15 (WiderSet leaves the int 15)" },   /* sub w23, w8, #1 -> mov w23, #15 */
    /* RoundLogic.ctor: miasmaCounter = Counter(MatchSettings.SixPlayer ? 1200 : 1500); the PC's is always 1500 (Switch-only code keyed
     * on SixPlayer, not on the width; docs/ws-wide.md 3.2) */
    { 0xb37c84, 0x52809608u, 0x5280bb88u, "RoundLogic.ctor: miasma after 1500 ticks in wide too (Switch 6P: 1200)" },   /* mov w8, #1200 -> #1500 */
    /* Session.GetSpawnArrows, not team mode: the Switch's `if (PlayerAmount >= 5)` (its six-player rule: 2 arrows, or 3 only with
     * NoAutobalance and a crown) where the PC (WiderSet doesn't touch it) always starts with 3 then autobalances (wide5: frame 60,
     * ArrowsInventory 2 vs 3). Never taken with 4 players or fewer, so a no-op in every standard match */
    { 0xb45dd0, 0x5400104au, 0xd503201fu, "GetSpawnArrows: no 5+ players rule (the PC's 3 starting arrows)" },   /* b.ge -> nop */
    /* RoundLogic.FinalKillTeams (the match's last kill in a team match: GotWin and a spotlight for each player of the winning team): the
     * PC loops over seats 0-3, the Switch over 0-5; WiderSet doesn't patch it, so on the PC seats 4-7 of the winning team get neither
     * (wide8end 134948: MatchStats[4..5].GotWin true on the Switch only). A no-op with 4 players or fewer */
    { 0xb3ab08, 0xf1001affu, 0xf10012ffu, "FinalKillTeams: seats 0-3 as the PC's (cmp x23, #6 -> #4)" },
    /* Session.EndRound's per-seat stats loop (MatchStats: rounds survived, SurvivedWithNoKills, ...; TF.State's saved state): the PC's
     * loops `i < 4`, unpatched by WiderSet, the Switch's 6. A no-op with 4 players or fewer */
    { 0xb44e60, 0xf1001a9fu, 0xf100129fu, "Session.EndRound stats: seats 0-3 as the PC's (cmp x20, #6 -> #4)" },
};
/* Only while a wide lobby match runs (SetWideMatch): constants WiderSet changes that the Switch doesn't scale. */
const Gap kWideGaps[] = {
    /* BigTreasureSpotValue's `y <= 110` (WiderSet: the first 110 -> 160), inlined into CompareBigTreasureSpots too: the load of rodata
     * 110.0f (+0x38e292c) -> 160.0f in the same page (+0x38e23b0) */
    { 0xa97178, 0xbd492d60u, 0xbd43b160u, "CompareBigTreasureSpots: 110 -> 160" },
    { 0xa971b8, 0xbd492d41u, 0xbd43b141u, "BigTreasureSpotValue: 110 -> 160" },
    /* LevelTiles.ctor: overwriteData = Calc.ReadCSVIntGrid(xml["SolidTiles"], 32, 24) (+0x90ad10); WiderSet's LevelTiles ctor hook makes
     * it 42. With 32 a wide level's tile overrides right of column 31 were dropped: on Dreadwood the right wall's lit tiles (44..47),
     * whose AddTileIDLights light sources are entities at depth 0, so Layer.ActualDepthLookup and every checksum differed */
    { 0xa982f8, 0x321b03e1u, 0x52800541u, "LevelTiles.ctor: SolidTiles read 32 -> 42 columns" },   /* mov w1, #32 -> #42 */
};
bool g_wideGapsOn = false;
void ApplyWideGaps(bool on) {
    if (on == g_wideGapsOn) return;
    uintptr_t b = Base(); int changed = 0;
    for (const Gap& g : kWideGaps) {
        auto* w = reinterpret_cast<uint32_t*>(b + g.off);
        uint32_t want = on ? g.pc : g.sw, have = on ? g.sw : g.pc;
        if (*w == want) continue;
        if (*w != have) { hw::Log("[pc] %s: insn is %08x, left alone", g.what, *w); continue; }
        exl::patch::CodePatcher p(g.off); p.Write<uint32_t>(want); changed++;
    }
    g_wideGapsOn = on;
    hw::Log("[pc] wide-only patches %s (%d)", on ? "applied" : "restored", changed);
}
bool g_on = false;
bool g_wide = false;                 /* this lobby match plays the wide set */
uint8_t* g_wideField = nullptr;      /* the MatchSettings.SixPlayer byte SetWideMatch wrote, and the value it had before */
uint8_t g_wideOrig = 0;
uint8_t* g_fixedFirst = nullptr;     /* the wide tower's VersusTowerData.FixedFirst byte SetWideMatch cleared, and its value */
uint8_t g_fixedFirstOrig = 0;
/* WiderSet's gameplay tables (its module ctor, on every PC that loads it), played in a wide lobby match. Each is a static array the
 * Switch's game keeps shorter; while the match is set up the static points at an array object of ours (WideArr: a live array's header
 * copied, its data and length repointed; a BRUTE array keeps both behind pointers, +0x10 / +0x20, megamod ArrayData), outside the GC
 * heap like LongStr, holding only our own memory. The game's array is kept in RuntimeAlloc memory (scanned, never freed: the module's
 * .bss is no GC root) until SetWideMatch puts it back.
 *  - Session.TeamStartArrows (int[], storage behind +0x4796168): Switch {3, 3, 2, 2, 1}, WiderSet {3, 3, 3, 3, 2, 2, 2}: a team of 3-4
 *    starts with 3 arrows, of 5-7 with 2;
 *  - TreasureSpawner.ChestChances (float[][], storage behind +0x4796ae0; a row per player count from 2): rows 0-2 (2-4 players) equal
 *    on both (word for word), the Switch's rows 3-4 its own ({.9,.9,.6,.8,.4,.2,.1}, {.9,.9,.6,.8,.6,.4,.2,.1}), WiderSet's 3-6 below. */
struct alignas(16) WideArr { uint8_t header[0x40]; };
struct Kept { void* teamArrows; void* chestChances; };   /* the game's arrays while ours stand in (RuntimeAlloc: scanned) */
Kept* g_kept = nullptr;
void** g_teamArrowsStorage = nullptr;
void** g_chestStorage = nullptr;
/* `obj` as an array object of `like`'s type over data / len; false (logged) if `like`'s header isn't as expected */
bool MakeWideArr(WideArr& obj, const uint8_t* like, void* data, int32_t* len, const char* what) {
    uintptr_t hdr = like ? *reinterpret_cast<const uintptr_t*>(like + 0x10) - reinterpret_cast<uintptr_t>(like) : 0;
    if (!like || hdr < 0x28 || hdr > sizeof(WideArr)) {
        hw::Log("[pc] ERROR wide: %s isn't an array as expected (header %#lx); the Switch's kept", what, (unsigned long)hdr);
        return false;
    }
    std::memcpy(obj.header, like, hdr);
    *reinterpret_cast<void**>(obj.header + 0x10) = data;
    *reinterpret_cast<int32_t**>(obj.header + 0x20) = len;
    return true;
}
int ArrLen(const uint8_t* arr) { const int32_t* l = arr ? *reinterpret_cast<int32_t* const*>(arr + 0x20) : nullptr; return l ? *l : 0; }
template <class T> T* ArrData(const uint8_t* arr) { return arr ? *reinterpret_cast<T* const*>(arr + 0x10) : nullptr; }

WideArr g_teamArrowsObj;
int32_t g_teamArrowsData[7] = { 3, 3, 3, 3, 2, 2, 2 }, g_teamArrowsLen = 7;

constexpr int kChestRows = 7;
struct ChestRow { int32_t len; float v[7]; };
const ChestRow kWideChest[4] = {   /* rows 3-6: 5-8 players */
    { 6, { .9f, .9f, .8f, .8f, .2f, .1f } },
    { 6, { .9f, .9f, .9f, .8f, .4f, .1f } },
    { 6, { .9f, .9f, .9f, .8f, .4f, .2f } },
    { 7, { .9f, .9f, .9f, .9f, .4f, .2f, .1f } },
};
WideArr g_chestObj, g_chestRowObj[kChestRows];
void* g_chestRowPtr[kChestRows];
float g_chestRowData[kChestRows][7];
int32_t g_chestRowLen[kChestRows], g_chestLen = kChestRows;

void WideTablesRestore() {
    if (g_teamArrowsStorage) { *g_teamArrowsStorage = g_kept->teamArrows; g_kept->teamArrows = nullptr; g_teamArrowsStorage = nullptr; }
    if (g_chestStorage) { *g_chestStorage = g_kept->chestChances; g_kept->chestChances = nullptr; g_chestStorage = nullptr; }
}
void WideTables() {
    if (!g_kept) g_kept = static_cast<Kept*>(tfex::gen::RuntimeAlloc(sizeof(Kept)));
    if (!g_kept) { hw::Log("[pc] ERROR wide: no runtime memory for the kept tables; the Switch's TeamStartArrows / ChestChances kept"); return; }
    /* Session's statics exist only once its class constructor ran (the first Session is made after this, at the match's start): the
     * game's own guard, as Session..ctor +0xb43b90 does it (the flag behind +0x4796158; the init helper +0x720 with it and +0x4796160).
     * Without it TeamStartArrows was still null here in every lobby match (wideteam5 111508) */
    uint8_t* sessionInit = *reinterpret_cast<uint8_t**>(Base() + 0x4796158);
    if (sessionInit && !*sessionInit)
        reinterpret_cast<void (*)(void*, void*)>(Base() + 0x720)(sessionInit, *reinterpret_cast<void**>(Base() + 0x4796160));
    void** ts = *reinterpret_cast<void***>(Base() + 0x4796168);
    const uint8_t* ta = ts ? static_cast<const uint8_t*>(*ts) : nullptr;
    if (!ta) hw::Log("[pc] ERROR wide: Session.TeamStartArrows is null (Session's statics not set up); the Switch's values kept");
    else if (MakeWideArr(g_teamArrowsObj, ta, g_teamArrowsData, &g_teamArrowsLen, "Session.TeamStartArrows")) {
        g_kept->teamArrows = *ts; g_teamArrowsStorage = ts; *ts = &g_teamArrowsObj;
        hw::Log("[pc] wide: TeamStartArrows 3 3 3 3 2 2 2 (WiderSet's; the Switch's had %d)", ArrLen(ta));
    }
    void** cs = *reinterpret_cast<void***>(Base() + 0x4796ae0);
    const uint8_t* outer = cs ? static_cast<const uint8_t*>(*cs) : nullptr;
    int n = ArrLen(outer);
    void* const* rows = ArrData<void*>(outer);
    if (!rows || n < 3) { hw::Log("[pc] ERROR wide: TreasureSpawner.ChestChances not as expected (%d rows); the Switch's kept", n); return; }
    const uint8_t* row0 = static_cast<const uint8_t*>(rows[0]);
    for (int r = 0; r < kChestRows; r++) {
        if (r < 3) {   /* the game's rows for 2-4 players, copied */
            const uint8_t* src = static_cast<const uint8_t*>(rows[r]);
            int len = ArrLen(src); if (len > 7) len = 7;
            g_chestRowLen[r] = len;
            const float* v = ArrData<float>(src);
            for (int k = 0; k < len; k++) g_chestRowData[r][k] = v ? v[k] : 0.f;
        } else {
            g_chestRowLen[r] = kWideChest[r - 3].len;
            std::memcpy(g_chestRowData[r], kWideChest[r - 3].v, sizeof(g_chestRowData[r]));
        }
        if (!MakeWideArr(g_chestRowObj[r], row0, g_chestRowData[r], &g_chestRowLen[r], "a ChestChances row")) return;
        g_chestRowPtr[r] = &g_chestRowObj[r];
    }
    if (!MakeWideArr(g_chestObj, outer, g_chestRowPtr, &g_chestLen, "TreasureSpawner.ChestChances")) return;
    g_kept->chestChances = *cs; g_chestStorage = cs; *cs = &g_chestObj;
    hw::Log("[pc] wide: ChestChances rows for 5-8 players are WiderSet's (%d rows; the Switch's had %d)", kChestRows, n);
}

/* A managed string outside the GC heap, long enough for level paths (tfex::game::MString holds 44 characters). Boehm ignores pointers
 * it doesn't own; strings are immutable. */
struct alignas(16) LongStr { uintptr_t klass; uint64_t zero; int32_t len; char16_t ch[160]; };
LongStr g_path;

/* ---- Level paths: ONE rule (2026-10-24). A level path in the game's data (GetLevels' list, so TF.State's RoundLevels, GGRS's checksum,
 * WiderSet's load seed) is always the PC's exact text, in the form TF.State keeps ('\\', OwnGenLevel's Replace); it becomes a Switch file
 * only where a file is opened (SwitchFilePath, LoadXmlHook). Three bugs came from mixing the two forms: '/' against '\\' (nn::fs wants '/'),
 * the Dark World towers (the PC's DarkWorldContent, 2026-10-24: every Dark World tower's checksum), WiderSet's mod root. The PC's texts:
 *   Content\Levels\Versus\<tower>\<n>.oel            towers 00..11
 *   DarkWorldContent\Levels\Versus\<tower>\<n>.oel   towers 12..15 (GameData.GetDarkWorldDirectory)
 *   mod:Teuria.WiderSet\Content\WideLevels\Versus\<tower>\<n>.oel   WiderSet's (a FortRise mod's RootPath; docs/ws-wide.md)
 * The Switch's files: Content/Levels/PCVersus/<tower>/<n>.oel (all 16 towers; tools/pc-levels.sh); WiderSet's stay in FortRise's form, a
 * mod path, read from its folder sd:/xplay/mods/<its folder> (tfex/FrModsBoot.cpp's overlay: any loaded mod's mod:<name> path).
 * Checked at install (PathsSelfTest: every tower, both directions). */
namespace paths {
const char kVersus[] = "Content\\Levels\\Versus\\", kDarkWorld[] = "DarkWorldContent\\Levels\\Versus\\", kWide[] = "mod:Teuria.WiderSet\\Content\\WideLevels\\Versus\\";
const char kSwitchVersus[] = "Content/Levels/PCVersus/";
bool DarkWorldTower(const char16_t* folder, int n) { return n >= 3 && folder[0] == u'1' && folder[1] >= u'2' && folder[1] <= u'5' && folder[2] == u' '; }
/* the PC's text for <tower>/<file> (wide: WiderSet's set) into out (chars, no terminator); its length, -1 if it doesn't fit */
int PcText(const char16_t* folder, int nf, const char16_t* file, int nl, bool wide, char16_t* out, int cap) {
    const char* root = wide ? kWide : DarkWorldTower(folder, nf) ? kDarkWorld : kVersus;
    int n = 0;
    for (const char* r = root; *r; r++) { if (n >= cap) return -1; out[n++] = (char16_t)*r; }
    if (n + nf + 1 + nl > cap) return -1;
    std::memcpy(out + n, folder, nf * sizeof(char16_t)); n += nf;
    out[n++] = u'\\';
    std::memcpy(out + n, file, nl * sizeof(char16_t)); n += nl;
    return n;
}
/* does c[0..n) start with the ASCII prefix p, either separator matching either? its length if so, else 0 */
int Prefix(const char16_t* c, int n, const char* p) {
    int k = 0;
    for (; p[k]; k++) {
        if (k >= n) return 0;
        bool sep = p[k] == '\\' || p[k] == '/';
        if (sep ? (c[k] != u'\\' && c[k] != u'/') : c[k] != (char16_t)p[k]) return 0;
    }
    return k;
}
/* a level path in either form -> its Switch file into out ('/' throughout); its length, -1 if it isn't a versus / wide level path */
int SwitchFile(const char16_t* c, int n, char16_t* out, int cap) {
    int k; const char* to;
    if ((k = Prefix(c, n, kDarkWorld)) || (k = Prefix(c, n, kVersus))) to = kSwitchVersus;
    else return -1;   /* (WiderSet's mod path too: its folder's file, FrModsBoot's overlay) */
    int m = 0;
    for (const char* r = to; *r; r++) { if (m >= cap) return -1; out[m++] = (char16_t)*r; }
    for (int i = k; i < n; i++) { if (m >= cap) return -1; out[m++] = c[i] == u'\\' ? u'/' : c[i]; }
    return m;
}
}   // namespace paths

/* where the game opens a file: a level path (either form) -> its Switch file; any other path in Windows form -> the same with '/'
 * (nn::fs takes '/'). Into g_path; false when the path is fine as it is. */
bool SwitchFilePath(void* str) {
    if (!str) return false;
    uintptr_t s = reinterpret_cast<uintptr_t>(str);
    int n = *reinterpret_cast<int32_t*>(s + 0x10);
    const char16_t* raw = reinterpret_cast<const char16_t*>(s + 0x14);
    if (n <= 0 || n >= 160) return false;
    int m = paths::SwitchFile(raw, n, g_path.ch, 159);
    if (m < 0) {
        bool back = false;
        for (int i = 0; i < n; i++) back |= raw[i] == u'\\';
        if (!back) return false;
        for (int i = 0; i < n; i++) g_path.ch[i] = raw[i] == u'\\' ? u'/' : raw[i];
        m = n;
    }
    g_path.klass = *reinterpret_cast<uintptr_t*>(s); g_path.zero = 0;
    g_path.len = m; g_path.ch[m] = 0;
    return true;
}
}

namespace {
/* WiderSet's level paths as managed strings outside the GC heap, one slot per (tower, level): the same text every time, so a list made
 * for an earlier round can keep pointing at them. */
LongStr g_wideStr[widelevels::kTowers][widelevels::kMaxLevels];
/* The tower folder in a level path ("<...>Levels/6P-Versus/<folder>/<n>.oel" or ".../Versus/<folder>/..."), as an index into
 * widelevels::kTable, and the length of the path up to and including "Levels/"; -1 if there is none. */
int WideTower(void* str, int* prefixLen) {
    uintptr_t s = reinterpret_cast<uintptr_t>(str);
    int n = *reinterpret_cast<int32_t*>(s + 0x10);
    const char16_t* c = reinterpret_cast<const char16_t*>(s + 0x14);
    const char16_t kLevels[] = u"Levels/";
    for (int i = 0; i + 7 <= n; i++) {
        if (std::memcmp(c + i, kLevels, 7 * sizeof(char16_t))) continue;
        int a = i + 7;
        while (a < n && c[a] != u'/') a++;                    /* skip 6P-Versus / Versus */
        int b = ++a;
        while (b < n && c[b] != u'/') b++;
        for (int t = 0; t < widelevels::kTowers; t++) {
            const char* f = widelevels::kTable[t].folder;
            int k = 0;
            while (f[k] && a + k < b && c[a + k] == (char16_t)f[k]) k++;
            if (!f[k] && a + k == b) { *prefixLen = i + 7; return t; }
        }
        return -1;
    }
    return -1;
}
/* WiderSet's level t/l: the PC's text (paths::PcText, wide) into g_wideStr[t][l], the class word taken from a live path. */
void* WidePath(void* like, int t, int l) {
    LongStr& o = g_wideStr[t][l];
    o.klass = *reinterpret_cast<uintptr_t*>(reinterpret_cast<uintptr_t>(like)); o.zero = 0;
    char16_t folder[48], file[16]; int nf = 0, nl = 0;
    for (const char* f = widelevels::kTable[t].folder; *f && nf < 47; f++) folder[nf++] = (char16_t)*f;
    for (const char* f = widelevels::kTable[t].levels[l]; *f && nl < 11; f++) file[nl++] = (char16_t)*f;
    for (const char* f = ".oel"; *f; f++) file[nl++] = (char16_t)*f;
    o.len = paths::PcText(folder, nf, file, nl, true, o.ch, 159);
    if (o.len < 0) o.len = 0;
    o.ch[o.len] = 0;
    return &o;
}
}

/* TF.EX v0.21.0 VersusLevelSystemExtensions.SortByFolderAndName (TF.State.Domain/Extensions/VersusLevelSystemExtensions.cs), applied by
 * OwnGenLevel to GetLevels' list before the seeded shuffle: by folder (in the order folders first appear), then by file name,
 * StringComparer.OrdinalIgnoreCase (upper-cased UTF-16 code units, a prefix first). Done here on the list GetLevels returns: on the
 * Switch GenLevels (+0xbae7a0) is the only caller that keeps the order. Every official and WiderSet tower's levels sit in one folder
 * named NN.oel (WiderSet: also a00), so the order only changes if the romfs lists them out of order. OwnGenLevel then makes every
 * '/' a '\\'; TFEX-DIFF: S-fspath (nn::fs paths take '/'): not done. The strings only meet `levels[0] == lastLevel`, and lastLevel
 * comes from this same list, so the comparison gives the same answer either way. */
namespace {
int Upper(char16_t c) { return c >= u'a' && c <= u'z' ? c - 32 : c; }
/* The folder (up to the last '/') and file name of a managed path string. */
void SplitPath(void* str, const char16_t** ch, int* folderLen, int* len) {
    uintptr_t s = reinterpret_cast<uintptr_t>(str);
    *len = *reinterpret_cast<int32_t*>(s + 0x10);
    *ch = reinterpret_cast<const char16_t*>(s + 0x14);
    int f = *len;
    while (f > 0 && (*ch)[f - 1] != u'/' && (*ch)[f - 1] != u'\\') f--;
    *folderLen = f;
}
bool SameFolder(void* a, void* b) {
    const char16_t *ca, *cb; int fa, fb, la, lb;
    SplitPath(a, &ca, &fa, &la); SplitPath(b, &cb, &fb, &lb);
    return fa == fb && !std::memcmp(ca, cb, (size_t)fa * sizeof(char16_t));
}
int CompareFileNames(void* a, void* b) {
    const char16_t *ca, *cb; int fa, fb, la, lb;
    SplitPath(a, &ca, &fa, &la); SplitPath(b, &cb, &fb, &lb);
    for (int i = fa, j = fb; i < la || j < lb; i++, j++) {
        if (i >= la) return -1;
        if (j >= lb) return 1;
        int d = Upper(ca[i]) - Upper(cb[j]);
        if (d) return d;
    }
    return 0;
}
/* Sorts List<string> `list` (items +0x10, count +0x18; array elements from [+0x10]) in place; returns how many moved. */
int SortByFolderAndName(void* list) {
    uintptr_t l = reinterpret_cast<uintptr_t>(list);
    int count = *reinterpret_cast<int32_t*>(l + 0x18);
    uintptr_t arr = *reinterpret_cast<uintptr_t*>(l + 0x10);
    void** items = arr ? *reinterpret_cast<void***>(arr + 0x10) : nullptr;
    if (!items || count < 2 || count > widelevels::kMaxLevels * 4) return 0;
    void* orig[widelevels::kMaxLevels * 4]; int folder[widelevels::kMaxLevels * 4];
    for (int i = 0; i < count; i++) {
        orig[i] = items[i];
        folder[i] = i;
        for (int k = 0; k < i; k++) if (SameFolder(items[k], items[i])) { folder[i] = folder[k]; break; }   /* folders.IndexOf: first appearance */
    }
    for (int i = 1; i < count; i++)   /* stable insertion sort (OrderBy/ThenBy are stable) */
        for (int j = i; j > 0; j--) {
            int fj = folder[j], fp = folder[j - 1];
            if (fp < fj || (fp == fj && CompareFileNames(items[j - 1], items[j]) <= 0)) break;
            void* t = items[j]; items[j] = items[j - 1]; items[j - 1] = t;
            int ft = folder[j]; folder[j] = folder[j - 1]; folder[j - 1] = ft;
        }
    int moved = 0;
    for (int i = 0; i < count; i++) moved += items[i] != orig[i];
    return moved;
}
/* The file names in list order ("00 01 ..."), for the log. */
void LogOrder(const char* what, void* list) {
    uintptr_t l = reinterpret_cast<uintptr_t>(list);
    int count = *reinterpret_cast<int32_t*>(l + 0x18);
    uintptr_t arr = *reinterpret_cast<uintptr_t*>(l + 0x10);
    void** items = arr ? *reinterpret_cast<void***>(arr + 0x10) : nullptr;
    char out[200]; int n = 0;
    for (int i = 0; items && i < count && n < 180; i++) {
        const char16_t* ch; int f, len;
        SplitPath(items[i], &ch, &f, &len);
        n += snprintf(out + n, sizeof out - n, " ");
        for (int k = f; k < len && ch[k] != u'.' && n < 190; k++) out[n++] = (char)ch[k];
        out[n] = 0;
    }
    out[n] = 0;
    hw::Log("[pc] %s:%s", what, out);
}
}

/* The managed string s as ASCII into out (at most cap-1 characters). */
void Ascii(void* s, char* out, int cap) {
    int n = s ? *reinterpret_cast<int32_t*>(reinterpret_cast<uintptr_t>(s) + 0x10) : 0, k = 0;
    const char16_t* c = s ? reinterpret_cast<const char16_t*>(reinterpret_cast<uintptr_t>(s) + 0x14) : nullptr;
    for (; k < n && k < cap - 1; k++) out[k] = c[k] < 128 ? (char)c[k] : '?';
    out[k] = 0;
}

HOOK_DEFINE_TRAMPOLINE(LoadXmlHook) {
    static void* Callback(void* path) {
        if (void* doc = tfex::gen::scen::MemFileXml(path)) return doc;   /* a TF.EX scenario's map, kept in memory (TEST ONLY: statescenario) */
        bool moved = g_on && SwitchFilePath(path);
        /* the round's level (VersusLevelSystem.GetNextRoundLevel +0xbae8f0 calls from +0xbae9d4): its path and the file opened, so a
         * level that doesn't open (FileStream throws, an unhandled exception) names itself. The first 8, then every 50th. */
        static int rounds = 0;
        if (__builtin_return_address(0) == reinterpret_cast<void*>(Base() + 0xbae9d8) && (++rounds <= 8 || rounds % 50 == 0)) {
            char a[160], f[160];
            Ascii(path, a, sizeof a);
            Ascii(moved ? &g_path : path, f, sizeof f);
            hw::Log("[pc] round level #%d: %s -> %s", rounds, a, f);
        }
        /* a PC versus level: made from the Switch's own and its delta (pclevels.cpp); the romfs's PCVersus file only without one */
        if (moved && g_path.len > 24) {
            char rel[160]; int k = 0;
            bool pc = true;
            for (int i = 0; i < 24; i++) pc &= g_path.ch[i] == (char16_t)paths::kSwitchVersus[i];
            for (int i = 24; pc && i < g_path.len && k < 159; i++) rel[k++] = g_path.ch[i] < 128 ? (char)g_path.ch[i] : '?';
            rel[k] = 0;
            if (pc) if (void* doc = pclevels::Doc(rel)) return doc;
        }
        return moved ? Orig(&g_path) : Orig(path);
    }
};

/* The game's own versus level path ("Content/Levels/Versus/<tower>/<n>.oel") -> a managed string with the PC's text (paths::PcText); the
 * path itself if it isn't one. */
void* PcVersusPath(void* path) {
    if (!path) return path;
    uintptr_t s = reinterpret_cast<uintptr_t>(path);
    int n = *reinterpret_cast<int32_t*>(s + 0x10);
    const char16_t* c = reinterpret_cast<const char16_t*>(s + 0x14);
    int k = paths::Prefix(c, n, paths::kVersus);
    if (!k) return path;
    int sep = k; while (sep < n && c[sep] != u'/' && c[sep] != u'\\') sep++;
    if (sep >= n) return path;
    char16_t buf[160];
    int m = paths::PcText(c + k, sep - k, c + sep + 1, n - sep - 1, false, buf, 160);
    if (m < 0) return path;
    void* r = reinterpret_cast<void* (*)(int32_t)>(Base() + 0x11d10)(m);         /* String.InternalAllocateStr */
    if (!r) return path;
    std::memcpy(reinterpret_cast<char16_t*>(reinterpret_cast<uintptr_t>(r) + 0x14), buf, m * sizeof(char16_t));
    return r;
}

/* The PC's versus levels are NN.oel (the Switch's names, tools/pc-levels.sh); a tower folder's other files are no PC level (the megamod's
 * TowerBall tb.oel, local play only: tools/megamod/romfs.py) */
bool PcLevelName(void* path) {
    uintptr_t s = reinterpret_cast<uintptr_t>(path);
    int n = *reinterpret_cast<int32_t*>(s + 0x10);
    const char16_t* c = reinterpret_cast<const char16_t*>(s + 0x14);
    return n >= 6 && (n == 6 || c[n - 7] == u'/' || c[n - 7] == u'\\') && c[n - 6] >= u'0' && c[n - 6] <= u'9' && c[n - 5] >= u'0' &&
           c[n - 5] <= u'9' && c[n - 4] == u'.' && c[n - 3] == u'o' && c[n - 2] == u'e' && c[n - 1] == u'l';
}

bool g_widerSet = false;   /* WiderSet's folder loaded (the catalog mod Teuria.WiderSet: WiderSetBoot) */
/* Local play on the six-player (wide) towers with WiderSet loaded: WiderSet's levels, as online (docs/PLAN-TFNX-2026-11-04.md: one wide set,
 * 8 spawns each; the Switch's own 6P-Versus levels have 6). MatchSettings.SixPlayer (+0x20); not a mod's versus mode with its own levels
 * (its VersusModeApi: TowerBall's tb.oel). */
bool LocalWide(void* matchSettings) {
    if (!g_widerSet || !matchSettings) return false;
    auto* ms = reinterpret_cast<uint8_t*>(matchSettings);
    int32_t mode = *reinterpret_cast<int32_t*>(ms + 0x30);
    for (int i = 0; i < tfex::mods::ApiCount(tfex::mods::kVersusModeApi); i++) {
        auto* m = static_cast<const tfex::mods::VersusModeApi*>(tfex::mods::ApiAt(tfex::mods::kVersusModeApi, i));
        if (m->mode == mode && m->ownLevels) return false;
    }
    return ms[0x20];
}
HOOK_DEFINE_TRAMPOLINE(GetLevelsHook) {
    static void* Callback(void* tower, void* matchSettings) {
        bool localWide = !g_on && LocalWide(matchSettings);
        if (!g_on && !localWide) return Orig(tower, matchSettings);
        uintptr_t b = Base();
        if (void* sp = tfex::gen::scen::ScenarioLevelPath()) {   /* TEST ONLY (statescenario): only the scenario's map */
            void* only = reinterpret_cast<void* (*)(void*)>(b + 0x9510)(nullptr);
            reinterpret_cast<void (*)(void*, void*)>(b + 0x97a0)(only, sp);
            hw::Log("[pc] GetLevels: the scenario's map only (statescenario)");
            return only;
        }
        void* out = reinterpret_cast<void* (*)(void*)>(b + 0x9510)(nullptr);          /* new List<string>() */
        uintptr_t levels = *reinterpret_cast<uintptr_t*>(reinterpret_cast<uintptr_t>(tower) + 0x28);   /* VersusTowerData.Levels */
        int count = levels ? *reinterpret_cast<int32_t*>(levels + 0x18) : 0;
        uintptr_t arr = levels ? *reinterpret_cast<uintptr_t*>(levels + 0x10) : 0;
        uintptr_t* items = arr ? *reinterpret_cast<uintptr_t**>(arr + 0x10) : nullptr;
        int players = reinterpret_cast<int32_t (*)()>(b + 0xb74cd0)();              /* TFGame.PlayerAmount */
        int kept = 0;
        if (g_wide || localWide) {   /* WiderSet's registered tower: its own files, FortRise's order (widelevels.hpp) */
            int prefix = 0;
            int t = (items && count > 0) ? WideTower(*reinterpret_cast<void**>(items[0] + 0x10), &prefix) : -1;
            if (t < 0) { hw::Log("[pc] GetLevels (wide): tower not in the wide set, %d levels", count); return Orig(tower, matchSettings); }
            /* the game's own rule (VersusTowerData.GetLevels, the PC's too): TEAM DEATHMATCH (mode 5) keeps a level whose TeamSpawns
             * (VersusLevelData +0x1c: the fewer of TeamSpawnA / TeamSpawnB) hold the largest team (MatchSettings.GetMaxTeamSize, 8 seats:
             * WiderSet's loop); every other mode PlayerSpawns (+0x18) >= the players. Online the mode is TF.EX's netplay mode (11): the
             * second. Without it a local wide TDM drew levels with no team spawns (Sacred Ground 00) and SpawnPlayersTeams threw */
            const bool tdm = localWide && *reinterpret_cast<int32_t*>(reinterpret_cast<uintptr_t>(matchSettings) + 0x30) == 5;
            const int need = tdm ? reinterpret_cast<int32_t (*)(void*)>(b + 0xabb5f0)(matchSettings) : players;
            int total = 0;
            for (int l = 0; widelevels::kTable[t].levels[l]; l++, total++) {
                void* path = WidePath(*reinterpret_cast<void**>(items[0] + 0x10), t, l);
                void* pc = reinterpret_cast<void* (*)(void*, void*)>(b + 0xbae420)(nullptr, path);
                int spawns = pc ? *reinterpret_cast<int32_t*>(reinterpret_cast<uintptr_t>(pc) + (tdm ? 0x1c : 0x18)) : 0;
                if (spawns >= need) { reinterpret_cast<void (*)(void*, void*)>(b + 0x97a0)(out, path); kept++; }
            }
            static int said = 0;
            if (!localWide || said++ < 5)
                hw::Log("[pc] GetLevels (wide%s): %s, %d of %d levels (%s >= %d)", localWide ? ", local play" : "", widelevels::kTable[t].folder, kept, total,
                        tdm ? "TeamSpawns" : "PlayerSpawns", need);
            int moved = SortByFolderAndName(out);
            LogOrder(moved ? "GetLevels order (sorted by folder and name, CHANGED)" : "GetLevels order (sorted by folder and name, as listed)", out);
            return out;
        }
        int other = 0;
        for (int i = 0; items && i < count; i++) {
            void* sw = *reinterpret_cast<void**>(items[i] + 0x10);                     /* VersusLevelData.Path */
            if (!sw || !PcLevelName(sw)) { other++; continue; }
            void* path = PcVersusPath(sw);                                              /* as the PC's text */
            void* pc = reinterpret_cast<void* (*)(void*, void*)>(b + 0xbae420)(nullptr, path);   /* reads the PC file (redirect on) */
            int spawns = pc ? *reinterpret_cast<int32_t*>(reinterpret_cast<uintptr_t>(pc) + 0x18) : 0;   /* PlayerSpawns */
            if (spawns >= players) { reinterpret_cast<void (*)(void*, void*)>(b + 0x97a0)(out, path); kept++; }   /* List<string>.Add */
        }
        hw::Log("[pc] GetLevels: %d of %d levels (PC files, PlayerSpawns >= %d)%s", kept, count - other, players,
                other ? " (the tower's non-PC files left out: the megamod's tb.oel)" : "");
        int moved = SortByFolderAndName(out);
        LogOrder(moved ? "GetLevels order (sorted by folder and name, CHANGED)" : "GetLevels order (sorted by folder and name, as listed)", out);
        return out;
    }
};

/* LoopPlatform.Added (+0xa9cb10): a horizontal loop platform (no master, MoveDir Left/Right: +0xe8 <= 1, master +0xf0) lists the
 * row's walls, `for (i = 0; i < 32; i++) x = (i + 31) % 32`, on the Switch as `(i - 1) & 31` with a fixed bound 32. WiderSet makes
 * the PC's 42 and 41 in a wide match (LoopPlatformHooks: all 32s, the first 31): there the lists are made again over 42 columns, in the
 * same order. Level (+0xa80f40) -> Tiles (+0xa0) -> Grid (+0xa98420), Grid[x, y] (+0x9337b0), Entity.Y (+0x92dde0),
 * new List<float> (+0x947280), Add (+0x947b70); wallsLeft +0x110, wallsRight +0x118. */
HOOK_DEFINE_TRAMPOLINE(LoopPlatformAddedHook) {
    static void Callback(void* self) {
        Orig(self);
        if (!(g_on && g_wide)) return;
        uintptr_t o = reinterpret_cast<uintptr_t>(self), b = Base();
        if (*reinterpret_cast<void**>(o + 0xf0) || *reinterpret_cast<uint32_t*>(o + 0xe8) > 1) return;
        void* level = reinterpret_cast<void* (*)(void*)>(b + 0xa80f40)(self);
        void* tiles = level ? *reinterpret_cast<void**>(reinterpret_cast<uintptr_t>(level) + 0xa0) : nullptr;
        void* grid = tiles ? reinterpret_cast<void* (*)(void*)>(b + 0xa98420)(tiles) : nullptr;
        if (!grid) return;
        auto cell = reinterpret_cast<bool (*)(void*, int32_t, int32_t)>(b + 0x9337b0);
        auto add = reinterpret_cast<void (*)(void*, float)>(b + 0x947b70);
        int y = (int)(reinterpret_cast<float (*)(void*)>(b + 0x92dde0)(self) / 10.f);
        void* left = reinterpret_cast<void* (*)(void*)>(b + 0x947280)(nullptr);
        void* right = reinterpret_cast<void* (*)(void*)>(b + 0x947280)(nullptr);
        for (int i = 0; i < 42; i++) {
            int x = (i + 41) % 42;
            bool gi = cell(grid, i, y), gx = cell(grid, x, y);
            if (!gi && gx) add(right, (float)(i * 10));
            else if (gi && !gx) add(left, (float)(i * 10));
        }
        *reinterpret_cast<void**>(o + 0x110) = left;
        *reinterpret_cast<void**>(o + 0x118) = right;
    }
};

/* Drawing never changes the saved state: TF.State's own LevelEntityPatch (Render) and ArrowPatch (DoWrapRender) keep the position and the
 * images' rotation across the draw (upstream since our candidate 2, docs/TFEX-UPSTREAM-CANDIDATES.md); generated, on with every TF.EX patch. */

/* The towers' level lists as the PC has them while online: VersusTowerData.Levels (+0x28) lists every .oel in the tower's folder, on the
 * Switch also the megamod's TowerBall tb.oel (local play only; tools/megamod/romfs.py), which the romfs lists first. TF.EX indexes that list
 * as the PC's (TFCommands.ParseStartLevel and StartOnLevel: test mode's startLevel 0 was tb.oel, no PC file, Calc.LoadXML threw; 2026-11-02),
 * so online it holds the PC's NN.oel only, in the same order; the entries taken out go back when online mode is left. GameData.VersusTowers
 * (static +0x488cad8) -> List<VersusTowerData>; List: items +0x10 (elements from [+0x10]), count +0x18; VersusLevelData.Path +0x10. */
constexpr int kTowerSlots = 24, kLevelSlots = 40;
struct TowerLevels { int32_t* count; void** items; int32_t n; void* orig[kLevelSlots]; };
TowerLevels g_towerLevels[kTowerSlots];
int g_towerLevelsUsed = 0;
void PcTowerLevels(bool on) {
    if (!on) {
        for (int t = 0; t < g_towerLevelsUsed; t++) {
            TowerLevels& s = g_towerLevels[t];
            std::memcpy(s.items, s.orig, (size_t)s.n * sizeof(void*));
            *s.count = s.n;
        }
        if (g_towerLevelsUsed) hw::Log("[pc] tower level lists back to the Switch's (%d towers)", g_towerLevelsUsed);
        g_towerLevelsUsed = 0;
        return;
    }
    uintptr_t towers = *reinterpret_cast<uintptr_t*>(Base() + 0x488cad8);
    int nt = towers ? *reinterpret_cast<int32_t*>(towers + 0x18) : 0;
    uintptr_t tarr = towers ? *reinterpret_cast<uintptr_t*>(towers + 0x10) : 0;
    uintptr_t* tl = tarr ? *reinterpret_cast<uintptr_t**>(tarr + 0x10) : nullptr;
    int removed = 0, skipped = 0;
    for (int i = 0; tl && i < nt; i++) {
        uintptr_t levels = tl[i] ? *reinterpret_cast<uintptr_t*>(tl[i] + 0x28) : 0;
        int n = levels ? *reinterpret_cast<int32_t*>(levels + 0x18) : 0;
        uintptr_t arr = levels ? *reinterpret_cast<uintptr_t*>(levels + 0x10) : 0;
        void** items = arr ? *reinterpret_cast<void***>(arr + 0x10) : nullptr;
        if (!items || n <= 0) continue;
        int keep = 0;
        for (int k = 0; k < n; k++) {
            void* p = items[k] ? *reinterpret_cast<void**>(reinterpret_cast<uintptr_t>(items[k]) + 0x10) : nullptr;
            keep += p && PcLevelName(p);
        }
        if (keep == n) continue;
        if (g_towerLevelsUsed >= kTowerSlots || n > kLevelSlots) { skipped++; continue; }   /* pools: logged below */
        TowerLevels& s = g_towerLevels[g_towerLevelsUsed++];
        s.count = reinterpret_cast<int32_t*>(levels + 0x18); s.items = items; s.n = n;
        std::memcpy(s.orig, items, (size_t)n * sizeof(void*));
        int w = 0;
        for (int k = 0; k < n; k++) {
            void* p = items[k] ? *reinterpret_cast<void**>(reinterpret_cast<uintptr_t>(items[k]) + 0x10) : nullptr;
            if (p && PcLevelName(p)) items[w++] = items[k];
        }
        for (int k = w; k < n; k++) items[k] = nullptr;
        *s.count = w;
        removed += n - w;
    }
    hw::Log("[pc] tower level lists as the PC's: %d non-PC entries (the megamod's tb.oel) taken out, from %d of %d towers (slots %d/%d)%s", removed,
            g_towerLevelsUsed, nt, g_towerLevelsUsed, kTowerSlots, skipped ? " -- POOL FULL: some towers keep their non-PC entries" : "");
}

void SetWideMatch(void* matchSettings, bool wide);
/* The level-path rule checked (paths:: above): for every tower, the PC's text from the game's path in either separator form, the Dark World
 * towers' DarkWorldContent root, WiderSet's root, and each back to its Switch file. One log line; failures each logged. */
void PathsSelfTest() {
    int cases = 0, bad = 0;
    auto eq = [](const char16_t* a, int na, const char* b) { int k = 0; for (; b[k]; k++) if (k >= na || a[k] != (char16_t)b[k]) return false; return k == na; };
    auto expect = [&](const char* what, const char16_t* got, int n, const char* want) {
        cases++;
        if (n >= 0 && eq(got, n, want)) return;
        bad++;
        char g[200]; int k = 0; for (; k < n && k < 199; k++) g[k] = got[k] < 128 ? (char)got[k] : '?'; g[k > 0 ? k : 0] = 0;
        hw::Log("[paths] self-test FAILED (%s): got '%s', want '%s'", what, n >= 0 ? g : "(none)", want);
    };
    for (int t = 0; t < widelevels::kTowers; t++) {
        const char* folder = widelevels::kTable[t].folder;
        bool dw = t >= 12;
        for (int sepForm = 0; sepForm < 2; sepForm++) {
            char sw[120]; std::snprintf(sw, sizeof sw, sepForm ? "Content\\Levels\\Versus\\%s\\01.oel" : "Content/Levels/Versus/%s/01.oel", folder);
            char16_t in[120]; int n = 0; for (; sw[n]; n++) in[n] = (char16_t)sw[n];
            int k = paths::Prefix(in, n, paths::kVersus);
            int sep = k; while (sep < n && in[sep] != u'/' && in[sep] != u'\\') sep++;
            char16_t pc[160]; int m = paths::PcText(in + k, sep - k, in + sep + 1, n - sep - 1, false, pc, 160);
            char want[160]; std::snprintf(want, sizeof want, "%sContent\\Levels\\Versus\\%s\\01.oel", dw ? "DarkWorld" : "", folder);
            expect("PC text", pc, m, want);
            char16_t file[160]; int f = paths::SwitchFile(pc, m, file, 160);
            std::snprintf(want, sizeof want, "Content/Levels/PCVersus/%s/01.oel", folder);
            expect("Switch file", file, f, want);
        }
        char16_t wf[48]; int nf = 0; for (const char* c = folder; *c; c++) wf[nf++] = (char16_t)*c;
        const char16_t wl[] = u"00.oel";
        char16_t pc[160]; int m = paths::PcText(wf, nf, wl, 6, true, pc, 160);
        char want[160]; std::snprintf(want, sizeof want, "mod:Teuria.WiderSet\\Content\\WideLevels\\Versus\\%s\\00.oel", folder);
        expect("wide PC text", pc, m, want);
        char16_t file[160]; int f = paths::SwitchFile(pc, m, file, 160);
        cases++;
        if (f >= 0) { bad++; hw::Log("[paths] self-test FAILED (wide Switch file): WiderSet's %s mapped; its folder serves it", folder); }
    }
    hw::Log("[paths] level-path self-test: %d cases, %s", cases, bad ? "FAILED (lines above)" : "OK");
}

void LevelHooks() {
    static bool hooked = false;
    if (hooked) return;
    LoadXmlHook::InstallAtOffset(0x90c570); GetLevelsHook::InstallAtOffset(0xbc2170); LoopPlatformAddedHook::InstallAtOffset(0xa9cb10);
    hooked = true;
    PathsSelfTest();
}

/* WiderSet's folder loaded (mods/Teuria.WiderSet/src/widerset.cpp Boot): its wide levels from the folder (a mod path), local play on
 * them. The Switch's stand-in for WiderSet's gameplay is this file's (its tables and constants); its 2-8 lobby is the mod's. */
void EnableWiderSet() {
    g_widerSet = true;
    LevelHooks();
    hw::Log("[pc] WiderSet started (its folder): wide levels from sd:/xplay/mods, local wide play on them, the 2-8 lobby");
}

void Set(bool on) {
    LevelHooks();
    if (on == g_on) return;
    uintptr_t b = Base();
    int changed = 0, odd = 0;
    for (const Gap& g : kGaps) {
        auto* w = reinterpret_cast<uint32_t*>(b + g.off);
        uint32_t want = on ? g.pc : g.sw, have = on ? g.sw : g.pc;
        if (*w == want) continue;
        if (*w != have) { hw::Log("[pc] %s: insn is %08x, left alone", g.what, *w); odd++; continue; }
        exl::patch::CodePatcher p(g.off); p.Write<uint32_t>(want); changed++;
    }
    if (on) hotcoals::Install(); else hotcoals::Uninstall();
    if (on) tfex::aislot::DropAll();   /* S: no AI archers in online games (LoaderAI's AI is local play only) */
    megamod::SetOnline(on);
    tfex::gen::GlueFrMods::ReskinOnline(on);   /* MEXICAN PUPPET: the game's alt as it ships online (glue SwitchMods.ReskinOnline) */   /* the megamod's local-play words back to the game's online (megamod/megamod.hpp) */
    if (on) tfex::Tower::ClearNoRandom();   /* TF.EX v0.21.0 TFGame.cs RequestNetplayEntry: MapScene.NoRandom.Clear() on entering netplay */
    if (!on) SetWideMatch(nullptr, false);
    PcTowerLevels(on);
    g_on = on;
    hw::Log("[pc] PC gameplay %s: %d instruction patches %s%s, hot coals, PC levels", on ? "ON" : "OFF", changed, on ? "applied" : "restored",
            odd ? " (some unexpected)" : "");
}
bool On() { return g_on; }
bool Wide() { return g_on && g_wide; }
/* A lobby match's MatchSettings.SixPlayer (+0x20) = wide, for every lobby match (a local 6-player game may have left it on); the value it
 * replaced is given back by SetWideMatch(nullptr, ...) (leaving online mode). */
void SetWideMatch(void* matchSettings, bool wide) {
    if (g_wideField && g_wideField != (matchSettings ? reinterpret_cast<uint8_t*>(matchSettings) + 0x20 : nullptr)) {
        *g_wideField = g_wideOrig;
        hw::Log("[pc] MatchSettings.SixPlayer back to %d", g_wideOrig);
        g_wideField = nullptr;
    }
    if (g_fixedFirst) { *g_fixedFirst = g_fixedFirstOrig; g_fixedFirst = nullptr; }
    WideTablesRestore();
    g_wide = matchSettings && wide;
    ApplyWideGaps(g_wide);
    if (!matchSettings) return;
    /* FixedFirst: the Switch's 6P Sacred Ground and Ascension have <fixedFirst/> in tower.xml (VersusTowerData +0x20, read by
     * VersusLevelSystem.GenLevels +0xbae7e0 through LevelSystem +0x28). WiderSet's are FortRise-registered towers, and FortRise's
     * RegisterVersusTower never sets FixedFirst: on the PC both play TF.State's OwnGenLevel without it. Cleared for the match. */
    void* lsys = *reinterpret_cast<void**>(reinterpret_cast<uintptr_t>(matchSettings) + 0x28);
    uint8_t* tower = lsys ? *reinterpret_cast<uint8_t**>(reinterpret_cast<uintptr_t>(lsys) + 0x28) : nullptr;
    if (wide && tower && tower[0x20]) {
        g_fixedFirst = tower + 0x20; g_fixedFirstOrig = tower[0x20]; tower[0x20] = 0;
        hw::Log("[pc] wide tower: FixedFirst off for the match (as WiderSet's registered tower)");
    }
    if (wide) WideTables();
    uint8_t* f = reinterpret_cast<uint8_t*>(matchSettings) + 0x20;
    if (!g_wideField) { g_wideField = f; g_wideOrig = *f; }
    *f = wide ? 1 : 0;
    hw::Log("[pc] lobby match: %s set (MatchSettings.SixPlayer %d, was %d)", wide ? "wide" : "standard", *f, g_wideOrig);
}
/* Every patched instruction as the mode says it should be (ON: the PC's, OFF: the Switch's originals), logged; true if all are. */
bool Verify(const char* when) {
    uintptr_t b = Base(); int bad = 0;
    for (const Gap& g : kGaps) { uint32_t w = *reinterpret_cast<uint32_t*>(b + g.off); if (w != (g_on ? g.pc : g.sw)) { bad++; hw::Log("[pc] verify: %s is %08x", g.what, w); } }
    for (const Gap& g : kWideGaps) { uint32_t w = *reinterpret_cast<uint32_t*>(b + g.off); if (w != (g_wideGapsOn ? g.pc : g.sw)) { bad++; hw::Log("[pc] verify: %s is %08x", g.what, w); } }
    const uint32_t hc[3][2] = { { 0xae8b14, 0x39452668u }, { 0xaea8d0, 0x39452668u }, { 0xae9610, 0x394b1268u } };   /* hot coals sites' first words */
    for (auto& h : hc) { uint32_t w = *reinterpret_cast<uint32_t*>(b + h[0]); bool orig = w == h[1]; if (orig == g_on) { bad++; hw::Log("[pc] verify: hot coals site +%#x is %08x", h[0], w); } }
    hw::Log("[pc] verify (%s): PC gameplay %s, %s", when, g_on ? "ON" : "OFF", bad ? "MISMATCH" : "all instructions as expected");
    return bad == 0;
}
}
