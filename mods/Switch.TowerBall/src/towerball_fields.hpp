/*
 * TowerBall port — game struct FIELD offsets, enums, and pure helpers.
 * HOST-SAFE: no exlaunch includes — this header is shared between the device
 * build (via towerball.hpp) and the macOS host test harness
 * (tools/towerball/host_test.cpp), which fakes game objects at these offsets.
 * All offsets are for TowerFall Switch v1.4.1.6 and were resolved/validated as
 * documented in docs/TOWERBALL-OFFSETS.md.
 */
#pragma once
#include <cstdint>

namespace tb {

namespace fld {
    /* Monocle.Entity */
    constexpr uint32_t Entity_PositionX = 0x14;   /* float; Position = Vector2 @ 0x14 */
    constexpr uint32_t Entity_PositionY = 0x18;   /* float */
    constexpr uint32_t Entity_Scene     = 0x20;   /* Scene* (get_Scene) */
    constexpr uint32_t Entity_LayerIndex= 0x54;   /* int (getter 0x92dd20) */
    /* TowerFall.Player */
    constexpr uint32_t Player_Level       = 0x68;  /* Level* (cached; == Scene, typed) */
    constexpr uint32_t Player_Arrows      = 0x100; /* ArrowList* */
    constexpr uint32_t Player_Dead        = 0x108; /* bool */
    constexpr uint32_t Player_PlayerIndex = 0xe4;  /* int (0-3) */
    constexpr uint32_t Player_Facing      = 0xe8;  /* int (+1 right / -1 left) */
    constexpr uint32_t Player_OnGround    = 0xec;  /* bool */
    constexpr uint32_t Player_TeamColor   = 0x10c; /* Allegiance int (-1 Neutral/0/1) */
    /* LevelEntity.Allegiance — Player ctor 0xae69dc stores the allegiance arg
     * as a BYTE at +0xb0 AND +0x138 (two copies, adjacent strb w25). The chest
     * ball spawn temporarily neutralizes the owner (PC BasketBallTreasureChest). */
    constexpr uint32_t Entity_Allegiance  = 0xb0;  /* int8 (-1 Neutral/0/1) */
    /* LevelEntity.Flashing — plain bool auto-property (get_Flashing 0xa80f80:
     * ldrb [this+0xc0]). Flashing entities are immune to explosion Hurt
     * (CanHurtIgnoreDodge). Wrapped around the dunk explosion (user decision:
     * dunks must not kill the dunker). */
    constexpr uint32_t Entity_Flashing    = 0xc0;  /* bool */
    constexpr uint32_t Player_Allegiance2 = 0x138; /* int8 (second copy) */
    constexpr uint32_t Player_StateMachine= 0x140; /* StateMachine* */
    constexpr uint32_t Player_Aiming      = 0x179; /* bool */
    constexpr uint32_t Player_AimDirection= 0x120; /* float radians (ldr s0,[x0,#0x120]) */
    constexpr uint32_t Player_ShieldObj   = 0x220; /* PlayerShield*; HasShield = [obj+0x22] */
    constexpr uint32_t Player_BowSprite   = 0x218; /* Sprite<string>* (SetBowRotation 0xaeb690) */
    /* Player.input (InputState struct) @ 0x148 — decl-order bools; JumpPressed
     * confirmed by 3-way read-count match with PC DuckingUpdate */
    constexpr uint32_t Player_JumpPressed = 0x149; /* bool */
    constexpr uint32_t Component_Visible  = 0x22;  /* bool (UpdateAnimation: strb wzr,[bow+0x22]) */
    /* Level / Session chain */
    constexpr uint32_t Level_Session         = 0x48;  /* Session* */
    constexpr uint32_t Level_Ending          = 0x88;  /* bool (OrbLogic.DoDarkOrb: ldrb [lvl+0x88]) */
    constexpr uint32_t Level_CanEnd          = 0x89;  /* bool (RoundLogic.FinalKill: ldrb [lvl+0x89]) */
    constexpr uint32_t Level_OrbLogic        = 0x80;  /* OrbLogic* (all DoSlowMoKill callers: ldr [lvl+0x80]) */
    constexpr uint32_t Session_MatchSettings = 0x20;  /* MatchSettings* */
    constexpr uint32_t Session_MatchStats    = 0x28;  /* MatchStats[] (stride 0x2a8) */
    constexpr uint32_t Session_Scores        = 0x30;  /* List<int>* (items@+0x10 -> data) */
    constexpr uint32_t Session_RoundLogic    = 0x58;  /* RoundLogic* */
    constexpr uint32_t Session_CurrentLevel  = 0x68;  /* Level* */
    constexpr uint32_t MatchSettings_Mode    = 0x30;  /* Modes enum (int) */
    constexpr uint32_t MatchSettings_Teams   = 0x38;  /* teams data (GetPlayerAllegiance reads it) */
    constexpr uint32_t MatchSettings_LevelSystem = 0x28; /* LevelSystem* (FinalKill tail) */
    constexpr uint32_t RoundLogic_Session    = 0x10;  /* Session* (base ctor str x1,[this,#0x10]) */
    /* base RoundLogic ctor 0xb37c0c does `strb w24,[rl,#0x50]` right before the
     * miasmaCounter setup at [rl+0x58] — PC field order CanMiasma;miasmaCounter.
     * PC TowerBallRoundLogic sets CanMiasma=false (no purple curtain). */
    constexpr uint32_t RoundLogic_CanMiasma  = 0x50;  /* bool */
    /* Monocle GraphicsComponent: Origin Vector2 — every arrow-class
     * CreateGraphics stores its fmov'd origin pair via `stur x9,[img,#0x2c]` */
    constexpr uint32_t Img_OriginX           = 0x2c;  /* float */
    constexpr uint32_t Img_OriginY           = 0x30;  /* float */
    constexpr uint32_t Img_ScaleX            = 0x34;  /* float (validated == 1.0f at ball
                                                       * image discovery before any write) */
    constexpr uint32_t Img_ScaleY            = 0x38;  /* float */
    constexpr uint32_t Img_Rotation          = 0x40;  /* float (bowSprite notes) */
    /* TowerFall.Arrow */
    constexpr uint32_t Arrow_PlayerIndex   = 0xf8;  /* int */
    constexpr uint32_t Arrow_SpeedX        = 0x140; /* Vector2 Speed (UpdateSeeking 0x986290 stores;
                                                     * cross-confirmed by Die(Arrow) reading it) */
    constexpr uint32_t Arrow_SpeedY        = 0x144;
    constexpr uint32_t Entity_Active       = 0x10;  /* bool (Arrow.Unfreeze 0x989260) */
    constexpr uint32_t Entity_Collidable   = 0x12;  /* bool */
    constexpr uint32_t Arrow_Frozen        = 0x119; /* bool */
    /* Arrow.State (ArrowStates enum int) — get_State accessor: ldr w0,[x0,#0xe4].
     * Shooting=0, Gravity=2 (v3 checks State==2 for rolling friction). */
    constexpr uint32_t Arrow_State         = 0xe4;
    /* PlayerCorpse.ArrowCushion (getter 0xafa600: ldr x0,[x0,#0x110]) */
    constexpr uint32_t Corpse_ArrowCushion = 0x110;
    /* Arrow.CannotHit (LevelEntity*) — from Arrow.SeekPrioritize 0x988c60:
     * `ldr x8,[x20,#0x138]; cmp x8,x19` (target != CannotHit). */
    constexpr uint32_t Arrow_CannotHit     = 0x138;
    /* Arrow hitbox objects (from Arrow ctor 0x983df0: WrapHitbox stores).
     * NormalHitbox @0x180 (dflt 2x2), otherArrowHitbox @0x188 (dflt 4x4) —
     * the latter is the arrow-vs-arrow collision box. v3 MyArrow widens the
     * BALL's to 6x6 / 12x12 so a fired arrow reliably connects and knocks it
     * (vanilla Arrow.Update: opposing dangerous arrows both EnterFallMode). */
    constexpr uint32_t Arrow_NormalHitbox     = 0x180;
    constexpr uint32_t Arrow_OtherArrowHitbox = 0x188;
    /* Monocle.Hitbox fields (base Hitbox ctor 0x934aa0: stp s11,s10,[x+0x20]
     * = W,H ; stp s9,s8,[x+0x18] = offX,offY). */
    constexpr uint32_t Hitbox_OffX   = 0x18;
    constexpr uint32_t Hitbox_OffY   = 0x1c;
    constexpr uint32_t Hitbox_Width  = 0x20;
    constexpr uint32_t Hitbox_Height = 0x24;
    /* Player.Speed (Vector2) — Player.OnCollideH 0xaeb7f0 is exactly
     * `str wzr,[x0,#0x2c8]` (Speed.X = 0). */
    constexpr uint32_t Player_SpeedX       = 0x2c8;
    constexpr uint32_t Player_SpeedY       = 0x2cc;
    /* LevelEntity.LightRadius (float) — Arrow ctor sets 40 here, Player 50,
     * torches 25, bosses 90; verified same offset across all classes. v3
     * BasketBall raises the ball to 80. */
    constexpr uint32_t Entity_LightRadius  = 0xa4;
    /* Level.Particles (ParticleSystem*) — get_Particles 0xa7b610:
     * ldr x0,[x0,#0x60]. */
    constexpr uint32_t Level_Particles     = 0x60;
    /* Level.LightingLayer — get_LightingLayer 0xa7b670: ldr x0,[x0,#0xc0]. */
    constexpr uint32_t Level_LightingLayer = 0xc0;
    /* Monocle.StateMachine.State — get_State 0x952e90: ldr w0,[x0,#0x24].
     * Player.get_State chains through Player.StateMachine (0x140). */
    constexpr uint32_t StateMachine_State  = 0x24;
    /* MatchVariants (Variants = [ms+0x40], from UpdateSeeking's chain).
     * Variant.Value = bool [variant+0x58] (op_Implicit 0xb9d9e0). Field slots
     * are NOT declaration-ordered (reflection init) — identified at RUNTIME by
     * matching Variant.Title strings (see main.cpp ScanVariants). Known:
     * GunnStyle=+0x30, NoSeekingArrows=+0x128 (anchors). */
    constexpr uint32_t MatchSettings_Variants = 0x40;
    constexpr uint32_t Variant_Value          = 0x58;
    /* Managed runtime layouts (BRUTE) */
    constexpr uint32_t List_Items          = 0x10;  /* backing array OBJECT ptr (List<T>) */
    constexpr uint32_t List_Count          = 0x18;  /* int32 */
    constexpr uint32_t Array_Data          = 0x10;  /* element data ptr */
    constexpr uint32_t Array_LenPtr        = 0x20;  /* ptr to int32 length */
    constexpr uint32_t String_Length       = 0x10;  /* int32 */
    constexpr uint32_t String_Chars        = 0x14;  /* UTF-16 data */
    /* Scene.Add reconstruction */
    constexpr uint32_t Scene_Layers        = 0x18;  /* Dictionary<int,Layer>* (getter 0x943c40) */
    /* VersusLevelSystem (from GenLevels body 0xbae7a0) */
    constexpr uint32_t VLS_Tower           = 0x28;  /* VersusTowerData* */
    constexpr uint32_t VLS_Levels          = 0x30;  /* List<string>* — round order */
    constexpr uint32_t VLS_LastLevel       = 0x38;  /* string */
}

/* ---- VIRTUAL method vtable slots (stable across builds) ---------------- */
namespace slot {
    constexpr uint32_t Update      = 0x40;
    constexpr uint32_t Added       = 0x30;
    constexpr uint32_t OnCollideH  = 0x1c8;
    constexpr uint32_t OnCollideV  = 0x1d0;
    constexpr uint32_t Arrow_Drop  = 0x208;  /* Arrow.Drop(int) — confirmed via thunk 0x26f2cc8 */
    constexpr uint32_t Arrow_ShootUpdate   = 0x1a8;  /* metaresolve VIRTUAL 424 (matches the
                                                      * 0x986cc4 dispatch in Arrow.Update) */
    constexpr uint32_t Arrow_EnterFallMode = 0x200;  /* wrapper 0x26f2a14: ldr [x8,#0x200] */
    /* Arrow.get_IsCollectible — metaresolve VIRTUAL 552 (byte 0x228); ToyArrow
     * inherits it. PC BasketBall overrides: collectible = Speed.Length() < 2
     * (cantCollectCounter is vestigial — JustSpawned is never called). */
    constexpr uint32_t Arrow_IsCollectible  = 0x228;
    /* Arrow.CheckForTargetCollisions — metaresolve VIRTUAL (both Arrow and the
     * ToyArrow override report slot 0x1b8). The vanilla ToyArrow body bounces
     * EVERY player (players are GameTags.Target); v3 REPLACES it with the
     * pickup-or-team-bounce logic — hooked like the other ball bodies. */
    constexpr uint32_t Arrow_CheckForTargets = 0x1b8;
    /* LevelEntity.OnExplode/OnExplodePush(Explosion, Vector2) — slots read off
     * their own metadata dispatch wrappers 0x278b76c / 0x278b830. v3: the ball
     * no-ops OnExplode/DoSquish/HitLava; the carrier drops the ball in
     * Player.OnExplode. */
    constexpr uint32_t Entity_OnExplode     = 0x98;
    constexpr uint32_t Entity_OnExplodePush = 0xa0;
    constexpr uint32_t Arrow_DoSquish       = 0x188;  /* metaresolve VIRTUAL 392 */
    constexpr uint32_t Arrow_HitLava        = 0x1c0;  /* metaresolve VIRTUAL 448 */
    constexpr uint32_t RL_OnUpdate          = 0x28;
    constexpr uint32_t RL_OnRoundStart      = 0x30;  /* confirmed: all dispatch thunks say 48 */
    constexpr uint32_t RL_OnLevelLoadFinish = 0x20;  /* confirmed via Session dispatch 0xb455a0 */
    /* OnPlayerDeath(player x1, corpse x2, playerIndex w3, cause w4, killer w5,
     * pos s0/s1) — slot read off dispatch wrapper 0x276fa18: ldr [x8,#0x38] */
    constexpr uint32_t RL_OnPlayerDeath     = 0x38;
    constexpr uint32_t LS_StopVersusMusic   = 0x68;  /* LevelSystem virtual (FinalKill tail) */
    constexpr uint32_t SFX_Play             = 0x20;  /* SFX virtual: (this, float x, float vol) */
}

/* TowerFall.Modes enum values (native-confirmed: get_TeamMode == Mode==5). */
namespace mode {
    enum Modes : int32_t {
        Quest = 0, DarkWorld = 1, Trials = 2, LastManStanding = 3, HeadHunters = 4,
        TeamDeathmatch = 5, Warlord = 6, EditorTest = 7, EditorPreview = 8, LevelTest = 9,
    };
}

/* TowerFall.DeathCause (PC decl order == values). */
namespace cause {
    enum DeathCause : int32_t {
        Arrow = 0, Explosion = 1, Brambles = 2, JumpedOn = 3, Lava = 4, Shock = 5,
        SpikeBall = 6, FallingObject = 7, Squish = 8, Curse = 9, Miasma = 10,
        Enemy = 11, Chalice = 12,
    };
}

/* TowerFall.ArrowTypes (PC decl order == enum values; base game). */
namespace arrow {
    enum ArrowTypes : int32_t {
        Normal = 0, Bomb = 1, SuperBomb = 2, Laser = 3, Bramble = 4, Drill = 5,
        Bolt = 6, Toy = 7, Feather = 8, Trigger = 9, Prism = 10,
    };
}

/* Tiny raw-memory helpers shared by device logic and host tests. */
inline void*    RdPtr (void* p, uint32_t off) { return p ? *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(p) + off) : nullptr; }
inline int32_t  RdI32 (void* p, uint32_t off) { return *reinterpret_cast<int32_t*>(reinterpret_cast<uint8_t*>(p) + off); }
inline float    RdF32 (void* p, uint32_t off) { return *reinterpret_cast<float*>(reinterpret_cast<uint8_t*>(p) + off); }
inline void     WrF32 (void* p, uint32_t off, float v) { *reinterpret_cast<float*>(reinterpret_cast<uint8_t*>(p) + off) = v; }
inline void     WrU8  (void* p, uint32_t off, uint8_t v) { *(reinterpret_cast<uint8_t*>(p) + off) = v; }
inline uint8_t  RdU8  (void* p, uint32_t off) { return *(reinterpret_cast<uint8_t*>(p) + off); }
inline void     WrI32 (void* p, uint32_t off, int32_t v) { *reinterpret_cast<int32_t*>(reinterpret_cast<uint8_t*>(p) + off) = v; }

/* Read the match Mode straight from a Session*. Returns -1 on any null link. */
inline int32_t ModeFromSession(void* session) {
    void* ms = RdPtr(session, fld::Session_MatchSettings);
    if (!ms) return -1;
    return RdI32(ms, fld::MatchSettings_Mode);
}

} // namespace tb
