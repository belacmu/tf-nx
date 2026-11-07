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
#include "hwlog.hpp"
#include "netws.hpp"
#include <lib/util/sys/modules.hpp>
#include <common.hpp>

#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <cstdlib>
#include <strings.h>
#include <algorithm>

/* The GGRS loopback test (VIABILITY-TESTS test 8), from ../ggrs-spike (Rust, built for the Switch
 * with a custom target and linked into this module). Two sessions in-process over an in-memory
 * socket with delay and loss; returns 0 on success and writes a one-line report. */
void net_main();
extern "C" void net_raw_log(const char* p, size_t n);
extern "C" int ggrs_spike_run(int ticks, int delay, char* out, size_t cap);

extern "C" void (*shim_random_bytes)(void*, size_t);   /* shim.c */

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
/* nn::socket::AddrInfo: ai_addr before ai_canonname (the Linux order, not BSD's). The console's lookup of the official server
 * (2026-11-01) put its only pointer at +0x18; read as BSD's order, every name lookup found no address, hidden until then by the
 * configured `ip` fallback. */
struct AddrInfo { int32_t flags, family, socktype, protocol; uint32_t addrlen; uint32_t pad; SockAddrIn* addr; char* canon; AddrInfo* next; };

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
/* Every TLS connection made an nn::ssl::Context and none was destroyed: after a couple of minutes of telemetry posts and lobby / room
 * sockets the process ran out of SSL contexts and every TLS setup failed with 0xe47b (2026-10-16: telemetry silent from ~1.5 min on, then
 * a lobby match's room socket couldn't connect: the console waited at the match start and the browser timed out). */
static Res (*sslContextDestroy)(void*);
using ThreadFn = void (*)(void*);
static Res (*osCreateThread)(void*, ThreadFn, void*, void*, size_t, int);
static void (*osStartThread)(void*);

/* UDP and the rest of WebRTC's needs (the ping measurement, ggrs_ffi's sockets): optional, so the lobby and relay work without them. */
static int (*sockBind)(int, const void*, uint32_t);
static int64_t (*sockSendTo)(int, const void*, size_t, int, const void*, uint32_t);
static int64_t (*sockRecvFrom)(int, void*, size_t, int, void*, uint32_t*);
static int (*sockGetSockName)(int, void*, uint32_t*);
static Res (*nifmPrimaryIp)(uint32_t*);
static void (*osRandomBytes)(void*, size_t);

static bool BindAll() {
    bool ok = true;
    Bind(sockBind, "_ZN2nn6socket4BindEiPK8sockaddrj");
    Bind(sockSendTo, "_ZN2nn6socket6SendToEiPKvmiPK8sockaddrj");
    Bind(sockRecvFrom, "_ZN2nn6socket8RecvFromEiPvmiP8sockaddrPj");
    Bind(sockGetSockName, "_ZN2nn6socket11GetSockNameEiP8sockaddrPj");
    Bind(nifmPrimaryIp, "_ZN2nn4nifm26GetCurrentPrimaryIpAddressEP7in_addr");
    if (Bind(osRandomBytes, "_ZN2nn2os19GenerateRandomBytesEPvm")) shim_random_bytes = osRandomBytes;   /* Rust's getrandom (DTLS keys, ICE credentials) */
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
    Bind(sslContextDestroy, "_ZN2nn3ssl7Context7DestroyEv");   /* (optional: without it the contexts leak, as before) */
    ok &= Bind(osCreateThread, "_ZN2nn2os12CreateThreadEPNS0_10ThreadTypeEPFvPvES3_S3_mi");
    ok &= Bind(osStartThread, "_ZN2nn2os11StartThreadEPNS0_10ThreadTypeE");
    return ok;
}

/* ---- Logging: the shared hw log (Ryujinx's log and sd:/xplay/hw-log.txt) ----------------------
 * hwlog.cpp's writer thread flushes it once a second (not this thread: it carries the GGRS packets), so after a hang the file shows
 * all but the last second. */
static void Log(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
static void Log(const char* fmt, ...) {
    char line[700];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    hw::Log("%s", line);   /* (no flush per line: the hw log's writer thread writes it once a second, off this transport thread) */
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
    if (r != 0) { Log("7b: FAIL (TLS handshake)"); sslConnDestroy(g_sslConn); if (sslContextDestroy) sslContextDestroy(g_sslCtx); sockClose(fd); return; }

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
    if (sslContextDestroy) sslContextDestroy(g_sslCtx);
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

static int64_t g_lastSendRet = 0;
static int g_sendRetries = 0;
static bool Write(const uint8_t* p, int n) {
    int off = 0;
    uint64_t t0 = 0;   /* when sending started failing (not when the call started: the thread can be descheduled for seconds) */
    while (off < n) {
        if (t0 && (int64_t)(Ticks() - t0) > 0 && Ms(Ticks() - t0) > 5000) {   /* signed: the counter read on another core can be a little behind */ Logging.Log("[hw] np: send still failing after 5 s (last return %lld, %d retries, %d of %d bytes sent)", (long long)g_lastSendRet, g_sendRetries, off, n); return false; }
        if (g_tls) {
            int w = 0;
            Res r = sslWrite(g_sslConn, reinterpret_cast<const char*>(p) + off, &w, (uint32_t)(n - off));
            if (r == 0) { off += w; continue; }
            if (r == 0x1987b) { if (!t0) t0 = Ticks(); svcSleepThread(200000); continue; }
            return false;
        }
        int64_t w = sockSend(g_fd, p + off, n - off, 0);
        g_lastSendRet = w;
        if (w < 0) { if (!t0) t0 = Ticks(); g_sendRetries++; svcSleepThread(200000); continue; }   /* EAGAIN / EINTR on the non-blocking socket: retry (gives up after 5 s) */
        if (w == 0) { Logging.Log("[hw] np: send returned 0 (%d of %d bytes sent)", off, n); return false; }
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
    if (cap <= 0 || sockPoll(&pf, 1, 0) <= 0) return 0;
    int64_t r = sockRecv(g_fd, p, cap, 0);
    static uint64_t errSince = 0;
    if (r > 0) { errSince = 0; return (int)r; }
    if (r == 0) return -1;                                   /* orderly close */
    if (!errSince) errSince = Ticks();                       /* a transient error (EAGAIN, EINTR): retry, up to 5 s */
    return (int64_t)(Ticks() - errSince) > 0 && Ms(Ticks() - errSince) > 5000 ? -1 : 0;
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

/* Connects to the relay (name or IPv4, plain or TLS) and does the WebSocket upgrade for /relay/<room>?peer=<uuid>. */
static bool Connect(const char* host, int port, int tls, const char* room, const char* peerIn, const char* ipOverride) {
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
        if (!found && ipOverride[0]) { found = ParseIp(ipOverride, &sa.addr); Log("conn: name resolution failed; using %s", ipOverride); }
    }
    if (!found) { Log("conn: FAIL (no address for %s)", host); return false; }
    sa.len = sizeof sa; sa.family = 2; sa.port = Port(port);

    g_fd = sockSocket(2, 1, 0);
    if (g_fd < 0) { Log("conn: socket() failed, errno %d", sockErrno()); return false; }
    int one = 1;
    sockSetOpt(g_fd, 6 /* IPPROTO_TCP */, 1 /* TCP_NODELAY */, &one, sizeof one);
    if (sockConnect(g_fd, &sa, sizeof sa) < 0) { Log("conn: connect() failed, errno %d", sockErrno()); sockClose(g_fd); return false; }
    g_tls = tls != 0;
    if (g_tls) {
        sslInitialize();
        std::memset(g_sslCtx, 0, sizeof g_sslCtx);
        std::memset(g_sslConn, 0, sizeof g_sslConn);
        sslContextCtor(g_sslCtx);
        Res r = sslContextCreate(g_sslCtx, 1);
        bool ctx = r == 0, conn = false;
        if (r == 0) { sslConnCtor(g_sslConn); r = sslConnCreate(g_sslConn, g_sslCtx); conn = r == 0; }
        if (r == 0) r = sslSetSocket(g_sslConn, g_fd);
        if (r == 0) r = sslSetHostName(g_sslConn, host, (uint32_t)std::strlen(host));
        if (r == 0) r = sslSetIoMode(g_sslConn, 1 /* blocking, for the handshake */);
        if (r == 0) r = sslHandshake(g_sslConn);
        if (r != 0) {
            Log("conn: FAIL (TLS setup %#x)", r);
            if (conn) sslConnDestroy(g_sslConn);
            if (ctx && sslContextDestroy) sslContextDestroy(g_sslCtx);
            sockClose(g_fd); return false;
        }
    }

    /* The WebSocket upgrade (blocking). */
    char peer[40], key[32];
    if (peerIn && peerIn[0] && std::strcmp(peerIn, "-")) std::snprintf(peer, sizeof peer, "%s", peerIn); else MakeUuid(peer);
    uint8_t raw[16];
    for (auto& b : raw) b = (uint8_t)Rand();
    Base64(raw, 16, key);
    char req[512];
    int reqLen = snprintf(req, sizeof req,
        "GET /relay/%s?peer=%s HTTP/1.1\r\nHost: %s\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
        "Sec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\n\r\n", room, peer, host, key);
    if (!Write(reinterpret_cast<uint8_t*>(req), reqLen)) { Log("conn: FAIL (upgrade write)"); sockClose(g_fd); return false; }
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
    if (!headerEnd || std::strncmp(head, "HTTP/1.1 101", 12) != 0) { Log("conn: FAIL (upgrade: %.40s)", have ? head : "no response"); sockClose(g_fd); return false; }
    int extra = have - (int)(headerEnd + 4 - head);
    if (extra > 0 && extra <= (int)sizeof g_rx) { std::memcpy(g_rx, headerEnd + 4, (size_t)extra); g_rxLen = extra; }
    if (g_tls) sslSetIoMode(g_sslConn, 2 /* non-blocking from here */);
    Log("conn: joined room %s as %s", room, peer);

    return true;
}

static void Run(const char* host, int port, int tls, int seconds, const char* ipOverride) {
    Log("7c: relay test to %s:%d (%s), %d s, 60 packets/s of %d bytes", host, port, tls ? "TLS" : "plain", seconds, kPkt);
    if (seconds * 60 + 200 > kMaxSent) { Log("7c: too long (max %d s)", (kMaxSent - 200) / 60); return; }
    const char* room = "relaytest";
    if (!Connect(host, port, tls, room, nullptr, ipOverride)) return;

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
    if (g_tls) { sslConnDestroy(g_sslConn); if (sslContextDestroy) sslContextDestroy(g_sslCtx); }
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

/* ---- The Rust library's WebRTC mesh (tfx-rtc, ggrs-switch/src/rtc.rs) and the address helpers, for TF.EX's ping measurement and
 * ggrs_ffi's sockets below. (The match transport that lived here, GGRS packets over the relay or this mesh through rings, was
 * replaced by ggrs_ffi's own sockets (ffisock) and is gone.) ---- */
namespace np {
extern "C" {
void* tfx_rtc_new(uint32_t ip, uint16_t port);
void tfx_rtc_free(void*);
void tfx_rtc_stun(void*, uint32_t ip, uint16_t port, const uint8_t* txid);
int tfx_rtc_reflexive(void*, uint32_t* ip, uint16_t* port);
void tfx_rtc_signal_in(void*, const uint8_t* text, int32_t len);
int32_t tfx_rtc_signal_out(void*, uint8_t* buf, int32_t cap);
void tfx_rtc_keepalive(void*);
void tfx_rtc_udp_in(void*, uint32_t sip, uint16_t sport, uint32_t dip, uint16_t dport, const uint8_t* d, int32_t len);
int32_t tfx_rtc_udp_out(void*, uint32_t* dip, uint16_t* dport, uint8_t* buf, int32_t cap);
int32_t tfx_rtc_drive(void*);
int32_t tfx_rtc_event(void*, char* peer, int32_t peerCap, uint8_t* buf, int32_t cap, int32_t* len);
int32_t tfx_rtc_send(void*, const char* peer, const uint8_t* d, int32_t len);
int32_t tfx_rtc_log(void*, char* buf, int32_t cap);
int32_t tfx_rtc_stats(void*, uint64_t* out, int32_t n);
}
/* the ping measurement's bind address and STUN server (ping_measurement_start sets them: the match sockets' settings) */
static char g_bindIp[32], g_stunHost[64], g_stunIp[32];
static int g_stunPort = 0;

static uint32_t HostOrder(uint32_t netOrderInMemory) { return __builtin_bswap32(netOrderInMemory); }
static bool Resolve(const char* host, const char* ipOverride, uint32_t* addr) {
    if (ParseIp(host, addr)) return true;
    AddrInfo hints{}; hints.family = 2; hints.socktype = 2;
    AddrInfo* res = nullptr;
    bool found = false;
    if (sockGetAddrInfo(host, nullptr, &hints, &res) == 0 && res) {
        for (AddrInfo* a = res; a && !found; a = a->next) if (a->addr && a->addr->family == 2 && a->addr->addr) { *addr = a->addr->addr; found = true; }
        sockFreeAddrInfo(res);
    }
    return found || (ipOverride && ipOverride[0] && ParseIp(ipOverride, addr));
}
} // namespace np

/* ---- TF.EX's standalone ping measurement (ggrs_ffi ping_measurement_start/stop/stats, direct: isRelay false), for the lobby's
 * pings (MatchmakingService.StartOrStopPingMeasurementIfNeeded): its own thread, UDP socket, tfx-rtc mesh and signaling WebSocket
 * <server>/ping_measurement/<room>?peer=<own> (a matchbox room of its own on the server), PING/PONG and the stats in
 * ggrs-switch/src/ping_measurement.rs. Separate from the match's sockets (ffisock; TF.EX's is a separate socket too); it runs in the lobby
 * screen only, so the two rarely overlap. ---- */
namespace pm {
struct Stats { int32_t rtt, spike, lossPercent, samples; };   /* ggrs-switch PingStats */
extern "C" {
void* tfx_pm_new();
void tfx_pm_free(void*);
void tfx_pm_disconnected(const void*, const char* peer);
int32_t tfx_pm_packet(const void*, const char* peer, const uint8_t* data, int32_t len, uint64_t now_us, uint8_t* reply, int32_t cap);
int32_t tfx_pm_ping_due(const void*, uint64_t now_us, uint8_t* out, int32_t cap);
void tfx_pm_pinged(const void*, const char* peer, uint64_t now_us, int32_t sent);
void tfx_pm_stats(const void*, const char* peer, Stats* out);
}
static volatile int g_want = 0;          /* 1 = measure g_url, 0 = stop */
static volatile int g_running = 0;
static volatile uint32_t g_gen = 0;      /* bumped by every start: the thread restarts on a new URL */
static char g_host[128], g_path[200], g_ip[32];
static int g_port = 0, g_tls = 0;
static void* volatile g_pm = nullptr;    /* the current measurement (read by the game thread's stats) */
static void* g_old = nullptr;            /* the previous one, freed at the next start (nobody reads it by then) */
static constexpr int kPeers = 8;
static char g_connected[kPeers][64];
alignas(0x1000) static uint8_t g_stack[0x40000];   /* DTLS and SDP run here */
static uint64_t NowUs() { return Ticks() / 192 * 10; }   /* 19.2 MHz -> microseconds */

static void RunOnce(uint32_t gen) {
    SockAddrIn sa{}; sa.len = sizeof sa; sa.family = 2;
    if (np::g_bindIp[0]) ParseIp(np::g_bindIp, &sa.addr); else if (nifmPrimaryIp) nifmPrimaryIp(&sa.addr);
    int fd = sockSocket(2, 2, 0);
    uint32_t sl = sizeof sa;
    if (fd < 0 || sockBind(fd, &sa, sizeof sa) != 0 || sockGetSockName(fd, &sa, &sl) != 0) {
        Log("pm: UDP socket failed (errno %d)", sockErrno()); if (fd >= 0) sockClose(fd); return;
    }
    uint32_t self = np::HostOrder(sa.addr); uint16_t selfPort = Port(sa.port);
    void* rtc = np::tfx_rtc_new(self, selfPort);
    void* pmx = tfx_pm_new();
    if (!rtc || !pmx) { if (rtc) np::tfx_rtc_free(rtc); if (pmx) tfx_pm_free(pmx); sockClose(fd); return; }
    if (g_old) tfx_pm_free(g_old);
    g_old = g_pm;
    __atomic_store_n(&g_pm, pmx, __ATOMIC_RELEASE);
    uint32_t stun = 0;
    if (np::g_stunHost[0] && np::Resolve(np::g_stunHost, np::g_stunIp, &stun)) {
        uint8_t txid[12]; for (auto& b : txid) b = (uint8_t)Rand();
        if (osRandomBytes) osRandomBytes(txid, sizeof txid);
        np::tfx_rtc_stun(rtc, np::HostOrder(stun), (uint16_t)np::g_stunPort, txid);
    }
    netws::Conn* ws = netws::Open(g_host, g_port, g_tls != 0, g_path, g_ip, netws::Rx::Small);
    if (!ws) { Log("pm: signaling connect failed (%s)", g_path); np::tfx_rtc_free(rtc); sockClose(fd); return; }
    Log("pm: Ping measurement started (%s, UDP %u)", g_path, selfPort);
    static uint8_t buf[16 * 1024];
    static char line[256];
    char peer[64];
    int nConnected = 0;
    uint64_t lastKeep = Ticks();
    uint32_t loops = 0;
    while (g_want && g_gen == gen) {
        PollFd both[2] = { { fd, 1, 0 }, { netws::Fd(ws), 1, 0 } };
        sockPoll(both, 2, 0);
        bool wsReady = (both[1].revents & 1) || (loops % 50 == 0) || netws::Tls(ws);
        uint8_t op; const uint8_t* pl; int len;
        bool closed = false;
        for (int k = 0; k < 16 && wsReady; k++) {
            int r = netws::Next(ws, &op, &pl, &len);
            if (r < 0) { closed = true; break; }
            if (r == 0) break;
            if (op == 1) np::tfx_rtc_signal_in(rtc, pl, len);
        }
        if (closed) { Log("pm: Ping measurement signaling closed"); break; }
        for (int k = 0; k < 64 && (both[0].revents & 1); k++) {
            if (k > 0) { PollFd pf{ fd, 1, 0 }; if (sockPoll(&pf, 1, 0) <= 0) break; }
            SockAddrIn from{}; uint32_t fl = sizeof from;
            int64_t n = sockRecvFrom(fd, buf, sizeof buf, 0, &from, &fl);
            if (n <= 0) break;
            np::tfx_rtc_udp_in(rtc, np::HostOrder(from.addr), Port(from.port), self, selfPort, buf, (int32_t)n);
        }
        np::tfx_rtc_drive(rtc);
        int32_t elen = 0;
        for (int e; (e = np::tfx_rtc_event(rtc, peer, sizeof peer, buf, sizeof buf, &elen)) != 0;) {
            if (e == 2) {   /* PeerState::Connected */
                Log("pm: Ping measurement connected to %s", peer);
                if (nConnected < kPeers) std::snprintf(g_connected[nConnected++], sizeof g_connected[0], "%s", peer);
            } else if (e == 3) {   /* PeerState::Disconnected */
                Log("pm: Ping measurement lost %s", peer);
                tfx_pm_disconnected(pmx, peer);
                for (int i = 0; i < nConnected; i++) if (!std::strcmp(g_connected[i], peer)) { std::memmove(g_connected[i], g_connected[i + 1], (size_t)(nConnected - i - 1) * sizeof g_connected[0]); nConnected--; break; }
            } else if (e == 4 && elen > 0) {
                uint8_t reply[16];
                int32_t n = tfx_pm_packet(pmx, peer, buf, elen, NowUs(), reply, sizeof reply);
                if (n > 0) np::tfx_rtc_send(rtc, peer, reply, n);
            }
        }
        uint8_t ping[16];
        uint64_t now = NowUs();
        if (tfx_pm_ping_due(pmx, now, ping, sizeof ping))
            for (int i = 0; i < nConnected; i++) tfx_pm_pinged(pmx, g_connected[i], now, np::tfx_rtc_send(rtc, g_connected[i], ping, 9));
        for (int32_t n; (n = np::tfx_rtc_signal_out(rtc, buf, sizeof buf)) > 0;) netws::SendText(ws, reinterpret_cast<const char*>(buf), n);
        uint32_t dip; uint16_t dport;
        for (int32_t n; (n = np::tfx_rtc_udp_out(rtc, &dip, &dport, buf, sizeof buf)) > 0;) {
            SockAddrIn to{}; to.len = sizeof to; to.family = 2; to.addr = __builtin_bswap32(dip); to.port = Port(dport);
            sockSendTo(fd, buf, (size_t)n, 0, &to, sizeof to);
        }
        while (np::tfx_rtc_log(rtc, line, sizeof line)) { if (!std::strstr(line, "ICE")) continue; Log("pm: %s", line); }
        if (Ms(Ticks() - lastKeep) > 10000) { lastKeep = Ticks(); np::tfx_rtc_keepalive(rtc); }
        loops++;
        svcSleepThread(2000000);   /* POLL_INTERVAL 2 ms */
    }
    netws::Close(ws);
    np::tfx_rtc_free(rtc);
    sockClose(fd);
    Log("pm: Ping measurement stopped");
}
static void Thread(void*) {
    for (;;) {
        if (!g_want) { g_running = 0; svcSleepThread(50000000); continue; }
        g_running = 1;
        uint32_t gen = g_gen;
        RunOnce(gen);
        while (g_want && g_gen == gen) svcSleepThread(1000000000);   /* failed or closed: wait for the next start (TF.EX: until restarted) */
    }
}
static bool g_threadStarted = false;
} // namespace pm

/* ---- TF.EX's ggrs_ffi sockets (hw.txt "ggrsffi 1", docs/ws-ggrs-ffi.md): ggrs-ffi's matchbox_socket stand-in
 * (ggrs-ffi-switch/matchbox-switch) only queues; this thread does the I/O of every socket it opens: the room's WebSocket (the url
 * ggrs-ffi gives: TF.EX's room or ping-measurement url), one UDP socket, the
 * mesh driven through netplay_switch_socket_*. TFEX-DIFF: the console's sockets (nn::socket) instead of matchbox's own. ---- */
namespace ffisock {
extern "C" {
int32_t netplay_switch_socket_next_open(char* url, int32_t cap);
int32_t netplay_switch_socket_bind(int32_t h, uint32_t ip, uint16_t port);
void netplay_switch_socket_stun(int32_t h, uint32_t ip, uint16_t port, const uint8_t* txid);
void netplay_switch_socket_signal_in(int32_t h, const uint8_t* text, int32_t len);
int32_t netplay_switch_socket_signal_out(int32_t h, uint8_t* buf, int32_t cap);
void netplay_switch_socket_keepalive(int32_t h);
void netplay_switch_socket_udp_in(int32_t h, uint32_t sip, uint16_t sport, uint32_t dip, uint16_t dport, const uint8_t* d, int32_t len);
int32_t netplay_switch_socket_udp_out(int32_t h, uint32_t* dip, uint16_t* dport, uint8_t* buf, int32_t cap);
int32_t netplay_switch_socket_drive(int32_t h);
void netplay_switch_socket_closed(int32_t h, int32_t failed, const uint8_t* msg, int32_t len);
int32_t netplay_switch_socket_wants_close(int32_t h);
void netplay_switch_socket_release(int32_t h);
int32_t netplay_switch_socket_log(int32_t h, char* buf, int32_t cap);
}
constexpr int kSlots = 4;   /* the match's socket and the ping measurement's, with room to spare (pools: full = a logged refusal) */
struct Slot { int32_t h; int fd; netws::Conn* ws; uint32_t self; uint16_t selfPort; uint64_t lastKeep; bool wsGone; };
static Slot g_slots[kSlots];
static int g_used = 0, g_peak = 0;
static char g_bindIp[32], g_stunHost[64], g_stunIp[32], g_serverIp[32];
static int g_stunPort = 0;
static bool g_started = false;
alignas(0x1000) static uint8_t g_stack[0x40000];   /* DTLS and SDP run here */

static void Refuse(int32_t h, const char* why) {
    Log("ffisock: socket %d not opened: %s", h, why);
    netplay_switch_socket_closed(h, 1, reinterpret_cast<const uint8_t*>(why), (int32_t)std::strlen(why));
}

/* ws[s]://host[:port]/path?query */
static bool ParseUrl(const char* url, char* host, int hostCap, int* port, bool* tls, char* path, int pathCap) {
    const char* p = url;
    if (!std::strncmp(p, "wss://", 6)) { *tls = true; *port = 443; p += 6; }
    else if (!std::strncmp(p, "ws://", 5)) { *tls = false; *port = 80; p += 5; }
    else return false;
    const char* slash = std::strchr(p, '/');
    const char* end = slash ? slash : p + std::strlen(p);
    const char* colon = static_cast<const char*>(std::memchr(p, ':', (size_t)(end - p)));
    const char* hostEnd = colon ? colon : end;
    if (hostEnd - p <= 0 || hostEnd - p >= hostCap) return false;
    std::memcpy(host, p, (size_t)(hostEnd - p)); host[hostEnd - p] = 0;
    if (colon) *port = std::atoi(colon + 1);
    std::snprintf(path, (size_t)pathCap, "%s", slash ? slash : "/");
    return true;
}

static void Open(int32_t h, const char* url) {
    int free = -1;
    for (int i = 0; i < kSlots; i++) if (!g_slots[i].h) { free = i; break; }
    if (free < 0) { Log("ffisock: all %d slots in use (pool full)", kSlots); Refuse(h, "no socket slot left"); return; }
    char host[128], path[256]; int port = 0; bool tls = false;
    if (!ParseUrl(url, host, sizeof host, &port, &tls, path, sizeof path)) { Refuse(h, "bad url"); return; }
    SockAddrIn sa{}; sa.len = sizeof sa; sa.family = 2;
    if (g_bindIp[0]) ParseIp(g_bindIp, &sa.addr); else if (nifmPrimaryIp) nifmPrimaryIp(&sa.addr);
    int fd = sockSocket(2, 2 /* SOCK_DGRAM */, 0);
    uint32_t sl = sizeof sa;
    if (fd < 0 || sockBind(fd, &sa, sizeof sa) != 0 || sockGetSockName(fd, &sa, &sl) != 0) {
        if (fd >= 0) sockClose(fd);
        Refuse(h, "UDP socket failed"); return;
    }
    uint32_t self = np::HostOrder(sa.addr); uint16_t selfPort = Port(sa.port);
    if (!netplay_switch_socket_bind(h, self, selfPort)) { sockClose(fd); Refuse(h, "bad host candidate"); return; }
    uint32_t stun = 0;
    if (g_stunHost[0] && np::Resolve(g_stunHost, g_stunIp, &stun)) {
        uint8_t txid[12]; for (auto& b : txid) b = (uint8_t)Rand();
        if (osRandomBytes) osRandomBytes(txid, sizeof txid);
        netplay_switch_socket_stun(h, np::HostOrder(stun), (uint16_t)g_stunPort, txid);
    }
    netws::Conn* ws = netws::Open(host, port, tls, path, g_serverIp, netws::Rx::Small);
    if (!ws) { sockClose(fd); Refuse(h, "signaling connect failed"); return; }
    g_slots[free] = Slot{ h, fd, ws, self, selfPort, Ticks(), false };
    if (++g_used > g_peak) g_peak = g_used;
    Log("ffisock: socket %d open: %s (UDP %u), %d of %d slots in use (peak %d)", h, path, selfPort, g_used, kSlots, g_peak);
}

static void CloseSlot(Slot& s) {
    if (s.ws) netws::Close(s.ws);
    sockClose(s.fd);
    netplay_switch_socket_release(s.h);
    Log("ffisock: socket %d closed", s.h);
    s = Slot{};
    g_used--;
}

/* One pass over a socket's WebSocket, UDP socket and mesh; true if anything moved. */
static bool Pass(Slot& s) {
    static uint8_t buf[16 * 1024];
    static char line[256];
    bool busy = false;
    PollFd both[2] = { { s.fd, 1, 0 }, { s.ws ? netws::Fd(s.ws) : -1, 1, 0 } };
    sockPoll(both, s.ws ? 2 : 1, 0);
    if (s.ws && ((both[1].revents & 1) || netws::Tls(s.ws))) {
        uint8_t op; const uint8_t* pl; int len;
        for (int k = 0; k < 16; k++) {
            int r = netws::Next(s.ws, &op, &pl, &len);
            if (r < 0) {
                static const char kWhy[] = "signaling closed";
                Log("ffisock: socket %d: %s", s.h, kWhy);
                netplay_switch_socket_closed(s.h, 0, reinterpret_cast<const uint8_t*>(kWhy), (int32_t)sizeof kWhy - 1);
                netws::Close(s.ws); s.ws = nullptr; s.wsGone = true;
                break;
            }
            if (r == 0) break;
            if (op == 1) netplay_switch_socket_signal_in(s.h, pl, len);
            busy = true;
        }
    }
    for (int k = 0; k < 256 && (both[0].revents & 1); k++) {   /* (a poll before each further recvfrom) */
        if (k > 0) { PollFd pf{ s.fd, 1, 0 }; if (sockPoll(&pf, 1, 0) <= 0) break; }
        SockAddrIn from{}; uint32_t fl = sizeof from;
        int64_t n = sockRecvFrom(s.fd, buf, sizeof buf, 0, &from, &fl);
        if (n <= 0) break;
        netplay_switch_socket_udp_in(s.h, np::HostOrder(from.addr), Port(from.port), s.self, s.selfPort, buf, (int32_t)n);
        busy = true;
    }
    netplay_switch_socket_drive(s.h);
    for (int32_t n; (n = netplay_switch_socket_signal_out(s.h, buf, sizeof buf)) > 0;) {
        if (s.ws) netws::SendText(s.ws, reinterpret_cast<const char*>(buf), n);
        busy = true;
    }
    uint32_t dip; uint16_t dport;
    for (int32_t n; (n = netplay_switch_socket_udp_out(s.h, &dip, &dport, buf, sizeof buf)) > 0;) {
        SockAddrIn to{}; to.len = sizeof to; to.family = 2; to.addr = __builtin_bswap32(dip); to.port = Port(dport);
        sockSendTo(s.fd, buf, (size_t)n, 0, &to, sizeof to);
        busy = true;
    }
    while (netplay_switch_socket_log(s.h, line, sizeof line)) Log("ffisock %d: %s", s.h, line);
    if (s.ws && Ms(Ticks() - s.lastKeep) > 10000) { s.lastKeep = Ticks(); netplay_switch_socket_keepalive(s.h); }
    return busy;
}

static void Thread(void*) {
    static char url[512];
    for (;;) {
        for (int32_t h; (h = netplay_switch_socket_next_open(url, sizeof url)) > 0;) Open(h, url);
        bool busy = false;
        for (auto& s : g_slots) {
            if (!s.h) continue;
            if (netplay_switch_socket_wants_close(s.h)) { CloseSlot(s); continue; }
            busy |= Pass(s);
        }
        svcSleepThread(busy ? 0 : (g_used ? 500000 : 5000000));
    }
}
} // namespace ffisock



/* ---- Test 8: GGRS running inside the game process ------------------------------------------- */

static void TestGgrs(int ticks, int delay) {
    Log("8: GGRS loopback, %d ticks, delay %d polls", ticks, delay);
    static char report[256];
    uint64_t t0 = Ticks();
    int rc = ggrs_spike_run(ticks, delay, report, sizeof report);
    Log("8: %s (%.0f ms): %s", rc == 0 ? "PASS" : "FAIL", Ms(Ticks() - t0), report);
}

/* ---- The thread ------------------------------------------------------------------------------ */

alignas(0x1000) static uint8_t g_stack[0x40000];   /* (WebRTC's DTLS and SDP run on this thread too) */
alignas(0x40) static uint8_t g_thread[0x1c0];
alignas(0x4000) static uint8_t g_socketPool[0x600000];

static void ReadConfig(char* buf, size_t cap) {
    buf[0] = 0;
    if (!hw::MountSd()) return;
    nn::fs::FileHandle h;
    if (nn::fs::OpenFile(&h, "sd:/xplay/net.txt", nn::fs::OpenMode_Read) != 0) return;
    long size = 0;
    nn::fs::GetFileSize(&size, h);
    if (size > (long)cap - 1) size = (long)cap - 1;
    nn::fs::ReadFile(h, 0, buf, size);
    nn::fs::CloseFile(h);
    buf[size] = 0;
}

static void RunStages() {
    static char config[1024];
    ReadConfig(config, sizeof config);
    if (!config[0]) { Log("net: no sd:/xplay/net.txt (lines: 'tcp <ipv4> <port>', 'tls <host> <port>', 'relay ...', 'ggrs <ticks> <delay>')"); return; }

    netws::EnsureNetwork();   /* the one-time nifm/socket/ssl init, shared with the lobby and devupdate (a second socket::Initialize aborts) */

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

/* The network thread: the stages in net.txt, then it only writes the shared log to the SD card. */
static void ThreadMain(void*) {
    svcSleepThread(15ll * 1000 * 1000 * 1000); /* let the game boot first */
    g_rng = Ticks() | 1;
    Log("network probe: SD %s", hw::MountSd() ? "mounted" : "NOT mounted");
    RunStages();
    for (int n = 0;; n++) { if (n % 20 == 0) hw::Flush(); svcSleepThread(50ll * 1000 * 1000); }
}

/* ---- netws: WebSocket client connections for the lobby and the match relay (netws.hpp) -------------------------------- */
namespace ws {
struct Conn {
    bool used = false, tls = false;
    bool upgraded = false;      /* the WebSocket handshake succeeded: Close sends a close frame first */
    int fd = -1;
    alignas(16) uint8_t sslCtx[512];
    alignas(16) uint8_t sslConn[1024];
    uint8_t* rx = nullptr;
    int cap = 0, len = 0;
    uint64_t skip = 0;          /* bytes still to drop of a frame bigger than the buffer */
    int consumed = 0;           /* the message handed out by Next, removed on the next call */
    uint64_t errSince = 0;
    uint32_t skipped = 0;
};
alignas(16) static uint8_t g_rxBig[512 * 1024];
/* connection slots: the lobby socket, devupdate, the lobby's ping measurement and its relay twin, the match's room socket,
 * LocalGuest; 4 slots / 3 small buffers left a quick play match's room socket without one (console 2026-10-17 12:34: "no free connection
 * slot", the match never started) */
constexpr int kConns = 8, kSmall = 7;
alignas(16) static uint8_t g_rxSmall[kSmall][16 * 1024];
static Conn g_conns[kConns];
static bool g_bigUsed = false, g_smallUsed[kSmall];
static volatile int g_netState = 0;   /* 0 not tried, 1 ready, 2 failed */
static volatile int g_netLock = 0;

static bool Write(Conn* c, const uint8_t* p, int n) {
    int off = 0;
    uint64_t t0 = 0;
    while (off < n) {
        if (t0 && (int64_t)(Ticks() - t0) > 0 && Ms(Ticks() - t0) > 5000) { Log("ws: send failing for 5 s (%d of %d bytes)", off, n); return false; }
        if (c->tls) {
            int w = 0;
            Res r = sslWrite(c->sslConn, reinterpret_cast<const char*>(p) + off, &w, (uint32_t)(n - off));
            if (r == 0 && w > 0) { off += w; t0 = 0; continue; }
            if (r == 0 || r == 0x1987b) { if (!t0) t0 = Ticks(); svcSleepThread(200000); continue; }
            Log("ws: TLS write failed %#x", r);
            return false;
        }
        int64_t w = sockSend(c->fd, p + off, (size_t)(n - off), 0);
        if (w > 0) { off += (int)w; t0 = 0; continue; }
        if (w == 0) return false;
        if (!t0) t0 = Ticks();
        svcSleepThread(200000);
    }
    return true;
}
static int ReadNow(Conn* c, uint8_t* p, int cap) {
    if (cap <= 0) return 0;
    if (cap > 16 * 1024) cap = 16 * 1024;   /* one modest read at a time (as the relay code that works does; huge reads misbehave in Ryujinx) */
    if (c->tls) {
        int got = 0;
        Res r = sslRead(c->sslConn, reinterpret_cast<char*>(p), &got, (uint32_t)cap);
        if (r == 0) return got > 0 ? got : -1;
        return r == 0x1987b ? 0 : -1;
    }
    PollFd pf{ c->fd, 1 /* POLLIN */, 0 };
    if (sockPoll(&pf, 1, 0) <= 0) return 0;
    int64_t r = sockRecv(c->fd, p, (size_t)cap, 0);
    if (r > 0) { c->errSince = 0; return (int)r; }
    if (r == 0) return -1;
    if (!c->errSince) c->errSince = Ticks();
    return (int64_t)(Ticks() - c->errSince) > 0 && Ms(Ticks() - c->errSince) > 5000 ? -1 : 0;
}
static bool SendFrame(Conn* c, uint8_t op, const uint8_t* payload, int n) {
    static uint8_t buf[2][14 + 64 * 1024];   /* one per sending thread kind is enough: big texts are lobby-only */
    uint8_t* f = buf[c->cap > 16 * 1024 ? 0 : 1];
    int o = 0;
    f[o++] = (uint8_t)(0x80 | op);
    if (n < 126) f[o++] = (uint8_t)(0x80 | n);
    else if (n <= 0xffff) { f[o++] = 0x80 | 126; f[o++] = (uint8_t)(n >> 8); f[o++] = (uint8_t)n; }
    else { f[o++] = 0x80 | 127; for (int k = 7; k >= 0; k--) f[o++] = (uint8_t)((uint64_t)n >> (8 * k)); }   /* TF.EX's SkinChunk frames (~264 KB) */
    uint8_t mask[4];
    for (auto& m : mask) m = (uint8_t)Rand();
    std::memcpy(f + o, mask, 4); o += 4;
    if (o + n <= (int)sizeof buf[0]) {
        for (int i = 0; i < n; i++) f[o + i] = payload[i] ^ mask[i & 3];
        return Write(c, f, o + n);
    }
    /* longer than the buffer: the header, then the payload masked and written 64 KB at a time (the mask runs on by offset) */
    if (!Write(c, f, o)) return false;
    for (int at = 0; at < n;) {
        int k = n - at < 64 * 1024 ? n - at : 64 * 1024;
        for (int i = 0; i < k; i++) f[i] = payload[at + i] ^ mask[(at + i) & 3];
        if (!Write(c, f, k)) return false;
        at += k;
    }
    return true;
}
} // namespace ws
} // namespace net

namespace netws {
using namespace net;
struct Conn : net::ws::Conn {};

bool EnsureNetwork() {
    while (__atomic_exchange_n(&ws::g_netLock, 1, __ATOMIC_ACQUIRE)) svcSleepThread(1000000);
    if (ws::g_netState == 0) {
        if (!g_rng) g_rng = net::Ticks() | 1;
        Res r = nifmInitialize();
        nifmSubmitAndWait();
        bool up = nifmAvailable();
        Res rs = sockInitialize(g_socketPool, sizeof g_socketPool, 0x20000, 14);
        sslInitialize();
        net::Log("ws: network init: nifm %#x available %d, socket %#x", r, (int)up, rs);
        ws::g_netState = 1;   /* "not available" this early is common (and Ryujinx's sockets work regardless): a failed connect is the real signal */
    }
    __atomic_store_n(&ws::g_netLock, 0, __ATOMIC_RELEASE);
    return ws::g_netState == 1;
}

static Conn* Take(Rx rx);
static bool Dial(Conn* c, const char* host, int port, bool tls, const char* ipOverride);
static int ReadHead(Conn* c, char* head, int cap, char** headerEnd);

Conn* Open(const char* host, int port, bool tls, const char* path, const char* ipOverride, Rx rx) {
    if (!EnsureNetwork()) return nullptr;
    Conn* c = Take(rx);
    if (!c) return nullptr;
    if (!Dial(c, host, port, tls, ipOverride)) { Close(c); return nullptr; }
    char key[32];
    uint8_t raw[16];
    for (auto& b : raw) b = (uint8_t)Rand();
    Base64(raw, 16, key);
    char req[768];
    int reqLen = snprintf(req, sizeof req,
        "GET %s HTTP/1.1\r\nHost: %s\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\n\r\n",
        path, host, key);
    if (!ws::Write(c, reinterpret_cast<uint8_t*>(req), reqLen)) { net::Log("ws: upgrade write failed"); Close(c); return nullptr; }
    char head[2048];
    char* headerEnd = nullptr;
    int have = ReadHead(c, head, sizeof head, &headerEnd);
    if (!headerEnd || std::strncmp(head, "HTTP/1.1 101", 12) != 0) { net::Log("ws: upgrade for %s refused: %.60s", path, have ? head : "no response"); Close(c); return nullptr; }
    int extra = have - (int)(headerEnd + 4 - head);
    if (extra > 0 && extra <= c->cap) { std::memcpy(c->rx, headerEnd + 4, (size_t)extra); c->len = extra; }
    if (tls) sslSetIoMode(c->sslConn, 2 /* non-blocking */);
    c->upgraded = true;
    net::Log("ws: connected to %s:%d%s", host, port, path);
    return c;
}

int HttpPost(const char* host, int port, bool tls, const char* path, const char* contentType, const char* body, int bodyLen, const char* ipOverride) {
    if (!EnsureNetwork()) return -1;
    Conn* c = Take(Rx::Small);
    if (!c) return -1;
    if (!Dial(c, host, port, tls, ipOverride)) { Close(c); return -1; }
    char req[640];
    int reqLen = snprintf(req, sizeof req, "POST %s HTTP/1.1\r\nHost: %s\r\nContent-Type: %s\r\nContent-Length: %d\r\nConnection: close\r\n\r\n", path, host, contentType, bodyLen);
    int status = -1;
    if (ws::Write(c, reinterpret_cast<uint8_t*>(req), reqLen) && ws::Write(c, reinterpret_cast<const uint8_t*>(body), bodyLen)) {
        char head[1024];
        char* headerEnd = nullptr;
        int have = ReadHead(c, head, sizeof head, &headerEnd);
        if (have > 12 && !std::strncmp(head, "HTTP/1.", 7)) status = std::atoi(head + 9);
    }
    Close(c);
    return status;
}

/* One plain-HTTP GET (no TLS: the dev server on the local network), body streamed to `sink` in chunks. Returns the status code (-1 on
 * failure); *bodyLen = the bytes delivered. Blocking. */
/* An SDK function by its mangled name (the game's nnSdk exports), or nullptr. */
void* SdkExport(const char* mangled) { return reinterpret_cast<void*>(net::FindExport(mangled)); }

int HttpGet(const char* host, int port, const char* path, void (*sink)(void* ctx, const uint8_t* p, int n), void* ctx, int64_t* bodyLen) {
    if (bodyLen) *bodyLen = 0;
    if (!EnsureNetwork()) return -1;
    Conn* c = Take(Rx::Small);
    if (!c) return -1;
    if (!Dial(c, host, port, false, nullptr)) { Close(c); return -1; }
    char req[512];
    int reqLen = snprintf(req, sizeof req, "GET %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n", path, host);
    int status = -1;
    if (ws::Write(c, reinterpret_cast<uint8_t*>(req), reqLen)) {
        char head[2048];
        char* headerEnd = nullptr;
        int have = ReadHead(c, head, sizeof head, &headerEnd);
        if (headerEnd && have > 12 && !std::strncmp(head, "HTTP/1.", 7)) {
            status = std::atoi(head + 9);
            int64_t want = -1;
            const char* cl = std::strstr(head, "Content-Length:");
            if (!cl) cl = std::strstr(head, "content-length:");
            if (cl && cl < headerEnd) want = std::atoll(cl + 15);
            int64_t got = 0;
            int extra = have - (int)(headerEnd + 4 - head);
            if (extra > 0) { sink(ctx, reinterpret_cast<uint8_t*>(headerEnd + 4), extra); got += extra; }
            static uint8_t buf[16384];   /* one GET at a time (the dev-update thread) */
            uint64_t t0 = net::Ticks();
            while ((want < 0 || got < want) && net::Ms(net::Ticks() - t0) < 30000) {
                int64_t r = sockRecv(c->fd, buf, sizeof buf, 0);
                if (r <= 0) break;
                sink(ctx, buf, (int)r); got += r;
            }
            if (want >= 0 && got != want) status = -1;
            if (bodyLen) *bodyLen = got;
        }
    }
    Close(c);
    return status;
}

/* One HTTP(S) request for TF.NX's updater (tfnxupdate.cpp; one at a time, from its worker). `url`: "https://host/path" or
 * "http://host[:port]/path". Redirects (301/302/303/307/308) followed up to 5 times when `follow`; else a redirect's Location is handed
 * back in `location` (TF.EX's AutoUpdater reads the latest release from github.com's /releases/latest redirect). The body streamed to
 * `sink` (GitHub's asset host sends a Content-Length; a chunked body is refused). A stall of 20 s with nothing received ends it. */
static bool SplitUrl(const char* url, bool* tls, char* host, int hostCap, int* port, const char** path) {
    const char* p;
    if (!std::strncmp(url, "https://", 8)) { *tls = true; *port = 443; p = url + 8; }
    else if (!std::strncmp(url, "http://", 7)) { *tls = false; *port = 80; p = url + 7; }
    else return false;
    const char* end = p;
    while (*end && *end != '/' && *end != ':') end++;
    int n = (int)(end - p);
    if (n <= 0 || n >= hostCap) return false;
    std::memcpy(host, p, (size_t)n); host[n] = 0;
    if (*end == ':') { *port = std::atoi(end + 1); while (*end && *end != '/') end++; }
    *path = *end ? end : "/";
    return true;
}
static const char* HeaderValue(const char* head, const char* headerEnd, const char* name, char* out, int cap) {
    size_t nl = std::strlen(name);
    for (const char* l = std::strstr(head, "\r\n"); l && l < headerEnd; l = std::strstr(l + 2, "\r\n")) {
        const char* k = l + 2;
        if (strncasecmp(k, name, nl) != 0 || k[nl] != ':') continue;
        const char* v = k + nl + 1;
        while (*v == ' ') v++;
        const char* e = std::strstr(v, "\r\n");
        int n = e ? (int)(e - v) : (int)std::strlen(v);
        if (n >= cap) n = cap - 1;
        std::memcpy(out, v, (size_t)n); out[n] = 0;
        return out;
    }
    return nullptr;
}
int HttpRequest(const char* method, const char* url, bool follow, void (*sink)(void* ctx, const uint8_t* p, int n), void* ctx,
                int64_t* bodyLen, int64_t* contentLength, char* location, int locCap) {
    if (bodyLen) *bodyLen = 0;
    if (contentLength) *contentLength = -1;
    if (location && locCap) location[0] = 0;
    if (!EnsureNetwork()) return -1;
    static char cur[2048], head[16384], value[2048];   /* (one request at a time: the updater's worker) */
    std::snprintf(cur, sizeof cur, "%s", url);
    for (int hop = 0; hop < 6; hop++) {
        bool tls; char host[256]; int port; const char* path;
        if (!SplitUrl(cur, &tls, host, sizeof host, &port, &path)) { net::Log("http: not a URL: %.80s", cur); return -1; }
        Conn* c = Take(Rx::Small);
        if (!c) { net::Log("http: no connection slot free (%s)", host); return -1; }
        if (!Dial(c, host, port, tls, nullptr)) { Close(c); return -1; }
        static char req[2600];
        int reqLen = snprintf(req, sizeof req, "%s %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: Towerfall\r\nAccept: */*\r\nConnection: close\r\n\r\n", method, path, host);
        if (reqLen >= (int)sizeof req || !ws::Write(c, reinterpret_cast<uint8_t*>(req), reqLen)) { Close(c); net::Log("http: request to %s not sent", host); return -1; }
        char* headerEnd = nullptr;
        int have = ReadHead(c, head, sizeof head, &headerEnd);
        if (!headerEnd || have < 12 || std::strncmp(head, "HTTP/1.", 7)) { Close(c); net::Log("http: no response head from %s (%d bytes)", host, have); return -1; }
        int status = std::atoi(head + 9);
        if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
            const char* loc = HeaderValue(head, headerEnd, "Location", value, sizeof value);
            Close(c);
            if (!loc) { net::Log("http: %d from %s without a Location", status, host); return -1; }
            char next[2048];
            if (loc[0] == '/') {   /* a path on the same server */
                char portText[8] = "";
                if (port != (tls ? 443 : 80)) std::snprintf(portText, sizeof portText, ":%d", port);
                std::snprintf(next, sizeof next, "%s://%s%s%s", tls ? "https" : "http", host, portText, loc);
            } else std::snprintf(next, sizeof next, "%s", loc);
            if (!follow) { if (location) std::snprintf(location, (size_t)locCap, "%s", next); return status; }
            std::snprintf(cur, sizeof cur, "%s", next);
            continue;
        }
        int64_t want = -1;
        if (HeaderValue(head, headerEnd, "Content-Length", value, sizeof value)) want = std::atoll(value);
        if (contentLength) *contentLength = want;
        if (HeaderValue(head, headerEnd, "Transfer-Encoding", value, sizeof value) && strncasecmp(value, "identity", 8) != 0) {
            Close(c); net::Log("http: %s sent a chunked body: not supported", host); return -1;
        }
        int64_t got = 0;
        if (!std::strcmp(method, "HEAD")) want = 0;
        int extra = have - (int)(headerEnd + 4 - head);
        if (extra > 0 && want != 0) { if (sink) sink(ctx, reinterpret_cast<uint8_t*>(headerEnd + 4), extra); got += extra; }
        static uint8_t buf[16384];
        uint64_t last = net::Ticks();
        while (want < 0 || got < want) {
            int r = ws::ReadNow(c, buf, sizeof buf);
            if (r < 0) break;
            if (r == 0) {
                if (net::Ms(net::Ticks() - last) > 20000) { net::Log("http: %s stalled 20 s at %lld of %lld bytes", host, (long long)got, (long long)want); break; }
                svcSleepThread(2000000);
                continue;
            }
            last = net::Ticks();
            if (sink) sink(ctx, buf, r);
            got += r;
        }
        Close(c);
        if (bodyLen) *bodyLen = got;
        if (want >= 0 && got != want) return -1;
        return status;
    }
    net::Log("http: too many redirects from %.80s", url);
    return -1;
}

static Conn* Take(Rx rx) {
    while (__atomic_exchange_n(&ws::g_netLock, 1, __ATOMIC_ACQUIRE)) svcSleepThread(1000000);
    Conn* c = nullptr;
    for (auto& k : ws::g_conns) if (!k.used) { c = static_cast<Conn*>(&k); break; }
    if (c) {
        c->used = true;
        if (rx == Rx::Big && !ws::g_bigUsed) { ws::g_bigUsed = true; c->rx = ws::g_rxBig; c->cap = sizeof ws::g_rxBig; }
        else for (int i = 0; i < ws::kSmall; i++) if (!ws::g_smallUsed[i]) { ws::g_smallUsed[i] = true; c->rx = ws::g_rxSmall[i]; c->cap = sizeof ws::g_rxSmall[i]; break; }
        if (!c->rx) { c->used = false; c = nullptr; }
    }
    __atomic_store_n(&ws::g_netLock, 0, __ATOMIC_RELEASE);
    if (!c) {
        int conns = 0, small = 0;
        for (auto& k : ws::g_conns) conns += k.used;
        for (bool u : ws::g_smallUsed) small += u;
        net::Log("ws: no free connection slot (%d of %d connections, %d of %d small buffers, big %s)", conns, ws::kConns, small, ws::kSmall, ws::g_bigUsed ? "used" : "free");
        return nullptr;
    }
    c->len = 0; c->skip = 0; c->consumed = 0; c->errSince = 0; c->skipped = 0; c->tls = false; c->fd = -1;
    return c;
}

static bool Dial(Conn* c, const char* host, int port, bool tls, const char* ipOverride) {
    SockAddrIn sa{};
    bool found = ParseIp(host, &sa.addr);
    if (!found) {
        char portText[8];
        snprintf(portText, sizeof portText, "%d", port);
        AddrInfo hints{};
        hints.family = 2; hints.socktype = 1;
        AddrInfo* res = nullptr;
        int gai = sockGetAddrInfo(host, portText, &hints, &res);
        int entries = 0;
        if (gai == 0 && res) {
            for (AddrInfo* a = res; a; a = a->next, entries++)
                if (a->addr && a->addr->family == 2) { sa = *a->addr; found = true; break; }
            /* a lookup that returned entries but no IPv4 address (console 2026-10-29: getaddrinfo 0, still "failed"): the first entry's
             * raw words, to check this AddrInfo layout against the SDK's (logged for the first few lookups) */
            static int dumped = 0;
            if (!found && dumped++ < 3) {
                const uint32_t* w = reinterpret_cast<const uint32_t*>(res);
                const uint8_t* ad = reinterpret_cast<uintptr_t>(res->addr) > 0x10000 && !(reinterpret_cast<uintptr_t>(res->addr) & 3) ? reinterpret_cast<const uint8_t*>(res->addr) : nullptr;
                net::Log("ws: lookup %s: %d entries, none IPv4 as read; entry 0 words %08x %08x %08x %08x %08x %08x | %08x%08x %08x%08x %08x%08x; "
                         "addr bytes %02x %02x %02x %02x %02x %02x %02x %02x", host, entries, w[0], w[1], w[2], w[3], w[4], w[5], w[7], w[6], w[9], w[8],
                         w[11], w[10], ad ? ad[0] : 0, ad ? ad[1] : 0, ad ? ad[2] : 0, ad ? ad[3] : 0, ad ? ad[4] : 0, ad ? ad[5] : 0, ad ? ad[6] : 0, ad ? ad[7] : 0);
            }
            sockFreeAddrInfo(res);
        }
        if (!found) net::Log("ws: name lookup for %s failed (getaddrinfo %d, errno %d, %d entries)%s", host, gai, sockErrno(), entries, ipOverride && ipOverride[0] ? "; using the configured address" : "");
        if (!found && ipOverride && ipOverride[0]) found = ParseIp(ipOverride, &sa.addr);
    }
    if (!found) { net::Log("ws: no address for %s", host); return false; }
    sa.len = sizeof sa; sa.family = 2; sa.port = Port(port);
    c->fd = sockSocket(2, 1, 0);
    if (c->fd < 0) { net::Log("ws: socket() failed, errno %d", sockErrno()); return false; }
    int one = 1;
    sockSetOpt(c->fd, 6 /* IPPROTO_TCP */, 1 /* TCP_NODELAY */, &one, sizeof one);
    if (sockConnect(c->fd, &sa, sizeof sa) < 0) { net::Log("ws: connect %s:%d failed, errno %d", host, port, sockErrno()); return false; }
    if (tls) {
        std::memset(c->sslCtx, 0, sizeof c->sslCtx);
        std::memset(c->sslConn, 0, sizeof c->sslConn);
        sslContextCtor(c->sslCtx);
        Res r = sslContextCreate(c->sslCtx, 1);
        bool ctx = r == 0, conn = false;
        if (r == 0) { sslConnCtor(c->sslConn); r = sslConnCreate(c->sslConn, c->sslCtx); conn = r == 0; }
        if (r == 0) r = sslSetSocket(c->sslConn, c->fd);
        if (r == 0) r = sslSetHostName(c->sslConn, host, (uint32_t)std::strlen(host));
        if (r == 0) r = sslSetIoMode(c->sslConn, 1 /* blocking for the handshake */);
        if (r == 0) r = sslHandshake(c->sslConn);
        if (r != 0) {
            net::Log("ws: TLS setup with %s failed %#x", host, r);
            if (conn) sslConnDestroy(c->sslConn);   /* (the connection, then its context: both, or the next setups run out) */
            if (ctx && sslContextDestroy) sslContextDestroy(c->sslCtx);
            return false;
        }
        c->tls = true;
    }
    return true;
}

/* Reads up to the end of the response headers (blocking, 10 s at most). Returns the bytes read; *headerEnd at "\r\n\r\n" or nullptr. */
static int ReadHead(Conn* c, char* head, int cap, char** headerEnd) {
    int have = 0;
    *headerEnd = nullptr;
    uint64_t t0 = net::Ticks();
    while (!*headerEnd && have < cap - 1 && net::Ms(net::Ticks() - t0) < 10000) {
        if (c->tls) {
            int got = 0;
            Res r = sslRead(c->sslConn, head + have, &got, (uint32_t)(cap - 1 - have));
            if (r == 0 && got > 0) have += got;
            else if (r != 0 && r != 0x1987b) break;
            else svcSleepThread(1000000);
        } else {
            int64_t r = sockRecv(c->fd, head + have, (size_t)(cap - 1 - have), 0);
            if (r <= 0) break;
            have += (int)r;
        }
        head[have] = 0;
        *headerEnd = std::strstr(head, "\r\n\r\n");
    }
    return have;
}

bool SendText(Conn* c, const char* s, int n) { return c && ws::SendFrame(c, 1, reinterpret_cast<const uint8_t*>(s), n); }
bool SendBinary(Conn* c, const uint8_t* p, int n) { return c && ws::SendFrame(c, 2, p, n); }

int Next(Conn* c, uint8_t* op, const uint8_t** payload, int* len) {
    if (!c) return -1;
    if (c->consumed) { std::memmove(c->rx, c->rx + c->consumed, (size_t)(c->len - c->consumed)); c->len -= c->consumed; c->consumed = 0; }
    for (;;) {
        if (c->skip) {   /* dropping the rest of an oversized frame */
            int n = c->len < (int64_t)c->skip ? c->len : (int)c->skip;
            c->skip -= (uint64_t)n;
            std::memmove(c->rx, c->rx + n, (size_t)(c->len - n)); c->len -= n;
        }
        if (!c->skip && c->len >= 2) {
            uint8_t o = c->rx[0] & 0x0f;
            uint64_t L = c->rx[1] & 0x7f;
            int off = 2;
            bool hdr = true;
            if (L == 126) { if (c->len < 4) hdr = false; else { L = (uint64_t)c->rx[2] << 8 | c->rx[3]; off = 4; } }
            else if (L == 127) { if (c->len < 10) hdr = false; else { L = 0; for (int i = 0; i < 8; i++) L = L << 8 | c->rx[2 + i]; off = 10; } }
            if (hdr) {
                if (L > (uint64_t)(c->cap - off)) {   /* never fits: skip it */
                    c->skipped++;
                    std::memmove(c->rx, c->rx + off, (size_t)(c->len - off)); c->len -= off;
                    c->skip = L;
                    continue;
                }
                if ((uint64_t)c->len >= off + L) {
                    const uint8_t* pl = c->rx + off;
                    int n = (int)L;
                    if (o == 8) { net::Log("ws: closed by the server (code %d)", n >= 2 ? pl[0] << 8 | pl[1] : 0); return -1; }
                    if (o == 9) { ws::SendFrame(c, 10, pl, n < 125 ? n : 125); std::memmove(c->rx, c->rx + off + n, (size_t)(c->len - off - n)); c->len -= off + n; continue; }
                    if (o == 1 || o == 2) { *op = o; *payload = pl; *len = n; c->consumed = off + n; return 1; }
                    std::memmove(c->rx, c->rx + off + n, (size_t)(c->len - off - n)); c->len -= off + n;   /* pong, continuation: ignored */
                    continue;
                }
            }
        }
        int got = ws::ReadNow(c, c->rx + c->len, c->cap - c->len);
        if (got < 0) { net::Log("ws: connection lost (receive failed or closed, errno %d)", sockErrno()); return -1; }
        if (got == 0) return 0;
        c->len += got;
    }
}

int Fd(Conn* c) { return c ? c->fd : -1; }
bool Tls(Conn* c) { return c && c->tls; }
void Close(Conn* c) {
    if (!c) return;
    /* A close frame (1000) before the socket goes: the server drops this peer at once. Without it the match's room connection stayed in
     * the server's room after the match (no "left" for the Switch's peer in the server log, in Ryujinx), and the rematch's reconnect
     * with the same peer id had to replace it (the local server lost the room doing that: match 2 never connected, 2026-10-17). */
    if (c->upgraded && c->fd >= 0) { static const uint8_t code[2] = { 0x03, 0xe8 }; ws::SendFrame(c, 8, code, 2); }
    c->upgraded = false;
    if (c->tls) { sslConnDestroy(c->sslConn); if (sslContextDestroy) sslContextDestroy(c->sslCtx); }
    if (c->fd >= 0) sockClose(c->fd);
    while (__atomic_exchange_n(&ws::g_netLock, 1, __ATOMIC_ACQUIRE)) svcSleepThread(1000000);
    if (c->rx == ws::g_rxBig) ws::g_bigUsed = false;
    for (int i = 0; i < ws::kSmall; i++) if (c->rx == ws::g_rxSmall[i]) ws::g_smallUsed[i] = false;
    c->rx = nullptr; c->cap = 0; c->used = false; c->fd = -1; c->tls = false;
    __atomic_store_n(&ws::g_netLock, 0, __ATOMIC_RELEASE);
}

bool StartThread(void (*fn)(void*), void* arg, void* stack, uint32_t stackSize) {
    /* one nn::os::ThreadType per thread for the module's life. 8: devupdate, the net/relay thread, the lobby service,
     * LocalGuest and hwlog's tick-log writer can all run in one session (4 left the writer without a thread: its writes never landed) */
    static uint8_t threads[8][0x1c0] __attribute__((aligned(0x40)));
    static int n = 0;
    if (n >= 8) { Log("[net] StartThread: all 8 thread slots used; thread not started"); return false; }
    void* t = threads[n++];
    if (osCreateThread(t, fn, arg, stack, stackSize, 12) != 0) { Log("[net] StartThread: CreateThread failed"); return false; }
    osStartThread(t);
    return true;
}
void Sleep(int64_t ns) { svcSleepThread(ns); }
uint64_t Ticks() { return net::Ticks(); }
double Ms(uint64_t t) { return net::Ms(t); }
void MakeUuid(char out[37]) { if (!g_rng) g_rng = net::Ticks() | 1; net::MakeUuid(out); }
void Log(const char* fmt, ...) {
    char line[700];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    net::Log("%s", line);
}
} // namespace netws

namespace net {
} // namespace net

/* ggrs_ffi's socket thread (ffisock above; hw.txt "ggrsffi 1"): the WebRTC UDP socket's address (bindIp, "" = nifm's), the STUN
 * server (stunIp if name lookup fails) and the TF.EX server's address if name lookup fails (serverIp). Starts once. */
extern "C" bool ggrsffi_sockets_start(const char* bindIp, const char* stunHost, int stunPort, const char* stunIp, const char* serverIp) {
    using namespace net::ffisock;
    if (g_started) return true;
    std::snprintf(g_bindIp, sizeof g_bindIp, "%s", bindIp ? bindIp : "");
    std::snprintf(g_stunHost, sizeof g_stunHost, "%s", stunHost ? stunHost : "");
    std::snprintf(g_stunIp, sizeof g_stunIp, "%s", stunIp ? stunIp : "");
    std::snprintf(g_serverIp, sizeof g_serverIp, "%s", serverIp ? serverIp : "");
    g_stunPort = stunPort;
    g_started = netws::StartThread(Thread, nullptr, g_stack, sizeof g_stack);
    net::Log("ffisock: socket thread %s", g_started ? "started" : "NOT started");
    return g_started;
}
/* TF.EX's ping_measurement_start(url, isRelay=false) / ping_measurement_stop / ping_measurement_stats (GGRSFFI.cs). url =
 * <ws|wss>://host[:port]/ping_measurement/<room>?peer=<own>; ip if name lookup fails. */
extern "C" int ping_measurement_start(const char* host, int port, int tls, const char* path, const char* ip,
                                      const char* bindIp, const char* stunHost, int stunPort, const char* stunIp) {
    using namespace net::pm;
    /* its UDP socket's address and STUN server: the match sockets' own (NetplayPreferences: netplay.txt rtcbind / stun) */
    std::snprintf(net::np::g_bindIp, sizeof net::np::g_bindIp, "%s", bindIp ? bindIp : "");
    std::snprintf(net::np::g_stunHost, sizeof net::np::g_stunHost, "%s", stunHost ? stunHost : "");
    std::snprintf(net::np::g_stunIp, sizeof net::np::g_stunIp, "%s", stunIp ? stunIp : "");
    net::np::g_stunPort = stunPort;
    std::snprintf(g_host, sizeof g_host, "%s", host);
    std::snprintf(g_path, sizeof g_path, "%s", path);
    std::snprintf(g_ip, sizeof g_ip, "%s", ip ? ip : "");
    g_port = port; g_tls = tls;
    __atomic_add_fetch(&g_gen, 1, __ATOMIC_RELEASE);
    g_want = 1;
    if (!g_threadStarted) g_threadStarted = netws::StartThread(Thread, nullptr, g_stack, sizeof g_stack);
    return g_threadStarted ? 1 : 0;
}
extern "C" void ping_measurement_stop() { net::pm::g_want = 0; }
extern "C" void ping_measurement_stats(const char* peer, int32_t* rtt, int32_t* spike, int32_t* lossPercent, int32_t* samples) {
    net::pm::Stats st{ -1, 0, 0, 0 };
    void* p = __atomic_load_n(&net::pm::g_pm, __ATOMIC_ACQUIRE);
    if (p && net::pm::g_want) net::pm::tfx_pm_stats(p, peer, &st);
    *rtt = st.rtt; *spike = st.spike; *lossPercent = st.lossPercent; *samples = st.samples;
}

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

/* Called from exl_main (main.cpp). */
/* ---- crash logger: the faulting instruction itself (the SDK's default handler aborts, and Atmosphère's report then shows the
 * abort, not the fault). The UserExceptionInfo's words are logged raw with module offsets for anything that points into a module;
 * the default handler runs afterwards as before. ---- */
namespace net { namespace crashlog {
static void (*setHandler)(void (*)(void*), void*, size_t, void*);
static void (*defaultHandler)(void*);
alignas(0x1000) static uint8_t g_stack[0x8000];
alignas(16) static uint8_t g_info[0x400];
static void Where(uint64_t v, char* out, size_t cap) {
    out[0] = 0;
    for (int i = (int)exl::util::ModuleIndex::Start; i < (int)exl::util::ModuleIndex::End; i++) {
        if (!exl::util::impl::mem_layout::s_ModuleBitset[i]) continue;
        auto& m = exl::util::GetModuleInfo((exl::util::ModuleIndex)i).m_Total;
        if (v >= m.m_Start && v < m.m_Start + m.m_Size) { std::snprintf(out, cap, " (module %d +%#lx)", i, (unsigned long)(v - m.m_Start)); return; }
    }
}
static void Handler(void* info) {
    const uint64_t* w = static_cast<const uint64_t*>(info);
    Log("CRASH: user exception (main module %#lx, this module %#lx); UserExceptionInfo words:",
        (unsigned long)exl::util::GetMainModuleInfo().m_Total.m_Start, (unsigned long)exl::util::GetSelfModuleInfo().m_Total.m_Start);
    char where[48];
    for (int i = 0; i < 0x130 / 8; i++) { Where(w[i], where, sizeof where); Log("CRASH: +%#04x %016lx%s", i * 8, (unsigned long)w[i], where); }
    /* The layout (the 2026-10-16 crash: type 0x101 = data abort): u32 type at +0, x0-x28 at +0x10,
     * fp lr sp pc at +0xf8, then u32 pstate afsr0 afsr1 esr at +0x118 and the fault address at +0x128. */
    const uint32_t* u = static_cast<const uint32_t*>(info);
    char wpc[48], wlr[48];
    Where(w[0x110 / 8], wpc, sizeof wpc); Where(w[0x100 / 8], wlr, sizeof wlr);
    /* (the console: esr is at +0x334, far at +0x338, as write tracking's self-test learned on 2026-10-16) */
    int eo = 0x334, fo = (eo + 4 + 7) & ~7;
    Log("CRASH: type %#x pc %#lx%s lr %#lx%s sp %#lx esr %#x (EC %#x) far %#lx (esr at +%#x)", u[0], (unsigned long)w[0x110 / 8], wpc, (unsigned long)w[0x100 / 8], wlr,
        (unsigned long)w[0x108 / 8], u[eo / 4], u[eo / 4] >> 26, (unsigned long)w[fo / 8], eo);
    hw::FlushNow();
    if (defaultHandler) defaultHandler(info);
}
static void Install() {
    Bind(setHandler, "_ZN2nn2os23SetUserExceptionHandlerEPFvPNS0_17UserExceptionInfoEEPvmS2_");
    Bind(defaultHandler, "_ZN2nn2os27DefaultUserExceptionHandlerEPNS0_17UserExceptionInfoE");
    if (setHandler) setHandler(Handler, g_stack, sizeof g_stack, g_info);
}
} } // namespace net::crashlog

void net_main() {
    Logging.Log("[net] network probe injected");
    net::crashlog::Install();
    if (!net::BindAll()) { Logging.Log("[net] some SDK functions are missing; not starting"); return; }
    /* on core 1 (the game's main thread is on core 0; Ryujinx ran this thread only when the main thread yielded: ~1 network pass a second) */
    using CoreFn = net::Res (*)(void*, net::ThreadFn, void*, void*, size_t, int, int);
    auto withCore = reinterpret_cast<CoreFn>(net::FindExport("_ZN2nn2os12CreateThreadEPNS0_10ThreadTypeEPFvPvES3_S3_mii"));
    int r = withCore ? (int)withCore(net::g_thread, net::ThreadMain, nullptr, net::g_stack, sizeof net::g_stack, 12, 1)
                     : (int)net::osCreateThread(net::g_thread, net::ThreadMain, nullptr, net::g_stack, sizeof net::g_stack, 12);   /* above the game's main thread (16): Horizon doesn't time-slice equal priorities, so a busy main thread would starve it */
    Logging.Log("[net] CreateThread -> %#x", r);
    if (r == 0) net::osStartThread(net::g_thread);
}
