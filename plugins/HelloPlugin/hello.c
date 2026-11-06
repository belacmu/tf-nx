/* HelloPlugin: the code-plugin proof (docs/DISTRIBUTION-2026-11-04.md "Code plugins"). One hook on Monocle's Music.Play(string)
 * (+0x9410d0; FrModsBoot.cpp hooks it too: hooks chain), a log line for its first three calls, a counter in writable data. */
#include "plugin_api.h"

static const XplayPluginApi* g_api;
static uintptr_t g_orig;
static int g_calls;

static void MusicPlay(void* name) {
    g_calls++;
    ((void (*)(void*))g_orig)(name);
    if (g_calls <= 3) g_api->log("HelloPlugin: Music.Play call %d done", g_calls);
}

int xplay_plugin_main(const XplayPluginApi* api) {
    g_api = api;
    if (api->version != XPLAY_PLUGIN_API_VERSION) { api->log("HelloPlugin: core API %u, built for %u: not started", api->version, XPLAY_PLUGIN_API_VERSION); return 1; }
    g_orig = api->hook(0x9410d0, (const void*)MusicPlay);
    api->log("HelloPlugin: hooked Music.Play (%s), game at 0x%lx", g_orig ? "ok" : "REFUSED", (unsigned long)api->game_base());
    return g_orig ? 0 : 2;
}
