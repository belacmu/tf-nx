/* FrVariants: FortRise's custom variants on the Switch. The game's MatchVariants constructor hooked: FortRise's patch_MatchVariants.ctor
 * (generated, unit Fr_MatchVariants) runs after the game's own, as MonoMod's [MonoModConstructor] does around orig_ctor. */
#pragma once

namespace tfex::frvariants {
void Install();
/* TFEX-DIFF S-native (cross-play): the Switch's game has four variants the PC's lacks (PASS THROUGH TEAM, KEEP YOUR ARROWS, NO HYPERS,
 * REAPER CHALICE): a PC or browser peer can't play them, so they join TF.EX's UnauthorizedVariant list (hidden and off in netplay, TF.EX's
 * own BuildMenu prefix / DisableUnauthorized). An [after] rule on MatchVariantsPatchs.MatchVariants_ctor passes the list; once */
void AddSwitchOnlyVariants(void* unauthorizedTitles);
}
