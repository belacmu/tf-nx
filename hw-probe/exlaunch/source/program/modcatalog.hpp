/* TF.NX's catalog mods (docs/PLAN-TFNX-2026-11-04.md): the code mods whose logic the module carries, each turned on by its folder in
 * sd:/xplay/mods. One folder per mod in the repo, mods/<name>/ (mod.toml, meta.json, its folder builder, src/ for native code);
 * tools/mods/catalog.py generates modcatalog.gen.hpp (the table) and docs/MODS-CATALOG.md from them. The registry
 * (tfex/runtime/tfexgen_mods.cpp) checks a folder against its entry; tfex/FrModsBoot.cpp runs the boots, replaces and overlays. */
#pragma once
#include <cstdint>

namespace tfex::mods {
struct CatalogReplace { const char* id; const char* with; int atlas; };   /* atlas: FortRise's SubtextureAtlasDestination */
struct CatalogMod {
    const char* name; const char* displayName; const char* version;     /* its meta.json (version: what its logic is) */
    const char* const* needs; int needCount;                             /* files its folder must have */
    const char* const* overlay; int overlayCount;                        /* game files its folder supplies while it's loaded */
    const CatalogReplace* replaces; int replaceCount;                    /* game textures its own replace while it's loaded */
    void (*boot)();    /* its start, once its folder's content loaded (FortRise constructing its module) */
    void (*early)();   /* at module start, folder or not (only what must precede the game's own setup; inert without the folder) */
};

/* APIs a mod publishes for other code (FortRise's IModInterop.GetApi<T>(name)): a kind and a struct, registered by the mod's boot, so
 * code never asks for a mod by name or reaches into it. Fixed pool (kMaxApis): a full pool is a logged pools error. */
enum ApiKind : int { kVersusModeApi = 1 };
struct VersusModeApi { int32_t mode; bool ownLevels; };   /* a built-in versus mode slot a mod fills (TowerBall: 6, its own levels) */
constexpr int kMaxApis = 16;
void RegisterApi(const char* mod, int kind, const void* api);
int ApiCount(int kind);
const void* ApiAt(int kind, int i);
const char* ApiMod(int kind, int i);
}  // namespace tfex::mods
