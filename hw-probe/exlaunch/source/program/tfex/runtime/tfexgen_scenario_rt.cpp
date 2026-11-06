/* tfexgen_scenario_rt.hpp: TF.EX's scenario sweep's Regex subset and in-memory map files (TEST ONLY) */
#include "tfexgen_scenario_rt.hpp"
#include "tfexgen_binding.hpp"
#include "tfexgen_ScenarioCatalog.hpp"
#include "tfexgen_ScenarioMapWriter.hpp"
#include "tfexgen_Auto_Scenario.hpp"
#include "../../hwlog.hpp"
#include <cstring>
#include <cstdio>

namespace tfex::gen::scen {
namespace {
/* ---- the Regex subset ---- */
enum Kind : uint8_t { Lit, Any, Class, Open, Close };
enum Quant : uint8_t { One, Star, LazyStar };
struct Node { Kind kind; Quant q; char16_t c; bool neg; uint8_t group; char16_t set[16]; uint8_t nset; };
struct Rx { uint32_t magic; bool singleline; bool ok; int n; int groups; Node nodes[96]; };
constexpr uint32_t kRxMagic = 0x52584458;   /* 'RXDX' */
constexpr int kText = 1 << 16;
char16_t g_in[kText]; int g_inLen;          /* the input (one Replace at a time: the game thread) */
int g_cap[10][2];

bool Parse(Rx& r, const char16_t* p, int n) {
    int stack[10], sp = 0;
    for (int i = 0; i < n; i++) {
        if (r.n >= 96) return false;
        Node& d = r.nodes[r.n]; std::memset(&d, 0, sizeof d);
        char16_t ch = p[i];
        if (ch == u'(') { if (r.groups >= 9 || sp >= 10) return false; d.kind = Open; d.group = (uint8_t)++r.groups; stack[sp++] = d.group; r.n++; continue; }
        if (ch == u')') { if (!sp) return false; d.kind = Close; d.group = (uint8_t)stack[--sp]; r.n++; continue; }
        if (ch == u'.') d.kind = Any;
        else if (ch == u'[') {
            d.kind = Class; i++;
            if (i < n && p[i] == u'^') { d.neg = true; i++; }
            while (i < n && p[i] != u']') { if (p[i] == u'\\' && i + 1 < n) i++; if (d.nset >= 16) return false; d.set[d.nset++] = p[i++]; }
            if (i >= n) return false;
        } else if (ch == u'\\' && i + 1 < n) { d.kind = Lit; d.c = p[++i]; }
        else if (ch == u'*' || ch == u'+' || ch == u'?' || ch == u'|' || ch == u'{' || ch == u'^' || ch == u'$') return false;   /* (not in the subset) */
        else { d.kind = Lit; d.c = ch; }
        if (i + 1 < n && p[i + 1] == u'*') { i++; d.q = Star; if (i + 1 < n && p[i + 1] == u'?') { i++; d.q = LazyStar; } }
        r.n++;
    }
    return sp == 0;
}
bool MatchOne(const Rx& r, const Node& d, int pos) {
    if (pos >= g_inLen) return false;
    char16_t c = g_in[pos];
    switch (d.kind) {
    case Lit: return c == d.c;
    case Any: return r.singleline || c != u'\n';
    case Class: { bool in = false; for (int k = 0; k < d.nset; k++) if (d.set[k] == c) { in = true; break; } return in != d.neg; }
    default: return false;
    }
}
/* the end position of a match of nodes[i..] at pos, or -1 (backtracking; depth bounded by the node count) */
int Match(const Rx& r, int i, int pos) {
    if (i == r.n) return pos;
    const Node& d = r.nodes[i];
    if (d.kind == Open) { int s = g_cap[d.group][0]; g_cap[d.group][0] = pos; int e = Match(r, i + 1, pos); if (e < 0) g_cap[d.group][0] = s; return e; }
    if (d.kind == Close) { int s = g_cap[d.group][1]; g_cap[d.group][1] = pos; int e = Match(r, i + 1, pos); if (e < 0) g_cap[d.group][1] = s; return e; }
    if (d.q == One) return MatchOne(r, d, pos) ? Match(r, i + 1, pos + 1) : -1;
    int maxRun = 0; while (MatchOne(r, d, pos + maxRun)) maxRun++;
    if (d.q == Star) { for (int k = maxRun; k >= 0; k--) { int e = Match(r, i + 1, pos + k); if (e >= 0) return e; } return -1; }
    for (int k = 0; k <= maxRun; k++) { int e = Match(r, i + 1, pos + k); if (e >= 0) return e; }
    return -1;
}

/* ---- the in-memory maps ---- */
struct MemFile { void* path; void* text; };
struct MemFiles { int n; MemFile files[128]; };
MemFiles* g_files;
bool SamePath(void* a, void* b) {
    int n = Len(a); if (n != Len(b)) return false;
    return std::memcmp(Chars(a), Chars(b), sizeof(char16_t) * (size_t)n) == 0;
}
}

void* RegexNew(void* pattern, int32_t options) {
    Rx* r = static_cast<Rx*>(RuntimeAlloc(sizeof(Rx)));
    if (!r) return nullptr;
    std::memset(r, 0, sizeof *r);
    r->magic = kRxMagic; r->singleline = (options & 16) != 0;   /* RegexOptions.Singleline = 16 */
    r->ok = pattern && Parse(*r, Chars(pattern), Len(pattern));
    if (!r->ok) hw::Log("[scenario] ERROR: a Regex pattern outside the supported subset (%d characters): it matches nothing", Len(pattern));
    return r;
}

void* RegexReplace(void* rxp, void* input, void* replacement, int32_t count) {
    Rx* r = static_cast<Rx*>(rxp);
    if (!input) return input;
    if (!r || r->magic != kRxMagic || !r->ok) return input;
    g_inLen = Len(input);
    if (g_inLen > kText) { hw::Log("[scenario] ERROR: Regex.Replace input of %d characters, more than %d: unchanged", g_inLen, kText); return input; }
    std::memcpy(g_in, Chars(input), sizeof(char16_t) * (size_t)g_inLen);
    static char16_t out[kText * 2]; int o = 0;
    int pos = 0, done = 0;
    const char16_t* rep = replacement ? Chars(replacement) : nullptr; int repLen = Len(replacement);
    while (pos <= g_inLen && (count < 0 || done < count)) {
        int start = -1, end = -1;
        for (int s = pos; s <= g_inLen; s++) {
            std::memset(g_cap, -1, sizeof g_cap);
            int e = Match(*r, 0, s);
            if (e >= 0) { start = s; end = e; break; }
        }
        if (start < 0) break;
        for (int k = pos; k < start && o < kText * 2; k++) out[o++] = g_in[k];
        for (int k = 0; k < repLen && o < kText * 2; k++) {
            if (rep[k] == u'$' && k + 1 < repLen && rep[k + 1] >= u'0' && rep[k + 1] <= u'9') {
                int gi = rep[++k] - u'0';
                if (gi == 0) { for (int q = start; q < end && o < kText * 2; q++) out[o++] = g_in[q]; }
                else if (gi <= r->groups && g_cap[gi][0] >= 0 && g_cap[gi][1] >= 0) for (int q = g_cap[gi][0]; q < g_cap[gi][1] && o < kText * 2; q++) out[o++] = g_in[q];
                continue;
            }
            out[o++] = rep[k];
        }
        done++;
        pos = end > start ? end : end + 1;
        if (end == start && start < g_inLen && o < kText * 2) out[o++] = g_in[start];
    }
    for (int k = pos; k < g_inLen && o < kText * 2; k++) out[o++] = g_in[k];
    if (o >= kText * 2) hw::Log("[scenario] ERROR: Regex.Replace output past %d characters: cut", kText * 2);
    return StrFromChars(out, o);
}

void MemFileWrite(void* path, void* text) {
    if (!path) return;
    if (!g_files) { g_files = static_cast<MemFiles*>(RuntimeAlloc(sizeof(MemFiles))); if (g_files) std::memset(g_files, 0, sizeof *g_files); }
    if (!g_files) return;
    for (int i = 0; i < g_files->n; i++) if (SamePath(g_files->files[i].path, path)) { g_files->files[i].text = text; return; }
    if (g_files->n >= 128) { PoolFull("scenario maps (MemFileWrite)", 128); return; }
    g_files->files[g_files->n++] = MemFile{ path, text };
    if (g_files->n == 1 || g_files->n % 16 == 0) hw::Log("[scenario] %d map(s) in memory (of 128); the latest %d characters", g_files->n, Len(text));
}

namespace { char g_scenario[48]; void** g_scenarioPath; bool g_scenarioTried; }
void ConfigureScenario(const char* name) {
    std::snprintf(g_scenario, sizeof g_scenario, "%s", name);
    hw::Log("[scenario] lobby matches play scenario '%s' (TEST ONLY)", g_scenario);
}
void* ScenarioLevelPath() {
    if (!g_scenario[0]) return nullptr;
    if (g_scenarioTried) return g_scenarioPath ? *g_scenarioPath : nullptr;
    g_scenarioTried = true;
    gen::Arr<void*> all = tfex::gen::ScenarioCatalog::All();
    for (int i = 0; i < all.len; i++) {
        void* s = all[i];
        void* nm = s ? tfex::gen::Auto_Scenario::get_Name(s) : nullptr;
        int n = Len(nm); if (n != (int)std::strlen(g_scenario)) continue;
        bool same = true; for (int k = 0; k < n && same; k++) same = Chars(nm)[k] == (char16_t)g_scenario[k];
        if (!same) continue;
        void* path = tfex::gen::ScenarioMapWriter::Write(s);
        RootStatic(g_scenarioPath) = path;
        char p8[160]; int m = Len(path); int k = 0; for (; k < m && k < 159; k++) p8[k] = (char)Chars(path)[k]; p8[k] = 0;
        gen::Arr<void*> vs = tfex::gen::Auto_Scenario::get_Variants(s);
        hw::Log("[scenario] '%s' (%d of %d): its map written to memory as %s; its variants (%d) are the lobby's to set (hw.txt lobbyvariants)", g_scenario, i, all.len, p8, vs.len);
        return path;
    }
    hw::Log("[scenario] ERROR: no scenario named '%s' among %d", g_scenario, all.len);
    return nullptr;
}

void* MemFileXml(void* path) {
    if (!g_files || !path) return nullptr;
    for (int i = 0; i < g_files->n; i++)
        if (SamePath(g_files->files[i].path, path)) {
            void* doc = Fn<void* (*)(void*)>(bind::m_XmlDocument__ctor_0)(nullptr);
            if (doc) Virt<void>(doc, bind::v_XmlDocument_LoadXml_6538, g_files->files[i].text);
            return doc;
        }
    return nullptr;
}
}
