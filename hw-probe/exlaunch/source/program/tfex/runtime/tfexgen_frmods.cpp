/* FortRise content mods on the SD card (OUR OWN CODE): see tfexgen_frmods.hpp. */
#include "tfexgen_frmods.hpp"
#include "../MatchmakingService.hpp"
#include "../gen/tfexgen_GlueFrMods.hpp"
#include "../gen/tfexgen_hooks.hpp"
#include <cmath>
#include "tfexgen_code_rt.hpp"
#include "tfexgen_mods.hpp"
#include "../gen/tfexgen_binding.hpp"
#include "../../hwlog.hpp"
#include "../../pools.hpp"
#include "nn/fs.hpp"
#include <cstdio>
#include <cstring>
#include <cstdlib>

namespace tfex::gen::frmods {
namespace {
char g_listed[kMaxListed][64];
int g_listedCount = 0;
nn::fs::DirectoryEntry g_entries[16];

/* a whole small file into buf (NUL-terminated), its length or -1 */
long ReadAll(const char* path, char* buf, long cap) {
    nn::fs::FileHandle h;
    if (nn::fs::OpenFile(&h, path, nn::fs::OpenMode_Read) != 0) return -1;
    long sz = 0; nn::fs::GetFileSize(&sz, h);
    if (sz >= cap) sz = cap - 1;
    if (sz > 0 && nn::fs::ReadFile(h, 0, buf, (size_t)sz) != 0) sz = -1;
    nn::fs::CloseFile(h);
    if (sz >= 0) buf[sz] = 0;
    return sz;
}

void Path(const void* managed, char* out, int cap) { if (!managed) { out[0] = 0; return; } Utf8Of(managed, out, cap); }
}  // namespace

/* the content mods the registry loaded (runtime/tfexgen_mods.cpp: meta.json, FortRise's dependency rule), in its order */
const mods::Mod* SdMod(int i) {
    for (int k = 0, n = 0; k < mods::Count(); k++) {
        const mods::Mod* m = mods::At(k);
        if (!m->builtin && m->loaded && n++ == i) return m;
    }
    return nullptr;
}
int ModCount() { int n = 0; while (SdMod(n)) n++; return n; }
void* ModFolder(int i) {
    const mods::Mod* m = SdMod(i);
    if (!m) return nullptr;
    char p[160]; int n = std::snprintf(p, sizeof p, "sd:/xplay/mods/%s/", m->folder);
    return ManagedUtf8(p, n);
}
void* ModName(int i) { const mods::Mod* m = SdMod(i); return m ? ManagedUtf8(m->name, (int)std::strlen(m->name)) : nullptr; }
/* any loaded mod's version, built-in ones too (FortRise's IModCollections.GetVersion over Interop.LoadedMods), or null */
void* ModVersion(const void* name) {
    char n[64]; Path(name, n, sizeof n);
    const mods::Mod* m = mods::Loaded(n);
    return m && m->version[0] ? ManagedUtf8(m->version, (int)std::strlen(m->version)) : nullptr;
}

int RegCount() { return mods::Count(); }
bool RegLoaded(int i) { const mods::Mod* m = mods::At(i); return m && m->loaded; }
void* RegText(int i, int field) {
    const mods::Mod* m = mods::At(i);
    if (!m) return nullptr;
    const char* const f[7] = { m->name, m->displayName, m->version, m->author, m->description, m->dll, m->folder };
    const char* v = field >= 0 && field < 7 ? f[field] : "";
    return ManagedUtf8(v, (int)std::strlen(v));
}
int RegDepCount(int i, bool optional) { const mods::Mod* m = mods::At(i); return !m ? 0 : optional ? m->optionalCount : m->depCount; }
void* RegDepText(int i, int k, bool optional, int field) {
    const mods::Mod* m = mods::At(i);
    if (!m || k < 0 || k >= (optional ? m->optionalCount : m->depCount)) return nullptr;
    const mods::Dep& d = optional ? m->optional[k] : m->deps[k];
    const char* v = field == 0 ? d.name : d.version;
    return ManagedUtf8(v, (int)std::strlen(v));
}
int BlackCount() { return mods::BlacklistCount(); }
void* BlackAt(int i) { const char* f = mods::BlacklistAt(i); return f ? ManagedUtf8(f, (int)std::strlen(f)) : nullptr; }
namespace { char g_newBlack[mods::kMaxBlacklist][64]; int g_newCount = 0; }
void BlackClear() { g_newCount = 0; }
void BlackAdd(const void* folder) {
    if (g_newCount >= mods::kMaxBlacklist) { hw::Log("[pools] ERROR mod blacklist full (%d): a folder left out of blacklist.txt", mods::kMaxBlacklist); return; }
    Path(folder, g_newBlack[g_newCount], sizeof g_newBlack[0]);
    g_newCount++;
}
bool BlackSave() {
    const char* names[mods::kMaxBlacklist];
    for (int i = 0; i < g_newCount; i++) names[i] = g_newBlack[i];
    return mods::SaveBlacklist(names, g_newCount);
}
st::Fr_Result_ModuleMetadata_string ParseFolderMeta(const void* dir) {
    void* meta = dir ? tfex::gen::GlueFrMods::FolderMeta(const_cast<void*>(dir)) : nullptr;
    static const char kNone[] = "TOGGLE MODS: a folder the mod registry has no meta.json for";
    return { meta, meta ? nullptr : ManagedUtf8(kNone, (int)sizeof kNone - 1), meta ? 0 : 1 };   /* (FortRise's Response: Ok 0, Error 1) */
}
bool FileExists(const void* path) {
    char p[256]; Path(path, p, sizeof p);
    nn::fs::FileHandle h;
    if (!p[0] || nn::fs::OpenFile(&h, p, nn::fs::OpenMode_Read) != 0) return false;
    nn::fs::CloseFile(h);
    return true;
}

void* ReadText(const void* path) {
    static char buf[64 * 1024];
    char p[256]; Path(path, p, sizeof p);
    long n = ReadAll(p, buf, sizeof buf);
    if (n < 0) return nullptr;
    if (n == (long)sizeof buf - 1) hw::Log("[frmods] %s: longer than %d bytes, cut", p, (int)sizeof buf - 1);
    int skip = n >= 3 && (uint8_t)buf[0] == 0xef && (uint8_t)buf[1] == 0xbb && (uint8_t)buf[2] == 0xbf ? 3 : 0;   /* a UTF-8 BOM: not text */
    return ManagedUtf8(buf + skip, (int)n - skip);
}

int FileSize(const void* path) {
    char p[256]; Path(path, p, sizeof p);
    nn::fs::FileHandle h;
    if (!p[0] || nn::fs::OpenFile(&h, p, nn::fs::OpenMode_Read) != 0) return -1;
    long sz = 0; nn::fs::GetFileSize(&sz, h);
    nn::fs::CloseFile(h);
    return sz > 0x7fffffff ? -1 : (int)sz;
}

void ReadIntoBytes(const void* path, uint8_t* data, int32_t len) {
    char p[256]; Path(path, p, sizeof p);
    nn::fs::FileHandle h;
    if (!data || len <= 0 || nn::fs::OpenFile(&h, p, nn::fs::OpenMode_Read) != 0) return;
    if (nn::fs::ReadFile(h, 0, data, (size_t)len) != 0) hw::Log("[frmods] %s: read failed", p);
    nn::fs::CloseFile(h);
}

int ListFiles(const void* folder) {
    g_listedCount = 0;
    char p[256]; Path(folder, p, sizeof p);
    int len = (int)std::strlen(p);
    if (len > 0 && p[len - 1] == '/') p[len - 1] = 0;   /* nn::fs wants no trailing slash */
    nn::fs::DirectoryHandle d;
    if (nn::fs::OpenDirectory(&d, p, nn::fs::OpenDirectoryMode_File) != 0) return 0;
    bool full = false;
    for (;;) {
        s64 n = 0;
        if (nn::fs::ReadDirectory(&n, g_entries, d, 16) != 0 || n <= 0) break;
        for (s64 i = 0; i < n; i++) {
            if (g_listedCount >= kMaxListed) { full = true; continue; }
            std::snprintf(g_listed[g_listedCount++], sizeof g_listed[0], "%s", g_entries[i].m_Name);
        }
        if (n < 16) break;
    }
    nn::fs::CloseDirectory(d);
    if (full) hw::Log("[frmods] POOL FULL: %s has more than %d files, the rest not loaded", p, kMaxListed);
    return g_listedCount;
}
void* ListedFile(int i) { return i < 0 || i >= g_listedCount ? nullptr : ManagedUtf8(g_listed[i], (int)std::strlen(g_listed[i])); }

void Log(const void* line) {
    char b[256]; Path(line, b, sizeof b);
    hw::Log("%s", b);
}

void* NewMemoryStream(void* managedBytes) {
    if (!managedBytes) return nullptr;
    return Fn<void* (*)(void*, void*)>(bind::m_MemoryStream__ctor_08e7)(nullptr, managedBytes);   /* this = null: allocates */
}

namespace {
struct HairEntry { void* archer; st::Fr_Option_HairInfo hair; };
HairEntry g_hair[kMaxHair];
int g_nHair = 0;
}
st::Fr_Option_HairInfo ExtraHairData(void* archerData) {
    for (int i = 0; archerData && i < g_nHair; i++) if (g_hair[i].archer == archerData) return g_hair[i].hair;
    return {};
}
void SetExtraHairData(void* archerData, const st::Fr_Option_HairInfo& v) {
    if (!archerData) return;
    for (int i = 0; i < g_nHair; i++) if (g_hair[i].archer == archerData) { g_hair[i].hair = v; return; }
    if (!v.HasValue) return;
    if (g_nHair == kMaxHair) { static bool said = false; if (!said) { said = true; hw::Log("[frmods] ERROR: the hair table is full (%d archers): this archer's FortRise hair isn't drawn", kMaxHair); } return; }
    g_hair[g_nHair++] = HairEntry{archerData, v};
    if (g_nHair == 1) pools::Register("FortRise archer hair (ExtraHairData)", [](pools::Use* u) { u->used = g_nHair; u->cap = kMaxHair; });
}
/* The Switch's ArcherData has hair values the PC's hasn't, between Hair (+0x40) and SFXID (+0x5c): Player.Added passes +0x44, +0x4c (the
 * link count: PlayerHair's constructor allocates that many images), +0x48 (a scale) and +0x54 (the y offset) to the Switch's PlayerHair
 * constructor (0xaedb04..0xaedb3c), parsed from <Hair>'s children (Madeline's, archer 9). An ArcherData FortRise makes (patch_ArcherData())
 * starts with archer 4's (ANCIENT EXILE: a plain <Hair>True</Hair>, the game's defaults), so a mod archer with hair gets a working one;
 * zero links threw in the constructor (TFEX-DIFF: Switch-only fields). */
constexpr uint32_t kHairValues = 0x44, kHairValuesEnd = 0x5c;
void* NewArcherData() {
    void* a = Fn<void* (*)(void*)>(game::brute::NewObject)(TypeOf(bind::t_ArcherData));
    void* archers = Fn<void* (*)()>(bind::m_ArcherData_get_Archers)();
    auto list = archers ? GameArr<void*>(archers, bind::m_Array_get_Length) : gen::Arr<void*>{};
    void* plain = list.len > 4 ? list.data[4] : nullptr;
    if (a && plain) {
        std::memcpy(reinterpret_cast<uint8_t*>(a) + kHairValues, reinterpret_cast<uint8_t*>(plain) + kHairValues, kHairValuesEnd - kHairValues);
        static bool said = false;
        if (!said) {
            said = true;
            auto w = reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(plain) + kHairValues);
            hw::Log("[frmods] FortRise's ArcherData: the Switch's hair values from archer 4: %08x %08x %08x %08x %08x %08x", w[0], w[1], w[2], w[3], w[4], w[5]);
        }
    } else if (a) hw::Log("[frmods] ERROR: FortRise's ArcherData made before the game's archers: no Switch hair values (a mod archer's hair would throw)");
    return a;
}
int32_t ObtainMode() {
    /* FortRise's EnumPool counts from the enum's highest value: FortRise's patch_Modes adds Custom = 10 to the game's Modes (the Switch's
     * own end at LevelTest 7, binding.json e_Modes), so the first custom mode is 11, as on the PC (the browser's TF.EX Netplay mode) */
    constexpr int32_t kModesEnd = 10;   /* patch_Modes.Custom */
    static int32_t next = kModesEnd;
    ++next;
    hw::Log("[frmods] custom mode value %d obtained (FortRise's EnumPool over the Modes with its Custom = %d: the PC's numbers)", (int)next, (int)kModesEnd);
    return next;
}
gen::Arr<float> GoalMultiplier() {
    /* the PC's `static readonly float[] GoalMultiplier = { 0.01f, 0.5f, 1f, 2f }`; the Switch's build has no field: its GoalScore reads
     * these four floats from rodata (+0x38e2240, checked: 0.01 0.5 1 2) */
    return gen::Arr<float>{ reinterpret_cast<float*>(gen::Base() + 0x38e2240), 4 };
}
int32_t PlayerGoals(void* self, int32_t p2goal, int32_t p3goal, int32_t p4goal) {
    /* the game's private MatchSettings.PlayerGoals through its hooked entry, as FortRise's custom GoalScore calls it on the PC: TF.EX's
     * MatchSettingsPatch postfix (test mode 2, local netplay 10) runs on the result */
    return gen::Fn<int32_t (*)(void*, int32_t, int32_t, int32_t)>(bind::m_MatchSettings_PlayerGoals_cf0f)(self, p2goal, p3goal, p4goal);
}
int32_t VanillaGoalScore(void* self) {
    /* FortRise's orig_get_GoalScore: the game's own GoalScore. The Switch's build inlined PlayerGoals into it (tools/tfex/inlined.py), so
     * TF.EX's PlayerGoals postfix never saw those calls. The PC's vanilla body (its IL): Mode 3 PlayerGoals(5, 4, 3), Modes 4 and 6
     * PlayerGoals(5, 8, 10), then ceil(goals * GoalMultiplier[MatchLength]); the other modes fixed numbers. Here: the Switch's own result,
     * unless the patched PlayerGoals differs from the unpatched one, then the PC's formula over the patched goals */
    int32_t r = tfex::gen::hooks::Orig_MatchSettings_get_GoalScore(self);
    int32_t mode = Fld<int32_t>(self, bind::f_MatchSettings_Mode), a, b, c;
    if (mode == 3) { a = 5; b = 4; c = 3; }
    else if (mode == 4 || mode == 6) { a = 5; b = 8; c = 10; }
    else return r;
    int32_t g = PlayerGoals(self, a, b, c);
    if (g == tfex::gen::hooks::Orig_MatchSettings_PlayerGoals(self, a, b, c)) return r;
    int32_t len = Fld<int32_t>(self, bind::f_MatchSettings_MatchLength);
    auto m = GoalMultiplier();
    if (len < 0 || len >= m.len) return r;
    int32_t v = (int32_t)std::ceil((double)((float)g * m.data[len]));
    static int logged = 0;
    if (logged < 4) { logged++; hw::Log("[frmods] GoalScore: TF.EX's PlayerGoals postfix at the inlined call (mode %d): goals %d -> %d (was %d)", (int)mode, (int)g, (int)v, (int)r); }
    return v;
}
namespace {
constexpr int kModSettingsMax = 16;   /* code mods with settings; more: the rest keep their defaults (logged as a full pool) */
constexpr long kModSettingsFileSize = 1024;   /* as TF.EX.settings.json: the JSON, then spaces (no delete / resize on the SD card) */
struct ModSettingsEntry { void* mod; void** root; char name[48]; SettingsLoad load; SettingsSave save; };
ModSettingsEntry g_modSettings[kModSettingsMax];
int g_modSettingsCount = 0;
ModSettingsEntry* FindModSettings(void* mod) {
    for (int i = 0; i < g_modSettingsCount; i++) if (g_modSettings[i].mod == mod) return &g_modSettings[i];
    return nullptr;
}
void ModSettingsPath(const ModSettingsEntry& e, char* out, int cap) { std::snprintf(out, cap, "sd:/xplay/%s.settings.json", e.name); }
}  // namespace
void AddModSettings(void* mod, void* settings, const char* name, SettingsLoad load, SettingsSave save) {
    if (!mod || !settings) return;
    if (g_modSettingsCount == kModSettingsMax) { hw::Log("[pools] EXHAUSTED mod settings (capacity %d): %s keeps its defaults", kModSettingsMax, name); return; }
    ModSettingsEntry& e = g_modSettings[g_modSettingsCount++];
    e.mod = mod; e.root = nullptr; RootStatic(e.root) = settings; e.load = load; e.save = save;
    std::snprintf(e.name, sizeof e.name, "%s", name);
    char path[96]; ModSettingsPath(e, path, sizeof path);
    static char text[kModSettingsFileSize + 1];
    long n = 0;
    nn::fs::FileHandle h;
    if (hw::MountSd() && nn::fs::OpenFile(&h, path, nn::fs::OpenMode_Read) == 0) {
        nn::fs::GetFileSize(&n, h);
        if (n > kModSettingsFileSize) n = kModSettingsFileSize;
        nn::fs::ReadFile(h, 0, text, n);
        nn::fs::CloseFile(h);
    }
    while (n > 0 && text[n - 1] == ' ') n--;
    text[n] = 0;
    bool read = n > 0 && load(settings, text, (int)n);
    hw::Log("[frmods] %s: settings %s (%s)", name, read ? "read from its file" : n > 0 ? "file not JSON: its defaults" : "no file: its defaults", path);
}
void* ModSettings(void* mod) {
    if (ModSettingsEntry* e = FindModSettings(mod)) return *e->root;
    char nm[16] = ""; Utf8Of(GlueFrMods::ContentName(mod), nm, sizeof nm);
    return std::strcmp(nm, "TF.EX") == 0 ? ::tfex::NetplaySettingsFile::Object() : nullptr;   /* TF.EX's NetplaySettings; a mod without settings: none */
}
void SaveModSettings(void* mod) {
    ModSettingsEntry* e = FindModSettings(mod);
    if (!e) { ::tfex::NetplaySettingsFile::StoreObject(); return; }   /* TF.EX's (its own file code) */
    static char buf[kModSettingsFileSize];
    int n = e->save(*e->root, buf, sizeof buf);
    char path[96]; ModSettingsPath(*e, path, sizeof path);
    if (n < 0 || !hw::MountSd()) { hw::Log("[frmods] %s: settings not saved (%s)", e->name, n < 0 ? "more than 1 KB" : "no SD card"); return; }
    for (int i = n; i < (int)sizeof buf; i++) buf[i] = ' ';
    nn::fs::CreateFile(path, kModSettingsFileSize);   /* fails if it exists: fine */
    nn::fs::FileHandle h;
    bool ok = nn::fs::OpenFile(&h, path, nn::fs::OpenMode_Write) == 0;
    if (ok) { ok = nn::fs::WriteFile(h, 0, buf, sizeof buf, nn::fs::WriteOption::CreateOption(0)) == 0; if (ok) nn::fs::FlushFile(h); nn::fs::CloseFile(h); }
    hw::Log("[frmods] %s: settings %s %s", e->name, ok ? "saved to" : "NOT saved to", path);
}
extern "C" int stb_vorbis_decode_memory(const unsigned char* mem, int len, int* channels, int* sample_rate, short** output);

Arr<uint8_t> OggToWavBytes(const uint8_t* ogg, int32_t len) {
    int channels = 0, rate = 0;
    short* pcm = nullptr;
    int frames = ogg && len > 0 ? stb_vorbis_decode_memory(ogg, len, &channels, &rate, &pcm) : -1;
    if (frames <= 0 || !pcm || channels < 1 || channels > 2) {
        hw::Log("[frmods] ERROR: music: an OGG file didn't decode (%d bytes: %d frames, %d channels)", (int)len, frames, channels);
        std::free(pcm);
        return NewArr<uint8_t>(bind::rt_ByteArray, 0);
    }
    uint32_t data = (uint32_t)frames * (uint32_t)channels * 2;
    auto wav = NewArr<uint8_t>(bind::rt_ByteArray, (int32_t)(44 + data));
    if (!wav.data) { hw::Log("[frmods] ERROR: music: no memory for a %u-byte track", (unsigned)data); std::free(pcm); return wav; }
    auto put32 = [&](int at, uint32_t v) { for (int i = 0; i < 4; i++) wav.data[at + i] = (uint8_t)(v >> (8 * i)); };
    auto put16 = [&](int at, uint16_t v) { wav.data[at] = (uint8_t)v; wav.data[at + 1] = (uint8_t)(v >> 8); };
    std::memcpy(wav.data, "RIFF", 4); put32(4, 36 + data); std::memcpy(wav.data + 8, "WAVEfmt ", 8);
    put32(16, 16); put16(20, 1); put16(22, (uint16_t)channels); put32(24, (uint32_t)rate);
    put32(28, (uint32_t)rate * channels * 2); put16(32, (uint16_t)(channels * 2)); put16(34, 16);
    std::memcpy(wav.data + 36, "data", 4); put32(40, data);
    std::memcpy(wav.data + 44, pcm, data);
    std::free(pcm);
    hw::Log("[frmods] music: an OGG track decoded (%d Hz, %d channels, %.1f s)", rate, channels, (double)frames / rate);
    return wav;
}
}  // namespace tfex::gen::frmods
