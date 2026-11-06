// Host test of the generated lobby models (tfexgen_Models.*): models_test <in dir> <out dir> <hand dir>
//   <out dir>/<file>: each sample re-serialized by the generated code (compared with TF.EX's oracle by models-test.sh);
//   <hand dir>: unused since the hand-written tfex/Models.* were retired (2026-10-18: the generated ones are the only lobby models).
#define TFEXGEN_TEST 1
#include "tfexgen_Models.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <string>
#include <vector>
#include <algorithm>

static std::string ReadFile(const std::string& p) {
    FILE* f = std::fopen(p.c_str(), "rb");
    if (!f) return {};
    std::string s; char b[4096]; size_t n;
    while ((n = std::fread(b, 1, sizeof b, f)) > 0) s.append(b, n);
    std::fclose(f);
    return s;
}
static void WriteFile(const std::string& p, const char* s, int n) { FILE* f = std::fopen(p.c_str(), "wb"); std::fwrite(s, 1, n, f); std::fclose(f); }

int main(int argc, char** argv) {
    if (argc < 4) { std::fprintf(stderr, "usage: models_test <in> <out> <hand>\n"); return 2; }
    std::vector<std::string> files;
    if (DIR* d = opendir(argv[1])) { while (dirent* e = readdir(d)) if (std::strstr(e->d_name, ".json")) files.push_back(e->d_name); closedir(d); }
    std::sort(files.begin(), files.end());
    static tfex::json::Node nodes[1 << 16];
    static char out[1 << 20];
    int ok = 0, unknown = 0, bad = 0;
    for (auto& f : files) {
        std::string text = ReadFile(std::string(argv[1]) + "/" + f);
        tfex::json::Doc doc{nodes, (int)(sizeof nodes / sizeof nodes[0]), 0};
        if (!doc.Parse(text.c_str(), (int)text.size())) { std::fprintf(stderr, "%s: unparsable\n", f.c_str()); bad++; continue; }
        const tfex::json::Node* root = doc.Root();
        const tfex::json::Node* body = doc.Child(root);
        std::string name(body->key, body->keyLen);
        tfex::json::Writer w; w.Init(out, sizeof out);
        if (!tfex::gen::ws::RoundTrip(name.c_str(), doc, root, w)) { std::fprintf(stderr, "%s: no generated class for %s\n", f.c_str(), name.c_str()); unknown++; continue; }
        if (!w.ok) { std::fprintf(stderr, "%s: output buffer too small\n", f.c_str()); bad++; continue; }
        WriteFile(std::string(argv[2]) + "/" + f, out, w.len);
        ok++;
    }
    std::printf("models_test: %d round-tripped, %d without a class, %d failed; sizeof Lobby %zu, Player %zu\n", ok, unknown,
                bad, sizeof(tfex::gen::ws::Lobby), sizeof(tfex::gen::ws::Player));
    return bad ? 1 : 0;
}
