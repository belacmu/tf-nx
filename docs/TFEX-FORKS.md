# Our TF.EX fork (belacmu/TF.EX): every branch, what it's for, where it stands

Upstream: Fcornaire/TF.EX. Our fork: belacmu/TF.EX (vendor/tfex's `origin`; `upstream` is Fcornaire's). Updated 2026-11-06.

**Rule (the user, 2026-11-06): TF.NX's main pins an upstream release, never our fork.** A fix of ours reaches TF.NX through upstream:
an issue first, a PR once Fcornaire agrees the shape (docs/TFEX-UPSTREAM-CANDIDATES.md has the write-ups). Online features for mod game
modes wait until TF.EX has them. What the browser runs: the official release (v0.29.0).

## In review or reported upstream

| Branch | Base | Commits | Upstream | What it is |
|---|---|---|---|---|
| `mod-modes` | v0.29.0 | 4 (+ the mod-modes-online ones) | PR #314, open | Mods' game modes online (the lobby's required mod, a mod's rollback state, replays, the test command) |
| `mod-modes-screenshots` | - | 1 | for PR #314 | Screenshots for #314's description |
| `wide-spawn-centre` | v0.29.0 | 2 | PR #316, open | TF.State's spawns use the screen's centre on wide (WiderSet) maps |
| `game-mode-scroll` | v0.29.0 | 15 (team-revive-modmodes-v0.29.0 + 1) | not filed: latent on the PC (candidate 7); raise with #314 | Local VERSUS: modes registered after TF.EX's are reachable. The Switch's pin until 2026-11-06 |

## Merged or done upstream (the branch can go)

| Branch | Upstream | Note |
|---|---|---|
| `team-revive-netplay` | PR #312 merged 2026-11-04 (2deb995 on main) | In upstream's next release |
| `team-revive-v0.28.1`, `team-revive-modmodes`, `team-revive-modmodes-v0.29.0` | (#312) | Earlier bases of the same fix, with or without mod-modes |
| `local-multiplayer`, `local-multiplayer-quickplay`, `lmp-v0.22.2`, `lmp-v0.27.0` | PR #283 closed: TF.EX v0.28.0 has its own local players | Retired |
| `main` | - | Our fork's main, at upstream's 2026-10-17 (#282); behind, unused |

## Our own, not proposed yet (8 players online and related)

| Branch | Base | Commits | What it is |
|---|---|---|---|
| `eight-players-v2` | v0.29.0 | 19 | 8 players online (wide lobbies), merging wide-spawn-centre; `eight-players` is its v0.28.1 predecessor |
| `wide-lobbies-8` | v0.29.0 | 2 | Wide lobbies up to 8 players, at most 4 devices |
| `wide-lobby-seats`, `wide-lobby-seats-v2` | v0.29.0 | 2, 4 | A wide lobby's player limit is the game's seats |
| `player-limits`, `player-limits-v2` | v0.29.0 | 1, 3 | Player counts in one place (Constants.MAX_PLAYERS) |
| `seat-loops` | v0.29.0 | 1 | Loops and arrays over the game's seats |
| `game-logic-speed` | v0.29.0 | 6 | Game logic speed shown only where it stops play |

These would each need an issue (design first) before a PR. Until then TF.NX's main has none of them.
