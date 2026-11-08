/* The Switch's mod registry (OUR OWN CODE): see tfexgen_mods.hpp. */
#include "tfexgen_mods.hpp"
#include "../gen/tfexgen_mods_builtin.hpp"
#include "modcatalog.gen.hpp"
#include "../../pools.hpp"
#include "../Json.hpp"
#include "../../hwlog.hpp"
#include "nn/fs.hpp"
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace tfex::mods {
namespace {
constexpr const char* kRoot = "sd:/xplay/mods";
Mod g_mods[kMaxMods];
int g_count = 0;
bool g_loaded = false;
constexpr const char* kBlacklist = "sd:/xplay/mods/blacklist.txt";
char g_black[kMaxBlacklist][64];
int g_blackCount = 0;

bool SameName(const char* a, const char* b) {
    for (; *a && *b; a++, b++) if (std::tolower((unsigned char)*a) != std::tolower((unsigned char)*b)) return false;
    return *a == *b;
}
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
/* a member by its meta.json key, either spelling (FortRise reads meta.json case-insensitively: "name" or "Name") */
const json::Node* Member(const json::Doc& d, const json::Node* obj, const char* key) {
    if (const json::Node* n = d.Get(obj, key)) return n;
    char k[32]; std::snprintf(k, sizeof k, "%s", key); k[0] = (char)std::toupper((unsigned char)k[0]);
    return d.Get(obj, k);
}
void Str(const json::Doc& d, const json::Node* obj, const char* key, char* out, int cap) {
    out[0] = 0;
    const json::Node* n = Member(d, obj, key);
    if (n && n->type == json::Type::String) json::Unescape(n->str, n->strLen, out, cap);
}
int Deps(const json::Doc& d, const json::Node* obj, const char* key, Dep* out, const char* mod) {
    const json::Node* arr = Member(d, obj, key);
    int n = 0;
    for (const json::Node* c = d.Child(arr); c; c = d.Next(c)) {
        if (n >= kMaxDeps) { hw::Log("[mods] ERROR: %s has more than %d %s (the rest not checked)", mod, kMaxDeps, key); break; }
        Str(d, c, "name", out[n].name, sizeof out[n].name);
        Str(d, c, "version", out[n].version, sizeof out[n].version);
        if (out[n].name[0]) n++;
    }
    return n;
}
/* a folder mod whose logic TF.NX carries (kCatalog): the logic's version in, every needed file checked */
void CheckCatalog(Mod& m) {
    for (int c = 0; c < kCatalogCount; c++) {
        const CatalogMod& k = kCatalog[c];
        if (!k.name || !SameName(k.name, m.name)) continue;
        m.catalog = true;
        std::snprintf(m.folderVersion, sizeof m.folderVersion, "%s", m.version);
        std::snprintf(m.version, sizeof m.version, "%s", k.version);
        for (int i = 0; i < k.needCount; i++) {
            char path[192]; std::snprintf(path, sizeof path, "%s/%s/%s", kRoot, m.folder, k.needs[i]);
            nn::fs::FileHandle h;
            if (nn::fs::OpenFile(&h, path, nn::fs::OpenMode_Read) != 0) { std::snprintf(m.missing, sizeof m.missing, "%s", k.needs[i]); break; }
            nn::fs::CloseFile(h);
        }
        if (m.missing[0]) hw::Log("[mods] %s: its folder %s has no %s (TF.NX's %s logic needs it): not loaded", m.displayName, m.folder, m.missing, k.version);
        else if (CompareVersions(m.folderVersion, k.version) != 0)
            hw::Log("[mods] %s: folder %s, TF.NX runs %s's logic (%d needed files there)", m.displayName, m.folderVersion, k.version, k.needCount);
        else hw::Log("[mods] %s %s: TF.NX's logic, its folder has the %d needed files", m.displayName, k.version, k.needCount);
        return;
    }
}
/* one meta.json into the next entry; false when it isn't a FortRise mod's (no name) or the list is full */
bool Add(const char* json, int len, bool builtin, const char* folder, const char* source) {
    if (g_count >= kMaxMods) { pools::Exhausted("mod registry", kMaxMods, source); return false; }   /* (the mod at `source` not registered) */
    static json::Node nodes[256];
    json::Doc d{nodes, 256, 0};
    if (!d.Parse(json, len) || !d.Root()) { hw::Log("[mods] %s: meta.json isn't JSON (or has more than 256 values): skipped", source); return false; }
    Mod& m = g_mods[g_count];
    std::memset(&m, 0, sizeof m);
    Str(d, d.Root(), "name", m.name, sizeof m.name);
    if (!m.name[0]) { hw::Log("[mods] %s: meta.json has no name: skipped", source); return false; }
    Str(d, d.Root(), "displayName", m.displayName, sizeof m.displayName);
    if (!m.displayName[0]) std::snprintf(m.displayName, sizeof m.displayName, "%s", m.name);
    Str(d, d.Root(), "version", m.version, sizeof m.version);
    Str(d, d.Root(), "author", m.author, sizeof m.author);
    Str(d, d.Root(), "description", m.description, sizeof m.description);
    Str(d, d.Root(), "dll", m.dll, sizeof m.dll);
    char prio[16];
    Str(d, d.Root(), "priority", prio, sizeof prio);
    static const char* const kPriorities[] = { "Lowest", "Low", "Normal", "High", "Highest" };
    m.priority = 2;
    for (int p = 0; p < 5; p++) if (SameName(prio, kPriorities[p])) m.priority = p;
    m.depCount = Deps(d, d.Root(), "dependencies", m.deps, m.name);
    m.optionalCount = Deps(d, d.Root(), "optionalDependencies", m.optional, m.name);
    m.builtin = builtin;
    std::snprintf(m.folder, sizeof m.folder, "%s", folder ? folder : "");
    std::snprintf(m.source, sizeof m.source, "%s", source);
    for (int i = 0; i < g_count; i++)
        if (SameName(g_mods[i].name, m.name)) { hw::Log("[mods] %s: a second %s (the first, from %s, stays)", source, m.name, g_mods[i].source); return false; }
    if (!builtin) CheckCatalog(m);
    m.off = false;
    for (int i = 0; i < g_blackCount && !builtin; i++) if (SameName(g_black[i], m.folder)) m.off = true;
    g_count++;
    return true;
}
/* FortRise's RiseCore.ReadBlacklistedMods: the JSON array of folder names; none when the file is missing or isn't one */
void ReadBlacklist() {
    static char text[2048];
    long len = ReadAll(kBlacklist, text, sizeof text);
    if (len < 0) return;
    static json::Node nodes[kMaxBlacklist + 8];
    json::Doc d{nodes, kMaxBlacklist + 8, 0};
    if (!d.Parse(text, (int)len) || !d.Root() || d.Root()->type != json::Type::Array) { hw::Log("[mods] %s isn't a JSON array of folder names (or has more than %d): no mod turned off", kBlacklist, kMaxBlacklist); return; }
    for (const json::Node* n = d.Child(d.Root()); n; n = d.Next(n)) {
        if (n->type != json::Type::String) continue;
        if (g_blackCount >= kMaxBlacklist) { hw::Log("[pools] ERROR mod blacklist full (%d): the rest of %s ignored", kMaxBlacklist, kBlacklist); break; }
        json::Unescape(n->str, n->strLen, g_black[g_blackCount], sizeof g_black[0]);
        g_blackCount++;
    }
    hw::Log("[mods] %s: %d folder(s) turned off (FortRise's blacklist)", kBlacklist, g_blackCount);
}
void ListSd() {
    /* the SD card mounted first (hwlog's MountSd: once, shared): the loader thread can get here before anything else mounted it, and
     * nn::fs aborts on an unmounted path (ResultFsNotMounted, 2026-10-30) */
    if (!hw::MountSd()) { hw::Log("[mods] the SD card couldn't be mounted: no content mods"); return; }
    nn::fs::DirectoryHandle dir;
    if (nn::fs::OpenDirectory(&dir, kRoot, nn::fs::OpenDirectoryMode_Directory) != 0) { hw::Log("[mods] no %s: no content mods", kRoot); return; }
    static nn::fs::DirectoryEntry entries[16];
    static char meta[8192];
    for (;;) {
        s64 n = 0;
        if (nn::fs::ReadDirectory(&n, entries, dir, 16) != 0 || n <= 0) break;
        for (s64 i = 0; i < n; i++) {
            const char* e = entries[i].m_Name;
            if (e[0] == '.') continue;
            char path[160]; std::snprintf(path, sizeof path, "%s/%s/meta.json", kRoot, e);
            long len = ReadAll(path, meta, sizeof meta);
            if (len < 0) { hw::Log("[mods] %s/%s: no meta.json: not a mod", kRoot, e); continue; }
            Add(meta, (int)len, false, e, path);
        }
        if (n < 16) break;
    }
    nn::fs::CloseDirectory(dir);
}
/* FortRise's dependency rule: every required dependency loaded, at that version or newer. Content mods failing it don't load, and
 * neither do their dependents (repeated until nothing changes); built-in mods load anyway, their unmet ones noted */
void Resolve() {
    for (int i = 0; i < g_count; i++) {
        Mod& m = g_mods[i];
        m.loaded = !m.missing[0] && !m.off;
        if (m.off) std::snprintf(m.note, sizeof m.note, "turned off (TOGGLE MODS: %s)", kBlacklist);
        else if (m.missing[0]) std::snprintf(m.note, sizeof m.note, "its folder has no %s (TF.NX %s needs it)", m.missing, m.version);
    }
    for (bool changed = true; changed;) {
        changed = false;
        for (int i = 0; i < g_count; i++) {
            Mod& m = g_mods[i];
            if (!m.loaded && !m.builtin) continue;
            m.note[0] = 0;
            if (m.catalog && CompareVersions(m.folderVersion, m.version) != 0)
                std::snprintf(m.note, sizeof m.note, "folder %s, TF.NX runs %s", m.folderVersion, m.version);
            for (int k = 0; k < m.depCount; k++) {
                const Dep& dp = m.deps[k];
                const Mod* have = Loaded(dp.name);
                const char* why = !have ? "not on the Switch" : (dp.version[0] && CompareVersions(have->version, dp.version) < 0) ? "older" : nullptr;
                if (!why) continue;
                int w = (int)std::strlen(m.note);
                std::snprintf(m.note + w, sizeof m.note - w, "%sneeds %s %s (%s%s%s)", w ? "; " : "", dp.name, dp.version, why,
                              have ? ": " : "", have ? have->version : "");
                if (!m.builtin && m.loaded) { m.loaded = false; changed = true; }
            }
        }
    }
}

/* "1.2.3-beta.4" -> numbers and the pre-release text */
void SplitVersion(const char* v, long num[3], const char** pre) {
    num[0] = num[1] = num[2] = 0; *pre = nullptr;
    const char* p = v ? v : "";
    for (int i = 0; i < 3 && *p; i++) {
        num[i] = std::strtol(p, const_cast<char**>(&p), 10);
        if (*p == '.') p++; else break;
    }
    if (*p == '-') *pre = p + 1;
}
}  // namespace

int CompareVersions(const char* a, const char* b) {
    long na[3], nb[3]; const char *pa, *pb;
    SplitVersion(a, na, &pa); SplitVersion(b, nb, &pb);
    for (int i = 0; i < 3; i++) if (na[i] != nb[i]) return na[i] < nb[i] ? -1 : 1;
    if (!pa || !pb) return pa ? -1 : pb ? 1 : 0;   /* a pre-release comes before its release */
    while (*pa || *pb) {   /* dot-separated parts: numbers numerically, below text, text in ASCII order */
        char xa[24] = "", xb[24] = ""; int la = 0, lb = 0;
        while (*pa && *pa != '.' && la < 23) xa[la++] = *pa++;
        while (*pb && *pb != '.' && lb < 23) xb[lb++] = *pb++;
        if (*pa == '.') pa++;
        if (*pb == '.') pb++;
        if (!la || !lb) return la ? 1 : lb ? -1 : 0;
        bool da = std::isdigit((unsigned char)xa[0]), db = std::isdigit((unsigned char)xb[0]);
        if (da && db) { long ia = std::atol(xa), ib = std::atol(xb); if (ia != ib) return ia < ib ? -1 : 1; }
        else if (da != db) return da ? -1 : 1;
        else if (int c = std::strcmp(xa, xb)) return c < 0 ? -1 : 1;
    }
    return 0;
}

void Load() {
    if (g_loaded) return;
    g_loaded = true;
    for (int i = 0; i < kBuiltinCount; i++) Add(kBuiltin[i].json, (int)std::strlen(kBuiltin[i].json), true, nullptr, kBuiltin[i].source);
    ReadBlacklist();
    ListSd();
    Resolve();
    int loaded = 0;
    for (int i = 0; i < g_count; i++) loaded += g_mods[i].loaded;
    hw::Log("[mods] %d mods, %d loaded (%d of %d entries used):", g_count, loaded, g_count, kMaxMods);
    pools::Register("mod registry", [](pools::Use* u) { u->used = g_count; u->cap = kMaxMods; });
    for (int i = 0; i < g_count; i++) {
        const Mod& m = g_mods[i];
        hw::Log("[mods]   %s %s (%s, %s)%s%s", m.name, m.version, m.builtin ? "built in" : m.folder, m.loaded ? "loaded" : "NOT loaded",
                m.note[0] ? ": " : "", m.note);
    }
}
int Count() { Load(); return g_count; }
int BlacklistCount() { Load(); return g_blackCount; }
const char* BlacklistAt(int i) { Load(); return i >= 0 && i < g_blackCount ? g_black[i] : nullptr; }
/* FortRise's RiseCore.WriteBlacklist: the folder names as a JSON array. Written over the old file in place (no SetFileSize here): a
 * shorter text is followed by spaces, which JSON ignores */
bool SaveBlacklist(const char* const* folders, int n) {
    static char text[2048];
    json::Writer w; w.Init(text, sizeof text);
    w.BeginArray();
    for (int i = 0; i < n; i++) w.Str(folders[i]);
    w.EndArray();
    if (!w.ok) { hw::Log("[mods] ERROR: the blacklist (%d folders) doesn't fit %d bytes: not written", n, (int)sizeof text); return false; }
    if (!hw::MountSd()) return false;
    nn::fs::CreateFile(kBlacklist, 0);   /* (fails if it's there: fine) */
    nn::fs::FileHandle h;
    if (nn::fs::OpenFile(&h, kBlacklist, nn::fs::OpenMode_Write | nn::fs::OpenMode_Append) != 0) { hw::Log("[mods] ERROR: %s can't be opened for writing", kBlacklist); return false; }
    long old = 0; nn::fs::GetFileSize(&old, h);
    bool ok = nn::fs::WriteFile(h, 0, text, (u64)w.len, nn::fs::WriteOption::CreateOption(0)) == 0;
    static const char pad[64] = "                                                               ";
    for (long at = w.len; ok && at < old; at += 63) ok = nn::fs::WriteFile(h, at, pad, (u64)(old - at < 63 ? old - at : 63), nn::fs::WriteOption::CreateOption(0)) == 0;
    if (ok) nn::fs::FlushFile(h);
    nn::fs::CloseFile(h);
    hw::Log("[mods] %s %s: %d folder(s) off for the next start", kBlacklist, ok ? "written" : "NOT written (a write failed)", n);
    return ok;
}
const Mod* At(int i) { Load(); return i >= 0 && i < g_count ? &g_mods[i] : nullptr; }
const Mod* Find(const char* name) {
    Load();
    for (int i = 0; name && i < g_count; i++) if (SameName(g_mods[i].name, name)) return &g_mods[i];
    return nullptr;
}
const Mod* Loaded(const char* name) { const Mod* m = Find(name); return m && m->loaded ? m : nullptr; }

/* The mods' published APIs (modcatalog.hpp: FortRise's IModInterop.GetApi, by kind) */
namespace {
struct Api { const char* mod; int kind; const void* api; };
Api g_apis[kMaxApis];
int g_apiCount = 0;
}
void RegisterApi(const char* mod, int kind, const void* api) {
    if (g_apiCount == 0) pools::Register("mod APIs", [](pools::Use* u) { u->used = g_apiCount; u->cap = kMaxApis; });
    if (g_apiCount == kMaxApis) { pools::Exhausted("mod APIs", kMaxApis, mod); return; }
    g_apis[g_apiCount++] = Api{mod, kind, api};
    hw::Log("[mods] %s publishes API kind %d (%d of %d)", mod, kind, g_apiCount, kMaxApis);
}
int ApiCount(int kind) { int n = 0; for (int i = 0; i < g_apiCount; i++) n += g_apis[i].kind == kind; return n; }
const void* ApiAt(int kind, int i) {
    for (int k = 0; k < g_apiCount; k++) if (g_apis[k].kind == kind && i-- == 0) return g_apis[k].api;
    return nullptr;
}
const char* ApiMod(int kind, int i) {
    for (int k = 0; k < g_apiCount; k++) if (g_apis[k].kind == kind && i-- == 0) return g_apis[k].mod;
    return nullptr;
}
}  // namespace tfex::mods
