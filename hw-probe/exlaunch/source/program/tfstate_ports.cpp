/* LevelRandomGeometry.GenerateData / LevelRandomItems.AddItems (Cataclysm's geometry and items): the Switch's own solutions for wide mode
 * and the PC's item placement (split out of xplay.cpp; shared state: xplay_internal.hpp). TF.State's gameplay patches that were hand-ported
 * here (Miasma, Icicle, MoonGlassBlock, PlayerCorpse, BrambleArrow, SensorBlock) run as TF.EX's own code now (tfex/gen State_*, installed at
 * boot by tfexgen_glue; 2026-10-21). */
#include "xplay_internal.hpp"

namespace xplay {
/* LevelRandomGeometry.GenerateData(List<Rectangle> rects, bool[,] baseData) -> bool[,] (Cataclysm's geometry). */
HOOK_DEFINE_TRAMPOLINE(GenerateDataHook) {
    static void* Callback(void* rects, void* base) {
        if (pcgameplay::Wide()) { if (void* r = levelitems::RunPcGeometry(rects, base)) return r; }   /* TF.EX wide: the PC's, WiderSet's constants */
        return Orig(rects, base);
    }
};

/* LevelRandomItems.AddItems(this, bool[,] solids, XmlElement entities, bool teams, bool extra) (Cataclysm, after GenerateData; the
 * Switch's has a 5th argument, the PC's 3: `extra` gates the Switch-only ChalicePad rectangle loop). */
HOOK_DEFINE_TRAMPOLINE(AddItemsHook) {
    static void Callback(void* self, void* solids, void* entities, bool teams, bool extra) {
        /* versus/netplay: the PC's AddItems (levelitems.cpp; the Switch's places Cataclysm's items differently) */
        bool pc = g_vsOn || g_vsPending || g_npOn;
        if (pc) levelitems::RunPc(self, solids, entities, teams); else Orig(self, solids, entities, teams, extra);
    }
};

/* LevelRandomBGTiles.GenerateBitData(bool[,] solids) and LevelRandomBGDetails.GenerateTileData(baseSolids, bg, finalSolids, entities)
 * (Cataclysm's background, after the items): in a wide match the PC's with WiderSet's constants (levelbg_core.hpp); the Switch's own
 * placed lanterns (BGTorches, which set arrows on fire) and lit tiles elsewhere in wide. Standard: the Switch's (equal to the PC's). */
HOOK_DEFINE_TRAMPOLINE(BGBitDataHook) {
    static void* Callback(void* solids) {
        if (pcgameplay::Wide()) { if (void* r = levelitems::RunPcBGBitData(solids)) return r; }
        return Orig(solids);
    }
};
HOOK_DEFINE_TRAMPOLINE(BGTileDataHook) {
    static void* Callback(void* baseSolids, void* bg, void* finalSolids, void* entities) {
        if (pcgameplay::Wide()) { if (void* r = levelitems::RunPcBGTileData(baseSolids, bg, finalSolids, entities)) return r; }
        return Orig(baseSolids, bg, finalSolids, entities);
    }
};
void InstallTfStateHooks() {
    xplay::BGBitDataHook::InstallAtOffset(0xa88d30);
    xplay::BGTileDataHook::InstallAtOffset(0xa84280);
    xplay::GenerateDataHook::InstallAtOffset(0xa8ab50);
    xplay::AddItemsHook::InstallAtOffset(0xa8fbc0);
}

} // namespace xplay
