# The "goal 5" RNG desync at frame 360 (2026-10-17): it was QUICK PLAY's variants

Branch `ws/goal5-desync`. Fix: `be6beb6` (+ test knobs `94cad37` and the `lobbyquickwait` commit).

## Symptom (console, 00:35 match #4 and 01:55)
Lobby match against the browser page, length 2 (goal 5), map -1: GGRS reports DESYNC at frame 360 (digest every 120 frames; 240
still matches). Only the gameplay RNG differs, the archers' positions and speeds are equal; it stays desynced and later rounds pick
different levels. First-to-1 lobbies stayed in sync.

## Cause
Both desynced matches were **QUICK PLAY** matches (`[screens] QuickPlaySearch`, `quick play: match found` in the console logs). The
in-sync goal-5 counter-example (00:53) and every first-to-1 test were custom lobbies (LobbyBrowser / LobbyBuilder). Goal 5 only
correlated because quick play lobbies are length 2. The seat and the archers don't matter (Ryujinx: seat 0 and seat 1 both desync).

TF.EX gives a quick play lobby the netplay tournament rules: `MainMenu.VersusMatchSettings.Variants.ApplyNetplayVariantRules()`
(= `NormalizeForNetplay()` + the game's `MatchVariants.TournamentRules()`, which for 1v1 turns on **SYMMETRICAL TREASURE** and
**NO AUTOBALANCE**) on "OPPONENT FOUND" (0.19.2 `MainMenuPatch.cs:900`) and in the lobby update (`:1679-1686`); the lobby's own
`GameData.Variants` (empty for quick play) are applied only when `!lobby.IsQuickPlay` (`:1685`, `MatchmakingService.cs:978`).
The Switch applied `ApplyVariants(lobby.GameData.Variants)` for every lobby, so quick play ran with all 93 variants off
(`[tfex] ApplyVariants: 93 variants off, 0 of 0 listed on` in both console logs).

How that splits the RNG: at round start (GGRS frame 265, after the 240 digest) `TreasureSpawner.GetChestSpawnsForLevel` loops
`while (... CanSpawnAnotherChest(n))`, one `Chance(ChestChances[0][n])` per try. With Symmetrical Treasure each chest also places its
mirror and counts 2, so after two chests n = 4 = `ChestChances[0].Length` and the loop ends without a draw; without it, a third
`Chance` is drawn. The page made 12 draws at frame 265, the Switch 13.

## Evidence
- Console 01:55 (seed 94309648): replaying xoshiro256** from the seed, the console's digest RNG at 360 (`99e43677…`) is the state
  after 18 draws, the page's (`385eb5f8…`) after 17. The console's draw dump: at frame 265 the chest-position shuffle (+0x9677a0, a
  `List<Vector2>` Fisher-Yates: 5 draws for TowerForge 08's 6 chests) then Chance, Chance, Range, NextFloat, Chance, Range, NextFloat,
  Chance (`TreasureSpawner` / `Calc`), nothing after 265 until 360.
- Ryujinx, quick play, `LVB_PAGEQUICK=1 LVB_SEED=94309648`, digest on both sides, the build without the fix (94cad37): DESYNC at frame
  360 (Switch log and the page's DesyncDetected), rng differs from tick 0 (frame 266) on, positions equal (335 of 599 ticks differ, all
  `rng`); draws at frame 265: Switch 13, page 12 (the page's trace ends with `GetTreasureSpawn`, no closing `CanSpawnAnotherChest`).
- Not reproduced by: page-hosted custom lobbies with the console's settings (length 2, map -1, seed 94309648, tower 10, Switch seat 1,
  archers 0/1 and the console's 1/0): 0 of 599, 14 = 14 draws from frame 200.

## Fix (TF.EX's code)
`tfex/NetplayManager.cpp` `VariantsExtensions::ApplyNetplayVariantRules()`: calls the game's `MatchVariants.TournamentRules`
(+0xac0960) on `MainMenu.VersusMatchSettings.Variants`, logs `[tfex] ApplyNetplayVariantRules (quick play): ... (SYMMETRICAL TREASURE 1,
NO AUTOBALANCE 1)`. Called on "OPPONENT FOUND" (`NetplayScreens.cpp` OnQuickPlayMatched, MMP:900) and at the lobby match start
instead of `ApplyVariants` when `Lobby::IsQuickPlay()` (Kind "QuickPlay"); custom lobbies still get `ApplyVariants(lobby variants)`.
TFEX-DIFF S-variants: TF.EX's `NormalizeForNetplay` nulls each per-player Variant's `playerValues` (keeping the arrays to restore in
`ClearNetplayMode`); the Switch leaves the arrays (a C++ side table would hide them from the GC) and `set_Value` gives every player the
same value, which is what a null `playerValues` reads as. Same values in play.

## Verified (Ryujinx, fixed build be6beb6 + lobbyquickwait)
- Quick play, Switch seat 0 (handle 0), seed 94309648, tower 10: 0 of 595 ticks differ, no DesyncDetected, draws at 265: 12 = 12.
- Quick play, Switch seat 1 (handle 1, `lobbyquickwait 110` so the page queues first): 0 of 598, no DesyncDetected, 12 = 12.
- Custom lobby (Switch hosts, length 2, map -1, seed 678802813): `ApplyVariants ... 0 of 0 listed on`, 0 of 593.
All runs vsstop 600 (one round start and ~330 ticks of play), scripted input, no rollback-heavy network conditions.

## Test knobs added
- hw.txt `lobbyarcher N`: the auto-UI steps Left on its rollcall seat until it has archer N, then readies.
- hw.txt `lobbyquickwait S`: the auto-UI waits S s on NetplaySelect before QUICK PLAY (the page queues first and gets seat 0).
- lobby-auto.sh: `LVB_PAGEBUILD=<keys>` (the page-hosted LobbyBuilder keys; `-` = TF.EX's defaults: RANDOM map, STANDARD length) and
  `LVB_PAGEPICK=<keys>` (the page's archer keys before ready).

## Left for the console
- A quick play match on the console against the page with digest on: expect `[tfex] ApplyNetplayVariantRules (quick play)` twice and
  no DESYNC; symmetric chest pairs appear in round 1. Also a later round (level order and round-start chests every round).
- Not covered here: quick play with TF.EX's WiderSet (wide) and team quick play (TeamTournamentRules is not what TF.EX calls: it calls
  TournamentRules for every quick play lobby, as ported).
- The 00:35 match #4 (seed 1354045655) fits (quick play, all variants off) but its page RNG at 360 wasn't captured, so it's
  checked only by the same signature.

## Later: the round-5 hat-vs-kill desync
Parked; see docs/ws-hat-desync.md.
