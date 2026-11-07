# TF.NX

[TF.EX](https://github.com/Fcornaire/TF.EX) for TowerFall on Nintendo Switch: online play with rollback netcode, in the same lobbies
as TF.EX on PC and the TowerFall browser port. It also brings [FortRise](https://github.com/FortRise/FortRise)'s mod loading to the
Switch, so PC mod folders work on the console.

TF.NX is not a rewrite. A translator (`tools/tfex`) turns TF.EX's and FortRise's C# into C++ that runs inside the Switch game, so TF.NX
follows each TF.EX release.

## What you need

- A Switch running [Atmosphère](https://github.com/Atmosphere-NX/Atmosphere).
- Your own copy of TowerFall for Switch, version **1.4.1.6**.

TF.NX contains no game files.

## Install

1. Download the zip from [Releases](https://github.com/belacmu/tf-nx/releases).
2. Extract it onto the root of your SD card. It adds `atmosphere/contents/0100A1C00359C000/exefs/` and `xplay/`.
3. Start TowerFall. **NETPLAY** is on the main menu, and mods are on the **MODS** screen.

TF.EX's server only lets its latest version play online, so install each new TF.NX release the same way.

## Mods

Put mod folders in `sd:/xplay/mods/<mod>/`, exactly as the PC release ships them.

- **Content mods** (archers, skins): drop them in.
- **Code mods** work when TF.NX carries their logic. The PC's DLL is never run on the Switch. The list is in
  [docs/MODS-CATALOG.md](docs/MODS-CATALOG.md).
  - [Teams+](https://github.com/belacmu/tf-teamsplus) (three teams) is included in the release.
  - **WiderSet** (8 players): download WiderSet from GameBanana and add its folder.

Online play uses TF.EX's own rules: mod game modes stay offline until TF.EX supports them online.

## Status

Early. Online lobbies, with one or two players per Switch, play in sync with TF.EX v0.29.0 in our tests. Spectating and replays
aren't supported yet. Known problems are in [STATUS.md](STATUS.md). If something breaks, open an issue and attach `sd:/xplay/hw-log.txt`.

## Building

You need devkitPro (devkitA64, libnx), the .NET 10 SDK, and a nightly Rust toolchain.

```bash
git submodule update --init
ggrs-switch/build.sh                    # the netplay library (Rust)
tools/tfex/translate.sh all v0.29.0     # TF.EX + FortRise -> C++ (optional: gen/ is committed)
tools/build-probe.sh                    # the module -> hw-probe/exlaunch/deploy/
tools/release.sh                        # the release zip -> dist/
```

Translating needs TF.EX's build inputs (`tools/tfex/inputs.py` says which).

The published `gen/` leaves out code mods whose authors haven't given a licence. Those mods' units are marked `public = false` in
`tools/tfex/translate.toml`.

Where to read further: [docs/CORE-FLOW.md](docs/CORE-FLOW.md) (how a match runs), [ROLLBACK.md](ROLLBACK.md), and
[tools/tfex/README.md](tools/tfex/README.md) (the translator).

## Licence

GPL-3.0, as TF.EX is. [NOTICE.md](NOTICE.md) lists every component and its licence.

TF.NX is a fan project, not affiliated with Matt Makes Games. TF.EX is by Fcornaire, and FortRise by Terria and the FortRise team.
