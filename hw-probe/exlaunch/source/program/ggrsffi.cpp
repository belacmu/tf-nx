/* ggrsffi.hpp. TFEX-DIFF: on PC ggrs_ffi runs its poller, matchbox loop and ping measurements on threads; the Switch's Rust std
 * has none, so they run in netplay_switch_pump once a frame here, and the sockets on net.cpp's ffisock thread. */
#include "ggrsffi.hpp"
#include "hwlog.hpp"
#include "tfex/MatchmakingService.hpp"
#include <cstdint>

extern "C" {
void netplay_switch_pump();
int32_t netplay_switch_log(char* buf, int32_t cap);
bool ggrsffi_sockets_start(const char* bindIp, const char* stunHost, int stunPort, const char* stunIp, const char* serverIp);
}

namespace ggrsffi {
namespace {
bool g_started = false, g_failed = false;
}

void Frame() {
    if (g_failed) return;
    static int frames = 0;
    if (!g_started && ++frames < 180) return;   /* (xplay mounts the SD at frame 120: NetplayPreferences' netplay.txt is readable by then) */
    if (!g_started) {
        using tfex::NetplayPreferences;
        NetplayPreferences::Load();
        g_started = ggrsffi_sockets_start(NetplayPreferences::RtcBind, NetplayPreferences::StunHost, NetplayPreferences::StunPort,
                                          NetplayPreferences::StunIp, NetplayPreferences::Ip);
        if (!g_started) { g_failed = true; hw::Log("[ggrsffi] ERROR: socket thread not started: ggrs_ffi has no transport"); return; }
    }
    netplay_switch_pump();
    static char line[512];
    for (int n = 0; n < 64 && netplay_switch_log(line, sizeof line); n++) hw::Log("[ggrsffi] %s", line);   /* (ggrs-ffi's queue holds 512; the rest next frame) */
}
}
