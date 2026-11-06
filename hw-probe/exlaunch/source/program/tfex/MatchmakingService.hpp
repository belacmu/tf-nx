/* TF.EX's MatchmakingService (TF.EX.Domain.Services.MatchmakingService; switch-online/TFEX-UI-SPEC.md 3.1,
 * switch-online/LOBBY-PROTOCOL.md): the lobby connection to TF.EX's server (`<server>/ws`). Same method names. The server's messages
 * reach TF.EX's HandleMessageContents on the game thread (PumpTransport), whose actions TF.EX's own queue runs (its TFGame.Update prefix). */
#pragma once
/* the TF.EX version this port implements (the matchmaking URL's ?version=, the version texts): TF.EX's meta.json, generated */
#include "gen/tfexgen_version.hpp"
#include <cstdint>

namespace tfex {

using Action = void (*)(void* ctx);
constexpr int kLobbySeats = 4;   /* a lobby's seats */
constexpr int kPeerId = 72;      /* a peer / room id (a UUID) with its terminator, as TF.EX's models hold it */

/* TF.EX's settings (TF.EX.Core/NetplaySettings.cs, FortRise's Saves/TF.EX/TF.EX.settings.json on the PC) in the same shape: the
 * properties below, kept in sd:/xplay/TF.EX.settings.json (the same JSON keys). TF.EX's NetplaySettings.Apply (generated) normalizes
 * them into TF.EX's NetplayPreferences, which TF.EX's code reads. First run (no file): name / server / inputdelay imported from
 * TF.EX's defaults, then saved. */
struct NetplaySettingsFile {
    static constexpr const char* kDefaultServer = TFNX_SERVER;   /* config.mk (local.mk, else TF.EX's official server): the first-run default and TF.EX's LOCAL preset */
    static bool Existed;   /* the file was there at boot: TF.EX's IsReturningPlayer */
    static void Save();    /* the object's properties as JSON into sd:/xplay/TF.EX.settings.json when they changed (generated: tfexgen_modsettings) */
    static bool Apply();   /* the object made, the file read over it, TF.EX's NetplaySettings.Apply (once; game thread, a menu) */
    static void* Object();        /* TF.EX's NetplaySettings object (made by Apply), or null */
    static void StoreObject();    /* saved, and the C-string copies (after TF.EX's options entries or its changelog / tutorial MarkSeen changed it) */
};
/* The Switch-only connection settings (S-native: ip, rtcbind, stun) from sd:/xplay/netplay.txt, and C-string copies of TF.EX's NetplayPreferences.Name /
 * Server for the native readers (the transport, WebRTC). */
struct NetplayPreferences {
    static char Server[128];      /* TF.EX's NetplayPreferences.Server, e.g. TF.EX's "wss://tfex-server.balatro-vs-matchmaking.eu" */
    static char Name[16];         /* TF.EX's NetplayPreferences.Name */
    static char Ip[32];           /* "ip <ipv4>": used when name resolution fails */
    static char RtcBind[32];      /* "rtcbind <ipv4>": the WebRTC UDP socket's address (default: nifm's primary address) */
    static char StunHost[64];     /* "stun <host> <port> [<ipv4 if name resolution fails>]" or "stun off": for the public address */
    static int StunPort;
    static char StunIp[32];
    static void Load();           /* netplay.txt (the Switch's connection keys) and TF.EX.settings.json (once) */
};

/* TF.EX's own MatchmakingService (generated, tfex/gen/tfexgen_MatchmakingService.*) is the lobby: it owns the lobby state and handles
 * the server's messages (on the game thread, queued by its listen thread). Its connection is TF.EX's own code over the runtime's
 * ClientWebSocket (tfexgen_bcl.cpp); this class is the Switch's part under it (the messages to the game thread, ping measurement: TF.EX's
 * excluded edge) plus thin forwarders for hand code. The object is made on the first
 * main menu (PumpTransport). TF.EX's code reaches it through tfex/gen services; hand code reads TF.EX's lobby with
 * gen::svc::GetOwnLobby(). */
class MatchmakingService {
public:
    static void* GenObject();
    /* connection (the transport) */
    bool IsConnectedToServer();          /* TF.EX's (its ClientWebSocket is the runtime's, tfexgen_bcl.cpp) */
    void DisconnectFromServer();         /* TF.EX's */
    void ResetPeer();
    const char* GetRoomPeerId();
    void SetRoomPeerId(const char* id);  /* MatchmakingService.peerId's storage ([service] rule) */
    /* forwarders to TF.EX's MatchmakingService (null-guarded: its object is made on the first menu) */
    void ClearOwnLobby();                /* TF.EX's UpdateOwnLobby(new Lobby()) */
    int GetLocalSeat();
    void LeaveLobby(Action onSuccess, Action onFail, void* ctx);
    bool IsLobbyReady();                 /* the host started the match (StartLobby received) */
    bool CanHostStart();
    void ShowMatchStartWarnings(void* scene);            /* TF.EX's, once a match is synchronized in round 0 */
    void SendJson(const char* text, int len);   /* TF.EX's Send (a binary frame of UTF-8 JSON, queued for its listen thread) */
    static void PostServerMessage(const char* text, int len);   /* TF.EX's listen thread: a message for HandleMessageContents (PumpTransport) */
    /* game thread */
    void PumpTransport();   /* every frame, before TF.EX's TFGame.Update prefix (xplay.cpp FrameShell) */

    static MatchmakingService& Instance();
};

} // namespace tfex
