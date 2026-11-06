/* tfexgen_bcl.hpp: the runtime's stand-ins for the BCL classes the game's code lacks. */
#include "tfexgen_bcl.hpp"
#include "tfexgen_binding.hpp"
#include "../MatchmakingService.hpp"
#include "../../hwlog.hpp"
#include "../../netws.hpp"
#include "nn/fs.hpp"
#include <cstdio>
#include <cstring>

extern "C" {
void* tfx_deflate_begin(int32_t level);
int32_t tfx_deflate_feed(void* d, const uint8_t* src, size_t len, bool finish, uint8_t* out, size_t cap, size_t* used, size_t* wrote);
void tfx_deflate_end(void* d);
}

namespace tfex::gen::bcl {
namespace {
constexpr uint32_t kMemMagic = 0x4d454d53, kZipMagic = 0x5a495041, kEntryMagic = 0x5a454e54, kHttpMagic = 0x48545450,
                   kContentMagic = 0x42414354, kResponseMagic = 0x52455350;

/* ---- MemoryStream: two, over module buffers (the module heap can't hold a report): 1 MB (a match report's zip) and 512 KB (a server
 * message TF.EX's listen loop puts together, up to a ~330 KB SkinChunk); a new stream takes the first free one, any thread ---- */
constexpr size_t kMemCap = 1024 * 1024, kMsgCap = 512 * 1024;
alignas(16) uint8_t g_memBuf[kMemCap];
alignas(16) uint8_t g_msgBuf[kMsgCap];
struct Mem { uint32_t magic; volatile bool used; size_t len; bool overflowed; uint8_t* buf; size_t cap; };
Mem g_mems[2] = { { kMemMagic, false, 0, false, g_memBuf, kMemCap }, { kMemMagic, false, 0, false, g_msgBuf, kMsgCap } };
Mem* AsMem(void* p) { auto* m = static_cast<Mem*>(p); return m && m->magic == kMemMagic && m->used ? m : nullptr; }

/* ---- CRC-32 (IEEE, as zip's) ---- */
uint32_t g_crcTable[256];
uint32_t Crc32(uint32_t crc, const uint8_t* p, size_t n) {
    if (!g_crcTable[1])
        for (uint32_t i = 0; i < 256; i++) { uint32_t c = i; for (int k = 0; k < 8; k++) c = c & 1 ? 0xedb88320u ^ (c >> 1) : c >> 1; g_crcTable[i] = c; }
    crc = ~crc;
    for (size_t i = 0; i < n; i++) crc = g_crcTable[(crc ^ p[i]) & 0xff] ^ (crc >> 8);
    return ~crc;
}
void Put16(uint8_t* p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
void Put32(uint8_t* p, uint32_t v) { Put16(p, v); Put16(p + 2, v >> 16); }

/* ---- ZipArchive: one archive, up to 8 entries; local headers written as each entry starts, patched when it ends ---- */
constexpr int kEntries = 8, kNameCap = 160;
/* zip's time fields: DOS 1980-01-01 00:00 (TFEX-DIFF S-native: the runtime has no wall clock, UtcNow counts from boot) */
constexpr uint16_t kDosTime = 0, kDosDate = (0 << 9) | (1 << 5) | 1;
struct Entry {
    uint32_t magic; bool used, open, dropped;
    struct Zip* zip;
    char name[kNameCap]; int nameLen;
    size_t headerAt; uint32_t crc; size_t raw, packed;
    int32_t level; void* deflate;
};
struct Zip { uint32_t magic; bool used; Mem* out; bool leaveOpen; int count; Entry entries[kEntries]; };
Zip g_zip;
Zip* AsZip(void* p) { auto* z = static_cast<Zip*>(p); return z && z->magic == kZipMagic && z->used ? z : nullptr; }
Entry* AsEntry(void* p) { auto* e = static_cast<Entry*>(p); return e && e->magic == kEntryMagic && e->used ? e : nullptr; }
/* .NET's CompressionLevel (Optimal 0, Fastest 1, NoCompression 2, SmallestSize 3) -> zlib's level (SmallestSize: 9, as .NET) */
int32_t ZlibLevel(int32_t level) { return level == 3 ? 9 : level == 1 ? 1 : level == 2 ? 0 : 6; }

bool MemPut(Mem* m, const void* p, size_t n) {
    if (m->overflowed || m->len + n > m->cap) { m->overflowed = true; return false; }
    std::memcpy(m->buf + m->len, p, n); m->len += n;
    return true;
}
/* compress `n` bytes (finish: the entry's last) into the archive's stream */
bool Deflate(Entry* e, const uint8_t* p, size_t n, bool finish) {
    Mem* m = e->zip->out;
    for (;;) {
        size_t used = 0, wrote = 0, room = m->overflowed || m->len >= m->cap ? 0 : m->cap - m->len;
        if (!room) { m->overflowed = true; return false; }
        int r = tfx_deflate_feed(e->deflate, p, n, finish, m->buf + m->len, room, &used, &wrote);
        m->len += wrote; e->packed += wrote; p += used; n -= used;
        if (r < 0) return false;
        if (r == 1) return true;
        if (!finish && n == 0) return true;
        if (!used && !wrote) { m->overflowed = true; return false; }   /* no progress: the buffer is full */
    }
}
void EndEntry(Entry* e) {
    if (!e->open) return;
    e->open = false;
    Mem* m = e->zip->out;
    bool ok = !e->dropped && Deflate(e, nullptr, 0, true);
    if (e->deflate) { tfx_deflate_end(e->deflate); e->deflate = nullptr; }
    if (!ok || m->overflowed) {   /* the entry doesn't fit: dropped whole, the archive stays valid without it */
        hw::Log("[tfex] zip: ERROR entry %s (%zu bytes) doesn't fit the %zu KB report buffer: left out", e->name, e->raw, m->cap / 1024);
        m->len = e->headerAt; m->overflowed = false; e->dropped = true;
        return;
    }
    uint8_t* h = m->buf + e->headerAt;
    Put32(h + 14, e->crc); Put32(h + 18, (uint32_t)e->packed); Put32(h + 22, (uint32_t)e->raw);
}
}   // namespace

void* MemoryNew() {
    for (Mem& m : g_mems) {
        bool free_ = false;
        if (__atomic_compare_exchange_n(&m.used, &free_, true, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) { m.len = 0; m.overflowed = false; return &m; }
    }
    hw::Log("[pools] EXHAUSTED MemoryStream (capacity 2: the report's and a server message's): no new one");
    return nullptr;
}
gen::Arr<uint8_t> MemoryToArray(void* memory) {
    Mem* m = AsMem(memory);
    auto a = gen::NewArr<uint8_t>(bind::rt_ByteArray, m ? (int32_t)m->len : 0);
    if (m && a.data && m->len) std::memcpy(a.data, m->buf, m->len);
    return a;
}
void MemoryDispose(void* memory) { if (Mem* m = AsMem(memory)) __atomic_store_n(&m->used, false, __ATOMIC_RELEASE); }

void* ZipNew(void* stream, int32_t mode, bool leaveOpen) {
    Mem* m = AsMem(stream);
    if (!m || mode != 1 /* Create */ || g_zip.used) {
        hw::Log("[tfex] zip: ERROR new ZipArchive not made (%s)", !m ? "not over the runtime's MemoryStream" : mode != 1 ? "only Create mode" : "one archive at a time");
        return nullptr;
    }
    g_zip.magic = kZipMagic; g_zip.used = true; g_zip.out = m; g_zip.leaveOpen = leaveOpen; g_zip.count = 0;
    return &g_zip;
}
void* ZipCreateEntry(void* zip, void* entryName, int32_t level) {
    Zip* z = AsZip(zip);
    if (!z) return nullptr;
    for (int i = 0; i < z->count; i++) EndEntry(&z->entries[i]);   /* .NET: one entry written at a time */
    if (z->count >= kEntries) { hw::Log("[tfex] zip: ERROR more than %d entries: the rest left out", kEntries); return nullptr; }
    Entry* e = &z->entries[z->count++];
    *e = Entry{};
    e->magic = kEntryMagic; e->used = true; e->zip = z; e->level = ZlibLevel(level);
    e->nameLen = entryName ? gen::Utf8Of(entryName, e->name, kNameCap) : 0;
    if (e->nameLen < 0) e->nameLen = 0;
    e->nameLen = (int)std::strlen(e->name);
    e->headerAt = z->out->len;
    uint8_t h[30] = {};
    Put32(h, 0x04034b50); Put16(h + 4, 20); Put16(h + 6, 0x0800 /* UTF-8 name */); Put16(h + 8, 8 /* deflate */);
    Put16(h + 10, kDosTime); Put16(h + 12, kDosDate); Put16(h + 26, (uint32_t)e->nameLen);
    if (!MemPut(z->out, h, sizeof h) || !MemPut(z->out, e->name, (size_t)e->nameLen)) { e->dropped = true; z->out->len = e->headerAt; z->out->overflowed = false; return e; }
    return e;
}
void* ZipEntryOpen(void* entry) {
    Entry* e = AsEntry(entry);
    if (!e || e->dropped) return e;
    if (e->open || e->deflate) return e;
    e->deflate = tfx_deflate_begin(e->level);
    if (!e->deflate) { hw::Log("[tfex] zip: ERROR no compressor for %s", e->name); e->dropped = true; return e; }
    e->open = true; e->crc = 0;
    return e;
}
void StreamWrite(void* stream, gen::Arr<uint8_t> buffer, int32_t offset, int32_t count) {
    if (Mem* m = AsMem(stream)) {   /* MemoryStream.Write (TF.EX's listen loop: a message's frames) */
        if (!buffer.data || offset < 0 || count <= 0 || offset > buffer.len - count) return;
        if (!MemPut(m, buffer.data + offset, (size_t)count)) hw::Log("[tfex] MemoryStream: ERROR %d bytes don't fit its %zu KB: the rest of the message is lost", count, m->cap / 1024);
        return;
    }
    Entry* e = AsEntry(stream);
    if (!e) { hw::Log("[tfex] Stream.Write: ERROR not one of the runtime's streams (zip entries, MemoryStreams): nothing written"); return; }
    if (!e->open || e->dropped || !buffer.data || offset < 0 || count <= 0 || offset > buffer.len - count) return;
    const uint8_t* p = buffer.data + offset;
    e->crc = Crc32(e->crc, p, (size_t)count); e->raw += (size_t)count;
    if (!Deflate(e, p, (size_t)count, false)) e->dropped = true;
}
void StreamDispose(void* stream) {
    if (Entry* e = AsEntry(stream)) EndEntry(e);
}
void* ZipAddFile(void* zip, void* path, void* entryName, int32_t level) {
    char p[256];
    if (!path || gen::Utf8Of(path, p, sizeof p) <= 0) return nullptr;
    nn::fs::FileHandle h;
    if (nn::fs::OpenFile(&h, p, nn::fs::OpenMode_Read) != 0) { hw::Log("[tfex] zip: %s not opened: not added", p); return nullptr; }
    long size = 0;
    nn::fs::GetFileSize(&size, h);
    void* e = ZipEntryOpen(ZipCreateEntry(zip, entryName, level));
    static uint8_t chunk[16384];
    gen::Arr<uint8_t> view{chunk, (int32_t)sizeof chunk, nullptr};
    for (long at = 0; e && at < size;) {
        size_t n = (size_t)(size - at) < sizeof chunk ? (size_t)(size - at) : sizeof chunk;
        if (nn::fs::ReadFile(h, at, chunk, n) != 0) break;
        StreamWrite(e, view, 0, (int32_t)n);
        at += (long)n;
    }
    nn::fs::CloseFile(h);
    StreamDispose(e);
    return e;
}
void ZipDispose(void* zip) {
    Zip* z = AsZip(zip);
    if (!z) return;
    for (int i = 0; i < z->count; i++) EndEntry(&z->entries[i]);
    Mem* m = z->out;
    size_t cdAt = m->len;
    int n = 0;
    for (int i = 0; i < z->count; i++) {
        Entry* e = &z->entries[i];
        if (e->dropped) continue;
        uint8_t c[46] = {};
        Put32(c, 0x02014b50); Put16(c + 4, 20); Put16(c + 6, 20); Put16(c + 8, 0x0800); Put16(c + 10, 8);
        Put16(c + 12, kDosTime); Put16(c + 14, kDosDate); Put32(c + 16, e->crc); Put32(c + 20, (uint32_t)e->packed); Put32(c + 24, (uint32_t)e->raw);
        Put16(c + 28, (uint32_t)e->nameLen); Put32(c + 42, (uint32_t)e->headerAt);
        if (MemPut(m, c, sizeof c) && MemPut(m, e->name, (size_t)e->nameLen)) n++;
    }
    uint8_t end[22] = {};
    Put32(end, 0x06054b50); Put16(end + 8, (uint32_t)n); Put16(end + 10, (uint32_t)n);
    Put32(end + 12, (uint32_t)(m->len - cdAt)); Put32(end + 16, (uint32_t)cdAt);
    if (!MemPut(m, end, sizeof end)) hw::Log("[tfex] zip: ERROR no room for the central directory: the report is not a valid zip");
    size_t raw = 0;
    for (int i = 0; i < z->count; i++) if (!z->entries[i].dropped) raw += z->entries[i].raw;
    hw::Log("[tfex] zip: report built: %d entr%s, %zu KB from %zu KB", n, n == 1 ? "y" : "ies", (m->len + 1023) / 1024, (raw + 1023) / 1024);
    for (auto& e : z->entries) e.used = false;
    z->used = false;
}

/* ---- System.Net.Http ---- */
namespace {
struct Http { uint32_t magic; int64_t timeoutTicks; } g_http{kHttpMagic, 100LL * 10000000LL};
struct Content { uint32_t magic; bool used; const uint8_t* data; int32_t len; } g_contents[2];
struct Response { uint32_t magic; bool used; int32_t status; } g_responses[2];
template <class T, int N> T* Take(T (&pool)[N], uint32_t magic, const char* what) {
    for (T& x : pool) if (!x.used) { x = T{}; x.magic = magic; x.used = true; return &x; }
    hw::Log("[tfex] %s: ERROR all %d in use (one is never disposed?)", what, N);
    return nullptr;
}
}
void* HttpNew() { return &g_http; }
void HttpSetTimeout(void* http, int64_t ticks) { if (auto* h = static_cast<Http*>(http); h && h->magic == kHttpMagic) h->timeoutTicks = ticks; }
void* ContentNew(gen::Arr<uint8_t> bytes) {
    Content* c = Take(g_contents, kContentMagic, "ByteArrayContent");
    if (c) { c->data = bytes.data; c->len = bytes.data ? bytes.len : 0; }
    return c;
}
void ContentDispose(void* content) { if (auto* c = static_cast<Content*>(content); c && c->magic == kContentMagic) c->used = false; }
void* HttpPost(void* http, void* url, void* content) {
    auto* c = static_cast<Content*>(content);
    Response* r = Take(g_responses, kResponseMagic, "HttpResponseMessage");
    if (!r) return nullptr;
    r->status = 0;
    char u[640];
    if (!url || gen::Utf8Of(url, u, sizeof u) <= 0 || !c || c->magic != kContentMagic) { hw::Log("[tfex] HttpClient.PostAsync: ERROR no url or content"); return r; }
    bool tls = !std::strncmp(u, "https://", 8);
    const char* s = tls ? u + 8 : !std::strncmp(u, "http://", 7) ? u + 7 : nullptr;
    if (!s) { hw::Log("[tfex] HttpClient.PostAsync: ERROR not an http(s) url: %.80s", u); return r; }
    char host[128];
    int i = 0;
    while (s[i] && s[i] != ':' && s[i] != '/' && s[i] != '?' && i < (int)sizeof host - 1) { host[i] = s[i]; i++; }
    host[i] = 0;
    int port = s[i] == ':' ? std::atoi(s + i + 1) : tls ? 443 : 80;
    const char* path = std::strpbrk(s + i, "/?");
    char p[600];
    std::snprintf(p, sizeof p, "%s%s", path && *path == '?' ? "/" : "", path ? path : "/");
    int status = netws::HttpPost(host, port, tls, p, "application/octet-stream", reinterpret_cast<const char*>(c->data), c->len,
                                 tfex::NetplayPreferences::Ip);
    r->status = status < 0 ? 0 : status;
    hw::Log("[tfex] HttpClient.PostAsync %s:%d%.*s (%d bytes): %s %d", host, port, 80, p, c->len, status < 0 ? "failed" : "status", status);
    return r;
}
int32_t HttpStatus(void* response) { auto* r = static_cast<Response*>(response); return r && r->magic == kResponseMagic ? r->status : 0; }
void HttpResponseDispose(void* response) { if (auto* r = static_cast<Response*>(response); r && r->magic == kResponseMagic) r->used = false; }

/* ---- File / Path / Uri / Encoding ---- */
bool FileExists(void* path) {
    char p[256];
    if (!path || gen::Utf8Of(path, p, sizeof p - 5) <= 0) return false;
    if (!std::strchr(p, ':')) {   /* .NET: a relative path is the game's folder ("Content/Atlas/atlas.xml"): the Switch's is rom:/ */
        std::memmove(p + 5, p, std::strlen(p) + 1);
        std::memcpy(p, "rom:/", 5);
    }
    nn::fs::FileHandle h;
    if (nn::fs::OpenFile(&h, p, nn::fs::OpenMode_Read) != 0) return false;
    nn::fs::CloseFile(h);
    return true;
}
void* PathGetFileName(void* path) {
    if (!path) return nullptr;
    int32_t n = gen::Len(path), from = 0;
    const char16_t* c = gen::Chars(path);
    for (int32_t i = 0; i < n; i++) if (c[i] == u'/' || c[i] == u'\\' || c[i] == u':') from = i + 1;
    return gen::SubstringOf(path, from);
}
void* EscapeDataString(void* s) {
    char u[512], out[1536];
    int n = s ? gen::Utf8Of(s, u, sizeof u) : 0, k = 0;
    for (int i = 0; i < n && k < (int)sizeof out - 4; i++) {
        unsigned char ch = (unsigned char)u[i];
        if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == '.' || ch == '~') out[k++] = (char)ch;
        else k += std::snprintf(out + k, sizeof out - (size_t)k, "%%%02X", ch);
    }
    return gen::ManagedUtf8(out, k);
}
gen::Arr<uint8_t> Utf8Bytes(void* s) {
    int32_t len = gen::Len(s), n = 0;
    const char16_t* c = gen::Chars(s);
    for (int32_t i = 0; s && i < len; i++) n += c[i] < 0x80 ? 1 : c[i] < 0x800 ? 2 : 3;
    auto a = gen::NewArr<uint8_t>(bind::rt_ByteArray, n);
    for (int32_t i = 0, k = 0; a.data && i < len; i++) {
        unsigned u = c[i];
        if (u < 0x80) a.data[k++] = (uint8_t)u;
        else if (u < 0x800) { a.data[k++] = (uint8_t)(0xc0 | u >> 6); a.data[k++] = (uint8_t)(0x80 | (u & 0x3f)); }
        else { a.data[k++] = (uint8_t)(0xe0 | u >> 12); a.data[k++] = (uint8_t)(0x80 | ((u >> 6) & 0x3f)); a.data[k++] = (uint8_t)(0x80 | (u & 0x3f)); }
    }
    return a;
}

gen::Arr<uint8_t> CurrentLog() {
    hw::FlushNow();
    const char* path = "sd:/xplay/hw-log.txt";
    nn::fs::FileHandle h;
    if (!hw::MountSd() || nn::fs::OpenFile(&h, path, nn::fs::OpenMode_Read) != 0) { hw::Log("[tfex] MatchReport: %s not opened: no log in the report", path); return gen::Arr<uint8_t>{nullptr, 0, nullptr}; }
    long size = 0;
    nn::fs::GetFileSize(&size, h);
    /* the file is padded with spaces to its fixed size (hwlog.cpp): the log ends at the last other byte */
    static char tail[4096];
    long end = size;
    while (end > 0) {
        long from = end > (long)sizeof tail ? end - (long)sizeof tail : 0;
        if (nn::fs::ReadFile(h, from, tail, (size_t)(end - from)) != 0) { end = 0; break; }
        long i = end - from;
        while (i > 0 && (tail[i - 1] == ' ' || tail[i - 1] == 0)) i--;
        if (i > 0) { end = from + i; break; }
        end = from;
    }
    auto a = gen::NewArr<uint8_t>(bind::rt_ByteArray, (int32_t)end);
    if (a.data && end > 0 && nn::fs::ReadFile(h, 0, a.data, (size_t)end) != 0) a = gen::Arr<uint8_t>{nullptr, 0, nullptr};
    nn::fs::CloseFile(h);
    if (!a.data && end > 0) hw::Log("[tfex] MatchReport: ERROR the log (%ld bytes) not read: no log in the report", end);
    return a;
}

/* ---- ClientWebSocket over netws (tfexgen_bcl.hpp) ---- */
namespace {
constexpr uint32_t kWsMagic = 0x57534f43, kWsResultMagic = 0x57535252;
enum WsState_ : int32_t { kNone = 0, kConnecting = 1, kOpen = 2, kClosed = 5, kAborted = 6 };
struct WsResult { uint32_t magic; int32_t count; int32_t type; bool end; };
struct Ws {
    uint32_t magic; volatile bool used; volatile int32_t state; volatile bool aborted;
    netws::Conn* conn;
    const uint8_t* pend; int32_t pendLen, pendType;   /* the rest of the frame ReceiveAsync hands out piece by piece */
    WsResult result;
};
constexpr int kWs = 4;
Ws g_ws[kWs];
Ws* AsWs(void* p) { auto* w = static_cast<Ws*>(p); return w && w->magic == kWsMagic && w->used ? w : nullptr; }
/* outgoing frames: SendAsync queues (the game thread), the receiving thread sends */
constexpr int kBigMsg = 280 * 1024;
struct BigBuf { int busy; char text[kBigMsg]; };
BigBuf g_outBig[2];
struct Out { int len; char* text; char own[12 * 1024]; };
Out g_out[8];
volatile uint32_t g_outW = 0, g_outR = 0;
char* TakeBig(int len) {
    if (len >= kBigMsg) { hw::Log("[pools] tfex outgoing message of %d bytes is longer than %d: dropped", len, kBigMsg); return nullptr; }
    for (auto& b : g_outBig) { int idle = 0; if (__atomic_compare_exchange_n(&b.busy, &idle, 1, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) return b.text; }
    hw::Log("[pools] EXHAUSTED tfex outgoing large messages (2 x %d bytes): a %d-byte message dropped", kBigMsg, len);
    return nullptr;
}
void GiveBig(const char* text) { for (auto& b : g_outBig) if (text == b.text) __atomic_store_n(&b.busy, 0, __ATOMIC_RELEASE); }
bool Flush(Ws* w) {   /* the receiving thread: the queued frames out; false when the connection failed */
    bool ok = true;
    while (g_outR != __atomic_load_n(&g_outW, __ATOMIC_ACQUIRE)) {
        Out& o = g_out[g_outR % 8];
        if (ok && !netws::SendBinary(w->conn, reinterpret_cast<const uint8_t*>(o.text), o.len)) ok = false;   /* TF.EX sends binary frames */
        GiveBig(o.text);
        __atomic_store_n(&g_outR, g_outR + 1, __ATOMIC_RELEASE);
    }
    return ok;
}
void Drop(Ws* w, int32_t state) { if (w->conn) { netws::Close(w->conn); w->conn = nullptr; } w->pend = nullptr; w->pendLen = 0; w->state = state; }
bool ParseUrl(const char* url, char* host, int hostCap, int* port, bool* tls, char* path, int pathCap) {
    const char* p = url;
    if (!std::strncmp(p, "wss://", 6)) { *tls = true; *port = 443; p += 6; }
    else if (!std::strncmp(p, "ws://", 5)) { *tls = false; *port = 80; p += 5; }
    else return false;
    const char* slash = std::strchr(p, '/');
    const char* colon = std::strchr(p, ':');
    const char* hostEnd = colon && (!slash || colon < slash) ? colon : slash ? slash : p + std::strlen(p);
    std::snprintf(host, (size_t)hostCap, "%.*s", (int)(hostEnd - p), p);
    if (colon && (!slash || colon < slash)) *port = std::atoi(colon + 1);
    std::snprintf(path, (size_t)pathCap, "%s", slash ? slash : "/");
    return host[0] != 0;
}
} // namespace

void* WsNew() {
    for (Ws& w : g_ws) {   /* a free slot, else one whose connection is over */
        bool free_ = false;
        if (__atomic_compare_exchange_n(&w.used, &free_, true, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
            w.magic = kWsMagic; w.state = kNone; w.aborted = false; w.conn = nullptr; w.pend = nullptr; w.pendLen = 0;
            w.result = WsResult{kWsResultMagic, 0, 0, true};
            return &w;
        }
    }
    for (Ws& w : g_ws) if (w.conn == nullptr && (w.state == kNone || w.state == kClosed || w.state == kAborted)) {
        w.state = kNone; w.aborted = false; w.pend = nullptr; w.pendLen = 0;
        return &w;
    }
    hw::Log("[pools] EXHAUSTED ClientWebSocket (capacity %d, all connected): no new one", kWs);
    return nullptr;
}
int32_t WsState(void* ws) { Ws* w = AsWs(ws); return w ? w->state : (int32_t)kClosed; }
void WsConnect(void* ws, void* url) {
    Ws* w = AsWs(ws);
    if (!w) { gen::Fault("ClientWebSocket.ConnectAsync: no socket"); return; }
    if (w->state == kOpen) return;
    char u[256]; if (url) gen::Utf8Of(url, u, sizeof u); else u[0] = 0;
    char host[128], path[160]; int port = 0; bool tls = false;
    if (!ParseUrl(u, host, sizeof host, &port, &tls, path, sizeof path)) { w->state = kClosed; hw::Log("[tfex] ClientWebSocket: bad url %s", u); gen::Fault("ClientWebSocket.ConnectAsync: bad url"); return; }
    w->state = kConnecting;
    g_outR = g_outW;   /* a new connection: nothing left over from the last one */
    w->conn = netws::Open(host, port, tls, path, ::tfex::NetplayPreferences::Ip, netws::Rx::Big);
    if (!w->conn) { w->state = kClosed; hw::Log("[tfex] ClientWebSocket: could not connect to %s", u); gen::Fault("ClientWebSocket.ConnectAsync: could not connect"); return; }
    w->state = w->aborted ? (int32_t)kAborted : (int32_t)kOpen;
    hw::Log("[tfex] ClientWebSocket: connected to %s", u);
}
/* netws reports the server's Close frame as a closed connection: TF.EX sees a lost connection (its catch), not MessageType.Close */
void* WsReceive(void* ws, gen::Arr<uint8_t> segment) {
    Ws* w = AsWs(ws);
    if (!w || w->state != kOpen || !w->conn) { gen::Fault("ClientWebSocket.ReceiveAsync: not open"); return w ? &w->result : nullptr; }
    WsResult& r = w->result;
    auto piece = [&]() {   /* the next piece of the frame in hand, at most the segment's length */
        int32_t n = w->pendLen < segment.len ? w->pendLen : segment.len;
        if (n > 0 && segment.data) std::memcpy(segment.data, w->pend, (size_t)n);
        w->pend += n; w->pendLen -= n;
        r = WsResult{kWsResultMagic, n, w->pendType, w->pendLen == 0};
        if (w->pendLen == 0) w->pend = nullptr;
        return static_cast<void*>(&r);
    };
    if (w->pend) return piece();
    for (;;) {
        if (w->aborted) { Drop(w, kAborted); gen::Fault("ClientWebSocket.ReceiveAsync: cancelled"); return &r; }
        if (!Flush(w)) { hw::Log("[tfex] ClientWebSocket: send failed: connection lost"); Drop(w, kAborted); gen::Fault("ClientWebSocket: connection lost"); return &r; }
        uint8_t op; const uint8_t* payload; int len;
        int got = netws::Next(w->conn, &op, &payload, &len);
        if (got < 0) { hw::Log("[tfex] ClientWebSocket: connection closed"); Drop(w, kAborted); gen::Fault("ClientWebSocket: connection lost"); return &r; }
        if (got > 0) { w->pend = payload; w->pendLen = len; w->pendType = op == 1 ? 0 : 1; return piece(); }
        gen::SetWouldBlock();   /* nothing yet: the listen loop's pass returns (it runs on the game thread, once a frame) */
        r = WsResult{kWsResultMagic, 0, 1, true};
        return &r;
    }
}
void WsSend(void* ws, gen::Arr<uint8_t> segment, int32_t, bool) {
    Ws* w = AsWs(ws);
    if (!w || w->state != kOpen) { gen::Fault("ClientWebSocket.SendAsync: not open"); return; }
    int len = segment.len;
    uint32_t wr = g_outW;
    if (wr - __atomic_load_n(&g_outR, __ATOMIC_ACQUIRE) >= 8) { hw::Log("[pools] EXHAUSTED tfex outgoing messages (8 slots): a %d-byte message dropped", len); return; }
    Out& o = g_out[wr % 8];
    char* dst = len < (int)sizeof o.own ? o.own : TakeBig(len);
    if (!dst) return;
    if (len > 0 && segment.data) std::memcpy(dst, segment.data, (size_t)len);
    dst[len] = 0; o.text = dst; o.len = len;
    if (std::strncmp(dst, "{\"KeepAlive", 11))   /* what TF.EX's code sends, 180 per line, at most 1080 (the scenario checks read these) */
        for (int i = 0; i < len && i < 1080; i += 180) hw::Log("[tfex] Send (%d bytes) @%d: %.180s", len, i, dst + i);
    __atomic_store_n(&g_outW, wr + 1, __ATOMIC_RELEASE);
}
void WsAbortAll() { for (Ws& w : g_ws) if (w.used && (w.state == kOpen || w.state == kConnecting)) w.aborted = true; }
int32_t WsResultCount(void* result) { auto* r = static_cast<WsResult*>(result); return r && r->magic == kWsResultMagic ? r->count : 0; }
int32_t WsResultType(void* result) { auto* r = static_cast<WsResult*>(result); return r && r->magic == kWsResultMagic ? r->type : 2; }
bool WsResultEnd(void* result) { auto* r = static_cast<WsResult*>(result); return !r || r->magic != kWsResultMagic || r->end; }
void* Utf8String(gen::Arr<uint8_t> bytes, int32_t index, int32_t count) {
    if (!bytes.data || index < 0 || count < 0 || index > bytes.len - count) return gen::ManagedUtf8("", 0);
    return gen::ManagedUtf8(reinterpret_cast<const char*>(bytes.data) + index, count);
}
bool ParseWsUrl(const char* url, char* host, int hostCap, int* port, bool* tls, char* path, int pathCap) { return ParseUrl(url, host, hostCap, port, tls, path, pathCap); }
}   // namespace tfex::gen::bcl
