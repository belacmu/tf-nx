// The ggrs-ffi port's glue (translator constructs 2 and 4): what TF.EX's NetplayManager hands ggrs_ffi, built the Switch's way.
using System;
using System.Collections.Generic;
using System.Threading;
using TF.EX.Common.Handle;
using TF.EX.Domain.Models;

namespace TfexGlue
{
    public static class Ffi
    {
        [Cpp("tfex::gen::ffi::BytesOfString($0)", Include = "tfexgen_ffi.hpp")]
        static extern SafeBytes<GGRSConfig> BytesOf(string json);

        // `new SafeBytes<GGRSConfig>(GGRSConfig, true)` (NativeInit): TF.EX's JSON of the config (MessagePack contractless -> JSON: the
        // property names as they are, in declaration order, null as null, enums as numbers), as ggrs-ffi's AppConfig reads it (serde
        // PascalCase), in a runtime buffer (ffi::BytesOfString) that the `using`'s Dispose frees.
        public static SafeBytes<GGRSConfig> GgrsConfigBytes(GGRSConfig config, bool useJson) => BytesOf(ConfigJson(config));

        // `Task.Run(() => Connect(roundLogic), token)` (NetplayManager.Init): on the PC Connect blocks in netplay_init on this worker until
        // the peers connect. TFEX-DIFF S-native: a game System.Threading.Thread, which on the Switch build runs its work on the caller (the
        // game thread); so the Switch's netplay_init doesn't wait: it returns once the transport is up and netplay_switch_pump finishes
        // the wait for the peers a step a frame (ggrs-ffi-switch neplay.rs PendingInit). One at a time (a second Run while one runs is
        // refused and logged); the token isn't watched.
        // `new SafeRawBytes(state)` / `new SafeRawBytes(result.Data, cleanup)` (SaveGameState / LoadGameState): the bytes ggrs_ffi moves; on the
        // Switch the 16-byte snapshot handle (TFEX-DIFF, performance: rollback.cpp's memory snapshot instead of TF.State's bytes)
        [Cpp("tfex::gen::ffi::RawOfArr($0)", Include = "tfexgen_ffi.hpp")]
        public static extern SafeRawBytes RawBytes(byte[] bytes);
        [Cpp("tfex::gen::ffi::RawOfFFI($0)", Include = "tfexgen_ffi.hpp")]
        public static extern SafeRawBytes RawBytesFFI(SafeBytesFFI bytes, Action cleanup);

        static Thread worker;
        [Cpp("tfex::gen::ffi::Log(\"[ffi] RunWorker: a worker already runs: refused\")", Include = "tfexgen_ffi.hpp")]
        static extern void LogBusy();
        public static void RunWorker(Action work, CancellationToken token)
        {
            if (worker != null && worker.IsAlive) { LogBusy(); return; }
            worker = new Thread(() => work());
            worker.IsBackground = true;
            worker.Start();
        }

        // TF.EX's MatchmakingService.ConnectToServerAndListen: `Task.Run(async () => { while (...) { ... ReceiveAsync ... } }, cancellationToken)`,
        // its listen loop. TFEX-DIFF S-async: on the game thread, a pass each frame (PumpListener, from MatchmakingService.cpp's PumpTransport):
        // the Switch build's managed Thread runs its work on the caller (a worker would block the game), and generated code can't run on a
        // native thread (its allocations). A pass takes the frames that have arrived and returns when the receive has nothing yet
        // (gen::WouldBlock); the loop is over when a pass returns otherwise (its catch on a lost connection, or a Close frame). A new
        // connection's loop replaces the last one (TF.EX's Close has cancelled it).
        static Func<System.Threading.Tasks.Task> listenWork;
        [Cpp("gen::TakeWouldBlock()")]
        static extern bool PassYielded();
        public static void RunListener(Func<System.Threading.Tasks.Task> work, CancellationToken token) => listenWork = work;
        public static void PumpListener()
        {
            var work = listenWork;
            if (work == null) return;
            PassYielded();   // (a stale mark from another caller: cleared)
            work();
            if (!PassYielded() && listenWork == work) listenWork = null;
        }
        // TFEX-DIFF S-net: the listen loop's `await HandleMessageContents(message)`: the message to the game thread, where TF.EX's handler runs
        // (MatchmakingService.cpp PumpTransport, each frame before TF.EX's prefix): the runtime's object pools aren't thread-safe
        [Cpp("tfex::gen::svc::PostServerMessage($1)", Include = "tfexgen_services.hpp")]
        public static extern void HandleOnGameThread(TF.EX.Domain.Services.MatchmakingService service, string message);
        // `new ArraySegment<byte>(buffer)` (the socket's buffers): the array itself, as a segment over all of it (S-net)
        public static byte[] Segment(byte[] array) => array;
        // TF.EX's Close(): `cancellationTokenSource.Cancel()` ends the listen loop's receive (ClientWebSocket stand-in: WsAbortAll)
        [Cpp("tfex::gen::bcl::WsAbortAll()", Include = "tfexgen_bcl.hpp")]
        public static extern void CancelSockets();

        static string Q(string s)
        {
            if (s == null) return "null";
            var r = "\"";
            for (int i = 0; i < s.Length; i++) { var ch = s[i]; r += ch == '"' ? "\\\"" : ch == '\\' ? "\\\\" : ch.ToString(); }
            return r + "\"";
        }
        static string B(bool b) => b ? "true" : "false";
        static string List(ICollection<string> items)
        {
            if (items == null) return "null";
            var r = "[";
            bool first = true;
            foreach (var s in items) { r += (first ? "" : ",") + Q(s); first = false; }
            return r + "]";
        }

        static string ConfigJson(GGRSConfig c)
        {
            if (c == null) return "null";
            return "{\"InputDelay\":" + c.InputDelay + ",\"MaxInputDelay\":" + c.MaxInputDelay + ",\"Fps\":" + c.Fps +
                   ",\"ConnectionTimeoutMs\":" + c.ConnectionTimeoutMs + ",\"Name\":" + Q(c.Name) +
                   ",\"Netplay\":" + NetplayJson(c.Netplay) + ",\"Test\":" + (c.Test == null ? "null" : "{\"CheckDistance\":" + c.Test.CheckDistance + "}") + "}";
        }

        static string NetplayJson(NetplayConfig n)
        {
            if (n == null) return "null";
            var local = n.LocalConf == null ? "null" : "{\"RemoteAddr\":" + Q(n.LocalConf.RemoteAddr) + ",\"Port\":" + n.LocalConf.Port + ",\"PlayerDraw\":" + (int)n.LocalConf.PlayerDraw + "}";
            var server = n.ServerConf == null ? "null" : "{\"RoomUrl\":" + Q(n.ServerConf.RoomUrl) + ",\"IsHost\":" + B(n.ServerConf.IsHost) +
                         ",\"RelayUrl\":" + Q(n.ServerConf.RelayUrl) + ",\"RelayPeers\":" + List(n.ServerConf.RelayPeers) + "}";
            var spectator = n.SpectatorConf == null ? "null" : "{\"RoomUrl\":" + Q(n.SpectatorConf.RoomUrl) + ",\"ToSpectate\":" + Q(n.SpectatorConf.ToSpectate) +
                            ",\"RelayUrl\":" + Q(n.SpectatorConf.RelayUrl) + "}";
            return "{\"NumPlayers\":" + n.NumPlayers + ",\"Spectators\":" + List(n.Spectators) + ",\"Players\":" + List(n.Players) +
                   ",\"LocalPeerId\":" + Q(n.LocalPeerId) + ",\"LocalConf\":" + local + ",\"ServerConf\":" + server + ",\"SpectatorConf\":" + spectator + "}";
        }
    }
}
