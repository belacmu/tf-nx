/* WebSocket client connections over the game's own nn::socket / nn::ssl (net.cpp), for the netplay lobby (TF.EX's matchmaking
 * server) and the match relay. Each connection is used by one thread at a time. */
#pragma once
#include <cstdint>

namespace netws {

/* Once, from any thread: nifm, socket and ssl initialised. True when the network is usable. */
bool EnsureNetwork();

struct Conn;
enum class Rx { Big, Small };   /* receive buffer: Big = 512 KB (the lobby: SkinChunk frames reach ~330 KB), Small = 16 KB */

/* Connects (name or IPv4; `ipOverride` if name resolution fails), TLS if `tls`, and does the WebSocket upgrade for `path`
 * (e.g. "/ws" or "/relay/<room>?peer=<uuid>"). Blocking (up to ~10 s). No Origin header (as desktop TF.EX). nullptr on failure. */
Conn* Open(const char* host, int port, bool tls, const char* path, const char* ipOverride, Rx rx);
/* One HTTP POST (Connection: close; no Origin). Returns the status code, or -1 when it failed. Blocking. */
int HttpPost(const char* host, int port, bool tls, const char* path, const char* contentType, const char* body, int bodyLen, const char* ipOverride);
void* SdkExport(const char* mangled);   /* an nnSdk function by its mangled name, or nullptr */
/* One plain-HTTP GET, the body streamed to `sink`. Returns the status code, or -1 (failure or a short body). Blocking. */
int HttpGet(const char* host, int port, const char* path, void (*sink)(void* ctx, const uint8_t* p, int n), void* ctx, int64_t* bodyLen);
/* One HTTP(S) GET / HEAD of a URL (TF.NX's updater), redirects followed when `follow` (else the Location handed back), the body streamed
 * to `sink`. Returns the final status code, or -1 (failure, a short body). Blocking; one at a time. */
int HttpRequest(const char* method, const char* url, bool follow, void (*sink)(void* ctx, const uint8_t* p, int n), void* ctx,
                int64_t* bodyLen, int64_t* contentLength, char* location, int locCap);
bool SendText(Conn* c, const char* s, int n);
bool SendBinary(Conn* c, const uint8_t* p, int n);
/* The next whole message, if one has arrived: 1 = *op (1 text, 2 binary), *payload, *len valid until the next call; 0 = nothing yet;
 * -1 = closed or failed. Pings are answered here; frames bigger than the buffer are skipped (and counted). */
int Next(Conn* c, uint8_t* op, const uint8_t** payload, int* len);
void Close(Conn* c);
int Fd(Conn* c);                 /* the socket (for a poll over several connections); -1 if none */
bool Tls(Conn* c);
/* A thread of its own (nn::os, priority 12 like the other network threads). */
bool StartThread(void (*fn)(void*), void* arg, void* stack, uint32_t stackSize);
void Sleep(int64_t ns);
uint64_t Ticks();
double Ms(uint64_t ticks);
void MakeUuid(char out[37]);
void Log(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

} // namespace netws
