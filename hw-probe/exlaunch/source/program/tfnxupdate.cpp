/* TF.NX's updater from its GitHub releases (docs/PLAN-UPDATER-2026-11-07.md). TF.EX's AutoUpdater on the PC, for the Switch's install
 * (TFEX-DIFF S-update, each part with its reason):
 *  - where: TF.NX's releases (github.com/belacmu/tf-nx; hw.txt "updatesource" for the test server), its 4-part version (TF.EX's + TF.NX's
 *    build). The latest from /releases/latest's redirect, as TF.EX; the zip named from the version (TF.NX-v<version>.zip), as TF.EX.
 *  - the download: streamed to sd:/xplay/update/update.zip (no heap for a 3.4 MB file).
 *  - Validate: the zip's xplay/tfnx/meta.json, its version the one announced, its game the one this console runs (a release made for
 *    another TowerFall update would crash at boot: refused with the reason). Space on the SD card checked first.
 *  - apply (FortRise's ModUpdater isn't on the Switch): every entry under the release's own folders unpacked to "<path>.dl" and checked
 *    (size, CRC-32) before anything is replaced; then a journal (install.txt) lists the paths, each old file is kept as "<path>.old" and
 *    the new one moved in; the journal goes last. A power cut mid-swap: the next start finishes the swap from the journal (every .dl was
 *    checked already). The module and the mods are read at launch: the running game is unaffected until it is restarted.
 *  - restart: the player restarts the game (the console can't relaunch it for us; the user's choice): the dialog says so, then a notice
 *    stays on the main menu. The first start of the new version says "TF.NX UPDATED TO V..." (and the release's one-line note).
 *  - TF.EX's online gate (TfexStatus): "update available" only when the new release carries a newer TF.EX (the official server refuses an
 *    old TF.EX); a TF.NX-only release never blocks online play. TF.NX's own updates: checked once at boot, AUTO UPDATE starts TF.EX's
 *    dialog on the main menu, else the OPTIONS entry (glue Updates) downloads on Confirm.
 *  - devserver.txt (a developer's console): GitHub updates off, so a dev build is never replaced by a release. */
#include "tfnxupdate.hpp"
#include "netws.hpp"
#include "hwlog.hpp"
#include "devupdate.hpp"
#include "nn/fs.hpp"
#include "tfex/Game.hpp"
#include "tfex/gen/tfexgen_version.hpp"
#include "tfex/runtime/tfexgen_code_rt.hpp"
#include "tfex/runtime/tfexgen_services.hpp"
#include "tfex/gen/tfexgen_NetplayEntry.hpp"
#include "tfex/gen/tfexgen_GlueUpdates.hpp"
#include "tfex/gen/tfexgen_Auto_NetplayPreferences.hpp"
#include "tfex/gen/tfexgen_Notification.hpp"
#include "tfex/NetplayOptions.hpp"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cstdarg>

#ifndef TFNX_BUILD
#define TFNX_BUILD "0"
#endif
extern "C" {
void* tfx_inflate_begin();
int32_t tfx_inflate_feed(void* s, const uint8_t* src, size_t len, uint8_t* out, size_t cap, size_t* used, size_t* wrote);
void tfx_inflate_end(void* s);
}

namespace tfnxupdate {
namespace {
constexpr const char* kVersion = TFEX_VERSION "." TFNX_BUILD;
constexpr const char* kGame = "1.4.1.6";   /* the TowerFall update this module's offsets are for (xplay.cpp) */
constexpr const char* kDefaultSource = "https://github.com/belacmu/tf-nx/releases";
constexpr const char* kDir = "sd:/xplay/update";
constexpr const char* kZip = "sd:/xplay/update/update.zip";
constexpr const char* kJournal = "sd:/xplay/update/install.txt";
constexpr const char* kInstalled = "sd:/xplay/update/installed.txt";
/* the release's own folders: nothing else on the SD card is written (settings, saves, hw.txt, the player's own mods stay) */
const char* const kAllowed[] = { "atmosphere/contents/0100A1C00359C000/exefs/", "xplay/tfex/", "xplay/mods/", "xplay/tfnx/" };
constexpr int64_t kFreshTicks = 19200000LL * 60 * 5;   /* TF.EX's CheckTimestamp: 5 minutes */

char g_source[256] = "";
bool g_off = false;                  /* devserver.txt */
volatile int g_ui = (int)Ui::Checking;
volatile int g_busy = 0;             /* a check or a download holds the network part */
volatile int g_checkReq = 0;
int64_t g_current = 0, g_latest = 0;
char g_latestText[24] = "", g_failure[128] = "";
uint64_t g_lastCheck = 0;            /* system ticks of the last good check (0: none) */
bool g_checked = false;
bool g_autoWanted = false, g_autoDone = false;
char g_justUpdated[24] = "", g_note[96] = "";
int g_restartAt = -1;                /* frames until the dialog is dismissed after "restart the game" */
bool g_test = false;                 /* hw.txt "updatetest" (TEST ONLY): title -> main menu, the dialog as AUTO UPDATE starts it, then OPTIONS */
int g_testFrames = 0, g_testStep = 0, g_testPress = 0;
unsigned long g_testBits = 0;
alignas(0x1000) uint8_t g_stack[0x8000];   /* (nn::os: a thread stack 4 KB-aligned, its size a multiple of 4 KB) */

uint64_t Now() { uint64_t t; __asm__ __volatile__("mrs %0, cntpct_el0" : "=r"(t)); return t; }
bool Parse(const char* s, int64_t* v) {   /* "a.b[.c[.d]]" -> System.Version packed (gen::VersionTryParse's order) */
    int32_t part[4] = { -1, -1, -1, -1 }; int n = 0; int64_t cur = -1;
    for (const char* p = s;; p++) {
        char c = *p;
        if (c >= '0' && c <= '9') { cur = (cur < 0 ? 0 : cur) * 10 + (c - '0'); if (cur > 65534) return false; continue; }
        if (cur < 0 || n >= 4) return false;
        part[n++] = (int32_t)cur; cur = -1;
        if (c != '.') break;
    }
    if (n < 2) return false;
    *v = ((int64_t)part[0] << 48) | ((int64_t)part[1] << 32) | ((int64_t)(part[2] + 1) << 16) | (int64_t)(part[3] + 1);
    return true;
}
int64_t TfexPart(int64_t v) { return v & ~0xffffLL; }   /* major.minor.build: TF.EX's version inside TF.NX's */
void Fail(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void Fail(const char* fmt, ...) {
    va_list a; va_start(a, fmt); std::vsnprintf(g_failure, sizeof g_failure, fmt, a); va_end(a);
    hw::Log("[update] ERROR: %s", g_failure);
}
void Lock() { while (__atomic_exchange_n(&g_busy, 1, __ATOMIC_ACQUIRE)) netws::Sleep(20000000); }
void Unlock() { __atomic_store_n(&g_busy, 0, __ATOMIC_RELEASE); }

/* ---- SD files ---- */
using PathFn = uint32_t (*)(const char*);
using RenameFn = uint32_t (*)(const char*, const char*);
using EntryTypeFn = uint32_t (*)(nn::fs::DirectoryEntryType*, const char*);
using FreeFn = uint32_t (*)(int64_t*, const char*);
PathFn g_delete = nullptr; RenameFn g_rename = nullptr; EntryTypeFn g_entryType = nullptr; FreeFn g_free = nullptr;
void Bind() {
    if (g_delete) return;
    g_delete = reinterpret_cast<PathFn>(netws::SdkExport("_ZN2nn2fs10DeleteFileEPKc"));
    g_rename = reinterpret_cast<RenameFn>(netws::SdkExport("_ZN2nn2fs10RenameFileEPKcS2_"));
    g_entryType = reinterpret_cast<EntryTypeFn>(netws::SdkExport("_ZN2nn2fs12GetEntryTypeEPNS0_18DirectoryEntryTypeEPKc"));
    g_free = reinterpret_cast<FreeFn>(netws::SdkExport("_ZN2nn2fs16GetFreeSpaceSizeEPlPKc"));
    if (!g_delete || !g_rename || !g_entryType) hw::Log("[update] ERROR: nn::fs DeleteFile / RenameFile / GetEntryType not found: updates can't be installed");
}
bool Exists(const char* path) { nn::fs::DirectoryEntryType t; return g_entryType && g_entryType(&t, path) == 0; }
void Delete(const char* path) { if (g_delete && Exists(path)) g_delete(path); }
void MakeParents(const char* path) {   /* "sd:/a/b/c" -> sd:/a, sd:/a/b */
    char p[320];
    std::snprintf(p, sizeof p, "%s", path);
    for (char* s = std::strchr(p + 4, '/'); s; s = std::strchr(s + 1, '/')) { *s = 0; nn::fs::CreateDirectory(p); *s = '/'; }
}
int64_t FreeSpace() { int64_t f = -1; if (g_free && g_free(&f, "sd:/") != 0) f = -1; return f; }
/* a whole small file (journal, meta.json, installed.txt) into `buf`; its length or -1 */
int ReadSmall(const char* path, char* buf, int cap) {
    nn::fs::FileHandle h;
    if (nn::fs::OpenFile(&h, path, nn::fs::OpenMode_Read) != 0) return -1;
    long sz = 0; nn::fs::GetFileSize(&sz, h);
    if (sz >= cap) sz = cap - 1;
    int r = nn::fs::ReadFile(h, 0, buf, (size_t)sz) == 0 ? (int)sz : -1;
    nn::fs::CloseFile(h);
    if (r >= 0) buf[r] = 0;
    return r;
}
bool WriteSmall(const char* path, const char* text) {
    int n = (int)std::strlen(text);
    Delete(path);
    MakeParents(path);
    if (nn::fs::CreateFile(path, n) != 0) return false;
    nn::fs::FileHandle h;
    if (nn::fs::OpenFile(&h, path, nn::fs::OpenMode_Write) != 0) return false;
    bool ok = nn::fs::WriteFile(h, 0, text, (u64)n, nn::fs::WriteOption::CreateOption(0)) == 0;
    nn::fs::FlushFile(h);
    nn::fs::CloseFile(h);
    return ok;
}

/* ---- CRC-32 (zlib's) ---- */
uint32_t g_crcTab[256];
void CrcInit() { if (g_crcTab[1]) return; for (uint32_t i = 0; i < 256; i++) { uint32_t c = i; for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1; g_crcTab[i] = c; } }
uint32_t Crc(uint32_t crc, const uint8_t* p, size_t n) { crc = ~crc; while (n--) crc = g_crcTab[(crc ^ *p++) & 0xff] ^ (crc >> 8); return ~crc; }

/* ---- the check ---- */
char g_loc[2048];
bool CheckLocked() {
    char url[320];
    std::snprintf(url, sizeof url, "%s/latest", g_source);
    int status = netws::HttpRequest("HEAD", url, false, nullptr, nullptr, nullptr, nullptr, g_loc, sizeof g_loc);
    const char* tag = status >= 300 && status < 400 ? std::strstr(g_loc, "/tag/v") : nullptr;
    int64_t v = 0;
    char text[24] = "";
    if (tag) {
        int n = 0;
        for (const char* p = tag + 6; *p && *p != '/' && *p != '?' && n < (int)sizeof text - 1; p++) text[n++] = *p;
        text[n] = 0;
    }
    if (!tag || !Parse(text, &v)) {
        Fail("could not read the latest TF.NX release (status %d%s%.60s)", status, g_loc[0] ? ", location " : "", g_loc);
        return false;
    }
    g_latest = v;
    std::snprintf(g_latestText, sizeof g_latestText, "%s", text);
    g_lastCheck = Now(); g_checked = true; g_failure[0] = 0;
    hw::Log("[update] check: TF.NX %s runs, the latest release is %s (%s%s)", kVersion, text, v > g_current ? "an update" : "up to date",
            v > g_current && TfexPart(v) > TfexPart(g_current) ? ", with a newer TF.EX: online play needs it" : "");
    return true;
}
void Check() {
    if (g_off) return;
    Lock();
    int ui = g_ui;
    if (ui != (int)Ui::Downloading && ui != (int)Ui::Downloaded) g_ui = (int)Ui::Checking;
    bool ok = CheckLocked();
    if (g_ui == (int)Ui::Checking) g_ui = (int)(!ok ? Ui::Failed : g_latest > g_current ? Ui::Available : Ui::UpToDate);
    Unlock();
}

void BootWork();
void Thread(void*) {
    /* the SD card is ready some seconds after launch (exl_main is too early for nn::fs), as devupdate's thread waits */
    netws::Sleep(5000000000LL);
    while (!hw::PreviousLogRead()) netws::Sleep(500000000LL);
    BootWork();
    if (g_off) { for (;;) netws::Sleep(3600000000000LL); }   /* an nn::os thread must never return */
    netws::Sleep(1000000000LL);
    Check();
    if (g_ui == (int)Ui::Available) g_autoWanted = true;
    for (;;) {
        if (__atomic_exchange_n(&g_checkReq, 0, __ATOMIC_ACQUIRE)) Check();
        netws::Sleep(250000000);
    }
}

/* ---- the zip ---- */
struct Entry { char name[200]; uint32_t crc, csize, usize, local; uint16_t method; };
constexpr int kMaxEntries = 128;
Entry g_entries[kMaxEntries];
int g_nEntries = 0;
uint8_t g_io[16384], g_out[32768];
uint16_t U16(const uint8_t* p) { return (uint16_t)(p[0] | p[1] << 8); }
uint32_t U32(const uint8_t* p) { return (uint32_t)(p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24); }

bool ReadDirectory(nn::fs::FileHandle h, long size) {
    static uint8_t tail[65557];
    long from = size > (long)sizeof tail ? size - (long)sizeof tail : 0;
    long n = size - from;
    if (n < 22 || nn::fs::ReadFile(h, from, tail, (size_t)n) != 0) { Fail("the downloaded zip can't be read"); return false; }
    long e = -1;
    for (long i = n - 22; i >= 0; i--) if (U32(tail + i) == 0x06054b50) { e = i; break; }
    if (e < 0) { Fail("the downloaded file is not a zip"); return false; }
    uint32_t count = U16(tail + e + 10), cdSize = U32(tail + e + 12), cdOff = U32(tail + e + 16);
    static uint8_t cd[65536];
    if (cdSize > sizeof cd || count > (uint32_t)kMaxEntries) {
        Fail("the release zip has %u entries / a %u-byte directory: more than the updater holds (%d, %u)", count, cdSize, kMaxEntries, (unsigned)sizeof cd);
        return false;
    }
    if (nn::fs::ReadFile(h, cdOff, cd, cdSize) != 0) { Fail("the zip's directory can't be read"); return false; }
    g_nEntries = 0;
    for (uint32_t off = 0, k = 0; k < count; k++) {
        if (off + 46 > cdSize || U32(cd + off) != 0x02014b50) { Fail("the zip's directory is damaged"); return false; }
        Entry& en = g_entries[g_nEntries++];
        en.method = U16(cd + off + 10); en.crc = U32(cd + off + 16); en.csize = U32(cd + off + 20); en.usize = U32(cd + off + 24);
        uint32_t nl = U16(cd + off + 28), xl = U16(cd + off + 30), cl = U16(cd + off + 32);
        en.local = U32(cd + off + 42);
        if (nl >= sizeof en.name) { Fail("a zip entry's name is too long (%u)", nl); return false; }
        std::memcpy(en.name, cd + off + 46, nl); en.name[nl] = 0;
        off += 46 + nl + xl + cl;
    }
    hw::Log("[update] zip: %d entries (pool %d)", g_nEntries, kMaxEntries);
    return true;
}
/* an entry's data, inflated, to `sink`: false on a damaged entry */
template <class F> bool Extract(nn::fs::FileHandle h, const Entry& en, F sink) {
    uint8_t lh[30];
    if (nn::fs::ReadFile(h, en.local, lh, 30) != 0 || U32(lh) != 0x04034b50) return false;
    long pos = (long)en.local + 30 + U16(lh + 26) + U16(lh + 28);
    long left = en.csize;
    uint32_t crc = 0; uint64_t out = 0;
    if (en.method == 0) {
        while (left > 0) {
            long n = left < (long)sizeof g_io ? left : (long)sizeof g_io;
            if (nn::fs::ReadFile(h, pos, g_io, (size_t)n) != 0 || !sink(g_io, (int)n)) return false;
            crc = Crc(crc, g_io, (size_t)n); out += n; pos += n; left -= n;
        }
    } else if (en.method == 8) {
        void* z = tfx_inflate_begin();
        if (!z) return false;
        bool ok = true, done = false;
        size_t have = 0, at = 0;
        while (ok && !done) {
            if (at == have && left > 0) {
                long n = left < (long)sizeof g_io ? left : (long)sizeof g_io;
                if (nn::fs::ReadFile(h, pos, g_io, (size_t)n) != 0) { ok = false; break; }
                pos += n; left -= n; have = (size_t)n; at = 0;
            }
            size_t used = 0, wrote = 0;
            int32_t r = tfx_inflate_feed(z, g_io + at, have - at, g_out, sizeof g_out, &used, &wrote);
            at += used;
            if (wrote) { if (!sink(g_out, (int)wrote)) { ok = false; break; } crc = Crc(crc, g_out, wrote); out += wrote; }
            if (r < 0) ok = false;
            else if (r == 1) done = true;
            else if (!used && !wrote && at == have && left == 0) ok = false;   /* the data ended before the stream did */
        }
        tfx_inflate_end(z);
        if (!ok || !done) return false;
    } else return false;
    return crc == en.crc && out == en.usize;
}
bool Allowed(const char* name) {
    if (std::strstr(name, "..") || name[0] == '/') return false;
    for (const char* a : kAllowed) if (!std::strncmp(name, a, std::strlen(a))) return true;
    return false;
}
/* "key": "value" in a small JSON text */
bool JsonText(const char* json, const char* key, char* out, int cap) {
    char k[48]; std::snprintf(k, sizeof k, "\"%s\"", key);
    const char* p = std::strstr(json, k);
    if (!p) return false;
    p = std::strchr(p + std::strlen(k), ':');
    if (!p) return false;
    while (*++p == ' ') {}
    if (*p != '"') return false;
    int n = 0;
    for (p++; *p && *p != '"' && n < cap - 1; p++) out[n++] = *p;
    out[n] = 0;
    return true;
}

struct Progress { void* onProgress; int64_t done, total; uint64_t lastTick; };
struct FileSink { nn::fs::FileHandle h; int64_t off; bool ok; Progress* pr; };
void DownloadSink(void* ctx, const uint8_t* p, int n) {
    FileSink* f = static_cast<FileSink*>(ctx);
    if (!f->ok) return;
    if (nn::fs::WriteFile(f->h, f->off, p, (u64)n, nn::fs::WriteOption::CreateOption(0)) != 0) { f->ok = false; return; }
    f->off += n;
    Progress* pr = f->pr;
    pr->done = f->off;
    if (pr->onProgress && Now() - pr->lastTick > 19200000 / 10) {   /* TF.EX reports each buffer; ten times a second is enough here */
        pr->lastTick = Now();
        tfex::gen::DlgInvoke<void, int64_t, int64_t>(pr->onProgress, pr->done, pr->total);
    }
}
void Phase(void* onPhase, const char* text) {
    hw::Log("[update] %s", text);
    if (onPhase) tfex::gen::DlgInvoke<void, void*>(onPhase, tfex::gen::ManagedUtf8(text, (int)std::strlen(text)));
}

/* the journal's swap: each listed path's .dl moved in, the old file kept as .old */
int Swap(char* list) {
    int n = 0;
    for (char* line = std::strtok(list, "\n"); line; line = std::strtok(nullptr, "\n")) {
        if (std::strncmp(line, "sd:/", 4)) continue;   /* (the header line) */
        char dl[330], old[330];
        std::snprintf(dl, sizeof dl, "%s.dl", line);
        std::snprintf(old, sizeof old, "%s.old", line);
        if (!Exists(dl)) continue;   /* already moved in */
        if (Exists(line)) { Delete(old); if (g_rename(line, old) != 0) Delete(line); }
        if (g_rename(dl, line) != 0) { hw::Log("[update] ERROR: %s couldn't be moved into place", dl); continue; }
        n++;
    }
    return n;
}

bool Apply(void* onPhase, void* onProgress) {
    Bind();
    CrcInit();
    if (!g_delete || !g_rename || !g_entryType) { Fail("the SD card's file functions weren't found"); return false; }
    char text[96];
    int64_t free0 = FreeSpace();
    if (free0 >= 0 && free0 < 24LL * 1024 * 1024) { Fail("not enough space on the SD card (%lld MB free, 24 MB needed)", (long long)(free0 >> 20)); return false; }
    std::snprintf(text, sizeof text, "DOWNLOADING V%s", g_latestText);
    Phase(onPhase, text);
    char url[400];
    std::snprintf(url, sizeof url, "%s/download/v%s/TF.NX-v%s.zip", g_source, g_latestText, g_latestText);
    nn::fs::CreateDirectory(kDir);
    Delete(kZip);
    if (nn::fs::CreateFile(kZip, 0) != 0) { Fail("can't create %s", kZip); return false; }
    FileSink f{};
    Progress pr{ onProgress, 0, -1, 0 };
    f.pr = &pr;
    if (nn::fs::OpenFile(&f.h, kZip, nn::fs::OpenMode_Write | nn::fs::OpenMode_Append) != 0) { Fail("can't open %s", kZip); return false; }
    f.ok = true;
    uint64_t t0 = Now();
    int64_t got = 0, len = -1;
    int status = netws::HttpRequest("GET", url, true, DownloadSink, &f, &got, &len, nullptr, 0);
    nn::fs::FlushFile(f.h);
    nn::fs::CloseFile(f.h);
    if (status != 200 || !f.ok) {
        Fail("the download failed (status %d, %lld of %lld bytes%s)", status, (long long)got, (long long)len, f.ok ? "" : ", an SD write failed");
        Delete(kZip);
        return false;
    }
    if (onProgress) tfex::gen::DlgInvoke<void, int64_t, int64_t>(onProgress, got, got);
    hw::Log("[update] downloaded %lld bytes in %.1f s", (long long)got, (double)(Now() - t0) / 19200000.0);

    Phase(onPhase, "APPLYING UPDATE");
    nn::fs::FileHandle h;
    if (nn::fs::OpenFile(&h, kZip, nn::fs::OpenMode_Read) != 0) { Fail("can't read %s", kZip); return false; }
    long zsize = 0; nn::fs::GetFileSize(&zsize, h);
    bool ok = ReadDirectory(h, zsize);
    /* Validate: meta.json's version is the one announced, its game the one this console runs */
    static char meta[4096]; int metaLen = 0;
    const Entry* me = nullptr;
    for (int i = 0; ok && i < g_nEntries; i++) if (!std::strcmp(g_entries[i].name, "xplay/tfnx/meta.json")) me = &g_entries[i];
    if (ok && (!me || me->usize >= sizeof meta)) { Fail("the release zip has no xplay/tfnx/meta.json"); ok = false; }
    if (ok) {
        ok = Extract(h, *me, [&](const uint8_t* p, int n) { std::memcpy(meta + metaLen, p, (size_t)n); metaLen += n; return true; });
        meta[metaLen] = 0;
        if (!ok) Fail("the release's meta.json is damaged");
    }
    char version[24] = "", game[24] = "";
    if (ok) {
        JsonText(meta, "version", version, sizeof version);
        JsonText(meta, "game", game, sizeof game);
        JsonText(meta, "notes", g_note, sizeof g_note);
        if (std::strcmp(version, g_latestText)) { Fail("the downloaded release holds %s instead of %s", version, g_latestText); ok = false; }
        else if (game[0] && std::strcmp(game, kGame)) { Fail("TF.NX %s needs TowerFall %s, this console has %s: update the game first", version, game, kGame); ok = false; }
    }
    uint64_t need = 0;
    for (int i = 0; ok && i < g_nEntries; i++) need += g_entries[i].usize;
    int64_t free1 = FreeSpace();
    if (ok && free1 >= 0 && free1 < (int64_t)need + 4 * 1024 * 1024) { Fail("not enough space on the SD card to unpack (%lld MB free, %llu MB needed)", (long long)(free1 >> 20), (unsigned long long)(need >> 20) + 4); ok = false; }
    /* unpack every file under the release's folders to <path>.dl, checked */
    static char journal[32768];
    int jl = ok ? std::snprintf(journal, sizeof journal, "TFNX-INSTALL %s\n", version) : 0;
    int files = 0, skipped = 0;
    for (int i = 0; ok && i < g_nEntries; i++) {
        const Entry& en = g_entries[i];
        size_t nl = std::strlen(en.name);
        if (!nl || en.name[nl - 1] == '/') continue;   /* a folder */
        if (!Allowed(en.name)) { skipped++; continue; }   /* the README / NOTICE at the zip's root: the SD card's root isn't ours */
        char dst[300], dl[310];
        std::snprintf(dst, sizeof dst, "sd:/%s", en.name);
        std::snprintf(dl, sizeof dl, "%s.dl", dst);
        Delete(dl);
        MakeParents(dl);
        nn::fs::FileHandle o;
        if (nn::fs::CreateFile(dl, en.usize) != 0 || nn::fs::OpenFile(&o, dl, nn::fs::OpenMode_Write) != 0) { Fail("can't create %s", dl); ok = false; break; }
        int64_t off = 0;
        bool wok = true;
        bool eok = Extract(h, en, [&](const uint8_t* p, int n) {
            if (nn::fs::WriteFile(o, off, p, (u64)n, nn::fs::WriteOption::CreateOption(0)) != 0) { wok = false; return false; }
            off += n; return true;
        });
        nn::fs::FlushFile(o);
        nn::fs::CloseFile(o);
        if (!eok || !wok) { Fail("%s: %s", en.name, wok ? "damaged in the zip (size or CRC-32)" : "an SD write failed"); ok = false; break; }
        int w = std::snprintf(journal + jl, sizeof journal - (size_t)jl, "%s\n", dst);
        if (w >= (int)sizeof journal - jl) { Fail("the install list is longer than %u bytes", (unsigned)sizeof journal); ok = false; break; }
        jl += w; files++;
    }
    nn::fs::CloseFile(h);
    if (!ok) {   /* nothing was replaced: the .dl files go */
        for (int i = 0; i < g_nEntries; i++) { char dl[310]; std::snprintf(dl, sizeof dl, "sd:/%s.dl", g_entries[i].name); Delete(dl); }
        Delete(kZip);
        return false;
    }
    /* the swap, from a journal a power cut can be finished from */
    if (!WriteSmall(kJournal, journal)) { Fail("can't write %s", kJournal); return false; }
    static char list[32768];
    std::memcpy(list, journal, (size_t)jl + 1);
    int moved = Swap(list);
    Delete(kJournal);
    Delete(kZip);
    char done[160];
    std::snprintf(done, sizeof done, "%s\n%s\n", version, g_note);
    WriteSmall(kInstalled, done);
    hw::Log("[update] installed TF.NX %s: %d of %d file(s) moved in (%d outside its folders skipped), the old ones kept as .old; restart the game", version, moved, files, skipped);
    return true;
}
}  // namespace

void SetTest() { g_test = true; hw::Log("[update] TEST: the title goes to the main menu, the update dialog starts as AUTO UPDATE would, then OPTIONS"); }

void SetSource(const char* base) {
    std::snprintf(g_source, sizeof g_source, "%s", base);
    size_t n = std::strlen(g_source);
    while (n && (g_source[n - 1] == '\n' || g_source[n - 1] == '\r' || g_source[n - 1] == ' ' || g_source[n - 1] == '/')) g_source[--n] = 0;
    hw::Log("[update] TEST: releases from %s", g_source);
}

namespace {
void BootWork() {
    Bind();
    /* a power cut mid-swap: finished from the journal (every .dl in it was checked before the swap began) */
    static char buf[32768];
    if (ReadSmall(kJournal, buf, sizeof buf) > 0) {
        int moved = Swap(buf);
        Delete(kJournal);
        hw::Log("[update] an interrupted install was finished: %d file(s) moved in; restart the game once more", moved);
        g_ui = (int)Ui::Downloaded;
    }
    char inst[160];
    if (ReadSmall(kInstalled, inst, sizeof inst) > 0) {
        char v[24] = "";
        std::sscanf(inst, "%23s", v);
        const char* nl = std::strchr(inst, '\n');
        if (nl) { std::snprintf(g_note, sizeof g_note, "%s", nl + 1); char* e = std::strchr(g_note, '\n'); if (e) *e = 0; }
        if (!std::strcmp(v, kVersion)) { std::snprintf(g_justUpdated, sizeof g_justUpdated, "%s", v); hw::Log("[update] TF.NX %s starts for the first time (installed by the updater)", v); }
        else hw::Log("[update] ERROR: TF.NX %s was installed, but %s runs (the module wasn't replaced?)", v, kVersion);
        Delete(kInstalled);
    }
    if (Exists("sd:/xplay/devserver.txt")) {   /* (devupdate's own flag is set only once its network is up) */
        g_off = true; g_ui = (int)Ui::Off;
        hw::Log("[update] dev server set (sd:/xplay/devserver.txt): GitHub updates off, TF.NX %s", kVersion);
        return;
    }
    hw::Log("[update] TF.NX %s, updates from %s", kVersion, g_source);
}
}  // namespace

void Start() {
    Parse(kVersion, &g_current);
    if (!g_source[0]) std::snprintf(g_source, sizeof g_source, "%s", kDefaultSource);
    netws::StartThread(Thread, nullptr, g_stack, sizeof g_stack);
}

/* ---- TF.EX's IAutoUpdater ---- */
void CheckForUpdate() { if (!IsStatusFresh()) Check(); }
bool IsStatusFresh() { return g_off || (g_checked && g_lastCheck && Now() - g_lastCheck < (uint64_t)kFreshTicks); }
void Invalidate() { g_checked = false; }
int32_t TfexStatus() {
    if (g_off) return 1;
    if (!g_checked) return 0;
    bool newerTfex = TfexPart(g_latest) > TfexPart(g_current);
    return newerTfex && g_ui != (int)Ui::Downloaded ? 2 : 1;
}
int64_t Latest() { return g_latest; }
int64_t Current() { return g_current; }
void* FailureReason() { return g_failure[0] ? tfex::gen::ManagedUtf8(g_failure, (int)std::strlen(g_failure)) : nullptr; }
bool DownloadAndApply(void* onPhase, void* onProgress) {
    if (g_off) return false;
    Lock();
    g_failure[0] = 0;
    g_ui = (int)Ui::Downloading;
    bool ok = Apply(onPhase, onProgress);
    if (ok) g_restartAt = 100000;   /* (the notice waits for the dialog: RestartNeeded sets the real countdown) */
    g_ui = (int)(ok ? Ui::Downloaded : Ui::Failed);
    Unlock();
    return ok;
}
void RestartNeeded() {
    hw::Log("[update] TF.NX %s is in: the game must be restarted (the dialog says so, then the main menu's notice)", g_latestText);
    g_ui = (int)Ui::Downloaded;
    g_restartAt = 180;   /* the dialog's "restart the game" shown 3 s, then the controllers come back */
}

/* ---- the OPTIONS entry ---- */
Ui UiState() { return (Ui)g_ui; }
const char* LatestText() { return g_latestText; }
const char* CurrentText() { return kVersion; }
bool Busy() { return g_busy != 0 || g_checkReq != 0; }
void CheckNow() { if (!g_off) { g_ui = (int)Ui::Checking; __atomic_store_n(&g_checkReq, 1, __ATOMIC_RELEASE); } }
bool WantsAutoStart() { return g_autoWanted && !g_autoDone && g_ui == (int)Ui::Available; }
void AutoStarted() { g_autoDone = true; }

unsigned long TestButtons() { return g_testPress > 0 ? g_testBits : 0; }   /* npad bits: A 0, Down 15 */

void Frame() {
    using namespace tfex::game;
    void* scene = CurrentScene();
    bool menu = scene && SceneIs("MainMenu");
    if (g_restartAt > 0 && --g_restartAt == 0) { g_restartAt = -1; tfex::gen::GlueUpdates::Dismiss(); hw::Log("[update] the dialog dismissed, the controllers back; the main menu says to restart"); }
    if (!menu) return;
    tfex::gen::GlueUpdates::Refresh();   /* the OPTIONS entry's state, live */
    int32_t state = At<int32_t>(scene, MainMenu::state);
    if (g_test) {   /* TEST ONLY: the menus walked (tools/update-test.sh) */
        static int changelog = 0;
        if (g_testPress > 0 && g_testStep != 2) g_testPress--;
        if (state == 66 && g_testPress == 0 && ++changelog % 60 == 0) {   /* TF.EX's changelog (a new TF.EX since the save's last): A closes it */
            if (changelog == 60) hw::Log("[update] TEST: TF.EX's changelog: A");
            g_testBits = 1u << 0; g_testPress = 6;
        }
        if (g_testStep == 0 && state == MainMenu::PressStart && ++g_testFrames > 120) {
            hw::Log("[update] TEST: the main menu"); Fn<void (*)(void*, int)>(MainMenu::set_State)(scene, MainMenu::Main); g_testStep = 1; g_testFrames = 0;
        } else if (g_testStep == 1) {   /* 2 s after the dialog has gone */
            g_testFrames = g_ui == (int)Ui::Downloaded && g_restartAt < 0 ? g_testFrames + 1 : 0;
            if (g_testFrames > 120) { hw::Log("[update] TEST: OPTIONS (the TF.NX entry)"); Fn<void (*)(void*, int)>(MainMenu::set_State)(scene, 12); g_testStep = 2; g_testFrames = 0; }
        } else if (g_testStep == 2) {   /* Down to the TF.NX entry (the TF.NX section, above TF.EX's), then 4 s on it */
            int at = tfex::NetplayOptions::TfnxEntry(), sel = tfex::NetplayOptions::SelectedIndex();
            if (g_testPress > 0) g_testPress--;
            else if (++g_testFrames > 90 && at >= 0 && sel != at && g_testFrames % 20 == 0) { g_testBits = 1u << 15; g_testPress = 6; }
            else if (at >= 0 && sel == at) { hw::Log("[update] TEST: the TF.NX entry selected"); g_testStep = 3; g_testFrames = 0; }
        } else if (g_testStep == 3 && ++g_testFrames == 240) hw::Log("[update] TEST: done");
    }
    if (g_justUpdated[0] && state == MainMenu::Main) {
        char t[64];
        std::snprintf(t, sizeof t, "TF.NX UPDATED TO V%s", g_justUpdated);
        tfex::gen::Notification::CreateOrDefer(scene, tfex::gen::ManagedUtf8(t, (int)std::strlen(t)));
        if (g_note[0]) tfex::gen::Notification::CreateOrDefer(scene, tfex::gen::ManagedUtf8(g_note, (int)std::strlen(g_note)));
        g_justUpdated[0] = 0;
    }
    /* AUTO UPDATE (TF.EX's setting): a newer TF.NX found at boot starts TF.EX's dialog on the main menu, once */
    if (state == MainMenu::Main && WantsAutoStart() && g_restartAt < 0) {
        AutoStarted();
        if (tfex::gen::Auto_NetplayPreferences::AutoUpdate || g_test) {
            hw::Log("[update] AUTO UPDATE: TF.NX %s found at boot: TF.EX's update dialog on the main menu", g_latestText);
            tfex::gen::NetplayEntry::StartUpdate(scene, tfex::gen::svc::Service(), tfex::gen::svc::Service());
        } else hw::Log("[update] TF.NX %s is out (AUTO UPDATE off: OPTIONS' TF.NX entry downloads it)", g_latestText);
    }
    if (g_ui == (int)Ui::Downloaded && g_restartAt < 0)   /* at the top of the main menu itself (its bottom has the menu's labels; other screens scroll under it) */
        devupdate::ShowNotice(scene, state == MainMenu::Main ? "TF.NX UPDATE READY - RESTART THE GAME" : "", 8.0f);
}
}  // namespace tfnxupdate
