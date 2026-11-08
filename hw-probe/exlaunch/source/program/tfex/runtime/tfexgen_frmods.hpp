/* FortRise content mods on the SD card (OUR OWN CODE, hand runtime): what translator/glue/fortrise/SwitchMods.cs's [Cpp] externs call.
 * A mod is a folder sd:/xplay/mods/<folder>/ with a meta.json (FortRise's mod layout, the zip a PC player installs, extracted as is).
 * Fixed-size: kMaxListed names per directory listing (running out is logged, never silent). */
#pragma once
#include "tfexgen_code_rt.hpp"
#include "../gen/tfexgen_structs.hpp"
#include <cstdint>

namespace tfex::gen::frmods {
constexpr int kMaxListed = 256;

void ReadIntoBytes(const void* path, uint8_t* data, int32_t len);
int ModCount();                       /* the mod folders found (listed once, on the first call) */
void* ModFolder(int i);               /* "sd:/xplay/mods/<folder>/" (a managed string) */
void* ModName(int i);
void* ModVersion(const void* name);   /* a loaded mod's meta.json "version" by its name (FortRise's IModCollections.GetVersion), or null */                 /* meta.json's "name" (FortRise's ModuleMetadata.Name), else the folder's name */
/* the registry (tfexgen_mods.hpp) for the glue's LoadedMods: field 0 name, 1 displayName, 2 version, 3 author, 4 description, 5 dll,
 * 6 folder (a content mod's, "" built-in); a dependency's field 0 name, 1 version (managed strings) */
int RegCount();
bool RegLoaded(int i);
void* RegText(int i, int field);
int RegDepCount(int i, bool optional);
void* RegDepText(int i, int k, bool optional, int field);
/* FortRise's blacklist (TOGGLE MODS, tfexgen_mods.hpp): the folders read at start; a new list made with BlackClear / BlackAdd, BlackSave
 * writes it for the next start (true when written) */
int BlackCount();
void* BlackAt(int i);
void BlackClear();
void BlackAdd(const void* folder);
bool BlackSave();
/* FortRise's ModuleMetadata.ParseMetadata(dir, metaPath) for TOGGLE MODS: Ok with the registry's entry for that folder (glue FolderMeta),
 * else an Error */
st::Fr_Result_ModuleMetadata_string ParseFolderMeta(const void* dir);
/* FortRise's Result<T, U>.Check(out ok, out error): ok and error out, true for Response.Ok (0) */
template <class R, class T, class U> bool ResultCheck(const R& r, T& ok, U& error) { ok = r.Value; error = r.ErrorValue; return r.ResponseType == 0; }
bool FileExists(const void* path);    /* a file at this managed "sd:/..." path */
void* ReadText(const void* path);     /* the file as a managed string (UTF-8, a BOM dropped), or null */
int FileSize(const void* path);       /* the file's size in bytes, or -1 */
template <class A> void ReadInto(const void* path, const A& into) { ReadIntoBytes(path, into.data, into.len); }
int ListFiles(const void* folder);    /* the files in a folder (names only; ListedFile(i) until the next ListFiles) */
void* ListedFile(int i);
void Log(const void* line);           /* into the console log (hwlog) */
void* NewMemoryStream(void* managedBytes);   /* the game's MemoryStream(byte[]) (translate.toml [pin]) over a managed byte[] */
template <class A> void* ManagedStream(const A& bytes) { return NewMemoryStream(bytes.obj); }
void* NewArcherData();
/* A mod's OGG music (FortRise's OggAudioTrack's files) decoded whole into a WAV file's bytes (16-bit PCM, its rate and channels) for the
 * game's SoundEffect.FromStream (glue SwitchMods.LoadTrack, TFEX-DIFF S-native: FortRise streams it through FAudio's stb_vorbis; the same
 * decoder here, thirdparty/stb_vorbis). An empty array (logged) if it doesn't decode. */
Arr<uint8_t> OggToWavBytes(const uint8_t* ogg, int32_t len);
template <class A> Arr<uint8_t> OggToWav(const A& ogg) { return OggToWavBytes(ogg.data, ogg.len); }
/* FortRise's patch_ArcherData.ExtraHairData (an archer's HairInfo): the game's ArcherData has no room and the value is too big for the
 * side table, so one entry per ArcherData object here (kMaxHair: archers with FortRise data, registered once at load; full is logged) */
constexpr int kMaxHair = 96;
st::Fr_Option_HairInfo ExtraHairData(void* archerData);
void SetExtraHairData(void* archerData, const st::Fr_Option_HairInfo& v);
int32_t ObtainMode();
gen::Arr<float> GoalMultiplier();
int32_t PlayerGoals(void* self, int32_t p2goal, int32_t p3goal, int32_t p4goal);   /* MatchSettings.PlayerGoals through its hooked entry (TF.EX's postfix) */
int32_t VanillaGoalScore(void* self);   /* FortRise's orig_get_GoalScore: the game's, with TF.EX's PlayerGoals postfix where the Switch inlined it */       /* MatchSettings.GoalMultiplier: the Switch's {0.01, 0.5, 1, 2} in rodata (+0x38e2240) */                 /* FortRise's EnumPool.Obtain<Modes>(): the next custom Modes value, from 11 as on the PC */                /* FortRise's patch_ArcherData(): an ArcherData with nothing parsed (the game's allocator) */
/* FortRise's ModuleSettings for every code mod (docs/PLAN-TFNX-2026-11-04.md section 10 step 3): its CreateSettings object, registered by
 * the mod's generated boot with its generated JSON load / save (tfexgen_modsettings), read from sd:/xplay/<mod>.settings.json; Mod.GetSettings
 * answers it (TF.EX's: its NetplaySettings), Mod.SaveSettings writes it */
using SettingsLoad = bool (*)(void* settings, const char* text, int len);
using SettingsSave = int (*)(void* settings, char* buf, int cap);
void AddModSettings(void* mod, void* settings, const char* name, SettingsLoad load, SettingsSave save);
void* ModSettings(void* mod);
void SaveModSettings(void* mod);
}  // namespace tfex::gen::frmods
