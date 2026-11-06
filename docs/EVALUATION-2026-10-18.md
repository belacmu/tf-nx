# Project evaluation (2026-10-18, master 941ba94)

This is a read-only evaluation: no code was changed and nothing was run on the emulator or the console. It covers the goals as the
user states them:
- **G1:** TF.EX's own code on the Switch.
- **G2:** taking new TF.EX versions should be cheap.
- **G3:** online play performs well.
- **M:** maintainability (organisation, docs, tools).

How claims are marked:
- **[F]** is a fact checked in the code or in git, with a file:line.
- **[J]** is a judgment.

Paths are relative to the repo root. `P/` means `hw-probe/exlaunch/source/program/`.

## Summary

- **The move to TF.EX's code has largely worked.** Hand look-alikes under tfex/ fell from 2,402 lines to 329 at 3e21ebc
  (docs/HAND-CODE-INVENTORY.md:30-36), and several more rows have been removed since. What is left is mostly real Switch glue.
  The largest block of TF.EX look-alike code is no longer in tfex/. It is the hand-ported **TF.State gameplay patches**: about
  500 lines in xplay.cpp plus most of rnd.cpp, outside the translator's scope (overrides.toml:558).
- **G2 is weaker than it looks.** The one-command bump (tools/tfex/new-tag.sh) has never run on a new tag; v0.21.1 was taken by hand
  ten minutes before the script existed. 192 `[service]` rules are keyed by member name, and nothing checks them against a new tag,
  so an upstream rename turns a Switch rule off **silently**.
- **Having `gen/` in git costs a lot.** 121 of the 954 commits since 10-15 touch `gen/`, and 26 merges had `gen/` changes on both
  sides. Most of the churn comes from one cause that is cheap to fix: the hand-written runtime is copied byte for byte into `gen/`.
- **G3 decisions currently rest on wrong numbers.** The frame-cost split wipes the GGRS, resimulation and rollback-load time before
  printing it. Log lines are cut at 299 characters. On the positive side, the console logs show where the time really goes: a
  resimulated tick costs about the same as a first-time tick (~6-8 ms with write tracking), so rollback depth, not bookkeeping,
  dominates at 3-4 players.
- **Housekeeping:**
  - 270 MB of Rust build output is committed (tfx-rtc/target-soft).
  - About 25 hw.txt research toggles and several finished "way back" flags remain.
  - The docs contradict each other on testing, publishing and the SD card.
  - The hand-code inventory and STATUS "Now" were already stale hours after they were written.

## Recommendations, highest value first

Effort scale: XS < 1 h, S ≤ half a day, M 1-2 days, L more than that. "Owner" follows AGENTS.md (A lobby, B match, T translator,
C coordinator).

### 1. Fix the measurements before any performance decision (G3; XS; no risk; B and C)

- **[F]** `FrameHook` resets `g_fc = FrameCost{}` at P/xplay.cpp:3500, after `NetplayGate` has already run at :3440. Inside the gate,
  GGRS (:3115), resimulation (:3047) and rollback load (:3178) add their time to `g_fc`, so it is wiped before it is printed. The
  console log shows `ggrs 0.00 | resim 0.00`. The `u0/u1` update timer (:3501-3503) also leaves the gate out, and idle samples over
  250 ms are dropped.
- **[F]** `net::Log` formats into `char line[300]` (P/net.cpp:180), and hw::Log has the same limit (P/hwlog.cpp:141). The
  `[hw] timing split`, `[rb] summary` and `[rb] timing` lines are cut off on the console.
- **[F]** `[rb] cost` lines stop for good after 1,500 per boot (P/xplay.cpp:3193).
- **[F]** About half the digest check frames print "no longer kept" (P/npdigest.cpp:56-66).
- **[J]** The 240 Hz analysis (CORE-FLOW "240 Hz") and any 3-4 player tuning would otherwise rest on these numbers. Also, its
  "game tick ~1.2 ms" figure is out of date: the 2026-10-18 console logs show ~3-3.8 ms with write tracking and 3 players.

**What to do:**
- Reset `g_fc` at the start of FrameHook and time the gate on its own.
- Keep frames over 250 ms in the stats.
- Raise the line buffers to about 700 bytes.
- Turn the `[rb] cost` cap into a periodic summary.
- Keep the digest frames that GGRS checks.

### 2. Make TF.EX updates checkable, then prove the routine once (G2; S-M; no risk; T)

- **[F]** new-tag.sh was added in 0bd89b5 (2026-10-17 09:50), after the hand bump eca9922 (09:40). Its summary says "Generated code: 0
  files changed" (tools/tfex/reports/v0.21.0-v0.21.1-summary.md:11), even though eca9922 regenerated gen/. CORE-FLOW still lists
  "the update routine proven on the next TF.EX release" as open.
- **[F]** `manifest.py check` validates only port, skip and replace keys. These are checked by nothing:
  - `[service]` (192 rules, overrides.toml:309-528)
  - `[[unit]]` members (57)
  - `[[rewrite]]` and `[[replace]]`
  - `[fortrise]` and `[invalid]`

  new-tag.py:73 lists `[service]` targets whose *body* changed, not targets that disappeared. If an upstream rename hits a rule's
  member, the rule stops applying and the call falls through to TF.EX's real PC body, with no error.
- **[J]** About 48 of the 192 `[service]` rules are not referenced in tfexgen_report.txt. Examples: ~25 `INetplayManager.*` rules,
  `GGRSFFI.netplay_*`, `ITfReplayApi.*`. They are either for parked units or leftovers, and nothing tells the two apart.
- **[F]** The translator reads four inputs from outside the repo (translate.toml:10-14,25-26):
  - the FortRise-patched DLL; without it the translator only warns, and its output differs
  - /Applications/TowerFall.app
  - the browser's FNA
  - a 0.19.2 zip for package references
- **[F]** This worktree's `vendor/tfex` is checked out at v0.21.0 while HEAD records v0.21.1 (`git submodule status`: `+04ef5d0 …
  (v0.21.0)`). new-tag.sh:21 would therefore treat v0.21.0 as "old", and a `git commit -a` here would move the pointer back.
  Other worktrees may be in the same state.

**What to do:**
- (a) Add a translator check that every rule key resolves at the tag and was used by some unit. Fail new-tag.sh on keys that don't
  resolve, and list unused keys as either `parked = "<unit>"` or for deletion.
- (b) Make translate.sh and new-tag.sh *fail* on a missing FortRise DLL and on a submodule checkout that differs from HEAD's pointer
  (or run `git submodule update` first). Record sha256 hashes of the outside inputs.
- (c) On the next TF.EX tag, run new-tag.sh in a clean worktree before touching anything, keep its log, and add
  `expect/v0.21.0-v0.21.1.toml` so the 12 unclassified v0.21.1 changes get rules.
- (d) Browser side: give the browser repo's tools/update-tfex.py a resolve-every-Harmony-target check (SwitchLobby, TfexFps,
  GameplayDigest, Replay*). Today that is a hand checklist (docs/research-tfex-feature-gaps.md:127-170). Also note that the browser
  is still on 0.19.2 while the target is 0.21.1 (STATUS.md:72).

### 3. Stop the `gen/` merge churn (M, G2; S; low risk; T)

- **[F]** All 10 runtime/ files are copied unchanged into gen/ (`Runtime.Copy`, tools/tfex/translator/CodeGen.cs:180; `cmp`
  identical). runtime/ had about 95 commits in 3 days because A and B edit `tfexgen_services.cpp`. Of the non-merge gen/ commits,
  68 of 110 touch these copies, and 24 touch only the copies and/or the report. That is where the "gen/ regenerated for
  Integration A's …" round trips come from.
- **[F]** Every unit writes into the same aggregate files: `tfexgen_report.txt` (changed in 69 commits), `tfexgen_binding.hpp`
  (50) and `tfexgen_hooks.cpp` (17). There is no `.gitattributes`.

**What to do:**
- Compile runtime/ in place, by adding it to the module's include and source dirs, instead of copying it.
- Split the report and binding.hpp per unit, or sort them deterministically.
- Mark `P/tfex/gen/** merge=ours linguist-generated`, together with a `tools/tfex/check-gen.sh` that regenerates into a temp dir and
  diffs. The coordinator runs it after each merge.
- Then let A and B regenerate for their own rule edits. Today only the translator session may commit gen/
  (docs/ws-tfex-bind.md:9), which turns every rule change into a hand-off between two sessions.

### 4. Remove the finished dual paths and dead code (G1, M; XS-S; low risk, build only; owners A, B, T)

AGENTS.md allows a flag only "as a short-lived way back". The following outlived that rule:

- **[F] `genstart` / `genend`** are on by default since aecb73f, yet `genend 0` keeps its own branch (P/xplay.cpp:3324-3328). The log
  at P/tfex/EndOfMatch.cpp:258 still says "the hand paths run", but those paths are gone.
- **`sessionpatch`** (P/xplay.cpp:308,396). **[F] `genmm`** and **`genmenus`** only log "ignored" (P/tfex/MatchmakingService.cpp:364,
  P/tfex/NetplayScreens.cpp:144). **`g_hudPatch`** is a constant `true` (P/tfex/EndOfMatch.cpp:64).
- **[F]** Dead MatchmakingService members: `NotifyMatchEnded`, which builds MatchEnded by hand (:422, no callers; generated HUDPatch
  sends it), `RematchChoice` (:432), `ArcherSelectChoice`, `RequestStart`, `IsCheckingConnections`, `SwitchPingMeasurementUrl` and
  `ResetLobby`. The `Gen()`-null fallbacks in the forwarders are dead too, because TF.EX's object is the only lobby now.
- **[F]** A **variants hand copy** runs beside the generated one. P/tfex/NetplayManager.cpp:335-337 calls the hand
  `ApplyNetplayVariantRules` / `ApplyVariants` (about 80 lines, :41-122). Generated `Auto_MatchVariantsExtensions::ApplyVariants`
  already runs from `ApplyGameSettingsToMatch` (gen/tfexgen_MatchmakingService.cpp:1092). The only real Switch difference is
  per-player variants (S-variants), and it belongs in a `[service]` rule, not in a copy. This one is medium effort: check it with a
  quick play and with a variants lobby.
- **[F]** Translator test hooks in runtime glue: `gendemo`, `gennetplayat`, `genbanner`, `genpanel` and `gencotest`
  (tfexgen_glue.cpp:129-137). Those classes are now generated and on.
- **[F]** About 25 hw.txt keys are used by no script. They are rollback and RNG research toggles: rbstudy, rbverify, findklass, atrace,
  crlog, vsfast, solidsdump, gentrace, pcitems, hotset, drill, rbdrill, rbcheck, rbglobstat, rbcoldlog, rbchangestat, rbabsorb,
  nppace, nojoinhook, padtest, onepad, notiftest, wtfpcr, enddebug and rngchains. 79 keys are parsed in total (P/xplay.cpp:337-428
  plus tfexgen_glue.cpp:129). **[J]** Remove each toggle together with the code only it reaches.
- **[F] A latent bug:** hw.txt is read into `static char buf[1024]` and cut off silently (P/xplay.cpp:345-346). smoke.sh *adds* lines to
  hw.txt, so the last keys can vanish. Make an over-long file a clear error, as the pools rule asks.
- docs/HAND-CODE-INVENTORY.md and STATUS.md:11 are already stale. STATUS still calls ws/match-gendefault "not on master", but it was
  merged in aecb73f. **[J]** Turn the inventory into a script (it is a classification of tfex/*.cpp by callers and markers), so each
  report regenerates it instead of hand-editing 500 lines.

### 5. Take the SD card and stray threads off the network path (G3; S; low risk, one console run for `[spike]`; B and C)

- **[F]** `net::Log` calls `hw::Flush()` after every line (P/net.cpp:183), on the transport thread that sends the GGRS packets. The RTC
  loop logs bursts of 4-5 lines every 10 s.
- **[F]** Once the 1 MB head is full, each flush rewrites the 512 KB tail (P/hwlog.cpp:212-223) while holding the hw::Log spinlock
  (:31), so a game-thread `[rb]` or `[desync]` line waits for it.
- **[J]** At ~0.3 s per 512 KB on the console, this matches the `poll … max 226 ms` in the RTC loop and GGRS's 500 ms "interrupted"
  events.
- **[F]** The rollback compare workers run at priority 12 on cores 1 and 2 (P/rollback.cpp:271). The transport thread is also priority
  12 and pinned to core 1 (P/net.cpp:1641). Horizon doesn't time-slice equal priorities, so `EndTick`'s join can wait behind a
  transport pass, and the other way round.
- **[F]** `netws::StartThread` (P/net.cpp:1468) sets no core. **[J]** Telemetry, devupdate, matchmaking and the hwlog writer therefore
  likely run on core 0, above the game's priority. The 82.6 ms Level.Update at lvTick 307 coincides with a mid-match DNS reconnect.
- **[F]** `[pause] frame N: menu buttons` is logged on every simulated frame while a button is held (P/tfex/MatchPause.cpp:49), with
  no limit: 1,429 lines, about 85% of one console match log. It also shows that `MenuPressed` reports the held level, not the press.
  That is the log-spam rule broken and possibly a behaviour bug.

**What to do:**
- `net::Log` only appends. The hwlog writer thread does the flushing, and writes the tail ring incrementally.
- Pin every helper thread off core 0 and below the game's priority.
- Put the compare workers and the transport thread on different cores.
- Limit the `[pause]` line to its first few occurrences, then a count.

### 6. Desync safety: what the digest sees, and in-window allocations (G3, correctness; S; low risk; B, T and C)

The parked kill desync is out of scope here, but the code shows gaps worth closing before it is investigated:

- **[F]** The digest covers only rng, position, speed, alive and arrows (P/npdigest.cpp:41-54). **[J]** Player state that decides hits
  (state machine, dodge and invincibility timers, shield, hat, duck) diverges unseen until a death. Add it on both sides, including
  the browser's GameplayDigest.
- **[J]** Module-held pointers to GC objects allocated *inside* the rollback window can dangle after a restore. Examples: the lazily
  made `g_side` from the in-tick `Player_Update_Postfix` (tfexgen_code_rt.cpp:131-135), `CoAlloc` frames (:99), `MakeUncollectable`
  containers (tfexgen_code_rt.hpp:369-374), and the model rings. Have each of them log loudly (or allocate up front) when called
  while `rb::Ready()`.
- **[F]** Generated code runs every frame *before* the gate: `MainMenuPatch::Update`, `DrainGameThreadActions` and `GetOwnLobby`
  (P/xplay.cpp:3412-3420). Any allocation it makes lands in the undo log.
- **[F]** Pools that still fail silently:
  - `AddRange` past `kMaxPg` after Init (P/rollback.cpp:50; only Init checks, :616)
  - `g_iv[512]` drops (:724)
  - `g_alloc[8192]`, only counted (:145)
  - `SideAll`, kept by hand (P/xplay.cpp:2637-2671), with no `static_assert` against `rb::kSideMax`
  - sfxsync `Snapshots = 40` < `kWindow = 48` (P/sfxsync.hpp:44)

  Route them through `pools::Exhausted`.

### 7. Cheaper rollbacks, and the input delay that is never applied (G3; M; medium risk; B and C)

- **[F]** The console's `[rb] cost` lines: depth 1 ≈ 11 ms, depth 3 ≈ 23 ms, depth 5 ≈ 38-42 ms. That is a fixed ~5 ms plus ~7 ms per
  resimulated tick.
- **[F]** EndTick still compares untracked pages every tick: module globals (1,014 pages) and Boehm metadata (133 pages), about 4.6 MB
  (P/rollback.cpp:800,825). The console's own probe reports that both can be made read-only ("reprotect probe … accepted").
- **[F]** `Rollback` unprotects and reprotects every heap section (P/rollback.cpp:950,964), so every resimulated tick faults again
  (11.1 µs per fault).
- **[F] A behaviour gap:** `SetSessionInputDelay` / `ClearSessionInputDelay` are `todo` no-ops (overrides.toml:399-400). The session
  delay is `g_npDelay = 2` (P/xplay.cpp:301), set only by hw.txt, and TF.EX's `InputDelayMs` from the settings store is never
  applied. **[J]** Implementing it is G1 (TF.EX's behaviour). It is also the cheapest G3 lever at 3-4 players: one more frame of delay
  removes most depth-1 rollbacks, at ~11 ms each.

**What to do, in order:**
1. Apply `InputDelayMs` (XS-S).
2. Split `[rb] cost` into scan, restore, protect and side-load.
3. Write-track globals and metadata.
4. Restore only the logged pages instead of `ProtectAll`.

### 8. Bring TF.State's gameplay patches into translator scope (G1, G2; L; high risk for determinism; T and B; after the move)

- **[F]** The whole `state/` tree is excluded as "state serialization" (overrides.toml:558). Yet 87 manifest entries are hand ports of
  TF.State *gameplay* patches:
  - 50 in rnd.cpp
  - 26 in xplay.cpp (Miasma, Icicle, Pickup tween, MoonGlass, Corpse, Bramble, SensorBlock, EnforceLimit, Arrow.Removed; roughly
    P/xplay.cpp:826-1300 and 1684-1697)
- **[J]** This is now the largest block of TF.EX code that only looks like TF.EX's. The manifest hashes flag upstream changes, but
  every change is still a hand port. Narrow the exclusion to serialization (TF.State's Get/Load state) and translate
  `TF.State.Patchs/Entity/*` the way the TF.EX patches are translated. Prove it per tower with the existing lockstep compares.

### 9. Remove 270 MB of committed build output (M; XS; no risk; C)

- **[F]** `tfx-rtc/target-soft/` is tracked: 1,017 files, about 270 MB, out of a ~290 MB tree. It was added in 066c619 (2026-10-17).
  `tfx-rtc/.gitignore` lists only `target/`.
- `git rm -r --cached tfx-rtc/target-soft` and ignore `target*/`.
- `tfx-rtc/vendor/` must stay: ggrs-switch/Cargo.toml:14,22,24 depend on it by path.
- **[J]** A history rewrite is cheap now, because the pack is still 67 MB. It is the user's decision, since every worktree shares the
  repo.

### 10. Split xplay.cpp and delete the feasibility probes (M, G1; M; low risk; C decides the owner)

- **[F]** xplay.cpp is 3,742 lines. About 650 of them are rollback-feasibility and research code that the real engine (rollback.cpp)
  replaced:
  - the math check run at every boot (:128-215, called at :3649)
  - the memory census and Trials-mode Save/Restore windows (:625-751, 1820-2139)
  - drill, hotset and FindKlasses (:2140-2336)
  - the Test 5 scripted versus (:752-825)
- **[J]** Delete the probes. Split the rest into:
  - `hwconfig.cpp` (the parser, with the overflow error)
  - `tfstate_ports.cpp` (ready to be swapped by item 8)
  - `lobbymatch.cpp` (start, end and session setup)
  - `netplay_gate.cpp` (gate, events, RbGate)

  xplay.cpp keeps the hooks, FrameHook and main. No section of xplay.cpp outside "match parts" has an owner today
  (AGENTS.md "Today").

### 11. Workaround rules to settle with the user (G1; decisions; A and B)

- **[F]** P/tfex/EndOfMatch.cpp:266-290 and overrides.toml:415 hold back TF.EX's `Disconnected` event, and the Switch ends the match
  instead. The reason given is "the guest crashed 0.3 s after the teardown". **[J]** By the user's own rule ("a workaround for a crash
  is not a reason") this should become a crash fix plus TF.EX's GetEventAndUpdate branch. It is parked by the user, so it needs their
  decision.
- **[F]** Smaller items:
  - `ResetPolledInput` is a `todo` no-op (overrides.toml:488), which skips upstream fix v0.20.1 #273.
  - `LastStateChecksum` is null (overrides.toml:45): MatchEnded carries no checksum for the server to compare.
  - Relayed sessions log "NOT supported on the Switch, connecting directly" (P/tfex/NetplayManager.cpp:300-310) with no ledger
    entry.
  - The `CancellationTokenSource` rules (overrides.toml:464,466) cite a crash. **[J]** The real reason is the platform's (no TF.EX
    tasks), so the wording should say that.
- **[J]** The single-trampoline-per-address limit in lib/hook is what forces hand glue in PlayerPatch.cpp, SessionPatch
  (P/xplay.cpp:1632-1670) and the six Pause hooks (P/tfex/EndOfMatch.cpp:116-144). Chaining trampolines (M, B) would let the
  generated hooks install themselves.
- **[F]** `AssignInputsHook` / `MenuUpdateInputsHook` (P/tfex/EndOfMatch.cpp:150-160) say "whether running it would harm wasn't
  tested". One `rematch` and one `quit` run would settle whether they are needed.

### 12. Docs, scripts and tests (M; S total; no risk; C)

**Contradictions [F]:**
- STATUS "Next" 1-4 (STATUS.md:189-194) plans an SD-card session and "lobby UI (now prioritised)".
- STATUS:24 says "brief emulator runs allowed"; AGENTS says "NO TESTING for now". AGENTS itself carries three generations of
  testing rules (10-17 afternoon, 10-18 11:2x, "test sparingly") with no note on which one applies.
- AGENTS's "Resources per workstream" table names four workstreams from 10-15.
- AGENTS lists 5 smoke scenarios; smoke.sh:4-19 has 12.
- STATUS:205-209 still points to run-vs.sh, switch-vs-browser.sh, make-sd.sh and hw-peer.sh.
- README.md describes the "feasibility probe" and exlaunch-overlay.
- LOBBY-PROTOCOL.md says "spec only, nothing implemented".
- MIASMA-PORT.md:3,124 cites a path that doesn't exist.

**What to do:**
- Keep one testing block in AGENTS (the current rule only) and replace the resources table with the three RJ slots.
- Cut STATUS down to "Now" plus links.
- Move the viability-era root docs into docs/archive/: VIABILITY-TESTS, TEST5-MATCH-SETUP, HARDWARE-SESSION, PLAN, MIASMA-PORT,
  BROWSER-RELAY, BROWSER-LOBBY-RELAY and TFSTATE-AUDIT.
- Move the ~13 finished ws-*.md files there too.

**Scripts and folders, [J] dead or superseded:**
- tools/archer3-test.sh: it writes hw.txt keys the parser no longer reads.
- lobby-observer.mjs and lobby-joiner.mjs.
- menu-walk.sh and compare-arrows.py (0 references).
- panel-check.sh and rematch-test.sh (covered by smoke).
- The relay-era echo-server.py, relay-*.mjs, hw-check.py, check-random-trace.py and crossplay-analyze.py.
- hw-peer.sh, make-sd.sh and sd-template (AGENTS bans the SD card).
- `exlaunch-overlay/`, an old copy of the probe.
- Archive or delete net-probe, browser-60hz and test-results (~12 MB of logs). Keep `test-scripts/versus/script*.txt`, which
  lobby-vs-browser.sh reads.
- relay-worker says it is deployed as `tf-relay-probe`. Whether that Worker should be taken down is the user's call.

**Tests [F]:** host-side unit tests exist but nothing runs them: hw-probe/tests/*-test.cpp with their reference files, and
tools/test-levelitems.sh / test-levelgen.sh. There is no CI. **[J]** A `tools/test-host.sh` that compiles and diffs them all needs no
emulator, so it fits today's "build-check every batch" rule (S).

**Build mirrors:** build-probe.sh's mirrors (`~/.cache/tfx-build/<name>-<sha>`) are never pruned when a worktree goes (XS).

## Process observations [J]

- **Merges:** 954 commits and 293 merges in about four days, many of them "merge master" plus "gen/ regenerated" pairs. Items 3 and
  4 remove most of the mechanical part. AGENTS already asks for "fewer, bigger" batches. The data says the gen/ hand-offs, not the
  sessions, are what force the small ones.
- **Status docs:** they go stale within hours (STATUS:11, the inventory, CORE-FLOW's 240 Hz numbers). Generated status (the
  inventory script, the coverage report, `smoke.sh` results) would stay accurate. Hand-written status should stay at a few lines.
- **What works well and should stay:**
  - the classified tag diff (update.sh)
  - the manifest's hash ledger
  - per-session Ryujinx slots
  - smoke.sh's single-line verdict
  - build-probe's unresolved-symbol check
  - the "pools must be loud" rule

## Suggested order

1. Before testing resumes, all build-only: items **1**, **9**, **4** (without the variants piece), **12**, and the logging parts
   of **5** and **6**.
2. Next, because each later TF.EX bump gets cheaper: items **3** and **2**.
3. On the next test day: the thread placement in **5**, the input delay in **7**, the variants piece of **4**, and the hook checks
   in **11**.
4. After the move: items **8** and **10**, and the engine work in **7**.
