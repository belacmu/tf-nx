/* The pool registry (pools.hpp). */
#include "pools.hpp"
#include "hwlog.hpp"
#include <cstdio>
#include <cstring>

namespace pools {
namespace {
struct Entry { const char* name; UseFn fn; int peak; int exhausted; };
constexpr int kMax = 48;   /* (this registry is a pool too: an unregistered pool is reported by name) */
Entry g_entries[kMax];
int g_n = 0, g_total = 0;
Entry* Find(const char* name) {
    for (int i = 0; i < g_n; i++) if (g_entries[i].name == name || !std::strcmp(g_entries[i].name, name)) return &g_entries[i];
    return nullptr;
}
}

void Register(const char* name, UseFn fn) {
    if (Entry* e = Find(name)) { e->fn = fn; return; }
    if (g_n == kMax) { hw::Log("[pools] EXHAUSTED the pool registry (capacity %d): \"%s\" not registered", kMax, name); return; }
    g_entries[g_n++] = Entry{ name, fn, 0, 0 };
}

void Report(const char* when) {
    char b[1500];
    int n = std::snprintf(b, sizeof b, "[pools] %s:", when);
    char full[300] = ""; int nf = 0;
    for (int i = 0; i < g_n && n < (int)sizeof b - 80; i++) {
        Entry& e = g_entries[i];
        Use u;
        e.fn(&u);
        if (u.peak < 0) { if (u.used > e.peak) e.peak = u.used; u.peak = e.peak; }
        else if (u.peak > e.peak) e.peak = u.peak;
        n += std::snprintf(b + n, sizeof b - n, " %s %d/%d", e.name, u.used, u.cap);
        if (e.peak != u.used) n += std::snprintf(b + n, sizeof b - n, " (peak %d)", e.peak);
        if (e.exhausted) n += std::snprintf(b + n, sizeof b - n, " (refused %d)", e.exhausted);
        n += std::snprintf(b + n, sizeof b - n, ",");
        if (u.cap > 0 && (e.peak >= u.cap || e.exhausted) && nf < (int)sizeof full - 40)
            nf += std::snprintf(full + nf, sizeof full - nf, "%s%s", nf ? ", " : "", e.name);
    }
    if (n > 0 && b[n - 1] == ',') b[--n] = 0;
    hw::Log("%s", b);
    if (nf) hw::Log("[pools] %s: FULL at some point: %s", when, full);
}

void Exhausted(const char* name, int cap, const char* what) {
    g_total++;
    Entry* e = Find(name);
    int k = e ? ++e->exhausted : g_total;
    hw::Log("[pools] EXHAUSTED %s (capacity %d): %s (refusal %d)", name, cap, what ? what : "", k);
}

int ExhaustedCount() { return g_total; }

} // namespace pools
