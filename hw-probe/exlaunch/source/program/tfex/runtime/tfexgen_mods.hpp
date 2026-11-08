/* The Switch's mod registry (OUR OWN CODE, hand runtime). TFEX-DIFF S-native: the Switch has no FortRise mod loader (no .NET runtime: code
 * mods can't load), so this stands in for FortRise's ModuleManager: one list of every mod, with FortRise's ModuleMetadata (meta.json:
 * name, displayName, version, author, description, dll, dependencies, optionalDependencies), read once at boot.
 *  - built into the module: gen/tfexgen_mods_builtin.hpp (translate.toml [mods]): TF.EX's mods with the meta.json TF.EX ships, FortRise's
 *    own entry, and the ones the Switch carries its own way (WiderSet);
 *  - content mods on the SD card: sd:/xplay/mods/<folder>/meta.json (a mod's zip extracted as is; glue/fortrise/SwitchMods.cs loads
 *    their archers, sprites and textures).
 * Dependencies are checked as FortRise does: a content mod whose required dependency is missing or older isn't loaded, and keeps the
 * reason (the MODS menu shows it); a dependent of a mod that didn't load doesn't load either. A built-in mod always loads (it is
 * compiled in); its unmet dependencies are listed as a note (TF.EX needs TF.Replay, not on the Switch yet).
 * Catalog mods (gen: kCatalog, translate.toml [mods.catalog]; docs/PLAN-TFNX-2026-11-04.md section 3): a folder whose code mod's logic is
 * compiled in. It loads when the folder has every file the logic needs, whatever its meta.json version (a different one is a notice);
 * a missing file keeps it out. Its entry carries the logic's version (what plays, so what TF.EX's lobby list shows).
 * Fixed-size: kMaxMods mods, kMaxDeps per list; running out is logged as an ERROR. */
#pragma once

namespace tfex::mods {
constexpr int kMaxMods = 64;   /* 32 ran to 30 used on the console (2026-11-05: 19 SSB archer packs) */
constexpr int kMaxDeps = 8;

struct Dep { char name[64]; char version[32]; };
struct Mod {
    char name[64], displayName[64], version[32], author[48], description[256], dll[48];
    Dep deps[kMaxDeps]; int depCount;
    Dep optional[kMaxDeps]; int optionalCount;
    int priority;          /* meta.json "priority", FortRise's ModulePriority: 0 Lowest, 1 Low, 2 Normal (the default), 3 High, 4 Highest */
    bool builtin;          /* compiled into the module (else a content mod on the SD card) */
    char folder[64];       /* a content mod's folder under sd:/xplay/mods */
    char source[96];       /* where the metadata came from */
    bool loaded;           /* FortRise would have loaded it (built-in: always) */
    bool off;              /* its folder is in blacklist.txt (FortRise's TOGGLE MODS): listed, not loaded */
    bool catalog;          /* a folder whose logic is compiled in (translate.toml [mods.catalog]): `version` is the logic's */
    char folderVersion[32];/* a catalog mod's folder's own meta.json version */
    char missing[96];      /* a catalog mod's first needed file its folder lacks (it doesn't load) */
    char note[160];        /* why it didn't load, or a built-in mod's unmet dependencies */
};

void Load();                          /* once, at boot (FrModsBoot, before the content mods load); idempotent */
int Count();
const Mod* At(int i);
const Mod* Find(const char* name);    /* by name, ignoring case (TF.EX's ModCollections compares OrdinalIgnoreCase) */
const Mod* Loaded(const char* name);  /* Find, if it loaded */
/* FortRise's SemanticVersion order: major.minor.patch, a pre-release ("-beta.4") before its release, pre-release parts compared in
 * turn (numbers as numbers); < 0, 0, > 0 */
int CompareVersions(const char* a, const char* b);
/* FortRise's blacklist (RiseCore.ReadBlacklistedMods / WriteBlacklist: Mods/blacklist.txt, a JSON array of folder names): sd:/xplay/mods/
 * blacklist.txt, read once with the registry; a folder in it is listed but not loaded (FortRise skips it). Written by TOGGLE MODS for the
 * next start: Save true when written */
constexpr int kMaxBlacklist = 32;
int BlacklistCount();
const char* BlacklistAt(int i);
bool SaveBlacklist(const char* const* folders, int n);
}  // namespace tfex::mods
