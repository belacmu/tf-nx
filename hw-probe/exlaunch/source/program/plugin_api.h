/* plugin_api.h: what the core module gives a code plugin (docs/DISTRIBUTION-2026-11-04.md "Code plugins").
 *
 * A plugin is a position-independent AArch64 ELF (built by plugins/build.sh) at sd:/xplay/mods/<Mod>/plugin.elf. At boot the core
 * (plugins.cpp) copies it into its plugin code area, applies its relocations and calls its ELF entry point with this table. A plugin
 * imports nothing: everything it needs from the core comes through here. Plain C so a plugin needs no C++ runtime. */
#pragma once
#include <stdint.h>

#define XPLAY_PLUGIN_API_VERSION 1u

typedef struct XplayPluginApi {
    uint32_t version;                 /* XPLAY_PLUGIN_API_VERSION of the core */
    uint32_t size;                    /* sizeof(XplayPluginApi) of the core */
    const char* mod;                  /* the mod folder's name (sd:/xplay/mods/<mod>/) */
    /* one line in the match log, printf style; the core prefixes "[plugin] " */
    void (*log)(const char* fmt, ...);
    /* the game's main module start (offsets from tools/re are relative to it) */
    uintptr_t (*game_base)(void);
    /* hooks the game function at game_base() + offset; returns the trampoline that calls the original (0: refused, logged) */
    uintptr_t (*hook)(uintptr_t offset, const void* callback);
} XplayPluginApi;

/* The plugin's entry point (its ELF entry): returns 0 when it started. */
typedef int (*XplayPluginMain)(const XplayPluginApi* api);
