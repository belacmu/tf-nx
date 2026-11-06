#include "lib.hpp"
#include "loggers.hpp"
#include "nn/fs.hpp"
#include "netws.hpp"
#include <common.hpp>

#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "hwlog.hpp"
#include "pools.hpp"

namespace hw {

/* The file: the session's first kHead bytes, then (once that is full) a marker line and a rolling tail of the last kTail bytes, so the
 * end of a long match is always there (2026-10-17: a 128 KB log filled before a hardware match's end, "(log full: later lines dropped)",
 * and why the match ended was lost). */
constexpr size_t kHead = 1024 * 1024, kTail = 512 * 1024, kMarker = 160;
constexpr size_t kLogMax = kHead + kMarker + kTail;   /* the fixed file size */
static char g_log[kHead];
static size_t g_len = 0;
static size_t g_headLimit = kHead;   /* hw.txt "hwloghead N": a smaller head (tests of the rolling tail) */
void SetHeadLimit(size_t n) { if (n >= 4096 && n <= kHead) g_headLimit = n; }
static char g_tail[kTail];
static size_t g_tailW = 0;          /* bytes ever put in the tail ring (position mod kTail) */
static uint32_t g_tailLines = 0;    /* lines that went to the tail */
static bool g_dirty = false, g_full = false;
static volatile int g_lock = 0;
static char g_copy[kLogMax];   /* Flush's snapshot of the file (and ReadPrevious' buffer before the first one) */
static bool g_mountTried = false, g_mounted = false;

static void Lock() { while (__sync_lock_test_and_set(&g_lock, 1)) svcSleepThread(0); }
static void Unlock() { __sync_lock_release(&g_lock); }

bool MountSd() {
    Lock();
    if (!g_mountTried) {
        g_mountTried = true;
        g_mounted = nn::fs::MountSdCardForDebug("sd") == 0;
    }
    bool ok = g_mounted;
    Unlock();
    return ok;
}


/* The previous session's log file, read before this session first overwrites it (Flush). */
static size_t g_prevFileLen = 0;   /* bytes of sd:/xplay/hw-log-prev.txt this boot wrote (0: none) */
static bool g_prevRead = false;
static char g_prevStatus[160] = "not read yet";   /* what ReadPrevious did (devupdate logs it at boot) */
static void ReadPrevious() {
    nn::fs::FileHandle h;
    if (int r = nn::fs::OpenFile(&h, "sd:/xplay/hw-log.txt", nn::fs::OpenMode_Read); r != 0) {
        std::snprintf(g_prevStatus, sizeof g_prevStatus, "sd:/xplay/hw-log.txt not opened (result %#x)", (unsigned)r);
        __atomic_store_n(&g_prevRead, true, __ATOMIC_RELEASE);
        return;
    }
    long size = 0;
    nn::fs::GetFileSize(&size, h);
    long fileSize = size;
    char* all = g_copy;   /* (Flush's buffer: ReadPrevious runs in Flush before it is used; not malloc: the module's heap aborts on 1.6 MB) */
    if (size > (long)kLogMax) size = (long)kLogMax;
    nn::fs::ReadFile(h, 0, all, (size_t)size);
    nn::fs::CloseFile(h);
    while (size > 0 && (all[size - 1] == ' ' || all[size - 1] == 0 || all[size - 1] == '\n')) size--;   /* the fixed-size padding */
    /* the whole previous session, kept as hw-log-prev.txt before this session's first write overwrites hw-log.txt: devupdate uploads it at
     * boot, so a session that never reached a match end (a hang, a force-quit, a crash) still reaches the dev server (2026-10-18) */
    long trimmedFrom = size;
    bool copied = size > 0 && WriteFixedRange("sd:/xplay/hw-log-prev.txt", all, 0, (size_t)size, kLogMax);
    if (copied) g_prevFileLen = (size_t)size;
    std::snprintf(g_prevStatus, sizeof g_prevStatus, "hw-log.txt %ld bytes (%ld after the padding), copy to hw-log-prev.txt %s", (long)fileSize,
                  trimmedFrom, size <= 0 ? "skipped (empty)" : copied ? "written" : "FAILED");
    /* last (2026-10-18): devupdate waits for this flag, then reads g_prevFileLen. Set first (as before), a copy that takes seconds on the
     * writer thread (since 13:06) was still running when devupdate looked: 0 bytes, no upload and no log line at every boot since */
    __atomic_store_n(&g_prevRead, true, __ATOMIC_RELEASE);
}
const char* PreviousStatus() { return g_prevStatus; }
size_t PreviousFileLen() { return g_prevFileLen; }
static volatile uint64_t g_appended = 0;   /* bytes ever appended (the live upload's "did it grow") */
uint64_t Appended() { return g_appended; }
static volatile bool g_live = false;
void SetLive(bool on) { g_live = on; }
bool Live() { return g_live; }
bool PreviousLogRead() { return __atomic_load_n(&g_prevRead, __ATOMIC_ACQUIRE); }

void Append(const char* line) {
    size_t n = std::strlen(line);
    bool rolled = false;
    Lock();
    g_appended += n + 1;
    if (!g_full && g_len + n + 1 < g_headLimit) {
        std::memcpy(g_log + g_len, line, n);
        g_len += n;
        g_log[g_len++] = '\n';
    } else {
        rolled = !g_full;
        g_full = true;
        if (n > kTail / 4) n = kTail / 4;
        for (size_t i = 0; i <= n; i++) g_tail[(g_tailW + i) % kTail] = i < n ? line[i] : '\n';
        g_tailW += n + 1;
        g_tailLines++;
    }
    g_dirty = true;
    Unlock();
    if (rolled) pools::Exhausted("hw log head", (int)g_headLimit, "the log's head is full: later lines go to a rolling 512 KB tail (the file keeps both)");
}

void Log(const char* fmt, ...) {
    char line[700];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    Logging.Log("[hw] %s", line);
    Append(line);
}

/* Writes data[from..n) (from rounded down to 4 KB) into the fixed-size file, or all of it padded to fixedSize when from == 0. */
bool WriteFixedRange(const char* path, const char* data, size_t from, size_t n, size_t fixedSize) {
    if (!MountSd() || n > fixedSize) return false;
    nn::fs::CreateFile(path, (long)fixedSize); /* fails if it exists; fine */
    nn::fs::FileHandle h;
    /* (Append: a write may grow the file: an existing shorter one, e.g. a 128 KB hw-log.txt from a build before 2026-10-17's 1.6 MB, made
     * the SDK abort on the first write past its end) */
    if (nn::fs::OpenFile(&h, path, nn::fs::OpenMode_Write | nn::fs::OpenMode_Append) != 0) return false;
    /* In chunks: no multi-hundred-KB stack or static page needed (per call: two threads may write files at once). */
    char chunk[4096];
    bool ok = true;
    size_t end = from ? n : fixedSize;   /* an update writes only what was appended; the first write pads the whole file */
    for (size_t off = from & ~(size_t)4095; off < end && ok; off += sizeof chunk) {
        size_t take = off < n ? (n - off < sizeof chunk ? n - off : sizeof chunk) : 0;
        if (take) std::memcpy(chunk, data + off, take);
        if (take < sizeof chunk) std::memset(chunk + take, ' ', sizeof chunk - take);
        size_t len = fixedSize - off < sizeof chunk ? fixedSize - off : sizeof chunk;
        if (from && off + len > n) len = n - off < len ? (n - off + 0) : len;   /* an update stops at the data's end (the padding is there) */
        ok = nn::fs::WriteFile(h, (long)off, chunk, len, nn::fs::WriteOption::CreateOption(0)) == 0;
    }
    if (ok) nn::fs::FlushFile(h);
    nn::fs::CloseFile(h);
    return ok;
}
bool WriteFixedFile(const char* path, const char* data, size_t n, size_t fixedSize) { return WriteFixedRange(path, data, 0, n, fixedSize); }

/* ---- WriteFixedAsync: a writer thread, so the game thread never waits for the SD card (a 3 MB rewrite took ~1.7 s on the console) ---- */
static volatile int g_wReq = 0, g_wStarted = 0;
static const char* g_wPath; static const char* g_wData; static size_t g_wN, g_wFixed;
static char g_wLastPath[64]; static size_t g_wLastN = 0;
alignas(0x1000) static uint8_t g_wStack[0x10000];   /* 64 KB: since 2026-10-18 13:06 this thread runs DoFlush and the boot's ReadPrevious (nn::fs calls
                                                     * under a 4 KB chunk, snprintf): 16 KB was tight for that */
static void DoFlush(bool now);
static void Writer(void*) {
    for (int k = 0;; k++) {
        if (k % 20 == 0) DoFlush(false);   /* the hw log, once a second (net::Log no longer flushes per line on the transport thread) */
        if (__atomic_load_n(&g_wReq, __ATOMIC_ACQUIRE)) {
            const char* path = g_wPath; const char* data = g_wData; size_t n = g_wN, fixed = g_wFixed;
            __atomic_store_n(&g_wReq, 0, __ATOMIC_RELEASE);
            bool same = !std::strcmp(g_wLastPath, path) && n >= g_wLastN && g_wLastN > 0;
            if (WriteFixedRange(path, data, same ? g_wLastN : 0, n, fixed)) { std::snprintf(g_wLastPath, sizeof g_wLastPath, "%s", path); g_wLastN = n; }
        }
        netws::Sleep(50'000'000ll);
    }
}
static void EnsureWriter() {
    if (!__atomic_exchange_n(&g_wStarted, 1, __ATOMIC_ACQ_REL)) {
        if (!netws::StartThread(Writer, nullptr, g_wStack, sizeof g_wStack)) {
            __atomic_store_n(&g_wStarted, 2, __ATOMIC_RELEASE);   /* no writer thread: write on the caller's thread (slow on the console, never lost) */
            Log("[hw] log writer thread not started: writing synchronously");
        }
    }
}
void WriteFixedAsync(const char* path, const char* data, size_t n, size_t fixedSize) {
    EnsureWriter();
    if (__atomic_load_n(&g_wStarted, __ATOMIC_ACQUIRE) == 2) { WriteFixedRange(path, data, 0, n, fixedSize); return; }
    if (__atomic_load_n(&g_wReq, __ATOMIC_ACQUIRE)) return;   /* the last one is still being written: the next request covers this one */
    g_wPath = path; g_wData = data; g_wN = n; g_wFixed = fixedSize;
    __atomic_store_n(&g_wReq, 1, __ATOMIC_RELEASE);
}

/* (the writer thread flushes every second: once the tail rolls, its rewrite waits at least 1 s unless `now`; so a force-quit or a hang
 * keeps all but the last second or so on the SD) */
/* Under the append lock only the tail bytes added since the last flush are copied (into g_tailMirror); the head is append-only, so its
 * bytes below g_len never change and are read without the lock. The file is built and written outside it (evaluation 2026-10-18, rec 5:
 * a game-thread line no longer waits for a 512 KB copy). g_flushLock keeps two flushing threads (the writer, FlushNow) apart. */
static char g_tailMirror[kTail];
static size_t g_mirW = 0;
static volatile int g_flushLock = 0;
static void DoFlush(bool now) {
    for (int tries = 0; __sync_lock_test_and_set(&g_flushLock, 1); tries++) {   /* (a crash handler's FlushNow won't hang on a held lock: 0.5 s) */
        if (tries >= 500) return;
        svcSleepThread(1000000);
    }
    if (!g_prevRead && MountSd()) ReadPrevious();
    static uint64_t lastTail = 0;
    uint64_t t = svcGetSystemTick();
    Lock();
    if (!g_dirty || (g_full && !now && t - lastTail < 19200000ull)) { Unlock(); __sync_lock_release(&g_flushLock); return; }
    g_dirty = false;
    size_t len = g_len, tailW = g_tailW; uint32_t tailLines = g_tailLines; bool full = g_full;
    size_t mfrom = tailW - g_mirW > kTail ? tailW - kTail : g_mirW;
    for (size_t i = mfrom; i < tailW; i++) g_tailMirror[i % kTail] = g_tail[i % kTail];
    g_mirW = tailW;
    Unlock();
    char* copy = g_copy;
    size_t n = len;
    std::memcpy(copy, g_log, n);
    if (full) {
        size_t have = tailW < kTail ? tailW : kTail, from = tailW - have;
        while (from < tailW && from > 0 && g_tailMirror[(from - 1) % kTail] != '\n') from++;   /* whole lines */
        int m = std::snprintf(copy + n, kMarker, "(log head full at %zu bytes: %u later lines went to the rolling tail; its last %zu bytes follow)\n",
                              len, tailLines, (size_t)(tailW - from));
        n += m > 0 ? (size_t)m : 0;
        for (size_t i = from; i < tailW; i++) copy[n++] = g_tailMirror[i % kTail];
        lastTail = t;
    }
    static size_t lastN = 0;   /* (a shorter rewrite than the last one: spaces over the stale end) */
    if (n < lastN) { std::memset(copy + n, ' ', lastN - n); n = lastN; }
    lastN = n;
    static size_t written = 0;   /* the head only grows; the marker and the tail are rewritten from the head's end */
    size_t at = full ? (written < len ? written : len) : (n >= written ? written : 0);
    if (WriteFixedRange("sd:/xplay/hw-log.txt", copy, at, n, kLogMax)) written = full ? len : n;
    __sync_lock_release(&g_flushLock);
}
/* the writer thread flushes once a second; without it (not started), the caller flushes */
void Flush() { EnsureWriter(); if (__atomic_load_n(&g_wStarted, __ATOMIC_ACQUIRE) != 1) DoFlush(false); }
void FlushNow() { DoFlush(true); }

} // namespace hw
