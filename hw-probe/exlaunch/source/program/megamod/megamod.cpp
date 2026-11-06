/* The megamod's shared patch-word table (megamod.hpp: megamod::Patch, SetOnline, SetOnlineWord): the instruction and data words the
 * local-play mods change (TowerBall's mode cap, WiderSet's 2-8 lobby: mods/*), put back to the game's while TF.EX's online mode is on.
 * The mods themselves live in mods/<name>/src (docs/PLAN-TFNX-2026-11-04.md). */

#include "lib.hpp"
#include "megamod.hpp"
#if XPLAY_EXTRAS
#include "patch/code_patcher.hpp"  /* exl::patch::CodePatcher: instruction patching */
#include <lib/util/sys/modules.hpp> /* exl::util::GetMainModuleInfo (module base) */
#include "../pools.hpp"

/* ---- megamod::Patch: the words changed for local play, the game's own back online ---- */
namespace megamod {
namespace {
struct Word { uintptr_t off; uint32_t game, local; const char* what; };
constexpr int kMaxPatches = 48;
Word g_words[kMaxPatches];
int g_nWords = 0;
bool g_online = false;
uintptr_t Base() { return exl::util::GetMainModuleInfo().m_Total.m_Start; }
void Write(const Word& w, uint32_t v) { exl::patch::CodePatcher p(w.off); p.Write<uint32_t>(v); }
}

void Patch(uintptr_t off, uint32_t expect, uint32_t repl, const char* what) {
    uint32_t cur = *reinterpret_cast<uint32_t*>(Base() + off);
    if (cur != expect) { MMLOG("[megamod] patch %s @+%#lx: found %08x, expected %08x: left alone", what, (unsigned long)off, cur, expect); return; }
    if (g_nWords == kMaxPatches) { MMLOG("[megamod] ERROR: patch table full (%d): %s not applied", kMaxPatches, what); return; }
    Word& w = g_words[g_nWords++];
    w = Word{off, expect, repl, what};
    if (!g_online) Write(w, repl);
}

void SetOnline(bool on) {
    if (on == g_online) return;
    g_online = on;
    int changed = 0, odd = 0;
    for (int i = 0; i < g_nWords; i++) {
        const Word& w = g_words[i];
        uint32_t want = on ? w.game : w.local, have = on ? w.local : w.game;
        uint32_t cur = *reinterpret_cast<uint32_t*>(Base() + w.off);
        if (cur == want) continue;
        if (cur != have) { MMLOG("[megamod] %s: word is %08x, left alone", w.what, cur); odd++; continue; }
        Write(w, want); changed++;
    }
    MMLOG("[megamod] %s: %d words %s%s", on ? "online (TF.EX's)" : "local play (the megamod's)", changed, on ? "back to the game's" : "changed",
          odd ? " (some unexpected)" : "");
}

/* The 8-seat substrate's words online (a wide TF.EX lobby past 6 seats needs them as WiderSet's on the PC): this word's local value or
 * the game's, while online (SetOnline puts every word back as it switches) */
void SetOnlineWord(uintptr_t off, bool local) {
    if (!g_online) return;
    for (int i = 0; i < g_nWords; i++) {
        const Word& w = g_words[i];
        if (w.off != off) continue;
        uint32_t want = local ? w.local : w.game, cur = *reinterpret_cast<uint32_t*>(Base() + w.off);
        if (cur == want) return;
        if (cur != (local ? w.game : w.local)) { MMLOG("[megamod] %s: word is %08x, left alone", w.what, cur); return; }
        Write(w, want);
        MMLOG("[megamod] online: %s %s", w.what, local ? "(a lobby past 6 seats)" : "back to the game's");
        return;
    }
    MMLOG("[megamod] ERROR: SetOnlineWord @+%#lx: no such patch word", (unsigned long)off);
}

void Install() {
    pools::Register("megamod patch words", [](pools::Use* u) { u->used = g_nWords; u->cap = kMaxPatches; });
}
}  // namespace megamod
#endif   // XPLAY_EXTRAS
