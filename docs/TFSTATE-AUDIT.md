# TF.State / TF.EX patches vs the Switch port (2026-10-15)

The browser runs TF.EX 0.19.2, whose **TF.State** patches change gameplay while capture (netplay) is active: they replace
coroutines/alarms/tweens with counters and closed-form state so the game can be saved and rolled back. Where such a replacement
behaves differently from vanilla, the Switch must do what TF.State does, or the sides desync.

How this list was made: every `[HarmonyPatch]` in TF.State.Patchs and TF.EX.Patchs (decompiled: switch-online/.cache/tfex-0.19.2,
`ilspycmd -p` of the DLLs in the browser's DShad.TF.EX-v0.19.2.zip) — 200 patched methods — matched against the port's code and docs
(121 not mentioned), then each gameplay one read against the vanilla PC method (TowerFall.exe, the browser's, byte-identical to Steam).
Switch bodies/fields come from tools/re (classmeta records, disassembly); see tools/re/README.md.

## Ported (this session) and verified

| patch | vanilla vs TF.State | Switch | verified |
|---|---|---|---|
| MiasmaPatch (Update, Dissipate) | coroutine vs closed form | hooks existed, but run-time discovery never matched: now installed on the bodies | Frostfang 371 -> 0 |
| SensorBlockPatch (Update) | coroutine (`yield 40` + tweens) vs counter 40/60/80/140/150 | ported (xplay.cpp) | Dreadwood 93 -> 0 |
| IciclePatch (Update, OnArrowHit, OnPlayerCollide, OnPlayerGhostCollide, OnExplodePush) | FallRoutine (5 x yield 2, ~10 ticks) vs counter 15 | ported | no regression run yet (scripts never trigger an icicle) |
| Arrow.Drop (GAMEPLAY-RNG-PORT A1, REQ) | draws not on the gameplay stream | hook added (rnd.cpp) | King's Court 710 -> 0 |

## Also ported (same session, second pass)

| patch | difference | Switch | verified |
|---|---|---|---|
| PickupPatch ctor (ProperlySetTween) | vanilla `Lerp(start, targetClampedY>=6, eased)` vs `Lerp(Position, target, eased)` | ported (lambda +0xae53c0 replaced; unclamped target kept from the ctor) | regression sweep |
| PlayerCorpsePatch DieByArrow / Update / StartDroppingArrows | bramble alarms+tweens and the prism coroutine vs counters 10/30/610/630 and 5/15/30; drop alarm replaced | ported (ghost counter: ReturnAsGhosts variant, not ported) | no trigger in the scripts |
| MoonGlassBlockPatch + CoroutinePatch | ExplodeSequence coroutine vs MoonGlassBlockExplodeController (wait 5, 10 cells per tick), stepped before the entity flush | ported | Moonstone not run yet |
| BrambleArrowPatch HitWall / Update | the same spread steps, scheduled after BrambleArrow.Update from the hit tick (vanilla: coroutine, next tick) | ported by driving the vanilla coroutine at TF.State's moment (group id / brothers edge not reproduced) | no trigger in the scripts |
| LevelEntityPatch Render | Position restored after Render (wrap drawing `x-320+320` is inexact for fractional x) | ported | regression sweep |

## Not needed for a default match (variant / mode / cosmetic / rollback bookkeeping)

- TreasureChestPatch: Bottomless chests (type 4: none in netplay levels — all 1166 chests are Normal) and the ShowTreasureSpawns
  variant preview.
- PlayerCorpsePatch Added / ghost counter: ReturnAsGhosts variant. PlayerGhostPatch, TeamReviverPatch: ghosts / team modes.
- Moving/Rotate/Loop/ShiftBlock Update: only ScenarioPlatformHolder (TF.EX test scenarios).
- PirateBanner / HeavyPhysicsObject / ShockCircle / Orb sprite / Lantern / Background / ParticleSystem / LightingLayer:
  `CosmeticFreeze` = only while the browser re-simulates rollback frames (cosmetic by TF.EX's definition).
- AlarmPatch / TweenPatch / BramblesPatch Removed, EntityPatch: pools and Scene references for state capture/restore.
- OrbPickupPatch: LocalSettingsGuard (local options neutralised around the pickup).
- Quest / Dark World / Trials entities (Bat, Slime, TechnoMage, QuestSpawnPortal, CataclysmMeat, ...), replay, input, menus.
