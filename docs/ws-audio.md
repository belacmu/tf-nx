# ws-audio: music and sound in online play (2026-10-16)

The user reported that "music / sound does not work yet". This note covers what was silent and why, what changed (branch
`ws/audio`), and how it was checked. Code: `hw-probe/exlaunch/source/program/sfx.cpp` (sound and music), the
`SoundEffect.Play` hook in `xplay.cpp`. Background: PLAN.md section 6 (why the game's audio pool breaks under a restore),
docs/ws-parity.md section 2 (TF.EX's SFXService port, `sfxsync.hpp`).

## 1. What was silent, and why

| sound | before | why |
|---|---|---|
| `SFX.Play` / `SFXInstanced.Play` inside a match's Level.Update | audible (D's reconciler, native voice pool; `sfxplay` on by default since b5b87fc) | - |
| `SFX.Play` outside a captured Level update (menus, lobby screens, results), **offline too** | **silent** | they fall through to `SoundEffect.Play` (+0x46760), which xplay.cpp's `SoundPlayHook` turned into `return false` for every caller, at all times (the PLAN.md section 6 isolation test was never narrowed down) |
| `SFXVaried.Play` / `PlaySpecific` (arrow hits, many player sounds), **offline too** | **silent** | they call `SoundEffect.Play` directly (+0x94c1f4, +0x94c494): same mute |
| `SFXLooped` | audible | its own `SoundEffectInstance` (`SoundEffectInstance.Play`, not muted) |
| music (menu, lobby, levels) | audible offline and in matches (`AltTitle`, then the tower's song) | see below |

**Music.** `Monocle.Music` (`Play` +0x9410d0, `PlayImmediate` +0x941180, `Stop` +0x941240, `CurrentSong` static behind slot
+0x4791fc8, `MasterVolume` behind +0x4791fc0) drives XACT (MonoGame's: `SoundBank.PlayCue` +0x432d0, `AudioCategory.Stop`
+0x32130, `AudioEngine.Update` +0x357e0 from `Music.Update`). The waves are **in memory, not streamed**:
`WaveBank.GetSoundEffectInstance` (+0x4adc0) takes `SoundEffect.GetPooledInstance(forXact)` (+0x466d0), i.e. the *same managed
SoundEffectInstance pool* that a rollback restore corrupts; a streaming wave bank would throw (`PlatformCreateStream` +0x4ae90 is
`throw NotImplementedException`). The native streaming layer (+0x17b10 thread, stream create +0x18760, refill +0x17f60) is never
used: 0 streams, 0 refills in every run. The XACT wave's Update (+0x4d3e0) starts the native voice (native play +0x18b30 returns
to +0x4d45c; the menu song is one looping voice).

Music was not muted by anything, so it played; the risk was in matches:
- `Music.CurrentSong` and all of XACT's objects (cues, sounds, the playing instance) are rolled back with the rest of memory, while
  the native voice is not.
- The versus calls that land inside a match (PC IL: `RoundLogic.FinalKillNoSpotlight` -> `LevelSystem.StopVersusMusic` ->
  `Music.Stop`; `RoundLogic.CancelFinalKill` -> `PlayVersusMusic` -> `Music.Play`; the level loader's `Music.Play(tower song)` each
  round, a no-op for the same song; `VersusMatchResults` sequence -> `Music.Stop` + `ArcherData/TeamData.PlayVictoryMusic` ->
  `PlayImmediate`) each draw or return pooled instances and change `CurrentSong` inside the rollback window. A restore across one
  brings back a cue whose voice was stopped (XACT then loops it again: the song restarts after the final kill) and can lose a
  pooled instance (finalized -> disposed into the pool -> `ObjectDisposedException` at the next draw, PLAN.md section 6).

## 2. What changed

**SFX: no more blanket mute.** `SoundPlayHook` (xplay.cpp) now:
- offline (no `np`, no `rb`): calls the game's own `SoundEffect.Play`, unchanged vanilla behaviour;
- while netplay or the undo log is on (`g_npOn || g_rbOn`): `sfx::PlayOneShot` plays the sound once through sfx.cpp's fixed
  native instance pool (the one D built for the reconciler: 48 instances in the module's memory, never allocated or freed), and
  releases the instance when its voice reports finished (`sfx::Tick` every frame, and before each new one-shot). Volume and pitch
  are SoundEffect.Play's arguments (SFX.Play already multiplies by `Audio.MasterVolume`), clamped; pan is ignored natively.

So the game's managed pool is never touched by gameplay sounds during a match. These one-shots are the sounds TF.EX plays
directly: SFX.Play outside a captured Level update, and SFXVaried (which in TF.EX also plays again when a frame is simulated
again; so it does here). `SFX.Play` / `SFXInstanced.Play` inside the Level update stay with the reconciler (TF.EX's SFXService).
`hw.txt sfxplay 0` mutes both online paths (the reconciler falls back to virtual voices); offline is never affected.

**Music: held out of the rolled-back state.** While the undo log runs (`rb::Ready()`, from `rb::Init` to `rb::End`), the three
Music calls are hooked and XACT is left alone. The cue that plays when the window opens keeps playing, and its managed objects
don't change, so a restore only rewrites what was already there. The calls are recorded in sfx.cpp's memory (never restored), the
last one wins, and repeats are ignored (resimulated ticks; `VersusStart` asks for the song at every round start):
- `Stop` mutes XACT's music voices natively: native set-volume (+0x19260) is hooked and forced to 0 for the instances XACT started
  (recognised by the native play's return address +0x4d45c). XACT keeps setting their volume, and those values are kept for the
  unmute. Our own song (below) stops.
- `Play(the cue XACT plays)` unmutes them and stops our song (a cancelled final kill, when XACT already played the tower's song).
- `Play(another cue)` / `PlayImmediate(cue)` mutes XACT and plays the cue's wave **on one of sfx.cpp's pool voices**: looping for
  `Play` (the tower's song, "Boss"/"DarkBoss" in overtime), once for `PlayImmediate` (the victory music). The wave is found without
  playing anything (`ResolveSong`): `SoundBank`'s name -> XactSound[] dictionary (+0x30, the lookup PlayCue does), the sound's
  clips and their PlayWaveEvent (wave bank and track, variation 0), the wave bank's SoundEffect[], and SoundEffect+0x28 (the
  native sound). That is a dictionary lookup and field reads: nothing allocated, no pool touched. Volume: what XACT last gave its
  music voices (it follows the options' music volume); pan and XACT's fades and RPC curves are not reproduced.
When the window closes (the first frame or music call after `rb::End`), our song stops, XACT's voices are unmuted and XACT catches
up: `Stop` if the last request was Stop or PlayImmediate (a victory jingle at the next screen would be late), `Play(name)` for
another song.

Check: every time XACT starts a music voice outside the window, the resolver is run on `CurrentSong` and compared with the native
sound XACT really plays (`[music] resolver check <song>: ... ok / MISMATCH`).

Consequences: the menu song keeps playing (muted) under the match, and the match's song plays from our voice, through rollbacks,
from its first tick (`VersusStart` asks for it inside the window, frame 172 in the scripted run). The music stops at the final kill
(as in TF.EX), resumes if the kill is cancelled (from the start: our voice was stopped), and the victory music plays once. A song
change caused by a mispredicted frame is not undone when the rollback corrects it; the real game's latest request wins.

**Hooks.** The JIT area for trampolines (`setting.hpp` `JitSize`) went from 0x6000 (122 hooks) to 0x8000 (163). The first build
with the extra hooks overran 122: a hook xplay installs late (after boot) aborted in `AllocForTrampoline`.

**Logging.** `[music]` lines (song changes, the window opening and closing, held calls, XACT voices) and every 600 frames an
`[audio]` line: current song, music volume, Music calls, XACT voices, frozen/muted, SFX volume, native voice starts since the
last line, voices on the native layer's list, one-shots played (and refused for want of a voice), pool use.

## 3. Verification (Ryujinx)

Ryujinx's audio backend here is `Dummy` (volume 0), so nothing can be heard or recorded; the evidence is the native layer's own
voice starts and states in the guest log (Ryujinx's renderer still runs the voices: one-shots finish and are released).

Final build (rebased on master 7f1f45f):
- **Rollback match vs the browser** (`tools/switch-vs-browser.sh "np 1" "npseat 0" "npdelay 2" "nppred 8" "rb 1" "rbprehot 1"
  "vsstop 4380"`): **0 differing ticks of 4,378**, 224 rollbacks (1,107 frames resimulated, deepest 14), no exception, no abort.
  Menu: AltTitle from XACT, `resolver check AltTitle: ok`. Window opened before the level; `frozen Play SacredGround ... F 172`
  -> `playing SacredGround ourselves (native sound 212c120180, volume 0.40, looping)`, XACT muted; our voice still playing at
  every 600-frame check through the match; `frozen Stop` at F 4282 (the final kill). Reconciler: 641 recorded, 637 started, 9
  stopped by rollbacks, 0 duplicates, 32 loads all with snapshots, 1 restart (after a rollback to its frame). One-shots: 26 (menus,
  SFXVaried), never without a voice; pool use at most 3 of 48.
- **Offline scripted versus** (`tools/run-vs.sh "vsstop 2400"`, no np/rb): 0 differing ticks of 2,399; 0 one-shots (the game's
  own SoundEffect.Play runs); music live through XACT, AltTitle then SacredGround, `resolver check SacredGround: 212c120180 vs
  XACT's 212c120180: ok` (the same native sound our voice played in the match); reconciler 316 started, 0 duplicates; no exception.
- Earlier build (before the native song voice, music only frozen): 0 differing ticks of 4,378 (142 rollbacks), no exception.

- **Lobby match to its end** (`tools/rematch-test.sh`: INSTANT match, Switch hosts, one page, both vote REMATCH!), two tries:
  - Try 2: match 1 **0 differing ticks of 1,842** vs the page. Music: menu AltTitle (XACT, resolver ok) -> window open at sync ->
    `frozen Play SacredGround F 172` -> our voice, looping -> `frozen Stop F 1913` (the final kill) -> `frozen PlayImmediate
    VictoryBlue F 2035` -> `playing VictoryBlue ourselves (... once)` -> results finished -> `rollback window closed: last request
    PlayImmediate VictoryBlue (3 held calls)` -> XACT stopped -> the lobby screen's `Play AltTitle` through XACT again (resolver
    ok). No exception. **No second match**: 34 frames after the results the Switch logged "the remote stopped after the match
    results (relay 2) at frame 2058: ending ours too" while the page saw the Switch go quiet at its frame 2053, so the page never
    reached its MatchEnded and never voted (time limit). That is the end-of-match handshake, not audio (music only logs there).
  - Try 1: the page's WebRTC link dropped 1.6 s after sync (the Switch's rtc thread starved: "36 loops in 10 s, udp in 6392 ms",
    Ryujinx socket errors EADDRNOTAVAIL), before any music call; the Switch played on alone and its music sequence was the same
    as try 2's (SacredGround, Stop, VictoryBlue, window closed, AltTitle back).
  - The window opening a second time (a rematch) is therefore still unseen.

Not exercised: overtime ("Boss"); a rematch (above).

## 4. Still needs a check

- A rematch (the window opening a second time): tools/rematch-test.sh didn't get to match 2 in this session (end-of-match
  handshake, see section 3); on hardware, play two lobby matches in a row and listen to the second one's song.

- Hear it: menu music, the tower song through a whole online match (our voice: same loudness as XACT's? loops cleanly?), the
  victory music, menu and lobby sounds, arrow and player sounds (SFXVaried) in matches, the music stopping at the final kill.
- 48 native instances for reconciled sounds and one-shots together, plus the game's own voices (music, SFXLooped): the renderer's
  voice count on the console isn't known; watch `no voice` / `pool full` in the `[audio]` lines.
- Native calls from the game thread add a little time per sound (RequestUpdateAudioRenderer is inside +0x18b30 / +0x18ef0).

## 5. Follow-ups

- Our song voice takes the cue's first wave variation and plays it plainly: a cue with several variations, a loop region, or
  XACT volume/pitch events would sound different from XACT (TowerFall's songs looked like one wave each; check by ear).
- SFXLooped plays through managed SoundEffectInstances (as in TF.EX). A loop started on a mispredicted frame and then rolled
  back could keep sounding until the game stops it; not seen in the runs.
