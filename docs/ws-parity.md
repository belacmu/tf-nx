# Workstream D: TF.EX parity (notes, 2026-10-15)

Branch `ws/parity`. Three tasks: (1) the remaining gameplay-RNG whitelist hooks, (2) TF.EX's sound handling around rollback,
(3) desync detection. Sources: TF.EX 0.19.2 decompiled (`.../tfex-analysis/src/`), its Rust library ggrs-ffi
(`Towerfall Browser/vendor/netplay-src/ggrs-ffi`), PC 1.3.3.3 decompile, the Switch 1.4.1.6 code (`tools/re/main.flat`).

Status in one line each:
- **(1) done, ready to merge**: 5 trampolines + 2 inline points in `rnd.cpp`, `teamspawn.hpp`, EnforceLimit gated on capture, and
  TF.EX's end-of-tick entity-list flush (F1, now implemented). Off-device test bit-identical to .NET running TF.EX's own
  `DeterministicRandom`. Ryujinx: `tools/run-vs.sh` (scripted LMS on Sacred Ground, rebased on a9f0072): all hooks installed,
  **0 of 4,497 ticks differ** from the browser reference; `tools/switch-vs-browser.sh` (Switch vs the real TF.EX browser, rollback on
  both sides: 154 Switch rollbacks up to 20 frames deep, 877 resimulated ticks): 1 of 2,996 ticks differs, T 2153, the known
  round-change logging artefact (ROLLBACK.md; rng equal), same as master. Sacred Ground has no orbs, spikeballs or team mode: the
  per-hook checks below are still to run.
- **(2) wired; audible playback works (`sfxplay 1`): 4 clean full rollback runs after fdd1cd4; still opt-in (the coordinator's call)**: `sfx.cpp` records SFX.Play / SFXInstanced.Play while capture is on, follows the rollback
  and reconciles once per real frame with `sfxsync.hpp` (= TF.EX's SFXService, bit-identical to a verbatim C# copy). The backend
  keeps virtual voices (real durations) and logs: making them audible needs a native player outside the GC heap (section 2).
- **(3) designed; shared parts done**: the digest definition (`digest.hpp` + its C# twin for the browser, identical on 40 cases),
  the GGRS plumbing in `ggrs-switch` (`tfx_session_new2`, `tfx_set_checksum`, Mac test `digesttest`). The in-game wiring
  waits for the browser patch.

---

## 1. Gameplay RNG: the remaining whitelist

All in `hw-probe/exlaunch/source/program/rnd.cpp` (offsets in `rnd::off`, each first instruction checked before hooking). How
each address was found and verified (PC source constants vs the Switch body, `tools/re-verify/fnspan.py`):

| TF.EX patch | Switch address | how it was verified | port |
|---|---|---|---|
| `OrbPatch` ctor `Orb(Vector2,bool)`: Reg/Unreg, postfix `ownerIndex = -1` while capture | `+0xad8970` | the only caller of `PlayEyeAnim` (+0xad8e90, reached from the metadata wrapper after the name `PlayEyeAnim`); body has `Hitbox(12,8,-6,-4)`, `Hitbox(12,12,-6,-6)`, `explodes` stored at +0x114, `SineWave(0x50)` + Randomize. `ownerIndex` +0x110: the shatter code (+0xad8f60) passes `[x19,#0x110]`, `[x19,#0x115]` (killed) to Explosion.Spawn | trampoline: RegGuard around the ctor, then `ownerIndex = -1` if capture |
| `SpikeballPatch` ctor postfix: `spinDir = Calc.Random.Choose(1,-1)` inside Reg | `+0xb61260` | caller of `new Random()` with `WrapHitbox(14,14,-7,-7)` (Depth is -170 on the Switch, -160 on PC), `exploding` at +0x110; the inlined `Choose` over `{1,-1}` stores into +0x118 = spinDir | trampoline: original ctor, then RegGuard + one `Next(2)` through the game's own `Random.Next(int)` entry on `Calc.Random`, `spinDir = i==0 ? 1 : -1` |
| `LavaControlPatch` ctor `(LavaMode,int)`: Reg/Unreg | `+0xa7a170` | `Calc.Range(0x258, 0x12c)` = Range(600,300) under `cbnz w1` (mode Pickup = 0); callers OrbLogic (+0xadb6ac/+0xadb70c) | trampoline, RegGuard |
| `LavaControlPatch.Added`: Reg/Unreg | `+0xa7a2d0` (vtable slot 0x30) | no direct callers (virtual); base.Added, `sfx_lavaLoop.SetVolume(0)`, two `Chance` calls (`fmov #0.5` + the 0.001 literal), four Lava ctor sites, `Target += 10` | trampoline on the body, RegGuard |
| `LocalSettingsGuard` around `TreasureSpawner..ctor` (B6) | `+0xb84e80` | the 4-arg ctor the `(Session, VersusTowerData)` ctor (+0xb84d90) chains to; calls GetItemExclusions (+0xabf540) and GetArrowShufflePickups (+0xb85610) | trampoline, `UnlockGuard` |
| `LocalSettingsGuard`: `Options.RemoveScrollEffects = false` | SaveData+0x18 -> Options+0x2b | three sites read +0x2b: the MainMenu toggle (+0xaa5a44, toggles then branches), OrbPickup's `if (!RemoveScrollEffects) DoSpaceOrbDelayed` (+0xadd0d4), GetItemExclusions' `Add(SpaceOrb)` (+0xabf724, `mov w1,#0x11`). OrbLogic reads +0x2a (RemoveSlowMotionWaveEffect) | added to `UnlockGuard` (now ctor + GetRandomArrowType + GetArrowShufflePickups) |
| `GameData.NoDarkWorldOverride = false` | n/a | `GameData.get_DarkWorldDLC` on the Switch is `mov w0,#1; ret` (+0xa46730): always true, the field is unused | nothing to do; **the browser must have DarkWorldDLC = true** (else trigger/prism arrows and DLC maps differ) |
| `RoundLogicPatch.SpawnPlayersTeams` (B2) | `+0xb39070`; inline points `+0xb39258`, `+0xb392d8` | Switch body differs from PC 1.3.3.3 (six-player build): team sizes via +0xabcc20, `TeamSpawnA/B` through a filtered GetXMLPositions(name, teamSize) +0xa7ed20 (adds an entry only if an attribute <= team size; plain 4-player levels: same as PC), split of `TeamSpawn` at half the screen width (160), Sort with comparators +0xb39750/+0xb39770 (identical to PC's SortTeamSpawnsLeft/Right), then `new Random().Next()`, two `Random(seed)` shuffles (+0x9677a0) of A (x21) and B (x25), Blue/Red spawn loops over 6 slots | inline before the shuffles: copy the sorted lists, draw TF.EX's `OwnShuffledIndexes(max(|A|,|B|))` on the gameplay stream; inline after them: overwrite the lists with `ApplyOrder` (`teamspawn.hpp`). Vanilla's unseeded Randoms stay cosmetic |
| `ArrowPatch.EnforceLimit` (B14) | `+0x9850e0` (already hooked in xplay.cpp) | was gated on `g_vsOn` | now `if (!rnd::Capture()) Orig(level)`: TF.EX's exact condition |

Not ported (and why it can't matter now): TF.EX's `TopUpSpawns` in team mode (only when a team has fewer spawns than players;
`VersusLevelData.TeamSpawns = min(|A|,|B|)` filters such levels out, and vanilla would throw there; the hook logs
"a team has no spawn" if it ever happens). `OrbPatch.Update` (cosmetic freeze of the sprite during resimulation: no RNG, no state).

Tests (off-device): `hw-probe/tests/teamspawn-test.cpp` vs `teamspawn-ref.txt`, made by `hw-probe/tests/refgen` (.NET 10) which
loads TF.EX's own `DeterministicRandom` from the release's `TF.State.Domain.dll` and runs verbatim copies of `Calc.Shuffle`,
`CalcExtensions.OwnShuffledIndexes`, `RoundLogicPatch.ApplyOrder` and SpikeballPatch's `Choose(1,-1)` (7 seeds x 9 list-size
pairs incl. unequal and empty lists; IDENTICAL). xoshiro/dnrandom tests still pass.

### How to verify each in Ryujinx (`tools/run-vs.sh` / `tools/switch-vs-browser.sh`, with `rngcapture 1`)
Boot line first: `[rnd] whitelist hooks installed (the 15 first ones + 6 of 6: ...)`; a `... isn't as expected` line names a hook that
was refused.
1. **Orb ctor**: a match on Twilight Spire (29 `Orb`) or Darkfang (17 `ExplodingOrb`). The `rng=` column of `compare-vs.py` must stay
   equal across the level load (without the hook it diverges right after the first Orb level loads: 1 draw per Orb, 3 per
   ExplodingOrb). An unowned orb that explodes must not credit player 1 (ownerIndex -1).
2. **Spikeball ctor**: Twilight Spire / Thornwood / Darkfang levels with a `SpikeBall`: rng equal after the load (1 draw), and the
   ball's spin direction the same on both screens.
3. **LavaControl ctor/Added**: a lava orb (LavaOrb or ChaosOrb) picked up: rng equal on the pickup tick and the tick after
   (ctor: 1 draw; Added: 1-2 Chance draws), lava on the same sides (left/right vs top/bottom) on both screens. Needs a script
   that opens a chest holding one (or `rngchains 1` to see the draw sites). With F1 in, Added runs at the end of the pickup tick,
   as in TF.EX.
4. **TreasureSpawner ctor guard**: in the Ryujinx save set `RemoveScrollEffects` on and lock SunkenCity/TowerForge/Ascension (or use a
   fresh save): chest contents and rng after the round start must still match the browser. (`tools/rebaseline-save.sh` restores.)
5. **Team spawns**: a Team Deathmatch match (mode 5). Log: `[rnd] SpawnPlayersTeams: |A| n |B| m, order of k drawn; first A (x,y)
   first B (x,y)`; both archers' spawn positions and the rng after the spawn equal the browser's (k-1 draws).
6. **EnforceLimit**: more than 12 arrows lying around (a long script that shoots and doesn't pick up): none may start decaying.

---

## 2. Sound around rollback

### What TF.EX does (TF.State.Patchs.SFX, TF.State.Domain.Services.SFXService, LevelPatch, LevelExtensions)
- `SFX.Play` and `SFXInstanced.Play` prefixes (only these two): while capture is active, not in replay viewing, `IsSfxCaptureActive`
  (= capture && !replay) and the scene is a Level: during a restore (`IsRestoring`: NetplayManager's state load) do nothing; else
  record `{Frame = Scene.FrameCounter, Name (file name, from the SFX ctor postfix), Volume, Pan = CalculatePan(panX),
  ObeysMasterPitch (SFX only), Data}` with `AddDesired` (no duplicate name+frame) and skip the original.
- **`SFXVaried.Play` (an override that doesn't call base) and `SFXLooped` are not patched**: they play immediately, also during a
  resimulation (TF.EX's own behaviour: those sounds can repeat after a rollback and are never cancelled).
- `Level.GetState` (GGRS save of frame f): `SaveSnapshot(f)`: drop entries older than 240 frames, store a copy for f (960 frames
  of history). `Level.LoadState(f)`: `UpdateLastRollbackFrame(f)`, `RestoreSnapshot(f)`.
- `Level_Update__Postfix`, after the real (last) update of a frame only (`!HasFramesToReSimulate && synchronized && connected`):
  `Synchronize(GGRS current frame)`: forget finished instances; pair desired with playing by name within 40 frames, closest
  first; start every unpaired desired sound with lastRollbackFrame <= Frame <= current (by frame); if lastRollbackFrame < current,
  stop unpaired playing sounds younger than 240 frames; lastRollbackFrame = current. A new level (`SkipLevelLoaderIfNeeded`)
  clears the desired list; netplay init/reset clears everything. Music is not touched.

### The Switch port
Why the game's own audio can't simply be used: the managed audio objects (SoundEffectInstance, the audio layer's active list and
pool, statics +0x4724770/+0x4724788) live in the GC heap, which the undo log rolls back, while their native voices keep playing;
that is why `SoundEffect.Play` (+0x46760) is muted today (ObjectDisposedException after restores). A sound played at real time T
is undone by any rollback to a frame before T. So:
1. **Capture** (hooks): `SFX.Play(panX, volume)` and `SFXInstanced.Play(panX, volume)` (find both bodies; the SFX ctor gives the
   file name): while `rnd::Capture()` and the scene is a Level, call `AddDesired` with `sound` = the `SoundEffect` object's address
   (`SFX.Data`; loaded once at boot, never moves or dies; TF.EX keys by file name, and the two only differ if two SFX objects load
   the same file: map those to one key if found), `frame` = the GGRS frame being simulated (`g_npFrame - 1` in xplay terms),
   volume, pan = `CalculatePan(panX)` (`(panX - 160) / 160` clamped? check the Switch body), pitch 0 / `ObeysMasterPitch`; skip
   the original. `SFXVaried.Play` / `SFXLooped.Play`: TF.EX lets them play directly; on the Switch they must go through the
   backend too (play immediately, unreconciled, mirroring TF.EX's duplicates).
2. **State**: `sfxsync::Reconciler` (`sfxsync.hpp`) is in the module's own memory, so the undo log doesn't touch it.
   `SaveSnapshot(f)` where a save happens (RbBeginTick / the last advance's BeginTick in xplay.cpp), `RestoreSnapshot(f)` +
   `UpdateLastRollbackFrame(f)` after `rb::Rollback` (the GGRS load), `Clear()` in SkipLevelLoaderIfNeeded, `Reset()` at match
   start/end. (Lockstep has no saves or loads: Synchronize alone then plays everything once, which is TF.EX's behaviour too.)
3. **Synchronize** once per real frame after the final Level.Update (`g_resim` false), with the GGRS current frame.
4. **Backend** (`sfxsync::Backend`): play/stop/state of a voice **outside the GC heap**: the game's native layer under
   `SoundEffect.Play` (+0x46760 calls +0x47f10, +0x480a0, +0x46380, +0x47050, +0x46ec0, +0x46d50: not yet mapped) or a small
   mixer of our own on `nn::audio` (the game links nn::audio's renderer API) fed with the SoundEffect's PCM. Volume
   `clamp(volume * Audio.MasterVolume, 0, 1)`, pitch `clamp(pitch + (obeys ? Audio.MasterPitch : 0), -1, 1)`, pan as recorded.
   This is the part that needs RE and a hardware check; everything else is ready.

### Wired (sfx.cpp, 2026-10-15)
- Switch bodies (vtables found through the relocations that point at the SFX.Play body): `SFX.Play(panX, volume)` +0x94a130
  (SFX vtable 0x46f7278), `SFXInstanced.Play` +0x94aef0 (0x46f72e0), `SFXLooped.Play` +0x94b700 (0x46f7348), `SFXVaried.Play`
  +0x94bfb0 (0x46f73e8). SFX.Data +0x10, ObeysMasterPitch +0x18, AddToPlayedList +0x94a310, CalculatePan +0x94a3b0. The Switch
  body differs from PC: the default panX is 1e8 = the screen's centre (`(int)width / 2`), and it plays `volume * MasterVolume`.
  `SoundEffect.Duration` = TimeSpan at +0x20 (getter +0x45f80).
- Capture (TF.EX's prefixes): while capture is on and a Level is updating, AddToPlayedList as before, then record {sound = the
  SoundEffect, frame = (int)Scene.FrameCounter, volume, pan = CalculatePan(the Switch's panX), obeysMasterPitch (SFX only)} and
  skip the original. SFXVaried / SFXLooped are left alone, as in TF.EX (SoundEffect.Play stays muted; SFXLooped plays through
  its own instance as before).
- State: SaveSnapshot(frame) at the start of each frame's Level.Update (resimulated ones too; not the nested first update),
  RestoreSnapshot + UpdateLastRollbackFrame after a rollback (to the frame actually resimulated from, which may be earlier than
  GGRS's when a late page forces it), Clear in SkipLevelLoaderIfNeeded, Reset at Session.StartGame, Synchronize after the frame's
  real update (never between resimulated ticks).
- Backend (for now): virtual voices that last their Duration in real frames; `[sfx]` log lines (the first 400 starts/stops) and
  counters every 1,200 frames and at vsstop.

Results (Ryujinx, `tools/switch-vs-browser.sh` with np 1 / npseat 0 / npdelay 2 / nppred 8 / rb 1 / rbprehot 1 / vsstop 4380):
- per-tick logs unchanged (4,378 compared, only the known T 2153); 222 rollbacks;
- 779 sounds recorded (re-recordings during resimulation are merged), 676 started, 51 stopped because a rollback removed them,
  **0 duplicates** (no sound started while the same sound of the same frame was still playing), every load found its snapshot;
- `tools/run-vs.sh` (no rollback): 639 recorded, 639 started, 0 stopped, 0 restarts, logs unchanged (0 of 4,497 differ).
- **Restarts are TF.EX's behaviour, not a bug**: a short sound that has already finished is started again when a rollback reaches
  back over its frame (SFXService removes finished voices before pairing, so the re-recorded sound finds no partner). In the first
  2,676 frames, 21 of 369 starts were such restarts (55-100 ms sounds). The counter `restarts (without a rollback to their frame)`
  checks that every restart follows a rollback to its frame or earlier: 0 in every run. Avoiding them would mean keeping finished
  voices in the pairing for up to MAX_SFX_DELAY frames: a deviation from TF.EX, so not done without a decision.

### Audible sound: the game's native voice layer (sfx.cpp, `sfxplay 1`)
What the game itself uses (all in the main module, verified by disassembly; imports mapped through the PLT):
- nn::audio's AudioRenderer directly (no FAudio/OpenAL): OpenAudioRenderer, InitializeAudioRendererConfig, AcquireFinalMix,
  AddDeviceSink, AcquireMemoryPool/RequestAttachMemoryPool, StartAudioRenderer (init +0x18130); per voice AcquireVoiceSlot,
  AppendWaveBuffer, SetVoiceDestination, SetVoiceMixVolume, SetVoiceVolume, SetVoicePitch (exp2f of the octave value),
  SetVoicePlayState, GetVoicePlayState, ReleaseVoiceSlot, RequestUpdateAudioRenderer; ParseAdpcmHeader for the sound data. Music
  streams through +0x17b10 (GetReleasedWaveBuffer/AppendWaveBuffer).
- A small native instance layer over it, one global mutex (+0x483be88/+0x483bea8), instances from `operator new` (the SDK's heap,
  not the GC heap): create +0x18720(manager = *(*(+0x4724738)), SoundEffect+0x28 = native sound) -> 0x30-byte instance (+0 manager,
  +8 native sound, +0x10 nn::audio VoiceType, +0x18 volume, +0x1c pitch, +0x20 1.0, +0x24 active, +0x28 list link); play
  +0x18b30(instance, loop) (releases an old voice slot, acquires one, appends the wave buffer, destination + mix volume, volume,
  pitch, Started); state +0x19310 (0 playing, 1 paused, 2 stopped/finished); stop +0x18ef0 (SetVoicePlayState + ReleaseVoiceSlot,
  no free); destroy +0x19430(&instance) (release + unlink + delete). Pan is not implemented natively (set_Pan's native call +0x19190
  is `ret`). The managed SoundEffectInstance keeps the instance pointer at +0x30; SoundEffect.Play's pooled path is +0x46380.
- sfx.cpp's backend with `sfxplay 1`: per started sound create + volume clamp(volume * Audio.MasterVolume, 0, 1) + pitch
  clamp(obeys ? Audio.MasterPitch : 0, -1, 1) + play(false); finished = state != 0; stop/dispose = destroy.
- Run (switch-vs-browser, rollback on, `sfxplay 1`): every start got a native voice, the state reads worked (voices finished by
  themselves), logs identical up to frame ~2,226, then **the game aborted** in the graphics-buffer assertion of ROLLBACK.md
  (`OnAssertionFailure` <- +0x20354 <- +0x20604, buffer SetData, a record retired twice) during Level.Update. Without `sfxplay`
  that has not happened in 6 runs. Most likely cause: native heap allocations and frees *during* the rollback window (create /
  destroy per sound), while ROLLBACK.md lists the SDK allocator object (+0x4903660) as still rolled back, so allocator state and
  memory go out of step and a graphics record gets handed out twice. Not proven (operator new is the SDK's import; graphics
  records use the game's wrapper +0x28617e0; whether they share the heap is not checked).
- **Pool (done, 5fb7101)**: the instances are a fixed pool of 48 structs in this module's memory, laid out as +0x18720 builds them,
  so nothing is ever allocated or freed: play re-points +8 at the sound (+0x18b30 releases the old voice slot and acquires a new
  one), finishing or stopping releases the slot with +0x18ef0 (stop: unlink, SetVoicePlayState Stop, ReleaseVoiceSlot,
  RequestUpdate; no free). The manager (+0x18130, `operator new`) and its instance lists (+0xd8 non-streamed, +0xe0 streamed) are
  in the SDK heap, outside the undo log.
- Three full rollback runs with `sfxplay 1` and the pool: two clean (4,374 / 4,377 ticks compared, only T 2153; ~685 sounds played,
  0 duplicates, pool never full, every restart after a rollback to its frame), one aborted again at frame ~2226 in the same
  graphics assertion.
- **The aborts are not the sound code: a rollback of ~20 frames at frame 2226 crashes without any audio.** All the crashed runs
  had a deep rollback at F 2226 back to <= 2208; `rbdrill 2226 20` (lockstep, `nppred 0`, sound muted/virtual) reproduces a crash
  right after the drill (+0x90d1b8 <- +0x94ddc0 <- Player update, the "invalid object" crash seen earlier), while `rbdrill 2230 25`
  survives. Audible sound only made such deep rollbacks likelier (native calls, RequestUpdateAudioRenderer included, add time on
  the game thread; deepest rollback 29 vs ~20). Repro: `tools/switch-vs-browser.sh "np 1" "npseat 0" "npdelay 2" "nppred 0"
  "rbprehot 1" "rbdrill 2226 20" "vsstop 2600"`. Rollback side (coordinator).
- `sfxplay` stays off by default until that rollback bug is fixed; the sound code itself has passed every run that didn't hit it.
- **After the coordinator's fdd1cd4** (the drill crash was partly a drill artefact: it skipped the real rollback's preparation):
  four full rollback runs with `sfxplay 1` (the fourth also with `rbcheck 1`): all four complete matches, no crash, per-tick logs
  identical but T 2153 (4,374-4,378 compared), 165-234 rollbacks, ~660-685 sounds played per match, 0 duplicates, every load found
  its snapshot, pool never full. One run counted 1 "restart without a rollback to its frame": TF.EX's SFXService also restarts a
  sound whose voice reports finished by the next frame (its frame equals lastRollbackFrame then), e.g. when the renderer had no
  voice slot left; it was past the logged window, so not identified. The counter's name overstates it: it flags that case too.

`sfxsync.hpp` is TF.EX's logic exactly (pairing order = OrderBy's stable order; stop only the voices that were unpaired before the
new ones were added; restore of a frame without a snapshot does nothing). Bounded arrays instead of lists (256 desired, 64
voices, 40 snapshots = the rollback window; an overflow is counted, not silent). Test: `hw-probe/tests/sfxsync-test.cpp` vs
`sfxsync-ref.txt` from `refgen sfx`, a verbatim copy of SFXService with a fake SoundEffectInstance on a scripted GGRS-like
timeline (4 seeds x 700 frames, 441 loads up to 8 frames deep, 331 stops): IDENTICAL.

---

## 3. Desync detection

### What TF.EX does
ggrs-ffi (`core/src/neplay.rs`): `DesyncDetection::On { interval: 120 * fps / 60 }`; every `SaveGameState` stores
`checksum = fletcher16(bincode::serialize(game_state.data()))` (the MessagePack+LZ4 state bytes from C#) in the cell. GGRS then sends
a `ChecksumReport` for the frame due (`last_sent + interval`) once it is confirmed, compares reports with its own, and raises
`DesyncDetected { frame, local, remote }`; TF.EX only logs that event in P2P (`NetplayManager`: warning). (The "Detected checksum
mismatch" path that ends the match is the SyncTest session.) The match-end message also carries SHA-256 of the last state.

The Switch can't produce TF.EX's serialized state, so a session with a Switch needs another checksum that both peers compute.

### The gameplay digest (version 1; `digest.hpp`, C# twin `hw-probe/tests/refgen/Digest.cs`)
Taken at GGRS's save of frame f (state at the start of f). Little-endian bytes, FNV-1a 64, sent as GGRS's u128 (high half 0):
`u32 version=1; u64 x4 gameplay xoshiro s0..s3; per seat 0..3: u8 alive (+ if alive: float bits of Position.X, Position.Y,
Speed.X, Speed.Y), alive = a Player with that PlayerIndex in Level[GameTags.Player] and !Dead; u32 arrow count
(Level[GameTags.Arrow]); the arrows' (X bits << 32 | Y bits) sorted ascending`.
Why these: they are the values the per-tick comparison (`compare-vs.py`, the browser's `[xplay2] T` lines) has already shown
equal on both sides for whole matches, they are cheap, order-independent (seat-indexed, arrows sorted), and they catch what goes
wrong first in practice (the RNG stream, archer physics, arrows). Version 2 candidates: scores, round number, miasma, chests/pickups.

### The browser patch (TF.EX in the browser)
1. ggrs-ffi: a way to give a save its checksum, e.g. `netplay_save_game_state_with_checksum(SafeBytes state, u64 checksum)` (or a
   session flag "digest mode" that makes `handle_save_game_state_request` take the checksum from C# instead of fletcher16).
2. TF.EX C#, in the save path (`Level.GetState` / `NetplayManager.SaveGameState`), when the session has a Switch peer (lobby
   flag; or always, in the browser build, if every peer runs the patch): fill `DigestInput` from the Level (xoshiro words from
   `StateContext.GetRng()`, players from `level[GameTags.Player]` by `PlayerIndex`, arrows from `level[GameTags.Arrow]`) and pass
   `GameplayDigest.Compute(...)`. Both peers of a session must use the same checksum function, or every report is a false desync.
3. Same interval as TF.EX: `120 * fps / 60`.
4. Optional: on `DesyncDetected`, show it (TF.EX only logs) and dump the digest inputs of that frame on both sides.

### The Switch side
- Done (`ggrs-switch/src/lib.rs`): `tfx_session_new2(..., desync_interval)` (0 = off, the old `tfx_session_new` calls it with 0:
  behaviour unchanged); the save cells are kept (last 128) and `tfx_set_checksum(session, frame, u64)` fills one; mismatches already
  arrive as event 7. Mac test `cargo run --release --bin digesttest`: an honest run reports nothing; checksums falsified from frame 150
  on are reported on both sides from 150. `selftest` still passes. **The Switch staticlib must be rebuilt (`ggrs-switch/build.sh`)**;
  from a worktree the `ggrs` path dependency (`../../../Towerfall Browser/...`) doesn't resolve: build from the main checkout.
- Wired (2026-10-15, off by default): hw.txt `npdigest 1` makes the session with `tfx_session_new2(..., 120 * fps / 60)` (ggrs-ffi's
  interval); at the two places where frame f's state is in memory before its advance (`RbResimTick` and the last advance), the
  digest of the current scene goes to `tfx_set_checksum(session, f, ...)` (`npdigest.cpp`: same reads as the browser, only the RNG
  when the scene isn't a Level, as the browser does); GGRS's event 7 logs `[np] DESYNC detected by GGRS at frame N` and
  `[digest] frame N local <hex>: rng=... p0=x,y,sx,sy ... arrows=N:...` (float bits; compare with the browser's `[digest]` line).
  Lockstep sessions make no saves, so no digests. The browser half: browser repo branch ws/browser-digest (06cd549),
  `netplay_set_pending_checksum` + a prefix on NetplayManager.SaveGameState, on with `digest=1` (xnet / xlobby / xplay2).
  **Joint test passed** (2026-10-15): Switch `npdigest 1` vs the browser on ws/browser-digest (now towerfall-browser main 467cc88)
  with xnet `digest=1` (`BROWSER_WT=<that worktree> SVB_PORT=18380 SVB_DRIVER_PORT=19700 XNET_EXTRA=digest=1
  tools/switch-vs-browser.sh ... "npdigest 1" "vsstop 4380"`): a full match with rollback on both sides (232 / 243 rollbacks), 0
  DesyncDetected on either side, per-tick logs identical but the known T 2153. Before that, a smoke test
  against the old browser (`npdigest 1`, vsstop 1500, the browser still on fletcher16): GGRS reports a desync on both sides at every check
  (frames 120, 240, 360, ...: the two checksum functions differ, as they must), the Switch logs its digest inputs for each, and
  the per-tick logs stay identical (0 of 1,496 differ). So the reports flow both ways; the real test is with ws/browser-digest.
- Seen once in five Switch-vs-browser runs (not reproduced in the rerun): a crash in Level.Update (+0x90d1b8, a call on an invalid
  object) 0.1 s after `[rb] WARNING: a late page wanted frame 2193, below the window's floor 2196`; the same warning appeared in
  the rerun without a crash. Rollback side (reported to the coordinator).
- Limits found by the test: lockstep sessions request no saves, so nothing is compared there; at high latency (12 frames one way in
  the test) GGRS stops reporting at all (the cell of the frame due is overwritten before that frame is confirmed and its fallback
  finds none), for browser-browser sessions too. A single bad frame can be skipped; a real desync persists and is reported.

---

## Findings outside the three tasks

- **F1 (fixed on this branch): TF.EX's end-of-tick entity-list flush** (`TF.EX.Patchs/Scene/LevelPatch.cs` `Level_Update__Postfix`
  -> `UpdateLayersEntityList`: `UpdateEntityList()` on the gameplay layer (`Layer.Index == 0`) and the HUD layer (`Index == 3`)
  after every Level.Update while capture is active, before SkipLevelLoaderIfNeeded). Without it, entities added during a tick
  entered the lists (and ran Added(), e.g. LavaControl.Added's gameplay draws) at the start of the next Scene.Update, i.e. after
  Level.Update's registered `OrbLogic.Update` (PC Level.cs: Session.OnUpdate, OrbLogic.Update, then base.Update): a lava orb
  picked up the tick before OrbLogic draws gave the two draws in the other order. Now `rnd::EndOfTickFlush` (called from
  LevelUpdateHook right after `Orig`, so also for the nested first update and the resimulated ticks, which call the same hooked
  body): `Layer.UpdateEntityList` +0x9371e0 on Level+0xd8 (the layer the Level constructor +0xa7aec0 gives SetLayer(0)) and
  Level+0xe0 (SetLayer(3)), each checked by `Layer.Index` (+0x38). It also fixes what the digest (section 3) sees at a save.
- **F2: Spikeball.Update replacement not ported** (`SpikeballPatch.Spikeball_Update`: while capture, delete the Spikeball's
  Coroutines and Tweens and drive the exploding spin from `SpikeballOwnedState`'s timer, `+Engine.TimeMult` per tick after the
  round starts). No RNG, but the exploding spikeball (Darkfang) moves differently from vanilla's coroutine if the timings differ.
- **F3: GameData.DarkWorldDLC is always true on the Switch** (+0xa46730): the browser peer must have the Dark World content enabled,
  or treasure exclusions (trigger/prism arrows) and the DLC maps differ.
- **F4: TF.EX itself doesn't guard `RemoveScrollEffects` in OrbPickup** (`if (!RemoveScrollEffects) DoSpaceOrbDelayed()`, a
  gameplay effect read from the local save): a TF.EX peer with that option set desyncs against anyone without it. The Switch's
  save has it off by default; keep it off.
- The metadata resolver now returns `SpawnPlayersFFA` +0xb38950 / `SpawnPlayersTeams` +0xb39070 (both verified), and still gives
  `Arrow.EnforceLimit` as +0x984ed0 (wrong; +0x9850e0 is right). Constructors can't be resolved by name (the `.ctor` records
  carry no wrapper); the ones above were found from callers/callees and constants.
