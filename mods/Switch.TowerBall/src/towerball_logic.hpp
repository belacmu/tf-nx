/*
 * TowerBall gameplay state machine — HOST-SAFE (no exlaunch includes).
 *
 * All mode-6 game logic lives here, driven through tb::env (a small table of
 * function pointers). On device, main.cpp binds env to the real game calls
 * (Arrow.Create, SceneAdd, RemoveSelf, virtual Drop, Level.GetPlayer, log).
 * On macOS, tools/towerball/host_test.cpp binds env to a fake world whose
 * objects are byte buffers laid out at the tb::fld offsets — so pickup/throw/
 * goal/death/level-swap sequences run as unit tests without the game.
 *
 * Frame-order contract (matches the real engine, and what the mocks simulate):
 *   frame N:  UpdateEntityLists (deferred adds get their Scene)  →  entity
 *   updates (player input: OnShoot/OnCollect/OnDie fire here)    →  RoundLogic
 *   OnUpdate (OnRLUpdate here). So an entity added during frame N has a null
 *   Scene until frame N+1 — hence the adoption grace period.
 */
#pragma once
#include "towerball_fields.hpp"
#include "towerball_levels.hpp"

namespace tb {

struct Env {
    void* (*GetPlayer)(void* level, int idx);                       /* Level.GetPlayer */
    void* (*ArrowCreate)(int type, void* owner, float x, float y, float dir);
    bool  (*SceneAdd)(void* level, void* entity);                   /* deferred add */
    void  (*RemoveSelf)(void* entity);                              /* deferred remove */
    void  (*ArrowDrop)(void* arrow, int side);                      /* virtual Drop */
    void  (*Log)(const char* fmt, long a, long b, long c, long d);
    /* new Player(idx, pos, ...) + Level.Add — the respawn primitive */
    void* (*NewPlayer)(void* session, void* level, int idx, int allegiance, float x, float y);
    int   (*GoalScore)(void* matchSettings);                        /* get_GoalScore */
    int   (*GetAllegiance)(void* session, int idx);                 /* -1 Neutral / 0 Blue / 1 Red */
    void  (*DrawRect)(float x, float y, float w, float h, uint32_t rgba); /* Monocle.Draw.Rect */
    void  (*SlowMo)(void* level);                                   /* OrbLogic.DoSlowMoKill */
    void  (*Explode)(void* level, float x, float y, int playerIndex); /* Explosion.Spawn, bools=0 */
    /* Draw an atlas subtexture centered at (x,y), tinted rgba (white = as-is).
     * Keys resolve+cache device-side via Atlas.get_Item; draws nothing until
     * the runtime string machinery is ready (see main.cpp MakeString). */
    void  (*DrawTex)(int key, float x, float y, uint32_t rgba);
    void  (*EndRound)(void* session);                               /* Session.EndRound */
    void  (*Squeak)(float x, float vol);    /* Sounds.env_arrowToyChar.Play (ball bounce) */
    /* RoundLogic.AddScore — MUST be used for scoring (emits GainPointEvent;
     * the results screen fills win pips from those events) */
    void  (*AddScore)(void* rl, int scoreIndex, int amount);
    int   (*GetHighest)(void* session);     /* Session.GetHighestScore */
    /* Draw.OutlineTextCentered with a runtime StaticString (key -> table) */
    void  (*DrawText)(int key, float x, float y, uint32_t rgba, float scale);
    /* Level.CollideFirst POINT query (GameTags: Solid=3, JumpThru=5) */
    void* (*CollideAt)(void* level, float x, float y, int tags);
    void  (*Flash)(void* entity, int frames);   /* LevelEntity.Flash(frames, null) */
    void  (*ShootUpdate)(void* arrow);      /* Arrow.ShootUpdate virtual (forceState) */
    void  (*ReleaseArrows)(void* cushion, float vx, float vy);  /* ArrowCushion */
    void  (*SpawnTreasure)(void* rl);       /* RL.SpawnTreasureChestsVersus (HoopTreasure) */
    /* v3 CheckForTargetCollisions primitives */
    bool  (*ArrowCheck)(void* entity, void* arrow);  /* LevelEntity.ArrowCheck (TargetCollider swap) */
    void  (*SetPlayerState)(void* player, int state);/* Player.set_State (StateMachine transition) */
    void  (*StartWiggle)(void* arrow);               /* Arrow.StartWiggle (bounce visual) */
    void  (*GoalSound)(float x);                     /* Sounds.sfx_devTimeFinalDummy.Play (v3 goal cue) */
    void  (*RemoveIndicator)(void* player);          /* Player.RemoveIndicator (60f after respawn) */
    bool  (*AimingBowSkin)(void* player);            /* true = special-skin archer currently aiming */
    /* Particles.Emit(PlayerDust[3], amount, pos, range[, direction]) — v3 ball
     * bounce dust. withDir=false uses the 4-arg overload. */
    void  (*EmitDust)(void* level, int amount, float px, float py,
                      float rx, float ry, float dir, bool withDir);
    /* v3 win flow: spotlight the winning team's players/corpses during the
     * end slow-mo (LightingLayer.SetSpotlight via fake managed array). */
    void  (*Spotlight)(void* level, void* session, int team);
    /* v3 IncreaseScore: PlayerInputs[scorer].Rumble(1f, 10) on every goal.
     * Device: CallVirtual slot 0x28 on PlayerInputs[i] (s0=strength, w1=frames).
     * May be null on hosts that predate it — call sites must null-check. */
    void  (*Rumble)(int playerIndex, float strength, int frames);
};

/* DrawText keys */
enum TextKey { TextDunk = 0, TextOvertime = 1, TextKeyCount = 2 };

/* DrawTex keys (device maps them to atlas name + atlas kind) */
enum TexKey { TexRim = 0, TexNet = 1, TexBall = 2, TexTeamA = 3, TexTeamB = 4,
              TexRimR = 5, TexNetR = 6, TexChest = 7,
              TexNetS1 = 8, TexNetS2 = 9, TexNetS3 = 10,      /* 0.5/0.7/0.85 squash */
              TexNetRS1 = 11, TexNetRS2 = 12, TexNetRS3 = 13,
              TexKeyCount = 14 };
extern Env env;    /* defined by main.cpp (device) or host_test.cpp (host) */

namespace logic {

/* ---------------- state ---------------- */
inline void* g_session      = nullptr;   /* current mode-6 session (null otherwise) */
inline void* g_ball         = nullptr;
inline bool  g_ballSeen     = false;     /* ball observed IN the scene (deferred add done).
                                          * Scene==null before that = still waiting for the
                                          * add flush (can take MANY frames during kill
                                          * hitstop/freeze) — never abandon a ball that was
                                          * never seen; that made a second one (2026-07-07) */
inline float g_prevX = 0, g_prevY = 0;   /* last tracked ball position (goal sweep) */
inline bool  g_prevValid    = false;
inline void* g_level        = nullptr;   /* current level (cached each OnRLUpdate; HUD reads it) */
inline int   g_ballCooldown = 0;         /* frames until (re)spawn */
/* Combined mod: TowerBall scales to 8 players (4v4). Per-player state and all
 * player-index loops/guards use kMaxPlayers instead of the old literal 4/3.
 * Additive — at <=4 players indices 4..7 stay empty, so <=4 play is unchanged. */
constexpr int kMaxPlayers = 8;
inline bool  g_hasBall[kMaxPlayers]   = {};  /* per-player HOLDING flag */
inline int   g_respawn[kMaxPlayers]   = {};  /* frames until player respawns (0 = alive) */
inline int   g_indicator[kMaxPlayers] = {};  /* frames until the P#-indicator is removed (v3: 60f) */
inline bool  g_endGame      = false;     /* goal score reached — let the round end */
inline bool  g_roundLive    = false;     /* set at OnRoundStart; no ball spawns before it
                                          * (PC: the chest ball appears per ROUND — the old
                                          * pre-round spawn left a second untracked ball) */
inline int   g_towerIdx     = -1;        /* set when mode-6 round order generated */
inline bool  g_towerWide    = false;
inline int   g_goalsLogged  = 0;
inline bool  g_hudDrawn     = false;     /* one scoreboard per frame (HUDRender fires per player) */
inline bool  g_teamsLogged  = false;     /* per-session team-assignment diagnostic (task #11) */
inline float g_flashAlpha   = 0.0f;      /* white screen flash on goal (PC hud.flashAlpha) */
inline int   g_endCounter   = 0;         /* v3: Counter(90) after the winning goal, then
                                          * Session.EndRound() every frame */
/* v3-EXACT interception (TowerBallv3 MyArrow.Create — the FortRise port lost
 * these assignments): every ball gets ThrownTeam = owner.Allegiance at Create;
 * a NEUTRAL owner (chest spawn) also gets JustSpawned() = 10-frame no-collect.
 * Wrong-team players touching a collectible ball make it BOUNCE off them
 * (squeak) and turn it Neutral — one bounce, then anyone can take it. */
inline int   g_thrownTeam   = -1;        /* -1 = Neutral */
inline int   g_ballNoCollect = 0;        /* v3 cantCollectCounter equivalent */
/* v3 slam: every ball Create starts slamming=Counter(15); it zeroes the moment
 * the ball is not moving DOWN fast (Speed.Y <= 0.1). A goal while it is still
 * live = SLAM: explosion at the hoop + the DUNK! notification. (Broken in the
 * 3.1 port — the counter was never re-armed per throw.) */
inline int   g_slamFrames   = 0;
/* v3 MyToyArrow physics state (the Update tail RESTORES Speed.X from
 * prevXSpeed every frame — collisions flip prevXSpeed, not Speed.X) */
inline float g_prevXSpeed      = 0.0f;
inline bool  g_fallModeEntered = false;
/* v3 MyPlayerCorpse: corpses removed 150f after death (then Flash(60) -> gone;
 * port removes at 210f, no flash — arrows cushioned on the corpse are lost,
 * documented deviation until ArrowCushion.ReleaseArrows is resolved). */
inline void* g_corpse[8]       = {};
inline int   g_corpseFrames[8] = {};
/* v3 DeadBall: cosmetic flashing ball tossed out on every score; bounces off
 * Solids (X *-0.8, Y *-0.5, gravity capped 5.5), gone after 60 frames. */
struct DeadBall { bool active; float x, y, vx, vy; int frames, flash; bool visible; };
/* v3 DoNetJump: net Scale.Y tweens 0.5 -> 1.0 (BounceOut, 40f) on every goal/
 * dunk at that basket — approximated with 3 baked squash frames. */
inline int g_netJump[2] = {};
/* 3.1 custom variants, carried by vanilla toggles on Switch (no custom-variant
 * UI exists natively): SUDDEN DEATH -> TimedRounds, BOTTOMLESS TREASURE ->
 * HoopTreasure. Flags set at session start (main.cpp ScanVariants). */
inline bool g_timedRounds  = false;
inline bool g_hoopTreasure = false;
inline bool g_overtime     = false;
inline int  g_secondsLeft  = 0;
inline int  g_frameAcc     = 0;
inline DeadBall g_deadBall[2] = {};
constexpr int kTagSolid = 3, kTagJumpThru = 5;
/* v3 BasketBall.scaleWiggler = Wiggler.Create(20, 4f): started on the player-
 * bounce branch of CheckForTargetCollisions; ballImage.Scale = 1 + 0.2*Value,
 * Value = cos(sineCounter)*Counter, sineCounter += 2pi*4/60 per frame, Counter
 * 1 -> 0 over 20 frames. Deterministic -> baked as a table (index = frames
 * since Start; [0] is applied on the Start frame itself, exactly v3's
 * onChange(1)). Ticked device-side in BallArrowUpdateHook next to the
 * rotation write (needs g_ballImg + the Scale-offset validation). -1 = idle. */
constexpr float kBallWiggle[21] = {
    1.0000f,  0.8678f,  0.6022f,  0.2627f, -0.0836f, -0.3750f, -0.5663f,
   -0.6358f, -0.5869f, -0.4450f, -0.2500f, -0.0470f,  0.1236f,  0.2342f,
    0.2741f,  0.2500f,  0.1827f,  0.1004f,  0.0309f, -0.0052f,  0.0000f };
inline int g_scaleWigStep = -1;
/* SlamNotification ("DUNK!"): yellow, scale 2, flashes 3f on / 3f off, 120f */
inline float g_notifX = 0, g_notifY = 0;
inline int   g_notifFrames  = 0;
inline int   g_notifFlash   = 0;
constexpr int kGoalLogMax   = 64;
constexpr int kRespawnDelay = 180;       /* v3 PlayerRespawner Counter(180) — user pick
                                          * (3.1 changed it to 70) */
/* v3 ball timing (user pick): round-start chest = SpawnBallChest(1) → opens
 * 60f after RoundStarted + Flash(30) + 1 ≈ 91f. After goals/dunks the chest
 * comes from the 300-frame alarm, THEN opens: ≈ 391f total. */
constexpr int kBallRespawn     = 91;     /* round start (chest already placed) */
constexpr int kBallRespawnGoal = 300;    /* 5s — user pick (v3 would be ~6.5s) */
inline uint32_t g_rng = 0x12345;         /* PC uses new Random() for spawn picks */
inline uint32_t NextRand() { g_rng = g_rng * 1664525u + 1013904223u; return g_rng >> 16; }

/* level-path patch registry (canonical strings — must be restorable) */
inline struct PathPatch { void* str; uint16_t c0, c1; } g_lvlPatch[24];
inline int g_lvlPatchCount = 0;

/* ---------------- mode masquerade ----------------
 * BRUTE inlines `Mode == 5` (TeamMode/allegiance) checks all over the game, so
 * hooks on get_TeamMode/GetPlayerAllegiance can't reach them (symptom: both
 * teams spawned with default outlines). Instead, while a TOWERBALL session is
 * live we write MatchSettings.Mode = 5 — every inlined check then runs the
 * full TeamDeathmatch team pipeline natively. Our own logic keys off g_session
 * (set only for true mode-6 sessions), never off the masked Mode value.
 * The mask is restored to 6 before any new GetRoundLogic (rematch safety) and
 * when the menu mode-select renders again (GetModeIcon). */
inline void* g_maskedMS = nullptr;       /* the MatchSettings we set to 5 */

inline void MaskMode(void* ms) {
    if (!ms) return;
    WrI32(ms, fld::MatchSettings_Mode, mode::TeamDeathmatch);
    g_maskedMS = ms;
}
inline void UnmaskMode() {
    if (!g_maskedMS) return;
    /* value-preserving: only restore 6 if our mask value (5) is still there —
     * the menu may have legitimately overwritten Mode since */
    if (RdI32(g_maskedMS, fld::MatchSettings_Mode) == mode::TeamDeathmatch)
        WrI32(g_maskedMS, fld::MatchSettings_Mode, mode::Warlord);
    g_maskedMS = nullptr;
}

/* ---------------- helpers ---------------- */
inline bool InMode6() { return g_session != nullptr; }   /* session-keyed, mask-proof */

inline int PlayerIdx(void* player) { return RdI32(player, fld::Player_PlayerIndex); }

inline bool AnyoneHolding() {
    for (int i = 0; i < kMaxPlayers; i++) if (g_hasBall[i]) return true;
    return false;
}

inline bool CurrentMarkers(lvl::TowerMarkers const** out) {
    if (g_towerIdx < 0 || g_towerIdx >= 16) return false;
    *out = g_towerWide ? &lvl::wide[g_towerIdx] : &lvl::std_[g_towerIdx];
    return true;
}

inline float LevelW() { return g_towerWide ? 420.0f : 320.0f; }

/* PC BasketBallBasket geometry: platform top-left = marker + (-10,+10) left /
 * (-5,+10) right, 15px wide. The hoop opening is the strip just above it. */
inline void HoopZone(lvl::V2 m, bool rightSide, float* x0, float* x1, float* y0, float* y1) {
    float px = m.x + (rightSide ? -5.0f : -10.0f);
    *x0 = px + 1.0f; *x1 = px + 14.0f;
    float py = m.y + 10.0f;
    *y0 = py - 5.0f; *y1 = py + 3.0f;
}

/* ---------------- state transitions ---------------- */
inline void ResetBallState(int spawnDelay) {
    g_ball = nullptr;
    g_ballSeen = false;
    g_prevValid = false;
    g_ballCooldown = spawnDelay;
    g_endGame = false;
    g_endCounter = 0;
    g_flashAlpha = 0.0f;
    g_thrownTeam = -1;
    g_ballNoCollect = 0;
    g_slamFrames = 0;
    g_prevXSpeed = 0.0f;
    g_fallModeEntered = false;
    g_notifFrames = 0;
    for (int i = 0; i < 8; i++) { g_corpse[i] = nullptr; g_corpseFrames[i] = 0; }
    g_deadBall[0].active = g_deadBall[1].active = false;
    g_netJump[0] = g_netJump[1] = 0;
    for (int i = 0; i < kMaxPlayers; i++) { g_hasBall[i] = false; g_respawn[i] = 0; g_indicator[i] = 0; }
    g_scaleWigStep = -1;
}

/* Session bootstrap (GetRoundLogic hook). Mode 6 → adopt; otherwise clear so a
 * stale session pointer can never satisfy InMode6 later. */
inline void OnSessionStart(void* session, bool isMode6) {
    g_roundLive = false;                    /* no balls until the first OnRoundStart */
    g_timedRounds = g_hoopTreasure = g_overtime = false;   /* rescanned per session */
    g_secondsLeft = 0;
    if (isMode6) {
        g_session = session;
        g_teamsLogged = false;
        ResetBallState(kBallRespawn);
        MaskMode(RdPtr(session, fld::Session_MatchSettings));
    } else {
        g_session = nullptr;
        UnmaskMode();                       /* a non-TowerBall session owns the ms now */
        ResetBallState(0);
    }
}

inline void OnRoundStart(void* rl) {
    g_roundLive = true;
    /* 3.1 OnRoundStart: base.OnRoundStart(); SpawnTreasureChestsVersus(); —
     * vanilla treasure chests every round (levels carry TreasureChest
     * markers; the body no-ops when CanSpawnTreasure is off). */
    if (rl) env.SpawnTreasure(rl);
    if (g_timedRounds) {                    /* 3.1: secondsleft = 30 * GoalScore */
        void* ms = g_session ? RdPtr(g_session, fld::Session_MatchSettings) : nullptr;
        g_secondsLeft = ms ? 30 * env.GoalScore(ms) : 150;
        g_frameAcc = 0;
        g_overtime = false;
    }
    ResetBallState(kBallRespawn);           /* chest pipeline: ball ~91f after FIGHT */
    if (!g_teamsLogged && g_session) {
        g_teamsLogged = true;
        env.Log("[towerball] teams: p0=%ld p1=%ld p2=%ld p3=%ld",
                env.GetAllegiance(g_session, 0), env.GetAllegiance(g_session, 1),
                env.GetAllegiance(g_session, 2), env.GetAllegiance(g_session, 3));
    }
}

/* (The old pinned indicator ENTITY is gone: the held ball is now drawn as the
 * real ball sprite riding the carrier — see RenderHUD. PC MyPlayer parity.) */

/* Toy arrows we spawned ourselves (chest / throw / death drop) become THE
 * tracked ball. v3's ball is a DISTINCT arrow type (7) — a stray vanilla toy
 * arrow (TOY ARROWS variant, toy-arrow pickups) must NOT be adopted, so the
 * spawn sites set g_adoptNext right before their Arrow.Create call. v3
 * MyArrow.Create bookkeeping: ThrownTeam = owner.Allegiance; Neutral owner
 * (chest) -> JustSpawned(10). */
inline bool g_adoptNext = false;
inline bool g_spawnNeutral = false;      /* set by the CHEST spawn site: this create is a
                                          * Neutral drop (don't derive a ThrownTeam) */

/* Set one of the arrow's WrapHitbox objects (w,h,offX,offY). */
inline void SetHitbox(void* arrow, uint32_t slot, float w, float h) {
    void* hb = RdPtr(arrow, slot);
    if (!hb) return;
    WrF32(hb, fld::Hitbox_Width,  w);
    WrF32(hb, fld::Hitbox_Height, h);
    WrF32(hb, fld::Hitbox_OffX,   -w * 0.5f);
    WrF32(hb, fld::Hitbox_OffY,   -h * 0.5f);
}
/* v3 MyArrow: the BALL gets NormalHitbox 6x6 + otherArrowHitbox 12x12 so a
 * fired arrow reliably overlaps and knocks it (arrow-vs-arrow EnterFallMode).
 * Non-ball toy arrows restore the vanilla 2x2 / 4x4 — the ball pool is shared,
 * so a recycled ball-turned-pickup must not keep the fat boxes. */
inline void SetBallHitboxes(void* arrow, bool ball) {
    if (ball) { SetHitbox(arrow, fld::Arrow_NormalHitbox, 6.0f, 6.0f);
                SetHitbox(arrow, fld::Arrow_OtherArrowHitbox, 12.0f, 12.0f); }
    else      { SetHitbox(arrow, fld::Arrow_NormalHitbox, 2.0f, 2.0f);
                SetHitbox(arrow, fld::Arrow_OtherArrowHitbox, 4.0f, 4.0f); }
}

inline void OnArrowCreated(void* arrow_, int type, void* owner) {
    bool adopt = g_adoptNext;
    bool neutral = g_spawnNeutral;
    g_adoptNext = false;                    /* consumed by ANY create — never leaks */
    g_spawnNeutral = false;
    if (arrow_ && type == arrow::Toy && InMode6())
        SetBallHitboxes(arrow_, adopt);     /* ball -> fat boxes; stray toy -> vanilla */
    if (arrow_ && type == arrow::Toy && InMode6() && adopt) {
        g_ball = arrow_;
        g_ballSeen = false;                 /* deferred SceneAdd — not in scene yet */
        g_prevValid = false;
        /* v3: ThrownTeam = owner.Allegiance (chest spawns are Neutral). The
         * session lobby team is the reliable allegiance source on device —
         * the Entity allegiance byte read Neutral in mode 6, which let
         * enemies catch each other's throws (2026-07-07). */
        int alleg = -1;
        if (!neutral && owner) {
            int oidx = PlayerIdx(owner);
            if (oidx >= 0 && oidx < kMaxPlayers && g_session)
                alleg = env.GetAllegiance(g_session, oidx);
        }
        g_thrownTeam = alleg;
        g_scaleWigStep = -1;                        /* fresh ball image: scale 1.0, pulse idle */
        g_ballNoCollect = (alleg == -1) ? 10 : 0;   /* v3 JustSpawned() */
        g_slamFrames = 15;                          /* v3: slamming = Counter(15) */
        g_prevXSpeed = RdF32(arrow_, fld::Arrow_SpeedX);
        WrF32(arrow_, fld::Entity_LightRadius, 80.0f);  /* v3 BasketBall LightRadius */
    }
}

/* CollectArrows hook decision: -1 = pass through to Orig, 0 = return false,
 * 1 = return true (collected → ball now held; the entity removes itself).
 * EXACT PC MyPlayer.CollectArrows: lone ball type → HasBasketBall = 1, true.
 * No holding cap, no team rule — PC has neither (ThrownTeam is never assigned
 * in the mod, and collect gating is done by IsCollectible: Speed < 2, which
 * the game itself consults before ever calling CollectArrows — hooked on
 * device via the ball's get_IsCollectible body). */
inline int OnCollect(void* player, int32_t const* types, int32_t count) {
    if (!InMode6() || !types || count != 1 || types[0] != arrow::Toy) return -1;
    int idx = PlayerIdx(player);
    if (idx < 0 || idx >= kMaxPlayers) return -1;
    /* CollectArrows has non-Player callers (audit 2026-07-13: 0x9908c4/0x990ab8
     * besides Arrow.CheckForTargets) — e.g. the ReturnAsGhosts variant's ghost
     * picking up a landed toy arrow. A ghost is NOT in the level's player list,
     * so verify the collector really is player `idx` before handing it the
     * ball; anything else falls through to the vanilla collect. */
    if (!g_level || env.GetPlayer(g_level, idx) != player) return -1;
    /* v3 MyPlayer.CollectArrows: lone ball type -> HasBasketBall = true. No
     * team gate here — that lives in CheckForTargetCollisions (the only path
     * that reaches a moving ball; a landed one was already neutralized). */
    g_hasBall[idx] = true;
    g_ball = nullptr;                       /* entity removes itself; ball is held */
    g_prevValid = false;
    env.Log("[towerball] player %ld picked up the ball", idx, 0, 0, 0);
    return 1;
}

/* v3 MyToyArrow.CheckForTargetCollisions — EXACT port. Replaces the vanilla
 * ToyArrow override (players are GameTags.Target, so the vanilla body squeaky-
 * bounces EVERY player every frame — the ball is pinned in Shooting, hence
 * always Dangerous, hence this runs constantly). v3 semantics: overlap =
 * pickup at ANY speed; the bounce fires only for a dodging player or a
 * wrong-team player on a team-owned ball, and neutralizes the ball.
 * (3.1 difference, NOT ported: 3.1 deletes the bounce branch and the dodge
 * gate — wrong-team players pass through. v3 is the design reference.)
 * Returns: -1 = not ours (call Orig), 0 = return false, 1 = return true. */
inline int OnBallCheckTargets(void* self) {
    if (!InMode6() || !self || self != g_ball) return -1;
    if (g_ballNoCollect > 0) return 0;      /* v3: if (cantCollectCounter) return false */
    void* level = RdPtr(self, fld::Player_Level);   /* LevelEntity.Level (shared offset) */
    if (!level) return 0;
    void* cannotHit = RdPtr(self, fld::Arrow_CannotHit);
    for (int i = 0; i < kMaxPlayers; i++) {
        void* player = env.GetPlayer(level, i);
        if (!player || player == cannotHit) continue;
        if (!env.ArrowCheck(player, self)) continue;
        void* sm = RdPtr(player, fld::Player_StateMachine);
        int state = sm ? RdI32(sm, fld::StateMachine_State) : 0;
        /* v3: player.Allegiance == ThrownTeam — session lobby teams on device.
         * USER RULE (2026-07-07, replaces v3's `State != 3` no-catch gate): a
         * DODGING player (state 3) catches ANY ball, even an enemy throw —
         * dodge works as an interception move. */
        int alleg = g_session ? env.GetAllegiance(g_session, i) : -1;
        if (state == 3 || alleg == g_thrownTeam || g_thrownTeam == -1) {
            /* player.CollectArrows([Toy]); RemoveSelf(); return false */
            g_hasBall[i] = true;
            g_ball = nullptr;
            g_prevValid = false;
            env.Log("[towerball] player %ld picked up the ball", i, 0, 0, 0);
            env.RemoveSelf(self);
            return 0;
        }
        /* bounce: val = (Sign(player.X - X), Sign(player.Speed.Y) or -1) */
        float bx = RdF32(self, fld::Entity_PositionX);
        float px = RdF32(player, fld::Entity_PositionX);
        float vy = RdF32(player, fld::Player_SpeedY);
        float dx = (px > bx) ? 1.0f : ((px < bx) ? -1.0f : 0.0f);
        float dy = (vy > 0.0f) ? 1.0f : ((vy < 0.0f) ? -1.0f : 0.0f);
        if (dy == 0.0f) dy = -1.0f;
        float inv = (dx != 0.0f) ? 0.70710678f : 1.0f;   /* SafeNormalize(dx,dy) */
        float nx = dx * inv, ny = dy * inv;
        /* player.OnSqueakyBounce(this, n): State = Normal; Speed = n * 5 */
        env.SetPlayerState(player, 0);
        WrF32(player, fld::Player_SpeedX, nx * 5.0f);
        WrF32(player, fld::Player_SpeedY, ny * 5.0f);
        /* Speed = n * 2; prevXSpeed = Speed.X; wiggle + squeak */
        WrF32(self, fld::Arrow_SpeedX, nx * 2.0f);
        WrF32(self, fld::Arrow_SpeedY, ny * 2.0f);
        g_prevXSpeed = nx * 2.0f;
        env.StartWiggle(self);
        g_scaleWigStep = 0;                 /* v3: scaleWiggler.Start() (scale pulse) */
        env.Squeak(bx, 1.0f);
        g_ballNoCollect = 15;
        g_thrownTeam = -1;                  /* one bounce -> ball is Neutral */
        return 1;
    }
    return 0;
}

/* PC BasketBall.IsCollectible (effective): Speed.Length() < 2. Bound to the
 * ball's get_IsCollectible body on device — the vanilla pickup path
 * (CheckForTargetCollisions → ArrowCheck → CollectArrows) then behaves
 * exactly like PC, including taking a slow-rolling ball mid-bounce. */
inline bool BallCollectible(void* self) {
    if (g_ballNoCollect > 0) return false;  /* v3 cantCollectCounter */
    float vx = RdF32(self, fld::Arrow_SpeedX);
    float vy = RdF32(self, fld::Arrow_SpeedY);
    return vx * vx + vy * vy < 4.0f;
}

/* ShootArrow hook: true = handled (ball thrown; suppress the original shot). */
inline bool OnShoot(void* player) {
    int idx = PlayerIdx(player);
    if (!InMode6() || idx < 0 || idx >= kMaxPlayers || !g_hasBall[idx]) return false;
    void* level = RdPtr(player, fld::Player_Level);
    float px  = RdF32(player, fld::Entity_PositionX);
    float py  = RdF32(player, fld::Entity_PositionY);
    float aim = RdF32(player, fld::Player_AimDirection);
    g_adoptNext = true;                     /* this create IS the ball */
    void* ball = env.ArrowCreate(arrow::Toy, player, px, py - 1.0f, aim);
    if (!ball || !level || !env.SceneAdd(level, ball)) return false;
    g_hasBall[idx] = false;
    /* The suppressed original ShootArrow would have done `Aiming = false` —
     * without it the shooting state machine fires AGAIN next frame with the
     * hold flag now cleared, releasing a normal arrow alongside the ball
     * (user-confirmed double-fire). Clear it ourselves. */
    WrU8(player, fld::Player_Aiming, 0);
    env.Log("[towerball] player %ld threw the ball", idx, 0, 0, 0);
    return true;
}

/* v3 AddDeadBall(pos, speed) */
inline void AddDeadBall(float x, float y, float vx, float vy) {
    for (auto& d : g_deadBall) {
        if (d.active) continue;
        d = { true, x, y, vx, vy, 60, 3, true };
        return;
    }
}

/* v3 DeadBall.Update, point-collision approximation of the 6x6 WrapHitbox */
inline void TickDeadBalls(void* level) {
    for (auto& d : g_deadBall) {
        if (!d.active) continue;
        if      (d.vx >  0.025f) d.vx -= 0.025f;   /* Calc.Approach(vx, 0, 0.025) */
        else if (d.vx < -0.025f) d.vx += 0.025f;
        else                     d.vx  = 0.0f;
        d.vy = d.vy + 0.2f; if (d.vy > 5.5f) d.vy = 5.5f;
        d.x += d.vx;
        float sideX = d.x + (d.vx >= 0 ? 3.0f : -3.0f);
        if (env.CollideAt(level, sideX, d.y, kTagSolid)) {
            d.x -= d.vx;
            d.vx *= -0.8f;
        }
        d.y += d.vy;
        if (d.vy > 0 && env.CollideAt(level, d.x, d.y + 3.0f, kTagSolid)) {
            for (int i = 0; i < 6 && env.CollideAt(level, d.x, d.y + 3.0f, kTagSolid); i++)
                d.y -= 1.0f;
            d.vy *= -0.5f;
        }
        if (--d.flash <= 0) { d.flash = 3; d.visible = !d.visible; }
        if (--d.frames <= 0) d.active = false;
    }
}

/* v3 AddSlamNotification: "DUNK!" at pos.X + 7 (ctor adds it), flashing. */
inline void AddSlamNotification(float x, float y) {
    g_notifX = x + 7.0f;
    g_notifY = y;
    g_notifFrames = 120;
    g_notifFlash = 3;
}

/* Score a goal for `team` (v3 IncreaseScore): AddScore(team, 1) through the
 * GAME's method — it also emits the GainPointEvent the results screen needs to
 * fill the win pips (raw Scores writes = empty pips + no match end). Endgame
 * check is v3-exact: Session.GetHighestScore() >= MatchSettings.GoalScore. */
inline void DoGoal(void* rl, void* sess, void* level, int team, int scorer, long bx, long by) {
    env.GoalSound((float)bx);               /* v3: sfx_devTimeFinalDummy.Play(X, 1) at the goal
                                             * sites — BEFORE the end-window guard */
    if (g_endGame) return;                  /* v3 IncreaseScore: if (endCounter != null) return */
    env.AddScore(rl, team, 1);
    /* v3 IncreaseScore: PlayerInputs[scorer].Rumble(1f, 10) on every goal */
    if (env.Rumble && scorer >= 0 && scorer < kMaxPlayers)
        env.Rumble(scorer, 1.0f, 10);
    long newScore = env.GetHighest(sess);   /* for the log; scorer just reached it or not */
    if (g_goalsLogged < kGoalLogMax) {
        env.Log("[towerball] GOAL! team=%ld highest=%ld (at %ld,%ld)", team, newScore, bx, by);
        g_goalsLogged++;
    }
    g_flashAlpha = 1.0f;                    /* PC: hud.flashAlpha = 1 on every score */
    void* ms = RdPtr(sess, fld::Session_MatchSettings);
    int goal = ms ? env.GoalScore(ms) : 0;
    void* rl2 = RdPtr(sess, fld::Session_RoundLogic);
    /* 3.1 IncreaseScore flow: untimed -> end at GoalScore; timed -> goals never
     * end UNTIL overtime, then ANY goal ends. Non-ending goals spawn vanilla
     * treasure when HoopTreasure is on. */
    bool ends;
    if (!g_timedRounds) ends = (goal > 0 && env.GetHighest(sess) >= goal);
    else                ends = g_overtime;
    if (ends) {
        g_endGame = true;
        g_endCounter = 90;                  /* v3: endCounter(90) -> Session.EndRound */
        env.Spotlight(level, sess, team);   /* v3: spotlight the winners... */
        env.SlowMo(level);                  /* ...then slow-mo + music stop + final-kill sting */
        env.Log("[towerball] GAME! team %ld ends it (highest=%ld)", team, env.GetHighest(sess), 0, 0);
    } else if (g_hoopTreasure && rl2) {
        env.SpawnTreasure(rl2);             /* 3.1 HoopTreasure: chests per goal */
    }
}

/* Dunk (PC MyPlayer.DuckingUpdate): carrier + JUMP pressed while standing ON
 * the basket platform → score, explosion, and the player intentionally drops
 * THROUGH the hoop (PC even pushes Position.Y += 6 to force it — the vanilla
 * jumpthru drop-through is part of the move, don't suppress it). PC detects
 * "standing on" via CollideCheck(JumpThru, pos+1px); we use a position band
 * around the platform top wide enough for the player-origin convention. */
inline void OnDucking(void* player) {
    int idx = PlayerIdx(player);
    if (!InMode6() || idx < 0 || idx >= kMaxPlayers || !g_hasBall[idx] || g_endGame) return;
    if (!RdU8(player, fld::Player_JumpPressed)) return;   /* PC: dunk = duck + JUMP on the hoop */
    lvl::TowerMarkers const* mk = nullptr;
    if (!CurrentMarkers(&mk)) return;
    float x = RdF32(player, fld::Entity_PositionX);
    float y = RdF32(player, fld::Entity_PositionY);
    for (int b = 0; b < 2; b++) {
        float px = mk->basket[b].x + (b == 1 ? -5.0f : -10.0f);
        float py = mk->basket[b].y + 10.0f;
        if (x >= px - 2.0f && x <= px + 17.0f && y >= py - 14.0f && y <= py + 3.0f) {
            void* level = RdPtr(player, fld::Player_Level);
            void* sess  = level ? RdPtr(level, fld::Level_Session) : nullptr;
            if (!level || !sess) return;    /* validate BEFORE consuming the ball */
            /* PC: !CollideCheck(Solid, pos + (0,3)) — no dunk with solid ground
             * right under the feet (feet ~= pos.y + 6 for the player hitbox) */
            if (env.CollideAt(level, x, y + 9.0f, kTagSolid)) return;
            g_hasBall[idx] = false;
            env.Log("[towerball] DUNK by player %ld!", idx, 0, 0, 0);
            /* USER DECISION (Switch port): the slam explosion must not kill the
             * scorer. Hurt(Explosion) is gated by !Flashing, but the kill sweep
             * runs in Explosion.Added() — a DEFERRED add, flushed later in the
             * frame — so a set/restore of the Flashing byte here is undone too
             * early (that was the "dunk still kills" bug). Use the game's own
             * Flash(frames): stays set across the sweep, auto-clears. */
            env.Flash(player, 5);
            env.Explode(level, px + 7.5f, py, idx);  /* PC slam explosion at the hoop */
            AddSlamNotification(px + (b == 1 ? -10.0f : 10.0f), py);
            AddDeadBall(x, y, 0.0f, 0.0f);  /* v3 uses player Speed; not tracked */
            g_netJump[b] = 40;              /* v3 DoNetJump */
            void* rl2 = RdPtr(sess, fld::Session_RoundLogic);
            DoGoal(rl2, sess, level, (b == 0) ? 1 : 0, idx, (long)x, (long)y);
            g_ballCooldown = kBallRespawnGoal;   /* v3 SpawnBallChest(300) + open */
            /* PC: push the dunker THROUGH the hoop (Position.Y += 6; +10 more
             * with a shield) — the drop-through is part of the move */
            float pushY = y + 6.0f;
            void* shield = RdPtr(player, fld::Player_ShieldObj);
            if (shield && RdU8(shield, 0x22)) pushY += 10.0f;
            WrF32(player, fld::Entity_PositionY, pushY);
            return;
        }
    }
}

/* Drop the ball at the (dying) carrier's position — PC DropBall parity. */
/* Two PC drop variants:
 *  - death (straight=false): DropBall(p, pos+ArrowOffset, facing) — dir by
 *    facing, Drop((int)facing), Speed.Y *= 0.5 (a small toss).
 *  - explode (straight=true): DropBall(p, pos) — dir 3π/2, Drop(0), then
 *    Speed.X = Speed.Y = 0 (ball just falls straight, no momentum). */
inline void DropBallAt(void* player, bool straight = false) {
    void* level = RdPtr(player, fld::Player_Level);
    float px = RdF32(player, fld::Entity_PositionX);
    float py = RdF32(player, fld::Entity_PositionY);
    int facing = RdI32(player, fld::Player_Facing);
    float dir = straight ? 4.712389f : ((facing == 1) ? 0.0f : 3.14159265f);
    g_adoptNext = true;                     /* this create IS the ball */
    void* ball = env.ArrowCreate(arrow::Toy, player, px, py - 1.0f, dir);
    if (ball && level && env.SceneAdd(level, ball)) {
        env.ArrowDrop(ball, straight ? 0 : facing);
        if (straight) {                     /* PC OnExplode drop: zero momentum */
            WrF32(ball, fld::Arrow_SpeedX, 0.0f);
            WrF32(ball, fld::Arrow_SpeedY, 0.0f);
        } else {                            /* PC death drop: half the toss arc */
            WrF32(ball, fld::Arrow_SpeedY, RdF32(ball, fld::Arrow_SpeedY) * 0.5f);
        }
        env.Log("[towerball] ball dropped at %ld,%ld", (long)px, (long)py, 0, 0);
    }
}

/* RoundLogic.OnPlayerDeath detour (mode 6 REPLACES Warlord's elimination logic,
 * PC parity: TowerBallRoundLogic.OnPlayerDeath does not call the versus base).
 * Returns true when handled (caller must skip Orig).
 * Carrier death: stomp kills HAND the ball to the stomper (PC MyPlayer.Die:
 * cause==JumpedOn -> HasBasketBall[killer]++); anything else drops it. */
inline bool OnRLPlayerDeath(void* player, int playerIndex, int deathCause, int killerIndex) {
    if (!InMode6()) return false;
    if (playerIndex >= 0 && playerIndex < kMaxPlayers) {
        if (g_hasBall[playerIndex]) {
            g_hasBall[playerIndex] = false;
            if (deathCause == cause::JumpedOn && killerIndex >= 0 && killerIndex < kMaxPlayers &&
                killerIndex != playerIndex) {
                g_hasBall[killerIndex] = true;   /* stomp steal — indicator re-pins next frame */
                env.Log("[towerball] player %ld stomped the carrier - takes the ball", killerIndex, 0, 0, 0);
            } else if (player) {
                DropBallAt(player);
            }
        }
        if (!g_endGame)
            g_respawn[playerIndex] = kRespawnDelay;
    }
    return true;
}

/* v3 MyPlayerCorpse: track corpses; remove them 210 frames after death. */
inline void OnCorpseSpawned(void* corpse) {
    if (!InMode6() || !corpse) return;
    for (int i = 0; i < 8; i++)
        if (!g_corpse[i]) { g_corpse[i] = corpse; g_corpseFrames[i] = 210; return; }
}

inline void TickCorpses() {
    for (int i = 0; i < 8; i++) {
        if (!g_corpse[i]) continue;
        /* LIVENESS FIRST (deep fix, 2026-07-13): retail can remove a corpse
         * long before our 210-frame timer — Exploding Corpses detonates it,
         * lava/bottomless swallow it, arrows can finish it — and our stored
         * pointer goes stale (the InvalidMemoryRegion crash: Flash() on freed
         * memory ~150f after an exploded corpse). An out-of-scene entity is
         * out of play: drop tracking the moment Scene (+0x20) reads null, the
         * same per-frame validation the ball already gets. This covers EVERY
         * current and future corpse-removing variant, not one symptom. */
        if (!RdPtr(g_corpse[i], fld::Entity_Scene)) {
            g_corpse[i] = nullptr;
            g_corpseFrames[i] = 0;
            continue;
        }
        --g_corpseFrames[i];
        if (g_corpseFrames[i] == 60)
            env.Flash(g_corpse[i], 60);     /* v3: Flash(60) then remove */
        if (g_corpseFrames[i] <= 0) {
            void* cushion = RdPtr(g_corpse[i], fld::Corpse_ArrowCushion);
            if (cushion) env.ReleaseArrows(cushion, 0.0f, 2.0f);   /* v3 RemoveMe */
            env.RemoveSelf(g_corpse[i]);
            g_corpse[i] = nullptr;
        }
    }
}

/* v3 MyPlayer.OnExplode: a carrier hit by an explosion DROPS the ball (even if
 * a shield saves them), then the vanilla explosion handling runs. */
inline void OnPlayerExploded(void* player) {
    if (!InMode6() || !player) return;
    int idx = PlayerIdx(player);
    if (idx < 0 || idx >= kMaxPlayers || !g_hasBall[idx]) return;
    g_hasBall[idx] = false;
    DropBallAt(player, /*straight=*/true);  /* PC OnExplode: straight drop, no toss */
    env.Log("[towerball] player %ld exploded - ball dropped", idx, 0, 0, 0);
}

/* PC RespawnPlayer: TeamSpawnA for Blue (0), TeamSpawnB for Red (1); Neutral
 * (shouldn't happen with TeamMode on — fall back to index parity). Rotates
 * through the level's spawn points. */
inline void DoRespawn(void* sess, void* level, int idx) {
    int alleg = env.GetAllegiance(sess, idx);
    bool sideB = (alleg == 1) || (alleg < 0 && (idx % 2) == 1);
    float x = LevelW() * 0.5f, y = 60.0f;
    lvl::TowerMarkers const* mk = nullptr;
    if (CurrentMarkers(&mk)) {
        lvl::V2 const* list = sideB ? mk->spawnB : mk->spawnA;
        int n = sideB ? mk->nB : mk->nA;
        if (n > 0) { lvl::V2 p = list[NextRand() % (uint32_t)n]; x = p.x; y = p.y + 2.0f; }
    }
    void* p = env.NewPlayer(sess, level, idx, alleg, x, y);
    if (p) env.Flash(p, 120);               /* v3 RespawnPlayer: Flash(120) */
    g_indicator[idx] = 60;                  /* v3: Alarm.Set(60, RemoveIndicator) */
    env.Log("[towerball] respawned player %ld team %ld at %ld,%ld",
            idx, alleg, (long)x, (long)y);
}

/* 3x5 rect-font: one uint16 per digit, 15 bits row-major top-to-bottom.
 * Lets the scoreboard show real numbers with Draw.Rect only — no strings. */
constexpr uint16_t kDigitFont[10] = {
    0b111101101101111,  /* 0 */
    0b010110010010111,  /* 1 */
    0b111001111100111,  /* 2 */
    0b111001111001111,  /* 3 */
    0b101101111001001,  /* 4 */
    0b111100111001111,  /* 5 */
    0b111100111101111,  /* 6 */
    0b111001001010010,  /* 7 */
    0b111101111101111,  /* 8 */
    0b111101111001111,  /* 9 */
};

inline void DrawDigit(float x, float y, int d, float px, uint32_t color) {
    if (d < 0 || d > 9) return;
    uint16_t bits = kDigitFont[d];
    for (int row = 0; row < 5; row++)
        for (int col = 0; col < 3; col++)
            if (bits & (1u << (14 - (row * 3 + col))))
                env.DrawRect(x + col * px, y + row * px, px, px, color);
}

/* Right-anchored 1-2 digit number (cap 99), pixel size px. */
inline void DrawScore(float cx, float cy, int value, float px, uint32_t color) {
    if (value < 0) value = 0;
    if (value > 99) value = 99;
    int tens = value / 10, ones = value % 10;
    float dw = 3 * px, gap = px;
    float total = tens ? (dw * 2 + gap) : dw;
    float x = cx - total * 0.5f, y = cy - 2.5f * px;
    if (tens) { DrawDigit(x, y, tens, px, color); x += dw + gap; }
    DrawDigit(x, y, ones, px, color);
}

/* Scoreboard (PC TowerBallHUD): team-colored boxes at the top with the numeric
 * score — pure Draw.Rect, no managed strings needed. Called from the first
 * Player.HUDRender of each frame (the HUD sprite batch is active there — same
 * injection point the PC mod used). */
inline void RenderHUD() {
    if (!g_session || g_hudDrawn) return;
    g_hudDrawn = true;
    /* goal flash: full-screen white fading out (premultiplied grey ramp) */
    if (g_flashAlpha > 0.0f) {
        uint32_t v = (uint32_t)(g_flashAlpha * 255.0f) & 0xFF;
        env.DrawRect(0.0f, 0.0f, LevelW(), 240.0f, v | (v<<8) | (v<<16) | (v<<24));
        g_flashAlpha -= 0.04f;              /* PC decay rate */
        if (g_flashAlpha < 0.0f) g_flashAlpha = 0.0f;
    }
    /* basket rims + nets: the mod's real sprites (PC BasketBallBasket anchors:
     * rim origin (1,1) at the platform corner, net origin (-3,-2)) */
    lvl::TowerMarkers const* mk = nullptr;
    if (CurrentMarkers(&mk)) {
        for (int b = 0; b < 2; b++) {
            float px = mk->basket[b].x + (b == 1 ? -5.0f : -10.0f);
            float py = mk->basket[b].y + 10.0f;
            /* v3 OutlineImage: sprite drawn at 8 one-pixel offsets in the
             * outline color, then normally on top. Net outline = black; rim
             * outline = team-ish tint: left (164,228,252), right (248,120,88). */
            uint32_t rimOutline = b ? 0xFF5878F8u : 0xFFFCE4A4u;
            /* net-jump squash frame (BounceOut approx over 40f):
             * 40..33 half, 33..26 0.85, 26..18 full, 18..10 0.85 dip, 10..0 full */
            int nj = g_netJump[b];
            int netKey = b ? TexNetR : TexNet;
            float netH = 9.0f;
            if      (nj > 33) { netKey = b ? TexNetRS1 : TexNetS1; netH = 4.0f; }
            else if (nj > 26) { netKey = b ? TexNetRS3 : TexNetS3; netH = 8.0f; }
            else if (nj > 18) { }
            else if (nj > 10) { netKey = b ? TexNetRS3 : TexNetS3; netH = 8.0f; }
            float netCy = py + 2.0f + netH * 0.5f;   /* top-anchored squash */
            for (int oy = -1; oy <= 1; oy++)
                for (int ox = -1; ox <= 1; ox++) {
                    if (!ox && !oy) continue;
                    env.DrawTex(netKey, px + 8.5f + ox, netCy + oy, 0xFF000000u);
                }
            for (int oy = -1; oy <= 1; oy++)
                for (int ox = -1; ox <= 1; ox++) {
                    if (!ox && !oy) continue;
                    env.DrawTex(b ? TexRimR : TexRim, px + 7.0f + ox, py + 3.5f + oy, rimOutline);
                }
            env.DrawTex(netKey, px + 8.5f, netCy, 0xFFFFFFFFu);
            env.DrawTex(b ? TexRimR : TexRim, px + 7.0f, py + 3.5f, 0xFFFFFFFFu);
        }
    }
    /* team banners (PC: teamA at x=5, teamB at 282/382, y=0; 33x40, top-left
     * anchored → centered draw at +16.5,+20) */
    env.DrawTex(TexTeamA, 5.0f + 16.5f, 20.0f, 0xFFFFFFFFu);
    env.DrawTex(TexTeamB, (g_towerWide ? 382.0f : 282.0f) + 16.5f, 20.0f, 0xFFFFFFFFu);
    /* held ball rides each carrier (PC renders per holding player: Position +
     * ArrowOffset + (facing*4, 2), ball image origin = its center) */
    if (g_level) {
        for (int holder = 0; holder < kMaxPlayers; holder++) {
            if (!g_hasBall[holder]) continue;
            void* carrier = env.GetPlayer(g_level, holder);
            if (!carrier) continue;
            float cx = RdF32(carrier, fld::Entity_PositionX);
            float cy = RdF32(carrier, fld::Entity_PositionY);
            int facing = RdI32(carrier, fld::Player_Facing);
            env.DrawTex(TexBall, cx + facing * 4.0f, cy + 1.0f, 0xFFFFFFFFu);
        }
    }
    for (auto& d : g_deadBall)              /* v3 DeadBall render (blinking) */
        if (d.active && d.visible) env.DrawTex(TexBall, d.x, d.y, 0xFFFFFFFFu);
    /* v3 chest visual: the chest 'spawns' 91 frames before the ball (open 60 +
     * flash 30 + 1), blinking for its first 15 and last 30 frames (Flash(15) on
     * appear, Flash(30) on open), then vanishes as the ball pops out. */
    if (g_roundLive && !g_endGame && !g_ball && !AnyoneHolding() &&
        g_ballCooldown > 0 && g_ballCooldown <= 91) {
        lvl::TowerMarkers const* mkc = nullptr;
        if (CurrentMarkers(&mkc)) {
            int t = g_ballCooldown;             /* 91 .. 1 */
            bool blinkPhase = (t > 76) || (t <= 30);   /* appear / open flash */
            bool show = !blinkPhase || ((t / 3) & 1);  /* 3-frame blink */
            if (show)
                env.DrawTex(TexChest, mkc->chest.x, mkc->chest.y, 0xFFFFFFFFu);
        }
    }
    /* SlamNotification: flashing DUNK! text (v3: Counter(120), blink 3f) */
    if (g_notifFrames > 0) {
        g_notifFrames--;
        if (--g_notifFlash <= 0) g_notifFlash = 6;   /* 3 on / 3 off */
        if (g_notifFlash > 3)
            env.DrawText(TextDunk, g_notifX, g_notifY, 0xFF00FFFFu, 2.0f);  /* yellow */
    }
    /* 3.1 timed HUD: yellow clock box top-center (m:ss) + OVERTIME! text.
     * BLACK digits on the yellow face (white-on-yellow was near-illegible, and
     * on dark levels the box also read as fogged) — matches shot-clock look and
     * pops through the level's ambient darkness. Thicker black outer border so
     * it stays crisp against the fog at the top-center vignette. */
    if (g_timedRounds) {
        float cxm = (LevelW() * 0.5f);
        constexpr uint32_t CLK_FACE = 0xFF5AFFFFu;   /* (255,255,90) yellow */
        constexpr uint32_t CLK_INK  = 0xFF000000u;   /* black digits */
        env.DrawRect(cxm - 28.0f, 4.0f, 56.0f, 22.0f, 0xFF000000u);  /* 2px black border */
        env.DrawRect(cxm - 25.0f, 7.0f, 50.0f, 16.0f, CLK_FACE);
        int mm = g_secondsLeft / 60, ss = g_secondsLeft % 60;
        DrawDigit(cxm - 17.0f, 10.0f, mm, 2.0f, CLK_INK);
        env.DrawRect(cxm - 9.0f, 12.0f, 2.0f, 2.0f, CLK_INK);    /* colon */
        env.DrawRect(cxm - 9.0f, 17.0f, 2.0f, 2.0f, CLK_INK);
        DrawDigit(cxm - 4.0f, 10.0f, ss / 10, 2.0f, CLK_INK);
        DrawDigit(cxm + 4.0f, 10.0f, ss % 10, 2.0f, CLK_INK);
        if (g_overtime)
            env.DrawText(TextOvertime, cxm, 40.0f, 0xFFFFFFFFu, 2.0f);
    }
    void* scores = RdPtr(g_session, fld::Session_Scores);   /* int[] object */
    void* items  = scores ? RdPtr(scores, fld::Array_Data) : nullptr;
    int32_t* lenp = scores ? reinterpret_cast<int32_t*>(RdPtr(scores, fld::Array_LenPtr)) : nullptr;
    int32_t cnt  = lenp ? *lenp : 0;
    int a = (items && cnt > 0) ? reinterpret_cast<int32_t*>(items)[0] : 0;
    int b = (items && cnt > 1) ? reinterpret_cast<int32_t*>(items)[1] : 0;
    constexpr uint32_t BLACK = 0xFF000000, WHITE = 0xFFFFFFFF;
    constexpr uint32_t BLUE  = 0xFF584000;   /* PC colorA (0,64,88)  */
    constexpr uint32_t RED   = 0xFF001488;   /* PC colorB (136,20,0) */
    float rx = g_towerWide ? 348.0f : 248.0f;
    struct { float x; uint32_t col; int score; } box[2] = {
        { 38.0f, BLUE, a }, { rx, RED, b } };
    for (auto& bx : box) {
        env.DrawRect(bx.x,        5.0f, 34.0f, 20.0f, BLACK);
        env.DrawRect(bx.x + 1.0f, 6.0f, 32.0f, 18.0f, WHITE);
        env.DrawRect(bx.x + 2.0f, 7.0f, 30.0f, 16.0f, bx.col);
        DrawScore(bx.x + 17.0f, 15.0f, bx.score, 2.0f, WHITE);   /* box center */
    }
}

/* Post-Update per-player pass: hide the bow while carrying the ball
 * (PC MyPlayer.Update: bowSprite.Visible = HasBasketBall <= 0). The game's own
 * UpdateAnimation re-shows it every frame, so this runs AFTER Player.Update. */
inline void OnPlayerPostUpdate(void* player) {
    if (!InMode6()) return;
    int idx = PlayerIdx(player);
    if (idx < 0 || idx >= kMaxPlayers || !g_hasBall[idx]) return;
    /* v3 archer-quirk: 3 special skins (char 8 alt0, char 6/7 alt1) keep the
     * bow visible WHILE AIMING even when holding the ball; everyone else hides
     * it. AimingBowSkin() folds the skin check + Aiming read (device side). */
    if (env.AimingBowSkin(player)) return;
    void* bow = RdPtr(player, fld::Player_BowSprite);
    if (bow) WrU8(bow, fld::Component_Visible, 0);
}

/* BasketBall bounce physics (PC BasketBall.OnCollideH/V): the ball never
 * sticks or lands — it reverses X on walls and bounces with -0.9 damping on
 * floors. Returns true when handled (caller skips Orig). Pickup of the
 * bouncing/resting ball is handled by the IsCollectible hook (Speed < 2),
 * exactly like PC — the ball is never allowed to enter vanilla landed states
 * (PC forceState pins it in Shooting). */
inline void ForceState(void* ball) {
    WrI32(ball, fld::Arrow_State, 0);       /* ArrowStates.Shooting */
    env.ShootUpdate(ball);
}

inline float SqueakVol(float v) {
    if (v < 0) v = -v;
    v = v / 5.0f;
    return v > 1.0f ? 1.0f : v;
}
inline float Atan2(float y, float x) { return __builtin_atan2f(y, x); }

inline bool OnBallCollideH(void* self) {
    if (!InMode6() || self != g_ball) return false;
    float vx = RdF32(self, fld::Arrow_SpeedX);
    if (vx > 1.5f || vx < -1.5f)            /* v3: squeak on hard wall hits */
        env.Squeak(RdF32(self, fld::Entity_PositionX), SqueakVol(vx));
    g_prevXSpeed *= -1.0f;                  /* v3: flip prevXSpeed; the Update
                                             * tail writes it back into Speed.X */
    /* v3 wall dust: num=round(|vx|)/2 at pos+(4*sign(vx),0), range (1,3),
     * direction = Angle((-sign(vx), -0.5)) — kicked back off the wall */
    {
        float ax = vx < 0 ? -vx : vx;
        int num = (int)(ax + 0.5f) / 2;
        int sgn = (vx > 0) ? 1 : ((vx < 0) ? -1 : 0);
        if (num > 0 && sgn != 0) {
            void* lvl = RdPtr(self, fld::Player_Level);
            if (lvl) env.EmitDust(lvl, num,
                RdF32(self, fld::Entity_PositionX) + 4.0f * sgn,
                RdF32(self, fld::Entity_PositionY),
                1.0f, 3.0f, Atan2(-0.5f, (float)-sgn), true);
        }
    }
    ForceState(self);
    return true;
}

inline bool OnBallCollideV(void* self) {
    if (!InMode6() || self != g_ball) return false;
    float x = RdF32(self, fld::Entity_PositionX);
    float y = RdF32(self, fld::Entity_PositionY);
    /* v3 OnCollideV vs the basket: OUTSIDE the inner edge -> angle-deflect
     * away from the edge point; over the platform -> handled by the goal sweep
     * (v3 scores here; our sweep in OnRLUpdate is the equivalent seam). */
    lvl::TowerMarkers const* mk = nullptr;
    if (CurrentMarkers(&mk)) {
        for (int b = 0; b < 2; b++) {
            bool right = (b == 1);
            float px = mk->basket[b].x + (right ? -5.0f : -10.0f);
            float py = mk->basket[b].y + 10.0f;
            if (y >= py - 6.0f && y <= py + 4.0f && x >= px - 3.0f && x <= px + 18.0f) {
                bool outerLip = (right && x <= px) || (!right && x >= px + 15.0f);
                if (outerLip) {
                    float vx0 = RdF32(self, fld::Arrow_SpeedX);
                    float vy0 = RdF32(self, fld::Arrow_SpeedY);
                    if (vy0 > 1.5f || vy0 < -1.5f)
                        env.Squeak(x, SqueakVol(vy0));
                    /* v3: Speed = AngleToVector(Angle(edgePoint -> ball), |Speed|) */
                    float ex = px + (right ? 0.0f : 15.0f);   /* inner edge point */
                    float dx = x - ex, dy = y - py;
                    float len = vx0 * vx0 + vy0 * vy0;  len = len > 0 ? __builtin_sqrtf(len) : 0;
                    float dn  = dx * dx + dy * dy;      dn  = dn  > 0 ? __builtin_sqrtf(dn)  : 1;
                    float nvx = dx / dn * len, nvy = dy / dn * len;
                    g_prevXSpeed = nvx + (right ? -0.1f : 0.1f);
                    WrF32(self, fld::Arrow_SpeedX, nvx);
                    WrF32(self, fld::Arrow_SpeedY, nvy * -0.9f);
                    return true;
                }
            }
        }
    }
    /* v3 (non-basket platform): squeak, Speed.Y *= -0.9 (0 when slow), then
     * forceState — the ball never leaves Shooting. */
    {
        float vy0 = RdF32(self, fld::Arrow_SpeedY);
        if (vy0 > 1.5f || vy0 < -1.5f)      /* v3: squeak on hard floor bounces */
            env.Squeak(RdF32(self, fld::Entity_PositionX), SqueakVol(vy0));
    }
    float sy = RdF32(self, fld::Arrow_SpeedY) * -0.9f;
    if (sy > -1.0f && sy < 1.0f) sy = 0.0f;
    else {
        /* v3 floor dust (post-damping |Speed.Y| > 1.75): num=round(|vy|)/2
         * at pos+(0,4), range One*3, no direction */
        float ay = sy < 0 ? -sy : sy;
        if (ay > 1.75f) {
            int num = (int)(ay + 0.5f) / 2;
            void* lvl = RdPtr(self, fld::Player_Level);
            if (num > 0 && lvl)
                env.EmitDust(lvl, num, x, y + 4.0f, 3.0f, 3.0f, 0.0f, false);
        }
    }
    WrF32(self, fld::Arrow_SpeedY, sy);
    ForceState(self);
    return true;
}

/* v3 MyToyArrow.Update, EXACT: prevXSpeed captured before the vanilla update,
 * restored after (Gravity state rolls it toward 0 at 0.025/frame); slamming +
 * cantCollect tick here; State re-pinned to Shooting via forceState. Driven
 * from the ToyArrow.Update body hook (ball only). */
inline void PreBallUpdate(void* self) {
    /* v3: prevXSpeed is a FIELD — captured here, possibly FLIPPED by collision
     * handlers during the vanilla update, applied by the tail below. */
    g_prevXSpeed = RdF32(self, fld::Arrow_SpeedX);
    if (g_slamFrames > 0) {
        g_slamFrames--;
        if (RdF32(self, fld::Arrow_SpeedY) <= 0.1f) g_slamFrames = 0;
    }
    if (g_ballNoCollect > 0) g_ballNoCollect--;
    g_fallModeEntered = false;
}

inline void PostBallUpdate(void* self) {
    if (g_fallModeEntered) g_prevXSpeed = RdF32(self, fld::Arrow_SpeedX);
    ForceState(self);
    if (RdI32(self, fld::Arrow_State) == 2) {           /* ArrowStates.Gravity */
        float v = g_prevXSpeed;
        if      (v >  0.025f) v -= 0.025f;
        else if (v < -0.025f) v += 0.025f;
        else                  v  = 0.0f;
        g_prevXSpeed = v;
        WrF32(self, fld::Arrow_SpeedX, v);
    } else {
        WrF32(self, fld::Arrow_SpeedX, g_prevXSpeed);
    }
}

/* v3 EnterFallMode override tail (after base): Speed.X *= 2; if 0 -> +-1
 * toward the level center (hardcoded 160 in BOTH mod versions). */
inline void PostBallEnterFallMode(void* self) {
    g_fallModeEntered = true;
    float vx = RdF32(self, fld::Arrow_SpeedX) * 2.0f;
    if (vx == 0.0f)
        vx = (RdF32(self, fld::Entity_PositionX) < 160.0f) ? 1.0f : -1.0f;
    WrF32(self, fld::Arrow_SpeedX, vx);
}

/* The mode brain: runs every frame from the RoundLogic.OnUpdate detour. */
inline void OnRLUpdate(void* rl) {
    void* sess = RdPtr(rl, fld::RoundLogic_Session);
    if (!sess || sess != g_session) return;
    void* level = RdPtr(sess, fld::Session_CurrentLevel);
    if (!level) return;
    g_level = level;
    g_hudDrawn = false;                     /* render phase follows update phase */

    /* TowerBall rounds end on GOALS, not elimination: until the goal score is
     * reached, keep vetoing any Ending flag the team pipeline may set (v3 does
     * this by overriding TeamCheckForRoundOver -> false). */
    if (!g_endGame && *reinterpret_cast<uint8_t*>(reinterpret_cast<uint8_t*>(level) + fld::Level_Ending))
        *reinterpret_cast<uint8_t*>(reinterpret_cast<uint8_t*>(level) + fld::Level_Ending) = 0;

    /* 3.1 TimedRounds: 1s per 60 frames; at zero -> leader wins (tie -> overtime) */
    if (g_timedRounds && g_roundLive && !g_endGame && !g_overtime) {
        if (++g_frameAcc >= 60) {
            g_frameAcc = 0;
            if (g_secondsLeft > 0 && --g_secondsLeft == 0) {
                void* scor = RdPtr(sess, fld::Session_Scores);
                void* itm  = scor ? RdPtr(scor, fld::Array_Data) : nullptr;
                int a2 = itm ? reinterpret_cast<int32_t*>(itm)[0] : 0;
                int b2 = itm ? reinterpret_cast<int32_t*>(itm)[1] : 0;
                if (a2 == b2) {
                    g_overtime = true;      /* 3.1: tied -> overtime, next goal wins */
                    env.Log("[towerball] OVERTIME!", 0, 0, 0, 0);
                } else {
                    g_endGame = true;
                    g_endCounter = 90;
                    env.Spotlight(level, sess, (a2 > b2) ? 0 : 1);
                    env.SlowMo(level);
                    env.Log("[towerball] TIME! team %ld wins %ld-%ld",
                            (a2 > b2) ? 0 : 1, (long)a2, (long)b2, 0);
                }
            }
        }
    }

    /* v3 endgame: 90 frames after the winning goal, EndRound every frame */
    if (g_endGame && g_roundLive) {
        if (g_endCounter > 0) g_endCounter--;
        else env.EndRound(sess);
    }

    TickCorpses();                          /* v3: corpses fade after death */
    TickDeadBalls(level);                   /* v3: cosmetic scored-ball bounce-out */
    if (g_netJump[0] > 0) g_netJump[0]--;
    if (g_netJump[1] > 0) g_netJump[1]--;

    /* v3 RespawnPlayer: RemoveIndicator 60f after the spawn. Ticked BEFORE the
     * respawn loop so the frame that sets the timer (DoRespawn) doesn't also
     * decrement it — a clean 60-frame countdown. */
    for (int i = 0; i < kMaxPlayers; i++) {
        if (g_indicator[i] > 0 && --g_indicator[i] == 0) {
            void* pl = env.GetPlayer(level, i);
            if (pl) env.RemoveIndicator(pl);
        }
    }
    /* respawn timers (PC: 70-frame alarm per death). ALIVE-CHECK at EXPIRY
     * only: retail code can revive a dead player first (Team Revive respawns
     * teammates on a kill, and mode 6 forces TeamMode, arming that path) —
     * respawning again would make TWO Players with one index. The check MUST
     * NOT run before expiry: Monocle removes dying players DEFERRED, so right
     * after a death GetPlayer still returns the dying entity — an every-frame
     * check cancelled every timer and nobody ever respawned (2026-07-13 log:
     * "already revived" after every death; zero-player towerball state then
     * crashed native). At the 70-frame expiry the corpse's entity is long
     * gone, while a Team-Revive'd player IS present — exact discrimination. */
    for (int i = 0; i < kMaxPlayers; i++) {
        if (g_respawn[i] > 0 && !g_endGame) {
            if (--g_respawn[i] == 0) {
                if (env.GetPlayer(level, i)) {
                    env.Log("[towerball] player %ld already revived - respawn skipped", i, 0, 0, 0);
                } else {
                    DoRespawn(sess, level, i);
                }
            }
        }
    }

    if (AnyoneHolding()) {                  /* ball is in someone's hands */
        for (int holder = 0; holder < kMaxPlayers; holder++) {
            if (g_hasBall[holder] && !env.GetPlayer(level, holder)) {
                g_hasBall[holder] = false;  /* carrier vanished outside Die (safety) */
                g_ballCooldown = 120;
            }
        }
        if (AnyoneHolding()) return;        /* held ball is drawn in RenderHUD */
    }

    if (g_ball) {
        if (!RdPtr(g_ball, fld::Entity_Scene)) {
            if (!g_ballSeen) return;        /* deferred add not flushed yet (hitstop-safe) */
            g_ball = nullptr;               /* was in the scene, now gone — out of play */
            g_prevValid = false;
            g_ballCooldown = 180;
            return;
        }
        g_ballSeen = true;
        float x = RdF32(g_ball, fld::Entity_PositionX);
        float y = RdF32(g_ball, fld::Entity_PositionY);
        /* swept goal test: current, midpoint, previous — no tunneling */
        float sxs[3] = { x, (g_prevValid ? (x+g_prevX)*0.5f : x), (g_prevValid ? g_prevX : x) };
        float sys[3] = { y, (g_prevValid ? (y+g_prevY)*0.5f : y), (g_prevValid ? g_prevY : y) };
        g_prevX = x; g_prevY = y; g_prevValid = true;
        int team = -1;
        bool movingDown = RdF32(g_ball, fld::Arrow_SpeedY) > 0.0f;   /* v3: goals only count FALLING in */
        lvl::TowerMarkers const* mk = nullptr;
        if (movingDown && CurrentMarkers(&mk)) {
            /* PC mapping: left basket -> team 1 Red, right -> team 0 Blue */
            for (int b = 0; b < 2 && team < 0; b++) {
                float x0, x1, y0, y1;
                HoopZone(mk->basket[b], b == 1, &x0, &x1, &y0, &y1);
                for (int s = 0; s < 3; s++)
                    if (sxs[s] >= x0 && sxs[s] <= x1 && sys[s] >= y0 && sys[s] <= y1) {
                        team = (b == 0) ? 1 : 0; break;
                    }
            }
        } else if (movingDown) {
            /* fallback (vanilla level in mode 6): edge strips */
            if      (x <= 10.0f)           team = 1;
            else if (x >= LevelW() - 10.f) team = 0;
        }
        if (team >= 0) {
            g_netJump[(team == 1) ? 0 : 1] = 40;   /* v3 DoNetJump on the scored basket */
            bool slam = g_slamFrames > 0;   /* v3: goal within the throw's slam window */
            int throwerIdx = RdI32(g_ball, fld::Arrow_PlayerIndex);
            /* v3 goal branch shapes the DeadBall exit velocity: near the
             * OUTER edge of the hoop -> Speed.X *= -1 (clank off the far lip);
             * else Speed.X = prevXSpeed * max(Speed.Y/2, 1) (carry through). */
            float dvx = RdF32(g_ball, fld::Arrow_SpeedX);
            float dvy = RdF32(g_ball, fld::Arrow_SpeedY);
            {
                lvl::TowerMarkers const* mkd = nullptr;
                if (CurrentMarkers(&mkd)) {
                    int bd = (team == 1) ? 0 : 1;
                    float pxd = mkd->basket[bd].x + (bd == 1 ? -5.0f : -10.0f);
                    bool right = (bd == 1);
                    if ((right && x - 5.0f < pxd) || (!right && x + 5.0f >= pxd + 15.0f))
                        dvx = -dvx;
                    else {
                        float t = dvy / 2.0f; if (t < 1.0f) t = 1.0f;
                        dvx = g_prevXSpeed * t;
                    }
                }
            }
            AddDeadBall(x, y, dvx, dvy);
            env.RemoveSelf(g_ball);         /* consume; fresh ball at the chest soon */
            g_ball = nullptr;
            g_prevValid = false;
            g_ballCooldown = kBallRespawnGoal;   /* v3 SpawnBallChest(300) + open */
            DoGoal(rl, sess, level, team, throwerIdx, (long)x, (long)y);
            if (slam) {
                lvl::TowerMarkers const* mk2 = nullptr;
                if (CurrentMarkers(&mk2)) {
                    int b = (team == 1) ? 0 : 1;     /* scored basket side */
                    float px = mk2->basket[b].x + (b == 1 ? -5.0f : -10.0f);
                    float py = mk2->basket[b].y + 10.0f;
                    /* USER DECISION: the slam explosion must not kill the
                     * scorer (same rule as the dunk site — Flash spans the
                     * deferred Explosion.Added kill sweep). */
                    if (throwerIdx >= 0 && throwerIdx < kMaxPlayers) {
                        void* thrower = env.GetPlayer(level, throwerIdx);
                        if (thrower) env.Flash(thrower, 5);
                    }
                    env.Explode(level, px + 7.5f, py, throwerIdx);
                    AddSlamNotification(px + (b == 1 ? -10.0f : 10.0f), py);
                    env.Log("[towerball] SLAM! by player %ld", throwerIdx, 0, 0, 0);
                }
            }
        }
        return;
    }

    if (!g_roundLive) return;               /* countdown/intro — chest not open yet */
    if (g_endGame) return;                  /* round is ending — no new balls */
    if (g_ballCooldown > 0) { g_ballCooldown--; return; }

    /* spawn: need any live player as the Arrow.Create owner */
    void* owner = nullptr;
    for (int i = 0; i < kMaxPlayers && !owner; i++) owner = env.GetPlayer(level, i);
    if (!owner) return;                     /* players not in yet — retry next frame */
    lvl::TowerMarkers const* mk = nullptr;
    float sx = LevelW() * 0.5f, sy = 60.0f;
    if (CurrentMarkers(&mk)) { sx = mk->chest.x; sy = mk->chest.y; }
    /* PC BasketBallTreasureChest.Update: Arrow.Create(ball, player, chestPos,
     * PI/2) with the owner's Allegiance temporarily Neutral — the ball fires
     * DOWNWARD out of the chest, untinted, and bounces to rest. */
    uint8_t a0 = RdU8(owner, fld::Entity_Allegiance);
    uint8_t a1 = RdU8(owner, fld::Player_Allegiance2);
    WrU8(owner, fld::Entity_Allegiance, 0xff);
    WrU8(owner, fld::Player_Allegiance2, 0xff);
    g_adoptNext = true;                     /* this create IS the ball */
    g_spawnNeutral = true;                  /* chest drop -> ThrownTeam = Neutral */
    void* ball = env.ArrowCreate(arrow::Toy, owner, sx, sy, 1.57079633f);
    WrU8(owner, fld::Entity_Allegiance, a0);
    WrU8(owner, fld::Player_Allegiance2, a1);
    if (!ball) return;
    if (env.SceneAdd(level, ball)) {
        g_ball = ball;
        g_ballSeen = false;
        env.Log("[towerball] ball dropped in @ %ld,%ld", (long)sx, (long)sy, 0, 0);
    }
}

/* ---------------- level-path swap (GenLevels hook) ---------------- */

/* Restore all outstanding canonical-string patches. MUST run before every
 * GenLevels Orig (any mode) — the list holds refs to the tower's canonical
 * path strings, and leaking "tb.oel" into them would corrupt other modes. */
inline void PreGenLevels() {
    for (int i = 0; i < g_lvlPatchCount; i++) {
        uint8_t*  s     = reinterpret_cast<uint8_t*>(g_lvlPatch[i].str);
        int32_t   len   = RdI32(s, fld::String_Length);
        uint16_t* chars = reinterpret_cast<uint16_t*>(s + fld::String_Chars);
        if (len >= 6 && chars[len-6] == u't' && chars[len-5] == u'b') {
            chars[len-6] = g_lvlPatch[i].c0;
            chars[len-5] = g_lvlPatch[i].c1;
        }
    }
    g_lvlPatchCount = 0;
}

/* After GenLevels Orig: in mode 6, rewrite each path tail "NN.oel" -> "tb.oel"
 * (same UTF-16 length, in place) and harvest tower index + wide flag. */
inline void PostGenLevels(void* vls, void* matchSettings) {
    int32_t md = matchSettings ? RdI32(matchSettings, fld::MatchSettings_Mode) : -1;
    if (md != mode::Warlord && matchSettings != g_maskedMS) return;   /* masked = ours */
    void* list = RdPtr(vls, fld::VLS_Levels);
    if (!list) return;
    void* arr = RdPtr(list, fld::List_Items);
    int32_t n = RdI32(list, fld::List_Count);
    if (!arr || n <= 0) return;
    void** data = reinterpret_cast<void**>(RdPtr(arr, fld::Array_Data));
    if (!data) return;
    int patched = 0;
    for (int i = 0; i < n && g_lvlPatchCount < 24; i++) {
        uint8_t* s = reinterpret_cast<uint8_t*>(data[i]);
        if (!s) continue;
        int32_t   len   = RdI32(s, fld::String_Length);
        uint16_t* chars = reinterpret_cast<uint16_t*>(s + fld::String_Chars);
        if (len < 8) continue;
        if (chars[len-6] < u'0' || chars[len-6] > u'9' ||
            chars[len-5] < u'0' || chars[len-5] > u'9' ||
            chars[len-4] != u'.' || chars[len-3] != u'o') continue;
        if (patched == 0) {
            /* tower index = first digit-pair followed by ' ' ("NN - Name");
             * wide set = "6P" anywhere in the path */
            g_towerIdx = -1; g_towerWide = false;
            for (int k = 0; k + 2 < len; k++) {
                if (chars[k] == u'6' && chars[k+1] == u'P') g_towerWide = true;
                if (g_towerIdx < 0 &&
                    chars[k] >= u'0' && chars[k] <= u'9' &&
                    chars[k+1] >= u'0' && chars[k+1] <= u'9' &&
                    chars[k+2] == u' ')
                    g_towerIdx = (chars[k]-u'0')*10 + (chars[k+1]-u'0');
            }
        }
        g_lvlPatch[g_lvlPatchCount++] = { data[i], chars[len-6], chars[len-5] };
        chars[len-6] = u't'; chars[len-5] = u'b';
        patched++;
    }
    env.Log("[towerball] level paths -> tb.oel (%ld patched, tower=%ld wide=%ld)",
            patched, g_towerIdx, (long)g_towerWide, 0);
}

} // namespace logic
} // namespace tb
