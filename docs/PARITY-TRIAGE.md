# Parity triage: PC 1.3.3 IL vs Switch 1.4.1.6 (2026-10-16)

Triage of `.cache/parity-all.txt` (tools/re/parity.py, made 2026-10-15 18:31) for gameplay differences that could desync a
versus netplay match. Every entry of the 85 classes that can run in a versus simulation (Player, arrows, pickups/treasure,
Level*, RoundLogic, MatchSettings/MatchVariants, Orb*, hazards and level entities, Monocle core) was checked against the PC IL
(`.cache/towerfall-pc.il`) and the Switch disassembly (`tools/re/main.flat`), mostly with `ctx.py`, `disasm.py` and small
scratch helpers (constant -> instruction, rodata xrefs, variant-field users, writers of a static). Not run: emulator, tests,
builds. Already-fixed items from the brief (mud jump, hot coals fire/hitbox/bounce, DoSlowMoKill, Arrow.Drop RNG, ChalicePad
loop data fix) are listed as KNOWN and not reported again.

Method notes:
- `parity.py` has been improved since the report was made: 10 entries (Coroutine/Engine/TFGame `.cctor` 1.1333, Layer 0.3453,
  Arrow.ArrowUpdate 1.87, LevelRandomBGDetails/Geometry/Items float lists, DodgingUpdate 1.8375) are artefacts of a double
  being decoded as two floats and no longer appear when it is rerun.
- A small script checked each Switch-only constant against the floats of the PC method's callees (2 levels): 60 of them are
  constants of inlined PC helpers (Calc.AngleDiff, ToRadians, IsHyper, BouncedOn, ResetSpin, BigTreasureSpotValue ...).
- Recurring patterns (all NOISE unless stated): `x/2` -> `x*0.5` and `x*2` -> `x+x` (exact); unrolled int loops folded to
  float immediates; `k ? 0.3 : 0.3*0.5` folded into a bool-indexed rodata table; packed Vector2 stores; **PC 160 vs Switch 1e8
  = the `pan` argument of SFX.Play** (rodata 0x38e2860), i.e. sound only; **PC 160/320 vs a Switch variable = the WrapMath
  screen-width static `[0x4792130]`** (read by `WrapMath.get_CenterX` 0xbd1f80 and inline), see REAL (conditional) C.

## REAL findings

No new unconditional gameplay difference was found. Three differences are real in the code but dormant for a default
2-4 player netplay match; they matter only if the condition changes.

| # | class.method | what differs | evidence | condition / fix idea |
|---|---|---|---|---|
| A | `Chalice.Finish`, `ChalicePad.Update`, `ChaliceGhost.*` | The Switch build has no chalice gameplay. `Chalice.Finish` 0x9cdbf0 is only `strb #1,[x0,#0xe9]; ret` (`finished = true`). The PC version adds a ScreenFlash(White, 0.5) and **spawns `ChaliceGhost(pad.OwnerIndex, this)`**. `ChalicePad` has no `Update` body (7 Switch records vs 9 PC methods), so the PC's ownership/contest/fill logic (`ChalicePad::Update`: OnInterval(5), IsOwnerOnPad, GetFriendlyOnPad, IsContestorOnPad, GetTeamMismatch ...) is missing. `ChaliceGhost` has 0 Switch bodies. Both builds still create pad and chalice from a `ChalicePad` XML entity (PC `Level::LoadEntity` IL_0598/IL_05a8; Switch 0xa7df38 `ChalicePad::.ctor`, 0xa7df74 `Chalice::.ctor`). | Dormant: STATUS.md says no PC netplay level can spawn a ChaliceGhost. Fix idea: assert at level load that a netplay level has no `ChalicePad` entity (cheap guard). Otherwise this needs a port of ChalicePad.Update + ChaliceGhost (large). |
| B | `Player.LeaveDodge`, `Player.DodgingUpdate` (+ `MatchVariants` layout) | The Switch has a variant the PC lacks at `MatchVariants+0x1d8`. When it is on, `DodgingUpdate` skips `canHyper = true` (0xae9cd8..0xae9cec; PC sets it unconditionally at IL_068c) and `LeaveDodge` clamps \|Speed.X\| to 2.8 keeping its sign (0xaea560..0xaea5ac, rodata 0x38e2960; the PC has no clamp). A second Switch-only variant sits at `+0x1a0` (used by `Session.GetSpawnArrows` / `StartsWithNoKeptArrows`). Field map, from the Switch code that reads each offset vs the PC methods that read each variant: StealthArchers 0x168 ... Encumbrance 0x198, **extra 0x1a0**, SmallQuivers 0x1a8, NoQuivers 0x1b0, NoDodging 0x1b8, SuperDodging 0x1c0, ClumsyArchers 0x1c8, ShowDodgeCooldown 0x1d0, **extra 0x1d8**, FreeAiming 0x1e0, NoSlipping 0x1e8, SlipperyFloors 0x1f0, CursedBows 0x1f8, CursedDodges 0x200, BigHeads 0x208. So PC field index i maps to Switch offset 0x18+8*i up to Encumbrance, +8 up to ShowDodgeCooldown, and +16 after that. | Off by default, so behaviour is the same. Make sure both extra variants stay off in netplay. Any port code that sets Switch variants from PC indices or names (TournamentRules, lobby variant sync) must use this shifted map. |
| C | all "W" entries (RoundLogic.SpawnPlayersFFA/Teams, TreasureSpawner.GetChestSpawnsForLevel, Player.SceneBegin/SideBouncePlayer, LevelEntity.EnforceScreenWrap, Actor.MoveTowardsWrap, WrapHitbox, WrapMath.*, LoopPlatform, Lava, OrbLogic space/offset/scroll, FakeWall, Ice, Crumble*/CrackedWall/SwitchBlock/HotCoals ctors, Miasma) | The PC hard-codes 320/160; the Switch reads `[0x4792130]`. That static is set only by `WrapMath..cctor` (320) and `Session.StartGame` (0xb44530: `MatchSettings+0x20 ? 420 : 320`). Miasma uses the same flag (table 0x38e1630 = {210,160}). LevelRandomTreasure.AddRandomTreasure uses solids-grid width/2*10 (0xa95558), which is 160 for 32-column PC levels. Spot-checked formulas (SideBouncePlayer, Opposite, DoSpaceOrb, Lava.Update 305 = W-15) are identical at W=320. | Equal while `MatchSettings+0x20` (wide / 5-6 player mode) is false. The netplay session must never set it, and must use 32-column PC level files. |

Report entries behind A/B (9): `Chalice.Finish`, `ChalicePad.Update` (not found), `ChaliceGhost` `.ctor`/`<.ctor>lambdas`/`Added`/`GetTargetSpeed`/`OnPlayerCollide`/`Update` (not found), `Player.LeaveDodge`. `Player.DodgingUpdate` is counted as COSMETIC (its flagged constant is the SFX pan).

## UNSURE

| class.method | open point | what is needed |
|---|---|---|
| `RoundLogic.SpawnPlayersTeams` (CALLS `Level.GetTeamXMLPositions`, `MatchTeams.GetAmountOfPlayersOfAllegiance`) | The Switch body was rewritten for 6 players and 3 teams. That is irrelevant only if the port runs TF.State's replacement (GAMEPLAY-RNG-PORT B2) in TDM. I did not verify that B2 is implemented in the Switch mod. | Check the port for B2 (TeamSpawn split at X<=160, A/B sort, OwnShuffledIndexes). Otherwise TDM spawns and draws differ. |
| `Level.LoadEntity` | The flagged floats are string-switch hash constants (noise). The entity dispatch itself (which XML names create what, NoHazards gating) cannot be compared from constants, apart from the ChalicePad case above. | A case-by-case diff of the two dispatch tables if a netplay level uses an uncommon entity. |
| `SineWave.*`, other `Math.Sin`/`Cos` users (e.g. CataclysmBlock nodeWave moves a Solid) | The code is identical, but the double-precision `Math.Sin` runs on .NET/CRT on the PC and libm on the Switch (bl 0xad50). parity.py cannot see a last-ulp difference, and conv.r4 usually hides it. | An empirical table of sin(counter) values on both sides if a desync is ever traced to a sine-driven solid. |

## COSMETIC / NOISE / KNOWN (one line each)

Counts (252 entries in the selected classes): REAL (dormant/conditional) 9 entries in findings A-B, plus all "W" NOISE
entries under condition C; UNSURE 3 points (1 report entry plus 2 cross-cutting notes); KNOWN 5; COSMETIC 56; NOISE 182.

### KNOWN (5)

- `TowerFall.HotCoals..ctor` (DIFFERS) - hitbox 6/-1 vs 5/0, already fixed; 320 = W
- `TowerFall.LevelRandomItems.AddItems` (CALLS) - Calc.Position call = the ChalicePad loop, already fixed (data fix)
- `TowerFall.OrbLogic.DoSlowMoKill` (DIFFERS) - 0.25 vs 0.5, already fixed
- `TowerFall.Player.Jump` (DIFFERS) - mud jump -2.2 vs -3.2, already fixed; 3.6 = inlined get_IsHyper
- `TowerFall.Player.Update` (CALLS) - DIFFERS 3.6 = inlined get_IsHyper (speed-trail particles + achievement); CALLS FireControl.Start = hot-coals fire, already fixed

### COSMETIC (56)

- `Monocle.Camera.CenterOrigin` (DIFFERS) - camera origin (render), wide-screen 210/120
- `Monocle.Engine.Draw` (CALLS) - Saver icon render
- `TowerFall.BottomMiasma..ctor` (DIFFERS) - SFX.Play pan: PC passes 160 (screen centre), Switch passes 1e8 (rodata 0x38e2860)
- `TowerFall.BottomMiasma.DrawLight` (DIFFERS) - lighting
- `TowerFall.CataclysmBlade..ctor` (DIFFERS) - SFX.Play pan: PC passes 160 (screen centre), Switch passes 1e8 (rodata 0x38e2860)
- `TowerFall.Chalice..ctor` (DIFFERS) - LightRadius 60 vs 40
- `TowerFall.Chalice.Render` (DIFFERS) - render
- `TowerFall.GameplayLayer.BatchedRender` (DIFFERS) - render
- `TowerFall.KingReaper.DeadEnter` (DIFFERS) - SFX.Play pan: PC passes 160 (screen centre), Switch passes 1e8 (rodata 0x38e2860)
- `TowerFall.KingReaper.DisappearEnter` (DIFFERS) - SFX.Play pan: PC passes 160 (screen centre), Switch passes 1e8 (rodata 0x38e2860)
- `TowerFall.KingReaper.HurtEnter` (DIFFERS) - SFX.Play pan: PC passes 160 (screen centre), Switch passes 1e8 (rodata 0x38e2860)
- `TowerFall.KingReaper.ThrowBombB` (DIFFERS) - SFX.Play pan: PC passes 160 (screen centre), Switch passes 1e8 (rodata 0x38e2860)
- `TowerFall.KingReaper.Update` (DIFFERS) - SFX.Play pan: PC passes 160 (screen centre), Switch passes 1e8 (rodata 0x38e2860)
- `TowerFall.Lava.DrawLight` (DIFFERS) - lighting
- `TowerFall.Lava.Render` (DIFFERS) - render
- `TowerFall.LavaControl.Added` (DIFFERS) - SFX.Play pan: PC passes 160 (screen centre), Switch passes 1e8 (rodata 0x38e2860)
- `TowerFall.Level.Begin` (DIFFERS) - SFX.Play pan: PC passes 160 (screen centre), Switch passes 1e8 (rodata 0x38e2860)
- `TowerFall.Level.CoreRender` (DIFFERS) - render
- `TowerFall.Level.HandlePausing` (DIFFERS) - pause menu position 160 vs 210
- `TowerFall.Level.set_Paused` (DIFFERS) - SFX.Play pan: PC passes 160 (screen centre), Switch passes 1e8 (rodata 0x38e2860)
- `TowerFall.LevelEntity.DrawLight` (DIFFERS) - lighting
- `TowerFall.LevelEntity.Render` (DIFFERS) - render (TF.State Render position restore already ported)
- `TowerFall.MatchSettings.get_CanStartWithTeams` (CALLS) - team-select UI check (6-player GetAmountOfPlayersOfAllegiance)
- `TowerFall.MatchVariants.BuildMenu` (DIFFERS) - variant menu
- `TowerFall.Miasma..ctor` (DIFFERS) - SFX.Play pan: PC passes 160 (screen centre), Switch passes 1e8 (rodata 0x38e2860)
- `TowerFall.Miasma.DrawLight` (DIFFERS) - lighting
- `TowerFall.Miasma.Render` (DIFFERS) - render
- `TowerFall.MoonGlassBlock.InsideExplode` (DIFFERS) - SFX.Play pan: PC passes 160 (screen centre), Switch passes 1e8 (rodata 0x38e2860)
- `TowerFall.OrbLogic.EndDark` (DIFFERS) - SFX.Play pan: PC passes 160 (screen centre), Switch passes 1e8 (rodata 0x38e2860)
- `TowerFall.OrbLogic.EndSlow` (DIFFERS) - SFX.Play pan: PC passes 160 (screen centre), Switch passes 1e8 (rodata 0x38e2860)
- `TowerFall.OrbLogic.StartDark` (DIFFERS) - SFX.Play pan: PC passes 160 (screen centre), Switch passes 1e8 (rodata 0x38e2860)
- `TowerFall.OrbLogic.StartSlow` (DIFFERS) - SFX.Play pan: PC passes 160 (screen centre), Switch passes 1e8 (rodata 0x38e2860)
- `TowerFall.OrbLogic.Update` (DIFFERS) - SFX.Play pan: PC passes 160 (screen centre), Switch passes 1e8 (rodata 0x38e2860)
- `TowerFall.OrbPickup.OnPlayerCollide` (DIFFERS) - SFX.Play pan: PC passes 160 (screen centre), Switch passes 1e8 (rodata 0x38e2860)
- `TowerFall.Player.Added` (DIFFERS) - PlayerHair offset (0,-6) read from ArcherData on the Switch (hair is cosmetic)
- `TowerFall.Player.DoWrapRender` (DIFFERS) - render
- `TowerFall.Player.DodgingUpdate` (DIFFERS) - SFX.Play pan: PC passes 160 (screen centre), Switch passes 1e8 (rodata 0x38e2860) (char_miracleGrab); the extra canHyper gate at 0xae9cd8 is the Switch-only variant +0x1d8, see REAL (dormant)
- `TowerFall.Player.UpdateAnimation` (DIFFERS) - sprite animation
- `TowerFall.PlayerCorpse.DoWrapRender` (CALLS) - render (white outline)
- `TowerFall.RoundLogic.FinalKill` (DIFFERS) - SFX.Play pan: PC passes 160 (screen centre), Switch passes 1e8 (rodata 0x38e2860)
- `TowerFall.RoundLogic.FinalKillNoSpotlight` (DIFFERS) - SFX.Play pan: PC passes 160 (screen centre), Switch passes 1e8 (rodata 0x38e2860)
- `TowerFall.RoundLogic.FinalKillNoSpotlightOrMusicStop` (DIFFERS) - SFX.Play pan: PC passes 160 (screen centre), Switch passes 1e8 (rodata 0x38e2860)
- `TowerFall.RoundLogic.FinalKillTeams` (DIFFERS) - SFX.Play pan: PC passes 160 (screen centre), Switch passes 1e8 (rodata 0x38e2860)
- `TowerFall.ShiftBlock.StartPostMove` (DIFFERS) - SFX.Play pan: PC passes 160 (screen centre), Switch passes 1e8 (rodata 0x38e2860)
- `TowerFall.ShiftBlock.StartShaking` (DIFFERS) - SFX.Play pan: PC passes 160 (screen centre), Switch passes 1e8 (rodata 0x38e2860)
- `TowerFall.ShiftBlock.Update` (DIFFERS) - SFX.Play pan: PC passes 160 (screen centre), Switch passes 1e8 (rodata 0x38e2860)
- `TowerFall.Spikeball.ChainRender` (DIFFERS) - render
- `TowerFall.Spikeball.Render` (DIFFERS) - render
- `TowerFall.SwitchBlock.DrawSolid` (DIFFERS) - render
- `TowerFall.TFGame..ctor` (DIFFERS) - window scale 3
- `TowerFall.TFGame.Initialize` (DIFFERS) - window scale 3
- `TowerFall.TFGame.OnSceneTransition` (CALLS) - Screen.SetSize (render target size)
- `TowerFall.TFGame.OpenStoreDarkWorldDLC` (DIFFERS) - store UI
- `TowerFall.TeamReviver.FinishReviving` (DIFFERS) - SFX.Play pan: PC passes 160 (screen centre), Switch passes 1e8 (rodata 0x38e2860)
- `TowerFall.TeamReviver.HUDRender` (DIFFERS) - HUD
- `TowerFall.TreasureChest.OpenChest` (DIFFERS) - SFX.Play pan: PC passes 160 (screen centre), Switch passes 1e8 (rodata 0x38e2860)

### NOISE (182)

- `Monocle.Bezier.GetPoint` (DIFFERS) - x*2 compiled as x+x (fadd s,s,s): exact
- `Monocle.Calc.AbsAngleDiff` (DIFFERS) - constants of an inlined PC callee (Calc.AngleDiff/ToRadians/AngleToVector, MathHelper.Pi...)
- `Monocle.Calc.AngleApproach` (DIFFERS) - constants of an inlined PC callee (Calc.AngleDiff/ToRadians/AngleToVector, MathHelper.Pi...)
- `Monocle.Calc.AngleLerp` (DIFFERS) - constants of an inlined PC callee (Calc.AngleDiff/ToRadians/AngleToVector, MathHelper.Pi...)
- `Monocle.Calc.ShorterAngleDifference` (DIFFERS) - constants of an inlined PC callee (Calc.AngleDiff/ToRadians/AngleToVector, MathHelper.Pi...)
- `Monocle.Calc.YoYo` (DIFFERS) - x*2 compiled as x+x (fadd s,s,s): exact
- `Monocle.Circle.get_Height` (DIFFERS) - Radius*2 as Radius+Radius: exact
- `Monocle.Circle.get_Width` (DIFFERS) - Radius*2 as Radius+Radius: exact
- `Monocle.Circle.set_Height` (DIFFERS) - x/2 (or /4, /8) compiled as x*0.5 (0.25, 0.125): exact
- `Monocle.Circle.set_Width` (DIFFERS) - x/2 (or /4, /8) compiled as x*0.5 (0.25, 0.125): exact
- `Monocle.Collider.get_BottomCenter` (DIFFERS) - x/2 (or /4, /8) compiled as x*0.5 (0.25, 0.125): exact
- `Monocle.Collider.get_Center` (DIFFERS) - x/2 (or /4, /8) compiled as x*0.5 (0.25, 0.125): exact
- `Monocle.Collider.get_CenterLeft` (DIFFERS) - x/2 (or /4, /8) compiled as x*0.5 (0.25, 0.125): exact
- `Monocle.Collider.get_CenterRight` (DIFFERS) - x/2 (or /4, /8) compiled as x*0.5 (0.25, 0.125): exact
- `Monocle.Collider.get_CenterX` (DIFFERS) - x/2 (or /4, /8) compiled as x*0.5 (0.25, 0.125): exact
- `Monocle.Collider.get_CenterY` (DIFFERS) - x/2 (or /4, /8) compiled as x*0.5 (0.25, 0.125): exact
- `Monocle.Collider.get_TopCenter` (DIFFERS) - x/2 (or /4, /8) compiled as x*0.5 (0.25, 0.125): exact
- `Monocle.Collider.set_BottomCenter` (DIFFERS) - x/2 (or /4, /8) compiled as x*0.5 (0.25, 0.125): exact
- `Monocle.Collider.set_Center` (DIFFERS) - x/2 (or /4, /8) compiled as x*0.5 (0.25, 0.125): exact
- `Monocle.Collider.set_CenterLeft` (DIFFERS) - x/2 (or /4, /8) compiled as x*0.5 (0.25, 0.125): exact
- `Monocle.Collider.set_CenterRight` (DIFFERS) - x/2 (or /4, /8) compiled as x*0.5 (0.25, 0.125): exact
- `Monocle.Collider.set_CenterX` (DIFFERS) - x/2 (or /4, /8) compiled as x*0.5 (0.25, 0.125): exact
- `Monocle.Collider.set_CenterY` (DIFFERS) - x/2 (or /4, /8) compiled as x*0.5 (0.25, 0.125): exact
- `Monocle.Collider.set_TopCenter` (DIFFERS) - x/2 (or /4, /8) compiled as x*0.5 (0.25, 0.125): exact
- `Monocle.Coroutine..cctor` (DIFFERS) - decoder artefact (a double read as two floats / string-hash ints); gone with current parity.py
- `Monocle.Ease.<.cctor>lambdas` (not found) - easer lambdas exist unnamed after Ease.UpDown (0x92bf00-0x92c420) with the same constants (BackIn checked at 0x92bf90)
- `Monocle.Ease.UpDown` (DIFFERS) - x*2 compiled as x+x (fadd s,s,s): exact
- `Monocle.Engine..cctor` (DIFFERS) - decoder artefact (a double read as two floats / string-hash ints); gone with current parity.py
- `Monocle.Engine..ctor` (DIFFERS) - decoder artefact (a double read as two floats / string-hash ints); gone with current parity.py
- `Monocle.Entity.get_BottomCenter` (DIFFERS) - x/2 (or /4, /8) compiled as x*0.5 (0.25, 0.125): exact
- `Monocle.Entity.get_Center` (DIFFERS) - x/2 (or /4, /8) compiled as x*0.5 (0.25, 0.125): exact
- `Monocle.Entity.get_CenterLeft` (DIFFERS) - x/2 (or /4, /8) compiled as x*0.5 (0.25, 0.125): exact
- `Monocle.Entity.get_CenterRight` (DIFFERS) - x/2 (or /4, /8) compiled as x*0.5 (0.25, 0.125): exact
- `Monocle.Entity.get_CenterX` (DIFFERS) - x/2 (or /4, /8) compiled as x*0.5 (0.25, 0.125): exact
- `Monocle.Entity.get_CenterY` (DIFFERS) - x/2 (or /4, /8) compiled as x*0.5 (0.25, 0.125): exact
- `Monocle.Entity.get_TopCenter` (DIFFERS) - x/2 (or /4, /8) compiled as x*0.5 (0.25, 0.125): exact
- `Monocle.Entity.set_BottomCenter` (DIFFERS) - x/2 (or /4, /8) compiled as x*0.5 (0.25, 0.125): exact
- `Monocle.Entity.set_Center` (DIFFERS) - x/2 (or /4, /8) compiled as x*0.5 (0.25, 0.125): exact
- `Monocle.Entity.set_CenterLeft` (DIFFERS) - x/2 (or /4, /8) compiled as x*0.5 (0.25, 0.125): exact
- `Monocle.Entity.set_CenterRight` (DIFFERS) - x/2 (or /4, /8) compiled as x*0.5 (0.25, 0.125): exact
- `Monocle.Entity.set_CenterX` (DIFFERS) - x/2 (or /4, /8) compiled as x*0.5 (0.25, 0.125): exact
- `Monocle.Entity.set_CenterY` (DIFFERS) - x/2 (or /4, /8) compiled as x*0.5 (0.25, 0.125): exact
- `Monocle.Entity.set_TopCenter` (DIFFERS) - x/2 (or /4, /8) compiled as x*0.5 (0.25, 0.125): exact
- `Monocle.Hitbox.CenterOrigin` (DIFFERS) - x/2 (or /4, /8) compiled as x*0.5 (0.25, 0.125): exact
- `Monocle.Layer.SetActualDepth` (DIFFERS) - decoder artefact (a double read as two floats / string-hash ints); gone with current parity.py
- `Monocle.Layer.UpdateEntityList` (DIFFERS) - decoder artefact (a double read as two floats / string-hash ints); gone with current parity.py
- `Monocle.SineWave..ctor` (DIFFERS) - constants of an inlined PC callee (Calc.AngleDiff/ToRadians/AngleToVector, MathHelper.Pi...)
- `Monocle.SineWave.Randomize` (DIFFERS) - x*2 compiled as x+x (fadd s,s,s): exact
- `Monocle.SineWave.StartDown` (DIFFERS) - x/2 (or /4, /8) compiled as x*0.5 (0.25, 0.125): exact
- `Monocle.SineWave.StartUp` (DIFFERS) - x/2 (or /4, /8) compiled as x*0.5 (0.25, 0.125): exact; 1.9463/3.7e19 = Sin(double) literal decoded as floats ((double)1.5707964f, exact)
- `Monocle.SineWave.Update` (DIFFERS) - x/2 (or /4, /8) compiled as x*0.5 (0.25, 0.125): exact
- `Monocle.Tilemap.get_HalfSize` (DIFFERS) - x/2 (or /4, /8) compiled as x*0.5 (0.25, 0.125): exact
- `Monocle.Wiggler.Create` (DIFFERS) - constants of an inlined PC callee (Calc.AngleDiff/ToRadians/AngleToVector, MathHelper.Pi...)
- `TowerFall.Actor.JumpThruRiseCollide` (DIFFERS) - unrolled int loop (i*3*j, 5*(i+1), 4*i...) folded to float immediates
- `TowerFall.Actor.MoveTowardsWrap` (DIFFERS) - PC 160/320 literals vs Switch WrapMath width static [0x4792130] (WrapMath.get_CenterX 0xbd1f80); set to 320 by Session.StartGame 0xb44530 unless MatchSettings+0x20 (wide/6p) -> 420; equal at 320
- `TowerFall.Arrow.ArrowUpdate` (DIFFERS) - decoder artefact (a double read as two floats / string-hash ints); gone with current parity.py
- `TowerFall.Arrow.DrillUpdate` (DIFFERS) - x*2 compiled as x+x (fadd s,s,s): exact
- `TowerFall.Arrow.UpdateSeeking` (DIFFERS) - x*2 compiled as x+x (fadd s,s,s): exact
- `TowerFall.BoltArrow.ShootUpdate` (DIFFERS) - x*2 compiled as x+x (fadd s,s,s): exact
- `TowerFall.BrambleSpore.OnPlayerCollide` (DIFFERS) - constants of an inlined PC callee (Calc.AngleDiff/ToRadians/AngleToVector, MathHelper.Pi...)
- `TowerFall.Brambles.Update` (DIFFERS) - x/2 (or /4, /8) compiled as x*0.5 (0.25, 0.125): exact
- `TowerFall.CataclysmBlock.Break` (DIFFERS) - x/2 (or /4, /8) compiled as x*0.5 (0.25, 0.125): exact
- `TowerFall.CataclysmBlock.Update` (DIFFERS) - constant folded (0.3*0.5 -> 0.15 bool-indexed table, int literal -> float)
- `TowerFall.CrackedPlatform.Emit` (DIFFERS) - x/2 (or /4, /8) compiled as x*0.5 (0.25, 0.125): exact
- `TowerFall.CrackedPlatform.FinishShaking` (DIFFERS) - x/2 (or /4, /8) compiled as x*0.5 (0.25, 0.125): exact
- `TowerFall.CrackedPlatform.Respawn` (DIFFERS) - x/2 (or /4, /8) compiled as x*0.5 (0.25, 0.125): exact
- `TowerFall.CrackedPlatform.Shift` (DIFFERS) - x/2 (or /4, /8) compiled as x*0.5 (0.25, 0.125): exact
- `TowerFall.CrackedPlatform.Update` (DIFFERS) - x/2 (or /4, /8) compiled as x*0.5 (0.25, 0.125): exact
- `TowerFall.CrackedWall..ctor` (DIFFERS) - PC 160/320 literals vs Switch WrapMath width static [0x4792130] (WrapMath.get_CenterX 0xbd1f80); set to 320 by Session.StartGame 0xb44530 unless MatchSettings+0x20 (wide/6p) -> 420; equal at 320
- `TowerFall.CrackedWall.Destroy` (DIFFERS) - unrolled int loop (i*3*j, 5*(i+1), 4*i...) folded to float immediates
- `TowerFall.CrackedWall.OnExplodePush` (DIFFERS) - x/2 (or /4, /8) compiled as x*0.5 (0.25, 0.125): exact
- `TowerFall.Crown.CollideH` (DIFFERS) - constants of an inlined PC callee (Calc.AngleDiff/ToRadians/AngleToVector, MathHelper.Pi...)
- `TowerFall.Crown.CollideV` (DIFFERS) - constants of an inlined PC callee (Calc.AngleDiff/ToRadians/AngleToVector, MathHelper.Pi...)
- `TowerFall.Crown.Update` (DIFFERS) - constant folded (0.3*0.5 -> 0.15 bool-indexed table, int literal -> float)
- `TowerFall.CrumbleBlock..ctor` (DIFFERS) - PC 160/320 literals vs Switch WrapMath width static [0x4792130] (WrapMath.get_CenterX 0xbd1f80); set to 320 by Session.StartGame 0xb44530 unless MatchSettings+0x20 (wide/6p) -> 420; equal at 320
- `TowerFall.CrumbleBlock.Destroy` (DIFFERS) - unrolled int loop (i*3*j, 5*(i+1), 4*i...) folded to float immediates
- `TowerFall.CrumbleBlock.OnExplodePush` (DIFFERS) - x/2 (or /4, /8) compiled as x*0.5 (0.25, 0.125): exact
- `TowerFall.CrumbleWall..ctor` (DIFFERS) - PC 160/320 literals vs Switch WrapMath width static [0x4792130] (WrapMath.get_CenterX 0xbd1f80); set to 320 by Session.StartGame 0xb44530 unless MatchSettings+0x20 (wide/6p) -> 420; equal at 320
- `TowerFall.CrumbleWallChunk.CreateChunks` (DIFFERS) - unrolled int loop (i*3*j, 5*(i+1), 4*i...) folded to float immediates
- `TowerFall.CrumbleWallChunk.Init` (DIFFERS) - float[] {0,pi,-pi/2,pi/2} initialised from a data blob (0x9ddc88 array copy); same Calc.Random draws
- `TowerFall.DreadFlower.Break` (DIFFERS) - constants of an inlined PC callee (Calc.AngleDiff/ToRadians/AngleToVector, MathHelper.Pi...)
- `TowerFall.DreadFlower.Update` (DIFFERS) - x*2 compiled as x+x (fadd s,s,s): exact
- `TowerFall.DreadTentacle.Update` (DIFFERS) - x/2 (or /4, /8) compiled as x*0.5 (0.25, 0.125): exact
- `TowerFall.DummyHead.Update` (DIFFERS) - x/2 (or /4, /8) compiled as x*0.5 (0.25, 0.125): exact
- `TowerFall.Enemy.DoSquish` (DIFFERS) - unrolled int loop (i*3*j, 5*(i+1), 4*i...) folded to float immediates
- `TowerFall.Enemy.GetHorizontalPlayer` (DIFFERS) - PC 160/320 literals vs Switch WrapMath width static [0x4792130] (WrapMath.get_CenterX 0xbd1f80); set to 320 by Session.StartGame 0xb44530 unless MatchSettings+0x20 (wide/6p) -> 420; equal at 320
- `TowerFall.EnemyAttack.get_AdjustedOffset` (DIFFERS) - x/2 (or /4, /8) compiled as x*0.5 (0.25, 0.125): exact
- `TowerFall.Exploder.ChaseEnter` (DIFFERS) - x*2 compiled as x+x (fadd s,s,s): exact
- `TowerFall.Exploder.ChaseLeave` (DIFFERS) - x/2 (or /4, /8) compiled as x*0.5 (0.25, 0.125): exact
- `TowerFall.FakeWall.SceneBegin` (DIFFERS) - PC 160/320 literals vs Switch WrapMath width static [0x4792130] (WrapMath.get_CenterX 0xbd1f80); set to 320 by Session.StartGame 0xb44530 unless MatchSettings+0x20 (wide/6p) -> 420; equal at 320
- `TowerFall.FloorMiasma.<Activate>lambdas` (not found) - lambda pushes Enemy-tag entities only (CollideDo tag 19); versus has none
- `TowerFall.FloorMiasma.OnPlayerCollide` (DIFFERS) - x/2 (or /4, /8) compiled as x*0.5 (0.25, 0.125): exact
- `TowerFall.HeavyPhysicsObject.CollideH` (DIFFERS) - constants of an inlined PC callee (Calc.AngleDiff/ToRadians/AngleToVector, MathHelper.Pi...)
- `TowerFall.HeavyPhysicsObject.CollideV` (DIFFERS) - constants of an inlined PC callee (Calc.AngleDiff/ToRadians/AngleToVector, MathHelper.Pi...)
- `TowerFall.HeavyPhysicsObject.Update` (DIFFERS) - constant folded (0.3*0.5 -> 0.15 bool-indexed table, int literal -> float)
- `TowerFall.Ice.BuildTiles` (DIFFERS) - PC 160/320 literals vs Switch WrapMath width static [0x4792130] (WrapMath.get_CenterX 0xbd1f80); set to 320 by Session.StartGame 0xb44530 unless MatchSettings+0x20 (wide/6p) -> 420; equal at 320
- `TowerFall.JumpPad.Update` (DIFFERS) - x/2 (or /4, /8) compiled as x*0.5 (0.25, 0.125): exact
- `TowerFall.Lantern.OnArrowHit` (DIFFERS) - x*2 compiled as x+x (fadd s,s,s): exact
- `TowerFall.Lava..ctor` (DIFFERS) - PC 160/320 literals vs Switch WrapMath width static [0x4792130] (WrapMath.get_CenterX 0xbd1f80); set to 320 by Session.StartGame 0xb44530 unless MatchSettings+0x20 (wide/6p) -> 420; equal at 320
- `TowerFall.Lava.Update` (DIFFERS) - PC 160/320 literals vs Switch WrapMath width static [0x4792130] (WrapMath.get_CenterX 0xbd1f80); set to 320 by Session.StartGame 0xb44530 unless MatchSettings+0x20 (wide/6p) -> 420; equal at 320
- `TowerFall.Level.LoadEntity` (DIFFERS) - decoder artefact (a double read as two floats / string-hash ints); gone with current parity.py
- `TowerFall.LevelEntity.EnforceScreenWrap` (DIFFERS) - PC 160/320 literals vs Switch WrapMath width static [0x4792130] (WrapMath.get_CenterX 0xbd1f80); set to 320 by Session.StartGame 0xb44530 unless MatchSettings+0x20 (wide/6p) -> 420; equal at 320
- `TowerFall.LevelRandomBGDetails.GenerateTileData` (DIFFERS) - decoder artefact (a double read as two floats / string-hash ints); gone with current parity.py
- `TowerFall.LevelRandomGeometry.GenerateData` (DIFFERS) - decoder artefact (a double read as two floats / string-hash ints); gone with current parity.py
- `TowerFall.LevelRandomItems.AddItems` (DIFFERS) - decoder artefact (a double read as two floats / string-hash ints); gone with current parity.py
- `TowerFall.LevelRandomTreasure.AddRandomTreasure` (DIFFERS) - 160/320 replaced by (solids grid width/2)*10 (0xa95558): 160 for the 32-column PC levels
- `TowerFall.LevelRandomTreasure.CompareBigTreasureSpots` (DIFFERS) - BigTreasureSpotValue (y>110) inlined
- `TowerFall.LoopPlatform..ctor` (DIFFERS) - PC 160/320 literals vs Switch WrapMath width static [0x4792130] (WrapMath.get_CenterX 0xbd1f80); set to 320 by Session.StartGame 0xb44530 unless MatchSettings+0x20 (wide/6p) -> 420; equal at 320
- `TowerFall.LoopPlatform.Added` (DIFFERS) - PC 160/320 literals vs Switch WrapMath width static [0x4792130] (WrapMath.get_CenterX 0xbd1f80); set to 320 by Session.StartGame 0xb44530 unless MatchSettings+0x20 (wide/6p) -> 420; equal at 320
- `TowerFall.LoopPlatform.Update` (DIFFERS) - PC 160/320 literals vs Switch WrapMath width static [0x4792130] (WrapMath.get_CenterX 0xbd1f80); set to 320 by Session.StartGame 0xb44530 unless MatchSettings+0x20 (wide/6p) -> 420; equal at 320
- `TowerFall.MatchSettings.get_GoalScore` (DIFFERS) - PlayerGoals(5,4,3)/(5,8,10) as tables extended to 6 players; identical for 2-4 players
- `TowerFall.Miasma.<Sequence>lambdas` (DIFFERS) - Sequence coroutine replaced by TF.State MiasmaPatch closed form (ported)
- `TowerFall.Miasma.OnPlayerCollide` (DIFFERS) - centre = MatchSettings+0x20 ? 210 : 160 (table 0x38e1630); 160 when not wide
- `TowerFall.Miasma.Update` (DIFFERS) - wide-mode table (210/420); body replaced by TF.State MiasmaPatch, already ported
- `TowerFall.Mole.JumpDownUpdate` (DIFFERS) - constant folded (0.3*0.5 -> 0.15 bool-indexed table, int literal -> float)
- `TowerFall.Mole.RollUpdate` (DIFFERS) - x*2 compiled as x+x (fadd s,s,s): exact
- `TowerFall.MoleShell.CollideH` (DIFFERS) - constants of an inlined PC callee (Calc.AngleDiff/ToRadians/AngleToVector, MathHelper.Pi...)
- `TowerFall.MoleShell.CollideV` (DIFFERS) - constants of an inlined PC callee (Calc.AngleDiff/ToRadians/AngleToVector, MathHelper.Pi...)
- `TowerFall.MoleShell.Update` (DIFFERS) - constant folded (0.3*0.5 -> 0.15 bool-indexed table, int literal -> float)
- `TowerFall.Orb.Update` (DIFFERS) - x/2 (or /4, /8) compiled as x*0.5 (0.25, 0.125): exact
- `TowerFall.OrbLogic.CancelVariants` (DIFFERS) - PC 160/320 literals vs Switch WrapMath width static [0x4792130] (WrapMath.get_CenterX 0xbd1f80); set to 320 by Session.StartGame 0xb44530 unless MatchSettings+0x20 (wide/6p) -> 420; equal at 320
- `TowerFall.OrbLogic.DoOffsetWorldVariant` (DIFFERS) - PC 160/320 literals vs Switch WrapMath width static [0x4792130] (WrapMath.get_CenterX 0xbd1f80); set to 320 by Session.StartGame 0xb44530 unless MatchSettings+0x20 (wide/6p) -> 420; equal at 320
- `TowerFall.OrbLogic.DoSpaceOrb` (DIFFERS) - PC 160/320 literals vs Switch WrapMath width static [0x4792130] (WrapMath.get_CenterX 0xbd1f80); set to 320 by Session.StartGame 0xb44530 unless MatchSettings+0x20 (wide/6p) -> 420; equal at 320
- `TowerFall.OrbLogic.EndScroll` (DIFFERS) - PC 160/320 literals vs Switch WrapMath width static [0x4792130] (WrapMath.get_CenterX 0xbd1f80); set to 320 by Session.StartGame 0xb44530 unless MatchSettings+0x20 (wide/6p) -> 420; equal at 320
- `TowerFall.Player..ctor` (DIFFERS) - constants of an inlined PC callee (Calc.AngleDiff/ToRadians/AngleToVector, MathHelper.Pi...)
- `TowerFall.Player.Die` (DIFFERS) - 3.6 = inlined get_IsHyper (Speed.LengthSquared >= 3.6)
- `TowerFall.Player.DoSquish` (DIFFERS) - unrolled int loop (i*3*j, 5*(i+1), 4*i...) folded to float immediates
- `TowerFall.Player.PlayerOnPlayer` (DIFFERS) - inlined BouncedOn (Scale 1.3/0.7, Speed.Y 2.8) and IsHyper
- `TowerFall.Player.SceneBegin` (DIFFERS) - PC 160/320 literals vs Switch WrapMath width static [0x4792130] (WrapMath.get_CenterX 0xbd1f80); set to 320 by Session.StartGame 0xb44530 unless MatchSettings+0x20 (wide/6p) -> 420; equal at 320
- `TowerFall.Player.SideBouncePlayer` (DIFFERS) - PC 160/320 literals vs Switch WrapMath width static [0x4792130] (WrapMath.get_CenterX 0xbd1f80); set to 320 by Session.StartGame 0xb44530 unless MatchSettings+0x20 (wide/6p) -> 420; equal at 320
- `TowerFall.Player.Update` (DIFFERS) - 3.6 = inlined get_IsHyper (speed-trail particles + achievement at 0xaefb3c)
- `TowerFall.PlayerCorpse.DieByJumpedOn` (DIFFERS) - Speed=(0,2.8) stored as one packed 64-bit int
- `TowerFall.PlayerCorpse.Update` (DIFFERS) - constant folded (0.3*0.5 -> 0.15 bool-indexed table, int literal -> float)
- `TowerFall.PlayerGhost.OnCollideH` (DIFFERS) - unrolled int loop (i*3*j, 5*(i+1), 4*i...) folded to float immediates
- `TowerFall.PlayerGhost.OnCollideV` (DIFFERS) - unrolled int loop (i*3*j, 5*(i+1), 4*i...) folded to float immediates
- `TowerFall.RotatePlatform.Update` (DIFFERS) - x/2 (or /4, /8) compiled as x*0.5 (0.25, 0.125): exact
- `TowerFall.RoundLogic.SpawnPlayersFFA` (DIFFERS) - PC 160/320 literals vs Switch WrapMath width static [0x4792130] (WrapMath.get_CenterX 0xbd1f80); set to 320 by Session.StartGame 0xb44530 unless MatchSettings+0x20 (wide/6p) -> 420; equal at 320
- `TowerFall.RoundLogic.SpawnPlayersTeams` (CALLS) - TF.State REPL (GAMEPLAY-RNG-PORT B2) replaces the body during netplay; Switch body (6-player GetTeamXMLPositions) irrelevant if B2 is ported
- `TowerFall.RoundLogic.SpawnPlayersTeams` (DIFFERS) - TF.State REPL (GAMEPLAY-RNG-PORT B2) replaces the body during netplay; Switch body (6-player GetTeamXMLPositions) irrelevant if B2 is ported
- `TowerFall.SensorBlock..ctor` (DIFFERS) - x/2 (or /4, /8) compiled as x*0.5 (0.25, 0.125): exact
- `TowerFall.ShiftBlock..ctor` (DIFFERS) - x/2 (or /4, /8) compiled as x*0.5 (0.25, 0.125): exact
- `TowerFall.ShiftBlock.Shake` (DIFFERS) - x/2 (or /4, /8) compiled as x*0.5 (0.25, 0.125): exact
- `TowerFall.ShiftBlock.StopShake` (DIFFERS) - x/2 (or /4, /8) compiled as x*0.5 (0.25, 0.125): exact
- `TowerFall.Skeleton.AlertUpdate` (DIFFERS) - constants of an inlined PC callee (Calc.AngleDiff/ToRadians/AngleToVector, MathHelper.Pi...)
- `TowerFall.Skeleton.Update` (DIFFERS) - constant folded (0.3*0.5 -> 0.15 bool-indexed table, int literal -> float)
- `TowerFall.Slime..ctor` (DIFFERS) - quest/Dark World enemy, never spawned in versus (not traced)
- `TowerFall.SlimeEye.CollideH` (DIFFERS) - constants of an inlined PC callee (Calc.AngleDiff/ToRadians/AngleToVector, MathHelper.Pi...)
- `TowerFall.SlimeEye.CollideV` (DIFFERS) - constants of an inlined PC callee (Calc.AngleDiff/ToRadians/AngleToVector, MathHelper.Pi...)
- `TowerFall.SlimeEye.Update` (DIFFERS) - constant folded (0.3*0.5 -> 0.15 bool-indexed table, int literal -> float)
- `TowerFall.Spikeball.Explode` (DIFFERS) - constants of an inlined PC callee (Calc.AngleDiff/ToRadians/AngleToVector, MathHelper.Pi...)
- `TowerFall.Spikeball.UpdatePosition` (DIFFERS) - constants of an inlined PC callee (Calc.AngleDiff/ToRadians/AngleToVector, MathHelper.Pi...)
- `TowerFall.Spikeball.get_RotateAngle` (DIFFERS) - x*2 compiled as x+x (fadd s,s,s): exact
- `TowerFall.SteelHat.CollideH` (DIFFERS) - constants of an inlined PC callee (Calc.AngleDiff/ToRadians/AngleToVector, MathHelper.Pi...)
- `TowerFall.SteelHat.CollideV` (DIFFERS) - constants of an inlined PC callee (Calc.AngleDiff/ToRadians/AngleToVector, MathHelper.Pi...)
- `TowerFall.SteelHat.Update` (DIFFERS) - constant folded (0.3*0.5 -> 0.15 bool-indexed table, int literal -> float)
- `TowerFall.SwitchBlock..ctor` (DIFFERS) - PC 160/320 literals vs Switch WrapMath width static [0x4792130] (WrapMath.get_CenterX 0xbd1f80); set to 320 by Session.StartGame 0xb44530 unless MatchSettings+0x20 (wide/6p) -> 420; equal at 320
- `TowerFall.TFGame..cctor` (DIFFERS) - decoder artefact (a double read as two floats / string-hash ints); gone with current parity.py
- `TowerFall.TeamReviver.CanReviveAtThisPosition` (DIFFERS) - unrolled int loop (i*3*j, 5*(i+1), 4*i...) folded to float immediates
- `TowerFall.TeamReviver.ResetCounter` (DIFFERS) - constant folded (0.3*0.5 -> 0.15 bool-indexed table, int literal -> float)
- `TowerFall.TeamReviver.ReviveUpdate` (DIFFERS) - constant folded (0.3*0.5 -> 0.15 bool-indexed table, int literal -> float)
- `TowerFall.TreasureSpawner.GetChestSpawnsForLevel` (DIFFERS) - PC 160/320 literals vs Switch WrapMath width static [0x4792130] (WrapMath.get_CenterX 0xbd1f80); set to 320 by Session.StartGame 0xb44530 unless MatchSettings+0x20 (wide/6p) -> 420; equal at 320
- `TowerFall.WoodenHat.CollideH` (DIFFERS) - constants of an inlined PC callee (Calc.AngleDiff/ToRadians/AngleToVector, MathHelper.Pi...)
- `TowerFall.WoodenHat.CollideV` (DIFFERS) - constants of an inlined PC callee (Calc.AngleDiff/ToRadians/AngleToVector, MathHelper.Pi...)
- `TowerFall.WoodenHat.Update` (DIFFERS) - constant folded (0.3*0.5 -> 0.15 bool-indexed table, int literal -> float)
- `TowerFall.Worm.DeadEnter` (DIFFERS) - constants of an inlined PC callee (Calc.AngleDiff/ToRadians/AngleToVector, MathHelper.Pi...)
- `TowerFall.Worm.FallEnter` (DIFFERS) - constants of an inlined PC callee (Calc.AngleDiff/ToRadians/AngleToVector, MathHelper.Pi...)
- `TowerFall.Worm.WalkEnter` (DIFFERS) - constants of an inlined PC callee (Calc.AngleDiff/ToRadians/AngleToVector, MathHelper.Pi...)
- `TowerFall.WrapHitbox.BuildHitList` (DIFFERS) - PC 160/320 literals vs Switch WrapMath width static [0x4792130] (WrapMath.get_CenterX 0xbd1f80); set to 320 by Session.StartGame 0xb44530 unless MatchSettings+0x20 (wide/6p) -> 420; equal at 320
- `TowerFall.WrapMath..cctor` (DIFFERS) - PC 160/320 literals vs Switch WrapMath width static [0x4792130] (WrapMath.get_CenterX 0xbd1f80); set to 320 by Session.StartGame 0xb44530 unless MatchSettings+0x20 (wide/6p) -> 420; equal at 320
- `TowerFall.WrapMath.AbsDiffY` (DIFFERS) - constants of an inlined PC callee (Calc.AngleDiff/ToRadians/AngleToVector, MathHelper.Pi...)
- `TowerFall.WrapMath.ApplyWrap` (DIFFERS) - constants of an inlined PC callee (Calc.AngleDiff/ToRadians/AngleToVector, MathHelper.Pi...)
- `TowerFall.WrapMath.ApplyWrapX` (DIFFERS) - PC 160/320 literals vs Switch WrapMath width static [0x4792130] (WrapMath.get_CenterX 0xbd1f80); set to 320 by Session.StartGame 0xb44530 unless MatchSettings+0x20 (wide/6p) -> 420; equal at 320
- `TowerFall.WrapMath.DiffX` (DIFFERS) - PC 160/320 literals vs Switch WrapMath width static [0x4792130] (WrapMath.get_CenterX 0xbd1f80); set to 320 by Session.StartGame 0xb44530 unless MatchSettings+0x20 (wide/6p) -> 420; equal at 320
- `TowerFall.WrapMath.Opposite` (DIFFERS) - PC 160/320 literals vs Switch WrapMath width static [0x4792130] (WrapMath.get_CenterX 0xbd1f80); set to 320 by Session.StartGame 0xb44530 unless MatchSettings+0x20 (wide/6p) -> 420; equal at 320
- `TowerFall.WrapMath.Shortest` (DIFFERS) - constants of an inlined PC callee (Calc.AngleDiff/ToRadians/AngleToVector, MathHelper.Pi...)
- `TowerFall.WrapMath.ShortestOpen` (DIFFERS) - PC 160/320 literals vs Switch WrapMath width static [0x4792130] (WrapMath.get_CenterX 0xbd1f80); set to 320 by Session.StartGame 0xb44530 unless MatchSettings+0x20 (wide/6p) -> 420; equal at 320
- `TowerFall.WrapMath.SignY` (DIFFERS) - constants of an inlined PC callee (Calc.AngleDiff/ToRadians/AngleToVector, MathHelper.Pi...)
- `TowerFall.WrapMath.Vec` (DIFFERS) - constants of an inlined PC callee (Calc.AngleDiff/ToRadians/AngleToVector, MathHelper.Pi...)
- `TowerFall.WrapMath.WrapHorizAngle` (DIFFERS) - PC 160/320 literals vs Switch WrapMath width static [0x4792130] (WrapMath.get_CenterX 0xbd1f80); set to 320 by Session.StartGame 0xb44530 unless MatchSettings+0x20 (wide/6p) -> 420; equal at 320
- `TowerFall.WrapMath.WrapHorizDistance` (DIFFERS) - PC 160/320 literals vs Switch WrapMath width static [0x4792130] (WrapMath.get_CenterX 0xbd1f80); set to 320 by Session.StartGame 0xb44530 unless MatchSettings+0x20 (wide/6p) -> 420; equal at 320
- `TowerFall.WrapMath.WrapHorizDistanceSquared` (DIFFERS) - PC 160/320 literals vs Switch WrapMath width static [0x4792130] (WrapMath.get_CenterX 0xbd1f80); set to 320 by Session.StartGame 0xb44530 unless MatchSettings+0x20 (wide/6p) -> 420; equal at 320
- `TowerFall.WrapMath.WrapHorizLineHit` (DIFFERS) - PC 160/320 literals vs Switch WrapMath width static [0x4792130] (WrapMath.get_CenterX 0xbd1f80); set to 320 by Session.StartGame 0xb44530 unless MatchSettings+0x20 (wide/6p) -> 420; equal at 320

## Skipped classes (not run in a versus simulation: UI/menus, editor, archives, quest/Dark World/trials, workshop, rendering/audio)

`Editor.*` (92 classes), Monocle.AudioLibrary, Monocle.CameraLayer, Monocle.Commands, Monocle.Draw, Monocle.GraphicsComponent, Monocle.Music, Monocle.NumberText, Monocle.ParticleType, Monocle.SFX, Monocle.SFXInstanced, Monocle.SFXLooped, Monocle.SFXVaried, Monocle.Screen, Monocle.Text, Monocle.TextureData, Monocle.TimerText, AmaranthBoss, AmaranthShot, ArcherPortrait, ArcherUnlockPanel, ArchivesAscensionPage, ArchivesAwardsPage, ArchivesController, ArchivesGlobalStatsPage, ArchivesPage, ArchivesQuestPage, ArchivesSessionPage, ArchivesStatsPage, ArchivesTipsPage, ArchivesTrialsPage, BGBigMushroom, BGCrystal, Background, BladeButton, BombParticle, BorderButton, BrowseWorkshopButton, CataclysmBullet, CataclysmEye, CataclysmShieldOrb, ClearAllData, CustomLevelSystem, CustomMapButton, CyclopsCeilingBones, CyclopsEye, CyclopsFist, CyclopsPlatform, CyclopsShot, DarkWorldBossTitle, DarkWorldButton, DarkWorldComplete, DarkWorldControl, DarkWorldDifficultySelect, DarkWorldGameOver, DarkWorldHUD, DarkWorldPlayerResults, DarkWorldPortal, DarkWorldVariantButton, DarkWorldVariantsSelect, DrawCredits, DreadBGTentacle, EditorButton, GifExportOptions, GifExporter, GoToWorkshopMapButton, GunnStyle, HUDFade, HumanoidEnemyRenderer, IntroControls, IntroScene, KeyboardConfigMenu, KingHead, KingIntro, Logo, MainMenu, MainModeButton, MapButton, MapCursor, MapRenderer, MapScene, MapSeedSelect, MenuBackground, MenuButtonGuide, MenuItem, MoonBreakSequence, OptionsButton, Particles, PauseMenu, PressStartText, QuestComplete, QuestControl, QuestDifficultySelect, QuestGameOver, QuestGauntletCounter, QuestPlayerHUD, ReplayViewer, RollcallElement, RoundEndCounter, Saver, SavingInfoScene, ScreenFlash, ScreenTitle, SubmitToWorkshopButton, SunkenCityRevealSequence, TFCommands, TeamSelectOverlay, TeamSelector, TipsScreen, TowerForgeRevealSequence, TreasurePreview, TrialsControl, TrialsLevelSelectOverlay, TrialsStart, TriggerArrowScreen, TurnToDustFastImage, UnlockData, VariantButton, VariantDisableAll, VariantPerPlayer, VariantPreset, VariantRandomize, VariantToggle, VariantTournament1v1, VariantTournament2v2, VersusAwards, VersusBeginButton, VersusCoinButton, VersusMapButton, VersusMatchResults, VersusModeButton, VersusPlayerMatchResults, VersusRoundResults, VersusSeedDisplay, VersusStart, VersusTowerButton, VersusTreasureDraft, VsyncAlert, WhiteArcherFreed, WhiteArcherPrison, WorkshopButton, WorkshopDataDisplay, WorkshopDiscovery, WorkshopListLoader, WorkshopModeOverlay, WorkshopNoLevels, WorkshopPortal, WorkshopRateOverlay, WorkshopSubscribeOverlay, XGamepadInput, YellowArcherReveal

## Misrounded float literals (2026-10-24, tools/re/round7.py)
The Switch build (BRUTE) wrote float literals with too few digits: 39 constants are a few ULPs off the PC's IL (mostly degree->radian
angles one ULP low; Spikeball.Update's 1f / 180f). parity.py compares to 4 decimals and could not see them. Found through a checksum
desync on Darkfang (seed 1704231922, spike ball rotation; with the Switch-only Spikeball Depth -170 vs the PC's -160). Fixed in
pcgameplay.cpp kGaps (online only): the 14 rodata words / instructions set to the PC's values; no PC code uses the Switch's values
(IL ldc.r4 count 0), so every reader of a shared word gets the PC's. Left out: Lava.DrawLight 0.20000003 (not a rounded literal,
drawing) and Mole.DrillUpdate 0.6 degrees (word shared with something else; quest). Rerun `round7.py` (from tools/re) to list them.

## The PC reference is FortRise's game (2026-10-28)
TF.EX players never run vanilla TowerFall: FortRise patches it (MonoMod rules), and the browser page does the same. The parity tools
compared the Switch with the vanilla exe, so two of the 39 "misrounded" constants above were not gaps at all: FortRise sets
Spikeball.Update's 1f / 180f to 0.005555555f (0x3bb60b60) and Arrow.get_SeekMaxAngle to 0.6981317f (0x3f32b8c2), the Switch's own
values ("the same for v1.3.3.1 and v1.3.3.3"). kGaps had made both the vanilla PC's, and spike ball towers desynced against the page
(smoke lobby 220639, seed 5966: Spikeball.RotatePercent a few ULPs apart from frame 60; CoreCLR reproduces the Switch's series with
the vanilla constant and the page's with FortRise's, bit for bit). Now: tools/re/pc-il.sh builds the reference (FortRise's patch step
from the browser harness, then ilspycmd), parity.py / round7.py read it (37 constant gaps, not 39), and tools/re/fortrise-diff.py lists
what FortRise changes against vanilla (286 methods, nearly all mod hooks that run vanilla with no mods; the numbers a versus match
uses: these two, TreasureSpawner.GetArrowShufflePickups' extra shuffle (already a [fortrise] rule), Engine.Update's TimeMult (TF.EX's
LevelPatch sets it in netplay on both sides)). Rerun both on a FortRise update.

## Tower sweep against plain TF.EX (2026-10-24)
Every versus tower (0-15) once, the Switch hosting the unmodified v0.22.2 page (browser worktree harness-dump: main #84 + the page's
?statedump and the local server's TFEX_FIXED_SEED), seed 1000 + tower, to tick 1800, state dumps every 120 frames on both sides,
statediff.py on the decoded states and a byte compare of the compressed ones. First pass: 13 of 16; three Switch faults (c70da06):
LZ4 block layout (MessagePack-CSharp's first buffer segment is 4096 bytes), gen::Snap's 64-element cap, JumpThru.counter unresolved.
After the fixes: 16 of 16 in sync with a clean GGRS checksum, compressed state bytes identical on every dumped frame but frame 0
(the Switch's first-frame Engine.TimeMult 1.0 vs TF.EX's 1.0000019: not checksummed, overwritten by the first Level.Update).
Not covered: wide (6-player) towers, variants, 3-4 players, levels a 1800-tick run doesn't reach.
