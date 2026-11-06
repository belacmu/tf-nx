/* Network probe (VIABILITY-TESTS.md, tests 7a and 7b): can code running inside the TowerFall
 * process reach the network?
 *
 *   7a  plain TCP: connect to a LAN echo server, 20 round trips, round-trip times.
 *   7b  TLS WebSocket to the signaling server (wss://<host>/room/<id>?peer=<uuid>); pass = the
 *       first message is {"IdAssigned": ...}.
 *
 * Everything is the game's own SDK (nnSdk): nn::nifm, nn::socket and nn::ssl, found by name in the
 * SDK module's dynamic symbols (the game's own import slots aren't bound at exl_main time).
 *
 * The test runs on its own thread, a while after boot, so the game starts normally. What it does
 * is read from sd:/xplay/net.txt, one stage per line:
 *     tcp <ipv4> <port>
 *     tls <host> <port> [<ipv4>]     (the address is used only if name resolution fails)
 *     ggrs <ticks> <delay>           (test 8: two GGRS sessions in-process)
 *     relay <host> <port> <tls 0|1> <seconds> [<ipv4>]   (test 7c: 60 packets/s through a WebSocket relay)
 * and every step is logged twice: to Ryujinx's log ("[net] ...") and to sd:/xplay/net-log.txt
 * (rewritten after each line, so a hang shows where it stopped; the only way to see results on a
 * console).
 *
 * Playing the game isn't needed or affected; no hooks are installed.
 */

#include "lib.hpp"
#include "loggers.hpp"
#include "nn/fs.hpp"
#include <lib/util/sys/modules.hpp>
#include <common.hpp>

#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <cstdlib>
#include <algorithm>

/* The GGRS loopback test (VIABILITY-TESTS test 8), from ../ggrs-spike (Rust, built for the Switch
 * with a custom target and linked into this module). Two sessions in-process over an in-memory
 * socket with delay and loss; returns 0 on success and writes a one-line report. */
extern "C" void net_raw_log(const char* p, size_t n);
extern "C" int ggrs_spike_run(int ticks, int delay, char* out, size_t cap);

namespace net {

/* ---- Finding SDK functions ------------------------------------------------------------------ */

/* Same lookup as the crossplay probe: the address of a function defined in any loaded module. */
struct Elf64Sym { uint32_t name; uint8_t info, other; uint16_t shndx; uint64_t value, size; };
static uintptr_t FindExport(const char* wanted) {
    for (int i = (int)exl::util::ModuleIndex::Start; i < (int)exl::util::ModuleIndex::End; i++) {
        if (!exl::util::impl::mem_layout::s_ModuleBitset[i]) continue;
        uintptr_t base = exl::util::GetModuleInfo((exl::util::ModuleIndex)i).m_Total.m_Start;
        uint32_t mod0 = *reinterpret_cast<uint32_t*>(base + 4);
        if (std::memcmp(reinterpret_cast<void*>(base + mod0), "MOD0", 4) != 0) continue;
        uintptr_t dyn = base + mod0 + *reinterpret_cast<int32_t*>(base + mod0 + 4);
        uintptr_t symtab = 0, strtab = 0;
        for (auto* d = reinterpret_cast<int64_t*>(dyn); d[0] != 0; d += 2) {
            if (d[0] == 6) symtab = base + d[1];
            if (d[0] == 5) strtab = base + d[1];
        }
        if (!symtab || !strtab || strtab <= symtab) continue;
        for (auto* s = reinterpret_cast<Elf64Sym*>(symtab); reinterpret_cast<uintptr_t>(s + 1) <= strtab; s++) {
            if (s->shndx != 0 && s->value != 0 && std::strcmp(reinterpret_cast<const char*>(strtab + s->name), wanted) == 0)
                return base + s->value;
        }
    }
    return 0;
}

template <class F> static bool Bind(F& fn, const char* mangled) {
    fn = reinterpret_cast<F>(FindExport(mangled));
    if (!fn) Logging.Log("[net] missing SDK function %s", mangled);
    return fn != nullptr;
}

/* nn::Result is a u32 in w0. */
using Res = uint32_t;

/* Horizon's native sockaddr_in (BSD layout: a length byte first). */
struct SockAddrIn { uint8_t len, family; uint16_t port; uint32_t addr; uint8_t zero[8]; };
/* Horizon's native addrinfo. */
struct AddrInfo { int32_t flags, family, socktype, protocol; uint32_t addrlen; uint32_t pad; char* canon; SockAddrIn* addr; AddrInfo* next; };

static Res (*nifmInitialize)();
static void (*nifmSubmitAndWait)();
static bool (*nifmAvailable)();
static Res (*sockInitialize)(void*, size_t, size_t, int);
static int (*sockSocket)(int, int, int);
static int (*sockConnect)(int, const void*, uint32_t);
static int64_t (*sockSend)(int, const void*, size_t, int);
static int64_t (*sockRecv)(int, void*, size_t, int);
static int (*sockClose)(int);
static int (*sockErrno)();
static int (*sockGetAddrInfo)(const char*, const char*, const void*, AddrInfo**);
static void (*sockFreeAddrInfo)(AddrInfo*);
struct PollFd { int fd; int16_t events, revents; };
static int (*sockPoll)(PollFd*, size_t, int);
static int (*sockSetOpt)(int, int, int, const void*, uint32_t);
static Res (*sslInitialize)();
static void (*sslContextCtor)(void*);
static Res (*sslContextCreate)(void*, int);
static void (*sslConnCtor)(void*);
static Res (*sslConnCreate)(void*, void*);
static Res (*sslSetSocket)(void*, int);
static Res (*sslSetHostName)(void*, const char*, uint32_t);
static Res (*sslSetIoMode)(void*, int);
static Res (*sslHandshake)(void*);
static Res (*sslWrite)(void*, const char*, int*, uint32_t);
static Res (*sslRead)(void*, char*, int*, uint32_t);
static void (*sslConnDestroy)(void*);
using ThreadFn = void (*)(void*);
static Res (*osCreateThread)(void*, ThreadFn, void*, void*, size_t, int);
static void (*osStartThread)(void*);

static bool BindAll() {
    bool ok = true;
    ok &= Bind(nifmInitialize, "_ZN2nn4nifm10InitializeEv");
    ok &= Bind(nifmSubmitAndWait, "_ZN2nn4nifm27SubmitNetworkRequestAndWaitEv");
    ok &= Bind(nifmAvailable, "_ZN2nn4nifm18IsNetworkAvailableEv");
    ok &= Bind(sockInitialize, "_ZN2nn6socket10InitializeEPvmmi");
    ok &= Bind(sockSocket, "_ZN2nn6socket6SocketEiii");
    if (!Bind(sockConnect, "_ZN2nn6socket7ConnectEiPKNS0_8SockAddrEj")) ok &= Bind(sockConnect, "_ZN2nn6socket7ConnectEiPK8sockaddrj");
    ok &= Bind(sockSend, "_ZN2nn6socket4SendEiPKvmi");
    ok &= Bind(sockRecv, "_ZN2nn6socket4RecvEiPvmi");
    ok &= Bind(sockClose, "_ZN2nn6socket5CloseEi");
    ok &= Bind(sockPoll, "_ZN2nn6socket4PollEP6pollfdmi");
    ok &= Bind(sockSetOpt, "_ZN2nn6socket10SetSockOptEiiiPKvj");
    ok &= Bind(sockErrno, "_ZN2nn6socket12GetLastErrnoEv");
    ok &= Bind(sockGetAddrInfo, "_ZN2nn6socket11GetAddrInfoEPKcS2_PKNS0_8AddrInfoEPPS3_");
    ok &= Bind(sockFreeAddrInfo, "_ZN2nn6socket12FreeAddrInfoEPNS0_8AddrInfoE");
    ok &= Bind(sslInitialize, "_ZN2nn3ssl10InitializeEv");
    ok &= Bind(sslContextCtor, "_ZN2nn3ssl7ContextC1Ev");
    ok &= Bind(sslContextCreate, "_ZN2nn3ssl7Context6CreateENS1_10SslVersionE");
    ok &= Bind(sslConnCtor, "_ZN2nn3ssl10ConnectionC1Ev");
    ok &= Bind(sslConnCreate, "_ZN2nn3ssl10Connection6CreateEPNS0_7ContextE");
    ok &= Bind(sslSetSocket, "_ZN2nn3ssl10Connection19SetSocketDescriptorEi");
    ok &= Bind(sslSetHostName, "_ZN2nn3ssl10Connection11SetHostNameEPKcj");
    ok &= Bind(sslSetIoMode, "_ZN2nn3ssl10Connection9SetIoModeENS1_6IoModeE");
    ok &= Bind(sslHandshake, "_ZN2nn3ssl10Connection11DoHandshakeEv");
    ok &= Bind(sslWrite, "_ZN2nn3ssl10Connection5WriteEPKcPij");
    ok &= Bind(sslRead, "_ZN2nn3ssl10Connection4ReadEPcPij");
    ok &= Bind(sslConnDestroy, "_ZN2nn3ssl10Connection7DestroyEv");
    ok &= Bind(osCreateThread, "_ZN2nn2os12CreateThreadEPNS0_10ThreadTypeEPFvPvES3_S3_mi");
    ok &= Bind(osStartThread, "_ZN2nn2os11StartThreadEPNS0_10ThreadTypeE");
    return ok;
}

/* ---- Logging: Ryujinx's log and a file on the SD card --------------------------------------- */

constexpr size_t kLogMax = 16384;
static char g_log[kLogMax];
static size_t g_logLen = 0;
static bool g_sdMounted = false;

static void FlushLog() {
    if (!g_sdMounted) return;
    nn::fs::CreateFile("sd:/xplay/net-log.txt", kLogMax); /* fails if it exists; fine */
    nn::fs::FileHandle h;
    if (nn::fs::OpenFile(&h, "sd:/xplay/net-log.txt", nn::fs::OpenMode_Write) != 0) return;
    static char page[kLogMax];
    std::memset(page, ' ', sizeof page);
    std::memcpy(page, g_log, g_logLen);
    nn::fs::WriteFile(h, 0, page, kLogMax, nn::fs::WriteOption::CreateOption(nn::fs::WriteOptionFlag_Flush));
    nn::fs::CloseFile(h);
}

static void Log(const char* fmt, ...) {
    char line[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    Logging.Log("[net] %s", line);
    size_t n = std::strlen(line);
    if (g_logLen + n + 1 < kLogMax) {
        std::memcpy(g_log + g_logLen, line, n);
        g_logLen += n;
        g_log[g_logLen++] = '\n';
    }
    FlushLog();
}

static uint64_t Ticks() { uint64_t t; asm volatile("mrs %0, cntpct_el0" : "=r"(t)); return t; }
static double Ms(uint64_t ticks) { return ticks / 19200.0; }

/* ---- 7a: plain TCP echo --------------------------------------------------------------------- */

static bool ParseIp(const char* s, uint32_t* out) {
    unsigned a, b, c, d;
    if (std::sscanf(s, "%u.%u.%u.%u", &a, &b, &c, &d) != 4 || a > 255 || b > 255 || c > 255 || d > 255) return false;
    *out = (a) | (b << 8) | (c << 16) | (d << 24); /* network order in memory on a little-endian CPU */
    return true;
}

static uint16_t Port(int p) { return (uint16_t)(((p & 0xff) << 8) | ((p >> 8) & 0xff)); }

static void TestTcp(const char* ip, int port) {
    Log("7a: TCP echo to %s:%d", ip, port);
    SockAddrIn sa{};
    sa.len = sizeof sa; sa.family = 2; sa.port = Port(port);
    if (!ParseIp(ip, &sa.addr)) { Log("7a: bad address"); return; }
    int fd = sockSocket(2, 1, 0);
    if (fd < 0) { Log("7a: socket() failed, errno %d", sockErrno()); return; }
    int rc = sockConnect(fd, &sa, sizeof sa);
    if (rc < 0) { Log("7a: connect() failed, errno %d", sockErrno()); sockClose(fd); return; }
    Log("7a: connected");
    double sum = 0, lo = 1e9, hi = 0;
    int ok = 0;
    for (int i = 0; i < 20; i++) {
        char msg[16], reply[16] = {};
        int n = snprintf(msg, sizeof msg, "PING %02d\n", i);
        uint64_t t0 = Ticks();
        if (sockSend(fd, msg, n, 0) != n) { Log("7a: send failed, errno %d", sockErrno()); break; }
        int64_t got = 0;
        while (got < n) {
            int64_t r = sockRecv(fd, reply + got, n - got, 0);
            if (r <= 0) { Log("7a: recv returned %ld, errno %d", (long)r, sockErrno()); got = -1; break; }
            got += r;
        }
        if (got != n || std::memcmp(msg, reply, n) != 0) break;
        double ms = Ms(Ticks() - t0);
        sum += ms; if (ms < lo) lo = ms; if (ms > hi) hi = ms;
        ok++;
    }
    Log("7a: %d/20 echoes, round trip ms: min %.2f avg %.2f max %.2f", ok, ok ? lo : 0, ok ? sum / ok : 0, ok ? hi : 0);
    Log("7a: %s", ok == 20 ? "PASS" : "FAIL");
    sockClose(fd);
}

/* ---- 7b: TLS WebSocket to the signaling server ---------------------------------------------- */

static uint64_t g_rng = 0;
static uint32_t Rand() { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 7; g_rng ^= g_rng << 17; return (uint32_t)(g_rng >> 16); }

static void Base64(const uint8_t* in, int n, char* out) {
    static const char t[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    int o = 0;
    for (int i = 0; i < n; i += 3) {
        uint32_t v = in[i] << 16 | (i + 1 < n ? in[i + 1] << 8 : 0) | (i + 2 < n ? in[i + 2] : 0);
        out[o++] = t[v >> 18 & 63]; out[o++] = t[v >> 12 & 63];
        out[o++] = i + 1 < n ? t[v >> 6 & 63] : '='; out[o++] = i + 2 < n ? t[v & 63] : '=';
    }
    out[o] = 0;
}

static void MakeUuid(char* out) {
    snprintf(out, 37, "%08x-%04x-4%03x-%04x-%08x%04x", Rand(), Rand() & 0xffff, Rand() & 0xfff,
             0x8000 | (Rand() & 0x3fff), Rand(), Rand() & 0xffff);
}

alignas(16) static uint8_t g_sslCtx[512];
alignas(16) static uint8_t g_sslConn[1024];

/* Read what's there; a "would block" result (module 123, description 204: 0x1987b) is retried for
 * up to 10 s, in case the connection isn't blocking. */
static bool SslReadSome(char* buf, int cap, int* got) {
    for (int tries = 0; tries < 1000; tries++) {
        *got = 0;
        Res r = sslRead(g_sslConn, buf, got, cap);
        if (r == 0) return *got > 0;
        if (r != 0x1987b) { Log("7b: ssl Read failed, result %#x", r); return false; }
        svcSleepThread(10ll * 1000 * 1000);
    }
    Log("7b: ssl Read kept returning would-block for 10 s");
    return false;
}

static void TestTls(const char* host, int port, const char* ipOverride) {
    Log("7b: TLS WebSocket to %s:%d", host, port);
    char portText[8];
    snprintf(portText, sizeof portText, "%d", port);
    SockAddrIn sa{};
    bool found = false;
    AddrInfo hints{};
    hints.family = 2; hints.socktype = 1;
    AddrInfo* res = nullptr;
    int rc = sockGetAddrInfo(host, portText, &hints, &res);
    if (rc == 0 && res) {
        for (AddrInfo* a = res; a; a = a->next) {
            if (a->addr && a->addr->family == 2) { sa = *a->addr; found = true; break; }
        }
        sockFreeAddrInfo(res);
    }
    else
        Log("7b: GetAddrInfo(%s) failed: %d, errno %d", host, rc, sockErrno());
    if (!found && ipOverride[0]) {
        sa = SockAddrIn{}; sa.len = sizeof sa; sa.family = 2;
        found = ParseIp(ipOverride, &sa.addr);
        Log("7b: using the address from net.txt: %s", ipOverride);
    }
    if (!found) { Log("7b: FAIL (no address for %s)", host); return; }
    Log("7b: resolved to %u.%u.%u.%u", sa.addr & 255, sa.addr >> 8 & 255, sa.addr >> 16 & 255, sa.addr >> 24);
    sa.port = Port(port);

    int fd = sockSocket(2, 1, 0);
    if (fd < 0) { Log("7b: socket() failed, errno %d", sockErrno()); return; }
    if (sockConnect(fd, &sa, sizeof sa) < 0) { Log("7b: connect() failed, errno %d", sockErrno()); sockClose(fd); return; }
    Log("7b: TCP connected");

    Res r = sslInitialize();
    Log("7b: ssl::Initialize -> %#x", r);
    std::memset(g_sslCtx, 0, sizeof g_sslCtx);
    std::memset(g_sslConn, 0, sizeof g_sslConn);
    sslContextCtor(g_sslCtx);
    r = sslContextCreate(g_sslCtx, 1 /* SslVersion_Auto */);
    Log("7b: Context::Create -> %#x", r);
    if (r != 0) { sockClose(fd); return; }
    sslConnCtor(g_sslConn);
    r = sslConnCreate(g_sslConn, g_sslCtx);
    Log("7b: Connection::Create -> %#x", r);
    if (r != 0) { sockClose(fd); return; }
    r = sslSetSocket(g_sslConn, fd);
    Log("7b: SetSocketDescriptor -> %#x", r);
    r = sslSetHostName(g_sslConn, host, (uint32_t)std::strlen(host));
    Log("7b: SetHostName -> %#x", r);
    r = sslSetIoMode(g_sslConn, 1 /* blocking */);
    Log("7b: SetIoMode(blocking) -> %#x", r);
    uint64_t t0 = Ticks();
    r = sslHandshake(g_sslConn);
    Log("7b: DoHandshake -> %#x (%.1f ms)", r, Ms(Ticks() - t0));
    if (r != 0) { Log("7b: FAIL (TLS handshake)"); sslConnDestroy(g_sslConn); sockClose(fd); return; }

    /* The WebSocket handshake. No Origin header: like desktop TF.EX, which the server always lets in. */
    char room[40], peer[40], key[32];
    MakeUuid(room); MakeUuid(peer);
    uint8_t raw[16];
    for (auto& b : raw) b = (uint8_t)Rand();
    Base64(raw, 16, key);
    char req[512];
    int reqLen = snprintf(req, sizeof req,
        "GET /room/%s?peer=%s HTTP/1.1\r\nHost: %s\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
        "Sec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\n\r\n", room, peer, host, key);
    int written = 0;
    r = sslWrite(g_sslConn, req, &written, (uint32_t)reqLen);
    Log("7b: sent the upgrade request: result %#x, %d of %d bytes", r, written, reqLen);
    if (r != 0 || written != reqLen) { Log("7b: FAIL (write)"); sslConnDestroy(g_sslConn); sockClose(fd); return; }

    /* Read the response headers, and whatever frame bytes follow in the same read. */
    static char in[4096];
    int have = 0;
    char* headerEnd = nullptr;
    while (have < (int)sizeof in - 1 && !headerEnd) {
        int got = 0;
        if (!SslReadSome(in + have, (int)sizeof in - 1 - have, &got)) break;
        have += got; in[have] = 0;
        headerEnd = std::strstr(in, "\r\n\r\n");
    }
    if (!headerEnd) { Log("7b: FAIL (no response headers; %d bytes)", have); sslConnDestroy(g_sslConn); sockClose(fd); return; }
    int statusLen = (int)(std::strchr(in, '\r') - in);
    Log("7b: response: %.*s", statusLen, in);
    if (std::strncmp(in, "HTTP/1.1 101", 12) != 0) { Log("7b: FAIL (not 101)"); sslConnDestroy(g_sslConn); sockClose(fd); return; }

    /* The first frame: text, unmasked from the server. */
    int start = (int)(headerEnd + 4 - in);
    auto need = [&](int n) {
        while (have - start < n) {
            int got = 0;
            if (!SslReadSome(in + have, (int)sizeof in - 1 - have, &got)) return false;
            have += got;
        }
        return true;
    };
    if (!need(2)) { Log("7b: FAIL (no first frame)"); sslConnDestroy(g_sslConn); sockClose(fd); return; }
    uint8_t op = (uint8_t)in[start], len = (uint8_t)in[start + 1] & 0x7f;
    if (!need(2 + len)) { Log("7b: FAIL (short frame)"); sslConnDestroy(g_sslConn); sockClose(fd); return; }
    Log("7b: first frame: opcode %#x, %d bytes: %.*s", op & 0xf, len, (int)len, in + start + 2);
    char payload[128] = {};
    std::memcpy(payload, in + start + 2, len < sizeof payload - 1 ? len : sizeof payload - 1);
    bool pass = (op & 0xf) == 1 && len < 126 && std::strstr(payload, "IdAssigned") != nullptr;
    Log("7b: %s", pass ? "PASS" : "FAIL (first message isn't IdAssigned)");

    /* Close politely: a masked close frame, status 1000. */
    uint8_t mask[4] = { 1, 2, 3, 4 };
    uint8_t closeFrame[8] = { 0x88, 0x82, mask[0], mask[1], mask[2], mask[3], (uint8_t)(0x03 ^ mask[0]), (uint8_t)(0xe8 ^ mask[1]) };
    sslWrite(g_sslConn, reinterpret_cast<char*>(closeFrame), &written, sizeof closeFrame);
    sslConnDestroy(g_sslConn);
    sockClose(fd);
}

/* ---- 7c: a WebSocket relay carrying rollback-sized traffic -------------------------------------
 * 60 packets a second of 100 bytes through the relay (tools/relay-server.mjs, or the real server)
 * to a peer that echoes each straight back (tools/relay-peer.mjs, or a browser on /echo.html).
 * Measures the round trip of every packet and the gaps between echoes: a lost packet stalls a TCP
 * connection and everything behind it, which rollback netcode dislikes. The pass line is the one in
 * VIABILITY-TESTS: p99 within 50 ms of the median, and no gap over 150 ms. */
namespace relay {
constexpr int kPkt = 100;
constexpr int kMaxSent = 20000;
static int g_fd = -1;
static bool g_tls = false;
static float g_rtt[kMaxSent];
static uint8_t g_seen[kMaxSent];
static float g_sorted[kMaxSent];
alignas(8) static uint8_t g_rx[8192];
static int g_rxLen = 0;

static bool Write(const uint8_t* p, int n) {
    int off = 0;
    uint64_t t0 = Ticks();
    while (off < n) {
        if (Ms(Ticks() - t0) > 5000) return false;
        if (g_tls) {
            int w = 0;
            Res r = sslWrite(g_sslConn, reinterpret_cast<const char*>(p) + off, &w, (uint32_t)(n - off));
            if (r == 0) { off += w; continue; }
            if (r == 0x1987b) { svcSleepThread(200000); continue; }
            return false;
        }
        int64_t w = sockSend(g_fd, p + off, n - off, 0);
        if (w <= 0) return false;
        off += (int)w;
    }
    return true;
}

/* > 0 bytes read, 0 nothing yet, -1 closed or failed. */
static int ReadNow(uint8_t* p, int cap) {
    if (g_tls) {
        int got = 0;
        Res r = sslRead(g_sslConn, reinterpret_cast<char*>(p), &got, (uint32_t)cap);
        if (r == 0) return got > 0 ? got : -1;
        return r == 0x1987b ? 0 : -1;
    }
    PollFd pf{ g_fd, 1 /* POLLIN */, 0 };
    if (sockPoll(&pf, 1, 0) <= 0) return 0;
    int64_t r = sockRecv(g_fd, p, cap, 0);
    return r > 0 ? (int)r : -1;
}

static bool SendFrame(uint8_t opcode, const uint8_t* payload, int n) {
    uint8_t f[6 + 125];
    uint8_t mask[4];
    for (auto& m : mask) m = (uint8_t)Rand();
    f[0] = 0x80 | opcode; f[1] = 0x80 | (uint8_t)n;
    std::memcpy(f + 2, mask, 4);
    for (int i = 0; i < n; i++) f[6 + i] = payload[i] ^ mask[i & 3];
    return Write(f, 6 + n);
}

static bool SendPacket(uint32_t seq) {
    uint8_t p[kPkt];
    for (int i = 0; i < kPkt; i++) p[i] = (uint8_t)(i * 7 + seq);
    std::memcpy(p, "XPNG", 4);
    std::memcpy(p + 4, &seq, 4);
    uint64_t t = Ticks();
    std::memcpy(p + 8, &t, 8);
    return SendFrame(2, p, kPkt);
}

static int g_received = 0, g_bigGaps = 0, g_pongs = 0;
static double g_maxGap = 0;
static uint64_t g_lastRx = 0;

/* Handles every whole frame in the buffer. False if the peer closed. */
static bool Process() {
    for (;;) {
        if (g_rxLen < 2) return true;
        uint8_t opcode = g_rx[0] & 0x0f;
        int len = g_rx[1] & 0x7f, off = 2;
        if (len == 126) { if (g_rxLen < 4) return true; len = g_rx[2] << 8 | g_rx[3]; off = 4; }
        else if (len == 127) return false;
        if (g_rxLen < off + len) return true;
        const uint8_t* payload = g_rx + off;
        if (opcode == 8) return false;
        if (opcode == 9 && len <= 125) { SendFrame(10, payload, len); g_pongs++; }
        if (opcode == 2 && len >= 16 && std::memcmp(payload, "XPNG", 4) == 0) {
            uint32_t seq; uint64_t sent;
            std::memcpy(&seq, payload + 4, 4);
            std::memcpy(&sent, payload + 8, 8);
            uint64_t now = Ticks();
            if (seq < (uint32_t)kMaxSent && !g_seen[seq]) {
                g_seen[seq] = 1;
                g_rtt[seq] = (float)Ms(now - sent);
                g_received++;
                if (g_lastRx) {
                    double gap = Ms(now - g_lastRx);
                    if (gap > g_maxGap) g_maxGap = gap;
                    if (gap > 150) g_bigGaps++;
                }
                g_lastRx = now;
            }
        }
        std::memmove(g_rx, g_rx + off + len, (size_t)(g_rxLen - off - len));
        g_rxLen -= off + len;
    }
}

static bool Pump() {
    int n = ReadNow(g_rx + g_rxLen, (int)sizeof g_rx - g_rxLen);
    if (n < 0) return false;
    g_rxLen += n;
    return Process();
}

static void Run(const char* host, int port, int tls, int seconds, const char* ipOverride) {
    Log("7c: relay test to %s:%d (%s), %d s, 60 packets/s of %d bytes", host, port, tls ? "TLS" : "plain", seconds, kPkt);
    if (seconds * 60 + 200 > kMaxSent) { Log("7c: too long (max %d s)", (kMaxSent - 200) / 60); return; }
    SockAddrIn sa{};
    bool found = ParseIp(host, &sa.addr);
    if (!found) {
        char portText[8];
        snprintf(portText, sizeof portText, "%d", port);
        AddrInfo hints{};
        hints.family = 2; hints.socktype = 1;
        AddrInfo* res = nullptr;
        if (sockGetAddrInfo(host, portText, &hints, &res) == 0 && res) {
            for (AddrInfo* a = res; a; a = a->next)
                if (a->addr && a->addr->family == 2) { sa = *a->addr; found = true; break; }
            sockFreeAddrInfo(res);
        }
        if (!found && ipOverride[0]) { found = ParseIp(ipOverride, &sa.addr); Log("7c: name resolution failed; using %s", ipOverride); }
    }
    if (!found) { Log("7c: FAIL (no address for %s)", host); return; }
    sa.len = sizeof sa; sa.family = 2; sa.port = Port(port);

    g_fd = sockSocket(2, 1, 0);
    if (g_fd < 0) { Log("7c: socket() failed, errno %d", sockErrno()); return; }
    int one = 1;
    sockSetOpt(g_fd, 6 /* IPPROTO_TCP */, 1 /* TCP_NODELAY */, &one, sizeof one);
    if (sockConnect(g_fd, &sa, sizeof sa) < 0) { Log("7c: connect() failed, errno %d", sockErrno()); sockClose(g_fd); return; }
    g_tls = tls != 0;
    if (g_tls) {
        sslInitialize();
        std::memset(g_sslCtx, 0, sizeof g_sslCtx);
        std::memset(g_sslConn, 0, sizeof g_sslConn);
        sslContextCtor(g_sslCtx);
        Res r = sslContextCreate(g_sslCtx, 1);
        if (r == 0) { sslConnCtor(g_sslConn); r = sslConnCreate(g_sslConn, g_sslCtx); }
        if (r == 0) r = sslSetSocket(g_sslConn, g_fd);
        if (r == 0) r = sslSetHostName(g_sslConn, host, (uint32_t)std::strlen(host));
        if (r == 0) r = sslSetIoMode(g_sslConn, 1 /* blocking, for the handshake */);
        if (r == 0) r = sslHandshake(g_sslConn);
        if (r != 0) { Log("7c: FAIL (TLS setup %#x)", r); sockClose(g_fd); return; }
    }

    /* The WebSocket upgrade (blocking). */
    char room[] = "relaytest", peer[40], key[32];
    MakeUuid(peer);
    uint8_t raw[16];
    for (auto& b : raw) b = (uint8_t)Rand();
    Base64(raw, 16, key);
    char req[512];
    int reqLen = snprintf(req, sizeof req,
        "GET /relay/%s?peer=%s HTTP/1.1\r\nHost: %s\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
        "Sec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\n\r\n", room, peer, host, key);
    if (!Write(reinterpret_cast<uint8_t*>(req), reqLen)) { Log("7c: FAIL (upgrade write)"); sockClose(g_fd); return; }
    static char head[2048];
    int have = 0;
    char* headerEnd = nullptr;
    uint64_t t0 = Ticks();
    while (!headerEnd && have < (int)sizeof head - 1 && Ms(Ticks() - t0) < 10000) {
        if (g_tls) {
            int got = 0;
            Res r = sslRead(g_sslConn, head + have, &got, (uint32_t)(sizeof head - 1 - have));
            if (r == 0 && got > 0) have += got;
            else if (r != 0 && r != 0x1987b) break;
            else svcSleepThread(1000000);
        } else {
            int64_t r = sockRecv(g_fd, head + have, sizeof head - 1 - have, 0);
            if (r <= 0) break;
            have += (int)r;
        }
        head[have] = 0;
        headerEnd = std::strstr(head, "\r\n\r\n");
    }
    if (!headerEnd || std::strncmp(head, "HTTP/1.1 101", 12) != 0) { Log("7c: FAIL (upgrade: %.40s)", have ? head : "no response"); sockClose(g_fd); return; }
    int extra = have - (int)(headerEnd + 4 - head);
    if (extra > 0 && extra <= (int)sizeof g_rx) { std::memcpy(g_rx, headerEnd + 4, (size_t)extra); g_rxLen = extra; }
    if (g_tls) sslSetIoMode(g_sslConn, 2 /* non-blocking from here */);
    Log("7c: joined room %s; waiting for the other peer's echo", room);

    /* The run. Packets go out at a steady 60 Hz (catching up if a sleep overshoots). */
    constexpr uint64_t kHz = 19200000, kInterval = kHz / 60;
    uint64_t start = Ticks(), next = start, end = start + (uint64_t)seconds * kHz, lastReport = start;
    uint32_t seq = 0;
    bool alive = true;
    while (alive && (Ticks() < end || seq == 0)) {
        uint64_t now = Ticks();
        while (now >= next && next < end && seq < (uint32_t)kMaxSent) {
            if (!SendPacket(seq++)) { alive = false; break; }
            next += kInterval;
        }
        if (!Pump()) alive = false;
        if (now - lastReport >= 10 * kHz) {
            lastReport = now;
            Log("7c: %d s: sent %u, echoed %d, longest gap so far %.0f ms", (int)((now - start) / kHz), seq, g_received, g_maxGap);
        }
        svcSleepThread(500000);
    }
    /* Stragglers. */
    uint64_t drain = Ticks() + 3 * kHz;
    while (alive && Ticks() < drain && g_received < (int)seq) { if (!Pump()) alive = false; svcSleepThread(500000); }
    SendFrame(8, reinterpret_cast<const uint8_t*>("\x03\xe8"), 2);
    if (g_tls) sslConnDestroy(g_sslConn);
    sockClose(g_fd);

    int n = 0;
    for (uint32_t i = 0; i < seq; i++) if (g_seen[i]) g_sorted[n++] = g_rtt[i];
    if (n == 0) { Log("7c: FAIL (nothing was echoed; is a peer in room %s?)", room); return; }
    std::sort(g_sorted, g_sorted + n);
    auto pct = [&](double p) { return g_sorted[std::min(n - 1, (int)(p * n))]; };
    float p50 = pct(0.50f);
    Log("7c: sent %u, echoed %d, lost %u%s", seq, n, seq - (unsigned)n, alive ? "" : " (connection ended early)");
    Log("7c: round trip ms: min %.2f p50 %.2f p90 %.2f p99 %.2f p99.9 %.2f max %.2f", g_sorted[0], p50, pct(0.90), pct(0.99), pct(0.999), g_sorted[n - 1]);
    Log("7c: p99 - p50 = %.2f ms; longest gap between echoes %.0f ms; gaps over 150 ms: %d", pct(0.99) - p50, g_maxGap, g_bigGaps);
    bool pass = alive && (int)seq == n && pct(0.99) - p50 < 50 && g_maxGap < 150;
    Log("7c: %s", pass ? "PASS" : "FAIL");
}
} // namespace relay

/* ---- Test 8: GGRS running inside the game process ------------------------------------------- */

static void TestGgrs(int ticks, int delay) {
    Log("8: GGRS loopback, %d ticks, delay %d polls", ticks, delay);
    static char report[256];
    uint64_t t0 = Ticks();
    int rc = ggrs_spike_run(ticks, delay, report, sizeof report);
    Log("8: %s (%.0f ms): %s", rc == 0 ? "PASS" : "FAIL", Ms(Ticks() - t0), report);
}

/* ---- The thread ------------------------------------------------------------------------------ */

alignas(0x1000) static uint8_t g_stack[0x20000];
alignas(0x40) static uint8_t g_thread[0x1c0];
alignas(0x4000) static uint8_t g_socketPool[0x600000];

static void ReadConfig(char* buf, size_t cap) {
    buf[0] = 0;
    if (!g_sdMounted) return;
    nn::fs::FileHandle h;
    if (nn::fs::OpenFile(&h, "sd:/xplay/net.txt", nn::fs::OpenMode_Read) != 0) return;
    long size = 0;
    nn::fs::GetFileSize(&size, h);
    if (size > (long)cap - 1) size = (long)cap - 1;
    nn::fs::ReadFile(h, 0, buf, size);
    nn::fs::CloseFile(h);
    buf[size] = 0;
}

static void ThreadMain(void*) {
    svcSleepThread(15ll * 1000 * 1000 * 1000); /* let the game boot first */
    g_rng = Ticks() | 1;
    g_sdMounted = nn::fs::MountSdCardForDebug("sd") == 0;
    Log("network probe: SD %s", g_sdMounted ? "mounted" : "NOT mounted");
    static char config[1024];
    ReadConfig(config, sizeof config);
    if (!config[0]) { Log("no sd:/xplay/net.txt (lines: 'tcp <ipv4> <port>', 'tls <host> <port>')"); return; }

    Res r = nifmInitialize();
    Log("nifm::Initialize -> %#x", r);
    nifmSubmitAndWait();
    Log("nifm: network available: %d", (int)nifmAvailable());
    r = sockInitialize(g_socketPool, sizeof g_socketPool, 0x20000, 14);
    Log("socket::Initialize -> %#x (nonzero may only mean the game already did it)", r);

    for (char* line = config; line && *line;) {
        char* nl = std::strchr(line, '\n');
        if (nl) *nl = 0;
        char kind[8], host[128], ip[32] = {};
        int port = 0, tlsFlag = 0, secs = 0;
        if (!std::strncmp(line, "relay ", 6)) {
            if (std::sscanf(line, "relay %127s %d %d %d %31s", host, &port, &tlsFlag, &secs, ip) >= 4) relay::Run(host, port, tlsFlag, secs, ip);
        }
        else if (std::sscanf(line, "%7s %127s %d %31s", kind, host, &port, ip) >= 3) {
            if (!std::strcmp(kind, "tcp")) TestTcp(host, port);
            else if (!std::strcmp(kind, "tls")) TestTls(host, port, ip);
            else if (!std::strcmp(kind, "ggrs")) TestGgrs(std::atoi(host), port);
        }
        line = nl ? nl + 1 : nullptr;
    }
    Log("network probe: done");
}

} // namespace net

/* Rust's stderr, line by line (see shim.c). */
extern "C" void net_raw_log(const char* p, size_t n) {
    char line[200];
    size_t i = 0;
    while (i < n) {
        size_t k = 0;
        while (i < n && p[i] != '\n' && k < sizeof line - 1) line[k++] = p[i++];
        if (i < n && p[i] == '\n') i++;
        line[k] = 0;
        if (k) net::Log("rust: %s", line);
    }
}

extern "C" void exl_main(void* x0, void* x1) {
    exl::hook::Initialize();
    Logging.Log("[net] network probe injected");
    if (!net::BindAll()) { Logging.Log("[net] some SDK functions are missing; not starting"); return; }
    int r = (int)net::osCreateThread(net::g_thread, net::ThreadMain, nullptr, net::g_stack, sizeof net::g_stack, 16);
    Logging.Log("[net] CreateThread -> %#x", r);
    if (r == 0) net::osStartThread(net::g_thread);
}

extern "C" NORETURN void exl_exception_entry() {
    EXL_ABORT("Default exception handler called!");
}
