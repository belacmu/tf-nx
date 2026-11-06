/* Dev updates over Wi-Fi (opt-in: sd:/xplay/devserver.txt = "<host> <port>", the Mac running tools/devserver.py).
 *
 * A thread polls http://<host>:<port>/manifest2.txt every few seconds: lines `file <sd path, %20 for spaces> <size> <crc32 hex> <url path>`. A file whose
 * SD copy has another size or CRC-32 is downloaded to "<sd path>.dl" (checked against size and CRC), then swapped in (delete + rename).
 * The mod and Atmosphère read these files at launch, so a new build or hw.txt takes effect when the game is restarted; the main menu
 * shows the state at the bottom of the screen (downloading / ready: restart). Test logs go back with Upload / UploadFile
 * (POST /upload/<name>). Without devserver.txt nothing here runs. */
#include "devupdate.hpp"
#include "netws.hpp"
#include "hwlog.hpp"
#include "pools.hpp"
#include "tfex/Game.hpp"
#include "tfex/MainMenuPatch.hpp"
#include "nn/fs.hpp"
#include <cstdio>
#include <cstring>
#include <cstdlib>

namespace devupdate {
using namespace tfex::game;

static char g_host[64];
static int g_port = 0;
static bool g_on = false;

/* state, written by the update thread, read by the game thread */
enum State { Idle, Downloading, Ready, Failed };
static volatile int g_state = Idle, g_done = 0, g_total = 0, g_updated = 0;
static char g_failWhat[40];

/* ---- CRC-32 (zlib's) ---- */
static uint32_t g_crcTab[256];
static void CrcInit() { for (uint32_t i = 0; i < 256; i++) { uint32_t c = i; for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1; g_crcTab[i] = c; } }
static uint32_t Crc(uint32_t crc, const uint8_t* p, size_t n) { crc = ~crc; while (n--) crc = g_crcTab[(crc ^ *p++) & 0xff] ^ (crc >> 8); return ~crc; }

/* ---- SD files ---- */
using DeleteFn = uint32_t (*)(const char*);
using RenameFn = uint32_t (*)(const char*, const char*);
static DeleteFn g_delete = nullptr;
static DeleteFn g_deleteDir = nullptr;   /* nn::fs::DeleteDirectoryRecursively: manifest "delete" lines */
using EntryTypeFn = uint32_t (*)(nn::fs::DirectoryEntryType*, const char*);
static EntryTypeFn g_entryType = nullptr;   /* nn::fs::GetEntryType */
static RenameFn g_rename = nullptr;
static uint8_t g_io[65536];

/* size and CRC of an SD file; false if it can't be read */
static bool LocalInfo(const char* path, int64_t* size, uint32_t* crc) {
    nn::fs::FileHandle h;
    if (nn::fs::OpenFile(&h, path, nn::fs::OpenMode_Read) != 0) return false;
    long sz = 0; nn::fs::GetFileSize(&sz, h);
    uint32_t c = 0;
    for (long off = 0; off < sz;) {
        long n = sz - off < (long)sizeof g_io ? sz - off : (long)sizeof g_io;
        if (nn::fs::ReadFile(h, off, g_io, (size_t)n) != 0) { nn::fs::CloseFile(h); return false; }
        c = Crc(c, g_io, (size_t)n); off += n;
    }
    nn::fs::CloseFile(h);
    *size = sz; *crc = c;
    return true;
}

struct Dl { nn::fs::FileHandle h; int64_t off, size; uint32_t crc; bool ok; };
static void DlSink(void* ctx, const uint8_t* p, int n) {
    Dl* d = static_cast<Dl*>(ctx);
    if (!d->ok || d->off + n > d->size) { d->ok = false; return; }
    if (nn::fs::WriteFile(d->h, d->off, p, (u64)n, nn::fs::WriteOption::CreateOption(0)) != 0) { d->ok = false; return; }
    d->crc = Crc(d->crc, p, (size_t)n); d->off += n;
}

/* the parent folders of an SD path ("sd:/a/b/c.txt" -> sd:/a, sd:/a/b) */
static void MakeParents(const char* path) {
    char p[300];
    std::snprintf(p, sizeof p, "%s", path);
    for (char* s = std::strchr(p + 4, '/'); s; s = std::strchr(s + 1, '/')) { *s = 0; nn::fs::CreateDirectory(p); *s = '/'; }
}

static bool Download(const char* sdPath, const char* url, int64_t size, uint32_t crc) {
    MakeParents(sdPath);
    char tmp[300];
    std::snprintf(tmp, sizeof tmp, "%s.dl", sdPath);
    if (g_delete) g_delete(tmp);
    if (nn::fs::CreateFile(tmp, size) != 0) { hw::Log("[dev] can't create %s", tmp); return false; }
    Dl d{};
    if (nn::fs::OpenFile(&d.h, tmp, nn::fs::OpenMode_Write) != 0) { hw::Log("[dev] can't open %s", tmp); return false; }
    d.size = size; d.ok = true;
    int64_t got = 0;
    int status = netws::HttpGet(g_host, g_port, url, DlSink, &d, &got);
    nn::fs::FlushFile(d.h);
    nn::fs::CloseFile(d.h);
    if (status != 200 || !d.ok || d.off != size || d.crc != crc) {
        hw::Log("[dev] download of %s failed (status %d, %lld of %lld bytes, crc %08x want %08x)", sdPath, status, (long long)d.off, (long long)size, d.crc, crc);
        if (g_delete) g_delete(tmp);
        return false;
    }
    if (g_delete) g_delete(sdPath);
    if (!g_rename || g_rename(tmp, sdPath) != 0) { hw::Log("[dev] can't move %s into place", tmp); return false; }
    hw::Log("[dev] updated %s (%lld bytes, crc %08x)", sdPath, (long long)size, crc);
    return true;
}

/* ---- the manifest ---- */
static char g_manifest[262144];   /* ~150 bytes a line: up to ~1700 files (2026-10-17: 408 lines, 57 KB of the old 64 KB) */
static int g_manLen = 0;
static bool g_manCut = false;    /* the manifest didn't fit: never a complete sync */
static void ManSink(void*, const uint8_t* p, int n) {
    int room = (int)sizeof g_manifest - 1 - g_manLen;
    if (n > room) { n = room; g_manCut = true; }
    std::memcpy(g_manifest + g_manLen, p, (size_t)n); g_manLen += n; g_manifest[g_manLen] = 0;
}

struct Entry { char sd[200]; char url[200]; int64_t size; uint32_t crc; };
/* the mod + settings + the PC levels (192) + the wide ones (194): 408 on 2026-10-17. It was 320, and the console silently synced only the
 * first 320 (the last 88 wide levels, PCWide from Moonstone 01 on, never reached the SD card). */
constexpr int kMaxEntries = 1024;
static Entry g_entries[kMaxEntries];

static void CheckOnce() {
    g_manLen = 0; g_manifest[0] = 0; g_manCut = false;
    int64_t len = 0;
    if (netws::HttpGet(g_host, g_port, "/manifest2.txt", ManSink, nullptr, &len) != 200) return;   /* server not running: quiet */
    /* the same manifest as the last complete sync: nothing to check (saves reading and checksumming every file every 4 s) */
    static uint32_t s_synced = 0;
    uint32_t man = Crc(0, reinterpret_cast<const uint8_t*>(g_manifest), g_manLen);
    if (man == s_synced) return;
    int n = 0, over = 0;
    static char dels[8][200]; int nd = 0;
    for (char* line = std::strtok(g_manifest, "\n"); line; line = std::strtok(nullptr, "\n")) {
        if (n == kMaxEntries) { if (!std::strncmp(line, "file ", 5)) over++; continue; }
        if (nd < 8 && std::sscanf(line, "delete %199s", dels[nd]) == 1) {   /* a file or folder the update removes (%20 = space) */
            char* w = dels[nd];
            for (const char* r = dels[nd]; *r; r++) { if (r[0] == '%' && r[1] == '2' && r[2] == '0') { *w++ = ' '; r += 2; } else *w++ = *r; }
            *w = 0; nd++; continue;
        }
        Entry e{};
        char crcText[16];
        long long sz = 0;
        if (std::sscanf(line, "file %199s %lld %15s %199s", e.sd, &sz, crcText, e.url) == 4) {
            e.size = sz; e.crc = (uint32_t)std::strtoul(crcText, nullptr, 16);
            char* w = e.sd;   /* %20 -> ' ' (manifest2) */
            for (const char* r = e.sd; *r; r++) { if (r[0] == '%' && r[1] == '2' && r[2] == '0') { *w++ = ' '; r += 2; } else *w++ = *r; }
            *w = 0;
            g_entries[n++] = e;
        }
    }
    if (g_manCut) pools::Exhausted("devupdate manifest buffer", (int)sizeof g_manifest, "the manifest was cut: files past it are not synced");
    if (over) { char w[96]; std::snprintf(w, sizeof w, "%d files of the manifest not synced", over); pools::Exhausted("devupdate entries", kMaxEntries, w); }
    const bool complete = !g_manCut && over == 0;
    static int lastN = 0, lastBytes = 0; lastN = n + over; lastBytes = g_manLen;
    static bool registered = false;
    if (!registered) {
        registered = true;
        pools::Register("devupdate entries", [](pools::Use* u) { u->used = lastN; u->cap = kMaxEntries; });
        pools::Register("devupdate manifest KB", [](pools::Use* u) { u->used = lastBytes / 1024; u->cap = (int)sizeof g_manifest / 1024; });
    }   /* (an incomplete sync is never recorded as done: it's tried again) */
    /* deletions first (e.g. an older install's files that would shadow the new ones): only what is there */
    bool deletedRomfs = false;
    for (int i = 0; i < nd; i++) {
        nn::fs::DirectoryEntryType t;
        if (!g_entryType) {   /* can't tell: said once (2026-11-05: the console kept sd:/xplay/mods/ThreeTeamMode with no line about it) */
            static bool said = false;
            if (!said) { said = true; hw::Log("[dev] ERROR: nn::fs::GetEntryType not found: the manifest's %d delete line(s) not done", nd); }
            break;
        }
        if (uint32_t r = g_entryType(&t, dels[i])) {   /* not there (fs's PathNotFound, 0x202) is the usual answer: nothing to do */
            if (r != 0x202) hw::Log("[dev] delete %s: can't tell what it is (result 0x%x), left", dels[i], r);
            continue;
        }
        uint32_t r = t == nn::fs::DirectoryEntryType_Directory ? (g_deleteDir ? g_deleteDir(dels[i]) : 1) : (g_delete ? g_delete(dels[i]) : 1);
        if (r == 0) hw::Log("[dev] delete %s: done", dels[i]);
        else hw::Log("[dev] delete %s: failed (result 0x%x, %s)", dels[i], r, t == nn::fs::DirectoryEntryType_Directory ? "a folder" : "a file");
        if (r == 0 && std::strstr(dels[i], "/romfs/")) deletedRomfs = true;
    }
    /* what differs */
    static int todo[kMaxEntries]; int nt = 0;
    for (int i = 0; i < n; i++) {
        int64_t sz = -1; uint32_t c = 0;
        if (!LocalInfo(g_entries[i].sd, &sz, &c) || sz != g_entries[i].size || c != g_entries[i].crc) todo[nt++] = i;
    }
    if (!nt) {
        if (deletedRomfs && g_delete) g_delete("sd:/atmosphere/contents/0100A1C00359C000/romfs_metadata.bin");   /* the romfs layout changed */
        if (deletedRomfs) { g_state = Ready; g_updated++; }
        if (complete) s_synced = man; return;
    }
    g_total = nt; g_done = 0; g_state = Downloading;
    hw::Log("[dev] %d file(s) to update from %s:%d", nt, g_host, g_port);
    for (int k = 0; k < nt; k++) {
        const Entry& e = g_entries[todo[k]];
        if (!Download(e.sd, e.url, e.size, e.crc)) {
            const char* base = std::strrchr(e.sd, '/');
            std::snprintf(g_failWhat, sizeof g_failWhat, "%s", base ? base + 1 : e.sd);
            g_state = Failed;
            return;
        }
        g_done = k + 1;
    }
    /* Atmosphere caches the romfs layout (romfs_metadata.bin next to romfs/); a new or replaced romfs file needs it rebuilt */
    bool romfs = false;
    for (int k = 0; k < nt; k++) if (std::strstr(g_entries[todo[k]].sd, "/romfs/")) romfs = true;
    romfs |= deletedRomfs;
    if (romfs && g_delete) {
        static const char kMeta[] = "sd:/atmosphere/contents/0100A1C00359C000/romfs_metadata.bin";
        hw::Log("[dev] romfs changed: removing %s (%s)", kMeta, g_delete(kMeta) == 0 ? "done" : "not there");
    }
    g_updated += nt;
    g_state = Ready;
    if (complete) s_synced = man;
}

static uint8_t g_stack[0x8000] __attribute__((aligned(0x1000)));
/* sd:/xplay/devserver.txt -> g_host, g_port; false without it */
static bool ReadConfig() {
    nn::fs::FileHandle h;
    if (nn::fs::OpenFile(&h, "sd:/xplay/devserver.txt", nn::fs::OpenMode_Read) != 0) return false;
    char text[128] = {};
    long sz = 0; nn::fs::GetFileSize(&sz, h);
    if (sz > (long)sizeof text - 1) sz = sizeof text - 1;
    nn::fs::ReadFile(h, 0, text, (size_t)sz);
    nn::fs::CloseFile(h);
    if (std::sscanf(text, "%63s %d", g_host, &g_port) != 2 || g_port <= 0) { hw::Log("[dev] devserver.txt: expected \"<host> <port>\""); return false; }
    return true;
}
static volatile int g_uploadReq = 0;   /* RequestUpload: done by this thread (the caller mustn't block on HTTP) */
static char g_uploadName[64], g_uploadPath[128];
bool UploadFile(const char* name, const char* sdPath);
/* The newest crash report of this game in sd:/atmosphere/crash_reports (names start with the time stamp), or false. */
static bool LatestCrashReport(char* best, int cap) {
    best[0] = 0;
    nn::fs::DirectoryHandle d;
    if (nn::fs::OpenDirectory(&d, "sd:/atmosphere/crash_reports", nn::fs::OpenDirectoryMode_File) != 0) return false;
    static nn::fs::DirectoryEntry entries[16];
    for (;;) {
        s64 n = 0;
        if (nn::fs::ReadDirectory(&n, entries, d, 16) != 0 || n <= 0) break;
        for (s64 i = 0; i < n; i++) {
            const char* e = entries[i].m_Name;
            if (!std::strstr(e, "0100a1c00359c000") && !std::strstr(e, "0100A1C00359C000")) continue;
            if (std::strcmp(e, best) > 0) std::snprintf(best, cap, "%s", e);   /* names start with the time stamp */
        }
        if (n < 16) break;
    }
    nn::fs::CloseDirectory(d);
    return best[0] != 0;
}
static void Thread(void*) {
    /* the SD and the network are ready some seconds after launch (exl_main is too early for nn::fs) */
    netws::Sleep(5'000'000'000ll);
    while (!hw::PreviousLogRead()) netws::Sleep(500'000'000ll);
    if (!ReadConfig()) { for (;;) netws::Sleep(3'600'000'000'000ll); }   /* an nn::os thread must never return */
    g_delete = reinterpret_cast<DeleteFn>(netws::SdkExport("_ZN2nn2fs10DeleteFileEPKc"));
    g_deleteDir = reinterpret_cast<DeleteFn>(netws::SdkExport("_ZN2nn2fs28DeleteDirectoryRecursivelyEPKc"));
    g_entryType = reinterpret_cast<EntryTypeFn>(netws::SdkExport("_ZN2nn2fs12GetEntryTypeEPNS0_18DirectoryEntryTypeEPKc"));
    g_rename = reinterpret_cast<RenameFn>(netws::SdkExport("_ZN2nn2fs10RenameFileEPKcS2_"));
    if (!g_delete || !g_rename) hw::Log("[dev] nn::fs::DeleteFile/RenameFile not found: updates can't be swapped in");
    while (!netws::EnsureNetwork()) netws::Sleep(10'000'000'000ll);
    CrcInit();
    g_on = true;
    hw::Log("[dev] dev updates from %s:%d", g_host, g_port);
    if (size_t prev = hw::PreviousFileLen()) {   /* the previous session's log, whatever ended it (a match-end upload never came for a hang) */
        bool ok = UploadFile("prev-hw-log.txt", "sd:/xplay/hw-log-prev.txt");
        hw::Log("[dev] the previous session's log (%zu bytes) %s; %s", prev, ok ? "uploaded" : "NOT uploaded (kept as sd:/xplay/hw-log-prev.txt)", hw::PreviousStatus());
    } else {
        hw::Log("[dev] no previous session's log to upload: %s", hw::PreviousStatus());
    }
    if (char crash[160]; LatestCrashReport(crash, sizeof crash)) {   /* the newest Atmosphère crash report for TowerFall, whole */
        static char path[256], name[192];
        std::snprintf(path, sizeof path, "sd:/atmosphere/crash_reports/%s", crash);
        std::snprintf(name, sizeof name, "crash-%s", crash);
        UploadFile(name, path);
    }
    uint64_t liveAt = 0;
    for (int n = 0;; n++) {
        if (n % 8 == 0) CheckOnce();   /* every 4 s: a new build is picked up while the game sits in the menu */
        if (n % 90 == 0 && hw::Live() && hw::Appended() != liveAt) {   /* every 45 s in a lobby or a match, if the log grew: a hard hang leaves a recent copy */
            liveAt = hw::Appended();
            hw::FlushNow();
            UploadFile("live-hw-log.txt", "sd:/xplay/hw-log.txt");
        }
        if (__atomic_exchange_n(&g_uploadReq, 0, __ATOMIC_ACQ_REL)) { hw::FlushNow(); UploadFile(g_uploadName, g_uploadPath); }
        netws::Sleep(500'000'000ll);
    }
}

void Start() { netws::StartThread(Thread, nullptr, g_stack, sizeof g_stack); }

void RequestUpload(const char* name, const char* sdPath) {
    std::snprintf(g_uploadName, sizeof g_uploadName, "%s", name);
    std::snprintf(g_uploadPath, sizeof g_uploadPath, "%s", sdPath);
    __atomic_store_n(&g_uploadReq, 1, __ATOMIC_RELEASE);
}

bool Enabled() { return g_on; }

bool Upload(const char* name, const char* data, int len) {
    if (!g_on) return false;
    char path[160];
    std::snprintf(path, sizeof path, "/upload/%s", name);
    int status = netws::HttpPost(g_host, g_port, false, path, "text/plain", data, len, nullptr);
    hw::Log("[dev] upload %s (%d bytes): %d", name, len, status);
    return status == 200;
}

bool UploadFile(const char* name, const char* sdPath) {
    if (!g_on) return false;
    nn::fs::FileHandle h;
    if (nn::fs::OpenFile(&h, sdPath, nn::fs::OpenMode_Read) != 0) return false;
    long sz = 0; nn::fs::GetFileSize(&sz, h);
    static char buf[1700 * 1024];   /* (this thread only; a log is at most 1.6 MB: no large malloc) */
    if (sz > (long)sizeof buf) sz = (long)sizeof buf;
    bool ok = false;
    if (nn::fs::ReadFile(h, 0, buf, (size_t)sz) == 0) {
        while (sz > 0 && (buf[sz - 1] == ' ' || buf[sz - 1] == 0)) sz--;   /* a fixed-size file's padding */
        ok = Upload(name, buf, (int)sz);
    }
    nn::fs::CloseFile(h);
    return ok;
}

/* ---- the notice: an OutlineText at the bottom of the main menu ---- */
static void* g_noticeScene = nullptr;
static void* g_noticeText = nullptr;
static int g_noticeShown = -1;
static MString g_noticeStr[2];
static int g_noticeCur = 0;

void Frame() {
    if (!g_on) return;
    int st = g_state;
    if (st == Idle) return;
    void* scene = CurrentScene();
    if (!scene || !Is(scene, "MainMenu")) return;   /* never inside a Level (netplay state) */
    const void* live = tfex::MainMenuPatch::LiveString();
    if (!live) return;
    char text[44];
    if (st == Downloading) std::snprintf(text, sizeof text, "DEV UPDATE: DOWNLOADING %d/%d", g_done + 1 > g_total ? g_total : g_done + 1, g_total);
    else if (st == Ready) std::snprintf(text, sizeof text, "DEV UPDATE READY (%d) - RESTART THE GAME", g_updated);
    else std::snprintf(text, sizeof text, "DEV UPDATE FAILED: %.20s", g_failWhat);
    int key = st * 1000 + g_done * 10 + g_updated;
    if (scene == g_noticeScene && key == g_noticeShown) return;
    g_noticeCur ^= 1;   /* the text object may still hold the other string */
    MakeString(&g_noticeStr[g_noticeCur], live, text);
    if (scene != g_noticeScene || !g_noticeText) {
        /* a new entity on the menu's layer -2 (screen-fixed), with the text */
        void* ent = Fn<void* (*)(void*, int, float, float)>(0x92d620)(nullptr, -2, 210.0f, 232.0f);   /* new Entity(Vector2 position, int layerIndex): layer -2, the screen-fixed one ScreenTitle uses */
        if (!ent) return;
        void* font = Fn<void* (*)()>(TFGame::get_Font)();
        g_noticeText = Fn<void* (*)(void*, void*, void*, float, float, int, int)>(OutlineText::ctor)(nullptr, font, &g_noticeStr[g_noticeCur], 0.0f, 0.0f, 1, 1);
        if (!g_noticeText) return;
        Fn<void (*)(void*, void*)>(Entity::Add_Component)(ent, g_noticeText);
        void* layers = At<void*>(scene, 0x18);                                   /* Scene.Layers (Dictionary<int, Layer>) */
        void* layer = layers ? Fn<void* (*)(void*, int)>(0x92d9f0)(layers, At<int32_t>(ent, 0x54)) : nullptr;   /* Layers[entity.LayerIndex] */
        if (!layer) return;
        Fn<void (*)(void*, void*, int)>(0x938e40)(layer, ent, 0);              /* Layer.Add (deferred, as Scene.Add) */
        g_noticeScene = scene;
    } else {
        Fn<void (*)(void*, void*)>(OutlineText::set_DrawText)(g_noticeText, &g_noticeStr[g_noticeCur]);
    }
    g_noticeShown = key;
}

} // namespace devupdate
