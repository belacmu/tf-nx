/* WiderSet, the catalog mod Teuria.WiderSet (mods/Teuria.WiderSet: mod.toml, NOTES.md). TFEX-DIFF S-native: its logic is the Switch's
 * stand-in for WiderSet (the game's own wide mode, pcgameplay.cpp's WiderSet tables and constants, eightplayer.hpp's 8 seats); its folder
 * is the player's own WiderSet download (its wide levels, read through FortRise's mod paths). */
#include "hwlog.hpp"
#include "megamod/megamod.hpp"
#if XPLAY_EXTRAS
#include "eightplayer.hpp"
#endif

namespace pcgameplay { void EnableWiderSet(); }

namespace widerset {
/* mod.toml early: the 8 seats' arrays and loop bounds, 6 -> 8, and their hooks, at module start: the game makes those arrays before any
 * mod folder loads (a no-op with 6 players or fewer; the 2-8 lobby itself waits for the folder: Boot) */
void Early() {
#if XPLAY_EXTRAS
    eightplayer::InstallAll();
#endif
}
/* mod.toml boot (its folder loaded): wide levels from the folder, local play on them, the 2-8 lobby */
void Boot() {
    pcgameplay::EnableWiderSet();
#if XPLAY_EXTRAS
    eightplayer::OfferEight();
#endif
}
}  // namespace widerset
