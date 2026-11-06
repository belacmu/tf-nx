// test-levelitems: the C++ port of the PC's LevelRandomItems.AddItems (hw-probe/.../levelitems_core.hpp) on the host, fed the
// .NET Random state and grid that tools/re/pcgen.py has after GenerateData; prints the same lines as `pcgen.py --items`
// (draws, el/child/mp lines, final grid). tools/test-levelitems.sh builds it and compares both over all Cataclysm levels and seeds.
// Input (stdin): "state <inext> <inextp> <56 ints>" and "grid <192 hex digits>" lines; "teams 1" optional.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "../hw-probe/exlaunch/source/program/dnrandom.hpp"
#include "../hw-probe/exlaunch/source/program/levelitems_core.hpp"

struct HostEnv {
    dnrandom::State st;
    int draws = 0;
    struct El { std::string line; int parent; };
    std::vector<El> els;
    std::vector<std::string> mps;
    int Next(int max) { draws++; return dnrandom::Next(&st, max); }
    double Sample() { draws++; return dnrandom::Sample(&st); }
    double Pow(double a, double b) { return std::pow(a, b); }
    int El(const char* name) { els.push_back({ std::string(name), -1 }); return (int)els.size() - 1; }
    void Attr(int h, const char* n, int v) { els[h].line += " " + std::string(n) + "=" + std::to_string(v); }
    void AttrS(int h, const char* n, const char* v) { els[h].line += " " + std::string(n) + "=" + v; }
    std::vector<int> order;   // appended elements in order (a child right after its parent, as pcgen prints them)
    void Append(int h) { order.push_back(h); }
    void AppendTo(int parent, int child) { els[child].parent = parent; order.push_back(child); }
    void MovingPlatform(int x, int y, int w, int h) { mps.push_back("mp " + std::to_string(x) + " " + std::to_string(y) + " " + std::to_string(w) + " " + std::to_string(h)); }
};

static std::string Hex(const levelitems::Grid& g) {
    std::string s;
    bool flat[768];
    for (int x = 0; x < 32; x++) for (int y = 0; y < 24; y++) flat[x * 24 + y] = g.s[x][y];
    for (int i = 0; i < 768; i += 4) s += "0123456789abcdef"[flat[i] * 8 + flat[i + 1] * 4 + flat[i + 2] * 2 + flat[i + 3]];
    return s;
}

int main() {
    HostEnv e{};
    levelitems::Grid g{};
    bool teams = false;
    char word[16];
    while (std::scanf("%15s", word) == 1) {
        if (!std::strcmp(word, "state")) {
            std::scanf("%d %d", &e.st.inext, &e.st.inextp);
            for (int i = 0; i < 56; i++) std::scanf("%d", &e.st.seedArray[i]);
        } else if (!std::strcmp(word, "grid")) {
            char h[200]; std::scanf("%199s", h);
            for (int i = 0; i < 192; i++) {
                int v = (h[i] >= 'a') ? h[i] - 'a' + 10 : h[i] - '0';
                for (int b = 0; b < 4; b++) { int k = i * 4 + b; g.s[k / 24][k % 24] = (v >> (3 - b)) & 1; }
            }
        } else if (!std::strcmp(word, "teams")) { int t; std::scanf("%d", &t); teams = t != 0; }
    }
    levelitems::Grid base = g;
    levelitems::AddItems(e, g, base, teams);
    std::printf("additems draws %d\n", e.draws);
    for (int h : e.order) std::printf("%s %s\n", e.els[h].parent >= 0 ? "child" : "el", e.els[h].line.c_str());
    for (auto& m : e.mps) std::printf("%s\n", m.c_str());
    std::printf("final %s\n", Hex(g).c_str());
}
