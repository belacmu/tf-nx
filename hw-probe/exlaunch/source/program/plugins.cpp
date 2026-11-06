/* plugins.cpp: code plugins from the SD card (docs/DISTRIBUTION-2026-11-04.md "Code plugins"; the API: plugin_api.h).
 *
 * For each folder sd:/xplay/mods/<Mod>/ holding a plugin.elf, at boot: the ELF's kPtLoad segments are copied into the plugin code area
 * (a page-aligned stretch of this module's own .text, zeros in the file), its relocations applied, each page given its segment's
 * permission (svcSetProcessMemoryPermission: code RX, read-only R, data RW) and its entry point called with the API table.
 * Fixed-size pools (logged at boot, refusals are errors): the code area (kAreaSize) and the plugin slots (kMaxPlugins). */
#include "lib.hpp"
#include "hwlog.hpp"
#include "pools.hpp"
#include "plugin_api.h"
#include "nn/fs.hpp"
#include <lib/util/sys/cur_proc_handle.hpp>
#include <lib/util/sys/modules.hpp>
#include <lib/nx/kernel/virtmem.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace plugins {
void LoadAll();
namespace {

constexpr size_t kPage = 0x1000, kAreaSize = 0x40000, kMaxFile = 0x40000;
constexpr int kMaxPlugins = 8;

__attribute__((section(".text.xplay_plugins"))) alignas(kPage) const uint8_t g_area[kAreaSize] = {};
size_t g_areaUsed = 0;
int g_count = 0;
alignas(16) uint8_t g_file[kMaxFile];

/* ELF64 (only what the loader reads) */
struct Ehdr { uint8_t ident[16]; uint16_t type, machine; uint32_t version; uint64_t entry, phoff, shoff; uint32_t flags;
              uint16_t ehsize, phentsize, phnum, shentsize, shnum, shstrndx; };
struct Phdr { uint32_t type, flags; uint64_t offset, vaddr, paddr, filesz, memsz, align; };
struct Dyn { int64_t tag; uint64_t val; };
struct Rela { uint64_t offset, info; int64_t addend; };
struct Sym { uint32_t name; uint8_t info, other; uint16_t shndx; uint64_t value, size; };
constexpr uint32_t kPtLoad = 1, kPtDynamic = 2, kPfX = 1, kPfW = 2;
constexpr int64_t kDtNull = 0, kDtPltRelSz = 2, kDtStrTab = 5, kDtSymTab = 6, kDtRela = 7, kDtRelaSz = 8, kDtJmpRel = 23;
constexpr uint32_t R_ABS64 = 257, R_GLOB_DAT = 1025, R_JUMP_SLOT = 1026, R_RELATIVE = 1027;

void ApiLog(const char* fmt, ...) {
    char line[256];
    va_list ap; va_start(ap, fmt); std::vsnprintf(line, sizeof line, fmt, ap); va_end(ap);
    hw::Log("[plugin] %s", line);
}
uintptr_t ApiGameBase() { return exl::util::GetMainModuleInfo().m_Total.m_Start; }
uintptr_t ApiHook(uintptr_t offset, const void* callback) {
    const auto& text = exl::util::GetMainModuleInfo().m_Text;
    if (!callback || offset >= text.m_Size) { hw::Log("[plugins] ERROR: hook at +0x%lx refused (outside the game's code)", (unsigned long)offset); return 0; }
    return exl::hook::Hook(ApiGameBase() + offset, reinterpret_cast<uintptr_t>(callback), true);
}
const XplayPluginApi g_api = { XPLAY_PLUGIN_API_VERSION, sizeof(XplayPluginApi), nullptr, ApiLog, ApiGameBase, ApiHook };

bool ReadFile(const char* path, size_t* size) {
    nn::fs::FileHandle h;
    if (nn::fs::OpenFile(&h, path, nn::fs::OpenMode_Read) != 0) return false;
    long sz = 0; nn::fs::GetFileSize(&sz, h);
    if (sz <= 0 || sz > (long)kMaxFile) {
        nn::fs::CloseFile(h);
        pools::Exhausted("plugin file buffer", (int)kMaxFile, path);
        return false;
    }
    bool ok = nn::fs::ReadFile(h, 0, g_file, (size_t)sz) == 0;
    nn::fs::CloseFile(h);
    *size = (size_t)sz;
    return ok;
}

/* one relocation table; false (logged) on a type or symbol the loader doesn't handle */
bool Relocate(const char* mod, uint8_t* base, uint8_t* rw, const Rela* r, size_t bytes, const Sym* syms, const char* strs) {
    for (size_t i = 0; i < bytes / sizeof(Rela); i++) {
        uint32_t type = (uint32_t)r[i].info, si = (uint32_t)(r[i].info >> 32);
        uint64_t* at = reinterpret_cast<uint64_t*>(rw + r[i].offset);
        if (type == R_RELATIVE) { *at = (uint64_t)base + r[i].addend; continue; }
        if (type == R_ABS64 || type == R_GLOB_DAT || type == R_JUMP_SLOT) {
            const Sym* s = syms ? &syms[si] : nullptr;
            if (!s || s->shndx == 0) {
                hw::Log("[plugins] ERROR: %s imports %s (a plugin imports nothing: use the API table): not loaded", mod,
                        s && strs ? strs + s->name : "?");
                return false;
            }
            *at = (uint64_t)base + s->value + r[i].addend;
            continue;
        }
        hw::Log("[plugins] ERROR: %s has relocation type %u (not handled): not loaded", mod, type);
        return false;
    }
    return true;
}

void Load(const char* mod, const char* path) {
    if (g_count == kMaxPlugins) { pools::Exhausted("plugin slots", kMaxPlugins, mod); return; }
    size_t size = 0;
    if (!ReadFile(path, &size)) { hw::Log("[plugins] ERROR: can't read %s", path); return; }
    const Ehdr* eh = reinterpret_cast<const Ehdr*>(g_file);
    if (size < sizeof(Ehdr) || std::memcmp(eh->ident, "\x7f" "ELF\x02\x01", 6) != 0 || eh->machine != 183 ||
        eh->phoff + (uint64_t)eh->phnum * sizeof(Phdr) > size) {
        hw::Log("[plugins] ERROR: %s isn't an AArch64 ELF: not loaded", path); return;
    }
    const Phdr* ph = reinterpret_cast<const Phdr*>(g_file + eh->phoff);
    uint64_t end = 0; const Phdr* dyn = nullptr;
    for (int i = 0; i < eh->phnum; i++) {
        if (ph[i].type == kPtDynamic) dyn = &ph[i];
        if (ph[i].type != kPtLoad) continue;
        if (ph[i].offset + ph[i].filesz > size) { hw::Log("[plugins] ERROR: %s is cut short: not loaded", path); return; }
        if (ph[i].vaddr + ph[i].memsz > end) end = ph[i].vaddr + ph[i].memsz;
    }
    size_t need = (end + kPage - 1) & ~(kPage - 1);
    if (need == 0 || g_areaUsed + need > kAreaSize) {
        pools::Exhausted("plugin code area", (int)kAreaSize, mod); return;
    }
    uint8_t* base = const_cast<uint8_t*>(g_area) + g_areaUsed;
    /* written through a writable alias (as exlaunch's hook area): the area's pages keep their first state, and Ryujinx allows only
     * one permission change from it (code -> data or read-only; nothing back to executable: 0xd401) */
    Handle proc = exl::util::proc_handle::Get();
    virtmemLock();
    uint8_t* rw = static_cast<uint8_t*>(virtmemFindAslr(need, 0));
    Result rc = rw ? svcMapProcessMemory(rw, proc, (u64)base, need) : (Result)1;
    virtmemUnlock();
    if (R_FAILED(rc)) { hw::Log("[plugins] ERROR: %s: no writable view of the code area (0x%x)", mod, rc); return; }
    std::memset(rw, 0, need);
    for (int i = 0; i < eh->phnum; i++)
        if (ph[i].type == kPtLoad) std::memcpy(rw + ph[i].vaddr, g_file + ph[i].offset, ph[i].filesz);
    bool ok = true;
    if (dyn) {
        const Dyn* d = reinterpret_cast<const Dyn*>(rw + dyn->vaddr);
        const Rela* rela = nullptr; const Rela* jmp = nullptr; size_t relasz = 0, jmpsz = 0;
        const Sym* syms = nullptr; const char* strs = nullptr;
        for (; d->tag != kDtNull; d++) {
            switch (d->tag) {
                case kDtRela: rela = reinterpret_cast<const Rela*>(rw + d->val); break;
                case kDtRelaSz: relasz = d->val; break;
                case kDtJmpRel: jmp = reinterpret_cast<const Rela*>(rw + d->val); break;
                case kDtPltRelSz: jmpsz = d->val; break;
                case kDtSymTab: syms = reinterpret_cast<const Sym*>(rw + d->val); break;
                case kDtStrTab: strs = reinterpret_cast<const char*>(rw + d->val); break;
            }
        }
        if (rela) ok = Relocate(mod, base, rw, rela, relasz, syms, strs);
        if (ok && jmp) ok = Relocate(mod, base, rw, jmp, jmpsz, syms, strs);
    }
    armDCacheFlush(rw, need);
    rc = svcUnmapProcessMemory(rw, proc, (u64)base, need);
    if (R_FAILED(rc)) hw::Log("[plugins] %s: the writable view stays mapped (0x%x)", mod, rc);
    /* each page: X from an executable segment, W from a writable one; a page both is refused */
    int pages[3] = {0, 0, 0};   /* rx, r, rw */
    for (size_t off = 0; ok && off < need; off += kPage) {
        uint32_t f = 0;
        for (int i = 0; i < eh->phnum; i++)
            if (ph[i].type == kPtLoad && ph[i].vaddr < off + kPage && ph[i].vaddr + ph[i].memsz > off) f |= ph[i].flags;
        if ((f & kPfX) && (f & kPfW)) { hw::Log("[plugins] ERROR: %s has a page both writable and executable (+0x%lx): not loaded", mod, (unsigned long)off); ok = false; break; }
        pages[(f & kPfX) ? 0 : (f & kPfW) ? 2 : 1]++;
        if (f & kPfX) continue;   /* (already read+execute: the module's own code pages) */
        rc = svcSetProcessMemoryPermission(proc, (u64)(base + off), kPage, (f & kPfW) ? Perm_Rw : Perm_R);
        if (R_FAILED(rc)) { hw::Log("[plugins] ERROR: %s: page +0x%lx's permission refused (0x%x)", mod, (unsigned long)off, rc); ok = false; }
    }
    armICacheInvalidate(base, need);
    g_areaUsed += need;   /* (a refused plugin's pages stay used: the area is never reused) */
    if (!ok) return;
    hw::Log("[plugins] %s: %lu KB at +0x%lx of the code area (%d code, %d read-only, %d data pages), starting it", mod,
            (unsigned long)(need / 1024), (unsigned long)(base - g_area), pages[0], pages[1], pages[2]);
    g_count++;
    static XplayPluginApi apis[kMaxPlugins];   /* (the plugin may keep the pointer) */
    apis[g_count - 1] = g_api;
    static char names[kMaxPlugins][64];
    std::snprintf(names[g_count - 1], sizeof names[0], "%s", mod);
    apis[g_count - 1].mod = names[g_count - 1];
    int r = reinterpret_cast<XplayPluginMain>(base + eh->entry)(&apis[g_count - 1]);
    hw::Log("[plugins] %s started (%d)", mod, r);
}

/* The scan waits for the game's first GameData.Load (the SDK's file system isn't up when exl_main runs: MountSdCard faulted there),
 * before the game's own data loads, as FortRise loads mods at startup. A plugin's hooks go in while this hook runs. */
constexpr uintptr_t kGameDataLoad = 0xa46740;
HOOK_DEFINE_TRAMPOLINE(GameDataLoadHook) {
    static void Callback() {
        static bool done = false;
        if (!done) { done = true; LoadAll(); }
        Orig();
    }
};

void AreaUse(pools::Use* u) { u->used = (int)(g_areaUsed / 1024); u->cap = (int)(kAreaSize / 1024); }
void SlotUse(pools::Use* u) { u->used = g_count; u->cap = kMaxPlugins; }

}  // namespace

void Install() {
    pools::Register("plugin code area KB", AreaUse);
    pools::Register("plugins", SlotUse);
    GameDataLoadHook::InstallAtOffset(kGameDataLoad);
}

void LoadAll() {
    if (!hw::MountSd()) { hw::Log("[plugins] ERROR: no SD card (sd: not mounted): no plugins"); return; }
    nn::fs::DirectoryHandle d;
    if (nn::fs::OpenDirectory(&d, "sd:/xplay/mods", nn::fs::OpenDirectoryMode_Directory) != 0) {
        hw::Log("[plugins] no sd:/xplay/mods: no plugins");
        return;
    }
    static nn::fs::DirectoryEntry entries[64];
    static char mods[64][64];
    int n = 0;
    for (;;) {
        s64 got = 0;
        if (nn::fs::ReadDirectory(&got, entries, d, 64) != 0 || got <= 0) break;
        for (s64 i = 0; i < got && n < 64; i++) std::snprintf(mods[n++], sizeof mods[0], "%s", entries[i].m_Name);
        if (got < 64) break;
    }
    nn::fs::CloseDirectory(d);
    int found = 0;
    for (int i = 0; i < n; i++) {
        char path[160];
        std::snprintf(path, sizeof path, "sd:/xplay/mods/%s/plugin.elf", mods[i]);
        nn::fs::FileHandle h;
        if (nn::fs::OpenFile(&h, path, nn::fs::OpenMode_Read) != 0) continue;
        nn::fs::CloseFile(h);
        found++;
        Load(mods[i], path);
    }
    hw::Log("[plugins] %d plugin(s) found in %d mod folder(s), %d started; code area %lu of %lu KB", found, n, g_count,
            (unsigned long)(g_areaUsed / 1024), (unsigned long)(kAreaSize / 1024));
}

}  // namespace plugins
