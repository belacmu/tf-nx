/*
 * TowerBall port — reusable hook layer + validated offset table.
 * ------------------------------------------------------------------
 * Two hooking primitives (Ryujinx, v1.4.1.6):
 *   - InstallAtOffset: non-virtual methods, exlaunch trampoline at a static offset.
 *   - ResolveVirtualBody + InstallAtPtr: virtual methods — read the body address
 *     off a live instance's vtable, trampoline the BODY at runtime. Class method
 *     tables are NEVER written (see the delegate-ctor warning below).
 *
 * Offsets are for TowerFall Switch v1.4.1.6 (title 0100A1C00359C000), module-
 * base-relative (== InstallAtOffset). Regenerate with tools/re/metaresolve.py
 * after any game update — offsets are per-build; vtable SLOTS are stable.
 */
#pragma once
#include "lib.hpp"
#include <lib/util/sys/modules.hpp>   /* exl::util::GetMainModuleInfo — for tb::Call */
#include "towerball_fields.hpp"        /* fld/slot/mode/arrow enums + pure helpers (host-safe) */

namespace tb {

/* ---- DIRECT (non-virtual) method body offsets ------------------------- */
namespace off {
    constexpr uintptr_t Player_ShootArrow       = 0xaf1c50;  /* void(Player*) */
    constexpr uintptr_t Player_CollectArrows    = 0xaf5c30;  /* bool(Player*, ArrowTypes[]) */
    /* WARNING: 0xaf3450 is the Die(Arrow) OVERLOAD — x1 is the killing Arrow*,
     * NOT the DeathCause enum. Hooking it with an int signature truncated the
     * pointer and crashed every arrow kill (2026-07-06). The 4-arg
     * Die(DeathCause,int,bool,bool) body is a different (unresolved) offset;
     * prefer the RL.OnPlayerDeath vtable detour, which covers all causes. */
    constexpr uintptr_t Player_DieByArrow       = 0xaf3450;  /* void(Player*, Arrow*) */
    constexpr uintptr_t Player_DuckingUpdate    = 0xaea610;  /* int(Player*) */
    constexpr uintptr_t Player_HUDRender        = 0xaf0ee0;  /* void(Player*, bool) */
    constexpr uintptr_t TowerMapData_GetLevelSystem = 0xb7f600; /* LevelSystem*(TowerMapData*) */
    constexpr uintptr_t Level_GetXMLPositions   = 0xa7e8c0;  /* List*(Level*, string) */
    /* ---- Phase 3 (harvested 2026-07-06; static — not yet runtime-validated) --- */
    constexpr uintptr_t Level_GetPlayer         = 0xa7f980;  /* Player*(Level*, int) — CALL to query */
    constexpr uintptr_t Session_GetHighestScore = 0xb45140;  /* int(Session*) */
    constexpr uintptr_t MatchSettings_GoalScore = 0xabae50;  /* int(MatchSettings*) — mode-switched, CALL it */
    constexpr uintptr_t Arrow_Create            = 0x985350;  /* the ball spawn factory (ABI below) */
    constexpr uintptr_t Explosion_Spawn         = 0xa3df10;  /* slam FX (static) */
    /* ---- Selectable-mode plumbing (harvested 2026-07-06) ------------------
     * Modes.Warlord (enum 6) is fully wired (name/icon/RoundLogic factory) but
     * UNREACHABLE in this build's versus UI (VersusModeButton caps Right at 5).
     * Lifting that cap makes mode 6 selectable — the vehicle for TowerBall. */
    constexpr uintptr_t Session_GetRoundLogic   = 0xb37f50;  /* Mode→RoundLogic jump-table (case 6 = 0xb68500) */
    constexpr uintptr_t VersusModeButton_GetModeName = 0xbb1c00; /* static string(Modes) — INLINED into Render; hook is dead */
    constexpr uintptr_t VersusModeButton_GetModeIcon = 0xbb1c80; /* static Subtexture(Modes) — called by Render */
    /* Mode-name String pointer table (module-relative). GetModeName/Render index
     * it by (mode-3), 8-byte stride: entry = table[mode-3] (a ptr-to-field),
     * String = *entry. Mode 6 → index 3. Used to patch the name String in place. */
    constexpr uintptr_t ModeName_Table          = 0x4576a00;
    /* Scene.Add(Entity) — RESOLVED 2026-07-06. It's a GENERIC method (why the
     * metadata resolver never found "Add") and BRUTE inlines it as a 4-step
     * dance, confirmed 1:1 against PC source (Monocle.Scene / Monocle.Layer):
     *   PC:  Scene.Add<T>(e) => Layers[e.LayerIndex].Add(e);
     *        Layer.Add(e, layerChange=false) => toAdd.Add(e);   // DEFERRED
     * Native (seen in ShootArrow @0xaf1db0.. and the Warlord factory tail):
     *   layers = [scene+0x18]            (getter 0x943c40, Dictionary<int,Layer>)
     *   idx    = [entity+0x54]           (getter 0x92dd20, Entity.LayerIndex)
     *   layer  = Dict_GetItem(layers,idx)  (0x92d9f0, 461 callers)
     *   Layer_Add(layer, entity, 0)        (0x938e40 = ldr x0,[x0,#0x60]; b List.Add)
     * The add is deferred into Layer.toAdd ([layer+0x60]); the engine's own
     * UpdateEntityList pass then does Entities.Add + entity.Scene=… + Added().
     * So calling this mid-frame (e.g. from RoundLogic.OnUpdate) is exactly as
     * safe as the game's own ShootArrow. Callable helper: tb::SceneAdd(). */
    constexpr uintptr_t Dict_Int_GetItem    = 0x92d9f0;  /* Layer*(Dictionary<int,Layer>*, int) */
    constexpr uintptr_t Layer_Add           = 0x938e40;  /* void(Layer*, Entity*, bool layerChange) */
    constexpr uintptr_t Entity_RemoveSelf   = 0x92f960;  /* void(Entity*) — deferred Scene.Remove (79 callers) */
    /* VersusLevelSystem.GenLevels(this, MatchSettings) — fills this.levels with the
     * round order (refs to the tower's CANONICAL path strings — patch with restore!). */
    constexpr uintptr_t VLS_GenLevels       = 0xbae7a0;  /* DIRECT, 2 callers */
    /* ---- Respawn machinery (harvested from SpawnPlayersFFA 0xb38a50, 2026-07-06) ----
     * Player ctor SELF-ALLOCATES when x0==0 (standard BRUTE ctor pattern). ABI read
     * off the construction site 0xb38e2c..0xb38e4c:
     *   x0=0  w1=playerIndex  x2=allegiance(-1 FFA)  x3=teamColor(-1)
     *   x4/x5=PlayerInventory (16-byte struct = GetPlayerInventory's x0/x1)
     *   x6=hatState  w7=frozen  [sp]=flash  [sp+8]=indicator  s0/s1=position */
    constexpr uintptr_t Player_Ctor              = 0xae6870;  /* 9 callers */
    constexpr uintptr_t Session_GetPlayerInventory = 0xb45950; /* (Session*,int) -> struct in x0/x1 */
    constexpr uintptr_t Session_GetSpawnHatState   = 0xb46260; /* (Session*,int) -> int */
    constexpr uintptr_t Level_GetPlayerOrCorpse    = 0xa7fb00; /* (Level*,int) -> LevelEntity* */
    constexpr uintptr_t RoundLogic_FinalKill       = 0xb3a6e0; /* (rl, corpse, spotlightIdx) — endgame FX */
    /* (NOTE: was mis-transcribed as 0xadb1a0 — a by-hand hex conversion slip;
     * always convert with hex(). Confirmed via all 5 call sites.) */
    constexpr uintptr_t OrbLogic_DoSlowMoKill      = 0xadb420; /* (OrbLogic*) — endgame slow-mo */
    /* ---- Team mode (harvested 2026-07-06) ----------------------------------
     * get_TeamMode: `ldr w8,[ms+0x30]; cmp w8,#5; cset eq` (59 callers) — hook
     * to also return true for mode 6 (PC TowerBall is TeamMode=true).
     * GetPlayerAllegiance: mode==5 ? TeamsMapper([ms+0x38], idx) : -1 (Neutral;
     * Allegiance here is -1 Neutral / 0 Blue / 1 Red — matches the live dump). */
    constexpr uintptr_t MatchSettings_GetTeamMode        = 0xaba740; /* bool(ms) */
    constexpr uintptr_t MatchSettings_GetPlayerAllegiance = 0xabb5c0; /* int(ms, idx) */
    constexpr uintptr_t Teams_GetAllegiance              = 0xabc420; /* int(teamsData, idx) — pattern mapper */
    /* Monocle.Draw.Rect(float x, float y, float w, float h, Color c) — ABI read
     * off the invoke thunk 0x26a1ca0: w0 = PACKED XNA Color (R|G<<8|B<<16|A<<24),
     * s0..s3 = x,y,w,h. Draws with the active sprite batch (call from HUDRender). */
    constexpr uintptr_t Draw_Rect                        = 0x9237b0; /* 89 callers */
    /* ---- Subtexture rendering chain (2026-07-06, from TeamReviver.HUDRender
     * 0xb6a470 — a real call site showing the full recipe) ------------------ */
    constexpr uintptr_t TFGame_get_Atlas       = 0xb74790; /* () -> Atlas* (271 callers) */
    constexpr uintptr_t TFGame_get_MenuAtlas   = 0xb747f0; /* () -> Atlas* (285 callers) */
    constexpr uintptr_t Atlas_get_Item         = 0x904b10; /* (Atlas*, String*) -> Subtexture* (597) — THROWS on missing key */
    constexpr uintptr_t Draw_OutlineTextureCentered = 0x927050; /* (Subtexture* x0, Color w1, pos s0/s1) (10 callers) */
    /* Sounds.sfx_finalKill static (data seg; holds SFX*) — from FinalKill tail
     * 0xb3a84c: played via SFX virtual slot 0x20 with (x=160.0f, vol=1.0f). */
    constexpr uintptr_t Sounds_sfx_finalKill        = 0x4795f88;
    /* ---- Team-spawn parity (2026-07-06 evening) ----------------------------
     * TDM.OnLevelLoadFinish body (found via its bl to base 0xb38070 right after
     * the TDM RL ctor 0xb68500): base.OnLevelLoadFinish + new VersusStart +
     * SceneAdd + SpawnPlayersTeams(0xb39070) + Players=PlayerAmount. Touches
     * ONLY base RoundLogic fields (PC-verified) → safe to call on the Warlord
     * RL instance in mode 6 for exact team spawns/outlines/aimers. */
    constexpr uintptr_t TDM_OnLevelLoadFinish       = 0xb685c0;
    constexpr uintptr_t RoundLogic_SpawnPlayersTeams = 0xb39070; /* (rl) — reference */
    /* Arrow.UpdateSeeking (direct body; known since the Speed-field harvest).
     * PC BasketBall forces Variants.NoSeekingArrows around every update — the
     * ball must never home on players. Hooked: self==g_ball → skip. */
    constexpr uintptr_t Arrow_UpdateSeeking         = 0x986290;
    /* Session.EndRound (metaresolve DIRECT). TowerBall v3 (the ORIGINAL mod)
     * ends the match itself: endCounter(90) after the winning goal, then
     * Session.EndRound() every frame — it never touches Level.Ending/CanEnd
     * (which is why the flag-based end never fired: score-5 hang, 2026-07-06). */
    constexpr uintptr_t Session_EndRound            = 0xb44b40;
    /* Sounds.env_arrowToyChar static cell (the toy-arrow/basketball SQUEAK —
     * v3 plays it on every ball bounce + intercept). Identified via Sounds.cs
     * declaration adjacency (env_arrowToyChar; env_arrowToyWall) matching the
     * adjacent cells 0x4792930/0x4792938, and the single Play(x, 1.0) site
     * 0x9861bc in ToyArrow.CheckForTargetCollisions. DOUBLE-DEREF like all
     * BRUTE statics. */
    constexpr uintptr_t Sounds_env_arrowToyChar     = 0x4792930;
    /* RoundLogic.AddScore(int scoreIndex, int add) — found via TDM's
     * team-round-win call site 0xb687cc (mov w2,#1; bl). Confirms Scores =
     * int[] at [sess+0x30], data [arr+0x10], len *[arr+0x20]. CRITICAL: also
     * emits GainPointEvent — the results screen fills the win pips from those
     * events, so scores MUST go through AddScore, never raw memory writes
     * (raw writes = the empty-pips bug, 2026-07-06). */
    constexpr uintptr_t RoundLogic_AddScore         = 0xb39db0;
    /* Draw.OutlineTextCentered(SpriteFont, string, Vector2, Color, float scale)
     * = 0x9245a0, lifted from VersusModeButton.Render's mode-name draw
     * 0xbb2128..21a0: x0=font x1=String w2=packed Color s0/s1=pos s2=scale.
     * Font from TFGame.get_Font (271 callers, same family as get_Atlas). */
    constexpr uintptr_t Draw_OutlineTextCentered_Str = 0x9245a0;
    constexpr uintptr_t TFGame_get_Font              = 0xb74bb0;
    /* Level.CollideFirst(Vector2 at, GameTags tags) — POINT query, lifted from
     * Explosion.Spawn's head (0xa3df68: w1=3 Solid, s0/s1=at). Returns Entity*.
     * GameTags: Solid=3, JumpThru=5 (v3 dunk uses both). */
    constexpr uintptr_t Level_CollideFirst_Point     = 0x9459d0;
    /* LevelEntity.Flash(int frames, Action onComplete) — found via its field
     * cluster (sets Flashing[+0xc0]=1, blink rate 3.0f at +0xc4, frame counts
     * at +0xc8); args verified: x0=this w1=frames x2=Action(null ok). */
    constexpr uintptr_t LevelEntity_Flash            = 0xa81cb0;
    /* ArrowCushion.ReleaseArrows(Vector2 speed) — via the PlayerCorpse-region
     * caller 0xafff1c: get_ArrowCushion then bl with s0/s1 = speed. v3 corpse
     * removal: ReleaseArrows((0,2)) so cushioned arrows drop free. */
    constexpr uintptr_t ArrowCushion_ReleaseArrows   = 0x98a570;
    /* RoundLogic.SpawnTreasureChestsVersus (rl) — vanilla treasure spawn:
     * 3.1 calls it every ROUND START, and HoopTreasure adds it per goal.
     * REAL prologue = 0xb39790 (CanSpawnTreasure -> IsInOvertime ->
     * GetXMLPositions "TreasureChest"/"BigTreasureChest" — PC-exact shape).
     * metaresolve's 0xb39610 is a `b` INTO the middle of SpawnPlayersTeams
     * (0xb39070) with live registers — calling it was the goal-crash
     * (2026-07-07). Another mislabeled-DIRECT-body case like Flash. */
    constexpr uintptr_t RL_SpawnTreasureChestsVersus = 0xb39790;
    /* MainMenu.VersusMatchSettings static cell (cap-lift era note). BRUTE
     * statics: cell -> storage -> object (DOUBLE deref). */
    constexpr uintptr_t MainMenu_VersusMatchSettings = 0x4793398;
    /* ---- v3 CheckForTargetCollisions port (2026-07-07) --------------------
     * LevelEntity.ArrowCheck(Arrow) — metaresolve DIRECT, disasm-verified:
     * TargetCollider([this+0x98]) swap around CollideCheck. */
    constexpr uintptr_t LevelEntity_ArrowCheck      = 0xa81930;
    /* Player.set_State — metaresolve DIRECT: tail-calls StateMachine.set_State
     * (0x952ea0) on [player+0x140]; proper Begin/End state transition.
     * OnSqueakyBounce == set_State(Normal=0) + Speed = normal*5. */
    constexpr uintptr_t Player_set_State            = 0xaec4a0;
    /* Arrow.StartWiggle — metaresolve DIRECT (4 callers); the bounce visual. */
    constexpr uintptr_t Arrow_StartWiggle           = 0x988e90;
    /* Player.RemoveIndicator — metaresolve DIRECT (prologue verified: reads
     * [this+0xf8] then removes the PlayerIndicator entity). v3 fires it 60f
     * after a respawn so the "P1/P2" marker clears. */
    constexpr uintptr_t Player_RemoveIndicator      = 0xaef240;
    /* Player.get_CharacterIndex / get_AltSelect — DIRECT bodies (read the
     * TFGame.Characters / TFGame.AltSelect statics). Needed for the 3
     * special-skin archer bow-visibility quirk. */
    constexpr uintptr_t Player_get_CharacterIndex   = 0xae8030;
    constexpr uintptr_t Player_get_AltSelect        = 0xae80e0;
    /* ---- ball bounce dust (v3 OnCollideH/V) --------------------------------
     * ParticleSystem.Emit overloads, ABI proven at Player.OnCollideV's dust
     * call 0xaeb970: x0=ParticleSystem ([level+0x60] via get_Particles
     * 0xa7b610), x1=ParticleType, w2=amount, s0/s1=position, s2/s3=range
     * (+ s4=direction for the 5-arg one, prologue saves v0-v4). */
    constexpr uintptr_t ParticleSystem_Emit4        = 0x9428a0;
    constexpr uintptr_t ParticleSystem_Emit5        = 0x942af0;
    /* Particles.PlayerDust static cell (ParticleType[]; v3 dust = element 3)
     * — from Player.DustParticleType 0xaecbb0's Neutral branch (TeamDust is
     * the adjacent cell 0x47955f0). DOUBLE deref like all BRUTE statics. */
    constexpr uintptr_t Particles_PlayerDust        = 0x47955e8;
    /* ---- winning-team spotlight (v3 IncreaseScore end flow) ----------------
     * LightingLayer.SetSpotlight = literally `str x1,[x0,#0xa8]; ret` — the
     * render loop foreach's the array (data [arr+0x10] / len *[arr+0x20]),
     * so the proven fake-array pattern (variants row) applies directly.
     * Level.GetPlayerOrCorpse(level, w1=idx) prologue-verified. */
    constexpr uintptr_t LightingLayer_SetSpotlight  = 0xa9a310;
    /* (Level.GetPlayerOrCorpse already defined above — 0xa7fb00, matches.) */
    /* Sounds.sfx_devTimeFinalDummy static cell — v3's goal "swish" cue. Found
     * via the interned "SFX_DEVTIME_FINALDUMMY" literal (cell 0x47907f0) ->
     * SFX ctor site 0xb5baa4 -> result stored to [0x4793958]. DOUBLE deref. */
    constexpr uintptr_t Sounds_sfx_devTimeFinalDummy = 0x4793958;
}

typedef void (*DrawRectFn)(uint32_t packedColor, float x, float y, float w, float h);

/* Explosion.Spawn ABI (from invoke thunk 0x2763754): x0=level, w1=playerIndex,
 * w2/w3/w4 = the three bools (plusOneKill/triggerBomb/bombTrap — we always pass
 * 0), s0/s1 = position. */
typedef void* (*ExplosionSpawnFn)(void* level, int playerIndex, int b1, int b2, int b3,
                                  float posX, float posY);

/* Player ctor + inventory-struct signatures (see off::Player_Ctor notes). */
struct Inv2 { uint64_t a, b; };
typedef Inv2  (*GetInventoryFn)(void* session, int idx);
typedef int   (*GetHatFn)(void* session, int idx);
typedef void* (*PlayerCtorFn)(void* self, int playerIndex, int64_t allegiance, int64_t teamColor,
                              uint64_t invA, uint64_t invB, uint64_t hatState, uint32_t frozen,
                              uint32_t flash, uint32_t indicator, float posX, float posY);

/* VersusModeButton mode-cap patch sites: `cmp w9,#5` (0x7100153f) gating the
 * Right-scroll to mode 5. Rewrite to `cmp w9,#6` (0x7100193f) to reach mode 6.
 *   0xbb1e78 = Update Right-increment cap (CRITICAL — the actual selectability gate)
 *   0xbb1f5c = Update's inlined UpdateSides DrawRight  (arrow display)
 *   0xbb1bd4 = standalone UpdateSides DrawRight        (arrow display)
 *   0xbb1ab8 = ctor's inlined UpdateSides DrawRight    (arrow display)
 * Left cap (0xbb1ee0 `cmp #3`) left as-is. Encodings verified against main.flat. */
namespace modecap {
    constexpr uintptr_t sites[] = { 0xbb1e78, 0xbb1f5c, 0xbb1bd4, 0xbb1ab8 };
    constexpr uint32_t  CMP_W9_5 = 0x7100153f;   /* original */
    constexpr uint32_t  CMP_W9_6 = 0x7100193f;   /* patched: allow mode 6 */
}

/* Arrow.Create ABI (resolved statically off the invoke-thunk @0x26f0c08):
 *   w0=type(ArrowTypes int)  x1=owner(LevelEntity*)  x2=nChar(Nullable<int>,0=None)
 *   x3=nPlayer(Nullable<int>,0=None)  s0=pos.X  s1=pos.Y  s2=dir   ret x0=Arrow*
 * Int/float args are separate banks so a plain C signature maps 1:1. */
typedef void* (*ArrowCreateFn)(int type, void* owner, uint64_t nChar, uint64_t nPlayer,
                               float posX, float posY, float dir);

/* Read the current match Mode from any Player (Player->Level->Session->
 * MatchSettings->Mode). Returns -1 if any link is null. */
inline int32_t ModeFromPlayer(void* player) {
    auto rd = [](void* p, uint32_t o) -> void* {
        return p ? *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(p) + o) : nullptr;
    };
    void* level = rd(player, fld::Player_Level);
    void* sess  = rd(level,  fld::Level_Session);
    void* ms    = rd(sess,   fld::Session_MatchSettings);
    if (!ms) return -1;
    return *reinterpret_cast<int32_t*>(reinterpret_cast<uint8_t*>(ms) + fld::MatchSettings_Mode);
}

/* Walk Player -> Level -> Session -> RoundLogic. Returns nullptr on any null. */
inline void* RoundLogicFromPlayer(void* player) {
    if (!player) return nullptr;
    auto rd = [](void* p, uint32_t o) -> void* {
        return p ? *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(p) + o) : nullptr;
    };
    void* level   = rd(player, fld::Player_Level);
    void* session = rd(level,  fld::Level_Session);
    return rd(session, fld::Session_RoundLogic);
}

/* Turn a module-relative offset from tb::off into a callable function ptr. */
template <typename Fn>
inline Fn Call(uintptr_t off) {
    return reinterpret_cast<Fn>(exl::util::GetMainModuleInfo().m_Total.m_Start + off);
}

/* Scene.Add(entity) — the reconstructed inline (see tb::off::Layer_Add notes).
 * `scene` = a Level* (Level derives Scene); safe mid-frame (deferred add: the
 * entity goes into Layer.toAdd; the engine sets Entity.Scene and calls Added()
 * on the next UpdateEntityList pass). Returns false if any link is null. */
inline bool SceneAdd(void* scene, void* entity) {
    if (!scene || !entity) return false;
    void* layers = *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(scene) + fld::Scene_Layers);
    if (!layers) return false;
    int idx = *reinterpret_cast<int32_t*>(reinterpret_cast<uint8_t*>(entity) + fld::Entity_LayerIndex);
    void* layer = Call<void* (*)(void*, int)>(off::Dict_Int_GetItem)(layers, idx);
    if (!layer) return false;
    Call<void (*)(void*, void*, int)>(off::Layer_Add)(layer, entity, 0);
    return true;
}

/* Call a virtual method on `instance` through its vtable slot, replicating the
 * dispatch thunk exactly (untag the body ptr; apply the 0/0x10 this-adjust the
 * tag's top bits encode). Fn is the plain C signature with `void* this` first. */
template <typename Fn, typename... Args>
inline auto CallVirtual(void* instance, uint32_t slot, Args... args) {
    uintptr_t v0   = *reinterpret_cast<uintptr_t*>(instance);
    uintptr_t v1   = *reinterpret_cast<uintptr_t*>(v0 + 8);
    uintptr_t raw  = *reinterpret_cast<uintptr_t*>(v1 + slot);
    uintptr_t body = raw & 0x7fffffffffffffffULL;
    uintptr_t adj  = (raw >> 0x3b) & 0x10;
    return reinterpret_cast<Fn>(body)(
        reinterpret_cast<void*>(reinterpret_cast<uint8_t*>(instance) + adj), args...);
}

/*
 * Patch one virtual method's vtable slot to `detour`, returning the original
 * (untagged) body via *out_orig. `instance` is any live object of the class;
 * all instances share the method table, so one write hooks the method
 * class-wide. Idempotent per (table, slot): re-patching the same slot is a
 * no-op after the first call. Returns true on the write that installs it.
 *
 * Layout (decoded from the dispatch thunk):
 *   V0 = *(void**)instance          ; object's vtable ptr @ +0
 *   V1 = *(void**)(V0 + 8)          ; method table
 *   *(void**)(V1 + slot)            ; tagged body ptr (untag with the mask)
 */
/* Call a stored TAGGED original slot value: strip the body tag and apply the
 * 0/0x10 this-adjust encoded in bit 63 — exactly what the dispatch thunks do.
 * EVERY orig call from a HookVirtual detour MUST go through this. */
template <typename Fn, typename... Args>
inline auto CallTagged(uintptr_t rawSlot, void* self, Args... args) {
    uintptr_t body = rawSlot & 0x7fffffffffffffffULL;
    uintptr_t adj  = (rawSlot >> 0x3b) & 0x10;
    return reinterpret_cast<Fn>(body)(
        reinterpret_cast<void*>(reinterpret_cast<uint8_t*>(self) + adj), args...);
}

/*
 * NEVER PATCH CLASS VTABLES (HookVirtual is gone — 2026-07-06 crash class).
 * BRUTE's delegate ctor (0x9840f0) REVERSE-RESOLVES vtable slot VALUES back to
 * method records when a ctor builds bound delegates (e.g. Arrow's OnCollideH/V
 * delegates from slot indices 0x39/0x3a). A foreign pointer written into a
 * shared method table makes that lookup return null and the very next instance
 * construction crashes reading [null+0x18]. This killed every ball respawn
 * (ToyArrow #2, ~90 frames after round start).
 *
 * Hook virtual methods by resolving the BODY from a live instance's vtable and
 * installing a normal exlaunch trampoline on the body (InstallAtPtr). The
 * table is never written, delegate resolution stays intact, and delegate-bound
 * invocations hit the hook too (delegates bind the concrete body pointer).
 */

/* Read one virtual slot off a live instance: returns the UNTAGGED body address
 * (absolute, InstallAtPtr-ready) and stores the 0/0x10 this-adjust the tag
 * encodes into *adj_out. Returns 0 on any null link. Detours installed on the
 * body receive the ADJUSTED this — subtract the adjust to get the object. */
inline uintptr_t ResolveVirtualBody(void* instance, uint32_t slot, uintptr_t* adj_out) {
    if (instance == nullptr) return 0;
    uintptr_t v0 = *reinterpret_cast<uintptr_t*>(instance);
    if (v0 == 0) return 0;
    uintptr_t v1 = *reinterpret_cast<uintptr_t*>(v0 + 8);
    if (v1 == 0) return 0;
    uintptr_t raw  = *reinterpret_cast<uintptr_t*>(v1 + slot);
    uintptr_t body = raw & 0x7fffffffffffffffULL;
    if (body < 0x1000) return 0;                      /* sanity */
    if (adj_out) *adj_out = (raw >> 0x3b) & 0x10;
    return body;
}

} // namespace tb
