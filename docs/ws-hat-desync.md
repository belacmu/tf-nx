# The round-5 hat-vs-kill desync (console, 2026-10-17 06:56)

Branch `ws/hat-desync`. Status: **cause not found, not reproduced, no fix.** Parked by the user's decision (2026-10-17): bugs seen in play are noted, not chased, while TF.EX is still being completed. Both sides are moving to TF.EX v0.21.1.

## The match
Console on master 84e6ecd (write tracking, digest, notif 1), quick play (`ApplyNetplayVariantRules ... SYMMETRICAL TREASURE 1, NO
AUTOBALANCE 1`), seed 692511899, goal 5, map -1, console handle 1 (p1, archer 0, normal hat), page p0 (archer 8). Round ends at
912/1574/2587/3225, then on the console 3831. First DESYNC at 3840 (3720 matched).

## What happened at frame 3740
- 3706: p0 (the page's archer, at about (216,131)) shoots: `Arrow.Create #6 type 0 at (216,131) dir -0.785` (45 degrees up-right).
- 3740 on the **console**: p1 dies. Seat-1 death rumble (1.0, 40 frames) and the same two death sounds as its deaths at 1483 and 3134
  (sound ids 61bc60/8f21e0, pan 0.44 = x 301 by `SFX.CalculatePan`, so at p1's spot). No other sound: TowerFall's `Hat.CreateHat`
  plays `char_hatFall` only for a hat knocked off by an arrow, so the console's archer was **killed with the hat on**: the arrow hit
  the body, `Die` -> `LoseHat()` (no arrow).
- On the **page**: the hat was shot off (the user saw it) and p1 lived: at 3840, 3960 and 4080 it stands at (300,82), speed 0.
- RNG, replayed from the seed (xoshiro256**): the page at 3840 is 94 draws in, the console 95. The console's draws at 3740 (its dump):
  Range(float), Range(int), NextFloat, Range(int). That is `DefaultHat`'s constructor (Speed.Y Range(-1,-1.2), SineWave Range(50,80),
  Randomize) and one more draw of the death. The page made the 3 hat draws only. Its p1 died later (between 4080 and 4200), drawing
  that one death draw: at 4200 its RNG (`6a7bdaa7…`) equals the console's at 3840. So the death's draws match. Only the outcome of the
  hit differs: hat (page) vs body (console).

## Ruled out
- The Switch's hat check in `Player.Update` (+0xaef280, at 0xaf0120..0xaf01fc) is the PC's: `CannotHit (+0x138) != this`,
  `Dangerous`, `Speed.Y (+0x144) < 2.0`, `CollideCheck`, `HatsShotOff++`, `LoseHat(arrow, true)`.
- Arrow flight: in Ryujinx (Switch hosts, scripted shooting, vsstop 600, `atrace 0 600` vs the page's `ptrace=266-866`) the arrows'
  Speed bits agree on every matched arrow-frame (625 of 625; the 61 unmatched are the Switch's first-pass frames of rolled-back ticks).
  No FMA in `Arrow.ArrowUpdate`/`Arrow.Update`; DeterministicMath is ported (dm.cpp). Positions matched at 3720 in the real match too.
- TF.EX's PlayerPatch (a disconnected player dies, v0.21.0, `tfex/PlayerPatch.cpp`): it logs `player N disconnected`, not in the log.
- p1 was not moving: no p1 sound in the 90 frames before (jumps, landings); both sides have it at (300,82).
- A longer Ryujinx match with page latency (`netdelay=60&netjitter=30`, 2400 ticks, digest on): 0 of 2394, no DesyncDetected. But the
  scripts shoot few hats (one round, ~2 hat draws), so it says little about this case.

## Still open (where it can be)
The hit is decided by state the digest doesn't cover: the order in which the player's hat check (Player.Update) and the arrow's body
check (Arrow.Update -> CheckForTargetCollisions -> LevelEntity.ArrowCheck) run in that frame (if the arrow touches both boxes in one
step, the earlier one wins), the player's collider (normal vs ducking hitbox; the hat box sits on its top), HasShield, HatState, and
the arrow's non-digest fields (State/Dangerous, CannotHit). The console's input for p1 is local there and remote (predicted, rolled
back) on the page; p0's shot is the reverse. TF.EX restores state by TF.State's LoadState (entities re-created), the Switch by
restoring memory (entity order kept), so after a rollback the update order of a re-created arrow can differ between the two.
Nothing here is proven.

Leading suspects, in order:
1. **The TF.EX version difference.** The page ran stock TF.EX 0.19.2. The Switch ports v0.21.0 (lobby, NetplayManager, PlayerPatch, its
   TF.State mirror). A difference between the two in rollback state (what TF.State saves and restores for players, hats and arrows) or in
   a gameplay patch would show up exactly like this: a hit decided by state the digest doesn't cover. Re-test when both run v0.21.1.
   The feature-gap audit (docs/research-tfex-feature-gaps.md V.6) says the hat rule itself is the same in both (Player.LoseHat is
   wrapped alike). Its first suspect is a 0.19.2 TF.State arrow-snapshot bug: an arrow's CannotHit isn't restored correctly on a
   rollback when the shooter is dead (fixed in v0.21.0, daa6de7). Checked: the page's consoleLog can't confirm it. Its in-match lines
   (GGRS events, perf, digest) have no rollback or resim lines at all; GGRS on the page logs only WaitRecommendation (one at 06:56:37,
   about 10 s before the 3840 desync) and network events. Caveat: the shooter here, p0, was alive throughout (alive at 3840 on both
   sides), so the "shooter dead" condition doesn't obviously hold for this arrow.
2. Update order of the hat check vs the arrow's body check after a rollback (TF.State re-creates entities, the Switch restores memory).
3. Non-digest player state at the hit (ducking hitbox, shield, hat state) after a misprediction of p1's input on the page.

What would settle it (not available, see below): the console's `sd:/xplay/vs-log-3m.txt` from this match. The Switch's PlayerDieHook writes
`[vs] event death p1 cause <c> killer <k> at (x,y)` there (cause 0 = arrow; 9 = curse), with `(resim)` if it happened in a
re-simulated tick.

## "The round doesn't end when desynced"
Each side keeps simulating its own diverged world. The console's p1 died, so the console's round ended (3831) and the next started.
On the page p1 lived, so its round went on until p1 died there (4080-4200). TF.EX 0.19.2, which the page runs, only logs a desync
(`NetplayManager.cs:404`, `LogWarning`: the page's `[Warning][TF.EX] [NetplayManager] DesyncDetected ...` lines). TF.EX v0.21.0
ends the match ("DESYNC DETECTED - match ended"). The Switch ports v0.21.0's notification but deliberately doesn't end the match
(`tfex/NetplayManager.cpp:357-362`, TFEX-DIFF S-start). So against today's page, neither side ends the match. Ending it on the
Switch alone would leave the page playing against a frozen opponent.


## The vs-log evidence (not available) and a suggestion
The console's `sd:/xplay/vs-log-3m.txt` from this match would decide between the suspects. The Switch's PlayerDieHook writes
`[vs] event death p1 cause <c> killer <k> at (x,y)` there: cause 0 = an arrow (the body hit), 9 = curse (the disconnect kill), and
`(resim)` if the death happened in a re-simulated tick (a rollback). With per-frame lines on, the same file has every tick's state.
It couldn't be read: there is no SD access, and the console uploads only match-hw-log.txt at match end (devupdate). Neither upload of
this match (085712, 085928) has `[vs] event` lines.
**Suggestion:** have the console upload vs-log-3m.txt at match end, as it does match-hw-log.txt, so the next in-play desync comes with
its death events and tick lines.

## Next
Re-test after the browser moves to TF.EX v0.21.1, which also removes 0.19.2's arrow-restore bugs: a far shot at a standing archer's
hat, with rollbacks (page latency), digest on both sides.
