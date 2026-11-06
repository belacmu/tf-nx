# tools/tfex: TF.EX update tooling

What changed between two TF.EX versions, where it lands in our port, and what kind of change it is (TFEX-BASE.md sections 6-7);
and the translator that turns TF.EX's C# into the Switch module's C++ (below).

## New TF.EX tag (the routine)

```
tools/tfex/new-tag.sh v0.22.0            # old = vendor/tfex's current tag; or: tools/tfex/new-tag.sh <new> <old>
```

One command, about 1.5 minutes, commits nothing. It runs, in order:
1. `git -C vendor/tfex fetch --tags` (`TFEX_REMOTE=<name>` for another remote, e.g. a fork);
2. `update.sh <old> <new>` -> `reports/<old>-<new>.md` (the full report, below);
3. moves vendor/tfex to `<new>` (detached);
4. `translate.sh coverage <new>` -> `reports/translator-coverage-<new>.md` (this also runs bind.py, which repairs binding.json);
5. `translate.sh state-msgpack <new>` (TF.State's MessagePack formatters: GameState's keys are checksummed bytes), `translate.sh all
   <new>` (regenerates tfex/gen), then `tools/re/statepins.py` (pins for the private game fields TF.State reads, from the layout
   database; needs `.cache/towerfall-pc.il`) and `all` again when it added any;
6. `manifest.py check <new>` (ledger keys that match nothing any more);
7. the module build (`tools/build-probe.sh`; `TFEX_NO_BUILD=1` skips it);
8. `new-tag.py`: **`reports/<old>-<new>-summary.md`, the one page to read**: the verdict (lockstep changes and what the port is
   behind on, protocol, UI, coverage before -> after, generated files changed, build, dead ledger keys), then every place of ours
   the change touches: hand-written ports behind the new body (manifest `at`), ledger replacements to re-check (overrides
   `[replace]`), translated units (translate.toml), service-map entries (overrides `[service]`), and the new or unported members with
   where they'd land. That list is what goes to whoever ports hand-written code (tfex-adopt).

When it stops (a rule key that no longer resolves, a build error: both printed), fix that and resume where it stopped instead of
rerunning everything (each full run is ~10 minutes): `TFEX_FROM=4b tools/tfex/new-tag.sh <new> <old>` (steps 3, 4, 4b, 5, 5b, 6, 7;
vendor/tfex already at `<new>`, `<old>` given). The rule-key gate stops only on keys that resolved at `<old>` and don't at `<new>`:
the keys already unresolved at `<old>` (older debt, 134 at the v0.29.0 update) are listed in the report but don't stop it (listed once
per `<old>` commit into `.cache/tfex/rule-keys-unresolved-<old>.txt`). In a worktree, ensure-vendor.sh links the main checkout's
shared `.cache` inputs (towerfall-pc.il, the FortRise-patched game, pc-src) that statepins.py and tools/re need.

Then: port what the summary lists (A first; a Postfix -> Finalizer rename shows as removed + added: read it before porting), and
continue with "After porting a tag" below; commit vendor/tfex, the reports, binding.json and tfex/gen together after a lobby match.
Log: `.cache/tfex/new-tag-<old>-<new>.log`. First run: v0.21.0 -> v0.21.1 (`reports/v0.21.0-v0.21.1-summary.md`): A changes are
Postfix -> Finalizer renames and a capture gate, no simulation change (hand reading in docs/ws-tfex-update.md "v0.21.1").

After a merge (gen/ merges with merge=ours, .gitattributes), check that gen/ is the merged rules' output (exit 1: regenerate):

```
tools/tfex/check-gen.sh            # tag: the vendor/tfex commit HEAD records
```

Rule keys (every overrides.toml [service] and translate.toml [fortrise] / [invalid] key and [[unit]] member must resolve at the tag;
unused keys are listed, to delete or mark `parked = "..."`; new-tag.sh runs it and fails on a key that resolved at `<old>`; ~40 s):

```
tools/tfex/translate.sh rules v0.21.1   # -> tools/tfex/reports/rule-keys-v0.21.1.md, exit 3 on an unresolved key
```

translate.sh and new-tag.sh fail when the FortRise-patched game DLL is missing or vendor/tfex isn't at the commit HEAD records
(`tools/tfex/inputs.py`), and record the outside inputs' sha256 in tools/tfex/reports/inputs-<tag>.sha256.

The hand-code inventory (docs/HAND-CODE-INVENTORY.md: every tfex/*.cpp function as L / S / T / D, by callers, rules and markers;
`--check` exits 1 when it is stale; a new function without a rule or marker shows as `?`):

```
tools/tfex/inventory.py
```

The report alone, for any two versions:

```
tools/tfex/update.sh v0.21.0 v0.22.0 tools/tfex/reports/v0.21.0-v0.22.0.md
```

A run takes about 25 s cold (splitter build, `patch-targets.sh` on both tags) and 2-3 s after that (cached in `.cache/tfex/`).
`TFEX_NO_TARGETS=1` skips the patch-target check, `TFEX_NO_TRANSLATE=1` the translator diff, `TFEX_JSON=<file>` also writes the
findings as JSON.

## A fork, branch or commit instead of a tag

Every script takes anything `git -C vendor/tfex rev-parse` resolves: a tag, a commit, or a remote branch (`origin/main`,
`fork/couch-local`); caches are keyed by commit, labels read `<ref> (<short sha>)`, and a `/` in a ref becomes `_` in report file
names. Checked with `origin/main` and a bare commit (update.sh, translate.sh). For our own TF.EX change (docs/research-couch-online.md):
- once per checkout: `git -C vendor/tfex remote add fork <fork url>`; then `TFEX_REMOTE=fork tools/tfex/new-tag.sh fork/<branch>`.
- to commit vendor/tfex at a fork commit, every checkout must be able to fetch it: point `.gitmodules` `url` at the fork (keeping
  upstream's tags there: `git push fork --tags`) and `git submodule sync`; otherwise a fresh clone or worktree can't check out
  vendor/tfex.
- a lockstep (A) change in the fork must run on both sides: the browser's TF.EX has to be built from the same fork commit for a
  Switch-vs-TF.EX match.
- a later upstream tag: rebase the fork branch on it, then `new-tag.sh fork/<branch> <previous fork commit>`.

## Pieces

| file | what |
|---|---|
| `splitter/` | Roslyn console app (net10.0, Microsoft.CodeAnalysis.CSharp 5.0.0). One JSON line per C# member: file, `Type.Member(param types)`, `strict` hash (tokens; whitespace and comments ignored), `norm` hash (after a syntax normalizer: target-typed `new`, collection expressions, `[.. x]` / `ToArray()` / `ToList()`, `=>` bodies, `this.`, `default(T)`), Harmony target. Equal `norm`, different `strict` = syntax-only. |
| `split.sh <tag>` | runs the splitter on a tag (git archive of vendor/tfex), cached by commit in `.cache/tfex/`. |
| `manifest.toml` | where our port implements each TF.EX member: `"<file> <Type>.<Member>" = { at, conf, note, hash }`. `hash` = the upstream body the port matches. |
| `overrides.toml` | the section-3 ledger as data: `[skip]` (with reason), `[replace]` (S-key, our replacement, upstream hash), `[names]` (PC → Switch). |
| `rules.toml` | how a change gets its class A-E (path globs, member globs, regexes over the changed tokens). |
| `update.py` / `update.sh <old> <new> [out]` | the report. |
| `new-tag.sh <new> [<old>]` / `new-tag.py` | the routine above and its one-page summary (reads update.py's `--json`). |
| `manifest.py fill/check/coverage <tag>` | upkeep: write hashes, find dead keys, measure what is accounted for. |
| `expect/<old>-<new>.toml` | a hand reading as expected labels; `update.sh` adds a check section when one exists (regression test for `rules.toml`). |
| `patch-targets.sh`, `targets.py`, `check.py` | Harmony targets vs the 1.4.1.6 binary (`TFEX_ALL=1` also lists the found ones). |
| `backlog.py` / `backlog.toml` | the backlog: every TF.EX member at the pin as generated, replaced, hand, skipped, or queued in a step of docs/STRATEGY-2026-10-28.md (`backlog.toml [assign]`, same keys); writes docs/TFEX-BACKLOG.md. `--check` fails on a member in no step (new-tag.sh step 5b lists them); `--list "step 3"` prints one step's members. |
| `reports/` | generated reports. |

Keys: `"<path under vendor/tfex/src> <Type>.<Member>"`. The file part is an fnmatch glob (`*` crosses `/`); the member part may
omit `(params)` (all overloads), be `Type.*` (the type, nested types included) or `*` (the file). A manifest entry wins over an override.

## The report

0. **Generated**: a changed member the translator emits (`tfex/gen/tfexgen_members.txt`, written by every translator run: each TF.EX
   member translated without a blocker, minus `[[replace]]`s) follows upstream by regeneration: counted per file, no port work. A manifest
   entry whose changes are all generated is listed for deletion; `manifest.py prune [<tag>]` deletes such [port] / [skip] entries
   (new-tag.sh runs it), so the ledger holds only the real hand differences.
1. **Stale**: a manifest entry whose upstream hash at the new tag differs from its `hash`: our code is behind. With the member diffs.
   "Still behind" lists entries that changed before the old tag (the port predates it).
2. **Overrides to review**: a `[replace]`d member changed upstream: does the replacement still fit?
3. **New and unported**: no manifest entry, no override. Each has "lands near": our port of the upstream members that use it
   (else the places of its type). Either port it (and add a manifest entry) or add a skip with its reason.
4. **Skipped**, 5. **Syntax-only**, 6. **Harmony targets** (new ones, found or not on 1.4.1.6),
   7. **Required checks**: for A the Switch-vs-TF.EX match and the rollback drill; for B the tfex-server to-do (models, message
   types, new endpoints and keys).

Classes: A lockstep, B protocol, C UI, D not ours, E noise, `?` no rule matched (triage, then add a rule).

## After porting a tag

1. Port the stale and new items (A first), add manifest entries for new ported members (`at`, `conf`), skips for decisions.
2. `python3 tools/tfex/manifest.py fill <new tag> [key glob]` (writes the entries' hashes at that tag; without a glob every entry: only after the whole tag is ported,
   otherwise pass a glob for what you ported), then `manifest.py check <new tag>`.
3. Commit `vendor/tfex` (new-tag.sh already moved it; by hand: `git -C vendor/tfex checkout <new tag>`).

## Translator (TF.EX C# -> Switch C++)

```
tools/tfex/translate.sh all v0.21.0          # models + code units -> hw-probe/exlaunch/source/program/tfex/gen/
tools/tfex/translate.sh coverage v0.21.0     # reports/translator-coverage-v0.21.0.md
tools/tfex/translator/test/models-test.sh    # stage 1 against TF.EX's own MessagePack (oracle) and the hand-written models
XPLAY_RJSLOT=<slot> tools/re/notif-test.sh "gennotiftest 200"               # stage 3: the generated Notification
```

`tfex/gen/` is only written by the translator (re-run it; never edit). `update.sh` runs it at both tags and reports the diff
(section 9). The pieces:

| file | what |
|---|---|
| `translator/Workspace.cs` | TF.EX's whole source as one Roslyn compilation with a semantic model, against TowerFall.exe, FNA, FortRise, MessagePack (translate.toml [refs]); accessibility ignored (TF.EX builds against a publicized TowerFall.exe) |
| `translator/ModelsGen.cs` | stage 1: `Models/WebSocket/**` -> structs + JSON read/write as TF.EX's MessagePack does it (tfexgen_Models.*) |
| `translator/CodeGen.cs`, `UnitEmitter*.cs` | stages 2-3: translate.toml `[[unit]]`s (a TF.EX type or some of its members) -> tfexgen_<unit>.*, from Roslyn's operation tree. Game and FNA members are reached by offset; classes deriving from game classes become real game subclasses (brute::MakeSubclass), lambdas game delegates, LINQ internal iteration, `is`/`switch` patterns IsInst, DynamicData members by name |
| `translator/Binder.cs`, `bind.py`, `binding.json` | the game members used -> tools/re (rtypes, sigs, tfexrefs, classmeta) -> offsets. binding.json is committed (the binary is fixed): builds need no main.flat. Overloads of corlib/FNA types: Mono's mscorlib / FNA metadata order (translate.toml [refs] mono) |
| `hw-probe/exlaunch/source/program/tfex/runtime/` | hand-written C++ the generated code includes (compiled in place since 2026-10-18; gen/ holds only generated files): JSON holders, FNA value types, string literals and built strings (String.InternalAllocateStr), LINQ snapshots, the test glue (hw.txt gendemo / gennotiftest) |
| `translator/glue/*.cs` | hand rules written as C# and translated like TF.EX: `[[rewrite]]` (a call -> glue), `[[replace]]` (a ledger difference replaces a member), `[Cpp]` externs (Switch code the glue calls), test drivers |
| `translate.toml` | refs, model capacities, units, rewrites, replacements, `[pin]` (game members the binder can't pick, with the evidence), `[invalid]` (members the reference TowerFall.exe lacks, FortRise's, by C# source text) |
| `translator/StructsGen.cs` | TF.EX's plain-data structs (NetworkStats, PingStats, Input, Vector2f ...) as C++ PODs in `tfexgen_structs.hpp` (`tfex::gen::st`) |
| `translator/test/` | oracle (TF.EX's model classes + MessagePack), real server messages (capture.mjs against the local tfex-server), synthetic v0.21.0 ones |

### Hosted units and [hand]

A `[[unit]]` with `hosted = true` translates methods of a TF.EX class whose object is hand-written (tfex/MatchmakingService.cpp):
no class of ours is made, `self` is passed through, and the class's own fields are read and written only through overrides.toml
`[service]` rules on the field (`"<Namespace.Type>.<field>" = { cpp = "<get>", set = "<C++ with $value>" }`); a field without one is
reported ("field of a hosted unit's type without a [service] rule"), so the report lists exactly what the hand side must provide.
`translate.toml [hand] need` lists game members hand-written code takes from tfexgen_binding.hpp that no unit uses
(`"f TowerFall.Sounds ui_invalid"`).

### After a bind.py change: run coverage

bind.py runs only for keys binding.json lacks or has as errors, and only for keys some unit requests. Its in-place repairs (abstract
vtable slots proven by the subclasses, null type getters) therefore apply only when it runs. `translate.sh code` requests just the
configured units' keys and often runs nothing, so after changing bind.py run `tools/tfex/translate.sh coverage v0.21.0` (it requests
every key and runs bind.py), then `translate.sh code`, and commit binding.json with gen/.

### FortRise reference

The translator compiles TF.EX against the FortRise-patched PC game (what TF.EX and the browser really run):
`~/Code/Towerfall Switch Mods/switch-online/.cache/fortrise-patched/TowerFall.Patch.dll`, in the main checkout's `.cache` (not in
git; every worktree reads it there through that absolute path). Without it the translator warns and falls back to the vanilla
`/Applications/TowerFall.app` exe (1.3.2.0): FortRise members turn Invalid and the output differs. To recreate it:

```
mkdir -p ~/Code/Towerfall\ Switch\ Mods/switch-online/.cache/towerfall-steam
cp ~/Library/Application\ Support/Steam/steamapps/common/TowerFall/TowerFall.app/Contents/Resources/TowerFall.exe ~/Code/Towerfall\ Switch\ Mods/switch-online/.cache/towerfall-steam/
cd ~/Code/Towerfall\ Browser/tools/FortRisePatch && dotnet run -c Release -- ~/Code/Towerfall\ Switch\ Mods/switch-online/.cache/towerfall-steam/TowerFall.exe ~/Code/Towerfall\ Switch\ Mods/switch-online/.cache/fortrise-patched
```

Patch a copy, never the Steam folder. The PC exe must be 1.3.3.1 or later (the Mac app's 1.3.2.0 is refused). The patcher is the
browser's FortRise (`vendor/fortrise/version.txt`, 5.5.0-beta.3; TF.EX 0.21.0 targets beta.2). FortRise's members that the Switch's
1.4.1.6 lacks are listed in translate.toml `[fortrise]` (absent: default reads, no-op writes and calls).

Every hand rule a unit used is listed in `tfex/gen/tfexgen_report.txt`; a construct the translator can't translate compiles to a
`static_assert` (TFEXGEN_UNSUPPORTED), so a unit with one doesn't build.
