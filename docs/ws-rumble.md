# ws-rumble: controller vibration in online play

The user felt the controller vibrate at the wrong times on the console during lobby matches against the browser. Master had no
rumble handling for netplay. Fix: `hw-probe/exlaunch/source/program/rumble.cpp` (new) and a few one-line calls in xplay.cpp.

## The game's rumble path (Switch 1.4.1.6)

Every rumble goes through one virtual call: `TFGame.PlayerInputs[seat].Rumble(strength, frames)`. Gameplay calls it for that seat's
own events (hit, death, catching an arrow, opening a chest, ...). `MenuInput.RumbleAll` and `RumblePlayers` call it for every seat.

- `TowerFall.JoyConInput.Rumble` +0xa62390 / `StopRumble` +0xa623a0 (vtable +0x470b830, slots 0x28 / 0x30) are 2-instruction thunks:
  `ldr x0, [x0, #0x18]; b JoyConData.X` (JoyConInput.JoyCon +0x18; size 0xb8).
- `Monocle.MInput+JoyConData.Rumble` +0x93e9b0 (size 0x90). Strength is multiplied by the vibration setting. If the result is
  stronger than `lastRumble` (+0x84), it becomes lastRumble, the `rumbleCounter` (+0x88, a Monocle.Counter whose value is the float
  at +0x24) is set to `frames`, and `GamePad.SetVibration` (+0xe5930, nn::hid) is called immediately. An equal strength only calls
  SetMax on the counter. A weaker one does nothing.
- `JoyConData.Update` +0x93e6e0 runs once per engine frame from MInput.Update, outside Level.Update, so it never runs in a
  resimulated tick. It counts the counter down and stops the motor (`StopRumble` +0x93e840) **only on the update where the counter
  reaches 0**.
- No other game code calls JoyConData.Rumble or StopRumble (only MInput.Initialize and Shutdown call StopRumble). The only other
  caller of SetVibration is a reflection invoke wrapper. TF.EX on the PC does nothing about rumble.

## What was wrong

1. **The remote players' events rumbled this console's controller.** At the lobby start, xplay.cpp gives every empty seat the console's
   controller object, as TF.EX's EnsureRemoteController does (a null slot crashed TreasureChest.OnPlayerCollide's rumble). On a
   console with one controller, that means every seat. So a remote archer's hits, deaths, catches and chests shook your pad. The old
   comment there already called it a "side effect". If you are seat 1 and your pad sits in slot 0, the remote player in seat 0 had
   your real pad object anyway.
2. **Rollback resimulation repeated rumbles.** A resimulated tick runs Level.Update again, so every rumble in the frames it covers is
   requested again. Because JoyConData.Rumble restarts the timer on a stronger rumble, an event could also start a second time,
   several frames late. A rumble from a predicted frame that the rollback undid (for example a hit that the real remote input
   avoided) kept running.
3. **The rumble timer was rolled back.** The JoyConData object lives in the GC heap, and the undo log restores the heap. A rollback put
   `lastRumble` and the counter back to their values at the frame rolled back to. If that was 0 while the motor was running,
   JoyConData.Update never sees the counter reach 0, so the motor **keeps buzzing** until some later rumble runs out. That is
   probably the most noticeable symptom. The opposite case happens too: a restored high `lastRumble` makes later, weaker rumbles do
   nothing. The same restore also put the pad's input state (the current and previous GamePadState, +0x14..+0x80) a few frames back.
   Then the next MInput.Update compared new buttons against old ones, which could make a held button count as pressed again.

## The fix (rumble.cpp)

- `rumble::MatchStart(localSeat, players)` runs right after the lobby start fills the empty slots. The local seat keeps the console's
  controller object. Every other seat of the match that held it gets a controller object of its own, which the game's own constructor
  makes in the GC heap: `JoyConInput.ctor(null, index)` (+0xa61690). With `this` null, the constructor allocates the object and
  returns it, as TFGame does for each pad. The index is the console controller's JoyConIndex (+0x20), so the new object wraps the
  same JoyConData, and its reads (GetState, Attached, icons) behave exactly as before, but it is a different object. The
  PlayerInputs array references it, so the GC keeps it. (The first version used a byte copy in the module's .bss; see "The console
  hang" below.) `MatchEnd` (in EndLobbyMatch, before the filled slots are emptied) stops the vibration and
  puts the controller object back.
- `JoyConInput.Rumble` / `StopRumble` are replaced (exlaunch replace hooks; the 16-byte thunks are checked before installing).
  Offline, and whenever no lobby match is on, they do exactly what the thunks did. During a match:
  - only the local seat's object reaches the motor. Another seat's object (made at match start, or another controller of this console in a remote seat) is
    dropped. Another seat's StopRumble is dropped too, because it would stop yours.
  - Inside Level.Update, every rumble is remembered as (frame label, strength, frames). A resimulated tick that requests the same
    rumble again is dropped. A rumble that only the resimulation produces (an event the rollback found, for example a hit by a
    remote arrow that was mispredicted) starts with the frames already gone subtracted, or is skipped if it would already be over.
    After a rollback (`OnLoad`), the rumbles of the resimulated frames are marked stale. Any rumble that the resimulation did not
    request again is cancelled at the end of the real frame (`Synchronize`): the motor stops, and the strongest rumble that should
    still be running is restarted for what is left of it.
  - Outside Level.Update (menus during the match), the local seat's rumble goes straight through.
- `rumble::KeepSave/KeepRestore` (called from xplay.cpp's KeepSave/KeepRestore, the rb::KeepOps) keep the local controller's JoyConData
  (0x90 bytes) and its counter value out of every restore. The controller is not game state. This fixes the stuck motor (3) and the
  stale input state.
- Logging (hw-log.txt, at most 300 lines per match): `[rumble] seat S frame F [(resim)] strength X frames N[: what happened]` for each
  request, `cancelled by a rollback`, plus a match start line and a match end summary (fired, resimulated repeats dropped, other
  seats dropped, started late, cancelled).

xplay.cpp changes (each one line): the include, `rumble::LevelUpdateBegin/End` around the Level.Update hook's Orig, `rumble::Synchronize`
next to `sfx::Synchronize`, `rumble::OnLoad` next to `sfx::OnLoad`, the KeepSave/KeepRestore calls, MatchStart in the lobby start's
slot-filling block, MatchEnd in EndLobbyMatch, and `rumble::Install()`.

## Verification in Ryujinx

Ran `tools/lobby-vs-browser.sh` (2 players, rollback: lobbypred 8, page `netdelay=40&netjitter=20`) on 2026-10-16. Ryujinx can't
vibrate, but every call is in the log.

- **Run A**, Ryujinx with all 6 pads connected: every seat has its own controller object, so nothing is filled or copied. Seat 0
  (this console) fired its own rumbles (Player.ShootArrow is 0.25 x 5 frames, the round start's RumbleAll is 1.00 x 30). Seat 1's
  rumbles were dropped, including its death (1.00 x 40) and a shot from a resimulated tick. The match was in sync (0 of 2206
  ticks differ) until it stalled at about tick 1950, Ryujinx being slow.
- **Run B**, Ryujinx with only Player1 connected, which is the console's case: `lobby start: 1 empty controller slot(s) of 2 seats
  given the local controller`, then `[rumble] match start: seat 0 keeps the controller ..., seats 0x2 get a copy without rumble`
  (the first version, with the .bss copy). Seat 1 (the copy) rumbles were dropped. Resimulated repeats of seat 0's own rumbles were dropped
  (`frame 1913 (resim) strength 1.00 frames 40: already felt, dropped`, which is your own death, felt once). The log also shows
  what the rollback would have done to the rumble timer: `restored the rumble timer 31 -> 0 (lastRumble 1.00 -> 0.00)` in the
  middle of that death rumble, and `9 -> 0` during a 0.80 one. On master, both mean a motor that never stops (cause 3). The
  timer was kept instead. The match reached vsstop in sync (0 of 2396 ticks differ), so the change has no effect on the
  simulation.
- Not seen in these runs: a rumble that only a resimulation found ("started late") and a rollback cancelling a rumble. Both code
  paths are simple, but neither has been exercised. `MatchEnd` (the stop and the summary) runs at EndLobbyMatch, after the
  results, and the vsstop runs end before that, so it is also unexercised in Ryujinx.

Ryujinx setting for run B: `XPLAY_RJCONFIG` with the default values plus `"input_config"` holding only the first entry of
Config.json's list (run-probe.sh restores the config after the run).

## The console hang (master 7fec2c2) and the rework

The first version (8314310, 3b1a6be) was published in master 7fec2c2. In a lobby match the console joined (local seat 1, so the real
pad's slot 0 became the remote seat and held the .bss copy), the game thread hung in frame 4139, where the local archer killed the
remote one. The last line was `[rumble] seat 0 frame 4139 strength 1.00 frames 40: not this console's seat, dropped` (telemetry
session 2653b751). There was no crash report, and the network and telemetry threads kept running. The coordinator reverted the
commits on master (312c297).

What I could rule out: on the Switch, `Player.Die` (+0xaee3d0) touches nothing of the input object after that Rumble call (next come
ScreenShake, LightFade, LoseHat). The dropped path only logs and returns. `KeepRestore`'s heap write happens while write
tracking has the heap unprotected (`rb::Rollback`: ProtectAll(false) before keep.save, true after keep.restore). In Ryujinx, the
same build ran a remote death through the copy (seed of run 5 below: seat 1 died at frame 2208, dropped, then also in a
resimulated tick) and played on past frame 3600 without hanging. Nothing found explains a hang, so the cause is not known. The
suspect is the one thing that was unusual: a managed object outside the GC heap (Boehm and BRUTE's runtime never expect one), shared
with the real controller's references. The rework removes it. The other seats now hold objects the game itself builds in the heap,
and hw.txt `rumblefix 0` turns the whole fix off.

Rework verified in Ryujinx (2026-10-17) in the console's exact setup: one controller connected (XPLAY_RJCONFIG with only Player1), the
page hosts, and the Switch joins as seat 1 (`LVB_PAGEHOST=1 tools/lobby-auto.sh -s 1100 ... "lobbyhost 0" "lobbyui 1" "lobbyjoin 1"
"lobbyscript 1" "lobbypred 8"`, page `netdelay=40&netjitter=20`). The log shows `[rumble] match start: seat 1 keeps the controller
..., seats 0x1 get a controller object of their own without rumble`. So the real pad's slot 0, the remote seat, holds a game-built
object, which is the slot that held the .bss copy on the console.
- **Run 6, remote archer's death:** `seat 0 frame 1913 strength 1.00 frames 40: not this console's seat, dropped`. The game thread
  went on (its next lines are at frame 1982) until the harness's 280 s limit ended the run.
- **Run 7, local archer's death:** `seat 1 frame 2208 strength 1.00 frames 40` was felt, and a rollback right after would have
  moved the timer (`3 -> 7`, kept). Seat 0's shots were all dropped, including those in resimulated ticks. The match reached vsstop
  (frame 2600) in sync: 0 of 2592 ticks differ.
- `rumblefix 0` was not run. It only makes MatchStart return before it changes anything, so the hooks pass every call through
  exactly as the original thunks did.

## What to feel for on the console

- The pad vibrates for **your** archer's events only: you get hit or die, you catch an arrow, you open a chest. Global events that
  rumble everyone in the vanilla game (MenuInput.RumbleAll / RumblePlayers) still rumble once.
- Nothing when the remote archer dies, gets hit or catches arrows.
- No buzzing that keeps going after the event, even in a laggy match with many rollbacks.
- A death caused by a late remote arrow (found by a rollback) still vibrates, a moment late and shorter.
- Nothing after the results screen, and nothing after leaving to the menu.
- Offline versus and the menus feel the same as before.
- If anything goes wrong, hw.txt `rumblefix 0` gives back master's old behaviour: MatchStart changes nothing, and the hooks do
  exactly what the game's code did.
- Possibly also noticeable: no double jump or shoot after a rollback while a button is held (the input state is no longer rolled back).

hw-log.txt lines to check: `[rumble] match start: seat S keeps the controller ..., seats 0x.. get a controller object of their own
without rumble` (on a
one-controller console every other seat should be in the mask), `[rumble] ... not this console's seat, dropped`, and the
`[rumble] match end` summary.
