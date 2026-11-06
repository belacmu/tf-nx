// test-levelgen: the C++ ports of the PC's LevelRandomGeometry.GenerateData (levelgeom_core.hpp) and LevelRandomItems.AddItems
// (levelitems_core.hpp) on the host, from what `tools/re/pcgen.py <level> <seed> --input [--wide]` prints (seed, RandomBlock rects,
// the level's solids); prints what `pcgen.py --items` prints (draw counts, entities, final grid). tools/test-levelgen.sh compares both
// over every Cataclysm level, the PC's standard set and WiderSet's wide set (docs/ws-wide.md).
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "../hw-probe/exlaunch/source/program/dnrandom.hpp"
#include "../hw-probe/exlaunch/source/program/levelgeom_core.hpp"

struct HostEnv {
    dnrandom::State st;
    int draws = 0;
    struct El { std::string line; int parent; };
    std::vector<El> els;
    std::vector<std::string> mps;
    std::vector<int> order;
    int Next(int max) { draws++; return dnrandom::Next(&st, max); }
    double Sample() { draws++; return dnrandom::Sample(&st); }
    double Pow(double a, double b) { return std::pow(a, b); }
    int El(const char* name) { els.push_back({ std::string(name), -1 }); return (int)els.size() - 1; }
    void Attr(int h, const char* n, int v) { els[h].line += " " + std::string(n) + "=" + std::to_string(v); }
    void AttrS(int h, const char* n, const char* v) { els[h].line += " " + std::string(n) + "=" + v; }
    void Append(int h) { order.push_back(h); }
    void AppendTo(int parent, int child) { els[child].parent = parent; order.push_back(child); }
    void MovingPlatform(int x, int y, int w, int h) { mps.push_back("mp " + std::to_string(x) + " " + std::to_string(y) + " " + std::to_string(w) + " " + std::to_string(h)); }
};

static std::string Hex(const levelitems::Grid& g, int cols) {
    std::vector<int> flat(cols * 24);
    for (int x = 0; x < cols; x++) for (int y = 0; y < 24; y++) flat[x * 24 + y] = g.s[x][y];
    std::string s;
    for (int i = 0; i < cols * 24; i += 4) s += "0123456789abcdef"[flat[i] * 8 + flat[i + 1] * 4 + flat[i + 2] * 2 + flat[i + 3]];
    return s;
}

int main() {
    HostEnv e{};
    levelitems::Grid base{};
    std::vector<levelitems::Rect> rects;
    bool wide = false; int seed = 0;
    char word[16];
    std::vector<std::string> baseHex;
    while (std::scanf("%15s", word) == 1) {
        if (!std::strcmp(word, "seed")) std::scanf("%d", &seed);
        else if (!std::strcmp(word, "wide")) { int w; std::scanf("%d", &w); wide = w != 0; }
        else if (!std::strcmp(word, "rect")) { levelitems::Rect r; std::scanf("%d %d %d %d", &r.X, &r.Y, &r.W, &r.H); rects.push_back(r); }
        else if (!std::strcmp(word, "base")) { char h[300]; std::scanf("%299s", h); baseHex.push_back(h); }
    }
    const int cols = wide ? 42 : 32;
    if (baseHex.empty()) return 1;
    const char* h = baseHex[0].c_str();
    for (int i = 0; i < cols * 6; i++) {
        int v = (h[i] >= 'a') ? h[i] - 'a' + 10 : h[i] - '0';
        for (int b = 0; b < 4; b++) { int k = i * 4 + b; base.s[k / 24][k % 24] = (v >> (3 - b)) & 1; }
    }
    dnrandom::Init(&e.st, seed);
    levelitems::Grid g{};
    levelgeom::GenerateData(e, rects.data(), (int)rects.size(), base, g, wide);
    int ngen = e.draws;
    levelitems::Grid b2 = g;
    levelitems::AddItems(e, g, b2, false, wide);
    std::printf("generate draws %d additems draws %d\n", ngen, e.draws - ngen);
    for (int k : e.order) std::printf("%s %s\n", e.els[k].parent >= 0 ? "child" : "el", e.els[k].line.c_str());
    for (auto& m : e.mps) std::printf("%s\n", m.c_str());
    std::printf("final %s\n", Hex(g, cols).c_str());
}
