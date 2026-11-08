/* tfexgen_services.hpp over the hand-written services. */
#include "../ModsMenu.hpp"
#include "../FrVersusModes.hpp"
#include "tfexgen_Fr_MatchSettings.hpp"
#include "tfexgen_services.hpp"
#include "tfexgen_GameContext.hpp"
#include "tfexgen_mods.hpp"
#include "../gen/tfexgen_ModCollections.hpp"
#include <strings.h>
#include "tfexgen_bcl.hpp"
#include "tfexgen_binding.hpp"
#include "../../rnd.hpp"
#include "modcatalog.hpp"
#include <strings.h>
#include "tfexgen_ffi.hpp"
#include "../MatchmakingService.hpp"
#include "tfexgen_MatchmakingService.hpp"
#include "tfexgen_NetplayManager.hpp"
#include "tfexgen_MatchReport.hpp"
#include "tfexgen_Auto_NetplayPreferences.hpp"
#include "../EndOfMatch.hpp"
#include "tfexgen_Auto_Lobby.hpp"
#include "tfexgen_Auto_ExFlags.hpp"
#include "../MainMenuPatch.hpp"
#include "../InputLock.hpp"
namespace tfex::MatchPause { bool Pressed(int button); }
namespace pcgameplay { bool On(); }   /* pcgameplay.cpp: online mode (ApplyNetplayMode .. ClearNetplayMode) */   /* tfex/MatchPause.cpp: LightPauseMenu.Pressed, per simulated frame, replayed in resimulations */
#include "../Json.hpp"
#include "../../hwlog.hpp"
#include <cstdio>
#include <cstring>

namespace tfex::gen::svc {
namespace {
::tfex::MatchmakingService& MS() { return ::tfex::MatchmakingService::Instance(); }   /* the transport (socket) */
ws::Lobby g_noLobby;   /* before TF.EX's MatchmakingService exists (the first main menu): an empty lobby */
/* an empty model to reset into: the models are ~36 KB, so `*out = T{}` would put one on the game thread's stack */
template <class T> const T kEmptyModel{};
} // namespace
void* GenMM() { return ::tfex::MatchmakingService::GenObject(); }
namespace GM = tfex::gen::MatchmakingService;

/* TF.EX's own MatchmakingService (generated, tfex/MatchmakingService.cpp makes it on the first main menu): its code is called directly */
void ExitQuickPlay() { if (void* o = GenMM()) GM::ExitQuickPlay(o); }
/* TF.EX MatchmakingService.UpdateLobbySettings(maxPlayers, gameData, mods): the [service] rule drops the mods argument, so they are the
 * own lobby's (TF.EX re-adds its WiderSet entry there before calling it); the message as TF.EX builds it, sent through the transport */
void UpdateLobbySettings(int32_t maxPlayers, ws::GameData* gameData) {
    if (!gameData) return;
    static ws::UpdateLobbySettingsMessage m;
    m = kEmptyModel<ws::UpdateLobbySettingsMessage>;
    ws::UpdateLobbySettings& u = m.UpdateLobbySettings.value;
    u.MaxPlayers = maxPlayers;
    u.GameData = Opt<ws::GameData>(*gameData);
    for (const ws::CustomMod& mod : GetOwnLobby()->Mods) if (ws::CustomMod* c = u.Mods.Add()) *c = mod;
    static char buf[8192];
    json::Writer w; w.Init(buf, sizeof buf);
    JWrite(w, nullptr, m);
    if (w.ok) MS().SendJson(buf, w.len); else hw::Log("[tfex] UpdateLobbySettings: the message doesn't fit %d bytes", (int)sizeof buf);
}
List<ws::Lobby, 16>* GetLobbies(void* onSuccess, void* onFail) {
    static List<ws::Lobby, 16> none;
    void* o = GenMM();
    if (!o) return &none;
    if (onSuccess || onFail) GM::GetLobbies(o, onSuccess, onFail);
    return &GM::S(o)->_lobbies;
}

void* Service() { static int token; return &token; }
void* ModApi(const void* name) {
    char n[64] = ""; if (name) Utf8Of(name, n, sizeof n);
    static const char* const kApis[] = { "TF.State", "Teuria.WiderSet" };
    for (const char* a : kApis)
        if (!strcasecmp(n, a)) return ::tfex::mods::Loaded(a) ? Service() : nullptr;
    /* any other mod: FortRise's GetApi is its API object when the mod is loaded, null when not. Calls through it are the provider class's
     * generated members ([[interop]]); the API classes of the mods we translate keep no state of their own (LoaderAI's, Soccer's), so the
     * object only says "loaded" (AINova's `LoaderAI == null` test; before 2026-11-06 every other mod's API was null) */
    return ::tfex::mods::Loaded(n) ? Service() : nullptr;
}
void* GenModCollections() {
    static void** root = nullptr;
    void*& o = RootStatic(root);
    if (!o && tfex::gen::ModCollections::EnsureClass()) o = tfex::gen::ModCollections::New(Service(), Service());
    return o;
}
int32_t ModsMenuState() { return tfex::ModsMenu::kState; }
namespace { char g_togglerEntry; }
void* ModTogglerEntry() { return &g_togglerEntry; }
int32_t MenuStateOf(void* entry) { return entry == &g_togglerEntry ? tfex::ModsMenu::kToggleState : tfex::ModsMenu::kState; }
void* ExportReplay() { tfex::gen::MatchReport::OnReplayExported(nullptr); return nullptr; }
/* TF.EX's ownLobby: the generated MatchmakingService's own field (TF.EX's code reads and writes it in place) */
ws::Lobby* GetOwnLobby() {
    void* o = GenMM();
    if (!o) return &g_noLobby;
    ws::Lobby*& l = GM::S(o)->ownLobby;
    if (!l) l = &GM::S(o)->ownLobby_box_[0];
    return l;
}
   /* (the hand service's copy is gone: nothing to sync) */
bool IsSpectator() { void* o = GenMM(); return o && GM::IsSpectator(o); }
int32_t GetLocalSeat() { void* o = GenMM(); return o ? GM::GetLocalSeat(o) : -1; }
/* TF.EX's GameContext (generated, unit GameContext): its object, made at boot with the NetplayManager (EndOfMatch.cpp), owns the local
 * controller, the input lock and the local seat. The Switch's own readers (InputLock.cpp's gates, the lobby screen) read them there. */
void** g_gameContextRoot = nullptr;
void* GameContextObj() { return g_gameContextRoot ? *g_gameContextRoot : nullptr; }
void SetGameContextObj(void** root) { g_gameContextRoot = root; }
void* LocalInput() { void* gc = GameContextObj(); return gc ? gen::GameContext::GetLocalInput(gc) : nullptr; }
bool InputLocked() { void* gc = GameContextObj(); return gc && gen::GameContext::IsInputLocked(gc); }
void LogLocalInput(void* input) {   /* (overrides.toml [after] GameContext.SetLocalInput: the first few changes) */
    static void* last = nullptr; static int logged = 0;
    if (input != last && logged < 8) { logged++; hw::Log("[input] the local controller: %p (PlayerInputs index %d)", input, input ? InputIndex(input) : -1); }
    last = input;
}
namespace { bool g_inputHeld = false; }   /* the last SetInputLocked: only DisableAllControllers (true) and EnableAllControllers (false) call it */
bool InputHeld() { return g_inputHeld; }
void LogInputLocked(bool locked) {   /* ([after] GameContext.SetInputLocked) */
    if (locked != g_inputHeld) hw::Log("[input] %s (TF.EX's GameContext; a lock expires after 5 s)", locked ? "menu input locked" : "menu input unlocked");
    g_inputHeld = locked;
}
int32_t ControllerSlots() {   /* TF.EX's `WiderSet ? 8 : 4`, WiderSet's seats: the game's own (TFGame.PlayerInputs' length; megamod/eightplayer.hpp widens it) */
    return gen::GameArr<void*>((gen::Cctor(gen::bind::c_TFGame), gen::Static<void*>(gen::bind::f_TFGame_PlayerInputs)), gen::bind::m_Array_get_Length).len;   /* (made at boot: TFGame.Initialize) */
}
/* TFEX-DIFF S-input: TF.EX's InputService.EnsureRemoteControllers gives a remote seat `new KeyboardInput()` (its menu getters are muted
 * there by IsForeignSeat, its GetState postfix gives the seat's GGRS input). The Switch has no keyboard: the seat gets
 * `new JoyConInput(index)` over the local controller's JoyCon (the same patches sit on JoyConInput, tfex/InputLock.cpp; rumble.cpp keeps
 * its rumble off the motor). Made ones are remembered so ClearRemoteSeatInputs takes them out of the menus' slots again. */
namespace { void* g_remoteSeatIn[16]; int g_remoteSeatNext = 0; }
void* NewRemoteSeatInput() {
    void* local = LocalInput();
    char tn[32] = "";
    if (local) rnd::TypeName(local, tn, sizeof tn);
    if (std::strcmp(tn, "JoyConInput")) {   /* no local controller recorded yet: the first JoyConInput in PlayerInputs */
        local = nullptr;
        void* arr = game::Static<void*>(game::TFGame::PlayerInputs);
        int32_t n = arr ? **reinterpret_cast<int32_t**>(reinterpret_cast<uintptr_t>(arr) + 0x20) : 0;
        void** data = arr ? game::ArrayData<void*>(arr) : nullptr;
        for (int32_t i = 0; i < n && !local; i++) {
            if (!data[i]) continue;
            rnd::TypeName(data[i], tn, sizeof tn);
            if (!std::strcmp(tn, "JoyConInput")) local = data[i];
        }
    }
    int32_t index = local ? *reinterpret_cast<int32_t*>(reinterpret_cast<uintptr_t>(local) + 0x20) : 0;   /* JoyConInput's JoyCon index */
    void* o = Fn<void* (*)(void*, int32_t)>(0xa61690)(nullptr, index);   /* new JoyConInput(index) */
    g_remoteSeatIn[g_remoteSeatNext++ % 16] = o;
    static int logged = 0;
    if (logged < 8) { logged++; hw::Log("[input] TF.EX's EnsureRemoteControllers: a remote seat's controller %p (JoyCon %d)", o, index); }
    return o;
}
void ClearRemoteSeatInputs() {
    void* arr = game::Static<void*>(game::TFGame::PlayerInputs);
    int32_t n = arr ? **reinterpret_cast<int32_t**>(reinterpret_cast<uintptr_t>(arr) + 0x20) : 0;
    void** data = arr ? game::ArrayData<void*>(arr) : nullptr;
    for (int32_t i = 0; i < n; i++)
        for (void*& r : g_remoteSeatIn) if (data[i] && data[i] == r) data[i] = nullptr;
    for (void*& r : g_remoteSeatIn) r = nullptr;
}
/* TFEX-DIFF S-input: InputService.IsSameDevice's gamepad branch (v0.28.1: `input is XGamepadInput pad && other is XGamepadInput otherPad`
 * -> ReferenceEquals(pad.XGamepad, otherPad.XGamepad), the pad slot's shared data). The Switch's controllers are JoyConInputs; the shared
 * data is the JoyConInput's JoyCon (MInput.JoyCons[i]'s JoyConData, +0x18: every JoyConInput made for npad i holds it, rumble.cpp). A remote
 * seat's stand-in (NewRemoteSeatInput: the PC's `new KeyboardInput()`, here a JoyConInput over the local JoyCon) is no gamepad, as on the
 * PC. Without this the branch tested a null type (no XGamepadInput on the Switch): no two objects were ever one device, so a guest's Joy-Con
 * became the local controller and both local seats read one Joy-Con (console 2026-11-01, a pair split into two players). */
void* AsPad(void* input) {
    char tn[32] = "";
    if (input) rnd::TypeName(input, tn, sizeof tn);
    if (std::strcmp(tn, "JoyConInput")) return nullptr;
    for (void* r : g_remoteSeatIn) if (r == input) return nullptr;
    return input;
}
bool SamePad(void* pad, void* otherPad) {
    void* a = pad ? *reinterpret_cast<void**>(reinterpret_cast<uintptr_t>(pad) + 0x18) : nullptr;   /* JoyConInput.JoyCon */
    void* b = otherPad ? *reinterpret_cast<void**>(reinterpret_cast<uintptr_t>(otherPad) + 0x18) : nullptr;
    return a && a == b;
}
int32_t InputIndex(void* input) {   /* input's index in TFGame.PlayerInputs, or -1 (for the log) */
    void* arr = game::Static<void*>(game::TFGame::PlayerInputs);
    if (!arr || !input) return -1;
    int32_t n = **reinterpret_cast<int32_t**>(reinterpret_cast<uintptr_t>(arr) + 0x20);   /* BRUTE array: lengths at +0x20 */
    void** data = game::ArrayData<void*>(arr);
    for (int32_t i = 0; i < n; i++) if (data[i] == input) return i;
    return -1;
}
void* PlatformCurrent() { static gen::LitN<7> lit{u"Switch"}; return lit.P(); }
void* SwitchOwnServer() { return ManagedUtf8(::tfex::NetplaySettingsFile::kDefaultServer, (int)std::strlen(::tfex::NetplaySettingsFile::kDefaultServer)); }   /* TF.EX's LocalServer on the Switch: its own default server (S-native) */
void* GetRoomPeerId() { const char* s = MS().GetRoomPeerId(); return s ? ManagedUtf8(s, (int)std::strlen(s)) : nullptr; }
void SetRoomPeerId(void* id) { char b[96] = ""; if (id) Utf8Of(id, b, sizeof b); MS().SetRoomPeerId(b); }
void ResetLobby() { if (void* o = GenMM()) GM::ResetLobby(o); }
int32_t GetSearchingCount() { void* o = GenMM(); return o ? GM::GetSearchingCount(o) : 0; }
int32_t GetStandardSearchingCount() { void* o = GenMM(); return o ? GM::GetStandardSearchingCount(o) : 0; }
int32_t GetWideSearchingCount() { void* o = GenMM(); return o ? GM::GetWideSearchingCount(o) : 0; }
Opt<bool> IsRelayedWith(ws::Player* player) { void* o = GenMM(); return o && player ? GM::IsRelayedWith(o, player) : Opt<bool>{}; }
Opt<int32_t> GetPingTo(ws::Player* player) { void* o = GenMM(); return o && player ? GM::GetPingTo(o, player) : Opt<int32_t>{}; }
Opt<int32_t> GetConnectionQualityTo(ws::Player* player) { void* o = GenMM(); return o && player ? GM::GetConnectionQualityTo(o, player) : Opt<int32_t>{}; }
}   // namespace tfex::gen::svc
namespace rightstick { tfex::gen::Vector2 Of(void* joyConInput); }
namespace xplay { void ScriptFeed(); void ScriptFeedSeat(int seat); void AfterPoll(void* nm); tfex::gen::Arr<uint8_t> SeamCapture(); int SeamRestore(tfex::gen::Arr<uint8_t> bytes); bool ReSimulatingForServices(); int64_t SyncWaitMs(); void* NpSessionForStats(int* seat, int* n); void NpStateForServices(bool* resim, int* inputDelay, int* remotesGone, bool* remoteGone); }
namespace tfex::gen::svc {
int64_t SyncWaitMs() { return xplay::SyncWaitMs(); }
namespace {
/* the session players ordered by Seat: index = GGRS handle (tfex/NetplayManager.cpp SortedBySeat) */
constexpr int kMaxSeats = 4;
int SessionBySeat(const ws::Player* out[kMaxSeats]) {   /* TF.EX's Lobby.SessionPlayers (MatchPlayers, else Players), by Seat */
    int n = 0;
    for (const ws::Player& p : *gen::Auto_Lobby::get_SessionPlayers(GetOwnLobby())) if (n < kMaxSeats) out[n++] = &p;
    for (int i = 1; i < n; i++) for (int j = i; j > 0 && out[j]->Seat < out[j - 1]->Seat; j--) { auto* t = out[j]; out[j] = out[j - 1]; out[j - 1] = t; }
    return n;
}
bool Own(const ws::Player& p) { const char* own = MS().GetRoomPeerId(); return own && *own && !p.RoomPeerId.null && !std::strcmp(p.RoomPeerId.s, own); }
}
uint32_t LocalSeatMask() {
    uint32_t m = 0;
    for (const ws::Player& p : GetOwnLobby()->Players) if (Own(p) && p.Seat >= 0 && p.Seat < 32) m |= 1u << p.Seat;
    return m ? m : 1u << (uint32_t)(GetLocalSeat() & 31);
}
bool IsLocalSeat(int32_t seat) { return seat >= 0 && seat < 32 && (LocalSeatMask() >> seat & 1); }
uint32_t LocalHandleMask() {
    const ws::Player* bySeat[kMaxSeats];
    int n = SessionBySeat(bySeat);
    uint32_t m = 0;
    for (int i = 0; i < n; i++) if (Own(*bySeat[i])) m |= 1u << i;
    int seat, count;
    if (!m && xplay::NpSessionForStats(&seat, &count)) m = 1u << (seat & 31);   /* a session without a lobby (xnet tests) */
    return m;
}
bool IsLocalInputIndex(int32_t index) {
    if (index < 0 || index >= 32) return false;
    return (InSession() ? LocalHandleMask() : LocalSeatMask()) >> index & 1;
}
namespace {
constexpr int kArcherSlots = 8;   /* keys 0..7: seats (4) and handles; TF.EX's dictionary is unbounded */
struct ArcherSlot { bool used; ws::Player player; };
ArcherSlot g_archers[kArcherSlots];
int g_archerDrops = 0;
}
Snap<Tup2<int32_t, void*>> ArcherSelections() {
    Snap<Tup2<int32_t, void*>> out;
    for (int k = 0; k < kArcherSlots; k++) {
        if (!g_archers[k].used) continue;
        char b[24]; int n = std::snprintf(b, sizeof b, "%d-%d", (int)g_archers[k].player.ArcherIndex, (int)g_archers[k].player.ArcherAltIndex);
        out.Add(Tup2<int32_t, void*>{k, gen::ManagedUtf8(b, n)});
    }
    return out;
}
Snap<Tup2<int32_t, ws::Player*>> ArcherPlayers() {
    Snap<Tup2<int32_t, ws::Player*>> out;
    for (int k = 0; k < kArcherSlots; k++) if (g_archers[k].used) out.Add(Tup2<int32_t, ws::Player*>{k, &g_archers[k].player});
    return out;
}
void AddArcher(int32_t index, ws::Player* player) {
    if (index < 0 || index >= kArcherSlots || !player) {
        if (g_archerDrops++ < 4) hw::Log("[tfexgen] AddArcher(%d): outside the archer pool (%d keys) or no player: dropped", (int)index, kArcherSlots);
        return;
    }
    ws::Player copy = *player;   /* (player may be one of these slots: copy first) */
    g_archers[index].player = copy;
    g_archers[index].used = true;
}
void ResetArcherSelections() { for (ArcherSlot& a : g_archers) a.used = false; }
void RemoveArcher(int32_t index) { if (index >= 0 && index < kArcherSlots) g_archers[index].used = false; }
/* INetplayManager on TF.EX's generated NetplayManager object (tfex/EndOfMatch.cpp makes it on the first menu) */
void* GenNM() { return ::tfex::EndOfMatch::GenNetplayManager(); }
bool InSession() { int seat, n; return xplay::NpSessionForStats(&seat, &n) != nullptr; }
/* TF.EX NetplayManager's mode and session flags. TF.EX's _netplayMode is Server from SetRoomAndServerMode (MainMenuPatch: a lobby
 * created/joined/matched) to ResetMode (TFGamePatch: back in the menus outside a lobby): on the Switch, while the own lobby isn't
 * empty, or a lobby match's GGRS session runs. Not rnd::Capture: hw.txt "rngcapture 1" forces that on in every menu. */
namespace {
bool Logged(const char* what, int i, bool v) {   /* each flag's changes, as generated code reads them (a run shows what TF.EX's code saw) */
    static signed char last[8] = {-1, -1, -1, -1, -1, -1, -1, -1};
    if (last[i] != (signed char)v) { last[i] = (signed char)v; hw::Log("[tfex] svc %s -> %d", what, v ? 1 : 0); }
    return v;
}
}
static_assert(sizeof(st::Input) == 64, "TF.EX Input = xplay.cpp TfxInput (11 int32, 4 float, int32)");
bool BoolTryParse(void* text, bool* result) {
    if (result) *result = false;
    if (!text) return false;
    char buf[32];
    gen::Utf8Of(text, buf, sizeof buf);
    const char* b = buf;
    const char* e = buf + std::strlen(buf);
    auto blank = [](char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f'; };
    while (b < e && blank(*b)) b++;
    while (e > b && blank(e[-1])) e--;
    size_t n = (size_t)(e - b);
    if (n == 4 && !strncasecmp(b, "true", 4)) { if (result) *result = true; return true; }
    if (n == 5 && !strncasecmp(b, "false", 5)) return true;
    return false;
}
/* FortRise (vendor/fortrise TowerFall.FortRise.mm/Patches/Monocle/Engine.cs): `backingField_IsFixedTimeStep = value; TargetElapsedTime =
 * value ? TimeSpan.FromSeconds(1.0f / 240f) : TimeSpan.FromSeconds(0.016666666666666666)`. .NET's FromSeconds truncates to whole ticks:
 * (float)(1/240) = 0.0041666668839... s = 41666.67 -> 41666, 1/60 -> 166666 (the PC page's 240 Hz TimeMult 0.249996 = 41666 ticks; 41667
 * here desynced a 240 Hz Switch-vs-page lobby at the first checksum, smoke 100625, 2026-10-28). */
 /* Game: IsFixedTimeStep +0x51, TargetElapsedTime +0x58 (TimeSpan ticks) */
static int32_t GetSessionFpsForLog() { void* o = GenNM(); return o ? gen::NetplayManager::GetSessionFps(o) : 240; }
void EnableFixedTimeStep(void* engine, bool on) {
    if (!engine) return;
    *reinterpret_cast<uint8_t*>(reinterpret_cast<uintptr_t>(engine) + 0x51) = on ? 1 : 0;
    *reinterpret_cast<int64_t*>(reinterpret_cast<uintptr_t>(engine) + 0x58) = on ? 41666 : 166666;
    static int logged = 0;
    if (logged++ < 6) {
        if (on) hw::Log("[np] the session's rate: %d Hz (TF.EX's ManageTimeStep: FortRise's EnableFixedTimeStep(1), TargetElapsedTime 41666 ticks%s)", (int)GetSessionFpsForLog(), GetSessionFpsForLog() != 240 ? ", then the session's step" : "");
        else hw::Log("[np] FortRise's EnableFixedTimeStep(0): TargetElapsedTime 166666 ticks (TF.EX's RestoreTimeStep)");
    }
}
void PollForGate() { void* o = GenNM(); if (!o) return; gen::NetplayManager::Poll(o); xplay::AfterPoll(o); }
gen::Arr<uint8_t> CaptureForSeam() { return xplay::SeamCapture(); }
int32_t RestoreForSeam(gen::Arr<uint8_t> bytes) { return xplay::SeamRestore(bytes); }
gen::Vector2 RightStickOf(void* input) { return ::rightstick::Of(input); }
void ScriptFeed() { ::xplay::ScriptFeed(); }
void ScriptFeedSeat(int32_t seat) { ::xplay::ScriptFeedSeat(seat); }
/* TF.EX's FakeController icons (controls/xb360/<name> on the PC): the Switch's MenuAtlas entry controls/buttons/<name>, looked up once */
void* SwitchMenuIcon(const char* name) {
    static struct { const char* name; void* tex; } cache[12]; static int n = 0;
    for (int i = 0; i < n; i++) if (!std::strcmp(cache[i].name, name)) return cache[i].tex;
    char path[64]; std::snprintf(path, sizeof path, "controls/buttons/%s", name);
    int len = (int)std::strlen(path);
    void* str = NewString(len);
    if (!str) return nullptr;
    char16_t* c = reinterpret_cast<char16_t*>(reinterpret_cast<uintptr_t>(str) + 0x14);
    for (int i = 0; i < len; i++) c[i] = (char16_t)path[i];
    void* atlas = (Cctor(bind::c_TFGame), Fn<void* (*)()>(bind::m_TFGame_get_MenuAtlas)());
    void* tex = atlas ? Fn<void* (*)(void*, void*)>(bind::m_Atlas_get_Item_6538)(atlas, str) : nullptr;
    if (n < 12) cache[n++] = { name, tex };
    return tex;
}
void UpdatePlayers(void* players, void* spectators) {
    gen::NetplayManager::UpdatePlayers(GenNM(), static_cast<List<ws::Player, 16>*>(players), static_cast<List<ws::Player, 8>*>(spectators));   /* (Lobby.Spectators' capacity) */
}
bool OnlineMode() { return pcgameplay::On(); }
/* FortRise's TreasureSpawner.GetArrowShufflePickups (the PC's): after the vanilla body (shuffled, trimmed to 2) it adds the modded arrow types
 * (none on the Switch) and shuffles the list again with Calc.Random: the game's own List<Pickups>.Shuffle(Random) (+0xb88440, the
 * Fisher-Yates the vanilla body calls), on Calc.Random (TF.State's registration is still on: the gameplay stream, as on the PC). */
bool BuiltInVersusMode(int32_t mode) {
    if (mode == 3 || mode == 4 || mode == 5) return true;   /* LastManStanding, HeadHunters, TeamDeathmatch */
    if (pcgameplay::On()) return false;   /* (local play only: a mod's mode slot is offline) */
    for (int i = 0; i < ::tfex::mods::ApiCount(::tfex::mods::kVersusModeApi); i++)   /* a mod's built-in mode slot (its VersusModeApi: TowerBall's 6) */
        if (static_cast<const ::tfex::mods::VersusModeApi*>(::tfex::mods::ApiAt(::tfex::mods::kVersusModeApi, i))->mode == mode) return true;
    return false;
}
int32_t BuiltInVersusModeCount() { return 3 + (pcgameplay::On() ? 0 : ::tfex::mods::ApiCount(::tfex::mods::kVersusModeApi)); }
void FortRiseStartGameTail(void* session) {
    /* FortRise's Session.StartGame (MonoModReplace) is the game's body plus, for a custom mode, its OnStartGame (Teams+ sizes the score
     * arrays to its groups there): `if (MatchSettings.IsCustom) MatchSettings.CustomVersusGameMode?.OnStartGame(this)` */
    void* ms = session ? gen::Fld<void*>(session, bind::f_Session_MatchSettings) : nullptr;
    if (!ms || !gen::SideGet<bool>("IsCustom", ms)) return;
    if (void* mode = gen::Fr_MatchSettings::get_CustomVersusGameMode(ms)) tfex::frvmodes::OnStartGame(mode, session);
}
void FortRiseArrowShuffleTail(void* list) {
    void* random = rnd::CalcRandomObject();
    if (list && random) gen::Fn<void (*)(void*, void*)>(0xb88440)(list, random);
}   /* TF.State ModesExtensions.IsNetplay (the PC's Mode 11): the Switch's online mode */
/* TF.EX's NetplayManager.GetEffectiveInputDelay / SetSessionInputDelay / ClearSessionInputDelay on its generated object (240 Hz frames: the
 * session override, else NetplayPreferences.InputDelayFrames from the settings' InputDelayMs); glue Ffi.ConfigJson converts it to the
 * session's rate (GGRSConfig.Fps) where it reaches ggrs_ffi, as the browser's TfexFps */
bool IsCaptureActive() { return Logged("IsCaptureActive", 3, gen::Auto_ExFlags::IsCaptureActive); }   /* TF.EX's ExFlags.IsCaptureActive (its PublishCaptureFlag, generated) */
bool LightPausePressed(int32_t button) { return tfex::MatchPause::Pressed(button); }
bool IsWide() { return tfex::MainMenuPatch::IsWide(); }
void SetWide(bool wide) { tfex::MainMenuPatch::SetWide(wide ? 1 : 0); }
float UIXOffset() { return IsWide() ? 50.0f : 0.0f; }   /* WiderSet: (420 - 320) / 2 on a wide screen */
/* TF.EX's and the mods' ILogger lines. A line repeated word for word is logged 3 times, then at its 10th, 100th, 1000th... with the
 * count, so a mod logging every frame can't fill the log. Fixed table of kSeen texts (by hash): full is logged once, and lines past it
 * are then counted as one group under the same rule */
void Log(const void* m) {
    char b[256]; Utf8Of(m, b, sizeof b);
    constexpr int kSeen = 512;
    static uint32_t hashes[kSeen]; static uint32_t counts[kSeen]; static int used = 0; static uint32_t overflow = 0;
    uint32_t h = 2166136261u; for (const char* p = b; *p; p++) h = (h ^ (uint8_t)*p) * 16777619u;
    if (!h) h = 1;
    uint32_t* n = nullptr;
    for (int i = 0; i < used && !n; i++) if (hashes[i] == h) n = &counts[i];
    if (!n && used < kSeen) { hashes[used] = h; n = &counts[used++]; }
    if (!n) {
        if (!overflow) hw::Log("[tfex] POOL FULL: the log's repeat table (%d texts): later new texts are counted together", kSeen);
        n = &overflow;
    }
    uint32_t c = ++*n;
    if (c <= 3) { hw::Log("[tfex] %s", b); return; }
    for (uint32_t d = 10; d && d <= c; d = d > 100000000u ? 0 : d * 10) if (c == d) { hw::Log("[tfex] %s (logged %u times, shown at 1-3, 10, 100...)", b, c); return; }
}

/* ---- TF.EX's MatchmakingService (generated) over the hand transport ---- */
/* TF.EX's listen loop: a received message for the game thread (TfexGlue.Ffi.HandleOnGameThread), there TF.EX's HandleMessageContents */
void PostServerMessage(void* msg) {
    if (!msg) return;
    static char b[280 * 1024];   /* the listen thread only (one at a time); a SkinChunk message is up to ~264 KB */
    int n = Utf8Of(msg, b, sizeof b);
    if (n >= (int)sizeof b - 1) { hw::Log("[tfex] a server message too long (%d bytes): dropped", n); return; }
    ::tfex::MatchmakingService::PostServerMessage(b, n);
}
int64_t TfexVersion() { int64_t v = 0; VersionTryParse(ManagedUtf8(TFEX_VERSION, (int)std::strlen(TFEX_VERSION)), v); return v; }   /* TF.EX's GetCurrentVersion (its ?version=) */
extern "C" int ping_measurement_start(const char* host, int port, int tls, const char* path, const char* ip,
                                      const char* bindIp, const char* stunHost, int stunPort, const char* stunIp);   /* net.cpp */
extern "C" void ping_measurement_stop();
/* GGRSFFI.ping_measurement_start(url, isRelay) / stop(isRelay): TF.EX's SwitchPingMeasurement (generated) over net.cpp's measurement
 * (its own thread, UDP socket and the Rust library's mesh), with the match sockets' bind address and STUN server. TFEX-DIFF
 * S-transport: TF.EX's relay ping measurement (UDP to the relay) isn't on the Switch: a failed status (TF.EX logs it) */
st::Status PingMeasurementStart(void* url, bool isRelay) {
    char u[256]; if (url) Utf8Of(url, u, sizeof u); else u[0] = 0;
    if (isRelay) { static int n = 0; if (n++ < 3) hw::Log("[tfex] relay ping measurement (%s) not supported on the Switch", u); return st::Status{0, 0}; }
    char host[128], path[192]; int port = 0; bool tls = false;
    using P = ::tfex::NetplayPreferences;
    bool ok = bcl::ParseWsUrl(u, host, sizeof host, &port, &tls, path, sizeof path) &&
              ping_measurement_start(host, port, tls ? 1 : 0, path, P::Ip, P::RtcBind, P::StunHost, P::StunPort, P::StunIp);
    hw::Log("[tfex] ping measurement %s: %s (STUN %s:%d)", ok ? "started" : "NOT started", u, P::StunHost[0] ? P::StunHost : "off", P::StunPort);
    return st::Status{(int16_t)(ok ? 1 : 0), 0};
}
void PingMeasurementStop(bool isRelay) { if (!isRelay) ping_measurement_stop(); }

extern "C" void ping_measurement_stats(const char* peer, int32_t* rtt, int32_t* spike, int32_t* lossPercent, int32_t* samples);   /* net.cpp */
void PingMeasurementStats(void* peer, bool isRelayed, st::PingStats* out) {
    *out = st::PingStats{-1, 0, 0, 0};
    if (!peer || isRelayed) return;   /* TFEX-DIFF: S-transport: no relay ping measurement on the Switch */
    char p[96]; Utf8Of(peer, p, sizeof p);
    ping_measurement_stats(p, &out->rtt, &out->spike, &out->loss_percent, &out->samples);
}

} // namespace tfex::gen::svc
