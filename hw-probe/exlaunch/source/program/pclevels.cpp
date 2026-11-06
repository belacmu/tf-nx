/* The PC's versus levels from the Switch's own (docs/PLAN-TFNX-2026-11-04.md section 4): online play needs the PC's levels (TF.EX plays
 * them; all 192 differ from the Switch's 1.4.1.6 ones), which a release can't contain. pclevels.gen.inc (tools/levels/delta.py) holds, per
 * level, the lines the PC's file changes; here the Switch's own file (the game's romfs, rom:/Content/Levels/Versus/<tower>/<n>.oel) gets
 * them and becomes an XmlDocument, as the game's Calc.LoadXML would have made from the PC's file (pcgameplay.cpp's LoadXmlHook asks).
 * A Switch file that isn't the one the delta was made from (its CRC-32) is refused: logged, and the caller falls back to the romfs's
 * Content/Levels/PCVersus file if a private build has one. Fixed buffers, sized by the generator's maxima; too big is a pools error. */
#include "hwlog.hpp"
#include "pools.hpp"
#include "nn/fs.hpp"
#include "tfex/runtime/tfexgen_code_rt.hpp"
#include "tfex/gen/tfexgen_binding.hpp"
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace pclevels {
namespace {
struct PcLevelDelta { const char* path; uint32_t crc; int lines; int pcBytes; const char* script; };
#include "pclevels.gen.inc"

constexpr int kFileMax = 32 * 1024, kLinesMax = 1024;
char g_in[kFileMax], g_out[kFileMax];
int g_lineAt[kLinesMax + 1];
int g_served = 0, g_refused = 0;
static_assert(kPcLevelMaxLines < kLinesMax && kPcLevelMaxBytes < kFileMax, "pclevels.gen.inc's levels outgrew the buffers");

uint32_t Crc32(const char* p, int n) {
    uint32_t c = 0xffffffffu;
    for (int i = 0; i < n; i++) {
        c ^= (uint8_t)p[i];
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xedb88320u & (0u - (c & 1)));
    }
    return ~c;
}
const PcLevelDelta* Find(const char* rel) {
    for (int i = 0; i < kPcLevelCount; i++) if (std::strcmp(kPcLevels[i].path, rel) == 0) return &kPcLevels[i];
    return nullptr;
}
/* the Switch's file, normalised as the generator did (no byte-order mark, '\n' line ends): its length, -1 if it can't be read */
int ReadSwitch(const char* rel) {
    char path[192]; std::snprintf(path, sizeof path, "rom:/Content/Levels/Versus/%s", rel);
    nn::fs::FileHandle h;
    if (nn::fs::OpenFile(&h, path, nn::fs::OpenMode_Read) != 0) return -1;
    long sz = 0; nn::fs::GetFileSize(&sz, h);
    if (sz <= 0 || sz >= kFileMax) { nn::fs::CloseFile(h); pools::Exhausted("PC level buffer", kFileMax, path); return -1; }
    bool ok = nn::fs::ReadFile(h, 0, g_in, (size_t)sz) == 0;
    nn::fs::CloseFile(h);
    if (!ok) return -1;
    int n = 0;
    for (long i = (sz >= 3 && (uint8_t)g_in[0] == 0xef && (uint8_t)g_in[1] == 0xbb && (uint8_t)g_in[2] == 0xbf) ? 3 : 0; i < sz; i++) {
        if (g_in[i] == '\r') { if (i + 1 < sz && g_in[i + 1] == '\n') continue; g_in[n++] = '\n'; continue; }
        g_in[n++] = g_in[i];
    }
    return n;
}
}  // namespace

/* The PC level <tower>/<n>.oel (its path under Content/Levels/Versus) as an XmlDocument made from the Switch's own file and its delta, or
 * null (no delta for it, the Switch file missing or not the expected one: logged). */
void* Doc(const char* rel) {
    const PcLevelDelta* d = Find(rel);
    if (!d) return nullptr;
    int n = ReadSwitch(rel);
    if (n < 0) { if (g_refused++ < 5) hw::Log("[pclevels] ERROR: rom:/Content/Levels/Versus/%s unreadable: no PC level from it", rel); return nullptr; }
    uint32_t crc = Crc32(g_in, n);
    if (crc != d->crc) {
        if (g_refused++ < 5) hw::Log("[pclevels] ERROR: %s is not the Switch level the delta was made from (CRC %08x, expected %08x): no PC level", rel, crc, d->crc);
        return nullptr;
    }
    int lines = 0; g_lineAt[0] = 0;
    for (int i = 0; i < n && lines < kLinesMax; i++) if (g_in[i] == '\n') g_lineAt[++lines] = i + 1;
    g_lineAt[++lines] = n + 1;   /* (the text after the last '\n' is a line too) */
    if (lines != d->lines) { hw::Log("[pclevels] ERROR: %s: %d lines, the delta expects %d", rel, lines, d->lines); return nullptr; }
    int o = 0; bool first = true;
    auto put = [&](const char* s, int len) {
        if (!first) { if (o < kFileMax) g_out[o] = '\n'; o++; }
        first = false;
        for (int i = 0; i < len; i++) { if (o < kFileMax) g_out[o] = s[i]; o++; }
    };
    for (const char* s = d->script; *s;) {
        const char* e = std::strchr(s, '\n'); if (!e) e = s + std::strlen(s);
        if (*s == '=') {
            int a = 0, b = 0; std::sscanf(s + 1, "%d,%d", &a, &b);
            for (int l = a; l < b && l < lines; l++) put(g_in + g_lineAt[l], g_lineAt[l + 1] - 1 - g_lineAt[l]);
        } else put(s + 1, (int)(e - s - 1));
        s = *e ? e + 1 : e;
    }
    if (o >= kFileMax) { pools::Exhausted("PC level buffer", kFileMax, rel); return nullptr; }
    if (o != d->pcBytes) { hw::Log("[pclevels] ERROR: %s rebuilt to %d bytes, the PC level has %d: not used", rel, o, d->pcBytes); return nullptr; }
    void* doc = tfex::gen::Fn<void* (*)(void*)>(tfex::gen::bind::m_XmlDocument__ctor_0)(nullptr);
    if (doc) tfex::gen::Virt<void>(doc, tfex::gen::bind::v_XmlDocument_LoadXml_6538, tfex::gen::ManagedUtf8(g_out, o));
    if (g_served++ < 5 || g_served % 50 == 0)
        hw::Log("[pclevels] %s: the PC level from the Switch's own and its delta (%d bytes; %d served)", rel, o, g_served);
    return doc;
}
}  // namespace pclevels
