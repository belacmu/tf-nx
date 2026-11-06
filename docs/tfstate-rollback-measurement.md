# TF.State's state capture on the Switch: a measurement (2026-10-20, branch ws/tfex-update)

The question: should the Switch roll back the way TF.EX does? TF.EX captures and serializes the state with TF.State. The Switch today
uses a whole-memory snapshot with write tracking (rollback.cpp).

**Answer, in short:** TF.State's save costs about a tenth of our per-tick bookkeeping. The restore side (LoadState) isn't measured yet,
and the state the Switch captures is still incomplete. Speed is not the obstacle; correctness and the size of the work are.

## What was built (present, not switched on)

- **The state path, translated.** TF.State's `LevelExtensions.GetState` / `LoadState` and every entity extension they reach, plus
  `TfStateApi`'s serialize and deserialize (translate.toml units `State_Level`, `State_Api`, and their auto units).
- **The serializer.** MessagePack-CSharp 2.5's formatters for `GameState` (101 types) are generated as glue:
  `tools/tfex/translate.sh state-msgpack <tag>` writes `translator/glue/StateMsgPack.cs`. The encoding follows MessagePack-CSharp's
  rules: arrays of integer keys, compact integers, float32 / float64, str8, unions as `[key, object]`.
- **Compression.** `Lz4BlockArray` is a statement-for-statement port of MessagePack's own LZ4 codec (`runtime/tfexgen_state.cpp`).
  It uses lz4net's 64-bit encoder with a 2048-entry hash table, which is not today's lz4, so a stock LZ4 library would give different
  bytes. Blocks follow the 32 KB pooled segments, and payloads under 64 bytes stay raw.
- **TFEX-DIFF, the RNG.** `StateContext`'s RNG reads and writes rnd.cpp's xoshiro, the netplay's own (`glue/State.cs`).
- **The measurement hook.** hw.txt `statemeasure <every> [loadEvery]` turns it on; nothing runs otherwise.
  - Every `<every>`th first-time simulated tick of a lobby match: GetState, serialize and deserialize are timed.
  - The logs give sizes, a fletcher16 (as ggrs_ffi computes it: over bincode's u64 length plus the bytes), and a p50 / p90 / max
    summary every 300 ticks and at match end.
  - Every `<loadEvery>`th measured tick it also runs LoadState and the round trip.
  - A runtime check of the private-field offsets runs on live entities (`state-fieldcheck.py`; log lines `[state] field check`).
- **Gaps don't break the build.** What doesn't translate is a logged gap in TF.State's units only (`[state] gap ...`). An unresolved
  game field reads a zeroed stand-in; an unresolved call returns 0. Every other unit still fails the build on an untranslated construct.
- **Translator additions, reusable beyond TF.State:**
  - one unit per instantiation of a generic class (`Sprite<int>`, `Sprite<string>`), and one copy per instantiation of a generic method;
  - single-implementation interfaces under `state/`;
  - foreach over a game Dictionary's `.Values`, a game HashSet, and `Enum.GetValues`;
  - `ArrowCushion.ArrowData`, `MatchStats` and `KillStats` as game structs;
  - game array fields read through DynamicData;
  - `typeof(T)` inside generic copies.

## Numbers

Hidden Ryujinx lobby matches, slot update, 2 players, measuring every tick, LoadState off, random towers. Times in ms, p50 / p90 / max
over 300 ticks:

| run | GetState | serialize (LZ4 part) | deserialize | bytes (before LZ4) |
|---|---|---|---|---|
| 1 | 0.13 / 0.25 / 1.15 | 0.03 / 0.05 / 2.59 (0.01 / 0.01 / 0.09) | 0.04 / 0.07 / 0.56 | 728 / 883 / 940 (1661 / 1800 / 1807) |
| 2 | 0.14 / 0.28 / 1.16 | 0.03 / 0.05 / 2.78 (0.01 / 0.01 / 0.12) | 0.04 / 0.07 / 0.78 | 836 / 1030 / 1091 (2064 / 2333 / 2338) |
| earlier | 0.11 / 0.20 / 0.99 | 0.02 / 0.04 / 0.31 | 0.03 / 0.06 / 0.44 | 613 / 721 / 807 (1341 / 1480 / 1487) |

The maxima are the first ticks: first use of each code path, and the GC.

**Our rollback in the same runs:**
- per-tick EndTick (the save's equivalent): p50 1.98 / p99 7.83 / max 16.65;
- restore: p50 0.76 / p99 2.95;
- over 40 rollbacks: depth p50 11 / p90 20 / max 28, bookkeeping and restore 3.35 / 8.42 / 22.94, resimulation 2.53 / 7.30 / 14.67 per tick.

**LoadState and the round trip: not measured.** LoadState crashes partway (see the caveats), so no restore cost exists for TF.State
yet. Deserialize alone is under 0.1 ms at p90.

## Console estimate

The ratio comes from `.devserve/results/20261020-031326-match-hw-log.txt` (console, 2026-10-20) against the same work in Ryujinx:

| work | console p50 | Ryujinx p50 | ratio |
|---|---|---|---|
| rollback bookkeeping (memory-heavy) | 4.86 – 6.19 ms | EndTick 1.98 ms | about 2.5× |
| level ticks (CPU-bound game code) | 1.32 ms | about 1.35 ms per resimulated tick, net of its EndTick | about 1× |

TF.State's capture allocates and chases pointers, so it is estimated between those two ratios (1 – 2.5×).

| | TF.State (estimate) | today's memory snapshot (console) |
|---|---|---|
| save per tick, 60 Hz | 0.15 – 0.4 ms (p90 0.3 – 0.8) | 4.9 – 6.2 ms bookkeeping |
| save per frame, 240 Hz (4 ticks) | 0.6 – 1.6 ms | about 4 × the above: doesn't fit a frame |
| rollback of depth d (2 – 8), 60 Hz | deserialize + LoadState (unmeasured) + d × (1.3 ms tick + 0.15 – 0.4 ms save) | restore + d × 1.3 ms tick + bookkeeping |

**Resimulation dominates deep rollbacks either way.** At 240 Hz a depth of 8 frames is 32 resimulated ticks, about 40 ms of game code
alone. So 240 Hz on the console needs shallow rollbacks whichever way the state is saved.

## Correctness so far

- **Capture stays read-only for the game.** Full matches ran with GetState / serialize / deserialize every tick and stayed in sync:
  0 of 596 and 0 of 599 ticks differ.
- **One run went out of sync** (10 of 599 ticks). Something in the stubbed parts touches game state; the most likely suspect is a
  game method running on a zeroed stand-in.
- **One tower crashed** in an entity's GetState; not chased.
- **Round trip** (GetState → LoadState → GetState, the same bytes?): not reached; LoadState crashes in `LoadState`'s layer and tag
  handling.
- **Byte-identity with the PC: untested.** The encoding rules and the LZ4 codec are MessagePack-CSharp's own. But a comparison needs
  the browser page to log TF.EX's checksum (or the state bytes) for the same frame, and today's page can't.
  - Desktop TF.EX runs the codec's 64-bit encoder (the one ported).
  - The browser is WebAssembly (32-bit pointers), so it takes MessagePack's `_32` encoder. That has the same algorithm and should
    give the same bytes, but it is unverified.
  - A state over 32 KB is split at 32 KB here. MessagePack-CSharp splits where a write doesn't fit its segment, so the split is
    approximated (logged once). Today's states are about 2 KB.

## What doesn't translate yet, and its size

| piece | size | what it takes |
|---|---|---|
| private game fields with no offset yet: zeroed stand-ins | 88 (about 40 references: Brambles, EnemyAttack, Explosion, MovingPlatform, Orb, PlayerCorpse, TeamReviver, TechnoMage, TreasureChest...) | RE, field by field; ChaliceGhost isn't on the Switch at all |
| private fields pinned from leads (`tools/tfex/state-pins.toml`, 294 entries) | about 50 checked at runtime (the reference fields of entities present); the rest, value fields above all, not confirmed | the runtime check catches wrong reference fields: it found 3 (Player.triggerArrows, Scene.tagsToAdd, a Chain field read) |
| calls with no Switch code: `return 0` stand-ins | 30 | see below |
| Player scheduler action names (`ActionExtensions.ToNames`) | small | `Delegate.Method` is reflection the Switch's runtime lacks, and the Switch's Player lambdas differ (it schedules three; the PC build two). Needs a map from the delegate's target body to TF.State's names |
| TF.State's entity cache (LazyCache) | small | replaced by a plain dictionary in glue (no expiry) |
| Monocle.Cache pooling (`DropLiveFromEntityPool`) | small | the binding of `Monocle.Cache.cache` is unconfirmed: a gap |
| `ConditionalWeakTable` / `DynData` state (spikeballs, icicles, sensor blocks, moon glass, moving platforms) | medium | TF.State keeps extra per-entity state there; it needs Switch storage (a side table, as the translator has for DynamicData) |
| arrays of TF.State structs (`Vector2f[]`) | small | a translator construct |
| ArrowCushion stuck arrows, MatchStats snapshots | done | as game structs; the stuck-arrow path runs but isn't checked |
| `LoadState` | the main unknown | crashes; its entity re-creation and tag lists need the remaining fields and the pool / cache pieces |
| TF.State's game patches (about 3,000 lines: entity IDs, RNG routing, SFX) | large | needed only if TF.State drives the rollback (the Switch's hand ports cover part of it today: `tfstate_ports.cpp`, rnd.cpp) |
| the netplay wiring | large | TF.EX's save / load through TF.State in place of rollback.cpp's snapshot |

## Recommendation

**Speed is not the reason to keep the memory snapshot.** TF.State's save is about a tenth of today's per-tick bookkeeping. If its
LoadState is anywhere near its GetState, rollbacks get cheaper too. Fewer bytes per frame would also make 240 Hz saves affordable,
though resimulation still limits rollback depth at 240 Hz.

**The cost is correctness and size:**
- the remaining fields (88 unresolved, about 240 unconfirmed);
- LoadState and the round trip;
- byte-identity with the PC (needs a checksum log on the page);
- the per-entity side state;
- then the netplay wiring and TF.State's patches.

My estimate is two to three times the work spent so far.

**Suggested next step, if the user wants to pursue it:** make LoadState and the round trip pass on one tower, and add a state-bytes /
fletcher16 log to the browser page for byte-identity. Both answer the open risks cheaply, before any wiring.

## How to rerun

```
tools/tfex/translate.sh state-msgpack v0.21.1     # (only when TF.State's models change)
tools/tfex/translate.sh code v0.21.1 && tools/build-probe.sh
XPLAY_RJSLOT=update XPLAY_VISIBLE=0 tools/smoke.sh lobby "statemeasure 1 0"     # 1 60: LoadState + round trip every 60th measured tick
```

Lines: `[state] frame N: <bytes> (<raw> before LZ4), fletcher16 ...`, `[state] summary (...)`, `[state] field check ...`,
`[state] gap: ...`. A field offset is fixed in `tools/tfex/state-pins.toml`, then `python3 tools/tfex/state-fieldcheck.py` and a
translate.

## Plan for full adoption (2026-10-20, after an experiment on the field layouts)

**The experiment.** The Switch's private field layouts were today's bottleneck. Static ways to find them failed:
- the binary has no private field names, records or GC pointer maps;
- layouts computed from the PC IL with C++ rules agree with only 11 of 36 hand-verified offsets.

**A runtime way worked.** Log, for a live object of each class, every 8-byte slot holding an object and that object's type, then
match the slots against the PC field types (declaration order among same-typed fields). One run, 5 classes: 26 reference fields
placed, 17 of 17 agreeing with existing pins. Player's whole reference layout came from one log line.

Each phase has a marker that says it's done; a phase that misses its marker stops the plan for a decision.

| phase | work | done when |
|---|---|---|
| 1. Layout database, references | `statemeasure` gets a layout-only mode (no GetState: it can't crash): slot maps of every class seen, a few snapshots a match, several towers; `tools/re/layoutdb.py` matches them to the PC fields and writes the offsets | at least 90% of the reference fields TF.State reads are placed, 0 disagreeing with confirmed pins, the runtime field check finds 0 wrong |
| 2. GetState runs everywhere | the database replaces the reference pins; the remaining value fields come from the slots left between known ones (width, declaration order, privfield) | GetState + serialize run every tick, in sync, on 6+ towers (2 players), no crash |
| 3. PC oracle | the browser page dumps TF.State's bytes for chosen frames; `tools/tfex/state-diff.py` decodes both sides and lists differing fields | a diff of one frame from both sides runs |
| 4. GetState parity | fix what the diff names (fields, translation) | 0 differing fields over 600 ticks on 3 towers; fletcher16 equal to the page's |
| 5. LoadState | the same oracle for loads | the round trip (GetState, LoadState, GetState) is byte-identical every 60 ticks over a match; LoadState cost measured |
| 6. TF.State's patches | translate them, or keep a Switch port where one already does the job | listed and on, matches in sync |
| 7. Shadow mode | TF.State saves beside rollback.cpp; after each rollback, compare | 0 mismatches over 10 matches |
| 8. Switch over | TF.State drives rollback; rollback.cpp's snapshot removed | in sync on the console; 240 Hz checked separately |

### Progress: phase 1 started (2026-10-20 evening)

- **The dump works.** `statelayout 100` dumps slot maps without TF.State: tower 0 ran in sync (0 of 594 ticks differ), with 114
  classes and 381 maps. Pointers are followed only into readable memory (svcQueryMemory). The runs on towers 1–5 ended in a guest abort
  about 1:45 in, after the dumps were taken; not yet looked at.
- **`tools/re/layoutdb.py` (6 towers):** 179 reference fields placed, 6 conflicting (dropped).
  - 0 disagree with the confirmed pins.
  - 60 agree with the earlier leads. The 2 that differ (OrbLogic's darkness counters) were wrong leads: both said 0x10.
- **Marker: not met yet.** Of the 153 private reference fields TF.State reads, 67 have an offset (43%; the marker is 90%). The rest
  belong to entities a 600-tick, two-player match on a plain tower never spawns: special arrows, Brambles, Explosion, Lava, Miasma,
  Orb, enemies, Chalice.
- **Next step:** a tour that makes those entities appear (all arrow types and treasure, variants such as Brambles / Lava / Miasma,
  Dark World enemies) or creates them in a test scene, then the same matcher.

### Phase 1 after the entity tour (2026-10-20, late)

**Runs.** Hidden, slot update, `statelayout 10` (slot maps every 10th tick):
- towers 0–15;
- nine arrow tours (`lobbyvariants START WITH <X> ARROWS|ALWAYS BIG TREASURE|BOMB CHESTS|RETURN AS GHOSTS`);
- a team match with TEAM REVIVE;
- two runs with the long maps split over continuation lines (`[state] slots+`). The log cuts lines at about 500 characters: older
  dumps lost the tail of long classes such as Player.

**Dumper fixes.** It now reads type names safely (every word checked readable) and doesn't descend into runtime or collection types
(delegates led into Mono's internals and crashed). Most runs were in sync; a few with arrow variants showed 2–62 differing ticks or a
digest desync. Variant matches have desynced before; not chased here.

**`tools/re/layoutdb.py` now:**
- matches each snapshot alone and each class's snapshots merged, keeping a field only where all agree;
- confines a class's fields to its own byte range (the parent's size to its own);
- fills runs of same-typed fields with null holes;
- places base classes' fields first, in a second pass;
- trusts cut lines only for exact counts.

**Result** (`tools/re/layoutdb.json`):
- 382 reference fields placed, 0 conflicting, 0 disagreeing with confirmed pins.
- 104 agree with earlier leads; 5 leads were wrong: ArrowList.sortSet, OrbLogic's two darkness counters, ScreenTitle.image and
  ScreenTitle.currentTexture.

**Marker: not met.** Of the 135 private reference fields TF.State reads that can matter in versus netplay (18 more belong to
quest / trials / Dark World entities: Dummy, EnemyAttack, QuestSpawnPortal, TechnoMage, TrialsControl, ChaliceGhost), 105 have an
offset: 77% against the 90% marker. The 30 left:
- **Never seen (15).** Lava / LavaControl (no lava tower among maps 0–15), Miasma (needs a long round), TeamReviver (a teammate has
  to die in team mode), a Pickup base field, RoundLogic.miasma, and SpriteData (game data, not an entity: a separate read).
- **Seen but unplaced (15).** Player's four sprites (headBackSprite is always null; the hole rule needs a full map of Player) and
  deathArrow / lastCaught (usually null); PlayerCorpse's brambles / dropArrowAlarm / prismCoroutine; OrbLogic.control / spaceTween;
  Icicle.cannotHit / cannotHitArrow; LevelEntity.onFinish; LightingLayer.spotlight. These need the full (continued) maps over more
  snapshots, or the constructor alignment for fields that stay null.

**To reach the marker:**
- a tour of the lava towers and long rounds (miasma);
- a team match where a teammate dies;
- more full-map runs for Player and PlayerCorpse;
- constructor alignment (tools/re/ctorfields.py) for fields that are null in every snapshot.

The method keeps holding: wherever it places a field, it agrees with every confirmed offset.

## How the work continues (2026-10-21, the user's direction; revised the same evening)

The revision came from steps A–D:
- **Static sources did most of the work.** Step C went from 77% to 98% without one emulator run, and the compiled accessors were
  the strongest source.
- **The run that mattered found a crash a check could have predicted:** a gap used as an array.
- **The 90% marker was arbitrary.** For rollback, every field TF.State reads has to be right.

**Working rules:**
1. **Static first, runs last.** The game's compiled code is the ground truth: accessors, constructor stores, and the methods that
   read a field once its neighbours are known. A run is planned only for what no static source answers.
2. **Ranked sources; a conflict is an error.**
   - The order: confirmed pins (translate.toml), then reflection, then compiled code, then runtime slot maps, then the layout rule.
   - A placement never sits on bytes a stronger source holds.
   - Two sources disagreeing stop the build, with both named; they're never resolved quietly. (ArrowList.sortSet was resolved
     quietly once, wrongly.)
3. **One command rebuilds and checks everything** from the kept inputs: the database, leave-one-out, the raw-byte check, the
   score, conflicts and the pins. It reproduces the committed files exactly; a difference is a regression.
4. **Check before running.** Before any run, a static check lists every gap and unconfirmed pin, with how the generated code uses
   it. A gap used as an object, array or call is high risk (LightingLayer.spotlight crashed GetState). A run starts only with no
   high-risk item left; it confirms, it doesn't discover.
5. **One run captures everything.**
   - A run carries every capture at once: slot maps, the raw dump, the field check and GetState.
   - It uses the spawn scene, extended to what towers and hits bring.
   - No runs just to look around. Emulator time and battery are scarce.
6. **Pilot, check, then scale.** A new kind of run starts with one run and an automatic check of its output; only then a batch.
7. **Capture once, analyze offline; inputs in the repo.** Run data is kept in `tools/re` (slotmaps.tgz, rawdump.tgz), not in /tmp.
   A new question means a new script, not a new run.
8. **Deterministic coverage.** Entities are created on purpose in a test scene, not hoped for through towers and variants.
9. **Markers mean correct: 100%.**
   - An exception is named, with its reason and the evidence for it.
   - "Placed" (an offset exists) and "confirmed" (real bytes or two independent sources) are counted separately. A step's marker is
     on confirmed.
10. **One marker at a time.** Each step ends at its marker with a short report: the result, what's left, the next step.
11. **Scripts in the repo** (`tools/re`, `tools/tfex`).

**Steps** (A and B done; the step results follow below):

| step | work | marker | status |
|---|---|---|---|
| A. Spawn scene | hw.txt `statespawn`: the game's own factories create every gameplay entity TF.State handles | one run shows every listed class alive, no crash | met |
| B. Raw memory dump | hw.txt `staterawdump`: each class's instances to SD-card files; `rawcheck.py` | one file per class, sizes equal to the class sizes | met |
| C. Layout database | slot maps, compiled accessors and the layout rule between anchors (`layoutdb.py`, `accessors.py`, `layoutpredict.py`) | **100% of the versus fields TF.State reads placed**; 0 conflicts; leave-one-out 0 wrong, or each miss explained | 98% (5 prism fields); 2 leave-one-out misses, both explained |
| C1. One command | `tools/re/statedb.sh`: rule 3 | reproduces the committed files exactly; prints every check | to do |
| C2. Static confirmation | constructor stores and method-body uses as sources, with known neighbours as constraints | **100% of the versus fields confirmed** (real bytes, or two independent sources) | 163 of 245 pins on real bytes |
| C3. Pre-run risk check | rule 4. It also settles whether the quest / trials / Dark World classes can occur in netplay (TF.EX's modes): if not, they are a named exception; if so, they're in scope | 0 versus gaps; no high-risk item | to do |
| D. Pins and the field check | `statepins.py` → `state-pins.toml`; the runtime field check covers number fields | GetState on 6+ towers, plus the extended spawn scene (tower objects, a prism hit) in the same runs: in sync, no crash, every field check summary with 0 "not", and every pinned versus field checked live at least once | built; runs paused |
| E. PC oracle | the browser page dumps TF.State's bytes for chosen frames (the browser workstream's page; this step specifies the format and writes the diff script) | the same frames give **identical bytes** on the Switch and the PC (a diff that only runs is a tool, not the marker) | to do |
| F. Parity, then LoadState | fix what the diff names; then the round trip | GetState → LoadState → GetState gives identical bytes on every tower, and a rollback match stays in sync | to do |

**The order while the emulator is off:**
1. C1, then C2 and the 5 missing prism fields (static).
2. C3.
3. The extended spawn scene for D: written and built, not run.
4. E's format and diff script.

Then D's runs, as few as rule 5 allows.

### Step A result (2026-10-21): marker met

**Spawn scene.** hw.txt `statespawn <tick>` (TEST ONLY, glue `tools/tfex/translator/glue/StateSpawn.cs`, called from
`tfexgen_state.cpp` LayoutTick). At that tick, in a lobby match, the game's own factories create:
- each arrow type (`Arrow.Create`);
- each pickup (`Pickup.CreatePickup`, except Gem: it has no case there and throws);
- a treasure chest and an orb;
- each hat state (`Hat.CreateHat`: Hat is abstract);
- lava (LavaControl), miasma, an explosion and a super explosion.

30 ticks later, the last player dies (`Player.Die`). A TeamReviver is then placed on its corpse in mode 1, by player index; mode 0
looks up a team colour, which a versus corpse lacks, and throws.

**Run.** Hidden, slot update:
```
tools/smoke.sh lobby "statelayout 10" "statespawn 120" "lobbymap 0" "lobbyvariants RETURN AS GHOSTS"
```
- no guest exception;
- the log shows "11 arrows, 28 other entities", then "1 team revivers";
- every listed class has slot maps: the 11 arrow classes, every pickup class, TreasureChest, Orb, LavaControl, Lava, Miasma,
  Explosion, PlayerCorpse, PlayerGhost, TeamReviver, DefaultHat, Crown, Brambles and Player.

The digest desyncs, as expected: only the Switch spawns.

**What it adds.** `layoutdb.py` over the tour runs plus the spawn runs: 425 reference fields placed (was 382), 0 conflicting, 0
disagreeing with confirmed pins.
- `tools/re/statescore.py` (the scoring, now in the repo) counts the versus fields TF.State reads: **117 of 136 (86%)**, was 105
  (77%).
- The 17 quest / trials / Dark World fields are out of scope.
- The inputs are kept as `tools/re/slotmaps.tgz`: every slot-map file, duplicate lines removed, which gives the same database.
  Reproduce with:
  ```
  mkdir x && tar xzf tools/re/slotmaps.tgz -C x && python3 tools/re/layoutdb.py x/*.txt
  ```

**The 19 still missing are fields that are null, or that a slot map can't show.** That is step B's job: raw memory, plus the
constructors for fields that stay null.
- Player's sprites (bodySprite, bowSprite, headSprite, headBackSprite), deathArrow and lastCaught;
- PlayerCorpse.brambles, dropArrowAlarm and prismCoroutine;
- OrbLogic.control and spaceTween; Icicle.cannotHit and cannotHitArrow;
- LightingLayer.spotlight, Pickup.timeoutCounter and RoundLogic.miasma;
- DummyHead.image;
- SpriteData.atlas and sprites (game data, not an entity).

**Next: step B** (a raw memory dump of each class to files).

### Step B result (2026-10-21): marker met

**Raw dump.** hw.txt `staterawdump <n>` (TEST ONLY; it runs with `statelayout`'s walk, in `tfexgen_state.cpp` RawDump).
- Each object the slot-map walk visits is written whole to `sd:/xplay/raw/<Class>.bin`. Its size comes from its type record
  (+0x00).
- Each record is `[u32 'TFRW', u32 tick, u32 size, u32 the address's low word]`, followed by the bytes.
- Each class gets up to `<n>` records, at most 2 per tick, with its ticks at least 60 apart. That way the same entities appear
  across the match, which step C uses to see how number fields change.
- The folder is emptied when the option is read.

`tools/re/rawcheck.py <dir> [slot maps]` is the check:
- every file parses to its end;
- each record's size equals its class's instance size (rtypes.py);
- one type descriptor per class;
- every class with a slot map has a file.

**Run.** Hidden, slot update:
```
tools/smoke.sh lobby "statelayout 10" "staterawdump 8" "statespawn 120" "lobbymap 0" "lobbyvariants RETURN AS GHOSTS"
```
No guest exception. The desyncs are those of the spawn scene.

**rawcheck results:**
- 156 class files, 655 records, **0 wrong**;
- 159 classes with a slot map, **0 without a raw file**;
- 3 generic classes, such as `Sprite<T>`: rtypes holds the open definition, so their size isn't checkable;
- 90 classes recorded at 2 or more ticks. For example Player at ticks 10–250, PlayerCorpse and TeamReviver at 160–520, Miasma at
  130–490.

The log's 11 "failed" are boxed values (Int32, Single, GameTags, DtdInputState): under 16 bytes, so not objects with fields.

The data is kept as `tools/re/rawdump.tgz` (94 KB: `raw/` plus that run's `slots.txt`).

**Not covered:** classes the towers bring and tower 0 lacks (Icicle, the Dark World entities). The spawn scene doesn't create
those; step C will show whether they're needed.

**Next: step C.** Build the offline layout database including number fields:
- by width and declaration order between known neighbours, on these bytes;
- checked for sane values and for how the values change between ticks.

### Step C result (2026-10-21): marker met

**Finding: the Switch build's layout rule.**
- A class's instance fields lie like a C struct: the PC IL's declaration order, natural alignment, the first field right after the
  parent's last byte. Tail padding is reused: Bat's first field sits at 444, inside Enemy's 448. The size rounds up to 8.
- Two exceptions:
  - **Auto-property backing fields** (`<P>k__BackingField`) sit elsewhere, among the class's first fields, in an order the PC IL
    doesn't give. For example Arrow's StuckTo is at 232, FrozenPlayer at 240 and Fire at 272.
  - **Fields of the Switch's own** appear in a few classes (ArcherData).

**Tools, all offline and repeatable (`tools/re`):**
- **`accessors.py` → `accessors.json`.** Each backing field's offset, read from its compiled accessor: `get_P` / `set_P` is a
  single load or store at that offset. 252 of 351 placed; getter and setter never disagree.
- **`layoutpredict.py` → `layoutpredict.json`.** The rule, anchored:
  - known offsets are fixed: reflection, confirmed pins, accessors and layoutdb's reference fields;
  - the other fields, in IL order without the backing fields, go between them;
  - a run is kept only when the walk from one anchor lands exactly on the next, or ends at the class's size from its type record
    (with no backing field left unplaced).
  - Result: 2870 fields placed.
  - `--check` runs leave-one-out: each known offset is hidden in turn and predicted from the others. 1348 come back exact, 2 wrong
    (MapButton.MapXIndex and UnlockData.AscensionBoxOpen: a Switch-only field that alignment hides), 517 not predicted.
- **`rawverify.py <raw dir>`.** Every placed field checked on step B's real bytes, over the records of its class and subclasses:
  bools 0/1, finite floats, enums -16..4095, references 0, heap or module addresses.
  - **870 fields checked, 0 implausible.**
  - The check caught one predictor bug: a fixed field starting exactly at the cursor was overlapped. OrbLogic.darkened was put on
    `<Level>`; it is fixed now, and darkened is at 0x18.
- **`statescore.py --all`.** The marker: every field, from layoutdb + accessors + layoutpredict.

**Marker:**
- Of the 261 private fields TF.State reads that can matter in versus netplay, **245 have an offset (93%; marker 90%)**.
- **0 disagree with confirmed pins.**
- 166 of the 245 are checked on real bytes, all plausible. The other 79 belong to classes the spawn run doesn't dump.
- Quest / trials / Dark World: 15 of 36, out of scope.

**Still missing (16):**
- Wiggler.sineCounter;
- ArrowCushion's four fields;
- Brambles.tweenedOut, LightingLayer.spotlight, LoopPlatform.waiting;
- OrbLogic's three space fields;
- PlayerCorpse.prismCoroutine / prismFall;
- Prism's three fields;
- RoundLogic.miasma, TeamReviver.Mode.

These are runs that no two anchors enclose.

**Earlier leads (`state-pins.toml`).** 26 differ from the database.
- Where the bytes can judge, the leads lose. Arrow.stuckDir at the lead's 0xe4 holds implausible values; several leads share one
  offset for two fields (OrbLogic's counters, ShiftBlock's three).
- The rest are both plausible: the database's offsets are the anchored ones.

**Next: step D.** `state-pins.toml` is generated from the database; the runtime field check is extended to number fields.

### Step D (2026-10-21): built, the marker run paused

Emulator testing is paused (the user). The offline part is done; the marker (GetState on 6+ towers: in sync, no crash, the field check
clean) is not yet run.

**Done:**
- **`tools/re/statepins.py` writes `tools/tfex/state-pins.toml`** from the database.
  - Every private field TF.State requests gets a pin with its source (slot maps, compiled accessor or layout rule) and its raw-byte
    verdict.
  - The old leads are gone. A field the database lacks is now a state gap, not a guessed offset.
  - 245 pins: 163 checked ok on real bytes, 82 in classes the dump hasn't seen. 22 gaps, all of them quest / Dark World classes
    except PlayerCorpse.prismCoroutine / prismFall and Prism's three fields.
- **The runtime field check covers number fields** (`state-fieldcheck.py`, `CheckOne` in tfexgen_state.cpp): 262 fields, 146
  reference and 116 number.
  - Bools 0/1, finite floats, enums -16..4095, Vector2s.
  - A reference may hold a subclass (`IsA` walks the type record's base getter, +0x88); before, any subclass counted as wrong.
  - A string literal in the module image counts as a string.
- **Regenerated and built** (the translate and build-probe OK).

**What the one pilot showed (before the pause), and the fixes:**
- **Pilot:** tower 0, `statemeasure 1 0`, with the first generated pins.
  - The field check at the first measured tick was clean: 49 as expected, 3 null, 0 wrong.
  - Then GetState crashed in AddRoundLogicState: LightingLayer.spotlight was a gap. A gap reference is a zeroed stand-in object,
    and Array.get_Length on it throws in a game list indexer.
- **The cause was a layoutpredict bug:** placed backing fields still acted as sequence anchors at the PC IL's position (the end),
  so a run that ends at the class's size could never close. They now only occupy bytes.
  - spotlight is placed at 0xa8. The raw bytes show sine at 0xa0 and the size 0xb0; a Switch-only int at 0x90 is why lightOrigin
    stays unplaced.
- **A second conflict, found by leave-one-out:** layoutdb put ArrowList.sortSet on `<Arrows>k__BackingField`'s 0x18. The compiled
  accessor is stronger.
  - `layoutdb.py` now takes accessors.json as known slots.
  - `layoutpredict.py` drops a slot-map placement on bytes a stronger source holds.
  - sortSet is 0x20, as the old lead had it.

**Numbers after the fixes:**
- leave-one-out: 1412 exact, 2 wrong, 453 not predicted;
- rawverify: 891 ok, 0 implausible;
- versus fields: **256 of 261 (98%)**, 0 disagreeing with confirmed pins.
- Still missing: PlayerCorpse.prismCoroutine, prismFall and Prism's three fields, so a prism arrow's state is partly gapped.

**To finish step D when testing resumes:**
```
XPLAY_RJSLOT=update XPLAY_VISIBLE=0 tools/smoke.sh lobby "statemeasure 1 0" "lobbymap <N>"
```
- A pilot on tower 0, then towers 1–5 or more.
- Per run: no guest exception, 0 differing ticks, and every "[state] field check" summary with 0 "not".

### C1–C3, D and E (2026-10-21, after the plan revision)

**C1, one command: done.** `tools/re/statedb.sh` rebuilds everything from the kept inputs, about 2.5 minutes:
- accessors, layoutdb, layoutpredict;
- `stateconflicts.py` (sources, byte overlaps, leave-one-out; rule 2);
- rawverify, `stateconfirm.py`, `staterisk.py`, statescore;
- the pins and the field check.

It reproduces the committed files exactly. Any conflict or difference fails it.

**C2, confirmation: 223 of 261 versus fields confirmed (85%), 30 placed only, 8 missing.**
- A field is confirmed by compiled code (pin, accessor) or by two agreeing independent sources: slot maps, real bytes with a
  non-zero value, the layout rule with that field left out, and the using methods' accesses with every other field's bytes
  excluded. 0 disagreements.
- Found and fixed on the way:
  - **The rule filled padding before a fixed field where the source puts the field after it.** ArrowCushion.rotation is at 0x30
    (the bytes hold angles), not 0x24. A run with two closing layouts is now ambiguous, so unplaced.
  - **Vector2 accessors (two loads) are now read.** PlayerCorpse.`<Squished>` at 0x118, which places PlayerCorpse's prism fields.
  - **Constructor stores dropped as a source.** They misaligned 3 times in about 45.
- **Not confirmed:** nearly all tower objects (MovingPlatform, ShiftBlock, Icicle, Spikeball...) that towers 0–5 don't have.
- **Missing:** ArrowCushion's 4 fields (two layouts fit), TeamReviver.Mode, Prism's 3.

**C3, the check before a run: done.** `staterisk.py`, risk per phase:
- GetState: 7 HIGH, all in Dark World classes that plain versus towers never create;
- LoadState: 54 HIGH (any unconfirmed field written), so LoadState waits.

The quest / trials / Dark World classes are in scope: TF.EX's lobby takes Trials, and its DARK PORTALS scenario brings portals and
enemies into versus.

**D, GetState on 6 towers: met for GetState.** Towers 0–5, `statemeasure 1 0` (GetState, serialize and deserialize every tick),
with the slot maps, raw dumps and state dumps in the same runs:

| tower | differing ticks | rollbacks | field checks: ok / wrong / null |
|---|---|---|---|
| 0 | 0 of 592 | 10 | 49 / 0 / 4 |
| 1 | 0 of 593 | 10 | 65 / 0 / 4 |
| 2 | 0 of 599 | 10 | 60 / 0 / 4 |
| 3 | 0 of 590 | 10 | 54 / 0 / 4 |
| 4 | 0 of 595 | 10 | 51 / 0 / 4 |
| 5 | 0 of 590 | 10 | 53 / 0 / 4 |

No guest exception. Timings in Ryujinx, ms p50/p90/max:
- GetState 0.16–0.20 / 0.21–0.27 / 0.48–1.21;
- serialize 0.03–0.04 / 0.05–0.07;
- deserialize 0.05–0.06 / 0.07–0.08;
- states 630–925 bytes after LZ4 (1.4–2.4 KB raw).

The rest of D's revised marker is open: every pinned field checked live at least once, and the extended spawn scene (tower objects,
a prism hit).

**What the runs found in the translation (fixed in the translator, 64767b7):**
- **TF.State's lazily made statics were made inside the rollback window,** so a restore undid them under the module's pointer. The
  glue's entity cache crashed tower 1 after its first rollback. Every unit now has MakeStatics(); EnsureStateClasses calls them
  before the match.
- **Making them early surfaced latent crashes:**
  - `typeof(CrumbleWall).GetMethod("FinishBreak")`: the Switch build has no working reflection;
  - `ConditionalWeakTable<entity, StrongBox<float>>` and `<entity, TF.EX class>`: not in the binary.
- **The emitter's fixes:**
  - `typeof(T).GetMethod("N")` resolves like AccessTools.Method;
  - `Delegate.CreateDelegate` over it becomes a method-group delegate, `AccessTools.MethodDelegate<D>` a thunk;
  - a ConditionalWeakTable keyed by an object is a value map, cleared at each match start (it's weak on the PC);
  - StrongBox<T> becomes gen::Box<T>; GetOrCreateValue makes `new V()`.
- **Inputs kept:** the six tower runs are in `tools/re/slotmaps.tgz` and `rawdump.tgz` (207 class files, 2570 records, 0 wrong).
  They lift the raw-byte check to 1016 fields, 0 implausible.

**E, the PC oracle: tooling done, the PC side open.**
- **`tools/tfex/statediff.py`:**
  - its own MessagePack and LZ4 decoders (`--self-test`);
  - the schema read from TF.State's models (`[Key]`, `[Union]`);
  - frames matched on GameState.Frame;
  - a field-by-field diff with names.
  - A Switch state (frame 120, 1711 bytes) decodes to 524 named fields.
- **The Switch side:** hw.txt `statedump <every>` writes `[state] dump frame <N> part <k>/<n> <base64>`.
- **The PC side needs no new TF.EX code.** TF.EX's replays (`.tow`) hold every keyframe's serialized state (`Record.State`), and
  statediff reads a `.tow` directly. Only the browser harness has to copy the page's newest replay out of its file system after a
  match (the driver, the browser workstream's code).

**A finding for the plan: TF.EX's own test harness.**
- `TF.EX.Domain/Scenarios/ScenarioCatalog.cs` holds about 80 deterministic scenarios. Each comes with a generated map
  (`ScenarioMapWriter`), scripted inputs (`InputScripter`), variants, the entity classes it covers, and an `Expect` check.
  Together they cover every tower object, variant, prism, icicle, Dark World portal and team-revive case still open here.
- `ScenarioSweeper` runs them in test mode, with a GGRS check distance (save, load, resimulate, compare), through TFCommands'
  `scenario` command.
- Running TF.EX's sweep on the Switch would replace the hand spawn scene (steps A/D) with TF.EX's own coverage. It would test
  LoadState the way TF.EX does, and on the PC the same scenarios give step E's frames.
- **Cost:** about 1,750 lines of TF.EX C# for the translator, plus test mode and loading a map from a written file on the Switch.
  Not started: a decision for the user.

### The scenario sweep: translated, then two spikes (2026-10-21, after the user's "step back")

**Translation (e2f87d7).** ScenarioCatalog, ScenarioSweeper, ScenarioMapWriter, InputScripter and TFCommands' launch path all
translate and build.
- About 15 general translator constructs were added for them (tuples up to 7, GroupBy, more host classes for delegates, ...).
- Two same-behaviour glue methods; a Regex subset and in-memory map files for the map writer.
- Nothing calls the sweep yet. Running it as TF.EX does needs a local test mode (a GGRS sync test with every seat local), which is
  frame-loop work in Integration B's files: not started.

**Spike (a): a TF.EX scenario's map in a lobby match. Yes (29ad6c4).**
- hw.txt `statescenario <name>`: TF.EX's own ScenarioMapWriter.Write makes the map in memory, and the match plays only that map.
- `icicle`: generated, loaded and played, icicles alive, no crash.
- The browser peer plays its own tower, so the digest desyncs (as with the spawn scene). Fine for coverage dumps; scripted inputs
  apply only on the local seat.

**Spike (b): TF.State LoadState round trips inside a lobby match. Runs, not yet faithful (ba77bbf).**
- `statemeasure 1 60`, tower 0: LoadState every 60th tick of the state just captured, then GetState again.
- 6 round trips, every one identical (644 bytes, LoadState 0.13–0.40 ms), no crash.
- Two real bugs found on the way, both fixed:
  - the models' containers leaked a runtime arena;
  - arrays of TF.State structs weren't serialized, which crashed Chain.Update after a load.
- **But after the first load the game stops advancing:** the local digest stays constant from frame 120, the browser moves on, 8
  desyncs. In a GetState-only run, players move from about frame 300.
- So something outside TF.State's bytes, or a side effect of LoadState, freezes the round (the round start is the first suspect).
  That's the next thing to trace, with statediff on the states around a load and the game's round fields.

### The LoadState freeze: found and fixed (2026-10-21)

**Cause.** TF.State's state isn't only what its extensions read from the game; its **Harmony patches** keep part of it.
- The round intro's progress is a count kept by `CoroutinePatch` (TF.State.Patchs/Component/Coroutine.cs): +1 per step of the
  VersusStart's SetupSequence coroutine while capturing. `LayerPatch` resets it when the intro leaves.
- LoadState deletes the intro entity and recreates it, replayed to that count, only when the count is above 0.
- Those patches weren't on the Switch, so the count stayed 0 (confirmed in the dumped states: `Entities.Hud.VersusStart.CoroutineState`
  is 0 throughout the intro). Every load dropped the intro, and the round never started.

**Fix.** TF.State's CoroutinePatch and LayerPatch translated as generated hooks (units State_Coroutine, State_Layer; on with hw.txt
`stategenpatch CoroutinePatch LayerPatch`, a key that takes several classes because smoke.sh keeps one line per first word). Two
translator constructs on the way:
- `dyn.TryGet("NAME", out v)` of TF.EX's own state goes to the side table;
- `x.GetType().Name` reads the runtime type record's name. MemberInfo.Name is abstract, its base body throws
  NotImplementedException, and its vtable slot doesn't bind.

**Result** (tower 0, `statemeasure 1 60`): the round now starts and runs after loads; players move and the digest advances.

**What the round trips show now.** `statedump` writes the before and after states of a differing round trip (`dump` / `dump2`), and
`statediff.py --tags dump,dump2` names the fields:
- `ScreenShake.CounterValue` is 5.0 (or about 0) before the load and 20.0 after: the level's shake counter is loaded wrongly.
- `Players[1].Animations.Body[1]` is 1 before, 2 after: a player's animation isn't restored exactly.
- `DefaultArrow` key 21 is −150 before, 0 after: an arrow field isn't restored. Then Arrow.Update → Collide.Check with a null
  reference crashed the run (about frame 450).

**The bigger lesson.** TF.State.Patchs has 66 files; about 15 are translated units today. Their side state (HUD counts, miasma and
moonglass controllers, the session, round-logic rules such as "no VersusStart on a rollback frame") is part of TF.State. A faithful
LoadState needs them installed with the extensions, not only GetState / LoadState. The next steps: fix these three fields with
statediff as the check, then bring in the remaining TF.State patches the same way.

### Round trips identical, towers 0–2 (2026-10-21, commits 0ef096a, 93456ba)

The three fields found by statediff are fixed: ScreenShake (LevelScreenShakePatch, and IsRestoring set by LoadGameStateBytes),
the arrow (ArrowPatch, and ToDictionary into a game Dictionary for the actualDepth lookup), and the animation frame
(`Sprite<T>.currentFrame` 0xb0 and `timer` 0xd0, pinned). With them pinned, two crashes showed, both from DynamicData resolved
on the wrong type. The translator now does what DynamicData does at run time:
- **A name the static type lacks, on a subclass:** the subclass's field, tested at run time; an object of none of them uses the
  side table. Before this fix, 12 reads went silently to the side table: Pickup.sprite / image (ShieldPickup, BombPickup …; a null
  sprite crashed tower 2), RoundLogic.done / roundEndCounter / wasFinalKill, Hat.image / spin, LevelSystem.lastLevel / levels,
  Enemy.slimeColor.
- **A generic method's T** (TF.State's `Level.Spawn<T>`): the instantiated type. Before this fix, `Scene`, `Level` and `actualDepth`
  of every entity a load respawns went to the side table; a respawned Orb without a Level crashed tower 1.

Result, lobby matches with hw.txt `statemeasure 1 60` (a TF.State LoadState round trip every 60 frames) and `stategenpatch
CoroutinePatch LayerPatch LevelScreenShakePatch ArrowPatch`:

| tower | ticks compared with the browser | differing | round trips identical |
|---|---|---|---|
| 0 | 594 | 6 | 3 of 3 |
| 1 | 594 | 0 | 3 of 3 |
| 2 | 298 (the Switch ran to its vsstop at 600; the compare's overlap with the page's tick log was 298) | 0 | 3 of 3 |

**Tower 0's differing ticks are most likely the tick log, not TF.State.** Three runs with loads: 6 of 594, 5 of 595, 0 of 596;
the control without loads: 0 of 592.
- In the failing runs the GGRS digest was clean (smoke.sh checks it before the tick compare and stops at the first FAIL).
- The differing lines are 5 in a row. On them the remote archer p1 is one input behind (it jumps a tick later), and from the 6th
  line on it matches the browser exactly. A real one-tick-late jump would leave a lasting offset, so these lines show a predicted
  simulation that a rollback then corrected.
- A raw probe (TEST ONLY, since removed) compared both Player objects' first 0x600 bytes before and after each LoadState: no word
  changed, and no player was recreated.
- The tick log's commit points write lines up to `confirmed + 1`: `LogCommit(confPrev + 1 …)` in netplay_gate.cpp, and at vsstop
  `NpConfirmed() + 1` in xplay.cpp. The loads add per-tick work (GetState every tick, LoadState about 1 ms), which means more
  rollbacks and more chances for a line to be written before its frame is final.

This is for Integration B, the owner of those files: check whether a committed line can still be resimulated. TF.State's
round trips stay identical on all three towers.

**Answer (Integration B2, 2026-10-21):** yes, at vsstop. RbGate commits a line only once its frame was confirmed before the previous
advance (that advance corrected it). The vsstop commit in xplay.cpp used the current confirmed frame + 1: frames confirmed in this
tick's poll, which only the next advance corrects, and the run stops before it. So the last few lines (about one prediction depth)
could be predicted ones. The vsstop commit is removed; the tick log ends at the last final line. Check: smoke lobby "statemeasure 1 60"
"lobbymap 0": 0 differing ticks of 587, digest clean (runs/152305 of /tmp/xplay-runs-87c3cfc3-b2).

### TF.State's patches, batch 1: on by default, the hand ports deleted (2026-10-21)

**What.** Every patch class of TF.State.Patchs that has no rnd.cpp hand port now runs as TF.EX's own code: 59 classes installed at
boot (tfexgen_glue `kStatePatches`; the 13 of 2026-10-19, the four that were TEST ONLY (CoroutinePatch, LayerPatch,
LevelScreenShakePatch, ArrowPatch) and 42 more). The translator needed eight new constructs for them (Calc.Choose<T> with Monocle's body,
Harmony Traverse `.Property(n)`, `Any()` of a game Stack, IEnumerable<int / Vector2> parameters as value lists, primary-constructor
structs, an absent FortRise collection in a foreach, a value `return` inside a foreach visitor, a `break` nested in a switch section,
`new List<T>(source)` of a C++ container, double.GetHashCode as .NET's, a Harmony FieldRef invoked as the field itself).

**The hand ports deleted** (each read against the generated code first: they were a working, in-sync version of the same patches):
- tfstate_ports.cpp: Miasma (and miasma_seq.hpp, the MiasmaSequenceController port), Icicle, MoonGlassBlock, PlayerCorpse,
  BrambleArrow, SensorBlock; what is left is the Switch's own Cataclysm GenerateData / AddItems.
- xplay.cpp: CoroutineUpdateHook (CoroutinePatch's Miasma / MoonGlassBlock / bramble parts), ArrowRemovedHook (ArrowPatch.Arrow_Removed),
  the per-level Miasma reset (SessionStatePatch.Session_LevelLoadStart), their SideAll copies; the Level.Update postfix now calls TF.State's
  StateApi.StepOwnedControllers (MoonGlassBlock shatter, DarkPortals sequence) where it called the hand MoonGlassStepAll.
- rnd.cpp: DropNextArrowHook (PlayerCorpsePatch's DropNextArrow prefix / postfix: the same RegisterRng / UnregisterRng on rnd.cpp's counters).

**Found on the way (static, before any run): why TF.State's patches on the Switch could desync, and the fixes.**
1. **TF.EX's calls into TF.State went nowhere.** overrides.toml had `ITfStateApi.* = noop` ("the Switch's rollback engine snapshots
   memory instead"), written before TF.State's patches ran on the Switch. So TF.EX's own ResetSession (NetplayManager.Reset),
   ResetMatch, SetSessionRoundStarted (SessionPatch.Session_StartRound), StepOwnedControllers (the Level.Update postfix) and PurgeCache
   did nothing. They now reach TF.State's TfStateApi (generated State_Api); the snapshot / byte calls stay no-ops.
2. **TF.State's classes were made only under statemeasure.** statert::BeforeMatch returned at once without it, so in a normal match
   TF.State's statics were made lazily inside the rollback window (the crash class MakeStatics fixed for the measurement) and its
   context was never reset between matches. BeforeMatch now runs before every lobby match.
3. **The DynamicData side table was built for UI state**: a 256-entry ring, oldest replaced, linear search. TF.State's patches keep
   per-entity gameplay state there (every coroutine's NAME, chests' and bats' counters), so a long match could silently evict live
   state. Now an open-addressing table of 16384 entries in one uncollectable GC block (a rollback restores it, its count included),
   emptied before each lobby match, its use logged, and full is a clear error (PoolFull), never an eviction.
4. `StateFlags.IsRollbackFrame` was a plain field nobody set (TF.EX sets it through SetDriverFlags): now the Switch's resimulation flag,
   as ExFlags.IsRollbackFrame already was (LastManStandingRoundLogicPatch and MatchVariantsPatch read it).

5. rnd.cpp's whitelist checked the first instruction of PlayerCorpse.DropNextArrow, which PlayerCorpsePatch now hooks: one failed
   check turns every gameplay-RNG hook off (the 2026-10-19 mechanism). rnd.cpp no longer checks or hooks it.

These are candidates for the desync from frame 120 that the 2026-10-19 switch-over left open (docs/ws-tfex-update.md "genstate").

**Found in the runs** (three crashes, each a translator or binding bug, fixed at the source):
- TweenPatch read Tween.cached through `AccessTools.StaticFieldRefAccess`, a reflection-made delegate the Switch doesn't have: a Harmony
  FieldRef invoked is now the field itself.
- AlarmPatch / BramblesPatch read the static pools `cached` through Traverse / DynamicData, which the translator read as instance
  fields (at the object + the static's address). Traverse and DynamicData now reach a static field at its storage.
- A hook argument of a game array type was declared as a C++ gen::Arr by value: the game passes one register, so TreasureChest's
  constructor got its later arguments shifted and the game threw. Hook arguments of array types are the managed array now.
- Pins proven from the Switch's code: Tween / Alarm / Brambles `cached` (each Removed's push through its static slot), and ten private
  fields (PlayerIndicator x5, PlayerCorpse.ghostCoroutine, SensorBlock.detector, TeamReviver.targetLightAlpha, TreasureChest.preview,
  LightningFlashLayer.alarm), each where two sources agree (the layout rule, the methods' accesses, the constructor's stores).

**tools/tfex/prerun-check.py**: these classes of bug, checked statically before an emulator run (unresolved members in default-on
patch units, statics read as instance fields, reflection left in, hooks on a hand hook's address or on rnd.cpp's whitelist,
by-value C++ containers as hook arguments, trampoline headroom), with the reviewed exceptions and their reasons. Run it before every run.

**Result** (lobby matches, hidden, nothing in hw.txt beyond the tower): towers 0, 1, 2 in sync (0 differing ticks of 596, 594, 594),
digest clean, 59 of 59 patch classes hooked, rnd.cpp's whitelist on, trampolines 262 of 327, no pool full.

**Hardware checklist line:** the boot log has `[tfexgen] TF.State: 59 of 59 patch classes hooked` and `[rnd] whitelist hooks installed`;
every lobby match logs `[state] TF.State's classes made before the match` at its start and, in `[pools] lobby match end`, the
DynamicData side table's use (no FULL); no `POOL FULL`; the match in sync with the other console / page.

**Not in batch 1:**
- batch 2: the patches rnd.cpp hand-ports on its gameplay stream (CalcPushRandom, TreasureSpawner, OrbLogic, Orb, Spikeball, OrbPickup,
  UnlockData, LevelLoaderXMLState, RoundLogic, SessionState's StartGame, LevelTestRoundLogic, LocalSettingsGuard, VersusLevelSystem).
  The Switch's stream never swaps the Calc.Random object (rnd.cpp counts and marks sentinel Randoms), so each needs a [[replace]] at
  the point where TF.EX swaps a Random.
- TFEX-DIFF, kept: SFXPatch / SFXInstancedPatch / MonoclePatch (sound under rollback is sfx.cpp's), DeterministicMathPatch (an IL
  transpiler; dm.cpp routes the same Math calls), CalcPatch itself (its members are rules on rnd.cpp's counters).
- Not applicable: InstantReplayViewer (no kill-cam), ModIO (FortRise), OptionsScroll (main menu), ChaliceGhostPatch (no ChaliceGhost
  type on 1.4.1.6), FadeLayerPatch (commented out in TF.EX).

### Batch 2, part 1: treasure and pickups (2026-10-21)

**On by default** (TF.EX's own code): TreasureSpawnerPatch, OrbPickupPatch, UnlockDataPatch, LevelLoaderXMLStatePatch, with
LocalSettingsGuard. **Deleted from rnd.cpp:** ChestSpawnsHook, ArrowTypeHook (its extra draw is TF.EX's own `Choose` of the
one-item list), ArrowShuffleHook, SpawnerCtorHook, OrbPickupCollideHook, UnlockGuard, the four UnlockData getter hooks,
LevelLoaderCtorHook (its draw-only screen-shake zeroing goes with it), BindSpawner, and their whitelist checks.

**The Switch underneath:**
- `IRngService.Gameplay` is one System.Random made in BeforeMatch (outside the rollback window, rooted), which rnd.cpp draws from
  the gameplay stream: TF.State's UseDeterministRandom sets the spawner's Random to it, as on the PC.
- `ModesExtensions.IsNetplay` (the PC's Mode 11) is the Switch's online mode (pcgameplay.cpp).
- FortRise's `orig_GetRandomArrowType` (the vanilla body) is the original under the Switch's hook: HookGen exports every hooked
  method's original as `Orig_<Type>_<Method>`, and a `[fortrise]` entry with `orig = "<Method>"` calls it.
- Pins proven from the code: the round logics' `done` (0x70) and `roundEndCounter` (0x68), used by LevelLoaderXMLStatePatch's Reset.

**Found and fixed** (both runtime infrastructure):
- Delegate host classes were made at the first delegate, inside the rollback window. A chest opened, rolled back and re-opened
  re-made the Pickup tween's delegate through a class record the restore had taken back, and the game crashed. The translator now
  makes every unit's host classes in MakeStatics, before the match.
- The runtime's "made inside the rollback window" warning was suppressed for runtime-made classes whenever the arena existed.
  Classes now always warn, with the call site, at frame > 0 (frame 0 is before GGRS's first save). A full run shows 0.
  tools/tfex/prerun-check.py checks for this statically too (item 7).

**Checked** (hidden lobby matches, tower 3 with the page's rngdiag trace and the Switch's rnggametrace):
- towers 0, 1, 2: in sync;
- tower 3 with BOMB CHESTS: in sync (0 of 597), 4 chests opened.

**ARROW SHUFFLE: found and fixed** (by the page's own RNG folds, not by guessing). The page prints the gameplay stream's fold
(s0^s1^s2^s3) at Session.StartGame / LevelLoadStart / OnLevelLoadFinish; stepping TF.State's xoshiro256** from the lobby seed
(RandomLevelSeed) counts the draws behind each: the PC made 9 at StartGame, then reseeded, and was 18 draws in at the first tick;
the Switch 8, never reseeded. Two differences, both TF.EX / FortRise behaviour the Switch lacked:
- FortRise's GetArrowShufflePickups (the PC's) shuffles the vanilla result once more after adding modded arrows (none): one more
  draw inside TF.State's registration. On the Switch: a `[fortrise]` wrapper rule (`after = ...`, HookGen runs it right after the
  original, before the patches' postfixes) with the game's own List<Pickups>.Shuffle(Random) on Calc.Random.
- TF.EX's MapScenePatch.StartSession_Postfix calls StateApi.ResetRng (and the menus SetSeed): on the Switch `ITfStateApi.*` was
  still a no-op. ResetRng, ResetRngOverride and SetSeed now reach rnd.cpp's stream as TF.State's TfStateApi does.
Result: tower 3 with ARROW SHUFFLE|BOMB CHESTS in sync (0 of 596); tower 0 in sync (0 of 595).

**Every reached no-op reviewed** (tools/tfex/noop-reviewed.toml, enforced by prerun-check.py item 8): 37 reviewed with a reason
each, 2 open for part 2 (ITfStateApi.GenerateVersusLevels / SetVersusLevels: TF.EX builds the level order through TF.State; the
Switch's is still rnd.cpp's GenLevels hand port).

**Also seen:** with `lobbyvariants ARROW SHUFFLE|ALWAYS BIG TREASURE|BOMB CHESTS`, the page applied only BOMB CHESTS and ARROW
SHUFFLE (90 variants against the Switch's 93). A lobby-variant propagation gap, reported to the coordinator.
(Settled later: vanilla's MatchVariants links make ALWAYS BIG TREASURE and BOMB CHESTS mutually exclusive, so both sides apply
the same 2. Not a bug.)

### Batch 2, part 2: level generation and spawns (2026-10-21)

**On by default, the hand ports deleted:** VersusLevelSystemPatch (GenLevels through TF.State's OwnGenLevel), RoundLogicPatch
(SpawnPlayersFFA / SpawnPlayersTeams with TF.State's own shuffles), LevelTestRoundLogicPatch, and SessionState's StartGame (the
reseed and RandomLevelSeed). ITfStateApi.GenerateVersusLevels / SetVersusLevels / SetSessionRoundStarted / StepOwnedControllers /
PurgeCache are routed to TF.State's TfStateApi, so the noop-reviewed `[open]` list is empty. Deleted from rnd.cpp:
- the sentinels;
- the Sent/Seed scopes;
- the StartGame, GenLevels, SpawnFFA and SpawnTeams hooks and their whitelist checks;
- teamspawn.hpp and miasma_seq.hpp.

**Translator rules added:**
- TF.State's `IEnumerable<int / float / string / Vector2>` parameters and results are game `List<T>`s (C#'s callers pass Lists
  there). A non-List argument, such as a LINQ chain or Enumerable.Range, is copied into a new List at the call, where C# enumerates
  it once too.
- `OrderBy(k1).ThenBy(k2[, StringComparer.Ordinal / OrdinalIgnoreCase])` becomes one stable sort.
- TF.State's own LoggerExtensions go to the Switch's log.

**Switch-only (S-native):** OwnGenLevel's `level.Replace('/', '\\')` is dropped. The game loads levels from the console's file
system, whose separator is '/'. The sort runs before it, so the order is unchanged. Before this rule, the first level load threw.

**Infrastructure workaround (rnd.cpp, flagged):** TF.EX's MainMenu calls StateApi.SetSeed(lobby.GameData.Seed) when a lobby is
created or joined. The Switch's lobby flow (tfex/MainMenuPatch.cpp, Integration A's stand-in) doesn't yet. Until it does,
rnd::SetLobbySeed sets the seed at the lobby match's start and logs `[rnd] ... TF.EX's SetSeed not reached by the lobby flow, set
here`. Once that line is gone, the lobby flow calls TF.EX's own SetSeed.

**Reached gap, reviewed:** StateContext's `_gameplayRandom = new DeterministicRandom(0)` stays null. Only TF.State's GetState /
LoadState (the measurement) read it; gameplay draws through the routed IRngService.Gameplay rule.

**Checked:** tower 0 pilot with rngdiag: in sync (0 of 595), 0 objects made inside the rollback window, "Generated 10 levels".
Then tower 1 (0 of 597), tower 2 (0 of 594) and the `team` scenario (0 of 598, TF.State's SpawnPlayersTeams ran): all in sync, 0
in-window warnings.

### Batch 2, part 3: orbs, spikeballs, PushRandom, arrows' Drop / EnterFallMode (2026-10-21)

**On by default, the hand ports deleted:** OrbLogicPatch (Update, DoDarkOrb, DoTimeOrb and StartScroll registrations; TF.State's own
DoSpaceOrb and DoOffsetWorldVariant), OrbPatch (ctor registration and ownerIndex = -1, CosmeticFreeze in Update), SpikeballPatch (ctor
spinDir draw, TF.State's Update), CalcPushRandomPatch, and ArrowPatch's Drop / EnterFallMode. Deleted from rnd.cpp:
- the whitelist hooks and their first-instruction checks (ArrowDrop, OrbUpdate, DoSpaceOrb with its wide-width swap, DoDarkOrb,
  DoTimeOrb, OffsetWorld, StartScroll);
- the EnterFallMode vtable pool and Arrow.Create discovery;
- the Orb and Spikeball ctor hooks;
- PushRandom0Hook.

The PushRandom() check had to go with its hook: it read the first instruction, which the generated hook replaces, and one failed
check turned every Random hook off. EnterFallMode is hooked on Arrow's base body only, as Harmony does on the PC. The hand pool found
the same single body (+0x988ea0, DefaultArrow). DoSpaceOrb's 320 width is TF.State's literal, so the wide-mode swap is gone too.
Pins: Orb.sprite 0xf0, Spikeball.exploding 0x110, Spikeball.spinDir 0x118, each proven by the constructor's own stores.

**Checked:**
- tower 0 pilot: in sync (0 of 598), 0 in-window warnings; OrbLogic.Update ran through TF.State's registration;
- the `rematch` scenario: match 1 in sync for all 2918 frames (no DESYNC), with TF.State's EnterFallMode drawing (46 draws).

**Not exercised by any scenario:** orb pickups (DoSpaceOrb / DoDarkOrb / DoTimeOrb), spikeballs, Arrow.Drop (a death with arrows in
the quiver), PushRandom(). For the console checklist.

**Found, not part 3 (handed on):**
- `rematch` never reaches "match results finished" after match 1 ends (harness: "no netplay sync 60 s after joining").
- `rematch` with `lobbyvariants BOMB CHESTS` desyncs at frame 720 with a constant RNG fold (state outside the RNG). Batch 2 part 1
  (944c503), part 2 (b3630a3) and part 3 give the same checksums, so it predates batch 2.

### How the work is checked, after part 2's two crashes (2026-10-21)

- prerun-check.py item 10: no translator gap (TFEXGEN_STATE_GAP) in default-on code outside TF.State's GetState / LoadState unless
  reviewed with a reason. Part 2's first crash was such a gap: an unsupported ThenBy left GenLevels' level list empty. 5 older gaps
  were reviewed and listed.
- tools/tfex/crash-stack.py <ryujinx.log>: a crash's stack with our frames through addr2line and the game's named by the nearest
  bound method, plus the last [hw] lines. This replaces five or six manual steps per crash.
- A/B by building the previous commit in a temporary worktree and running the same scenario in the same slot: it settles "did this
  batch cause it" in one run per commit.

### The lobby seed through TF.EX's own SetSeed (2026-10-21)

The `ITfStateApi.GetSeed` rule was a stand-in returning the own lobby's GameData.Seed. TF.EX's MatchmakingService.HandleLobbyUpdate
re-seeds when `lobby.GameData.Seed != StateApi.GetSeed()`, so it compared the lobby's seed with itself and never fired once the
seed changed after creation. GetSeed now reads TF.State's stream seed (rnd.cpp's, as TfStateApi.GetSeed = RngService.GetSeed), and
svc::GetStateSeed is deleted. rnd::SetLobbySeed's bridge (part 2) is gone; it is a check again. The `lobby` (0 of 591) and `join`
(0 of 598) scenarios are in sync, and both log `[rnd] the lobby match's seed N = TF.State's stream seed`.

### Virtual method groups bound as ldvirtftn (2026-10-21, Integration B2's find)

A delegate from a virtual game method (`tween.OnComplete = self.FinishUnpack`) was bound to the declaring type's body, so a
BombPickup's FinishUnpack override (the 40-frame tween ending in Explode) never ran: bomb chests' bombs never went off on the
Switch, and `rematch "lobbyvariants BOMB CHESTS"` desynced at frame 720. The translator now takes the receiver's override from its
vtable slot (gen::VirtFn, as C#'s ldvirtftn; `base.M` stays the declared body). That changes two sites: PickupPatch.ProperlySetTween
and PickupExtensions' LoadState. The same run now has no desync; its end-of-match step still fails, which is Integration B's (reported).

## The rollback seam and what the switch-over deletes (2026-10-22, Integration B2)

### The seam
ggrs_ffi's save and load requests reach the game through one pair in `hw-probe/exlaunch/source/program/netplay_gate.cpp`:

- `SaveForGgrs(&bytes, &len)` (line 143): the bytes ggrs_ffi keeps for the state frame. It also hands GGRS the frame's checksum
  (`netplay_set_pending_checksum`: the digest, npdigest.cpp).
- `LoadForGgrs(bytes, len)` (line 153): makes those bytes the game's state and returns the frame the game is now at the start of
  (-1: can't load).
- They are called from RbGate's request loop (lines 262 and 268). That loop keeps TF.EX's own steps: SetIsRollbackFrame and
  `g_rbFrame` (TF.EX's IsRollbackFrame, set after the load and cleared at the end of the next Level.Update), and
  VersusMatchResultsInputGate.NotifyRollback (`g_resultsFrame`).

Today the memory snapshot is the only implementation. A save is a 16-byte handle naming the undo log's frame. A load rolls the undo
log back to the nearest frame it can reach (`rb::SafeTarget`), then resimulates the catch-up ticks up to the handle's frame.

TF.State's GetState / LoadState replaces both bodies in one batch, on by default. A TF.State load lands exactly on the frame, so the
catch-up ticks go with the snapshot.

### Deleted with the snapshot (it exists only for the memory restore)
| piece | where | why it exists |
|---|---|---|
| the undo log: page table, BeginTick / EndTick, SafeTarget / PendingScan, Rebaseline, demotion, worker threads | rollback.cpp (all); rollback.hpp | the memory snapshot |
| per-tick undo-log calls | netplay_gate.cpp RbResimTick (61), the first-time tick at the end of RbGate, NetplayGate's `g_rbPending` EndTick; xplay.cpp 628 (EndTick), 632 and lobbymatch.cpp 191 (SetTracking), lobbymatch.cpp 44 (SetPreHot), 72 (rb::End) | the same |
| Boehm allocation hooks and the allocation records (`kMaxAlloc` 8192, the pool the console ran out of on 2026-10-21) | rollback.cpp 116, InstallHooks 187; xplay.cpp 1524 | pages first written inside the window |
| write tracking: page protection, the user exception handler, the SDK early-copy hook (sdk+0x32fa68), the nn::fs::ReadFile hooks | wtrack.cpp (all), wtrack.hpp; rollback.cpp's wt:: calls | which pages a tick wrote |
| hw.txt `rb`, `rbcold`, `rbdemote`, `rbprehot`, `rbthreads`, `rbwrite` | hwconfig.cpp 31-37 | the undo log's settings |
| the input ring for the catch-up ticks (`g_rIn`, `g_rInFrame`) | netplay_gate.cpp 8-9 | catch-up resimulation |
| KeepSave / KeepRestore: NmKeep (TF.EX's NetplayManager fields, which sit in the restored heap), rumble's controller object, KeepObject's native objects (graphics buffer slots, the GraphicsDevice and its native block, the GPU pool globals, the native platform globals, the SDK allocator) | xplay.cpp 818, 846 (GfxRingHook), 865 (PresentHook), 1308-1360; rumble.cpp 211-230 | the restore would overwrite them |
| the graphics graveyard: deferred NVN destroys, ReviveOnUse, graphics records (512) | xplay.cpp 1084-1210 (GraveInstall, GraveFlush); netplay_gate.cpp RbGate's GraveFlush(rb::Floor) | a restore could bring back a disposed buffer |
| RepairSoundLists | xplay.cpp 913; LoadForGgrs | a restore leaves a List's count over a nulled slot |
| GC off during the match, RbSafeCollect, the frame hold for a collection | netplay_gate.cpp 80-101, RbGate's `g_gcWanted`; NpStart's GC switch | no collection may happen inside the window |
| the pre-match runtime arena and the `force` record logic | tfexgen_code_rt.cpp 108-150 (ReserveRuntimeArena, RuntimeAlloc); lobbymatch.cpp 301 | a restore frees containers made inside the window. The hand runtime is shared: done together with the translator |
| `[rb]` statistics: page counts, write-tracking timing, `rb::Stats` at match end | xplay.cpp 582-593, 1460 | the undo log's numbers. TF.State's save/load timings replace them |

### Stays, used by the new path
| piece | where | note |
|---|---|---|
| the side state per frame: seat players and ticks, `vsOrigin`, `lvTick` (tick log, our own bookkeeping) | SideAll, xplay_internal.hpp 88; SideSave / SideLoad, xplay.cpp 1044 | today it rides on the undo log (`rb::SideOf`). The new path keeps a small ring keyed by frame, saved in SaveForGgrs and loaded in LoadForGgrs. Its `rnd::SideState` part (the RNG streams) goes once TF.State's state carries the gameplay RNG (check that it does) |
| `sfx::OnLoad(frame)`, `rumble::OnLoad(frame)` | sfx.cpp 252, rumble.cpp 139 | TF.EX's Level.LoadState sound snapshot: called from LoadForGgrs with the loaded frame |
| sfx.cpp's "the window is open" test, `rb::Ready()` | sfx.cpp 441 | needs a match-running flag instead |
| the digest as GGRS's checksum | SaveForGgrs, npdigest.cpp | switches to TF.State's checksum only together with the page |
| [desync] logging (DsRollback, `g_rSt`), tick-log commits (LogCommit), the rollback cost line | netplay_gate.cpp | they key on frames, not on the undo log. `g_rSt` loses `rb::kWindow` and needs its own size |
| net.cpp's crash logger | net.cpp 1789-1805 | uses `wt::EsrOffset` / `wt::SetFallback`. It needs its own user exception handler once wtrack.cpp is gone (the ESR offset 0x334 is known) |

### The switch-over: TF.State is the rollback (2026-10-22, ws/tfex-update up to 4875715)

**The seam** (B2's SaveForGgrs / LoadForGgrs in netplay_gate.cpp). The save is TfStateApi.CaptureGameState, labelled TF.EX's way:
SetCurrentFrame(netplay_current_frame()), which is this loop's state frame + 1. The load is RestoreGameStateBytes; it lands on the
frame, and the label minus 1 gives the state frame back. The loop's bookkeeping (seats, vsOrigin, lvTick) is a ring keyed by frame.
The gameplay RNG comes from TF.State's state. GC runs during matches, as on the PC. B2's branch deletes the snapshot path
(rollback.cpp, wtrack, NmKeep, the graveyard, GC-off) on top of this. The runtime arena and the window warnings are deleted here.

**Fields:** versus 302 of 303 confirmed. 54 were confirmed statically from the compiled code by three research agents. The last,
CustomLevelSystem, is absent on the Switch. ChaliceGhost is ChaliceGhostX, an older class without `lerp`: a named version gap.

**Faults found by running it, all fixed:**
- **Player state:** the scheduler's action names were placeholders (ToNames), so ToActions threw on a load; the names now come from
  the delegates' bodies.
- **Round state:**
  - the round-end and ghost-wait counters were captured as 0;
  - RoundLogic.SpotlightDephts (a VList) was added to as if it were a game collection.
- **Game caches:**
  - DropLiveFromEntityPool keyed Monocle.Cache with a raw type handle. The System.Type object is read from record+0x160, because
    making it from our code traps.
  - Explosion and EnemyAttack are rebuilt through the game's Cache.Create<T> instantiations.
- **Containers:**
  - the per-frame SFX / MatchStats snapshot maps were 64 entries for a 960-frame history;
  - the deserializer and TF.State's LINQ built their lists from the runtime's 8-slot ring, so a load swapped players' arrows. TF.State
    and its glue now use GC memory.
- **Remaining translator gaps on the state path:** DynData<T>, conditional DynamicData, property patterns, Enumerable.Empty,
  Reverse, Stack/Queue sources, constant-name foreach, closed static delegates, model IEnumerable parameters.

**Checks:**
- lobby, towers 0-5, and team: in sync;
- rematch: match results at 2917 (2918 on the snapshot path), match 2 synchronizes.
- **PC parity:** the page's ?statedump against hw.txt `statedump N`, compared with tools/tfex/statediff.py.
  - Fixed: the frame label, the per-seat arrays (6 seats on the Switch, 4 on the PC: S-native) and the level paths (TF.State's '\\',
    read as '/' in pcgameplay's PcPath: S-native).
  - Lobby: in sync with GGRS's own fletcher16 checksum (SMOKE_DIGEST=0, ?switchdigest=0), states byte-identical to the page's.
  - **Open:** in rematch, checksums differ from frame 1200, only in Players' Animations.Head (look up/down vs idle). Most likely the
    input-to-aim conversion for received inputs; routed to Integration B. Evidence: runs/111445.

**Tools added:**
- prerun-check item 10 (called gaps can't be excused per method);
- gen::Throw logs its site; POOL FULL logs its caller;
- tools/tfex/crash-stack.py;
- TFEX_TRACE_UNITS covers generic copies.

### The deletion half (2026-10-22, Integration B2, on top of ws/tfex-update 21464ad)
The seam's bodies are TF.State's only; `stateroll` is gone with the snapshot path. Deleted, as listed above:
- rollback.cpp, rollback.hpp, wtrack.cpp, wtrack.hpp;
- the per-tick undo-log calls and the catch-up input ring's restore role (`g_rIn` stays for the [desync] log, sized `kRbWindow`);
- KeepSave / KeepRestore with NmKeep, rumble's keep and KeepObject;
- the graphics graveyard and its hooks: Finalize wrap, pool and native free, Retire, Dispose (also the inline ones), TextureDispose, GfxRing, Present;
- RepairSoundLists;
- GC off during the match, RbSafeCollect and the GC tuning;
- the call to the pre-match runtime arena (its code goes on the translator's side);
- the `[rb]` undo-log statistics;
- the hw.txt keys `rb`, `rbcold`, `rbdemote`, `rbprehot`, `rbthreads`, `rbwrite`, `rbgcmb`, `stateroll`.

What changed instead:
- SideAll keeps only the frame loop's bookkeeping, with no RNG.
- The ring is reset at each match's first save, which also sets the state frame.
- sfx's music window follows the match session (`xplay::NpWindowOpen`).
- The crash logger uses the known ESR offset 0x334.

The hang at frame ~380-385 (after `[state] gap: ActionExtensions.ToNames`) was TF.State's ToNames placeholders, fixed on the translator's
side (104a8ab). The rematch crash in AddRoundLogicState (a value list added to as a game collection) and the swapped arrows on a load
(lists from the 8-slot ring) were fixed in e93fd0a.

**The scenario set** on this batch (35cc677; hidden, slot b2, the digest as GGRS's checksum): 13 of 16 PASS. The tick compare showed
0 differing ticks wherever it applies.
- lobby, join, private, privatejoin, quickplay, rematch, archer, team, report, series, options, quit, three: PASS.
- disconnect: only smoke.sh's new "a stalled match FAILs" rule. Its local run-on always ended on the harness's stall stop: now `stallok`.
- pause: fails identically on master b3fe4f6 (the overlay closes 2 frames after it opens; the menu buttons show Back held). It predates
  this batch.
- spectate: the page drops spectators for Switch lobbies ("no spectators"). Page side, predates this batch.

**Open: PC parity** (GGRS's own fletcher16 of TF.State's bytes, `SMOKE_DIGEST=0`, which also passes the page's `?switchdigest=0`):
- Without RefreshSprites (the translator's branch): lobby is in sync. Rematch mismatches from frame ~1200, only in
  Players[].Animations.Head (look up / down / back vs idle, and the head scale).
- The cause: Player.UpdateHead runs only in DoWrapRender, at draw time. A save carries the last draw's head, which depends on where
  each side's rollbacks fell; it's always the predicted seat that differs.
- TF.EX's Level_Update__Postfix runs PlayerPatch.RefreshSprites (every player's DoWrapRender) while capture is on. That needs the
  translator's Matrix.Lerp.
- With RefreshSprites (Integration B2's branch claude/ws-match2-f9e559, 16380f7): the rematch parity run had 81 mismatches from frame 120.
  Not diagnosed; the page dumped no states. Suspects:
  - RefreshSprites' extra DoWrapRender per update: wings.PlayerUpdate, Hair, the head's Play;
  - a Flash.FlashInterval difference.
- Next: one run with both sides' statedump, after the user's OK. This batch keeps the digest as the checksum and leaves RefreshSprites out.

### PC parity with RefreshSprites, diagnosed (2026-10-22 afternoon, Integration B2)
The "81 mismatches from frame 120" were the save label. 4875715's `f + 1` assumed a state frame one behind netplay_current_frame. Since
the deletion half sets the state frame from netplay_current_frame at a match's first save, they are equal (logged 0/0, 1/1), so `+1`
labelled every save one frame ahead. The label is now netplay_current_frame() itself, TF.EX's ExFlags.CurrentFrame, and a load
returns it unchanged. Master's seam (e3a2d79) never had the +1.

Then, rematch with SMOKE_DIGEST=0 (Switch statedump 1, page ?statedump=120 via SMOKE_URLEXTRA), hidden, slot b2: 12 DesyncDetected,
at the confirmed frames 2040, 2880, 3000 and 3120 only.
- All page dumps up to 2040 are identical, or differ only in seat 0's input-driven fields. Those are the page's predicted saves: it
  dumps every 120th save, rollbacks included.
- The head animation (RefreshSprites) no longer differs at any frame.

What is left, both in TF.State's state code:
- `RoundLogic.EventLogs.CrownChanges[].PlayerWithCrown` has 6 entries on the Switch, 4 on the PC: a seat array still at the Switch's
  length, from frame 2040 (the round end).
- `PlayerCorpses[0].DodgeTooLateCounter`: Switch -0.4055, PC 0.0, from frame 2846.

Evidence: run 133238 (a copy in B2's $TMPDIR/bc/parity4); `tools/tfex/statediff.py <ryujinx.log> <lvb-browser.txt>`.

## Console, 240 Hz sync test, 2026-10-24 (branch claude/switch-load-restore-speed 4c6a3bd)
Log: `.devserve/results/20261024-095644-prev-hw-log.txt` (hw.txt `hz 240`, `tfextest LMS 0 1 42 2`, `allocwatch 1`, `gcdivisor 1`). The user: "very
very playable", the round intro still bad.

| | before (2026-10-24 08:09, 1b35655) | after (4c6a3bd) |
|---|---|---|
| save (GetState + encode + LZ4) | ~0.9 ms | ~0.45 ms |
| load, round running | ~2.3 ms (spikes to 5) | ~1.4 ms |
| load, during the round intro | ~2.3 ms | climbs to ~8.4 ms (TF.State's intro replay: docs/TFEX-UPSTREAM-CANDIDATES.md 1) |
| game ticks per drawn frame | 119 (falling behind) | p50 4, avg 5.7 (max 84 in the intro) |
| GC | not measured | 2-3 collections per 600 saves once the round runs, ~15 ms world-stopped each |
| sync-test mismatches | not logged | 0 to frame 5507 |

What changed: gen::As cache; TF.State's GetAll/Get/DeleteAll/Delete<T> over a per-layer entity index ([[replace]], TFEX-DIFF S-performance);
RemoveAll(lambda) on game lists lowered in place; FromList reads game lists' fields; the MessagePack writer keeps its buffer; Boehm's
GC_free_space_divisor 2 -> 1. Allocations: a save ~250 managed objects / 21 KB, a load ~490 / 30 KB once the round runs (up to 8,000 / 237 KB
in the intro). Process memory (svcGetInfo UsedMemorySize) read 3188 MB with either divisor: not a useful heap figure.
