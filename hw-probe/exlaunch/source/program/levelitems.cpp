/* The PC's LevelRandomItems.AddItems in the game (levelitems_core.hpp is the port; tools/test-levelitems.sh checks it against the PC
 * reference, which reproduces the browser's grids). Replaces the Switch's AddItems (0xa8fbc0, 1.4.1.6: other candidate scans and an
 * extra ChalicePad pass, so Cataclysm's jump pads, crumble walls and so on came out elsewhere) during versus/netplay; xplay.cpp hooks it.
 *
 * The game's own pieces, as the Switch's AddItems uses them:
 *   draws: Calc.Random through System.Random.Next(int) / Sample (rnd.cpp's .NET-compatible hooks), Math.Pow +0xae20;
 *   XML: entities.OwnerDocument (virtual +0xc8), XmlDocument.CreateElement(string) +0x561470, Calc.SetAttr(XmlElement, string, object)
 *        +0x90e500 (stores value.ToString()), Int32.ToString(int*) +0x21b160, XmlNode.AppendChild (virtual +0x120);
 *   the object: BaseSolids (+0x10) = solids.Clone() (+0x6990), MovingPlatforms (+0x18) = new List<Rectangle> (+0xa83c30, Add +0xa83d00),
 *   which LevelLoaderXML hands to LevelRandomTreasure.AddRandomTreasure next (+0xa835b0). bool[,]: data at +0x10, [x, y] = x * 24 + y. */
#include <cstdint>
#include <cstring>
#include "tfex/Game.hpp"
#include "levelitems_core.hpp"
#include "levelgeom_core.hpp"
#include "levelbg_core.hpp"
#include "rnd.hpp"
#include "hwlog.hpp"

namespace pcgameplay { bool Wide(); }

namespace levelitems {
using tfex::game::Base; using tfex::game::Fn; using tfex::game::Virt; using tfex::game::ArrayData; using tfex::game::MString;

namespace {
constexpr uintptr_t kRandomNextMax = 0x272040, kRandomSample = 0x272580, kMathPow = 0xae20;
constexpr uintptr_t kCreateElement = 0x561470, kSetAttr = 0x90e500, kIntToString = 0x21b160;
constexpr uintptr_t kArrayClone = 0x6990, kListRectNew = 0xa83c30, kListRectAdd = 0xa83d00;
constexpr uint32_t kOwnerDocument = 0xc8, kAppendChild = 0x120;

void* IntStr(int v) { int32_t t = v; return Fn<void* (*)(int32_t*)>(kIntToString)(&t); }

/* The literals as managed strings outside the GC heap (tfex::game::MString), made once from a live string's class word. */
MString g_str[24]; const char* g_strText[24]; int g_nStr = 0;
void* Str(const char* s) {
    for (int i = 0; i < g_nStr; i++) if (g_strText[i] == s || !std::strcmp(g_strText[i], s)) return &g_str[i];
    if (g_nStr >= 24) { hw::Log("[items] too many literals"); return nullptr; }
    tfex::game::MakeString(&g_str[g_nStr], IntStr(0), s);
    g_strText[g_nStr] = s;
    return &g_str[g_nStr++];
}

struct GameEnv {
    void* rng; void* entities; void* doc; void* platforms;
    int Next(int max) { return Fn<int32_t (*)(void*, int32_t)>(kRandomNextMax)(rng, max); }
    double Sample() { return Fn<double (*)(void*)>(kRandomSample)(rng); }
    double Pow(double a, double b) { return Fn<double (*)(double, double)>(kMathPow)(a, b); }
    void* El(const char* name) { return Fn<void* (*)(void*, void*)>(kCreateElement)(doc, Str(name)); }
    void Attr(void* el, const char* n, int v) { Fn<void (*)(void*, void*, void*)>(kSetAttr)(el, Str(n), IntStr(v)); }
    void AttrS(void* el, const char* n, const char* v) { Fn<void (*)(void*, void*, void*)>(kSetAttr)(el, Str(n), Str(v)); }
    void Append(void* el) { Virt<void*>(entities, kAppendChild, el); }
    void AppendTo(void* parent, void* child) { Virt<void*>(parent, kAppendChild, child); }
    void MovingPlatform(int x, int y, int w, int h) {
        uint64_t lo = (uint32_t)x | ((uint64_t)(uint32_t)y << 32), hi = (uint32_t)w | ((uint64_t)(uint32_t)h << 32);
        Fn<void (*)(void*, uint64_t, uint64_t)>(kListRectAdd)(platforms, lo, hi);
    }
};
}

void RunPc(void* self, void* solids, void* entities, bool teams) {
    uintptr_t o = reinterpret_cast<uintptr_t>(self);
    uint8_t* d = ArrayData<uint8_t>(solids);
    const bool wide = pcgameplay::Wide();   /* a wide lobby match: 42x24 solids, WiderSet's AddItems constants (levelitems_core.hpp) */
    const int w = wide ? 42 : 32;
    Grid g{};
    for (int x = 0; x < w; x++) for (int y = 0; y < 24; y++) g.s[x][y] = d[x * 24 + y] != 0;
    *reinterpret_cast<void**>(o + 0x10) = Fn<void* (*)(void*)>(kArrayClone)(solids);       /* BaseSolids = (bool[,])solids.Clone() */
    void* platforms = Fn<void* (*)(void*)>(kListRectNew)(nullptr);
    GameEnv e{ rnd::CalcRandomObject(), entities, Virt<void*>(entities, kOwnerDocument), platforms };
    const Grid base = g;
    AddItems(e, g, base, teams, wide);
    *reinterpret_cast<void**>(o + 0x18) = platforms;                                       /* MovingPlatforms */
    for (int x = 0; x < w; x++) for (int y = 0; y < 24; y++) d[x * 24 + y] = g.s[x][y];
}

/* LevelRandomGeometry.GenerateData(List<Rectangle> rects, bool[,] baseData) -> bool[,] in a wide match: the PC's (levelgeom_core.hpp)
 * with WiderSet's constants (the Switch's own derives its columns from baseData; tools/test-levelgen.sh checks the port against
 * tools/re/pcgen.py --wide). The result is a clone of baseData (+0x6990) overwritten with the generated grid. nullptr if baseData isn't
 * 42 columns (Array.GetLength(0) +0x6280): the caller then keeps the Switch's. List<Rectangle>: items +0x10, size +0x18. */
void* RunPcGeometry(void* rects, void* baseData) {
    if (!baseData || Fn<int32_t (*)(void*, int32_t)>(0x6280)(baseData, 0) != 42) return nullptr;
    const uint8_t* bd = ArrayData<uint8_t>(baseData);
    Grid base{};
    for (int x = 0; x < 42; x++) for (int y = 0; y < 24; y++) base.s[x][y] = bd[x * 24 + y] != 0;
    int n = rects ? *reinterpret_cast<int32_t*>(reinterpret_cast<uintptr_t>(rects) + 0x18) : 0;
    const int32_t* ri = rects ? ArrayData<int32_t>(*reinterpret_cast<void**>(reinterpret_cast<uintptr_t>(rects) + 0x10)) : nullptr;
    Rect rs[64]; int k = 0;
    for (int i = 0; ri && i < n && k < 64; i++) rs[k++] = { ri[i * 4], ri[i * 4 + 1], ri[i * 4 + 2], ri[i * 4 + 3] };
    GameEnv e{ rnd::CalcRandomObject(), nullptr, nullptr, nullptr };
    Grid out{};
    levelgeom::GenerateData(e, rs, k, base, out, true);
    void* arr = Fn<void* (*)(void*)>(kArrayClone)(baseData);
    uint8_t* d = ArrayData<uint8_t>(arr);
    for (int x = 0; x < 42; x++) for (int y = 0; y < 24; y++) d[x * 24 + y] = out.s[x][y];
    hw::Log("[items] wide GenerateData: %d rects (PC port, WiderSet's constants)", k);
    return arr;
}

/* LevelRandomBGTiles.GenerateBitData(bool[,] solids) -> bool[,] in a wide match: the PC's with WiderSet's constants (levelbg_core.hpp;
 * the Switch's own placed Cataclysm's background differently in wide). The result is a clone of solids (+0x6990) overwritten.
 * nullptr if solids isn't 42 columns: the caller keeps the Switch's. */
void* RunPcBGBitData(void* solids) {
    if (!solids || Fn<int32_t (*)(void*, int32_t)>(0x6280)(solids, 0) != 42) return nullptr;
    const uint8_t* sd = ArrayData<uint8_t>(solids);
    Grid s{};
    for (int x = 0; x < 42; x++) for (int y = 0; y < 24; y++) s.s[x][y] = sd[x * 24 + y] != 0;
    GameEnv e{ rnd::CalcRandomObject(), nullptr, nullptr, nullptr };
    Grid out{};
    levelbg::GenerateBitData(e, s, out, true);
    void* arr = Fn<void* (*)(void*)>(kArrayClone)(solids);
    uint8_t* d = ArrayData<uint8_t>(arr);
    for (int x = 0; x < 42; x++) for (int y = 0; y < 24; y++) d[x * 24 + y] = out.s[x][y];
    return arr;
}

/* LevelRandomBGDetails.GenerateTileData(baseSolids, bg, finalSolids, entities) -> int[,] in a wide match: the PC's with WiderSet's
 * constants (levelbg_core.hpp), its BGLantern elements appended to entities. The result is Calc.ReadCSVIntGrid("", 42, 24) (+0x90ad10:
 * a new int[42, 24] of -1) filled in. nullptr if the grids aren't 42 columns: the caller keeps the Switch's. */
void* RunPcBGTileData(void* baseSolids, void* bg, void* finalSolids, void* entities) {
    void* grids[3] = { baseSolids, bg, finalSolids };
    for (void* g : grids) if (!g || Fn<int32_t (*)(void*, int32_t)>(0x6280)(g, 0) != 42) return nullptr;
    auto grid = [](void* arr) { const uint8_t* d = ArrayData<uint8_t>(arr); Grid g{}; for (int x = 0; x < 42; x++) for (int y = 0; y < 24; y++) g.s[x][y] = d[x * 24 + y] != 0; return g; };
    const Grid base = grid(baseSolids), back = grid(bg), fin = grid(finalSolids);
    GameEnv e{ rnd::CalcRandomObject(), entities, Virt<void*>(entities, kOwnerDocument), nullptr };
    static levelbg::Tiles out;   /* (16 KB: off the stack) */
    levelbg::GenerateTileData(e, base, back, fin, out, true);
    void* arr = Fn<void* (*)(void*, int32_t, int32_t)>(0x90ad10)(Str(""), 42, 24);   /* Calc.ReadCSVIntGrid */
    int32_t* d = ArrayData<int32_t>(arr);
    for (int x = 0; x < 42; x++) for (int y = 0; y < 24; y++) d[x * 24 + y] = out.t[x][y];
    static int logged = 0;
    if (logged < 4) { logged++; hw::Log("[items] wide BG details: the PC's GenerateTileData (WiderSet's constants)"); }
    return arr;
}

} // namespace levelitems
