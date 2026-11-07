# Licence and components

TF.NX is licensed under the GNU General Public License, version 3 ([LICENSE](LICENSE)). It has to be: it is built on TF.EX
(GPL-3.0), whose code is translated into the Switch module.

| Component | Authors | Licence | How TF.NX uses it |
|---|---|---|---|
| [TF.EX](https://github.com/Fcornaire/TF.EX), TF.State | Fcornaire and contributors | GPL-3.0 | translated into the module (`vendor/tfex`, `hw-probe/exlaunch/source/program/tfex/gen`) |
| [ggrs-ffi](https://github.com/Fcornaire/ggrs-ffi) | Fcornaire | GPL-2.0 | built into the module with Switch changes (`ggrs-ffi-switch`). It states no GPL version of its own, which GPL-2.0 section 9 reads as any version, so it is used here under GPL-3.0 |
| [FortRise](https://github.com/FortRise/FortRise) | Terria and the FortRise team | MIT | its content loader and registries translated (`vendor/fortrise`) |
| [exlaunch](https://github.com/shadowninja108/exlaunch) | shadowninja108 | GPL-2.0 | the module's loader layer (`hw-probe/exlaunch`). It states no GPL version of its own (as ggrs-ffi), used here under GPL-3.0 |
| And64InlineHook | Rprop | MIT | inline hooks |
| libnx, oss-rtld | switchbrew, Thog | ISC | the Switch runtime |
| [GGRS](https://github.com/Fcornaire/ggrs) (Fcornaire's fork) | gschup, Fcornaire and contributors | MIT / Apache-2.0 | the rollback sessions (`ggrs-switch/vendor/ggrs`, with a 5-line change) |
| matchbox and the Rust crates in `ggrs-switch`, `tfx-rtc` | their authors | MIT / Apache-2.0 | netplay and WebRTC |
| [Teams+](https://github.com/belacmu/tf-teamsplus) | belacmu | MIT | a translated code mod (`vendor/teamplus`) |
| Soccer, Move or Die, LoaderAI, AIJimmy, AINova | ebe1kenobi | no licence stated | linked as submodules only: their translation is left out of the published `gen/` and of releases (`public = false` in `tools/tfex/translate.toml`) until the author gives a licence |

TowerFall is Matt Makes Games'. This repository has no game files: the PC game's levels come in as line differences from the
Switch's own (`tools/levels/delta.py`), and the tools read a game copy you provide yourself.
