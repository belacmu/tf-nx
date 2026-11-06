/* TF.EX's NetplayManager, the part a lobby match start needs (TF.EX.Domain.Services.NetplayManager.UpdatePlayers / Init, ArcherService,
 * ArcherDataExtensions.ResolveArchers; switch-online/LOBBY-PROTOCOL.md 4.3, BROWSER-LOBBY-RELAY.md 4 "Exactly what the Switch must
 * reproduce"). Called on the game thread when StartLobby arrives; the match itself is xplay's native start + GGRS over WebRTC (ggrs_ffi's sockets, net.cpp ffisock). */
#include "MatchmakingService.hpp"
#include "runtime/tfexgen_services.hpp"
#include "gen/tfexgen_Auto_Lobby.hpp"
#include "gen/tfexgen_Models.hpp"
#include "gen/tfexgen_NetplayManager.hpp"
#include "MainMenuPatch.hpp"
#include "../netws.hpp"
#include <cstdio>
#include <cstring>


#include "../lobbymatch.hpp"
#include "Game.hpp"
#include "Notification.hpp"
#include "../dnrandom.hpp"

namespace tfex {
bool ParseNetplayServer(char* host, int hostCap, int* port, bool* tls);
constexpr int kMaxPlayers = xplay::kNpSeats;   /* the lobby match's seat capacity (lobbymatch.hpp); the lobby decides how many play */

/* GGRS handles = players ordered by Seat (ArcherService.CompactSeatsToHandles): handle = rank of Seat. The players are
 * Lobby.SessionPlayers. TFEX-DIFF S-start: the native start needs the local GGRS handle, the teams and the room peers before the map
 * screen; the archers themselves are TF.EX's ArcherService (CompactSeatsToHandles + ApplyToGame in MapScenePatch.StartSession_Prefix). */
template <class P> static int SortBySeat(const P* players, int count, const P* out[kMaxPlayers]) {
    int n = 0;
    for (int i = 0; i < count && n < kMaxPlayers; i++) out[n++] = &players[i];
    for (int i = 1; i < n; i++) for (int j = i; j > 0 && out[j]->Seat < out[j - 1]->Seat; j--) { const P* t = out[j]; out[j] = out[j - 1]; out[j - 1] = t; }
    return n;
}
/* TF.EX's own lobby (the generated MatchmakingService's ownLobby, gen::svc::GetOwnLobby): its SessionPlayers by Seat */
static int SortedBySeat(gen::ws::Lobby* l, const gen::ws::Player* out[kMaxPlayers]) {
    auto* sp = l ? gen::Auto_Lobby::get_SessionPlayers(l) : nullptr;
    return sp ? SortBySeat(sp->items, sp->count, out) : 0;
}
/* ---- Variants: TF.EX's own code applies them (the generated MatchmakingService.ApplyGameSettingsToMatch: MatchVariantsExtensions.
 * ApplyVariants on every lobby update; the generated NetplayMenus: VersusModeExtensions.ApplyNetplayVariantRules for quick play, whose
 * NormalizeForNetplay keeps the per-player values in a GC-scanned map). The hand copy that applied them again at the match start was
 * deleted 2026-10-18 (evaluation rec 4); the unported-variant gate on 2026-10-28 (every variant's TF.State patch is generated). ---- */

/* ---- The lobby's tower: TF.EX's MapScenePatch (TF.EX.Patchs/Scene/MapScene.cs, v0.21.0). Begin_Postfix selects the MapScene button
 * whose Data.ID.X == MapId, or VersusRandomSelect when MapId is -1 or no such button exists; starting on VersusRandomSelect calls
 * GetRandomVersusTower, whose postfix takes the VersusMapButtons whose Title is in Constants.NETPLAY_SAFE_MAP (locked ones dropped
 * without the Dark World DLC; NoRandom ones dropped unless all are), shuffles them with Calc.Shuffle(new Random(lobby seed)) and plays
 * the first. The buttons are MapScene.InitVersusButtons': VersusRandomSelect, then GameData.VersusTowers[i] for every i with
 * SaveData.Instance.Unlocks.GetTowerUnlocked(i), in order (Switch +0xab1fe0, same as the PC's). Since v0.21.0 InitVersusButtons_Prefix
 * makes SunkenCity, Moonstone, TowerForge and Ascension count as unlocked while the buttons are made, in a netplay session
 * (InitVersusButtons_Postfix restores them): every side draws from the same 16 towers whatever its save has unlocked (0.19.2: each
 * side's own unlocks). Switch: GameData.DarkWorldDLC is constant true (+0xa46730), so VersusMapButton.Locked (ID.X >= 12 &&
 * !DarkWorldDLC) is always false. NoRandom: v0.21.0 clears MapScene.NoRandom when netplay is entered (TFGame.cs RequestNetplayEntry)
 * and in NetplayManager's reset, so no button is NoRandom in a lobby match (the Switch clears its NoRandomStates at the match start,
 * ClearNoRandom). Title = TowerMapData(LevelData): Theme.Name, + " II" if ID.Y == 1, + " III" if ID.Y > 1 (Switch +0xb7f4e0: ID
 * +0x10/+0x14, Theme +0x18; TowerTheme.Name +0x10). ---- */
namespace Tower {
using namespace game;
constexpr uintptr_t VersusTowers = 0x47926b8;          /* static List<VersusTowerData> (GameData.Load +0xa46740) */
constexpr uintptr_t SixPlayerTowers = 0x4794320;       /* static List<VersusTowerData>: the 6P-Versus towers (GameData.Load +0xa475bc) */
constexpr uintptr_t SaveDataInstance = 0x47924e0;      /* static SaveData; .Unlocks at +0x40 */
constexpr uintptr_t GetTowerUnlocked = 0xb990f0;       /* bool UnlockData.GetTowerUnlocked(int) */
static const char* const kSafe[] = { "SACRED GROUND", "TWILIGHT SPIRE", "BACKFIRE", "FLIGHT", "MIRAGE", "THORNWOOD", "FROSTFANG KEEP",
    "KING'S COURT", "SUNKEN CITY", "MOONSTONE", "TOWERFORGE", "ASCENSION", "THE AMARANTH", "DREADWOOD", "DARKFANG", "CATACLYSM" };

static void** Towers(int* n, bool wide = false) {
    void* list = Static<void*>(wide ? SixPlayerTowers : VersusTowers);
    *n = list ? At<int32_t>(list, 0x18) : 0;
    return list ? ArrayData<void*>(At<void*>(list, 0x10)) : nullptr;
}
static bool Unlocked(int i) {
    void* save = Static<void*>(SaveDataInstance);
    void* unlocks = save ? At<void*>(save, 0x40) : nullptr;
    return unlocks && Fn<bool (*)(void*, int)>(GetTowerUnlocked)(unlocks, i);
}
/* The button's Title, as ASCII (non-ASCII becomes '?'). */
static void Title(void* tower, char* out, int cap) {
    out[0] = 0;
    void* theme = At<void*>(tower, 0x18);
    void* name = theme ? At<void*>(theme, 0x10) : nullptr;
    int n = 0;
    if (name) {
        int len = At<int32_t>(name, 0x10);
        const char16_t* ch = reinterpret_cast<const char16_t*>(reinterpret_cast<uintptr_t>(name) + 0x14);
        for (int i = 0; i < len && n < cap - 1; i++) out[n++] = ch[i] < 0x80 ? (char)ch[i] : '?';
    }
    int y = At<int32_t>(tower, 0x14);
    const char* suf = y == 1 ? " II" : y > 1 ? " III" : "";
    for (; *suf && n < cap - 1; suf++) out[n++] = *suf;
    out[n] = 0;
}
/* TF.EX v0.21.0 MapScenePatch.InitVersusButtons_Prefix / _Postfix (TF.EX.Patchs/Scene/MapScene.cs): while the map buttons are made
 * in a netplay session, Unlocks.SunkenCity, Moonstone, TowerForge and Ascension are true; the save's values come back afterwards.
 * UnlockData bools, Switch: Moonstone +0x10, SunkenCity +0x11, TowerForge +0x12, Ascension +0x13 (GetTowerUnlocked's jump table at
 * +0x38f3a98: tower 8 -> +0x11, 9 -> +0x10, 10 -> +0x12, 11 and 15 (Switch only) -> +0x13). This replaces the 0.19.2 port's UnlockAll
 * (the save's flags set for good, as the browser's BrowserHost.UnlockArchers does): the arrow pools have TF.EX's own guard
 * (LocalSettingsGuard, rnd.cpp UnlockGuard) and the unlock events TF.EX's UnlockDataPatch (rnd.cpp), so nothing else needs them. */
struct InitVersusButtonsUnlocks {
    uint8_t* unlocks = nullptr;
    uint8_t state[4] = {};   /* __state: SunkenCity, Moonstone, TowerForge, Ascension as the save has them */
    static constexpr uint32_t kOff[4] = { 0x11, 0x10, 0x12, 0x13 };
    InitVersusButtonsUnlocks() {
        void* save = Static<void*>(SaveDataInstance);
        unlocks = save ? At<uint8_t*>(save, 0x40) : nullptr;
        if (!unlocks) return;
        for (int i = 0; i < 4; i++) { state[i] = unlocks[kOff[i]]; unlocks[kOff[i]] = 1; }
    }
    ~InitVersusButtonsUnlocks() { if (unlocks) for (int i = 0; i < 4; i++) unlocks[kOff[i]] = state[i]; }
};
/* MapScene.NoRandom.Clear() (TF.EX v0.21.0, TFGame.cs RequestNetplayEntry and NetplayManager's reset). The PC TF.EX builds against
 * has a NoRandom collection; 1.4.1.6 has MapScene.NoRandomStates, a static bool[] per VersusTowers index (slot +0x4794eb0, made by the
 * static ctor +0xab0d30; VersusMapButton's ctor copies it into NoRandom): all false = none NoRandom. */
constexpr uintptr_t NoRandomStates = 0x4794eb0;
void ClearNoRandom() {
    void* arr = Static<void*>(NoRandomStates);
    int32_t* bounds = arr ? At<int32_t*>(arr, 0x20) : nullptr;   /* array length at [+0x20], elements from [+0x10] */
    uint8_t* states = ArrayData<uint8_t>(arr);
    int n = bounds ? *bounds : 0, set = 0;
    for (int i = 0; states && i < n; i++) { set += states[i] != 0; states[i] = 0; }
    netws::Log("[tfex] MapScene.NoRandom cleared (%d of %d were set)", set, n);
}
/* A wide lobby (docs/ws-wide.md 3.1): WiderSet 0.9.0 registers its 16 towers as FortRise towers after the official ones (IDs 16..31,
 * the WideLevels folders 00..15 in order, which is kSafe's order: they reuse the official themes), and in wide the map shows only those.
 * TF.EX's MapScenePatch then works as above on those buttons: the button with ID.X == MapId (never one of 0..15), else RANDOM over the
 * unlocked netplay-safe ones (MapScene.InitVersusButtons checks GetTowerUnlocked(list index)), Calc.Shuffle(new Random(lobby seed)).
 * The Switch plays the same tower from its 6P-Versus list (GameData's SixPlayerTowers, matched by title); pcgameplay.cpp lists
 * WiderSet's files for it. Returns the SixPlayerTowers index, or -1. */
static bool g_lastRandom = false;   /* the last Select went through RANDOM (MapId -1, or no button with that ID: Begin_Postfix's fallback) */
static int SelectWide(int mapId, int seed, bool quiet = false) {
    InitVersusButtonsUnlocks unlockedForButtons;
    g_lastRandom = false;
    int n = 0; void** t = Towers(&n, true);
    if (!t || n <= 0) { netws::Log("[tfex] wide tower: no 6P towers"); return -1; }
    constexpr int kWide = sizeof kSafe / sizeof kSafe[0];
    int order[kWide]; char buf[48];
    for (int k = 0; k < kWide; k++) {
        order[k] = -1;
        for (int i = 0; i < n; i++) { Title(t[i], buf, sizeof buf); if (!std::strcmp(buf, kSafe[k])) { order[k] = i; break; } }
        if (!quiet) netws::Log("[tfex] wide tower %d (id %d): \"%s\" -> 6P tower %d, %s", k, 16 + k, kSafe[k], order[k], Unlocked(k) ? "unlocked" : "locked");
    }
    if (mapId != -1)
        for (int k = 0; k < kWide; k++) if (Unlocked(k) && order[k] >= 0 && 16 + k == mapId) return order[k];
    g_lastRandom = true;
    int list[kWide]; int c = 0;
    for (int k = 0; k < kWide; k++) if (Unlocked(k) && order[k] >= 0) list[c++] = k;
    if (c == 0) { netws::Log("[tfex] wide tower: random over no towers"); return -1; }
    dnrandom::State r; dnrandom::Init(&r, seed);
    for (int k = c; --k > 0;) { int j = dnrandom::Next(&r, k + 1); int v = list[k]; list[k] = list[j]; list[j] = v; }   /* Calc.Shuffle */
    if (!quiet) netws::Log("[tfex] wide tower: map %d -> random over %d towers (seed %d): %d (6P tower %d)", mapId, c, seed, list[0], order[list[0]]);
    return order[list[0]];
}
/* The GameData.VersusTowers index TF.EX plays for this lobby on this side, or -1 (no towers). */
/* Once per boot, a check of the wide selection against TF.EX + WiderSet's model (the standard one was retired with the hand narrow pick,
 * 2026-10-18: TF.EX's generated map screen picks a standard lobby's tower). */
static void SelfTest() {
    static bool done = false;
    if (done) return;
    done = true;
    /* Wide (the model's WideTower: WiderSet's towers are IDs 16..31, so MapId 0..15 matches no button: RANDOM, the same shuffle over
     * 16 buttons, so the id is 16 + the standard answer; MapId 17 = Twilight Spire): checked by the 6P tower's title. */
    static const int32_t kWideSeeds[9] = { 109144773, 109144726, 1288949485, 42, 7, 123456789, -5, 2147483647, 1004 };
    static const int kWideWant[9] = { 5, 8, 8, 8, 10, 9, 7, 0, 0 };   /* + 16 */
    int nw = 0; void** tw = Towers(&nw, true);
    int badWide = 0; char wtitle[48];
    for (int k = 0; tw && k < 9; k++) {
        const int maps[3] = { -1, 0, 17 };
        for (int m = 0; m < 3; m++) {
            int i = SelectWide(maps[m], kWideSeeds[k], true);
            int want = maps[m] == 17 ? 1 : kWideWant[k];
            if (i >= 0 && i < nw) Title(tw[i], wtitle, sizeof wtitle); else wtitle[0] = 0;
            if (std::strcmp(wtitle, kSafe[want])) { badWide++; netws::Log("[tfex] wide tower self-test: seed %d map %d -> \"%s\", model \"%s\"", kWideSeeds[k], maps[m], wtitle, kSafe[want]); }
        }
    }
    netws::Log("[tfex] wide tower self-test (model WideTower, 9 seeds x MapId -1, 0, 17): %s", !tw ? "no 6P towers" : badWide ? "WRONG" : "OK");
}
int Select(int mapId, int seed, bool wide) {
    SelfTest();
    if (wide) return SelectWide(mapId, seed);
    g_lastRandom = mapId < 0;   /* a standard lobby: no hand pick (TF.EX's generated map screen picks: MapScenePatch) */
    return -1;
}
bool LastWasRandom() { return g_lastRandom; }
/* The map screen's button for tower `index` (VersusTowers, wide: the 6P towers): a VersusMapButton whose Title (MapButton +0x78, the
 * TowerMapData's title, what TF.EX's IsNetplaySafe compares) is the tower's. */
bool ButtonIsTower(void* button, int index, bool wide) {
    int n = 0; void** t = Towers(&n, wide);
    if (!button || !t || index < 0 || index >= n || !At<void*>(button, 0x70)) return false;   /* MapButton.Data null: RANDOM and the like */
    const void* title = At<void*>(button, 0x78);
    char want[48]; Title(t[index], want, sizeof want);
    if (!title) return false;
    int len = At<int32_t>(title, 0x10);
    const char16_t* ch = reinterpret_cast<const char16_t*>(reinterpret_cast<uintptr_t>(title) + 0x14);
    int i = 0;
    for (; i < len && want[i]; i++) if ((ch[i] < 0x80 ? (char)ch[i] : '?') != want[i]) return false;
    return i == len && !want[i];
}
/* VersusTowerData.GetLevelSystem() (virtual, slot 0x20): a new VersusLevelSystem for that tower. */
void* LevelSystem(int index, bool wide) {
    int n = 0; void** t = Towers(&n, wide);
    return t && index >= 0 && index < n ? Virt<void*>(t[index], 0x20) : nullptr;
}
} // namespace Tower

/* MatchSettings.GoalScore (PC): ceil(base * GoalMultiplier[MatchLength]), multipliers { 0.01, 0.5, 1, 2 }; base with 2 players: 5 for
 * Last Man Standing (PlayerGoals(5, 4, 3)), Head Hunters (PlayerGoals(5, 8, 10)) and Team Deathmatch (5). So Instant 1, Quick 3,
 * Standard 5, Epic 10 (as logged in browser lobby matches for the first three). */
static int GoalFor(int matchLength) {
    static const float mult[4] = { 0.01f, 0.5f, 1.f, 2.f };
    int len = matchLength < 0 ? 0 : matchLength > 3 ? 3 : matchLength;
    float g = 5.f * mult[len];
    int i = (int)g;
    return (float)i < g ? i + 1 : i;   /* Math.Ceiling */
}

void NetplayManagerStartLobbyMatch() {
    auto& mm = MatchmakingService::Instance();
    gen::ws::Lobby* l = gen::svc::GetOwnLobby();   /* TF.EX's own lobby (2026-10-18: no longer the hand models::Lobby copy) */
    if (!l || !l->GameData.has) { netws::Log("[tfex] StartLobby: no own lobby"); return; }
    const gen::ws::GameData& gd = l->GameData.value;
    const gen::ws::Player* bySeat[kMaxPlayers];
    int n = SortedBySeat(l, bySeat);
    if (auto* sp = gen::Auto_Lobby::get_SessionPlayers(l); sp && sp->count > kMaxPlayers) {
        netws::Log("[tfex] StartLobby: ERROR: %d players, more than the lobby match's %d seats (xplay::kNpSeats, a pools error): not started", sp->count, kMaxPlayers);
        return;
    }
    xplay::LobbyMatch m{};
    m.seed = gd.Seed;
    m.players = n;
    m.goal = GoalFor(gd.MatchLength);
    /* this device's handle: the session index of TF.EX's local seat (the device's main player, MatchmakingService.GetLocalSeat). Several
     * players can share this device's peer id (#283 local players): the last one with it was taken before, and the main controller then
     * played the second player's seat while the main seat fell back to the same controller (console 2026-10-25) */
    m.handle = -1;
    const int localSeat = mm.GetLocalSeat();
    for (int i = 0; i < n; i++) if (bySeat[i]->Seat == localSeat && !std::strcmp(bySeat[i]->RoomPeerId.s, mm.GetRoomPeerId())) m.handle = i;
    m.mode = gd.Mode;
    bool teamMode = gen::Auto_Lobby::get_IsTeamMode(l);
    for (int i = 0; i < kMaxPlayers; i++) m.team[i] = (i < n && teamMode) ? bySeat[i]->Team : -1;
    if (n < 2 || m.handle < 0) { netws::Log("[tfex] StartLobby: %d players, my handle %d: not a match of 2 or more players I play in", n, m.handle); return; }
    char host[128]; int port = 0; bool tls = false;
    if (!ParseNetplayServer(host, sizeof host, &port, &tls)) { netws::Log("[tfex] StartLobby: bad server %s", NetplayPreferences::Server); return; }
    netws::Log("[tfex] lobby match: seed %d, mode %d, goal %d (length %d), map %d%s, handle %d, teams %d %d, room %s:%d/room/%s",
               m.seed, m.mode, m.goal, gd.MatchLength, gd.MapId, MainMenuPatch::IsWide() ? " (wide)" : "", m.handle, m.team[0], m.team[1],
               host, port, l->RoomId.s);
    Tower::ClearNoRandom();
    /* WiderSet plays the wide set from the player's own setting (WiderSetModApi.IsWide, chosen on the STANDARD / WIDE screen), not from
     * the lobby: the lobby's Teuria.WiderSet entry only gates joining (CanUseWiderSet), and a quick-play lobby has no Mods at all (the
     * server matches equal is_wide instead). Reading the lobby made a wide quick play narrow on the Switch while the browser played wide
     * (console, 2026-10-17 10:43). */
    m.wide = MainMenuPatch::IsWide();
    m.tower = Tower::Select(gd.MapId, m.seed, m.wide);
    m.randomMap = Tower::LastWasRandom();
    /* the match's connection is TF.EX's: ggrs_ffi joins the lobby's room (GGRSConfig.ServerConf.RoomUrl, SetRoomAndServerMode) from
     * NetplayManager.Init's Connect, over net.cpp's ffisock thread (the hand RunRtc request is gone) */
    xplay::StartLobbyMatch(m);
}

} // namespace tfex

void* xplay::LobbyLevelSystem(int tower, bool wide) { return tfex::Tower::LevelSystem(tower, wide); }
/* For xplay.cpp's GGRS event loop (one line there, a block-scope declaration). */
