# What TF.EX does, and what it means for Switch online play

From TF.EX 0.19.2 (`DShad.TF.EX` + `DShad.TF.State`), decompiled with ilspycmd on 2026-10-14. The
release zip is in the browser repo at `vendor/tfex/`. Items marked **(checked)** were read
directly in the code. The rest come from one read-through of the decompiled source; check the
code before building on them.

If the Switch is ever to play TF.EX players (or a custom TF.EX that keeps compatible), "the same
game" means **TF.EX's patched game**, not vanilla PC.

## Changes to our plan

1. **240 updates per second, not 60 (checked).** `Constants.NETPLAY_FPS = 240`, and the GGRS
   session runs at that rate. The `Level.Update` prefix (`TF.EX.Patchs.Scene/LevelPatch.cs`)
   overwrites these while capture is active or frames are being resimulated:

   ```csharp
   float num = (float)((Game)Engine.Instance).TargetElapsedTime.TotalSeconds * Engine.TimeRate;
   SetEngineTimeMult?.Invoke(num * 60f);   // 0.25 at normal speed
   SetEngineDeltaTime?.Invoke(num);
   SetEngineDeltaTicks?.Invoke(((Game)Engine.Instance).TargetElapsedTime.Ticks);
   ```

   Our determinism test (PLAN step 3) ran at 60 Hz with TimeMult = 1. The Switch must use this
   exact formula, with the same float roundings, and run 4 updates per frame. `Scene.OnInterval`
   is rescaled too (`SceneIntervalPatch.cs`). Some entities get explicit tick counters (bats,
   slimes, icicles, and others).

2. **TF.EX replaces Sin/Cos/Atan2/Pow (checked).** `TF.State.Domain/DeterministicMath.cs` holds
   software versions (fdlibm-style), and `DeterministicMathPatch.cs` swaps them in for
   `Math.Sin`, `Cos`, `Atan2` and `Pow`. That covers `Calc.AngleToVector`, `Calc.Angle`, SineWave,
   Wiggler, `Arrow.ArrowUpdate`, `Player.DodgingUpdate`, the easing functions, and more. It was
   written for Windows-vs-Linux desyncs. PLAN step 2c, matching the Switch SDK's math to the
   browser's and installing musl's pow, aimed at the wrong target: **port `DeterministicMath`
   to the Switch mod** and patch the same call sites. Both sides then run the same code. The
   `Math.Round` fix (step 2e) still stands; TF.EX doesn't touch `Math.Round`.

## Patterns to copy

- **Sound as reconciled events.** While capture is active, `SFX.Play` and `SFXInstanced.Play` never
  play directly. They record `{frame, name, volume, pan}`, the list is snapshotted every frame
  (`SFXService`), and calls during a restore are dropped. Once per real frame, never during a
  resimulation, `SFXService.Synchronize`:
  - matches recorded sounds to playing ones by name, within 40 frames;
  - starts the unmatched ones from the frames since the last rollback;
  - stops playing sounds that are no longer recorded (rolled-back sounds get cancelled).

  Music is never rolled back. This is the design step 6 of PLAN arrived at; the Switch hook point
  is `SoundEffect.Play` (+0x46760).
- **Two random number generators.** The gameplay generator is xoshiro256**, seeded from the lobby
  with SplitMix64, and its state is saved. `Calc.Random` is switched to it only inside a whitelist
  of gameplay methods (`CalcPatch.RegisterRng`): arrow and hat drops, treasure, chests, orbs, the
  crown, chains, spikeballs, miasma, corpses, `MatchVariants.Randomize`, and others. Particles and
  other effects keep their own generators. The Switch needs the same whitelist and generator,
  bit for bit.
- **Cosmetics freeze during resimulation.** These don't update while frames are resimulated:
  particles, backgrounds, the lighting layer, hair and orb visuals. Screen shake, hit-stop
  (`Level.Frozen`) and slow motion (TimeRate, `OrbLogic.Time`) are game state and roll back.
- **State is saved field by field, not as raw memory.** About 50 entity types have hand-written
  save and load code. Entities are keyed by their `actualDepth`, and references are stored as
  depths and re-linked after loading. Arrows, corpses and pickups are deleted and rebuilt from
  state. Coroutines (round intro and results) are stored as step counts and fast-forwarded on
  load. The state is MessagePack plus LZ4.

  This is the alternative to our raw-memory snapshot. It's smaller and immune to native-state
  problems like graphics and sound, but it's a lot of code to keep in step.
- **Desync checks.** GGRS compares checksums of the saved state, computed on the Rust side,
  presumably over the serialized bytes. Their desync tool flags Dictionary key-order differences
  and re-sorts layers by depth after every load, so ordering mattered to them.
- **Input** is 16 ints per frame: check and pressed flags, move x/y, the aim axes as float bits,
  and a disconnected flag. "Pressed" is OR-latched between polls. Default input delay is 2 ticks
  at 240 Hz. When ahead of the other side, the tick period is stretched by 10%.
- **Online restrictions:** 16 netplay-safe maps, some variants hidden or reset, warnings for mods
  that don't register their state, and warnings for modified game files and mixed OSes.

## Level paths are the PC's text; the Switch's file only where a file is opened (2026-10-24)
Three separate bugs came from two path forms meeting: `/` against `\` (nn::fs takes `/`, TF.State stores `\`), the Dark World towers
(the PC keeps 12-15 in `DarkWorldContent`, the Switch in `Content`: every Dark World tower's GGRS checksum differed once TF.State's bytes
became the checksum, with identical gameplay) and WiderSet's `mod:Teuria.WiderSet\...` root. The rule, in one place (pcgameplay.cpp
`paths::`): a level path in the game's data (GetLevels' list, so TF.State's RoundLevels and the checksum) is always the PC's exact text;
`SwitchFilePath` turns it into the Switch's file where LoadXML opens it. `PathsSelfTest` checks every tower both ways at install
(`[paths] level-path self-test: 96 cases, OK`). A new path source (another mod, another content root) is added to `paths::`, not patched
where it shows up. Finding such a difference: `statediff.py` on both sides' state dumps (hw.txt `statedump N`, the page's `?statedump=N`).
