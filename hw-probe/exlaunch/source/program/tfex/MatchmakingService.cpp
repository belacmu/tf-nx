#include "gen/tfexgen_GlueFfi.hpp"
#include "MatchmakingService.hpp"
#include "gen/tfexgen_GlueFrMods.hpp"
#include "gen/tfexgen_Auto_Tutorial.hpp"
#include "gen/tfexgen_Auto_Changelog.hpp"
#include "MainMenuPatch.hpp"
#include "NetplayScreens.hpp"
#include "Game.hpp"
#include "gen/tfexgen_MatchmakingService.hpp"
#include "runtime/tfexgen_services.hpp"
#include "gen/tfexgen_NetplaySettings.hpp"
#include "gen/tfexgen_modsettings.hpp"
#include "gen/tfexgen_Auto_NetplayPreferences.hpp"
#include "../netws.hpp"
#include "../hwlog.hpp"
#include "../pools.hpp"
#include "Json.hpp"
#include "nn/fs.hpp"
#include <cstdio>
#include <cstring>
#include <cstdlib>

namespace tfex {

char NetplayPreferences::Server[128] = TFNX_SERVER;
char NetplayPreferences::Name[16] = "SWITCH";
char NetplayPreferences::Ip[32] = "";
char NetplayPreferences::RtcBind[32] = "";
char NetplayPreferences::StunHost[64] = "stun.l.google.com";
int NetplayPreferences::StunPort = 19302;
char NetplayPreferences::StunIp[32] = "";

bool NetplaySettingsFile::Existed = false;

namespace {
constexpr const char* kSettingsPath = "sd:/xplay/TF.EX.settings.json";
constexpr long kSettingsFileSize = 1024;   /* fixed: the JSON, then spaces (the file is made once at this size) */
int ReadSd(const char* path, char* buf, int cap) {
    nn::fs::FileHandle h;
    if (nn::fs::OpenFile(&h, path, nn::fs::OpenMode_Read) != 0) return -1;
    long size = 0;
    nn::fs::GetFileSize(&size, h);
    if (size > cap - 1) size = cap - 1;
    nn::fs::ReadFile(h, 0, buf, size);
    nn::fs::CloseFile(h);
    buf[size] = 0;
    return (int)size;
}
/* the settings file's text as read at boot (FortRise reads a mod's settings file before its OnInitialize); empty when there was none */
char g_settingsText[kSettingsFileSize + 1];
int g_settingsLen = 0;
/* the file's text, written padded with spaces to its fixed size */
bool WriteSettingsFile(const char* text, int len) {
    if (!hw::MountSd() || len < 0 || len >= (int)kSettingsFileSize) return false;
    static char buf[kSettingsFileSize];
    std::memcpy(buf, text, len);
    for (int i = len; i < (int)sizeof buf; i++) buf[i] = ' ';
    nn::fs::CreateFile(kSettingsPath, kSettingsFileSize);   /* fails if it exists: fine */
    nn::fs::FileHandle h;
    if (nn::fs::OpenFile(&h, kSettingsPath, nn::fs::OpenMode_Write) != 0) { hw::Log("[tfex] %s: can't open for writing", kSettingsPath); return false; }
    bool ok = nn::fs::WriteFile(h, 0, buf, sizeof buf, nn::fs::WriteOption::CreateOption(0)) == 0;
    if (ok) nn::fs::FlushFile(h);
    nn::fs::CloseFile(h);
    return ok;
}
}  // namespace

/* FortRise's ModuleSettings save: the object's properties as JSON (generated: tfexgen_modsettings, CodeGen.ModSettings), when they
 * differ from the file's */
void NetplaySettingsFile::Save() {
    void* s = Object();
    if (!s) return;
    static char text[kSettingsFileSize];
    int n = gen::modsettings::Save_NetplaySettings(s, text, sizeof text);
    if (n < 0) { hw::Log("[tfex] %s: the settings don't fit %d bytes, not saved", kSettingsPath, (int)kSettingsFileSize); return; }
    if (n == g_settingsLen && !std::memcmp(text, g_settingsText, n)) return;   /* unchanged */
    bool ok = WriteSettingsFile(text, n);
    if (ok) { std::memcpy(g_settingsText, text, n); g_settingsLen = n; g_settingsText[n] = 0; }
    hw::Log("[tfex] settings %s %s: %s", ok ? "saved to" : "NOT saved to", kSettingsPath, text);
}

/* TF.EX's module's OnInitialize (FortRise invokes it once the settings are read: ConfigureChangelog, ConfigureTutorial); its constructor
 * ran at mod load (FrModsBoot.cpp) */
static void TfexModuleBoot() {
    gen::GlueFrMods::Initialize();
    hw::Log("[tfex] module initialized (TF.EX's TFEXModModule.OnInitialize): version %s, returning player %d, tutorial %s, changelog %s",
            TFEX_VERSION, (int)NetplaySettingsFile::Existed, gen::Auto_Tutorial::get_IsPending() ? "pending (shown after PRESS START)" : "seen",
            gen::Auto_Changelog::get_HasPending() ? "releases pending" : "nothing pending");
}

/* TF.EX's NetplaySettings.OnVerify -> Apply, on the game thread in a menu (the settings object is a GC allocation): the stored values
 * into TF.EX's NetplaySettings (generated), its own Apply (clamping, the upper-case name, the default server, the modes, a
 * "local:<guid>" PlayerId when empty) into TF.EX's NetplayPreferences, then the normalized values back into the file (saved when they
 * changed) and the C-string copies for the native readers. Once. */
static void** g_settingsRoot = nullptr;   /* TF.EX's NetplaySettings object: kept alive, as FortRise keeps the module's settings */
void* NetplaySettingsFile::Object() { return gen::RootStatic(g_settingsRoot); }
/* the object's values saved (TF.EX's options entries changed them and ran its Apply, or its changelog / tutorial MarkSeen) and the
 * C-string copies for the native readers (the transport connects with NetplayPreferences::Server) */
void NetplaySettingsFile::StoreObject() {
    namespace NP = gen::Auto_NetplayPreferences;
    if (!Object()) return;
    gen::Utf8Of(NP::Name(), NetplayPreferences::Name, sizeof NetplayPreferences::Name);
    gen::Utf8Of(NP::Server(), NetplayPreferences::Server, sizeof NetplayPreferences::Server);
    Save();
}
bool NetplaySettingsFile::Apply() {
    namespace NS = gen::NetplaySettings;
    namespace NP = gen::Auto_NetplayPreferences;
    void*& s = gen::RootStatic(g_settingsRoot);
    static bool tried = false;
    if (s || tried) return s != nullptr;
    tried = true;
    NetplayPreferences::Load();
    if (!NS::EnsureClass() || !(s = NS::New())) { hw::Log("[tfex] settings: TF.EX's NetplaySettings not made: the native values only"); return false; }
    /* TFEX-DIFF S-native: the Switch's first-run defaults over TF.EX's (its own server, the name SWITCH, no updater: S-update) */
    auto str = [](const char* v) { return gen::ManagedUtf8(v, (int)std::strlen(v)); };
    NS::set_Name(s, str("SWITCH"));
    NS::set_Server(s, str(kDefaultServer));
    NS::set_AutoUpdate(s, false);
    /* FortRise: the module's settings file read over the object (generated: the properties the file has) */
    bool read = g_settingsLen > 0 && gen::modsettings::Load_NetplaySettings(s, g_settingsText, g_settingsLen);
    if (g_settingsLen > 0 && !read) hw::Log("[tfex] %s: not JSON, the defaults", kSettingsPath);
    NS::Apply(s);
    gen::Utf8Of(NP::Name(), NetplayPreferences::Name, sizeof NetplayPreferences::Name);
    gen::Utf8Of(NP::Server(), NetplayPreferences::Server, sizeof NetplayPreferences::Server);
    hw::Log("[tfex] settings %s (TF.EX's NetplaySettings.Apply): name %s, server %s", read ? "read and applied" : "defaults applied",
            NetplayPreferences::Name, NetplayPreferences::Server);
    Save();
    TfexModuleBoot();
    return true;
}

/* sd:/xplay/netplay.txt: only the Switch's own connection settings (S-native: ip, rtcbind, stun). TF.EX's own settings (name, server,
 * input delay ...) are TF.EX's NetplaySettings in sd:/xplay/TF.EX.settings.json, set on its options entries (the OPTIONS screen); a
 * missing file means TF.EX's defaults (NetplaySettings' initializers), saved in its shape. */
void NetplayPreferences::Load() {
    static bool loaded = false;
    if (loaded || !hw::MountSd()) return;
    loaded = true;
    static char buf[1024];
    if (ReadSd("sd:/xplay/netplay.txt", buf, sizeof buf) > 0) {
        for (char* line = buf; line && *line;) {
            char* nl = std::strchr(line, '\n');
            if (nl) *nl = 0;
            char v[128];
            if (std::sscanf(line, "ip %31s", v) == 1) std::snprintf(Ip, sizeof Ip, "%s", v);
            else if (std::sscanf(line, "rtcbind %31s", v) == 1) std::snprintf(RtcBind, sizeof RtcBind, "%s", v);
            else if (std::sscanf(line, "stun %63s", v) == 1 && !std::strcmp(v, "off")) StunHost[0] = 0;
            else if (char h[64], ip[32] = ""; std::sscanf(line, "stun %63s %d %31s", h, &StunPort, ip) >= 2) {
                std::snprintf(StunHost, sizeof StunHost, "%s", h);
                std::snprintf(StunIp, sizeof StunIp, "%s", ip);
            }
            line = nl ? nl + 1 : nullptr;
        }
    }
    /* TF.EX's settings file, read now (its object is made on the game thread: NetplaySettingsFile::Apply); the name and server for the
     * native readers until then */
    int n = ReadSd(kSettingsPath, g_settingsText, sizeof g_settingsText);
    g_settingsLen = n > 0 ? n : 0;
    while (g_settingsLen > 0 && g_settingsText[g_settingsLen - 1] == ' ') g_settingsText[--g_settingsLen] = 0;   /* the file's padding */
    NetplaySettingsFile::Existed = g_settingsLen > 0;
    std::snprintf(Name, sizeof Name, "%s", "SWITCH");
    std::snprintf(Server, sizeof Server, "%s", NetplaySettingsFile::kDefaultServer);
    if (g_settingsLen > 0) {
        static json::Node nodes[48];
        json::Doc d{nodes, 48, 0};
        if (d.Parse(g_settingsText, g_settingsLen) && d.Root()) {
            if (d.Get(d.Root(), "Name")) d.Str(d.Root(), "Name", Name, sizeof Name);
            if (d.Get(d.Root(), "Server")) d.Str(d.Root(), "Server", Server, sizeof Server);
        }
        hw::Log("[tfex] settings file %s: %d bytes (name %s, server %s)", kSettingsPath, g_settingsLen, Name, Server);
    } else {
        hw::Log("[tfex] no %s: TF.EX's default settings (saved once its object is made)", kSettingsPath);
    }
}

namespace {
/* ---- the Switch's part under TF.EX's generated MatchmakingService: its ClientWebSocket is the runtime's (tfexgen_bcl.cpp); the server's
 * messages its listen thread receives come here, for TF.EX's HandleMessageContents on the game thread ---- */
char g_roomPeerId[kPeerId];             /* MatchmakingService.peerId ([service] rule: GetRoomPeerId / SetRoomPeerId) */

/* oversize messages (TF.EX's SkinChunk: a 192 KB chunk in base64, 262 KB, plus its JSON): two large buffers, taken by a slot
 * whose message doesn't fit its own buffer and given back once the message is sent / handled (busy flags: one side takes, the other
 * gives back). Both taken: the message is dropped and logged, as any full pool */
constexpr int kBigMsg = 280 * 1024;
struct BigBuf { int busy; char text[kBigMsg]; };
BigBuf g_inBig[2];
char* TakeBig(BigBuf* b, const char* what, int len) {
    if (len >= kBigMsg) { netws::Log("[pools] tfex %s message of %d bytes is longer than %d: dropped", what, len, kBigMsg); return nullptr; }
    for (int i = 0; i < 2; i++) {
        int idle = 0;
        if (__atomic_compare_exchange_n(&b[i].busy, &idle, 1, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) return b[i].text;
    }
    netws::Log("[pools] EXHAUSTED tfex %s large messages (2 x %d bytes): a %d-byte message dropped", what, kBigMsg, len);
    return nullptr;
}
void GiveBig(BigBuf* b, const char* text) { for (int i = 0; i < 2; i++) if (text == b[i].text) __atomic_store_n(&b[i].busy, 0, __ATOMIC_RELEASE); }

/* incoming messages (TF.EX's listen thread -> the game thread, where TF.EX's HandleMessageContents runs: PumpTransport) */
struct In { int len; char* text; char own[64 * 1024]; };
constexpr int kIn = 4;
In g_in[kIn];
volatile uint32_t g_inW = 0, g_inR = 0;
int g_inPeak = 0, g_inDropped = 0;
void InUse(pools::Use* u) { u->used = (int)(g_inW - g_inR); u->cap = kIn; u->peak = g_inPeak; }
void Enqueue(const char* text, int len) {   /* the listen thread */
    uint32_t w = g_inW, used = w - __atomic_load_n(&g_inR, __ATOMIC_ACQUIRE);
    if (used >= (uint32_t)kIn) {
        g_inDropped++;
        netws::Log("[pools] EXHAUSTED tfex inbound messages (capacity %d slots): a %d-byte message dropped (%.40s), %d so far", kIn, len, text, g_inDropped);
        return;
    }
    In& m = g_in[w % kIn];
    char* dst = len < (int)sizeof m.own ? m.own : TakeBig(g_inBig, "inbound", len);
    if (!dst) { g_inDropped++; return; }
    std::memcpy(dst, text, (size_t)len); dst[len] = 0; m.text = dst; m.len = len;
    if ((int)used + 1 > g_inPeak) g_inPeak = (int)used + 1;
    __atomic_store_n(&g_inW, w + 1, __ATOMIC_RELEASE);
}

bool ParseServer(char* host, int hostCap, int* port, bool* tls) {
    const char* s = NetplayPreferences::Server;
    *tls = !std::strncmp(s, "wss://", 6);
    if (*tls) s += 6; else if (!std::strncmp(s, "ws://", 5)) s += 5;
    int i = 0;
    while (s[i] && s[i] != ':' && s[i] != '/' && i < hostCap - 1) { host[i] = s[i]; i++; }
    host[i] = 0;
    *port = s[i] == ':' ? std::atoi(s + i + 1) : (*tls ? 443 : 80);
    return host[0] != 0;
}

} // namespace

/* ---- TF.EX's own MatchmakingService (generated), the lobby's only implementation ---- */
namespace GM = gen::MatchmakingService;
namespace {
void** g_genRoot = nullptr;   /* the object, in uncollectable (GC-scanned) memory: it holds managed references */
void* Gen() { return g_genRoot ? *g_genRoot : nullptr; }
void MakeGen() {   /* game thread, on a menu (the class and its statics are GC allocations) */
    static bool tried = false;
    if (tried) return;
    tried = true;
    g_genRoot = static_cast<void**>(game::Fn<void* (*)(uint64_t)>(game::brute::MallocUncollectable)(sizeof(void*)));
    if (!g_genRoot) { hw::Log("[tfex] TF.EX's MatchmakingService: no GC root"); return; }
    NetplaySettingsFile::Apply();   /* TF.EX's settings (its NetplayPreferences: name, server, PlayerId) before any connection, as its OnVerify at load */
    void* s = gen::svc::Service();
    *g_genRoot = GM::New(s, s, s, s, s);
    pools::Register("tfex inbound messages", InUse);
    hw::Log("[tfex] TF.EX's MatchmakingService (generated) %s", *g_genRoot ? "made: it handles the server's messages" : "FAILED: no netplay lobby");
}
/* a hand (Action, ctx) callback as TF.EX's Action: the target is a managed object[] holding (fn, ctx), as the translator's closures */
struct Cb { Action fn; void* ctx; };
void CbRun(void* target) { Cb c{}; if (void* d = gen::ClosureData(target)) std::memcpy(&c, d, sizeof c); if (c.fn) c.fn(c.ctx); }
void* Delegate(Action fn, void* ctx) {
    if (!fn) return nullptr;
    void* target = gen::NewClosure(Cb{fn, ctx}, gen::bind::t_Object_49d5);
    return target ? game::brute::NewNativeDelegate(gen::bind::t_Action, target, reinterpret_cast<void*>(&CbRun)) : nullptr;
}
/* one server message for TF.EX's HandleMessageContents (RematchLobby / ArcherSelectLobby too: TF.EX's own handlers, the Switch's start
 * and teardown under them in EndOfMatch.cpp / MapScenePatch.cpp; SeriesLobby: TF.EX's ReturnToMenu(SeriesLobby), its SeriesScreen). */
/* Logged per message name: the first 5 of each, then every 100th with its count (TF.EX's server sends QuickPlayStatus every second
 * while searching: one cap for all messages was spent on those, and QuickPlayMatchFound never showed). */
void LogServerMessage(const char* text, int len) {
    static char names[32][40]; static int counts[32]; static int used = 0, unnamed = 0;
    char tag[40] = ""; std::sscanf(text, "{\"%39[^\"]", tag);
    int i = 0; while (i < used && std::strcmp(names[i], tag) != 0) i++;
    if (i == used) {
        if (used == 32) { if (unnamed++ < 40) hw::Log("[tfex] server message %s (%d bytes): TF.EX's HandleMessageContents", tag, len); return; }
        std::snprintf(names[used++], sizeof names[0], "%s", tag);
    }
    int n = ++counts[i];
    if (n <= 5) hw::Log("[tfex] server message %s (%d bytes): TF.EX's HandleMessageContents", tag, len);
    else if (n % 100 == 0) hw::Log("[tfex] server message %s: %d so far (logged: the first 5, then every 100th)", tag, n);
}
void GenDispatch(void* o, const char* text, int len) {
    LogServerMessage(text, len);
    GM::HandleMessageContents(o, gen::ManagedUtf8(text, len));
}
} // namespace
void* MatchmakingService::GenObject() { return Gen(); }

/* NetplayPreferences.Server split into host, port, TLS (the match relay is on the same server). */
bool ParseNetplayServer(char* host, int hostCap, int* port, bool* tls) { return ParseServer(host, hostCap, port, tls); }

static MatchmakingService g_instance;   /* not a function-local static: those need __cxa_guard_*, which this module doesn't link */
MatchmakingService& MatchmakingService::Instance() { return g_instance; }

void MatchmakingService::DisconnectFromServer() { if (void* o = Gen()) GM::DisconnectFromServer(o); }
bool MatchmakingService::IsConnectedToServer() { void* o = Gen(); return o && GM::IsConnectedToServer(o); }
void MatchmakingService::PostServerMessage(const char* text, int len) { Enqueue(text, len); }
void MatchmakingService::ResetPeer() { if (void* o = Gen()) GM::ResetPeer(o); }
void MatchmakingService::SetRoomPeerId(const char* id) { std::snprintf(g_roomPeerId, sizeof g_roomPeerId, "%s", id ? id : ""); }
const char* MatchmakingService::GetRoomPeerId() { return g_roomPeerId; }
void MatchmakingService::ClearOwnLobby() { static gen::ws::Lobby empty; if (void* o = Gen()) GM::UpdateOwnLobby(o, &empty); }   /* (never written: TF.EX's empty Lobby) */
int MatchmakingService::GetLocalSeat() { void* o = Gen(); return o ? GM::GetLocalSeat(o) : -1; }
void MatchmakingService::LeaveLobby(Action onSuccess, Action onFail, void* ctx) { if (void* o = Gen()) GM::LeaveLobby(o, Delegate(onSuccess, ctx), Delegate(onFail, ctx)); }
bool MatchmakingService::IsLobbyReady() { void* o = Gen(); return o && GM::IsLobbyReady(o); }
bool MatchmakingService::CanHostStart() { void* o = Gen(); return o && GM::CanHostStart(o); }
void MatchmakingService::ShowMatchStartWarnings(void* scene) { if (void* o = Gen()) GM::ShowMatchStartWarnings(o, scene); }
void MatchmakingService::SendJson(const char* text, int len) { if (void* o = Gen()) GM::Send(o, gen::ManagedUtf8(text, len)); }   /* TF.EX's Send */
/* S-net, every frame before TF.EX's TFGame.Update prefix (which drains TF.EX's own game-thread queue, generated): the server's messages
 * TF.EX's listen thread received, into TF.EX's HandleMessageContents (the PC runs it on that thread; here on the game thread: the
 * runtime's object pools aren't thread-safe); then TF.EX's StartOrStopPingMeasurementIfNeeded with the Switch's condition */
void MatchmakingService::PumpTransport() {
    if (!Gen() && game::SceneIs("MainMenu")) MakeGen();
    void* o = Gen();
    if (o) gen::GlueFfi::PumpListener();   /* TF.EX's listen loop: one pass (the frames that have arrived) */
    while (o && g_inR != __atomic_load_n(&g_inW, __ATOMIC_ACQUIRE)) {
        In& m = g_in[g_inR % kIn];
        GenDispatch(o, m.text, m.len);
        GiveBig(g_inBig, m.text);
        __atomic_store_n(&g_inR, g_inR + 1, __ATOMIC_RELEASE);
    }
    /* TF.EX: Scene is MainMenu && !netplayManager.IsInit(); here also !hasHostStartedMatch (the measurement socket closes before the
     * match's room socket connects) */
    if (o) GM::StartOrStopPingMeasurementIfNeeded(o, game::SceneIs("MainMenu") && !GM::S(o)->hasHostStartedMatch);
}


} // namespace tfex
