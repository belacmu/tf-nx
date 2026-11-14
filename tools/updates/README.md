# Upstream updates: noticed, then investigated (nothing changed)

Something TF.NX carries got a newer upstream version: `check.py` notices it, an agent investigates it, the user decides. The
investigation **changes no tracked file**: no commit, no edit, no `vendor/*` checkout moved, no gen/ regenerated, no build, no publish.
It writes one report and the user approves the work before any of it starts (the user, 2026-11-14).

## Who runs it

- **The scheduled task `tfnx-update-check`** (Claude desktop app, on this Mac, twice a day) runs `check.py`. A NEWER/CHANGED line not
  seen last run (`~/.claude/scheduled-tasks/tfnx-update-check/last-newer.txt`) starts the investigation below in the same session, then
  a phone/desktop notification with the report's verdict.
- **GitHub (`.github/workflows/update-check.yml`)** runs `check.py` too and opens one issue mentioning the owner. Notice only: the
  investigation needs this Mac (the game DLL, `tools/re`, the TF.EX splitter and translator).

## The investigation

Write `.cache/updates/<component>-<old>-<new>.md` (`.cache/` is ignored; a "/" in a name becomes "_"). Read-only commands only; fetching
upstream refs (`git -C vendor/<x> fetch --tags`, `gh release view`) is fine, checking them out is not.

**TF.EX** (`<old>` = the tag `vendor/tfex` is at, `<new>` = check.py's version with a `v`):
1. `git -C vendor/tfex fetch -q https://github.com/Fcornaire/TF.EX.git refs/tags/<old>:refs/tags/<old> refs/tags/<new>:refs/tags/<new>`
   (by URL: vendor/tfex's `origin` is the fork belacmu/TF.EX, which may not have upstream's tags)
2. `TFEX_JSON=.cache/updates/tfex-<old>-<new>.json tools/tfex/update.sh <old> <new> .cache/updates/tfex-<old>-<new>-full.md` (about
   2.5 minutes). This is new-tag.sh's report step without moving vendor/tfex (tools/tfex/README.md "New TF.EX tag"). Its translator
   runs record `tools/tfex/reports/inputs-<new>.sha256`: delete that new untracked file afterwards. A translator abort in the run's
   log (`.cache/tfex/gen-<sha>.log`) is a finding: a translate.toml rule the new tag breaks.
3. `gh release view <new> -R Fcornaire/TF.EX` and the commits between the tags (`git -C vendor/tfex log --oneline <old>..<new>`).
4. Read the full report's A (simulation) and B (protocol) sections and every changed member with no ledger entry; read the hunks.

**Teams+ / a GitHub-release mod**: `gh release view <tag> -R <repo>`, `gh api repos/<repo>/compare/<old>...<new>` against the commit the
submodule (`mods/<mod>/mod.toml` `vendored`) is at. `tools/mods/update.sh <mod> <tag>` is the whole update when the diff only touches
what it translates; say so.

**FortRise**: `gh release view <tag> -R FortRise/FortRise` and the compare against `vendor/fortrise`'s commit; which of its files our
translation reads (translate.toml [fortrise] rules) changed. No script does this update (the last one: d86eb879).

**A GameBanana mod** (WiderSet, TowerBall): the page's changelog and files (GameBanana's API, as check.py reads it); what our port
(mods/<mod>/, its overview in mods/README.md) would have to follow. No download into the repo.

## The report (one page, in this order)

1. **Verdict**, one line: "script only" (the update script does it all, then one emulator run), "small hand port" or "real porting
   work", and for TF.EX whether a Switch at the old version can still play online.
2. **What changed upstream**: the release notes, the commits, in a few lines.
3. **The work**, as steps someone can approve: the command(s), then each hand change with the file it lands in (ours: gen/ rules,
   overrides.toml, translate.toml, hand runtime, Switch code) and why; the browser harness at the new version when TF.EX's lockstep or
   protocol changed; the emulator scenario that proves it (`tools/smoke.sh <scenario>`, AGENTS.md "Testing").
4. **Risks and unknowns**: a desync risk (A changes), server changes (B: our tf-ex-matchmaking-server fork), new Harmony targets
   without RE (patch-targets), whatever the agent couldn't settle read-only.
5. **Estimate**: the size of the work (small / a session / several sessions).
