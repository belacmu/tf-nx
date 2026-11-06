# Making the Switch's code as close to TF.EX as possible (investigation, 2026-10-26)

**Status (2026-10-26, branch claude/tfex-auto-updates-switch-a92140):** steps 1 and 2 below are done.
- **Every TF.EX patch is on by default.** `gen::hooks::EnableAll()` at boot; the three hand enable lists and their method skips are gone.
  Each hooked game method has a generated call struct, and the Switch's own steps go around TF.EX's patches with
  `gen::hooks::<Type>_<Method>::Wrap(shell)` (shell gets `c`; `c()` runs TF.EX's patches and the original) instead of a second
  trampoline. Hand hooks on TF.EX patch targets: 12 -> 0 (hook-collisions.py). Deleted: tfex/PlayerPatch.cpp, xplay.cpp's
  EnforceLimit hook, pcgameplay.cpp's LevelEntity.Render / Arrow.DoWrapRender copies (TF.State does both now).
- **The ledger holds only real differences.** The translator writes tfex/gen/tfexgen_members.txt. update.py files changes to those
  members as "generated" (no port work), and `manifest.py prune` (run by new-tag.sh) deletes entries for translated code. Manifest
  562 -> 141 entries, skips 170 -> 71 (the memory-copy S-rollback skips included). v0.22.2 -> v0.27.0: 496 of 780 changes are
  generated; to triage by hand 144 -> 23.
- **Checked:** smoke `rematch` in Ryujinx PASS on 8534cdc (the first run on 86f6172 crashed: TF.State's Arrow_DoWrapRender, newly on,
  read a game array through a Harmony FieldRef wrongly; translator fixed). Hardware checklist lines: `[tfexgen] 93 of 93 TF.EX patch
  classes hooked`, `[tfexgen] hook Arrow.DoWrapRender call 1: ... =1`, `[lobby] voted REMATCH! (TF.EX's PauseMenu_VersusRematch)`.
- **The gameplay RNG is TF.State's own code (2026-10-26).** StateContext's RNG members, RngService, CalcPatch (the Calc.Random swap)
  and DeterministicRandom are generated; rnd.cpp's own stream (xoshiro.hpp, the seed, CalcPatch's counters, SaveRng / LoadRng /
  SaveSide) is deleted with 10 [service] rules (CalcPatch.* 6, IRngService.* 4), the four StateContext RNG [[replace]]s and their glue.
  The translator now makes a TF.EX class deriving from a corlib class a real subclass of it (DeterministicRandom : System.Random).
  What stays in rnd.cpp, TFEX-DIFF: .NET's Random algorithm in place of the runtime's JKISS32, and, because the AOT build calls
  Random's methods devirtualized, a type test in those hooked bodies that sends a DeterministicRandom to its generated methods (with
  the draw count and trace). Hardware log line: `[rnd] the first gameplay draw: on TF.State's DeterministicRandom ... Calc.Random is it`.
- **Sound is TF.State's own code (2026-10-26).** sfx.cpp, sfxsync.hpp and xplay.cpp's SoundEffect.Play hook are deleted (their reason,
  the undo log rolling back the GC heap, went with 2c843ea). SFXPatch / SFXInstancedPatch are units (State_SFX, State_SFXInstanced),
  SFXService / StateContext's sound members run as generated, and LevelUpdateHook calls TF.EX's `SynchronizeSfx` (State_Api) where
  TF.EX's Level_Update__Postfix does, before SkipLevelLoaderIfNeeded's `ClearSfx`. Music and one-shot sounds are the game's own again.
  Switch-specific, each TFEX-DIFF: SFX.Play's panX = 1e8 "centre" sentinel resolved before TF.State's prefix (xplay.cpp SfxPanX, a
  Wrap shell); SFXExtensions.ToSoundEffectInstance builds the instance with SoundEffect.CreateInstance (the Switch's MonoGame audio has no
  SoundEffectInstance(SoundEffect) constructor for TF.State's reflection; glue State.cs). Pins: SoundEffectInstance.get_State / Stop()
  (the binder had read State as a field past the object). Log: `[sfx] F <n> TF.EX's Synchronize started <name>@<frame>` and `... stopped <name>@<frame> (a rollback undid it)` (first 16 each),
  `[sfx] vsstop: TF.EX's Synchronize ran ...`.
- **DeterministicMath and MonoclePatch are TF.State's own code (2026-10-26).** dmath.hpp (the hand copy) is deleted; DeterministicMath
  is the unit State_DeterministicMath and dm.cpp's redirected Math.Sin/Cos/Atan2/Pow wrappers call it. TFEX-DIFF: DeterministicMathPatch
  is an IL transpiler (the Switch runs none), so dm.cpp still patches the four wrappers for every caller. Bit-exactness: the module is
  built with -ffp-contract=off (config.mk; no fmadd in the ELF outside libm's pow), and hw-probe/tests/dmath-test.cpp, now on the
  generated unit, equals the .NET reference (special values incl. NaN bits, 4 x 200,000-input hashes). For it the translator now
  emits .NET semantics generally: Math.Abs(double/float) as fabs (sign bit cleared), double.NaN / float.NaN as .NET's negative NaN,
  BitConverter.DoubleToInt64Bits / Int64BitsToDouble as bit casts. MonoclePatch is the unit State_Monocle (hooks on: Audio.Stop while
  capturing stops only the pitched SFX); its `typeof(Audio).GetField("pitchList").GetValue(null)` is read as the static field (the
  translator's reflection rule, as Traverse). Log: `[dm] self-test ... matches TF.EX's DeterministicMath on .NET`.
- **A lobby match's round logic is TF.EX's NetplayRoundLogic (2026-10-26).** Generated as a real TowerFall.RoundLogic subclass (its
  OnLevelLoadFinish / OnRoundStart / OnUpdate / OnPlayerDeath in the vtable), made by xplay.cpp GetRoundLogicHook (+0xb37f50) where
  FortRise's GetRoundLogic patch makes it on the PC (TFEX-DIFF S-start: no mode registry / mode 11 on the AOT build; Mode stays the
  lobby's 3/4/5). Deleted: the four OnLevelLoadFinish hooks/wraps and GenStart* holds in xplay.cpp, EndOfMatch's GenSetPending, the
  RoundSpawner glue and its 3 IRoundPlayerSpawner rules, 2 [replace]s. Translator: interface type tests against one class of ours,
  DynamicData members on our subclasses of a game type (TF.State's done / wasFinalKill / roundEndCounter), model lists of another
  capacity copied. Smoke lobby / rematch / team PASS (0/591, 0/593). Log: `[vs] round logic: TF.EX's NetplayRoundLogic ... (mode N)`,
  `[tfex] Spawning players at netplay frame ... (round N)`. docs/ws-netplay-roundlogic.md.
- **prerun-check.py runs again** (it looked for the deleted kStatePatches list; the default-on set is now tfexgen_hooks' kClasses). It
  found ITfStateApi.SetSpawnHatRules (the lobby's hatless spawns) in the ITfStateApi.* wildcard no-op: routed to TF.State's TfStateApi.
  ExFlags.IsRestoring is TF.EX's own field again: the gate does TF.EX's SetIsUpdating around RestoreGameStateBytes, and SetRestoring
  goes to TF.State (sync test 240 Hz: 0 mismatches to frame 67302).
- **TF.EX's LevelPatch is generated hooks (2026-10-27, docs/ws-netplay-loop.md step 1).** The Level ctor postfix, Level.Update's
  prefix / postfix and both HandlePausing prefixes run as TF.EX's code; xplay.cpp keeps Wrap shells for its test tooling only (tick log,
  vsstop, rumble, [sfx] log, statemeasure) and Scene.FrameCounter until NetplayLogic is generated. LevelUpdateHook / HandlePausingHook,
  rnd.cpp's EndOfTickFlush, the gate's hand ShowMatchStartWarnings and xplay's g_rbFrame are gone (IsRollbackFrame is NetplayManager's
  own field). Translator: reflection-made static property-setter delegates are the setters' calls. Log: `[tfexgen] hook Level.Update
  call 1: LevelPatch.Level_Update__Prefix=1 ...`, `[vs] Level.Update frame N: TF.EX's LevelPatch step: Engine.DeltaTicks 166667 ... (the session's step 166667: same ...)`; rule Game.TargetElapsedTime = the session's step in a session (S-60hz). Smoke rematch PASS.
- **The session rate is TF.EX's 240 Hz by default (2026-10-27).** tfex/SessionRate.cpp sets TF.EX's GGRSConfig.Fps per match after
  its UpdatePlayers: 60 for a lobby with a browser player (the browser's SwitchLobby rule; GGRS peers must agree), otherwise the
  OPTIONS toggle 60 HZ MODE (Switch-only, off = 240). GetSessionFps is TF.EX's generated one; deleted: svc::SessionFps, its
  S-60hz [service] rule, xplay's g_npFps (hw.txt `hz` is now a test override of the toggle). Log: `[np] session rate N Hz (lobby: ...)`.
- **Next:** xplay.cpp's gate (S-start), MatchmakingService.cpp's transport, and the hand Models.

The question (the user): not an automatic checker, but **our code as compatible as possible with TF.EX**, so a new TF.EX version is
mostly a regeneration. The server is someone else's work and isn't covered here. Below: where our code still departs from TF.EX, which
of those departures a real update hits, and what to change, ranked.

## Where we are

- The Switch runs TF.EX v0.22.2 plus our fork's two local-multiplayer commits (belacmu/TF.EX `lmp-v0.22.2`, 38c7b1e). Upstream is at
  v0.27.0: six tags, 16 commits.
- A large share is already TF.EX's own code: 76k lines of generated C++ in `tfex/gen/`. Next to it are 2.7k lines of hand TF.EX-side code
  (`tfex/*.cpp`, of which 1,860 are S: Switch differences), the 5k-line hand runtime, and 6.8k lines elsewhere in `program/` (xplay.cpp,
  rnd.cpp, sfx.cpp, ...).
- Dry run `tools/tfex/update.sh v0.22.2 v0.27.0` (`tools/tfex/reports/v0.22.2-v0.27.0.md`): 780 changed members. `translate.sh all v0.27.0`
  runs and leaves 11 `TFEXGEN_UNSUPPORTED`, in ranked, stats and menu code: method groups as delegates, `[]` field initializers, tuple `==`,
  `bool?` in a string, `typeof`, `TimeSpan.Minutes`, an SDL3 conversion, LINQ over IEnumerable<AwardInfo>.

## Where the code departs from TF.EX today

| seam | size | what it is |
|---|---|---|
| Which TF.EX patches run | 3 hand lists | `xplay.cpp:1091` (16 classes), `runtime/tfexgen_glue.cpp kStatePatches` (TF.State), and scattered `hooks::Enable` calls, some limited to certain methods (`FrModsBoot.cpp`: PlayerPatch's two methods only) |
| translate.toml `[[unit]]` | 161 units, 94 with hooks | an allowlist: a TF.EX type or patch class runs only if someone lists it (types reached from a unit come in as `Auto_`) |
| overrides.toml `[service]` | 307 rules | TF.EX fields and interface calls mapped to hand C++: INetplayManager 23, MatchmakingService 21 (hosted), IGameContext 17, ITfStateApi 15, FortRise 40+ |
| `[[rewrite]]` / `[[replace]]` + glue | 54 / 28, 5.6k lines of glue C# | calls redirected to glue, members replaced (S-performance, S-transport, ...) |
| Hand ports of lockstep code | rnd.cpp (CalcPatch, 30 manifest entries), dm.cpp (DeterministicMathPatch), sfx.cpp (SFXPatch / SFXInstancedPatch), xplay.cpp's start and load-finish hooks (S-start), teamspawn / pcgameplay (S-mode) | TF.EX lockstep code we run as our own copy: an upstream fix to it doesn't reach us |
| Our fork | 2 commits, +583 / -52 | local multiplayer (#283); since v0.24, upstream has its own local-seat code (`InputCodec` / `ArcherSeatInfo`, `InputService.EnsureLocalControllerSeat`) |
| `[pin]` (binary offsets) | many | fixed: the game binary doesn't change. Not a compatibility cost. |

## What makes an update hard (seen in the dry run)

1. **The ledger describes the old hand port, not today's code.**
   - 291 of the manifest's ~630 `at` pointers name functions or files that no longer exist (a rough name check). The hand code was
     deleted when the generated code took over. For example, the stale A item `OnQuickPlayMatched` points at NetplayScreens.cpp, which no
     longer has it; it's generated (tfexgen_NetplayMenus.cpp).
   - 45 `[skip]` entries say "S-rollback: the memory copy does it", but the memory copy was deleted on 10-22 (2c843ea): TF.State *is* the
     rollback. The dry run therefore files real lockstep changes under "Skipped": Ice / Explosion / Arrow / Brambles GetState / LoadState,
     the Brambles pool fix (3fc0a14), the explosion rollback fix (da5ba2f), MatchStats in GameState. These do come in through the
     generated `Auto_` closure, but **any of them that is a Harmony patch runs only if a hand list enables it**, and the report hides them.
   - Result: the report's "stale / skipped / new" sections no longer say where our code differs, so every update needs a person to
     re-derive it.
2. **Allowlists instead of "everything unless excluded".** A new upstream patch class, or a new patch method in a class we enable for
   certain methods only, doesn't run on the Switch until someone adds it to translate.toml and one of the three enable lists. This is
   silent: the build passes, the game just behaves like older TF.EX. 45 of the 144 Harmony patch classes at our pin aren't listed as
   units. Most are justified (TF.Replay, the input displayer, keyboard), but there are lockstep ones among them (CalcPatch,
   DeterministicMathPatch, SFXPatch, StateFrameDriverPatch, FadeLayerPatch, ChaliceGhostPatch, MonoclePatch) that are hand-ported or not
   ported at all.
3. **`[service]` rules on fields.** A hosted class (MatchmakingService) or a mapped interface breaks the translation when upstream adds a
   field or method: "field of a hosted unit's type without a [service] rule". MatchmakingService accounts for most of the dry run's 14
   stale B items.
4. **The translator learns new C# on update day.** Each tag brings 3-7 new constructs. A gap in an adopted unit fails the build.
5. **The fork overlaps upstream's new code.**

## What to change, by how much compatibility it buys

**1. Default-on: every TF.EX patch in scope runs unless the ledger excludes it with an S reason.** HookGen already knows every
`[HarmonyPatch]` class. Generate the enable list (`tfexgen_hooks.cpp`) from "every hook unit minus `[skip]`/`[replace]` at class or
method granularity", and delete the three hand lists and the per-method filters. Units go the same way: every type under overrides.toml
`[scope]` is translated and compiled. `[[unit]]` stays only for options (hosted, exclude-with-reason), not for inclusion. Then a new
upstream patch or fix runs on the Switch by default, and an exclusion is a visible, reasoned line.

**2. One ledger, derived from what actually differs.** Replace manifest.toml's hand-written `at` pointers with a generated list of the
real differences:
- translate.toml exclusions and `[[replace]]` / `[[rewrite]]`;
- overrides `[service]`;
- the S rows of `inventory.py` (hand code that stands in for a TF.EX member, by its TFEX-DIFF marker naming that member).

Delete the S-rollback skips that refer to the memory copy (or re-reason the few that still hold). The update report then has exactly
two sections that need a person: "TF.EX changed a member we replace" and "TF.EX added something an exclusion rule swallowed". Everything
else is regeneration.

**3. Generate the remaining lockstep hand ports.** These are the departures that cost a desync when TF.EX fixes something:
- CalcPatch (rnd.cpp), DeterministicMathPatch (dm.cpp), SFXPatch / SFXInstancedPatch (sfx.cpp): TF.State patches, the same kind as the
  70 already generated.
- NetplayRoundLogic (S-start / S-mode: "no FortRise custom mode 11 on the AOT build"). Re-check that reason now that game subclasses work
  (`brute::MakeSubclass`, used for FortRise archers): if the mode can be registered as a real subclass, the xplay.cpp start path,
  teamspawn and the round logics' load-finish hooks become TF.EX's code.

**4. Unhost the services; keep `[service]` only at platform seams.**
- Generate NetplayManager, GameContext and MatchmakingService as whole classes. MatchmakingService already compiles against stubs.
- Keep `[service]` / glue only where the console really differs: the WebSocket and relay transport, GGRSFFI → ggrs-switch, file I/O,
  FortRise's loader, the keyboard.
- Target: `[service]` from 307 rules down to the platform interfaces. New fields then need nothing.

**5. Make the translator cover all of TF.EX at the latest tag, not only the adopted units.** Today's 11 gaps are all general C#
features. Fix them as features, not per unit. Then keep `translate.sh coverage` at the newest upstream commit near 100%, so a new tag
rarely brings a new construct. (At v0.21.1, 81% of applicable methods translated; most blockers were the translator's own.)

**6. Fold the fork into upstream's design.**
- Rebase local multiplayer onto upstream's own seat code (v0.24+) and drop what upstream now covers.
- Offer the rest as the #283 refresh (STATUS: "ready for its author"). Until it's merged, keep it as small as possible.

**7. Send our S-performance changes upstream** (`docs/TFEX-UPSTREAM-CANDIDATES.md`: TF.State's per-layer entity index, the round-intro
replay, the MessagePack buffer). Each accepted one removes a `[[replace]]`. Upstream PRs are deferred today; that's the user's call.

**8. Prune the remaining TFEX-DIFFs to the "truly needed" bar (AGENTS.md).** Every `[[replace]]`, `[[rewrite]]` and S row gets a one-line
reason that is a console fact (60 Hz, no .NET, no clipboard, the transport), not a workaround.

## Suggested order

1 and 2 first. They're tooling-only, change no gameplay, and turn every later update from "re-derive where we differ" into "read the
short list of real differences". Then 3 (the lockstep ports: the desync risk), 5 (the 11 gaps, which also unblock v0.27.0), 4, and 6
while catching up to v0.27.0. 7 and 8 as ongoing hygiene.
