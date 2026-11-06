# Testing parity with TF.EX (plan, 2026-10-24)

Goal: the Switch plays every match the way TF.EX does, byte for byte in TF.State's saved state, so GGRS never desyncs. Tested in
Ryujinx against the browser page (TF.EX's own code), confirmed on the console before each publish.

## The tool
`tools/parity-sweep.sh [scenario file] [names...]`: one smoke lobby match per scenario (the Switch hosts, the page joins), TF.State's
state dumped on both sides every 120 frames, compared by `tools/tfex/statediff.py --bytes` (the fields that differ, then the saved
bytes GGRS checksums). One verdict line per scenario in `<out>/summary.txt`. Scenarios: `tools/parity/scenarios.txt`
(`name | seed | hw.txt lines | environment`). The page is the browser worktree harness-dump (browser main + three local harness
commits: the page's ?statedump, the local server's TFEX_FIXED_SEED, xlobby wide=1).

## How to test everything without running everything
1. **Mix many features per match, split on failure.** A scenario combines independent dimensions at once: tower, wide, mode,
   players per device, a group of variants. The set is chosen so every pair of values of two dimensions meets in at least one
   scenario (pairwise coverage): interaction bugs show up in a few dozen matches, not thousands. A failing scenario is split
   (half the variants, then each dimension back to its default) until the smallest failing combination is left; that one becomes
   a permanent scenario.
2. **Inputs that use the features.** The scripted players mostly move and jump. Seeded random inputs that shoot, dodge, catch and
   pick things up exercise the variants and items far more (to do: a script generator per seed).
3. **Three checks, cheapest first:** GGRS's checksum (does it desync), statediff (which fields), the saved bytes (is it
   serialization, not gameplay: the LZ4 block layout was one).
4. **Three tiers.**
   - Quick (about 6 scenarios, 25 min): after any change to gameplay, state, translation rules or the runtime.
   - Nightly (pairwise set, a few hours): fresh seeds each night; a failure is kept with its seed.
   - Console: a short checklist before each publish (the emulator can't show timing, the GC, the real network).
5. **Player-count agnostic.** Nothing may assume a number of players (the 8-player Switch mod comes later): counts are read from
   the game (TFGame.Players) or the lobby. Known hard-coded counts to remove: `i < 6` loops in lobbymatch.cpp, xplay.cpp,
   EndOfMatch.cpp; `kMaxPlayers = 4` in tfex/NetplayManager.cpp (hand code); MainMenuPatch's lobby size clamp to 4.
6. **Known upstream issues** are listed apart so they don't hide new ones (below).

## Dimensions
| dimension | values | harness |
|---|---|---|
| tower | 16 versus, 16 wide | hw.txt lobbymap |
| wide | off / on | lobbywide 1 + page LVB_MODS=TF.EX,Teuria.WiderSet LVB_XLOBBY_EXTRA=wide=1 LVB_PREKEYS=c |
| mode | free for all, team deathmatch | lobbymode 5, lobbyteam, page LVB_TEAMKEY |
| players | 1 or 2 on the Switch (lobbylocal2), 1 page (more pages: to do) | lobbymax, lobbylocal2 |
| variants | 93, in groups of compatible ones | lobbyvariants TITLE\|TITLE (160 characters) |
| match length | 1800 ticks (several levels); whole match + rematch: to do | vsstop, the rematch scenario |
| rate | 60 Hz; 240 Hz: to do | hw.txt hz |

## Findings so far
- 16-tower sweep (c70da06): LZ4 block layout, gen::Snap's 64 cap, JumpThru.counter unresolved. All fixed.
- Mixed scenarios: Session.Scores in a team match saved as seats (fixed); controller 2 never picked a team (harness, fixed).
- **Upstream: Arrow.DoWrapRender changes saved state while drawing.** It adds the wiggle angle to each image's Rotation, draws, and
  subtracts it again; `(x + n) - n` isn't always `x` in floats, and TF.State saves the rotation. The result depends on how many frames
  were drawn (frame skips; rollbacks resimulate without drawing), so two desktop TF.EX players can desync on it too. Seen as a 1-ULP
  BombArrow sprite rotation in mix-team-two-wide (seed 2001). Fix: restore the exact rotation after drawing, on the Switch and the
  browser alike; an upstream candidate (docs/TFEX-UPSTREAM-CANDIDATES.md).
- Mixed scenarios, second batch (each fixed at the root, with a guard so the class can't return):
  - **gen::Opt had .NET's Nullable layout** (flag first); the game is Mono (value first). Every game `T?` field TF.State reads or writes
    was misread (a ghost's lastDir: the reader shifted, Session.Miasma read null, a crash). Fixed for all nullables; static_asserts check
    the layout at build time.
  - **foreach over a translator value list (VList) used the game's List calls** (BrambleSpreadState.Made: saved empty, brambles regrew on
    every rollback). The translator now loops over value lists as such, everywhere.
  - **[[rewrite]] Calc.Shuffle went to a List<MapButton> glue for every element type** (TF.State's spawn indexes were shuffled through
    MapButton's accessors: other team spawns). Generic glue methods are now copied per type argument, and a rewrite whose glue doesn't
    take the call's argument types is an error (it found SceneAddList / SceneRemoveList, made generic too).
  - **FortRise's publicised fields** (VersusLevelSystem.LastLevel = the game's private lastLevel) resolve through the private spelling,
    in bind.py's field records and in the [pin]s.
  - **Drawing changed the saved state** (the upstream issue above): LevelEntity.Render and Arrow.DoWrapRender now put the position /
    rotations back exactly, on the Switch and in the browser.
  - Guard: the parity sweep runs `statemeasure 1 0`: every save is read back and written again (a reader / writer mismatch is a named
    failure, not a crash later).
- Not covered yet: the Reaper Chalice variant (the Switch game has no chalice gameplay: ChaliceGhost's fields are unresolved).
- Frame 0: the Switch's first Engine.TimeMult is 1.0, TF.EX's 1.0000019; not checksummed, overwritten by the first Level.Update.
