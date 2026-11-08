#pragma once
/*
 * TowerFall Switch — "8-Player Mode" native mod (exlaunch overlay)
 * ----------------------------------------------------------------
 * Goes at exlaunch/source/program/main.cpp of THIS MOD'S OWN exlaunch
 * checkout (~/Code/tf8-exlaunch — do NOT share a checkout with
 * towerball-mod; see ../../README.md "Isolation").
 *
 * Design + RE strategy: ../../../docs/8-PLAYER-FEASIBILITY.md
 * Offsets + build order:  ../../OFFSETS.md (v1.4.1.6, flat-image offsets)
 *
 * Milestone 1 (ACTIVE — the cap hook, step 1 of the build order):
 *   - get_MaxPlayers @ 0xb34030: return 8 when RollcallMode == 5, else
 *     Orig(). Modes 0-4 are byte-identical (additive rule).
 *   - get_MinPlayers needs NO hook: retail already returns 2 for mode >= 5
 *     (the `(mode-2) u>= 3` branch — see OFFSETS.md).
 *   - DEV SHIM (TF8_DEV_FORCE_MODE5): the "2-6 PLAYERS" confirm (0xb72260)
 *     is re-pointed to enter mode 5, so the cap is testable before the real
 *     "2-8" menu entry exists (step 2). MUST be off in any shipped build.
 *
 * Controllers: NOTHING TO DO (FINDINGS.md) — retail already calls
 * nn::hid::SetSupportedNpadIdType({No1..No8, Handheld}, 9); the 6-player
 * limit is purely game logic.
 *
 * Everything past M0 is ADDITIVE: it must not change 4/6-player behavior.
 * Gate every 8-only hook behind RollcallMode == 5.
 */

#include "lib.hpp"
#include "megamod/megamod.hpp"   /* MMLOG (the match log), the online gate, Patch */
#include "tfex/gen/tfexgen_hooks.hpp"
#include "tfex/gen/tfexgen_GlueFrMods.hpp"   /* AliasMissingSubtexture: the controller icons for players 7-8 */
#include "tfex/runtime/tfexgen_code_rt.hpp"
#include "tfex/gen/tfexgen_binding.hpp"   /* the game members WiderSet's roll call look uses */
#include <cstdio>   /* the generated hooks this shares a method with (Teams+'s) */
#include "tfex/runtime/tfexgen_services.hpp"   /* the own TF.EX lobby (its MaxPlayers: roll call slots past 6 online) */
#include "patch/code_patcher.hpp"   /* exl::patch::CodePatcher — in-place patching */
#include <lib/util/sys/modules.hpp> /* exl::util::GetMainModuleInfo (module base) */

/* Dev-only: force-mode-5 shim — RETIRED after on-device test 2026-07-07.
 * Forcing RollcallMode=5 at the 2-6 confirm sent the menu dispatch down the
 * DarkWorld branch (out-of-range 5 hits the >= range checks in the
 * mode-dispatch sites), because the post-confirm screens branch on
 * RollcallMode. The real mode 5 needs those dispatch sites patched (steps
 * 2/3). Keep 0 unless re-testing dispatch behavior deliberately. */
#define TF8_DEV_FORCE_MODE5 0

/* Dev-only CAP TEST (the current shim): patch the shared rodata cap table so
 * mode 1 ("VERSUS 2-6") caps at 8 through the NORMAL versus flow. On-device
 * test 2026-07-07 proved get_MaxPlayers is INLINED at the real cap checks
 * (0xb32e24, 0xb333bc — its only BL caller is the reflection thunk
 * 0x27eefec), but every inlined copy and the getter read the same table at
 * 0x38e22d0 = {6,2,4,1} for modes 1..4. Patching entry[0] 6->8 flips every
 * cap check at once. NOT additive (alters retail 2-6) — dev builds only;
 * the shipped mod patches nothing here and gates on mode 5 instead. */
#define TF8_DEV_CAP_2TO8 1

namespace tf8 {

/* Flat-image offsets, v1.4.1.6 — regenerate via tools/re/metaresolve.py
 * (see OFFSETS.md "Reproduce"). Same convention as towerball.hpp:
 * module-base-relative == exlaunch InstallAtOffset. */
namespace off {
    /* RollcallElement.get_MaxPlayers — THE CAP (DIRECT, 1 caller). */
    constexpr uintptr_t RollcallElement_get_MaxPlayers = 0xb34030;
    /* Versus 2-6 (mode 1) MAIN-MENU confirm callback — writes RollcallMode=1
     * (str @0xbce5d4) then tail-calls the menu-state transition
     * (mov w1,#5; b 0xa9ed20 — state 5 = rollcall). Template for the future
     * mode-5 button; dev shim target for now.
     * NOTE: 0xb72260 also writes mode 1 but is a DIFFERENT path (never fired
     * on the menu route in on-device test 2026-07-07). The confirm family:
     * mode1=0xbce530, mode2=0xb0d620ish, mode3=0x9f6d20ish; general setter
     * set_RollcallMode(v) tail @0xbce4bc. */
    constexpr uintptr_t Versus6_OnConfirm = 0xbce530;
    /* Versus cap table (rodata): {6,2,4,1} indexed by mode-1 for modes 1..4;
     * mode 0 falls through to a literal 4. Read by get_MaxPlayers @0xb34078
     * AND by its inlined copies @0xb32e24 / @0xb333bc (the real rollcall cap
     * checks). One shared table -> one dev patch point. */
    constexpr uintptr_t VersusCapTable = 0x38e22d0;
    /* MainMenu.RollcallMode static — BRUTE statics are DOUBLY indirected:
     * the data cell holds a pointer to the static's storage, the value lives
     * at +0 in the storage (get_MaxPlayers @0xb34060: ldr x8,[base+0x4792b58];
     * ldr w8,[x8]). Same crash-class warning as TowerBall's Sounds statics:
     * never skip the second deref. */
    constexpr uintptr_t RollcallMode_cell = 0x4792b58;
}

/* The new mode value: RollcallModes = {Versus4=0, Versus6=1, Quest=2,
 * DarkWorld=3, Trials=4} + our additive Versus8 = 5. get_MinPlayers already
 * yields 2 for any mode >= 5, so only the max cap needs a hook. */
constexpr int kVersus8Mode = 5;
constexpr int kVersus8Cap  = 8;

namespace off {
    /* MainMenu.CreateRollcall (DIRECT) — builds the join slots. Mode 1 path
     * @0xaa13f4 unrolls RollcallElement idx 0..5; mode!=1 path @0xaa15d0 is
     * the 4-slot variant (w2=0). */
    constexpr uintptr_t MainMenu_CreateRollcall = 0xaa1150;
    /* The slot factory + scene-add chain, exactly as CreateRollcall's
     * unrolled body calls them (args recovered from the disassembly):
     *   el    = RollcallSlotNew(nullptr, index, sixVariant)   @0xb31f10
     *   scene = MenuScene(mainMenu)                           @0x943c40
     *   li    = ElementLayerIndex(el)                         @0x92dd20
     *   layer = SceneGetLayer(scene, li)                      @0x92d9f0
     *   SceneLayerAdd(layer, el, 0)                           @0x938e40 */
    constexpr uintptr_t RollcallSlotNew    = 0xb31f10;
    constexpr uintptr_t MenuScene          = 0x943c40;
    constexpr uintptr_t ElementLayerIndex  = 0x92dd20;
    constexpr uintptr_t SceneGetLayer      = 0x92d9f0;
    constexpr uintptr_t SceneLayerAdd      = 0x938e40;
    /* ---- Kill-replay input rows for P7/P8 --------------------------------
     * The replay system (RE'd 2026-07-13): ReplayRecorder.Record (0xb2cfb0)
     * snapshots each player's InputState (0x1c bytes) per frame into a states
     * array at frame+0x50; the viewer object (Level+0x78, ctor 0xb2f4d0) holds
     * per-player InputRenderers in an array @+0xb8 (created UNROLLED 0..5, one
     * per joined player, stacked by height int @renderer+0x10, ctor
     * 0xa5c450(null, playerIndex, float y)); the replay render pass (0xb30a10)
     * has six unrolled `if (renderers[i]) renderers[i].Render(&frame.states[i])`
     * blocks (0xa5c8b0) with NO early exit before them. Widening the two arrays
     * + the Record loop (see kArrayWidens/kBoundPatches) plus replicating the
     * two unrolled sequences for idx 6/7 gives full 8-player replays. */
    constexpr uintptr_t ReplayViewer_ctor    = 0xb2f4d0; /* factory: (null,..) -> viewer */
    constexpr uintptr_t ReplayRender         = 0xb30a10; /* unrolled per-player render pass */
    constexpr uintptr_t InputRenderer_New    = 0xa5c450; /* (null, playerIndex, float y) */
    constexpr uintptr_t InputRenderer_Render = 0xa5c8b0; /* (self, InputState* 0x1c) */
    constexpr uint32_t  Viewer_Renderers  = 0xb8;  /* InputRenderer[] */
    constexpr uint32_t  Viewer_Frames     = 0x70;  /* List<frame> (items @+0x10) */
    constexpr uint32_t  Viewer_FrameIdx   = 0x78;  /* int, index into Frames */
    constexpr uint32_t  Frame_States      = 0x50;  /* InputState[] (stride 0x1c) */
    constexpr uint32_t  Renderer_Height   = 0x10;  /* int, row height for stacking */
    /* ---- Variant per-player values for P7/P8 -----------------------------
     * Variant methods are compiled UNROLLED over playerValues[0..5] (this+0x60,
     * bool[], widened to 8 via kArrayWidens) — there is no loop bound to patch,
     * so the four unrolled methods get post-hooks replicating indices 6/7.
     * Method map from the metadata builder (@0x282bf84):
     *   get_Value  0xb9c570  any-player-has-it   -> OR in [6]/[7]
     *   set_Value  0xb9c750  set-for-all-players -> also write [6]/[7]  (this is
     *                        what makes GLOBAL variant toggles apply to P7/P8)
     *   get_AllTrue 0xb9cd90 all-JOINED-players  -> AND in [6]/[7] when joined
     *   Clean      0xb9d6c0  clear-for-unjoined  -> clear [6]/[7] when unjoined
     * get_Item/set_Item are dynamic (bounds = array length = 8) — no hook.
     * EnforceAll is a thin wrapper over set_Value — covered. */
    constexpr uintptr_t Variant_get_Value  = 0xb9c570;
    constexpr uintptr_t Variant_set_Value  = 0xb9c750;
    constexpr uintptr_t Variant_get_AllTrue= 0xb9cd90;
    constexpr uintptr_t Variant_Clean      = 0xb9d6c0;
    constexpr uint32_t  Variant_playerValues = 0x60;   /* bool[] field */
    /* VariantPerPlayer (per-player toggle screen). Ctor 0xb9f710 creates the
     * six wigglers UNROLLED (factory 0x95ab20(20,0,0,0,0, 5.0f) + component-add
     * 0x92f9d0(self, w)) and points this+0xe8 at a SHARED STATIC positions
     * Vector2[] (4- or 6-entry by the six flag @+0xe0). Update/Render column
     * bounds widened via kBoundPatches; the hook supplies wigglers 6/7 and an
     * 8-entry positions array (a static FAKE managed array — klass copied from
     * the retail array, data@+0x10 / len-ptr@+0x20, Vector2 has no refs so the
     * conservative GC is indifferent — avoiding newarr's type-init timing). */
    /* FightButton ctor (0xb2c120; factory `void*(self)`, self=null -> alloc) —
     * the FIGHT banner + "press + to begin" prompt, built FIRST on the rollcall
     * and team-select screens so it renders BEHIND the join boxes. Our slots
     * hold depths -8..-1 (RollcallSlotFactoryHook); the banner gets -20 so the
     * prompt draws in FRONT of the portraits in the 8-player lobby. */
    constexpr uintptr_t FightButton_ctor  = 0xb2c120;
    constexpr int       kFightButtonDepth = -20;
    /* Mode-start confirm handlers that reset versus join state: each clears
     * TFGame.Players[0..5] UNROLLED (strb wzr,[data,#0..5]; audit 2026-07-13,
     * sites 0xae2a74/0xae2d44/0xae3114) — P7/P8 joins from a prior 8p match
     * would leak into Quest/co-op menus as phantom players. Post-hooks clear
     * slots 6/7 to complete the reset. */
    constexpr uintptr_t ResetJoin_A = 0xae2880;
    constexpr uintptr_t ResetJoin_B = 0xae2b50;
    constexpr uintptr_t ResetJoin_C = 0xae2f20;
    constexpr uintptr_t VariantPerPlayer_ctor = 0xb9f710;
    constexpr uintptr_t Wiggler_New          = 0x95ab20;
    constexpr uintptr_t Entity_AddComponent  = 0x92f9d0;
    constexpr uint32_t  VPP_Wigglers  = 0xd8;   /* Wiggler[]           */
    constexpr uint32_t  VPP_SixFlag   = 0xe0;   /* bool: 6-variant UI  */
    constexpr uint32_t  VPP_Positions = 0xe8;   /* Vector2[] (static)  */
    /* VariantPerPlayer.cctor (0xba0090) also builds a per-player HEAD-ICON
     * subtexture array (static cell 0x4796ce8, ref[6] from six per-player
     * statics 0x478fda8..0x478fdd0) — Render indexes it per column, and it was
     * THE thrower (+0xba0f48 stub) once the column loop ran to 8 (on-device
     * crash 2026-07-13, "pressed R -> crash"). Rebuilt once as a REAL managed
     * ref[8] via the game's newarr with the cctor's own array-type cell
     * (0x4722a98) — GC-safe; entries 6/7 reuse P1/P2's icons until we have
     * dedicated art. */
    constexpr uintptr_t VPP_Heads_cell     = 0x4796ce8;
    constexpr uintptr_t SubtexArrType_cell = 0x4722a98;
    /* Monocle.Entity.set_Depth(self, int) @0x92d970 — writes the depth field
     * (+0x58) and, if the entity is already in a scene, re-sorts; when the
     * scene ptr (+0x20) is null it just stores the field (safe to call during
     * construction, before the scene-add). actualDepth is (re)computed from
     * +0x58 by the add path (0x26bcf30 -> SetActualDepth 0x937b30), so a depth
     * set here takes effect when the slot is added. Monocle convention: LOWER
     * depth renders in FRONT. */
    constexpr uintptr_t Entity_set_Depth   = 0x92d970;
}

/* Call a game function by flat-image offset (same convention as towerball.hpp
 * tb::Call). */
template <typename Fn>
static inline Fn Call(uintptr_t off_) {
    return reinterpret_cast<Fn>(exl::util::GetMainModuleInfo().m_Total.m_Start + off_);
}

/* ---- Step 4: widen the TFGame player arrays 6 -> 8 -----------------------
 * The RollcallElement ctor indexes PlayerInputs/Players/AltSelect/Characters
 * by playerIndex (PC TFGame.cs:75-81,227 — all `new xxx[8]` in the 8p build),
 * so index 6 threw IndexOutOfRange (on-device crash 2026-07-07, throw stub
 * 0xb327a8). Every alloc materializes its count as `orr w8,wzr,#6` right
 * before `bl 0x6110` (newarr); patch each to `movz w8,#8`. Additive-safe:
 * old modes never index past 5 (cap-guarded), extra slots stay empty.
 * CoOpCrowns (0xb74430) stays count-4: co-op only, not versus. */
struct ArrayWiden { uintptr_t movOff; const char* name; };
constexpr ArrayWiden kArrayWidens[] = {
    { 0xb74308, "Players"       },  /* cctor      -> cell 0x4793890 */
    { 0xb7438c, "Characters"    },  /* cctor      -> cell 0x4792458 */
    { 0xb74474, "AltSelect"     },  /* cctor      -> cell 0x4792460 */
    { 0xb75820, "PlayerInputs"  },  /* Initialize -> cell 0x4791fa8 */
    { 0xb054fc, "PlayerInputs2" },  /* SECOND alloc into 0x4791fa8 (str
                                     * @0xb05548) — this one actually runs on
                                     * boot; missing it left len=6 (runtime
                                     * diag 2026-07-07: 8/8/8/6 → same rollcall
                                     * throw). */
    { 0x93d280, "MInput.JoyCons" }, /* Switch pad list (cell 0x4791f78).
                                     * MInput's per-frame maintenance loop
                                     * (0x93d4d4..0x93d630) runs to
                                     * JoyCons.Length (bl 0x1885e0 = getLen)
                                     * and assigns PlayerInputs[i] from
                                     * JoyCons[i] — widening this array is
                                     * what lets pads 7/8 register. */
    { 0xa7af7c, "Level+0x110 bool[]" }, /* per-player bool array on the Level
                                     * object (ctor alloc, newarr @0xa7afac)
                                     * — IndexOutOfRange during 8-player TDM
                                     * level load (test 17, thrower region
                                     * 0xa7c850). = PC Level.cs:205
                                     * controllerAttachedFlags[8]. */
    { 0xb37be0, "RoundLogic.Kills" }, /* int[] into RoundLogic+0x30 (newarr
                                     * @0xb37c04) = PC RoundLogic.cs:56
                                     * Kills = new int[8]; indexed on every
                                     * kill by killer/victim index. */
    { 0xb389b4, "SpawnFFA Vector2[]" }, /* SpawnPlayersFFA's local spawn-pos
                                     * array (newarr @0xb389e4) = PC
                                     * RoundLogic.cs:168 new Vector2[8]. */
    { 0xb9c394, "Variant.playerValues" }, /* bool[] -> Variant+0x60 (newarr
                                     * @0xb9c3c4) = PC Variant.cs:186 [8].
                                     * Variant.GetValue(idx) @0xb9d0d0 threw
                                     * for idx 6 during Player ctor — found
                                     * instantly by the throw-site logger
                                     * (+0xb9d160, test 21). */
    { 0xaec24c, "Player.wasColliders" }, /* static Collider[] (cell 0x47958a0,
                                     * lazy-init in Player code @0xaec240) =
                                     * PC Player.cs:390 [8]; used by player
                                     * collision every frame — found by the
                                     * static audit, not a crash. */
    { 0xb9fa44, "VariantPerPlayer.wigglers" }, /* ref[] -> +0xd8, Variant menu
                                     * region = PC VariantPerPlayer.cs:57 [8].
                                     * Audit find. */
    { 0xb2d2c0, "ReplayRecorder.states" }, /* per-frame InputState[6] (stride 0x1c,
                                     * newarr @0xb2d2f0) — the kill-replay input
                                     * snapshot. Widened so P7/P8 inputs record;
                                     * paired with the Record loop bound patch +
                                     * the two replay hooks below. */
    { 0xb2f5cc, "ReplayViewer.inputRenderers" }, /* InputRenderer[6] @viewer+0xb8
                                     * (newarr @0xb2f5fc; viewer object lives at
                                     * Level+0x78). Widened so slots 6/7 exist for
                                     * ReplayViewerCtorHook to fill. */
    { 0xba9df4, "VersusAwards.playerAwards[]" }, /* List<AwardInfo>[] in GetAwards
                                     * (newarr @0xba9e24) = PC VersusAwards.cs:453
                                     * [8]. AssignAward's rating loop is already
                                     * dynamic (Players.Length=8) so it rates
                                     * P7/P8, but the size-6 array made the
                                     * playerAwards[6/7] bounds check throw. See
                                     * AssignAwardHook + the assign-loop bound
                                     * patch below. */
};

constexpr uint32_t kOrrW8Imm6 = 0x321f07e8;  /* orr w8,wzr,#6 (expected) */
constexpr uint32_t kMovzW8Imm8 = 0x52800108; /* movz w8,#8 */

/* Literal loop bounds that don't derive from array lengths. Each entry is
 * verified against the expected original word before patching. */
struct BoundPatch { uintptr_t off; uint32_t expect; uint32_t repl; const char* name; };
constexpr BoundPatch kBoundPatches[] = {
    /* RoundLogic.SpawnPlayersTeams (0xb39070): three `i++ / cmp wX,#6 /
     * b.ne loop` player loops -> only 6 of 8 joined archers spawned in TDM
     * (on-device 2026-07-07). cmp #6 -> cmp #8; loop exit still exact. */
    { 0xb39320, 0x71001b1f, 0x7100231f, "SpawnTeams loop1 (w24)" },
    { 0xb39448, 0x71001b1f, 0x7100231f, "SpawnTeams loop2 (w24)" },
    { 0xb395b0, 0x71001aff, 0x710022ff, "SpawnTeams loop3 (w23)" },
    /* SpawnPlayersFFA per-player spawn loop: `x23++; cmp x23,#6; b.lo` —
     * 64-bit counter (why earlier 32-bit scans missed it). Only 6 archers
     * spawned in 8-FFA (test 19). */
    { 0xb38f40, 0xf1001aff, 0xf10022ff, "SpawnFFA loop (x23)" },
    /* VersusMatchResults portrait layout: hard `PlayerAmount == 6` assert
     * (cases 1-5 exit earlier; 7/8 -> throw @+0xbb0500, match-end crash
     * test 24). NOP the b.ne so 7/8 reuse the 6-player layout; counts 1-6
     * behave identically. */
    { 0xbafd10, 0x54003dc1, 0xd503201f, "MatchResults count==6 assert" },
    /* Task B — VersusRoundResults (between-round standings) P7/P8 "P#" labels.
     * The win-pip icons + crowns loop on Session.Scores.Length (already 8 in
     * FFA via the Session ctor widening), so those show for P7/P8 already. The
     * player-LABEL loop is the one hardcoded to 6: it's a countdown coroutine
     * loop where playerIndex i = x24+5, x24 inits 0 and exits at x24==-6
     * (i=5..0). Start x24 at 2 so i=7..0 (8 labels). The FFA label Y is a
     * parallel accumulator w23 = 41+i*20 seeded for i=5 (add w8,#0x65=141);
     * reseed for i=7 (add w8,#0x8d=181, i.e. two more 20px rows). Draws only
     * for JOINED players, so 6-player games are unaffected (6/7 skip). Team
     * branch indexes an int[2] via GetScoreIndex (0/1) — safe for idx 6/7. */
    { 0xbba2c0, 0xaa1f03f8, 0xd2800058, "RoundResults label loop init i=5->7 (x24=0->2)" },
    { 0xbba2c8, 0x11019517, 0x11023517, "RoundResults label Y init (i=7 row, 141->181)" },
    /* Task-A awards: VersusAwards.AssignAward assignment loop (0xbab490+) hands
     * the award to every player matching the max rating, but is capped `cmp
     * x23,#6`. Widen to 8 so P7/P8 (already rated by the dynamic rating loop)
     * can receive awards. Paired with the playerAwards[] widen above +
     * AssignAwardHook (inits the 6/7 list slots). */
    { 0xbab530, 0xf1001aff, 0xf10022ff, "AssignAward assign loop (x23) 6->8" },
    /* VariantPerPlayer (the per-player variant toggle screen) — Update's cursor
     * right-bound and Render's column loop are both `csel(4, 6)` by the
     * six-variant flag (this+0xe0); widen the 6 to 8. Safe at <=6 players:
     * cursor movement scans TFGame.Players[] for a JOINED index before landing,
     * and Render's per-column body is behind the same joined check — indices
     * 6/7 are only reached when P7/P8 actually joined. Their wigglers +
     * positions[6/7] are supplied by VariantPerPlayerCtorHook. */
    { 0xba0718, 0x321f07e8, 0x52800108, "VariantPerPlayer.Update cursor bound 6->8 (w8)" },
    { 0xba0c38, 0x321f07fb, 0x5280011b, "VariantPerPlayer.Render column loop 6->8 (w27)" },
    /* ReplayRecorder.Record per-player snapshot loop: `x24++; cmp x24,#6; b.lo`
     * over TFGame.Players/PlayerInputs writing states[x24] (stride 0x1c). With
     * the states array widened to 8 (kArrayWidens), widening this bound makes
     * the kill replay record P7/P8's inputs too. Reads are all from size-8
     * arrays (Players/PlayerInputs); the write target is the widened array. */
    { 0xb2d3ec, 0xf1001b1f, 0xf100231f, "ReplayRecorder.Record loop (x24) 6->8" },
    /* MatchSettings.GetMaxTeamSize (0xabb5f0): `x21++; cmp x21,#6; b.lo` over the seats (get_Item serves 6-7); WiderSet's loop is 8. The
     * Team Deathmatch level filter reads it (VersusTowerData.GetLevels: TeamSpawns >= GetMaxTeamSize): a team of 7 counted as 5 lists
     * other levels than the PC's */
    { 0xabb730, 0xf1001abf, 0xf10022bf, "MatchSettings.GetMaxTeamSize loop (x21) 6->8" },
    /* GetAwards save-stats loop (records each player's awards into
     * SaveData.Stats.Awards for achievements/unlocks): `cmp x24,#6`. The
     * consolation-prize assignment itself is a DYNAMIC foreach over
     * playerAwards.Length (already 8), so P7/P8 display their consolation
     * prize without this; widening here just lets their awards count toward
     * save stats. Inner accesses (playerAwards[i], Characters[i]) are size-8. */
    { 0xbaab88, 0xf1001b1f, 0xf100231f, "GetAwards save-stats loop (x24) 6->8" },
    /* NOTE: results-screen ROW loops (0xbafff0/0xbb0020/0xbb0148 cmp x24,#6)
     * were briefly widened to 8 to show P7/P8 on the results screen — REVERTED
     * (test 28 team-match-end crash): those loops feed GetScoreIndex (0xb45230)
     * + a web of per-player/per-score arrays that are NOT all size-8 on the
     * team path (team Scores is size 2), so index 6/7 threw IndexOutOfRange at
     * 0xbaffc8 -> 0xbb049c. With the loops at retail #6 + the assert above
     * NOP'd, 8-player matches reach the results screen and show the first 6
     * players' stats (cosmetic gap). P7/P8-in-results = deep future work
     * (widen the whole results backing-array web); not worth a match-end crash. */
};

/* Session ctor (0xb43bxx) per-player allocs — counts are shared regs, and
 * these are ORR w21/w23 forms, so they get their own expected/replacement
 * words. TeamMode's 2-counts stay 2 (team scores really are 2 slots).
 *   w21: MatchStats[6] (elem 0x2a8, ->+0x28) + Scores FFA count (->+0x30)
 *   w23: OldScores FFA count (->+0x38) + ref array (->+0x40)
 * = PC Session.cs:80-82 (all 8 in the 8p build). Throw-site logger hit
 * +0xb4591c (Scores read) in test 22. */
constexpr BoundPatch kSessionCountPatches[] = {
    { 0xb43bbc, 0x321f07f5, 0x52800115, "Session w21 count (MatchStats/Scores)" },
    { 0xb43c4c, 0x321f07f7, 0x52800117, "Session w23 count (OldScores/+0x40)" },
};

/* Permanent, online too, like the arrays (PatchPlayerArrays): the loop bounds (seats 7-8 empty: the same as the game's 6; a wide TF.EX
 * lobby past 6 seats needs them, as WiderSet's 8 on the PC: the results screen throws for 7-8 players without the count==6 nop), and
 * the Session's per-seat counts: TF.State's seat padding (tfex/runtime/tfexgen_state.cpp SeatsToPc) maps an array of
 * TFGame.Players.Length seats to the PC's; a 6-seat MatchStats beside 8 Players desynced smoke lobby (140734, frame 0). */
static void PatchBounds() {
    uintptr_t base = exl::util::GetMainModuleInfo().m_Total.m_Start;
    for (const auto& b : kBoundPatches) {
        uint32_t cur = *reinterpret_cast<uint32_t*>(base + b.off);
        if (cur != b.expect) { MMLOG("[8player] %s @+%#x: found %08x, expected %08x: left alone", b.name, (unsigned)b.off, cur, b.expect); continue; }
        exl::patch::CodePatcher p(b.off);
        p.Write<uint32_t>(b.repl);
    }
    for (const auto& b : kSessionCountPatches) {
        uint32_t cur = *reinterpret_cast<uint32_t*>(base + b.off);
        if (cur != b.expect) { MMLOG("[8player] %s @+%#x: found %08x, expected %08x: left alone", b.name, (unsigned)b.off, cur, b.expect); continue; }
        exl::patch::CodePatcher p(b.off);
        p.Write<uint32_t>(b.repl);
    }
}

/* Permanent (not megamod::Patch): most of these run once at boot (TFGame's cctor, MInput), and the PC's arrays are all 8 (the
 * PC sources cited per entry), so online keeps them too. */
static void PatchPlayerArrays() {
    uintptr_t base = exl::util::GetMainModuleInfo().m_Total.m_Start;
    int patched = 0;
    for (const auto& w : kArrayWidens) {
        uint32_t cur = *reinterpret_cast<uint32_t*>(base + w.movOff);
        if (cur != kOrrW8Imm6) {
            MMLOG("[8player] array widen SKIP %s @%x (found %08x, expected %08x)",
                        w.name, (unsigned)w.movOff, cur, kOrrW8Imm6);
            continue;
        }
        exl::patch::CodePatcher p(w.movOff);
        p.Write<uint32_t>(kMovzW8Imm8);
        patched++;
    }
    MMLOG("[8player] player arrays widened 6->8 (%d/%d sites)", patched,
                (int)(sizeof(kArrayWidens) / sizeof(kArrayWidens[0])));
}

static inline uint32_t* RollcallModeStorage() {
    uintptr_t base = exl::util::GetMainModuleInfo().m_Total.m_Start;
    return *reinterpret_cast<uint32_t**>(base + off::RollcallMode_cell);
}

/* BRUTE managed array via its static cell: cell -> storage ptr; *storage ->
 * array object; obj+0x10 -> element data; obj+0x20 -> ptr to length (the
 * guard sequence @0xb3252c / @0xb0564c). */
static void** ArrayObj(uintptr_t cell) {
    uintptr_t base = exl::util::GetMainModuleInfo().m_Total.m_Start;
    void** storage = *reinterpret_cast<void***>(base + cell);
    if (!storage) return nullptr;
    return reinterpret_cast<void**>(*storage);
}
static int ArrayLenObj(void** obj) {
    if (!obj) return -1;
    int32_t* boundsp = *reinterpret_cast<int32_t**>(reinterpret_cast<uint8_t*>(obj) + 0x20);
    return boundsp ? *boundsp : -1;
}
static void** ArrayData(void** obj) {
    return obj ? *reinterpret_cast<void***>(reinterpret_cast<uint8_t*>(obj) + 0x10) : nullptr;
}
static int ArrayLen(uintptr_t cell) { return ArrayLenObj(ArrayObj(cell)); }

namespace off {
    /* PlayerInput.AssignInputs (DIRECT, 3 callers). Its tail (@0xb05610+)
     * unrolls `if (MInput.JoyCons[i]) PlayerInputs[i] = new JoyConInput(i)`
     * for i = 0..5 ONLY — with JoyCons widened to 8, connected pads 7/8 get a
     * JoyCon but no JoyConInput, and MInput's maintenance loop then calls a
     * JoyConInput method on the isinst-null PlayerInputs[i] -> native fault
     * @0xa61a40 (test 9). Post-hook replicates iterations 6/7. */
    constexpr uintptr_t PlayerInput_AssignInputs = 0xb054b0;
    /* JoyConInput factory/ctor: (nullptr, index) -> new JoyConInput(index);
     * allocates when x0 == null, stores index at +0x20. */
    constexpr uintptr_t JoyConInput_New = 0xa61690;
    constexpr uintptr_t JoyCons_cell      = 0x4791f78;  /* MInput.JoyCons */
    constexpr uintptr_t PlayerInputs_cell = 0x4791fa8;  /* TFGame.PlayerInputs */
    /* BRUTE newarr helper: (typeObj, elemKindOrSize, &count, 1) -> array.
     * Same call every alloc site makes. */
    constexpr uintptr_t NewArr = 0x6110;
    /* TreasureSpawner.ChestChances (static cell) — retail float[5][] for
     * player counts 2..6, indexed [PlayerAmount-2] (throw @+0xb873c0 at
     * round transition with 8 players, test 23). PC 8p = float[7][]. Fixed
     * lazily in CreateRollcallHook: rebuild as 7 rows, rows 5/6 alias the
     * 6-player row (row 4). Outer array's type object cell: 0x4724640. */
    constexpr uintptr_t ChestChances_cell     = 0x4796ae0;
    constexpr uintptr_t ChestChances_typeCell = 0x4724640;
    /* MatchTeams.get_Item (DIRECT, 55 callers): jump-table over the SIX
     * Allegiance fields, idx>5 -> throw stub @0xabc4c8. Related bodies:
     * get_HasEvenTeams=0xabc520, get_ProperlyAssigned=0xabc0d0,
     * GetPlayersOfAllegiance=0xabc950 (all 6-field-bound; extend as needed). */
    constexpr uintptr_t MatchTeams_get_Item = 0xabc420;
    constexpr uintptr_t MatchSettings_GetPlayerTeamSize = 0xabb910;   /* int(MatchSettings*, int playerIndex): seats 0-5 unrolled */
    /* MatchTeams.set_Item (0xabc870): `cmp w1,#5; b.hi throw`, then a jump
     * table storing w2 to one of the six Allegiance fields (+0x10..+0x24).
     * idx>5 throws — so a P7/P8 TeamSelector writing its team would crash.
     * Hooked (symmetric with get_Item): route idx 6/7 into g_teamSide.
     * (metaresolve returned UNKNOWN for the property setter name; found via
     * the write sites inside TeamSelector.Update @0xb6bc98 / @0xb6bd18.) */
    constexpr uintptr_t MatchTeams_set_Item = 0xabc870;
    /* MatchTeams.AmountOfAllegiance (0xabcc20): UNROLLED reads of the six
     * physical fields (+0x10..+0x24) gated on TFGame.Players[i] — counts only
     * players 0..5, no get_Item call. TeamSelector.Update uses it for the
     * `< 4` per-team cap, so P7/P8 must be added post-Orig or a team could
     * hold 5. sig: (self, int allegiance) -> int. */
    constexpr uintptr_t MatchTeams_AmountOfAllegiance = 0xabcc20;
    /* MatchTeams.GetPlayersOfAllegiance (0xabc950, DIRECT): allocates a fresh
     * List<int> (bl 0x861e0), then UNROLLED per player 0..5 does `if
     * (fieldAllegiance == arg && TFGame.Players[i]) list.Add(i)` via
     * List<int>.Add @0x89840. Returns the list. Extend post-Orig for 6/7.
     * sig: (self, int allegiance) -> List<int>*. Used by team spawning
     * (SpawnPlayersTeams @0xb390c0) + team win/results (@0xbc2a88). */
    constexpr uintptr_t MatchTeams_GetPlayersOfAllegiance = 0xabc950;
    constexpr uintptr_t List_int_Add                      = 0x89840;
    /* MainMenu.CreateTeamSelect (DIRECT, 1 caller) — builds the per-player
     * team cursors. Its loop (@0xaa23dc) runs playerIndex 0..(4|6) bound and
     * builds a TeamSelector only for joined players, via the SAME factory+add
     * chain as CreateRollcall. Post-hook adds cursors for idx 6/7. */
    constexpr uintptr_t MainMenu_CreateTeamSelect = 0xaa2200;
    /* MatchTeams.get_ProperlyAssigned (0xabc0d0): the start gate
     * (CanStartWithTeams => Teams.ProperlyAssigned). Iterates the six physical
     * fields + TFGame.Players and returns false if any joined player is
     * Neutral (also enforces 1..4 per team for the physical slots). Hooked
     * post-Orig to ALSO block start while an active P7/P8 is still Neutral,
     * so they must pick a team like players 1-6. */
    constexpr uintptr_t MatchTeams_ProperlyAssigned = 0xabc0d0;
    /* TeamSelector ctor (0xb6b7f0): (self, int playerIndex, Vector2 position,
     * Vector2 tweenFrom). Vector2 is a 2-float HFA -> position in v0/v1,
     * tweenFrom in v2/v3 (AArch64 AAPCS); playerIndex stored at self+0xb0.
     * Reads TFGame.Characters[playerIndex] (bounds-checked; widened ✓). */
    constexpr uintptr_t TeamSelector_ctor = 0xb6b7f0;
    constexpr uintptr_t Players_cell = 0x4793890;   /* TFGame.Players (bool[]) */
    /* Teams round-over diagnosis (2026-07-07): static path is clean —
     * GetPlayerAllegiance (0xabb5c0) calls get_Item (our hook handles 6/7),
     * and TeamCheckForRoundOver (0xb3a200) uses bool[2] + a cap-free live-player
     * ENUMERATOR. So P8's team should count IF its entity is in Level.Players
     * with the right Allegiance. These two hooks log the runtime truth:
     * GetPlayerAllegiance(idx>=6) result, and each round-over verdict. */
    constexpr uintptr_t MatchSettings_GetPlayerAllegiance = 0xabb5c0; /* int(ms,idx) */
    constexpr uintptr_t RoundLogic_TeamCheckForRoundOver  = 0xb3a200; /* bool(self, out Allegiance) */

    /* ---- Task A: 8-player end-of-match RESULTS screen -------------------
     * VersusMatchResults ctor (0xbaf700; args x0=this, x1=session). Retail's
     * layout/winner/creation are all hardcoded to 6 (the ==6 branch; >6
     * threw until we NOP'd the assert), so an 8p match reaches results but
     * only shows the first 6 portraits. We do NOT widen the internal loops
     * (their size-6 local Vector2[] arrays would throw — the test-28 class of
     * crash). Instead:
     *   - a POST-hook on the ctor spawns the 2 extra VersusPlayerMatchResults
     *     (players 6/7) the same way retail spawns 0..5, and
     *   - a hook on the VPMR ctor repositions ALL portraits to the PC's 4+4
     *     8-player grid by playerIndex (FFA, PlayerAmount>6 only).
     * FFA first (team results index size-2 per-score arrays; deferred). */
    constexpr uintptr_t VersusMatchResults_ctor       = 0xbaf700;
    /* VersusPlayerMatchResults ctor (0xbb23b0): args x0=this, x1=session,
     * x2=parent(VMR), w3=playerIndex, x4=awards(List<AwardInfo>), w5=small,
     * v0/v1=tweenFrom, v2/v3=tweenTo. Stores session@+0x88, parent@+0xd8,
     * playerIndex@+0x90. */
    constexpr uintptr_t VersusPlayerMatchResults_ctor = 0xbb23b0;
    /* VersusPlayerMatchResults.Render concrete body (virtual, vtable slot
     * 0x50; resolved 2026-07-24 via metaresolve builder@0x283e168 -> dispatch
     * thunk @0x283e550, body adjacent to the ctor in the class's text
     * cluster). Shape matches PC Render: base.Render() @0x92f680 then
     * gem(+0xc8).DrawOutline @0x932cd0 + tail virtual gem.Render. Hooked to
     * re-clamp award-icon scale EVERY frame: the awards sequence's scale
     * tween (PC: icon.Scale = One*lerp(1.5,1,t)) overwrites any Add-time
     * scaling, so a one-shot shrink can never stick. */
    constexpr uintptr_t VersusPlayerMatchResults_Render = 0xbb2f10;
    constexpr uintptr_t TFGame_get_PlayerAmount   = 0xb74cd0;   /* int() */
    constexpr uintptr_t MatchSettings_get_TeamMode = 0xaba740;  /* bool(settings) */
    constexpr uintptr_t Session_GetWinner   = 0xb453b0;         /* int(session) */
    constexpr uintptr_t Session_GetScoreIndex = 0xb45230;       /* int(session,idx) */
    /* new List<AwardInfo>() — the factory retail calls in GetAwards's unrolled
     * per-player init (bl @0xba9ecc, x0=null -> alloc). We can't widen that
     * unrolled 0..5 init to 8, so we mint a fresh empty list here for P7/P8;
     * VPMR requires a non-null awards list (it calls .Clear()/.Count). */
    constexpr uintptr_t NewAwardsList       = 0xbab140;         /* List<AwardInfo>*(null) */
    /* VersusAwards.AssignAward (0xbab210): per-award-category assign. Its
     * rating loop is bounded by Players.Length (already 8), but the assignment
     * loop is `cmp #6` (patched) and playerAwards[6/7] start null (the
     * per-player init is unrolled 0..5). Hooked at entry to lazily create the
     * 6/7 lists so P7/P8 can receive awards. playerAwards static cell (double
     * indirected) = 0x4797320; awards are read back from it in VMRResultsHook. */
    constexpr uintptr_t VersusAwards_AssignAward = 0xbab210;    /* void(settings,awardInfo) */
    constexpr uintptr_t PlayerAwards_cell        = 0x4797320;   /* List<AwardInfo>[] */
    constexpr uintptr_t PlayerResults_ListAdd = 0xbb0660;       /* void(list,item) */
    /* Session instance fields: MatchSettings+0x20, MatchStats+0x28 (array of
     * structs, stride 0x2a8, .Won byte at +0), CurrentLevel+0x68.
     * VersusMatchResults.playerResults List at this+0x98. */
}

/* TFGame.Players is a bool[] (1-byte elements, NOT pointers), so index its
 * element data as bytes. Returns true iff slot `idx` is a joined player. */
static bool PlayersJoined(int idx) {
    void** obj = ArrayObj(off::Players_cell);
    if (!obj) return false;
    if (idx < 0 || idx >= ArrayLenObj(obj)) return false;
    uint8_t* d = reinterpret_cast<uint8_t*>(ArrayData(obj));
    return d && d[idx] != 0;
}

/* Vector2 (2-float HFA) for by-value ctor args — passed in v-register pairs. */
struct Vec2 { float x, y; };

} /* namespace tf8 */

/* ---- Step 1: the cap hook ------------------------------------------------
 * Call Orig() FIRST: its prologue runs the lazy class-init guard
 * ([base+0x4792520]), so by the time we read the RollcallMode storage it is
 * guaranteed initialized. Modes 0-4 return exactly Orig's value. */
HOOK_DEFINE_TRAMPOLINE(MaxPlayersHook) {
    static int Callback(void* self) {
        MM_ONLINE_PASS(Orig(self));
        int retail = Orig(self);
        uint32_t* mode = tf8::RollcallModeStorage();
        /* One-time liveness proof: confirms InstallAtOffset addressing works
         * even when the new mode is never entered. */
        static bool alive = false;
        if (!alive) {
            alive = true;
            MMLOG("[8player] cap hook alive (mode=%d retail_cap=%d)",
                        mode ? (int)*mode : -1, retail);
        }
        if (mode && *mode == tf8::kVersus8Mode) {
            static bool logged = false;
            if (!logged) {
                logged = true;
                MMLOG("[8player] get_MaxPlayers: mode 5 active -> cap 8");
            }
            return tf8::kVersus8Cap;
        }
        return retail;
    }
};

/* ---- Crash diagnosis: managed-throw site logger ---------------------------
 * Every managed C# throw goes through the runtime helper @0x2861ac0 (all the
 * compiler throw stubs `bl` it). The game's level-update try/catch RETHROWS,
 * destroying the inner stack in Ryujinx's crash dump — so log the throw
 * site (module-relative LR) the moment it happens. Turns every crash into an
 * exact address instead of stack archaeology. */
HOOK_DEFINE_TRAMPOLINE(ManagedThrowHook) {
    static void Callback(void* x0, void* x1, void* x2) {
        MM_ONLINE_PASS(Orig(x0, x1, x2));
        uintptr_t base = exl::util::GetMainModuleInfo().m_Total.m_Start;
        uintptr_t lr = reinterpret_cast<uintptr_t>(__builtin_return_address(0));
        MMLOG("[8player] managed throw from +0x%lx",
                    static_cast<unsigned long>(lr - base));
        Orig(x0, x1, x2);
    }
};

/* ---- MatchTeams for players 7-8 ------------------------------------------
 * The Switch MatchTeams class STRUCTURALLY lacks player7Team/player8Team
 * (six Allegiance fields @+0x10..+0x24; PC 8p has eight). Its indexer throws
 * "Index out of bounds!" for idx 6/7 — the teams-mode crash at match start
 * once the spawn loops were widened (they call get_Item(6)). Fix: serve idx
 * 6/7 from a mod-side store, reset every rollcall. P7/P8 have their own
 * TeamSelect cursors (task C), so they start UNASSIGNED (Neutral) and pick a
 * team just like players 1-6 — no forced assignment.
 * Allegiance: Neutral=-1, Blue=0, Red=1. */
static int g_teamSide[2] = { -1, -1 };

HOOK_DEFINE_TRAMPOLINE(TeamsGetItemHook) {
    static int Callback(void* self, int idx) {
        /* online too (no MM_ONLINE_PASS in these four MatchTeams hooks): the PC's MatchTeams serves seats past its fields wherever
         * WiderSet is loaded (its get_Item / set_Item finalizers), and TF.EX's ApplyTeamsToMatchSettings sets every seat of
         * TFGame.Players (8 on the Switch): without them every team lobby threw in set_Item(6) (smoke wideteam5 110955) */
        if (idx == 6 || idx == 7) return g_teamSide[idx - 6];
        return Orig(self, idx);
    }
};

/* ---- TEAM ASSIGNMENT for P7/P8 (task C) ----------------------------------
 * The physical MatchTeams has only six Allegiance fields, so the retail
 * set_Item throws for idx 6/7. Serve those two indices from the same
 * g_teamSide store the get_Item hook reads — this is what makes a P7/P8
 * TeamSelector's left/right actually stick instead of crashing. */
HOOK_DEFINE_TRAMPOLINE(TeamsSetItemHook) {
    static void Callback(void* self, int idx, int value) {
        if (idx == 6 || idx == 7) { g_teamSide[idx - 6] = value; return; }
        Orig(self, idx, value);
    }
};

/* ---- Team-roster queries for P7/P8 (WiderSet parity) ---------------------
 * GetPlayersOfAllegiance / GetAmountOfPlayersOfAllegiance are UNROLLED over the
 * six physical Allegiance fields, so they ignore P7/P8. Team-mode SPAWNING
 * (SpawnPlayersTeams distributes players across TeamSpawnA/B using these) and
 * team win/results depend on them, so without extending, P7/P8 aren't placed
 * or counted on their team. These post-hooks add idx 6/7 when joined and their
 * mod-side allegiance (g_teamSide) matches the queried one — served from the
 * same store the get_Item/set_Item hooks use.
 * Naturally inert outside our 8-lobby (PlayersJoined(6/7) is false in 4/6p).
 * NOTE: these do NOT impose a team-size cap. The one TeamSelector.Update site
 * that calls GetAmountOfPlayersOfAllegiance (@0xb6bb14, `cmp #6`) counts the
 * player's OWN current team to lay out its cursor — it is not a join gate.
 * Team composition is validated only at the start gate (ProperlyAssignedHook
 * below), and our team levels carry 8 TeamSpawn points per side, so any split
 * spawns cleanly. */
HOOK_DEFINE_TRAMPOLINE(GetPlayersOfAllegianceHook) {
    static void* Callback(void* self, int allegiance) {
        void* list = Orig(self, allegiance);
        if (list)
            for (int idx = 6; idx < 8; idx++)
                if (tf8::PlayersJoined(idx) && g_teamSide[idx - 6] == allegiance)
                    tf8::Call<void (*)(void*, int)>(tf8::off::List_int_Add)(list, idx);
        return list;
    }
};
/* MatchSettings.GetPlayerTeamSize (0xabb910): unrolled over seats 0-5 (WiderSet's loop is 8). Session.GetSpawnArrows indexes
 * TeamStartArrows with it: a team of 6 counted as 4 started with 3 arrows on the Switch, 2 on the PC (wide8end 134226). Seats 6-7
 * added when joined and on the asking player's team; online too (inert with 6 seats or fewer). */
HOOK_DEFINE_TRAMPOLINE(PlayerTeamSizeHook) {
    static int Callback(void* self, int idx) {
        int n = Orig(self, idx);
        if (!self || !(tf8::PlayersJoined(6) || tf8::PlayersJoined(7))) return n;
        if (!(tf8::Call<bool (*)(void*)>(tf8::off::MatchSettings_get_TeamMode)(self) & 1)) return n;
        void* teams = *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(self) + 0x38);   /* MatchSettings.Teams (GetPlayerTeamSize's own read) */
        if (!teams) return n;
        int mine = tf8::Call<int (*)(void*, int)>(tf8::off::MatchTeams_get_Item)(teams, idx);
        for (int i = 6; i < 8; i++)
            if (i != idx && tf8::PlayersJoined(i) && tf8::Call<int (*)(void*, int)>(tf8::off::MatchTeams_get_Item)(teams, i) == mine) n++;
        return n;
    }
};
HOOK_DEFINE_TRAMPOLINE(AmountOfAllegianceHook) {
    static int Callback(void* self, int allegiance) {
        int n = Orig(self, allegiance);
        for (int idx = 6; idx < 8; idx++)
            if (tf8::PlayersJoined(idx) && g_teamSide[idx - 6] == allegiance)
                n++;
        return n;
    }
};

/* TFGame.CharacterTaken (0xb76630): unrolled over seats 0-5 (WiderSet's transpiler makes the PC's loop 8,
 * TFGameHooks EightPlayerTranspilerNoCondition). P7/P8's archers counted as free: the roll call let a player
 * or an AI (RollcallElement.EnforceCharacterLock / ChangeSelectionRight, the AI's own check) take P7's archer
 * for P8 (the user, 2026-11-08: every time with L's AI fill). Seats 6-7 added; online too (WiderSet's on the PC). */
HOOK_DEFINE_TRAMPOLINE(CharacterTakenHook) {
    static bool Callback(int ch) {
        if (Orig(ch)) return true;
        void** cobj = tf8::ArrayObj(0x4792458);   /* TFGame.Characters int[] */
        if (!cobj || tf8::ArrayLenObj(cobj) < 8) return false;
        const int32_t* cd = reinterpret_cast<const int32_t*>(tf8::ArrayData(cobj));
        for (int i = 6; i < 8; i++)
            if (cd && tf8::PlayersJoined(i) && cd[i] == ch) return true;
        return false;
    }
};

/* Start gate for team mode. In OUR 8-lobby (mode 1) we replace retail's verdict
 * with an explicit all-8-aware rule so it exactly matches what the user wants:
 *   - every JOINED player must have picked a team (not Neutral), AND
 *   - BOTH teams must have at least one player.
 * That's it — there is NO per-team maximum, so any split (1v7 … 7v1) is legal;
 * only "everyone on one team" (8v0 / 0v8) is blocked. Safe because our team
 * levels carry 8 TeamSpawn points per side, so a full-8 team still spawns.
 * get_Item is hooked to serve idx 6/7 from g_teamSide, so this counts all 8.
 * Retail modes (mode != 1, e.g. 4-player) fall through to Orig untouched.
 * Allegiance: Neutral = -1, Blue = 0, Red = 1. */
/* A shell around the generated hook on ProperlyAssigned (one trampoline: Teams+'s RulesHooks prefix is on the same method, tfex/gen
 * hooks): Teams+'s rule first, when its match is set up (its prefix skips the game's getter); otherwise the game's, then this rule in
 * our 8-lobby. */
static void ProperlyAssignedShell(tfex::gen::hooks::MatchTeams_get_ProperlyAssigned& c) {
    c();
    if (::megamod::Online() || !c.ran_) return;   /* online: the game's; Teams+ decided: its verdict */
    uint32_t* mode = tf8::RollcallModeStorage();
    if (!mode || *mode != 1) return;
    int blue = 0, red = 0;
    for (int i = 0; i < 8; i++) {
        if (!tf8::PlayersJoined(i)) continue;
        int a = tf8::Call<int (*)(void*, int)>(tf8::off::MatchTeams_get_Item)(c.self, i);
        if (a == 0)      blue++;
        else if (a == 1) red++;
        else { c.result_ = false; return; }   /* joined but still Neutral */
    }
    c.result_ = blue >= 1 && red >= 1;
}

/* ---- TeamSelect cursors for P7/P8 ----------------------------------------
 * CreateTeamSelect's loop builds a TeamSelector only for joined players up to
 * its 4/6 bound, so pads 7/8 get no team cursor. Post-hook: after Orig builds
 * 0..5, replicate the exact ctor + scene-add chain for indices 6/7 (when
 * joined). tweenFrom.X = -40 (even idx) / 460 (odd) for a side slide-in.
 * Rows: retail's 6-seat select starts at Y=105, 20 px a row (+0xaa24bc
 * `add w26,w26,#20`): P7/P8 at 225/245 were off the 240-px screen (the user,
 * 2026-11-08). WiderSet's own CreateTeamSelect transpiler makes the step
 * 20 - 5 when wide; here the same 15 for this call (the word put back after:
 * the 4-seat select is the game's), and P7/P8 continue the grid (195, 210). */
constexpr uintptr_t kTeamSelectRowStep = 0xaa24bc;
constexpr uint32_t kRowStep20 = 0x1100535a /* add w26,w26,#20 */, kRowStep15 = 0x11003f5a /* add w26,w26,#15 */;
constexpr float kTeamRowY = 105.0f, kTeamRowStep = 15.0f;
static bool SetTeamRowStep(uint32_t word) {
    uint32_t cur = *reinterpret_cast<const uint32_t*>(exl::util::GetMainModuleInfo().m_Total.m_Start + kTeamSelectRowStep);
    if (cur != kRowStep20 && cur != kRowStep15) {
        MMLOG("[8player] ERROR: team select row step @+%#lx: found %08x, expected %08x: left alone", (unsigned long)kTeamSelectRowStep, cur, kRowStep20);
        return false;
    }
    if (cur != word) exl::patch::CodePatcher(kTeamSelectRowStep).Write(word);
    return true;
}
/* A shell around the generated hook on CreateTeamSelect (one trampoline: Teams+'s MenuHooks prefix builds its own four-column select
 * there and skips the game's): P7/P8's cursors only when the game's select was built. */
static void CreateTeamSelectShell(tfex::gen::hooks::MainMenu_CreateTeamSelect& c) {
    uint32_t* mode = tf8::RollcallModeStorage();
    const bool eight = !::megamod::Online() && mode && *mode == 1;   /* dev: 8-cap rides mode 1 */
    const bool step = eight && SetTeamRowStep(kRowStep15);
    c();
    if (step) SetTeamRowStep(kRowStep20);
    if (!eight || !c.ran_) return;
    void* self = c.self;
    int added = 0;
    for (int idx = 6; idx < 8; idx++) {
        if (!tf8::PlayersJoined(idx)) continue;
        float y = step ? kTeamRowY + kTeamRowStep * idx : 105.0f + 20.0f * idx;
        tf8::Vec2 pos{ 160.0f, y };
        tf8::Vec2 tw{ (idx % 2 == 0) ? -40.0f : 460.0f, y - 10.0f };
        void* el    = tf8::Call<void* (*)(void*, int, tf8::Vec2, tf8::Vec2)>(
                          tf8::off::TeamSelector_ctor)(nullptr, idx, pos, tw);
        void* scene = tf8::Call<void* (*)(void*)>(tf8::off::MenuScene)(self);
        int   li    = tf8::Call<int (*)(void*)>(tf8::off::ElementLayerIndex)(el);
        void* layer = tf8::Call<void* (*)(void*, int)>(tf8::off::SceneGetLayer)(scene, li);
        tf8::Call<void (*)(void*, void*, int)>(tf8::off::SceneLayerAdd)(layer, el, 0);
        added++;
    }
    static int logged = 0;
    if (logged++ < 4) MMLOG("[8player] team select: rows %d px apart, %d cursor(s) for P7/P8 added", step ? 15 : 20, added);
}

/* ---- Variant per-player values for P7/P8 ----------------------------------
 * See the off:: block for the method map. Helpers read/write the Variant's
 * playerValues bool[] (this+0x60, length 8 after the alloc widen). All four
 * hooks no-op cleanly if the array is missing or still length 6. */
namespace tf8 {
static inline uint8_t* VariantPV(void* variant, int* lenOut) {
    if (!variant) return nullptr;
    void** arr = *reinterpret_cast<void***>(
        reinterpret_cast<uint8_t*>(variant) + off::Variant_playerValues);
    if (!arr) return nullptr;
    if (lenOut) *lenOut = ArrayLenObj(arr);
    return reinterpret_cast<uint8_t*>(ArrayData(arr));
}
} /* namespace tf8 */

HOOK_DEFINE_TRAMPOLINE(VariantGetValueHook) {
    static bool Callback(void* self) {
        MM_ONLINE_PASS(Orig(self));
        bool r = Orig(self);
        if (r) return r;
        int len = 0; uint8_t* pv = tf8::VariantPV(self, &len);
        return pv && len >= 8 && (pv[6] || pv[7]);
    }
};

HOOK_DEFINE_TRAMPOLINE(VariantSetValueHook) {
    static void Callback(void* self, bool value) {
        MM_ONLINE_PASS(Orig(self, value));
        Orig(self, value);
        int len = 0; uint8_t* pv = tf8::VariantPV(self, &len);
        if (pv && len >= 8) { pv[6] = value ? 1 : 0; pv[7] = value ? 1 : 0; }
    }
};

HOOK_DEFINE_TRAMPOLINE(VariantAllTrueHook) {
    static bool Callback(void* self) {
        MM_ONLINE_PASS(Orig(self));
        bool r = Orig(self);
        if (!r) return r;
        int len = 0; uint8_t* pv = tf8::VariantPV(self, &len);
        if (!pv || len < 8) return r;
        for (int idx = 6; idx < 8; idx++)
            if (tf8::PlayersJoined(idx) && !pv[idx]) return false;
        return r;
    }
};

HOOK_DEFINE_TRAMPOLINE(VariantCleanHook) {
    static void Callback(void* self) {
        MM_ONLINE_PASS(Orig(self));
        Orig(self);
        int len = 0; uint8_t* pv = tf8::VariantPV(self, &len);
        if (pv && len >= 8)
            for (int idx = 6; idx < 8; idx++)
                if (!tf8::PlayersJoined(idx)) pv[idx] = 0;
    }
};

/* ---- "Press + to begin" banner in front of the portraits -------------------
 * FightButton is built before the slots, and our 4+4 grid overlaps it. Give it
 * a depth in front of the slots' -8..-1 so the prompt reads over the portraits.
 * Gated on the 8-player lobby (mode 1); other modes keep retail layering. */
HOOK_DEFINE_TRAMPOLINE(FightButtonFrontHook) {
    static void* Callback(void* self) {
        MM_ONLINE_PASS(Orig(self));
        void* obj = Orig(self);
        uint32_t* mode = tf8::RollcallModeStorage();
        if (obj && mode && *mode == 1)
            tf8::Call<void (*)(void*, int)>(tf8::off::Entity_set_Depth)(
                obj, tf8::off::kFightButtonDepth);
        return obj;
    }
};

/* ---- Complete the join-state reset for slots 7/8 ---------------------------
 * The three mode-start confirm handlers clear TFGame.Players[0..5] unrolled;
 * clear 6/7 too so P7/P8 joins can't leak into Quest/co-op as phantom players. */
namespace tf8 {
static inline void ClearP78Join() {
    void** obj = ArrayObj(off::Players_cell);
    if (!obj || ArrayLenObj(obj) < 8) return;
    uint8_t* d = reinterpret_cast<uint8_t*>(ArrayData(obj));
    if (d) { d[6] = 0; d[7] = 0; }
}
} /* namespace tf8 */

HOOK_DEFINE_TRAMPOLINE(ResetJoinAHook) {
    static void* Callback(void* a, void* b, void* c, void* d) {
        MM_ONLINE_PASS(Orig(a, b, c, d));
        void* r = Orig(a, b, c, d); tf8::ClearP78Join(); return r;
    }
};
HOOK_DEFINE_TRAMPOLINE(ResetJoinBHook) {
    static void* Callback(void* a, void* b, void* c, void* d) {
        MM_ONLINE_PASS(Orig(a, b, c, d));
        void* r = Orig(a, b, c, d); tf8::ClearP78Join(); return r;
    }
};
HOOK_DEFINE_TRAMPOLINE(ResetJoinCHook) {
    static void* Callback(void* a, void* b, void* c, void* d) {
        MM_ONLINE_PASS(Orig(a, b, c, d));
        void* r = Orig(a, b, c, d); tf8::ClearP78Join(); return r;
    }
};

/* ---- VariantPerPlayer: columns 7/8 on the per-player toggle screen --------
 * Post the ctor: create wigglers 6/7 exactly as retail's unrolled blocks do,
 * and swap this+0xe8 to an 8-entry positions array (static fake managed array;
 * entries 0-5 copied from retail's shared static, 6/7 extrapolated with the
 * same column spacing). Only for the six-variant screen (+0xe0). */
namespace tf8 {
struct FakeVec2Array {
    void*    klass;   /* +0x00: copied from the retail array object   */
    uint64_t pad0;    /* +0x08                                        */
    void*    data;    /* +0x10: -> vecs                               */
    uint64_t pad1;    /* +0x18                                        */
    int32_t* lenPtr;  /* +0x20: -> len                                */
    int32_t  len;
    float    vecs[16];                       /* 8 x Vector2 */
};
inline FakeVec2Array g_vppPositions8 = {};
} /* namespace tf8 */

HOOK_DEFINE_TRAMPOLINE(VariantPerPlayerCtorHook) {
    static void* Callback(void* self, void* x1, void* x2, void* x3,
                          float f0, float f1, float f2, float f3) {
        MM_ONLINE_PASS(Orig(self, x1, x2, x3, f0, f1, f2, f3));
        void* obj = Orig(self, x1, x2, x3, f0, f1, f2, f3);
        if (!obj) return obj;
        uint8_t* o = reinterpret_cast<uint8_t*>(obj);
        if (!*(o + tf8::off::VPP_SixFlag)) return obj;   /* 4-player screen: leave */
        /* positions -> 8 entries */
        void** pos = *reinterpret_cast<void***>(o + tf8::off::VPP_Positions);
        if (pos && tf8::ArrayLenObj(pos) == 6) {
            auto& fk = tf8::g_vppPositions8;
            if (!fk.data) {                              /* one-time build */
                float* src = reinterpret_cast<float*>(tf8::ArrayData(pos));
                if (src) {
                    fk.klass  = *reinterpret_cast<void**>(pos);
                    fk.data   = fk.vecs;
                    fk.lenPtr = &fk.len;
                    fk.len    = 8;
                    /* 8 columns COMPRESSED into the retail 6-column span (same
                     * center, spacing * 6/7 -> total width 6 retail steps, half
                     * a step past the old P1/P6 on each side: near the panel
                     * edges with more room between icons than 5/7 gave). */
                    float cx = (src[0] + src[10]) * 0.5f, cy = (src[1] + src[11]) * 0.5f;
                    float sx = (src[10] - src[8]) * (6.0f / 7.0f);
                    float sy = (src[11] - src[9]) * (6.0f / 7.0f);
                    for (int i = 0; i < 8; i++) {
                        fk.vecs[i*2]   = cx + (i - 3.5f) * sx;
                        fk.vecs[i*2+1] = cy + (i - 3.5f) * sy;
                    }
                }
            }
            if (fk.data)
                *reinterpret_cast<void**>(o + tf8::off::VPP_Positions) = &fk;
        }
        /* head-icon static -> ref[8], once (Render indexes it per column; this
         * was the crash when the column loop first ran past 6). Allocated with
         * the game's newarr + the cctor's own type cell, so the GC owns it. */
        {
            uintptr_t base = exl::util::GetMainModuleInfo().m_Total.m_Start;
            void** hstorage = *reinterpret_cast<void***>(base + tf8::off::VPP_Heads_cell);
            if (hstorage && *hstorage) {
                void** harr = reinterpret_cast<void**>(*hstorage);
                if (tf8::ArrayLenObj(harr) == 6) {
                    void** tstorage = *reinterpret_cast<void***>(base + tf8::off::SubtexArrType_cell);
                    if (tstorage && *tstorage) {
                        int cnt = 8;
                        void** neu = tf8::Call<void** (*)(void*, int, int*, int)>(
                                         tf8::off::NewArr)(*tstorage, 8, &cnt, 1);
                        void** od = tf8::ArrayData(harr);
                        void** nd = tf8::ArrayData(neu);
                        if (od && nd) {
                            for (int i = 0; i < 6; i++) nd[i] = od[i];
                            /* The "icons" are interned STRINGS ("P1".."P6", cctor
                             * @0x8ee7f0 interns via 0x12670(utf16*,len) — copies
                             * from any pointer). Make real "P7"/"P8" labels; the
                             * per-column COLOR comes from a size-8 table already. */
                            static const uint16_t kP7[] = { u'P', u'7' };
                            static const uint16_t kP8[] = { u'P', u'8' };
                            void* s7 = tf8::Call<void* (*)(const void*, int)>(0x12670)(kP7, 2);
                            void* s8 = tf8::Call<void* (*)(const void*, int)>(0x12670)(kP8, 2);
                            nd[6] = s7 ? s7 : od[0];
                            nd[7] = s8 ? s8 : od[1];
                            *hstorage = neu;
                            MMLOG("[8player] variant-menu P7/P8 labels widened to 8");
                        }
                    }
                }
            }
        }
        /* wigglers 6/7 (Render calls Wiggler.get_Value with NO null check) */
        void** warr = *reinterpret_cast<void***>(o + tf8::off::VPP_Wigglers);
        if (warr && tf8::ArrayLenObj(warr) >= 8) {
            void** wd = tf8::ArrayData(warr);
            if (wd)
                for (int idx = 6; idx < 8; idx++) {
                    if (wd[idx]) continue;
                    void* w = tf8::Call<void* (*)(int, void*, void*, int, int, float)>(
                                  tf8::off::Wiggler_New)(20, nullptr, nullptr, 0, 0, 5.0f);
                    if (!w) continue;
                    wd[idx] = w;
                    tf8::Call<void (*)(void*, void*)>(tf8::off::Entity_AddComponent)(obj, w);
                }
        }
        return obj;
    }
};

/* ---- Kill-replay: InputRenderers for P7/P8 --------------------------------
 * Post-hook the viewer ctor: retail's unrolled 0..5 creation is done, the
 * renderers array is size 8 (kArrayWidens), so replicate the per-index rule
 * for 6/7: `if (Players[idx]) { r = new InputRenderer(idx, y); y += Height }`
 * where y resumes from the stacked heights of the renderers retail created. */
HOOK_DEFINE_TRAMPOLINE(ReplayViewerCtorHook) {
    static void* Callback(void* self, void* x1, void* x2, void* x3) {
        MM_ONLINE_PASS(Orig(self, x1, x2, x3));
        void* obj = Orig(self, x1, x2, x3);
        if (!obj) return obj;
        void** arr = *reinterpret_cast<void***>(reinterpret_cast<uint8_t*>(obj) + tf8::off::Viewer_Renderers);
        if (!arr || tf8::ArrayLenObj(arr) < 8) return obj;   /* alloc patch didn't take */
        void** data = tf8::ArrayData(arr);
        if (!data) return obj;
        float y = 0.0f;
        for (int i = 0; i < 6; i++)
            if (data[i])
                y += static_cast<float>(*reinterpret_cast<int32_t*>(
                         reinterpret_cast<uint8_t*>(data[i]) + tf8::off::Renderer_Height));
        int made = 0;
        for (int idx = 6; idx < 8; idx++) {
            if (!tf8::PlayersJoined(idx) || data[idx]) continue;
            void* r = tf8::Call<void* (*)(void*, int, float)>(tf8::off::InputRenderer_New)(nullptr, idx, y);
            if (!r) continue;
            data[idx] = r;
            y += static_cast<float>(*reinterpret_cast<int32_t*>(
                     reinterpret_cast<uint8_t*>(r) + tf8::off::Renderer_Height));
            made++;
        }
        if (made) MMLOG("[8player] replay viewer: %d InputRenderer(s) added for P7/P8", made);
        return obj;
    }
};

/* Post-hook the replay render pass: retail's six unrolled blocks have run
 * (verified: no early return/branch skips them), so replicate blocks 6/7 —
 * `if (renderers[idx]) renderers[idx].Render(&frames[frameIdx].states[idx])`.
 * Every access is re-validated (array lengths, frame index) so frames recorded
 * without P7/P8 data, or a viewer without our renderers, just no-op. */
HOOK_DEFINE_TRAMPOLINE(ReplayRenderHook) {
    static void Callback(void* self, void* x1, void* x2, void* x3) {
        MM_ONLINE_PASS(Orig(self, x1, x2, x3));
        Orig(self, x1, x2, x3);
        if (!self) return;
        uint8_t* v = reinterpret_cast<uint8_t*>(self);
        void** arr = *reinterpret_cast<void***>(v + tf8::off::Viewer_Renderers);
        if (!arr || tf8::ArrayLenObj(arr) < 8) return;
        void** rdata = tf8::ArrayData(arr);
        if (!rdata) return;
        void* frames = *reinterpret_cast<void**>(v + tf8::off::Viewer_Frames);
        if (!frames) return;
        void** items = *reinterpret_cast<void***>(reinterpret_cast<uint8_t*>(frames) + 0x10);
        int fi = *reinterpret_cast<int32_t*>(v + tf8::off::Viewer_FrameIdx);
        if (!items || fi < 0 || fi >= tf8::ArrayLenObj(items)) return;
        void** idata = tf8::ArrayData(items);
        if (!idata) return;
        void* frame = idata[fi];
        if (!frame) return;
        void** states = *reinterpret_cast<void***>(reinterpret_cast<uint8_t*>(frame) + tf8::off::Frame_States);
        if (!states || tf8::ArrayLenObj(states) < 8) return;   /* pre-widen frame */
        uint8_t* sdata = reinterpret_cast<uint8_t*>(tf8::ArrayData(states));
        if (!sdata) return;
        for (int idx = 6; idx < 8; idx++) {
            if (!rdata[idx]) continue;
            uint64_t buf[4];                                    /* 0x1c-byte InputState copy */
            __builtin_memcpy(buf, sdata + idx * 0x1c, 0x1c);
            tf8::Call<void (*)(void*, void*)>(tf8::off::InputRenderer_Render)(rdata[idx], buf);
        }
    }
};

/* Teams round-over counting: VERIFIED CORRECT 2026-07-07 (test 28) — both
 * statically (cap-free path) and at runtime: GetPlayerAllegiance(6)=0 Blue,
 * (7)=1 Red; TeamCheckForRoundOver correctly returned over=1 surviving=0
 * (Blue) when Red was wiped. The earlier "P8's team lost while alive" was the
 * FORCED assignment (P8 always Red), not a miscount. Diagnostics removed. */

/* ---- Inputs for pads 7-8 -------------------------------------------------
 * Post-hook AssignInputs: after Orig fills slots 0-5 (unrolled), replicate
 * the same per-slot logic for 6/7. Restores the maintenance-loop invariant
 * "JoyCons[i] connected => PlayerInputs[i] is JoyConInput" for the widened
 * range. */
HOOK_DEFINE_TRAMPOLINE(AssignInputsHook) {
    static void Callback(void* x0, void* x1) {
        /* online too: a device seating 7-8 players (TF.EX's local players, a wide lobby) needs pads 7-8's inputs (inert without them) */
        Orig(x0, x1);
        void** joyArr = tf8::ArrayObj(tf8::off::JoyCons_cell);
        void** piArr  = tf8::ArrayObj(tf8::off::PlayerInputs_cell);
        if (!joyArr || !piArr) return;
        int jlen = tf8::ArrayLenObj(joyArr), plen = tf8::ArrayLenObj(piArr);
        void** jd = tf8::ArrayData(joyArr);
        void** pd = tf8::ArrayData(piArr);
        if (!jd || !pd) return;
        int made = 0;
        /* the controller icons for players 7-8 (a JoyConInput looks its style's up: a missing one throws): the game has players 1-6;
         * missing ones are the same style's 5-6 (glue SwitchMods.AliasMissingSubtexture; no game art shipped) */
        static bool iconsDone = false;
        if (!iconsDone && ((jlen > 6 && jd[6]) || (jlen > 7 && jd[7]))) {
            iconsDone = true;
            static const char* const kStyles[] = { "nsdual", "nsjoycon", "nspro" };
            int ok = 0;
            for (const char* st : kStyles)
                for (int p = 7; p <= 8; p++) {
                    char id[48], from[48];
                    int ni = std::snprintf(id, sizeof id, "controls/%s/player%d", st, p), nf = std::snprintf(from, sizeof from, "controls/%s/player%d", st, p - 2);
                    ok += tfex::gen::GlueFrMods::AliasMissingSubtexture(tfex::gen::ManagedUtf8(id, ni), tfex::gen::ManagedUtf8(from, nf), 2) ? 1 : 0;
                }
            MMLOG("[8player] controller icons for players 7-8: %d of 6 there (missing ones are players 5-6's)", ok);
        }
        for (int idx = 6; idx < 8 && idx < jlen && idx < plen; idx++) {
            if (!jd[idx]) continue;                     /* no pad -> stay null */
            if (pd[idx]) continue;                      /* already assigned    */
            pd[idx] = tf8::Call<void* (*)(void*, int)>(tf8::off::JoyConInput_New)(nullptr, idx);
            made++;
        }
        if (made)
            MMLOG("[8player] AssignInputs: created %d JoyConInput(s) for slots 7/8", made);
    }
};

/* ---- Rollcall: WiderSet's two rows of four ----------------------------------
 * (was a 4+4 staggered layout of full-height portraits: they overlapped; the user asked for WiderSet's, 2026-11-06)
 * Hook the slot factory so ALL slots (retail's 0-5 and our 6-7) are
 * repositioned by index: two rows of four, bottom row offset half a slot so
 * it sits under the gaps of the top row. Row geometry derived from retail's
 * single six-row (x = 68i - 12, y = 100, mid 158). Fields: Position
 * +0x14/+0x18, tweenTo +0xd8/+0xdc, tweenFrom +0xe0/+0xe4 (slide in from the
 * side the slot sits on). Gated on the six-variant flag + mode 1 (dev cap
 * path; ships gated on mode 5). */
/* Online, a wide TF.EX lobby with more seats than the game's 6 (WiderSet's on the PC: up to TFGame.Players.Length): its seat count,
 * else 0. The roll call's slots 7-8 and their two-row layout follow it (the game's own layout for 6 seats or fewer). */
static int OnlineSeatsPastSix() {
    if (!::megamod::Online()) return 0;
    uint32_t* mode = tf8::RollcallModeStorage();
    if (!mode || *mode != 1) return 0;   /* the wide (6-slot) roll call */
    tfex::gen::ws::Lobby* lobby = tfex::gen::svc::GetOwnLobby();
    int seats = lobby ? lobby->MaxPlayers : 0, cap = tf8::ArrayLen(0x4793890);   /* TFGame.Players */
    if (seats > cap) seats = cap;
    return seats > 6 ? seats : 0;
}

HOOK_DEFINE_TRAMPOLINE(RollcallSlotFactoryHook) {
    static void* Callback(void* self, int index, int sixVariant) {
        if (::megamod::Online() && !OnlineSeatsPastSix()) return Orig(self, index, sixVariant);   /* online: the game's, but past 6 seats */
        void* el = Orig(self, index, sixVariant);
        if (!el || !sixVariant) return el;
        uint32_t* mode = tf8::RollcallModeStorage();
        if (!mode || *mode != 1) return el;
        /* WiderSet's grid (its RollcallElement.GetPosition / GetTweenSource prefixes, WiderSet 0.9.0): two rows of four, x 55 + 70 * col,
         * y 75 / 165; the top row slides in from the left, the bottom row from the right. Fits with WiderSet's 60 x 60 portraits
         * (PortraitRenderHook below). */
        int row = index / 4, col = index % 4;
        float x = 55.0f + 70.0f * col;
        float y = row ? 165.0f : 75.0f;
        uint8_t* f = reinterpret_cast<uint8_t*>(el);
        /* WiderSet's ctor transpiler: the archer arrows (+0xa8 / +0xb0, Y 60 in the constructor) at 30, the small portrait's bottom */
        for (uint32_t a : {0xa8u, 0xb0u})
            if (void* arrow = *reinterpret_cast<void**>(f + a)) *reinterpret_cast<float*>(reinterpret_cast<uint8_t*>(arrow) + 0x28) = 30.0f;
        *reinterpret_cast<float*>(f + 0x14) = x;
        *reinterpret_cast<float*>(f + 0x18) = y;
        *reinterpret_cast<float*>(f + 0xd8) = x;
        *reinterpret_cast<float*>(f + 0xdc) = y;
        *reinterpret_cast<float*>(f + 0xe0) = x + (row ? 420.0f : -420.0f);
        *reinterpret_cast<float*>(f + 0xe4) = y;
        /* Z-order so each slot's bottom-right stays visible: top-left frontmost,
         * bottom-right backmost (front->back = index 0..7). With LOWER depth =
         * front (Monocle), depth = index - 8 keeps every slot NEGATIVE (i.e. in
         * front of the depth-0 "FIGHT" title + background, as before) while
         * ordering them amongst themselves: top row draws over the bottom row,
         * and within a row the left slot draws over the one to its right. */
        tf8::Call<void (*)(void*, int)>(tf8::off::Entity_set_Depth)(el, index - 8);
        return el;
    }
};

/* The 8-slot roll call (local WIDE with WiderSet's folder, or an online wide lobby past 6 seats): WiderSet's look on it */
static bool EightRollcall() {
    uint32_t* mode = tf8::RollcallModeStorage();
    if (!mode || *mode != 1) return false;
    return !::megamod::Online() || OnlineSeatsPastSix() > 0;
}
/* WiderSet's ArcherPortrait transpilers (ctor, SetCharacter, StartJoined, Leave): on the 8-slot roll call a portrait is the top 60 x 60
 * of its 60 x 120 art, from y 10 (a not-joined archer 6 from y 60: WiderSet's NotJoinedCharacterOffset[6] = (0, 50)). The game sets
 * an image's ClipRect (+0x58: X Y W H in the atlas) to the whole art whenever it changes it; this crops it again before drawing. The
 * portrait (+0x68) and its flip side (+0x70). */
static void CropPortraitImage(void* image, int yFrom) {
    if (!image) return;
    int32_t* r = reinterpret_cast<int32_t*>(reinterpret_cast<uint8_t*>(image) + 0x58);
    if (r[3] <= 60) return;   /* cropped already */
    r[1] += yFrom; r[2] = 60; r[3] = 60;
    tf8::Call<void (*)(void*)>(tfex::gen::bind::m_GraphicsComponent_CenterOrigin)(image);
}
HOOK_DEFINE_TRAMPOLINE(PortraitRenderHook) {
    static void Callback(void* self) {
        if (self && EightRollcall()) {
            uint8_t* p = reinterpret_cast<uint8_t*>(self);
            bool joined = p[0x64] != 0;   /* ArcherPortrait.joined */
            p[0x58] = 0;   /* ShowTitle: no archer name over the small portrait (WiderSet's Render prefix) */
            int ch = tf8::Call<int (*)(void*)>(0x96eed0)(self);   /* get_CharacterIndex */
            int alt = tf8::Call<int (*)(void*)>(0x96eee0)(self);  /* get_AltSelect: 0 normal */
            CropPortraitImage(*reinterpret_cast<void**>(p + 0x68), (!joined && alt == 0 && ch == 6) ? 60 : 10);
            CropPortraitImage(*reinterpret_cast<void**>(p + 0x70), 10);
            CropPortraitImage(*reinterpret_cast<void**>(p + 0x78), 10);   /* the join flash (a 60 x 120 Sprite: WiderSet's ctor 120 -> 60) */
            static bool said = false;
            if (!said) { said = true; MMLOG("[8player] roll call portraits: WiderSet's 60 x 60 crop"); }
        }
        Orig(self);
    }
};
/* WiderSet's ArcherPortrait.InitGem transpiler: the gem at the 60 x 60 portrait's bottom edge (the game's +60 -> +30) */
HOOK_DEFINE_TRAMPOLINE(PortraitInitGemHook) {
    static void Callback(void* self) {
        Orig(self);
        if (!self || !EightRollcall()) return;
        void* gem = *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(self) + 0x80);   /* ArcherPortrait's gem sprite */
        if (gem) *reinterpret_cast<float*>(reinterpret_cast<uint8_t*>(gem) + 0x28) -= 30.0f;   /* its Position.Y */
    }
};
/* WiderSet's HandleControlIcons / Update transpilers: RollcallElement.ControlIconPos - (15, 120) on the 8-slot roll call (the Switch
 * reads it only in RollcallElement.Render; its static (0, 90), GOT slot +0x4795ec0, made by the class constructor) */
static void SetControlIconPos(bool eight) {
    uintptr_t base = exl::util::GetMainModuleInfo().m_Total.m_Start;
    float* pos = *reinterpret_cast<float**>(base + 0x4795ec0);
    if (!pos) return;
    pos[0] = eight ? -15.0f : 0.0f;
    pos[1] = eight ? -30.0f : 90.0f;   /* WiderSet's: on the portrait's top edge (RollcallIconsHook draws them in front of it) */
    /* the Switch's own costume prompt (R / ?: no PC counterpart), its static (22, 45) at GOT +0x4795ed0, read in Render: up 30 with the
     * arrows and the gem, inside the 60 x 60 portrait */
    float* alt = *reinterpret_cast<float**>(base + 0x4795ed0);
    if (alt) alt[1] = eight ? 15.0f : 45.0f;
}
/* The 8-slot roll call's input icon, its name and the GameCube badge in front of the portraits. The Switch's RollcallElement.Render draws
 * them (at Position + ControlIconPos + the shake +0x108) before base.Render draws the portrait, which on the small grid covers them
 * (the user, 2026-11-07). Here the game's draw goes off screen (ControlIconPos moved for the call) and the same three draws follow
 * base.Render, as the game makes them. */
HOOK_DEFINE_TRAMPOLINE(RollcallIconsHook) {
    static void Callback(void* self) {
        if (!self || !EightRollcall()) { Orig(self); return; }
        using namespace tfex::gen;
        uintptr_t base = exl::util::GetMainModuleInfo().m_Total.m_Start;
        float* icon = *reinterpret_cast<float**>(base + 0x4795ec0);   /* RollcallElement.ControlIconPos (SetControlIconPos) */
        if (!icon) { Orig(self); return; }
        float ix = icon[0], iy = icon[1];
        icon[0] = -100000.0f; icon[1] = -100000.0f;
        Orig(self);
        icon[0] = ix; icon[1] = iy;
        void* input = Fld<void*>(self, bind::f_RollcallElement_input);
        if (!input) return;
        int pi = Fld<int32_t>(self, bind::f_RollcallElement_playerIndex);
        int state = Fn<int (*)(void*)>(bind::m_StateMachine_get_State)(Fld<void*>(self, bind::f_RollcallElement_state));
        uint8_t* e = reinterpret_cast<uint8_t*>(self);
        Vector2 pos = Fld<Vector2>(self, bind::f_Entity_Position);
        pos.X += ix + *reinterpret_cast<float*>(e + 0x108);
        pos.Y += iy + *reinterpret_cast<float*>(e + 0x10c);
        /* the name, 15 under the icon, in the archer's ColorA (ColorB once joined), black outline */
        Cctor(bind::c_TFGame);
        void* chars = *reinterpret_cast<void**>(*reinterpret_cast<void**>(base + 0x4792458));   /* TFGame.Characters */
        void* archers = Fn<void* (*)()>(bind::m_ArcherData_get_Archers)();
        int32_t ch = (chars && pi >= 0 && pi < **reinterpret_cast<int32_t**>(reinterpret_cast<uint8_t*>(chars) + 0x20)) ? ArrData<int32_t>(chars)[pi] : -1;
        void* ad = (archers && ch >= 0 && ch < **reinterpret_cast<int32_t**>(reinterpret_cast<uint8_t*>(archers) + 0x20)) ? ArrData<void*>(archers)[ch] : nullptr;
        uint32_t black = **reinterpret_cast<uint32_t**>(base + 0x4724b18);   /* Color.Black (the game's get_Black) */
        if (ad) {
            uint32_t col = Fld<uint32_t>(ad, state == 1 ? bind::f_ArcherData_ColorB : bind::f_ArcherData_ColorA);
            void* font = Fn<void* (*)()>(bind::m_TFGame_get_Font)();
            void* name = Virt<void*>(input, bind::v_PlayerInput_get_Name);
            Fn<void (*)(void*, void*, Vector2, uint32_t, uint32_t)>(bind::m_Draw_OutlineTextCentered_cf06)(font, name, Vector2{pos.X, pos.Y + 15.0f}, col, black);
        }
        Virt<void>(input, 0xd8, pos);   /* the input draws its icon there (Switch-only virtual, the game's call) */
        /* joined with a GameCube controller: its badge 16 to the right */
        void* inputs = *reinterpret_cast<void**>(*reinterpret_cast<void**>(base + 0x4791fa8));   /* TFGame.PlayerInputs */
        if (state == 1 && inputs && pi >= 0 && pi < **reinterpret_cast<int32_t**>(reinterpret_cast<uint8_t*>(inputs) + 0x20)) {
            void* pin = ArrData<void*>(inputs)[pi];
            if (pin && reinterpret_cast<uint8_t*>(pin)[0x10]) {
                void* tex = Fn<void* (*)(void*, void*)>(bind::m_Atlas_get_Item_6538)(Fn<void* (*)()>(bind::m_TFGame_get_MenuAtlas)(),
                                                                                       **reinterpret_cast<void***>(base + 0x478deb0));   /* "controls/gamecube" */
                Fn<void (*)(void*, Vector2)>(0x926ee0)(tex, Vector2{pos.X + 16.0f, pos.Y});   /* Draw.TextureCentered(Subtexture, Vector2) */
            }
        }
        static bool said = false;
        if (!said) { said = true; MMLOG("[8player] roll call: input icons drawn in front of the portraits"); }
    }
};

/* ---- Rollcall slots 7-8 -------------------------------------------------
 * CreateRollcall's mode-1 body unrolls exactly 6 slots, so an 8 cap alone
 * gives pads 7-8 no join UI (on-device test 2026-07-07). Post-hook: after
 * Orig() builds slots 0-5, replicate its exact factory+add sequence for
 * indices 6 and 7. Gated on mode 1 while the dev cap patch is the entry
 * path; the shipped mod gates on mode 5. Slot positions come from the
 * element itself (index-based) — expect visual jank until the 2-row layout
 * (step 6). */
/* CreateRollcall's factory + add sequence for one more slot (positioning happens inside RollcallSlotFactoryHook: this direct call runs
 * through the same patched prologue) */
static void AddRollcallSlot(void* menu, int idx) {
    void* el    = tf8::Call<void* (*)(void*, int, int)>(tf8::off::RollcallSlotNew)(nullptr, idx, 1);
    void* scene = tf8::Call<void* (*)(void*)>(tf8::off::MenuScene)(menu);
    int   li    = tf8::Call<int (*)(void*)>(tf8::off::ElementLayerIndex)(el);
    void* layer = tf8::Call<void* (*)(void*, int)>(tf8::off::SceneGetLayer)(scene, li);
    tf8::Call<void (*)(void*, void*, int)>(tf8::off::SceneLayerAdd)(layer, el, 0);
}
/* Default archers for P7/P8, once (the PC's TFGame cctor: Characters[i] = i for all 8). Retail seeds TFGame.Characters from a 6-element
 * blob {0..5}, so the widened slots 6/7 start on archer 0: the same colour locally, and online a taken archer (seat 0's), so the roll
 * call refuses that seat's A. Players still re-pick freely; this only sets the starting archer. */
static void SeedArchersPastSix() {
    static bool done = false;
    if (done) return;
    void** cobj = tf8::ArrayObj(0x4792458);   /* TFGame.Characters int[] */
    if (!cobj || tf8::ArrayLenObj(cobj) < 8) return;
    int32_t* cd = reinterpret_cast<int32_t*>(tf8::ArrayData(cobj));
    if (cd) { cd[6] = 6; cd[7] = 7; done = true; }
}
HOOK_DEFINE_TRAMPOLINE(CreateRollcallHook) {
    static void Callback(void* self, void* x1) {
        if (::megamod::Online()) {
            /* online: the game's 6 slots, then (a wide TF.EX lobby past 6 seats, WiderSet's on the PC) one for each seat past them */
            Orig(self, x1);
            int seats = OnlineSeatsPastSix();
            /* the roll call's join check (NotJoinedUpdate: PlayerAmount < the cap table's VERSUS entry) and StartVersus's: 8, as WiderSet's
             * on the PC, while this lobby has seats past 6; the game's 6 otherwise */
            ::megamod::SetOnlineWord(tf8::off::VersusCapTable, seats > 0);
            if (seats) SeedArchersPastSix();
            SetControlIconPos(seats > 0);
            for (int idx = 6; idx < seats; idx++) AddRollcallSlot(self, idx);
            if (seats) MMLOG("[8player] online: roll call slots 7-%d added (a %d-seat wide lobby)", seats, seats);
            return;
        }
        Orig(self, x1);
        g_teamSide[0] = -1; g_teamSide[1] = -1;  /* fresh match: P7/P8 unassigned */
        /* Lazy one-time: widen TreasureSpawner.ChestChances 5 -> 7 rows via
         * the game's own allocator; rows 5/6 alias row 4 (6-player odds). */
        {
            uintptr_t base = exl::util::GetMainModuleInfo().m_Total.m_Start;
            void** storage = *reinterpret_cast<void***>(base + tf8::off::ChestChances_cell);
            if (storage && *storage) {
                void** oldArr = reinterpret_cast<void**>(*storage);
                if (tf8::ArrayLenObj(oldArr) == 5) {
                    void* type = **reinterpret_cast<void***>(base + tf8::off::ChestChances_typeCell);
                    int count = 7;
                    void** neu = tf8::Call<void** (*)(void*, int, int*, int)>(tf8::off::NewArr)(type, 8, &count, 1);
                    void** od = tf8::ArrayData(oldArr);
                    void** nd = tf8::ArrayData(neu);
                    if (od && nd) {
                        for (int i = 0; i < 5; i++) nd[i] = od[i];
                        nd[5] = od[4]; nd[6] = od[4];
                        *storage = neu;
                        MMLOG("[8player] ChestChances widened to 7 rows (5/6 alias 6p)");
                    }
                }
            }
        }
        uint32_t* mode = tf8::RollcallModeStorage();
        SetControlIconPos(mode && *mode == 1);
        if (!mode || *mode != 1) return;   /* dev: 8-cap rides mode 1 */
        SeedArchersPastSix();
        /* DIAGNOSTIC: prove whether the array widening actually took at
         * runtime before we risk constructing slot 6. */
        MMLOG("[8player] array lens: Players=%d Characters=%d AltSelect=%d PlayerInputs=%d",
                    tf8::ArrayLen(0x4793890), tf8::ArrayLen(0x4792458),
                    tf8::ArrayLen(0x4792460), tf8::ArrayLen(0x4791fa8));
        for (int idx = 6; idx < 8; idx++) AddRollcallSlot(self, idx);
        MMLOG("[8player] rollcall slots 7+8 added (WiderSet's two rows of four)");
    }
};

/* ---- Task A: 8-player results screen -------------------------------------
 * 4+4 staggered grid spread across the FULL 420-wide results canvas (cards are
 * 74 wide -> half-width 37). Since the Switch has no shorter card asset, two
 * rows of 200-tall cards overlap vertically; maximizing the horizontal spread
 * + half-step stagger makes overlapping cards sit BETWEEN each other so more
 * of each is visible. The corner cards are pushed PAST the edges (center x=20
 * -> left edge -17, and x=400 -> right edge +17) so the outer border clips off
 * but the centered portrait/text stays on screen, buying extra spread. Rows
 * are pulled toward center vertically (top 88 so portraits clear the top edge,
 * bottom 160 so the award icons clear the bottom edge). Half-step stagger:
 *   top[i]    = 20 + step*i           (i 0..3)
 *   bottom[i] = 20 + step*(0.5+i-4)   (i 4..7)   -> bottom[7] = 20+3.5*step = 400
 * tweenFrom (off-screen slide-in) = tweenTo shifted -420 (top) / +420 (bottom). */
namespace tf8 {
struct GridPos { float toX, toY, fromX, fromY; };
static inline GridPos ResultsGrid(int idx) {
    /* SINGLE vertically-centered row, ordered P1..P8 left-to-right (user
     * 2026-07-23; replaces the 4+4 stagger). With kCardScale-shrunk cards a
     * no-overlap row of 8 fits: centers 30..390, step 51.43. 7-8 players
     * ONLY (ResultsEightActive) — 6 and below keep retail layouts. */
    constexpr float kLeft = 30.0f;
    constexpr float kStep = (390.0f - 30.0f) / 7.0f;
    GridPos g;
    g.toX = kLeft + kStep * idx;
    g.toY = 120.0f;
    g.fromX = g.toX - 420.0f;   /* everyone slides in from the left (uniform) */
    g.fromY = g.toY;
    return g;
}
static inline bool ResultsEightActive(void* session) {
    /* Both FFA and TEAM: the per-player results entity only indexes size-8
     * arrays (MatchStats/Characters/AltSelect by playerIndex), never the
     * size-2 team Scores, and we don't widen retail's internal loops (that
     * was the old team crash). The win check GetScoreIndex(i)==GetWinner maps
     * player->team in team mode, so the whole winning team gets crowns. */
    int pa = Call<int (*)()>(off::TFGame_get_PlayerAmount)();
    return pa > 6 && session != nullptr;
}
} /* namespace tf8 */

/* Reposition every portrait to the 8-player 4+4 grid by playerIndex (the
 * retail ctor lays 0..5 out for a 6-player screen; this hook fires for those
 * AND our added 6/7). Gated on FFA + PlayerAmount>6 so 2-6 players and team
 * mode keep retail's layout untouched. */
/* ---- 8p results-card WHOLE-CARD scaling (user 2026-07-23) ------------------
 * At 8 players the full-size cards overlap; there is NO compact card asset on
 * Switch, so shrink each card's VISUALS to 0.75x in place:
 *  - frame + portrait Images (ctor fields +0xb0/+0xb8): Scale AND relative
 *    Position x0.75 (GraphicsComponent: Position +0x24, Scale +0x34).
 *  - components added later by DoSequence: scale-only (their positions are
 *    TWEEN-driven; the tweens stay untouched). Scalable types, from the
 *    2026-07-23 component census (module-relative type headers):
 *      +0x46f7000 stat OutlineTexts, +0x462cdd8 name text (1.5 -> 1.125),
 *      plus anything with the Image header (crowns, award icons) — learned
 *      at runtime from the card's own bg Image.
 *    NOT scaled: +0x46f7cc8 tweens (x44!), +0x46f5cc8, +0x46f5170 (logic;
 *    zeroed/garbage geometry in the census). */
static constexpr float kCardScaleX = 0.75f;  /* card WIDTH (user-approved) */
static constexpr float kCardScaleY = 1.0f;   /* FULL height ("longer cards"):
                                              * stat/award positions are tween-
                                              * driven at native Y offsets, so a
                                              * full-height frame fits them all
                                              * (3 awards incl.) with room */
static constexpr float kAwardScale = 0.5f;   /* awards are 1.5x-scale art wider
                                              * than a card column; halve them */
static void* g_cards[8];
static int   g_cardCount = 0;
static uintptr_t g_imageHdr = 0;   /* learned from the first card's bg */
static int g_cardAddLogBudget = 0;
/* Award icons per card (PC max 3 awards). The sequence's tween drives
 * icon.Scale every frame (lerp 1.5->1), so VPMRRenderHook re-asserts
 * kAwardScale each Render instead of scaling once at Add. */
static void* g_cardIcons[8][3];
static int   g_cardIconN[8];
static inline void ScaleGraphic(void* g, float fx, float fy, bool positionX) {
    if (!g) return;
    float* sc = reinterpret_cast<float*>(reinterpret_cast<uint8_t*>(g) + 0x34);
    sc[0] *= fx; sc[1] *= fy;
    if (positionX) {
        float* po = reinterpret_cast<float*>(reinterpret_cast<uint8_t*>(g) + 0x24);
        po[0] *= kCardScaleX;   /* X only: native Y offsets fit the full-height card */
    }
}
HOOK_DEFINE_TRAMPOLINE(EntityAddComponentHook) {
    static void Callback(void* entity, void* component) {
        /* online too: acts only past 6 players (a wide TF.EX lobby's seats 7-8; the game's results throw for 7-8) */
        if (g_cardCount && component) {
            int ci = -1;
            for (int c = 0; c < g_cardCount && ci < 0; c++)
                if (g_cards[c] == entity) ci = c;
            if (ci >= 0) {
                uintptr_t base = exl::util::GetMainModuleInfo().m_Total.m_Start;
                uintptr_t hdr = *reinterpret_cast<uintptr_t*>(component);
                void** sprCell = *reinterpret_cast<void***>(base + 0x478aeb0);
                uintptr_t sprHdr = sprCell ? reinterpret_cast<uintptr_t>(*sprCell) : 0;
                float* s34 = reinterpret_cast<float*>(reinterpret_cast<uint8_t*>(component) + 0x34);
                if ((sprHdr && hdr == sprHdr) ||
                    (s34[0] == 1.5f && s34[1] == 1.5f)) {
                    /* award icon. Identified by SCALE FINGERPRINT, not type:
                     * PC Sequence sets icon.Scale = One*1.5f right before
                     * Add(icon), and no other post-ctor card component is
                     * added at scale (1.5,1.5). (The 0x478aeb0 Sprite<int>
                     * cell guess kept as a fast path but has never been seen
                     * to fire.) Record it — VPMRRenderHook holds its scale at
                     * kAwardScale every frame; the sequence's tween would
                     * overwrite a one-shot shrink here. */
                    if (g_cardIconN[ci] < 3)
                        g_cardIcons[ci][g_cardIconN[ci]++] = component;
                    ScaleGraphic(component, kAwardScale, kAwardScale, false);
                    MMLOG("[8player] award icon add: card=%d hdr=+0x%lx via=%s n=%d",
                                ci, static_cast<unsigned long>(hdr - base),
                                (sprHdr && hdr == sprHdr) ? "typecell" : "scale1.5",
                                g_cardIconN[ci]);
                }
                else if (hdr == base + 0x46f7000 ||     /* stat OutlineText  */
                         hdr == base + 0x462cdd8 ||     /* name text         */
                         (g_imageHdr && hdr == g_imageHdr)) /* crown/gem Image */
                    ScaleGraphic(component, kCardScaleX, kCardScaleX, false);
                else if (g_cardAddLogBudget > 0 && hdr != base + 0x46f5170 && hdr != base + 0x46f7cc8) {   /* (not the known tweens: 44 a card) */
                    /* census: unhandled types (Coroutine +0x46f5170 skipped) */
                    g_cardAddLogBudget--;
                    float* p24 = reinterpret_cast<float*>(reinterpret_cast<uint8_t*>(component) + 0x24);
                    MMLOG("[8player] cardAdd?: hdr=+0x%lx p24=(%.1f,%.1f) p34=(%.2f,%.2f)",
                                static_cast<unsigned long>(hdr - base),
                                (double)p24[0], (double)p24[1], (double)s34[0], (double)s34[1]);
                }
            }
        }
        Orig(entity, component);
    }
};

HOOK_DEFINE_TRAMPOLINE(VPMRCtorHook) {
    /* Factory ctor (x0=null -> allocates, returns the new object), so the
     * Callback MUST return Orig's result to the caller. */
    static void* Callback(void* self, void* session, void* parent, int idx, void* awards,
                          int small, float tfx, float tfy, float tx, float ty) {
        /* online too: acts only past 6 players (a wide TF.EX lobby's seats 7-8; the game's results throw for 7-8) */
        if (idx >= 0 && idx < 8 && tf8::ResultsEightActive(session)) {
            tf8::GridPos g = tf8::ResultsGrid(idx);
            /* Force the COMPACT layout (small=1). Retail computes small as
             * (PlayerAmount==6) -> false for 8, giving full-size portraits +
             * award banners that overflow the 4+4 grid. The PC 8p build uses
             * small = PlayerAmount>5 -> true: bgSmall/winnerBgSmall and awards
             * cleared. Assets already exist (retail uses them for 6-player). */
            void* obj = Orig(self, session, parent, idx, awards, 1, g.fromX, g.fromY, g.toX, g.toY);
            if (g_cardCount < 8) {
                g_cardIconN[g_cardCount] = 0;
                g_cards[g_cardCount++] = obj;
            }
            /* shrink the ctor-built visuals in place */
            void* bg       = *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(obj) + 0xb0);
            void* portrait = *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(obj) + 0xb8);
            if (bg && !g_imageHdr) g_imageHdr = *reinterpret_cast<uintptr_t*>(bg);
            ScaleGraphic(bg, kCardScaleX, kCardScaleY, true);        /* narrow, FULL height */
            ScaleGraphic(portrait, kCardScaleX, kCardScaleX, true);  /* faces undistorted */
            /* lift the portrait a touch (user 2026-07-24); the border rect
             * follows below, and the stat/award anchors follow for free —
             * Sequence reads portrait.Position live, so everything under the
             * portrait rises too, buying the award stack extra room */
            constexpr float kPortraitLift = 6.0f;
            if (portrait)
                reinterpret_cast<float*>(reinterpret_cast<uint8_t*>(portrait) + 0x24)[1] -= kPortraitLift;
            /* win/lose backdrop DrawRectangle @+0xc0 (x/y/w/h @+0x24..+0x30):
             * shrink width only, height keeps the native portrait framing */
            void* rect = *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(obj) + 0xc0);
            if (rect) {
                /* keep the colored border hugging the SCALED portrait: shrink
                 * both axes and re-center vertically on the portrait's native
                 * center (portrait scales 0.75 around its own center) */
                float* f = reinterpret_cast<float*>(reinterpret_cast<uint8_t*>(rect) + 0x24);
                float h = f[3];
                f[0] *= kCardScaleX;                       /* x */
                f[1] += h * (1.0f - kCardScaleX) * 0.5f;   /* y toward center */
                f[1] -= kPortraitLift;                     /* follow the portrait */
                f[2] *= kCardScaleX;                       /* w */
                f[3] *= kCardScaleX;                       /* h */
            }
            /* win/lose label text @+0xc8 */
            void* label = *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(obj) + 0xc8);
            ScaleGraphic(label, kCardScaleX, kCardScaleX, true);
            return obj;
        }
        return Orig(self, session, parent, idx, awards, small, tfx, tfy, tx, ty);
    }
};

/* Hold every tracked card's award icons at kAwardScale. The awards sequence
 * tween-drives icon.Scale each Update (lerp 1.5->1, PC Sequence), so this
 * re-asserts the shrink each Render — after Update, before the draw — and
 * always wins. Cards are only tracked when ResultsEightActive, so 2-6 player
 * screens render retail-native (g_cardCount stays 0). */
HOOK_DEFINE_TRAMPOLINE(VPMRRenderHook) {
    static void Callback(void* self) {
        /* online too: g_cardCount is 0 unless a results screen past 6 players made cards */
        for (int c = 0; c < g_cardCount; c++) {
            if (g_cards[c] != self) continue;
            for (int k = 0; k < g_cardIconN[c]; k++) {
                float* sc = reinterpret_cast<float*>(
                    reinterpret_cast<uint8_t*>(g_cardIcons[c][k]) + 0x34);
                sc[0] = kAwardScale;
                sc[1] = kAwardScale;
                static bool logged = false;
                if (!logged) {
                    logged = true;
                    MMLOG("[8player] render clamp LIVE (card=%d icons=%d)", c, g_cardIconN[c]);
                }
            }
            break;
        }
        Orig(self);
    }
};

/* Give P7/P8 real award medals. AssignAward rates all players (its rating loop
 * is bounded by Players.Length=8) and — with the assign-loop bound widened +
 * the playerAwards[] array widened to 8 — assigns to P7/P8 too, but the size-6
 * unrolled init leaves playerAwards[6/7] null. Create those two lists at entry
 * (idempotent; runs once per award category). */
HOOK_DEFINE_TRAMPOLINE(AssignAwardHook) {
    static void Callback(void* settings, void* awardInfo) {
        /* online too: acts only past 6 players (a wide TF.EX lobby's seats 7-8; the game's results throw for 7-8) */
        void** arr = tf8::ArrayObj(tf8::off::PlayerAwards_cell);
        if (arr && tf8::ArrayLenObj(arr) >= 8) {
            void** data = tf8::ArrayData(arr);
            if (data)
                for (int i = 6; i < 8; i++)
                    if (tf8::PlayersJoined(i) && !data[i])
                        data[i] = tf8::Call<void* (*)(void*)>(tf8::off::NewAwardsList)(nullptr);
        }
        Orig(settings, awardInfo);
    }
};

/* Post-hook the results ctor: retail's creation loop only spawns portraits for
 * joined players in indices 0..5, so spawn the entities for players 6/7 the
 * same way (VPMR ctor + the Level scene-add chain + add to playerResults). */
HOOK_DEFINE_TRAMPOLINE(VMRResultsHook) {
    /* Factory ctor: x0 arrives null and the object is Orig's RETURN value —
     * use that as `this`, and return it so the caller gets the real object. */
    static void* Callback(void* self, void* session, void* arg2) {
        /* online too: acts only past 6 players (a wide TF.EX lobby's seats 7-8; the game's results throw for 7-8) */
        g_cardCount = 0;                 /* new results screen: reset card set */
        for (int c = 0; c < 8; c++) g_cardIconN[c] = 0;
        g_cardAddLogBudget = 24;    /* (a results screen's log: 24 lines at most) census stays on until the award icon's
                                     * identity is CONFIRMED in a log (the
                                     * typecell guess never fired; 2026-07-24
                                     * fingerprint pass needs verification) */
        void* obj = Orig(self, session, arg2);
        if (!tf8::ResultsEightActive(session)) return obj;
        uint8_t* s = reinterpret_cast<uint8_t*>(session);
        int   winner     = tf8::Call<int (*)(void*)>(tf8::off::Session_GetWinner)(session);
        void* matchStats = *reinterpret_cast<void**>(s + 0x28);
        uint8_t* msData  = reinterpret_cast<uint8_t*>(tf8::ArrayData(reinterpret_cast<void**>(matchStats)));
        void* level      = *reinterpret_cast<void**>(s + 0x68);
        void* list       = *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(obj) + 0x98);
        int made = 0;
        for (int i = 6; i < 8; i++) {
            if (!tf8::PlayersJoined(i)) continue;
            /* winner crown flag (retail's winner loop only ran 0..5) */
            if (msData) {
                int si = tf8::Call<int (*)(void*, int)>(tf8::off::Session_GetScoreIndex)(session, i);
                msData[i * 0x2a8] = (si == winner) ? 1 : 0;
                /* Full PC winner-loop parity (retail body @0xbaff0c, unrolled
                 * 0..5): a winning player ALSO records the archer win —
                 * SaveData.Instance.Stats.Wins[Characters[i]]++ (int64[], chain
                 * cell 0x47924e0 -> storage -> instance -> Stats@+0x20 ->
                 * Wins@+0x1c8) and SessionStats.RegisterArcherWin(i) @0xb478f0.
                 * Without this, P7/P8 wins never reached the archives/lifetime
                 * stats. The class-init guard retail runs first has already run
                 * (Orig executed the 0..5 loop before we get here). */
                if (si == winner) {
                    void** cobj = tf8::ArrayObj(0x4792458);   /* TFGame.Characters */
                    int32_t* cd = cobj ? reinterpret_cast<int32_t*>(tf8::ArrayData(cobj)) : nullptr;
                    if (cd && tf8::ArrayLenObj(cobj) > i) {
                        int ch = cd[i];
                        uintptr_t base = exl::util::GetMainModuleInfo().m_Total.m_Start;
                        void** sdStorage = *reinterpret_cast<void***>(base + 0x47924e0);
                        void* inst  = sdStorage ? *sdStorage : nullptr;
                        void* stats = inst ? *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(inst) + 0x20) : nullptr;
                        void** wins = stats ? reinterpret_cast<void**>(*reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(stats) + 0x1c8)) : nullptr;
                        if (wins && ch >= 0 && ch < tf8::ArrayLenObj(wins)) {
                            int64_t* wd = reinterpret_cast<int64_t*>(tf8::ArrayData(wins));
                            if (wd) wd[ch]++;
                        }
                        tf8::Call<void (*)(int)>(0xb478f0)(i);   /* RegisterArcherWin */
                        MMLOG("[8player] archer-win stat recorded for P%d", i + 1);
                    }
                }
            }
            /* P7/P8's earned awards, computed by GetAwards into the static
             * playerAwards[] (AssignAwardHook + widening let them win). Fall
             * back to a fresh empty list if unavailable (VPMR needs non-null). */
            void* awards = nullptr;
            void** awArr = tf8::ArrayObj(tf8::off::PlayerAwards_cell);
            if (awArr && tf8::ArrayLenObj(awArr) >= 8) {
                void** awData = tf8::ArrayData(awArr);
                if (awData) awards = awData[i];
            }
            if (!awards) awards = tf8::Call<void* (*)(void*)>(tf8::off::NewAwardsList)(nullptr);
            tf8::GridPos g = tf8::ResultsGrid(i);
            void* vpmr = tf8::Call<void* (*)(void*, void*, void*, int, void*, int, float, float, float, float)>(
                             tf8::off::VersusPlayerMatchResults_ctor)(
                             nullptr, session, obj, i, awards, 1, g.fromX, g.fromY, g.toX, g.toY);
            /* Level.Add chain (same as retail's creation loop) */
            void* scene = tf8::Call<void* (*)(void*)>(tf8::off::MenuScene)(level);
            int   li    = tf8::Call<int (*)(void*)>(tf8::off::ElementLayerIndex)(vpmr);
            void* layer = tf8::Call<void* (*)(void*, int)>(tf8::off::SceneGetLayer)(scene, li);
            tf8::Call<void (*)(void*, void*, int)>(tf8::off::SceneLayerAdd)(layer, vpmr, 0);
            if (list) tf8::Call<void (*)(void*, void*)>(tf8::off::PlayerResults_ListAdd)(list, vpmr);
            made++;
        }
        if (made) MMLOG("[8player] results: added %d portrait(s) for P7/P8", made);
        return obj;
    }
};

#if TF8_DEV_FORCE_MODE5
/* ---- DEV SHIM: enter mode 5 via the 2-6 button ---------------------------
 * Orig() writes RollcallMode=1 (str @0xb72614) and queues the menu
 * transition; the rollcall screen is built after this callback returns, so
 * overwriting the static here means CreateRollcall/get_MaxPlayers see 5.
 * Exact signature unknown (menu callback using x0/x1) — forward both and
 * return x0 untouched. */
HOOK_DEFINE_TRAMPOLINE(Versus6ConfirmShim) {
    static void* Callback(void* self, void* arg) {
        MM_ONLINE_PASS(Orig(self, arg));
        void* ret = Orig(self, arg);
        uint32_t* mode = tf8::RollcallModeStorage();
        if (mode) {
            *mode = tf8::kVersus8Mode;
            MMLOG("[8player] DEV SHIM: 2-6 confirm -> forced RollcallMode=5");
        }
        return ret;
    }
};
#endif

namespace eightplayer {
/* Install every 8-player hook + the array/loop widening substrate. Called from
 * the combined exl_main AFTER exl::hook::Initialize() (TowerBall owns that and
 * the exception entry). The widening is always-on substrate TowerBall inherits
 * so it can run at 8 players too. */
inline void InstallAll() {
    /* Proof-of-life — search the Ryujinx log for "[8player]". */
    MMLOG("[8player] injected into TowerFall (M1: cap hook"
#if TF8_DEV_FORCE_MODE5
                " + DEV mode-5 shim on the 2-6 button"
#endif
                ")");

    MaxPlayersHook::InstallAtOffset(tf8::off::RollcallElement_get_MaxPlayers);
    CreateRollcallHook::InstallAtOffset(tf8::off::MainMenu_CreateRollcall);
    RollcallSlotFactoryHook::InstallAtOffset(tf8::off::RollcallSlotNew);
    PortraitRenderHook::InstallAtOffset(0x96f350);
    PortraitInitGemHook::InstallAtOffset(tfex::gen::bind::m_ArcherPortrait_InitGem);   /* ArcherPortrait::Render (tools/re callmap) */
    RollcallIconsHook::InstallAtOffset(tfex::gen::bind::m_RollcallElement_Render);
    AssignInputsHook::InstallAtOffset(tf8::off::PlayerInput_AssignInputs);
    TeamsGetItemHook::InstallAtOffset(tf8::off::MatchTeams_get_Item);
    TeamsSetItemHook::InstallAtOffset(tf8::off::MatchTeams_set_Item);
    tfex::gen::hooks::MatchTeams_get_ProperlyAssigned::Wrap(ProperlyAssignedShell);   /* (Teams+'s hook's trampoline) */
    GetPlayersOfAllegianceHook::InstallAtOffset(tf8::off::MatchTeams_GetPlayersOfAllegiance);
    AmountOfAllegianceHook::InstallAtOffset(tf8::off::MatchTeams_AmountOfAllegiance);
    PlayerTeamSizeHook::InstallAtOffset(tf8::off::MatchSettings_GetPlayerTeamSize);
    CharacterTakenHook::InstallAtOffset(0xb76630);   /* TFGame.CharacterTaken(int) */
    tfex::gen::hooks::MainMenu_CreateTeamSelect::Wrap(CreateTeamSelectShell);   /* (Teams+'s hook's trampoline) */
    ReplayViewerCtorHook::InstallAtOffset(tf8::off::ReplayViewer_ctor);
    ReplayRenderHook::InstallAtOffset(tf8::off::ReplayRender);
    VariantGetValueHook::InstallAtOffset(tf8::off::Variant_get_Value);
    VariantSetValueHook::InstallAtOffset(tf8::off::Variant_set_Value);
    VariantAllTrueHook::InstallAtOffset(tf8::off::Variant_get_AllTrue);
    VariantCleanHook::InstallAtOffset(tf8::off::Variant_Clean);
    VariantPerPlayerCtorHook::InstallAtOffset(tf8::off::VariantPerPlayer_ctor);
    FightButtonFrontHook::InstallAtOffset(tf8::off::FightButton_ctor);
    ResetJoinAHook::InstallAtOffset(tf8::off::ResetJoin_A);
    ResetJoinBHook::InstallAtOffset(tf8::off::ResetJoin_B);
    ResetJoinCHook::InstallAtOffset(tf8::off::ResetJoin_C);
    VMRResultsHook::InstallAtOffset(tf8::off::VersusMatchResults_ctor);
    EntityAddComponentHook::InstallAtOffset(tf8::off::Entity_AddComponent);
    VPMRCtorHook::InstallAtOffset(tf8::off::VersusPlayerMatchResults_ctor);
    VPMRRenderHook::InstallAtOffset(tf8::off::VersusPlayerMatchResults_Render);
    AssignAwardHook::InstallAtOffset(tf8::off::VersusAwards_AssignAward);
    ManagedThrowHook::InstallAtOffset(0x2861ac0);   /* crash-site logger */
    tf8::PatchPlayerArrays();   /* step 4: arrays 6->8 (must precede any idx>5 use) */
    tf8::PatchBounds();         /* step 3/4: literal player-loop bounds 6->8 */
#if TF8_DEV_FORCE_MODE5
    Versus6ConfirmShim::InstallAtOffset(tf8::off::Versus6_OnConfirm);
#endif
}
/* The 2-8 lobby (VERSUS's cap and its labels): when WiderSet's folder loaded (widerset::Boot, mods/Teuria.WiderSet); the
 * arrays and loop bounds above stay widened from boot either way */
inline void OfferEight() {
#if TF8_DEV_CAP_2TO8
    /* entry[0] = mode 1 (Versus 2-6) cap: 6 -> 8. Quest/DarkWorld/Trials
     * entries untouched. */
    megamod::Patch(tf8::off::VersusCapTable, 6, 8, "cap table: VERSUS 2-6 caps at 8");
    /* The menu labels: "2-6 ARCHERS" (FIGHT's WIDE subtitle), "6-PLAYER" (SELECT LEVEL SET's set), "SUPPORTS UP TO 6 ARCHERS" (its
     * subtitle) say 8. The game copies every string literal into a managed string at startup (0x12670(text, length), kept in a slot)
     * before any mod boots, so the '6' is changed in that copy (patching the literal's text in main.flat changed nothing on screen:
     * the console and an emulator shot, 2026-11-06). Slots and the '6''s index from tools/re (xref of each text). */
    {
        struct Label { uintptr_t got; int at; const char* what; };
        static const Label kLabels[] = { {0x478d0b8, 2, "FIGHT's \"2-8 ARCHERS\""}, {0x478d150, 0, "SELECT LEVEL SET's \"8-PLAYER\""},
                                         {0x4790e98, 15, "\"SUPPORTS UP TO 8 ARCHERS\""} };
        uintptr_t base = exl::util::GetMainModuleInfo().m_Total.m_Start;
        for (const Label& l : kLabels) {
            void** slot = *reinterpret_cast<void***>(base + l.got);
            void* str = slot ? *slot : nullptr;
            char16_t* c = str ? const_cast<char16_t*>(tfex::gen::Chars(str)) + l.at : nullptr;
            if (c && (*c == u'6' || *c == u'8')) { *c = u'8'; MMLOG("[8player] label %s", l.what); }
            else MMLOG("[8player] ERROR label %s: its string isn't the expected one (slot +%#lx)", l.what, (unsigned long)l.got);
        }
    }
#endif
    MMLOG("[8player] the 2-8 lobby offered (WiderSet's folder loaded)");
}
} // namespace eightplayer
