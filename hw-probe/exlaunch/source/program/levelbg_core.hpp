/* The PC's LevelRandomBGTiles.GenerateBitData and LevelRandomBGDetails.GenerateTileData (TowerFall 1.3.3.3, the build TF.EX runs),
 * transcribed line for line, with WiderSet's wide constants. Cataclysm's procedural background: the BG bit grid, then the BG tile
 * overrides (vines, cracks, lit tiles) and the BGLantern entities. Gameplay reads them: lanterns are BGTorches (they set arrows on
 * fire; the 2026-10-27 wide Cataclysm desync was a lantern the Switch's own GenerateTileData placed elsewhere), lit tiles add
 * AddTileIDLights light sources (depth-0 entities in the saved state).
 *
 * WiderSet (0.9.0) transpiles these in wide, by IL occurrence (FortRise names the vanilla GenerateTileData `GenCataclysm`):
 *   GenerateBitData: every ldc.i4 16 -> 21, 32 -> 42, 31 -> 41; Set: the first 31 -> 41.
 *   GenCataclysm: ldc.i4 16 -> 21 except occurrences 13..15 (in IL order: the tile 16 at the third and fourth rectangle in the
 *     horizontal-vine pass, and the `l <= 16` bound of the bush pass); every 32 -> 42 (its counter meant to skip two never advances,
 *     so the tile 32 of the vertical vines becomes 42 too); 31 -> 41 except the first two (the tile 31 of the 2x2 block).
 *   Check: the first 32 -> 42; Empty: the first 31 -> 41.
 * W16 / W32 / W31 below are those widened constants; T16 / T32 / T31 mark the IL occurrences WiderSet leaves alone or widens as tiles.
 *
 * Header-only and environment-agnostic, as levelitems_core.hpp; Env provides Next / Sample / Pow / El / Attr / Append. */
#pragma once
#include <initializer_list>
#include "levelitems_core.hpp"

namespace levelbg {
using levelitems::Rect; using levelitems::Pt; using levelitems::Vec; using levelitems::Grid; using levelitems::Shuffle;
using levelitems::ChanceD; using levelitems::ChanceF;

struct Tiles { int t[42][24]; };

template <class Env> inline int Choose(Env& e, std::initializer_list<int> v) { return *(v.begin() + e.Next((int)v.size())); }

/* ---- LevelRandomBGTiles.GenerateBitData(bool[,] solids) ---- */
template <class Env> void GenerateBitData(Env& e, const Grid& solids, Grid& data, bool wide) {
    const int W16 = wide ? 21 : 16, W32 = wide ? 42 : 32, W31 = wide ? 41 : 31, SetW31 = wide ? 41 : 31;
    data = Grid{};
    auto Set = [&](int x, int y) {
        data.s[x][y] = true;
        if (x > 0 && solids.s[x - 1][y]) data.s[x - 1][y] = true;
        if (x < SetW31 && solids.s[x + 1][y]) data.s[x + 1][y] = true;
        if (y > 0 && solids.s[x][y - 1]) data.s[x][y - 1] = true;
        if (y < 23 && solids.s[x][y + 1]) data.s[x][y + 1] = true;
    };
    auto Check = [&](int x, int y) { return data.s[x][y] || solids.s[x][y]; };
    for (int i = 0; i < W32; i++) {
        bool flag; int num;
        if (!solids.s[i][0]) {
            flag = i > 0 && i < W31 && solids.s[i - 1][0] && solids.s[i + 1][0];
            num = Choose(e, {1, 2, 2, 2, 2, 2, 2, 3, 3, 3});
            for (int j = 0; j < num; j++) { Set(i, j); if (flag) { Set(i - 1, j); Set(i + 1, j); } }
        }
        if (solids.s[i][23]) continue;
        flag = i > 0 && i < W31 && solids.s[i - 1][23] && solids.s[i + 1][23];
        num = Choose(e, {1, 2, 2, 2, 2, 2, 2, 3, 3, 3});
        for (int j = 0; j < num; j++) { Set(i, 23 - j); if (flag) { Set(i - 1, 23 - j); Set(i + 1, 23 - j); } }
    }
    for (int k = 0; k < 24; k++) if (!solids.s[0][k]) { Set(0, k); Set(1, k); }
    switch (Choose(e, {0, 1})) {
    case 0: {
        int num2 = Choose(e, {8, 9, 9, 10});
        int num = Choose(e, {4, 5, 5, 6});
        int num4 = 0;
        for (int i = 0; i < W16; i++) {
            if (num4 >= 4 || ChanceF(e, 0.6f)) { num4 = 0; num = num <= 2 ? num + 1 : num < num2 ? num + Choose(e, {-2, -1, -1, 1, 1, 2}) : num - 1; }
            else num4++;
            for (int k = 0; k < num; k++) data.s[i][k] = true;
        }
        num = Choose(e, {4, 5, 5, 6});
        num4 = 0;
        for (int i = 0; i < W16; i++) {
            if (num4 >= 4 || ChanceF(e, 0.6f)) { num4 = 0; num = num <= 2 ? num + 1 : num < num2 ? num + Choose(e, {-2, -1, -1, 1, 1, 2}) : num - 1; }
            else num4++;
            for (int k = 0; k < num; k++) data.s[i][23 - k] = true;
        }
        break;
    }
    case 1: {
        int num2 = Choose(e, {8, 9, 10, 10, 11});
        int num3 = Choose(e, {5, 6, 6, 7});
        int num4 = 0;
        for (int k = 0; k < 24; k++) {
            if (num4 >= 4 || ChanceF(e, 0.6f)) { num4 = 0; num3 = num3 <= 2 ? num3 + 1 : num3 < num2 ? num3 + Choose(e, {-2, -1, -1, 1, 1, 2}) : num3 - 1; }
            else num4++;
            for (int i = 0; i < num3; i++) data.s[i][k] = true;
        }
        num3 = Choose(e, {5, 6, 6, 7});
        num4 = 0;
        for (int k = 0; k < 24; k++) {
            if (num4 >= 4 || ChanceF(e, 0.6f)) { num4 = 0; num3 = num3 <= 2 ? num3 + 1 : num3 < num2 ? num3 + Choose(e, {-2, -1, -1, 1, 1, 2}) : num3 - 1; }
            else num4++;
            for (int i = 0; i < num3; i++) data.s[i][k] = true;
        }
        break;
    }
    }
    for (int j = 0; j < 2; j++)
        for (int i = 1; i < W31; i++)
            for (int k = 1; k < 23; k++) {
                if (data.s[i][k] && !solids.s[i][k]) {
                    if (!data.s[i - 1][k] && !data.s[i + 1][k]) {
                        if (!Check(i - 1, k + 1) && !Check(i - 1, k - 1)) { Set(i + 1, k); continue; }
                        if (!Check(i + 1, k + 1) && !Check(i + 1, k + 1)) { Set(i - 1, k); continue; }
                        Set(i + Choose(e, {1, -1}), k);
                    } else if (!data.s[i][k - 1] && !data.s[i][k + 1]) {
                        if (!Check(i + 1, k - 1) && !Check(i - 1, k - 1)) { Set(i, k + 1); continue; }
                        if (!Check(i + 1, k + 1) && !Check(i - 1, k + 1)) { Set(i, k - 1); continue; }
                        Set(i, k + Choose(e, {1, -1}));
                    }
                } else if (!data.s[i][k] && !solids.s[i][k] && Check(i - 1, k) && Check(i + 1, k) && Check(i, k - 1) && Check(i, k + 1)) {
                    Set(i, k);
                }
            }
    for (int i = 0; i < W16; i++) for (int k = 0; k < 24; k++) data.s[W31 - i][k] = data.s[i][k];
}

/* ---- LevelRandomBGDetails.GenerateTileData(bool[,] baseSolids, bool[,] bg, bool[,] finalSolids, XmlElement entities) ---- */
template <class Env> void GenerateTileData(Env& e, const Grid& baseSolids, const Grid& bg, const Grid& finalSolids, Tiles& out, bool wide) {
    const int W16 = wide ? 21 : 16, W32 = wide ? 42 : 32, W31 = wide ? 41 : 31;
    const int CheckW = wide ? 42 : 32, EmptyW = wide ? 41 : 31;
    int (&a)[42][24] = out.t;
    auto Check = [&](const Grid& d, int x, int y, int width, int height, bool match) {
        for (int i = x > 0 ? x : 0; i < (CheckW < x + width ? CheckW : x + width); i++)
            for (int j = y > 0 ? y : 0; j < (24 < y + height ? 24 : y + height); j++)
                if (d.s[i][j] != match) return false;
        return true;
    };
    auto Empty = [&](int x, int y, int width, int height) {
        for (int i = x > 0 ? x : 0; i < (EmptyW < x + width ? EmptyW : x + width); i++)
            for (int j = y > 0 ? y : 0; j < (23 < y + height ? 23 : y + height); j++)
                if (a[i][j] != -1) return false;
        return true;
    };
    auto RollK = [&](int k0, double mul, double base) { int k; for (k = k0; ChanceD(e, mul * e.Pow(base, k)); k++) {} return k; };
    for (int i = 0; i < W32; i++) for (int j = 0; j < 24; j++) a[i][j] = -1;
    int k;

    /* 2x2 blocks (8/9/28/29 or 10/11/30/31), mirrored */
    k = RollK(0, 0.8999999761581421, 0.8999999761581421);
    if (k > 0) {
        Vec<Pt, 1024> list;
        for (int l = 0; l < W16; l++) for (int m = 0; m < 23; m++)
            if (Check(baseSolids, l, m, 2, 2, false) && Check(bg, l - 1, m - 1, 4, 4, true)) list.Add({l, m});
        Shuffle(e, list);
        while (k > 0 && list.n > 0) {
            Pt p = list[0]; list.RemoveAt(0); k--;
            if (ChanceF(e, 0.5f)) {
                a[p.X][p.Y] = 8; a[p.X + 1][p.Y] = 9; a[p.X][p.Y + 1] = 28; a[p.X + 1][p.Y + 1] = 29;
                if (p.X < 15) { int l = W32 - p.X - 2; a[l][p.Y] = 8; a[l + 1][p.Y] = 9; a[l][p.Y + 1] = 28; a[l + 1][p.Y + 1] = 29; }
            } else {
                a[p.X][p.Y] = 10; a[p.X + 1][p.Y] = 11; a[p.X][p.Y + 1] = 30; a[p.X + 1][p.Y + 1] = 31;   /* T31: WiderSet's skipped two */
                if (p.X < 15) { int l = W32 - p.X - 2; a[l][p.Y] = 10; a[l + 1][p.Y] = 11; a[l][p.Y + 1] = 30; a[l + 1][p.Y + 1] = 31; }
            }
            for (int i = 0; i < list.n; i++) {
                Pt q = list[i];
                if (q.X <= p.X + 1 && q.X >= p.X - 1 && q.Y <= p.Y + 1 && q.Y >= p.Y - 1) { list.RemoveAt(i); i--; }
            }
        }
    }
    /* 49/50 pairs on ledges */
    k = RollK(0, 0.8999999761581421, 0.800000011920929);
    if (k > 0) {
        Vec<Pt, 1024> list;
        for (int l = 0; l < W16; l++) for (int m = 0; m < 23; m++)
            if (Check(baseSolids, l, m, 2, 1, false) && Check(baseSolids, l, m + 1, 2, 1, true) && Check(bg, l - 1, m - 1, 4, 3, true) && Empty(l, m, 2, 1))
                list.Add({l, m});
        Shuffle(e, list);
        while (k > 0 && list.n > 0) {
            Pt p = list[0]; list.RemoveAt(0); k--;
            a[p.X][p.Y] = 49; a[p.X + 1][p.Y] = 50;
            if (p.X < 15) { int l = W32 - p.X - 2; a[l][p.Y] = 49; a[l + 1][p.Y] = 50; }
            for (int i = 0; i < list.n; i++) { Pt q = list[i]; if (q.X <= p.X + 1 && q.X >= p.X - 1 && q.Y == p.Y) { list.RemoveAt(i); i--; } }
        }
    }
    /* 13/33 columns */
    k = RollK(0, 0.8999999761581421, 0.800000011920929);
    if (k > 0) {
        Vec<Pt, 1024> list;
        for (int l = 0; l < W16; l++) for (int m = 0; m < 22; m++)
            if (Check(baseSolids, l, m, 1, 2, false) && baseSolids.s[l][m + 2] && Check(bg, l - 1, m - 1, 3, 4, true) && Empty(l, m, 1, 2)) list.Add({l, m});
        Shuffle(e, list);
        while (k > 0 && list.n > 0) {
            Pt p = list[0]; list.RemoveAt(0); k--;
            a[p.X][p.Y] = 13; a[p.X][p.Y + 1] = 33;
            int l = W31 - p.X; a[l][p.Y] = 13; a[l][p.Y + 1] = 33;
        }
    }
    /* 69/70 floor details */
    k = RollK(0, 0.949999988079071, 0.8999999761581421);
    if (k > 0) {
        Vec<Pt, 1024> list;
        for (int l = 0; l < W16; l++) for (int m = 0; m < 23; m++)
            if (!baseSolids.s[l][m] && baseSolids.s[l][m + 1] && a[l][m] == -1 && Check(bg, l - 1, m - 1, 3, 3, true)) list.Add({l, m});
        Shuffle(e, list);
        while (k > 0 && list.n > 0) {
            Pt p = list[0]; list.RemoveAt(0); k--;
            int num = Choose(e, {69, 70}); int num2 = num == 69 ? 70 : 69;
            a[p.X][p.Y] = num; a[W31 - p.X][p.Y] = num2;
        }
    }
    /* vertical vines (12 / 32 / 52) */
    k = RollK(0, 0.800000011920929, 0.5);
    if (k > 0) {
        Vec<Rect, 1024> list2; bool has = false; Rect r{};
        for (int l = 1; l < W16; l++) {
            for (int m = 0; m < 24; m++) {
                bool flag = !baseSolids.s[l][m] && Check(bg, l - 1, m - 1, 3, 3, true) && a[l][m] == -1;
                if (has) {
                    if (flag) { r.H++; continue; }
                    if (r.H >= 4) list2.Add(r);
                    has = false;
                } else if (flag) { r = {l, m, 1, 1}; has = true; }
            }
            if (has) { if (r.H >= 4) list2.Add(r); has = false; }
        }
        Shuffle(e, list2);
        while (k > 0 && list2.n > 0) {
            Rect r2 = list2[0]; list2.RemoveAt(0); k--;
            if (r2.H > 3) {
                Rect r3 = r2;
                r2.Y += e.Next(r2.H - 3); r2.H = 3;
                if (r3.Bottom() - r2.Bottom() > 0) r2.H += e.Next(r3.Bottom() - r2.Bottom());
            }
            a[r2.X][r2.Y] = 12; a[r2.X][r2.Bottom() - 1] = 52;
            for (int i = 1; i < r2.H - 1; i++) a[r2.X][r2.Y + i] = W32;   /* the tile 32: widened by WiderSet (T32) */
            int l = W31 - r2.X;
            a[l][r2.Y] = 12; a[l][r2.Bottom() - 1] = 52;
            for (int i = 1; i < r2.H - 1; i++) a[l][r2.Y + i] = W32;
        }
    }
    /* horizontal vines (71 / 72 / 73) */
    k = RollK(0, 0.800000011920929, 0.5);
    if (k > 0) {
        Vec<Rect, 1024> list2; bool has = false; Rect r{};
        for (int m = 0; m < 24; m++) {
            for (int l = 1; l < W16; l++) {
                bool flag = !baseSolids.s[l][m] && Check(bg, l - 1, m - 1, 3, 3, true) && a[l][m] == -1;
                if (has) {
                    if (flag) { r.W++; continue; }
                    if (r.W >= 4) list2.Add(r);
                    has = false;
                } else if (flag) { r = {l, m, 1, 1}; has = true; }
            }
            if (has) { if (r.W >= 4) list2.Add(r); has = false; }
        }
        Shuffle(e, list2);
        while (k > 0 && list2.n > 0) {
            Rect r2 = list2[0]; list2.RemoveAt(0); k--;
            if (r2.W > 3) {
                Rect r3 = r2;
                r2.X += e.Next(r2.W - 3); r2.W = 3;
                if (r3.Right() - r2.Right() > 0) r2.W += e.Next(r3.Right() - r2.Right());
            }
            if (r2.Right() == W16) {
                r2.W *= 2;
                a[r2.X][r2.Y] = 71; a[r2.Right() - 1][r2.Y] = 73;
                for (int i = 1; i < r2.W - 1; i++) a[r2.X + i][r2.Y] = 72;
                continue;
            }
            a[r2.X][r2.Y] = 71; a[r2.Right() - 1][r2.Y] = 73;
            for (int i = 1; i < r2.W - 1; i++) a[r2.X + i][r2.Y] = 72;
            int l = W32 - r2.X - r2.W;
            a[l][r2.Y] = 71; a[l + r2.W - 1][r2.Y] = 73;
            for (int i = 1; i < r2.W - 1; i++) a[l + i][r2.Y] = 72;
        }
    }
    /* single details (48 / 51 / 53 / 68) */
    k = RollK(1, 1.0, 0.949999988079071);
    if (k > 0) {
        Vec<Pt, 1024> list;
        for (int l = 0; l < W16; l++) for (int m = 0; m < 24; m++)
            if (!baseSolids.s[l][m] && a[l][m] == -1 && Check(bg, l - 1, m - 1, 3, 3, true)) list.Add({l, m});
        Shuffle(e, list);
        while (k > 0 && list.n > 0) {
            Pt p = list[0]; list.RemoveAt(0); k--;
            int num = Choose(e, {48, 51, 53, 68});
            a[p.X][p.Y] = num; a[W31 - p.X][p.Y] = num;
        }
    }
    /* BGLanterns (BGTorch entities) */
    k = RollK(1, 1.0, 0.8999999761581421);
    if (k > 0) {
        Vec<Pt, 1024> list;
        for (int l = 2; l <= W16; l++) {
            if (l == 15) continue;
            for (int m = 2; m <= 22; m++)
                if (Check(baseSolids, l - 1, m - 1, 2, 3, false) && Check(bg, l - 2, m - 1, 4, 3, true) && Empty(l - 1, m, 2, 1)) list.Add({l, m});
        }
        Shuffle(e, list);
        while (k > 0 && list.n > 0) {
            Pt p = list[0]; list.RemoveAt(0); k--;
            auto x = e.El("BGLantern"); e.Attr(x, "x", p.X * 10); e.Attr(x, "y", p.Y * 10); e.Append(x);
            if (p.X != W16) { x = e.El("BGLantern"); e.Attr(x, "x", (W32 - p.X) * 10); e.Attr(x, "y", p.Y * 10); e.Append(x); }
            for (int i = 0; i < list.n; i++) {
                Pt q = list[i];
                if (std::abs(q.X - p.X) <= 2 && std::abs(q.X - p.X) <= 2) { list.RemoveAt(i); i--; }   /* (the PC tests X twice) */
            }
        }
    }
    /* vertical pillars (14 / 15 / 74, 35, 34 / 54) */
    k = RollK(0, 0.800000011920929, 0.699999988079071);
    if (k > 0) {
        Vec<Rect, 1024> list2; bool has = false; Rect r{};
        for (int l = 1; l < 15; l++) {
            for (int m = 1; m < 24; m++) {
                if (has) {
                    if (finalSolids.s[l][m]) { has = false; continue; }
                    r.H++;
                    if ((m < 23 && finalSolids.s[l][m + 1]) || Check(bg, l - 1, m, 3, 1, true)) {
                        if (r.H >= 4 && r.H <= 10) list2.Add(r);
                        has = false;
                    } else if (!Check(bg, l - 1, m, 3, 1, false) || !Check(finalSolids, l - 1, m, 3, 1, false)) has = false;
                } else if ((Check(bg, l - 1, m, 3, 1, true) && !finalSolids.s[l][m]) ||
                           (finalSolids.s[l][m - 1] && Check(bg, l - 1, m, 3, 1, false) && Check(finalSolids, l - 1, m, 3, 1, false))) {
                    r = {l, m, 1, 1}; has = true;
                }
            }
            has = false;
        }
        Shuffle(e, list2);
        while (k > 0 && list2.n > 0) {
            Rect r2 = list2[0]; list2.RemoveAt(0); k--;
            int num3 = W31 - r2.X;
            a[r2.X][r2.Y] = 14; a[r2.X][r2.Bottom() - 1] = 74;
            for (int i = 1; i < r2.H - 1; i++) a[r2.X][r2.Y + i] = 15;
            a[num3][r2.Y] = 14; a[num3][r2.Bottom() - 1] = 74;
            for (int i = 1; i < r2.H - 1; i++) a[num3][r2.Y + i] = 15;
            if (r2.H >= 6) {
                if (ChanceF(e, 0.7f)) {
                    int num4 = 1 + e.Next((r2.H - 2) / 2);
                    a[r2.X][r2.Y + num4] = 35; a[num3][r2.Y + num4] = 35;
                    num4 = r2.H - r2.H / 2 + e.Next((r2.H - 2) / 2);
                    a[r2.X][r2.Y + num4] = 35; a[num3][r2.Y + num4] = 35;
                } else if (ChanceF(e, 0.9f)) {
                    int num4 = 2 + e.Next(r2.H - 4);
                    a[r2.X][r2.Y + num4] = 35; a[num3][r2.Y + num4] = 35;
                }
            } else if (ChanceF(e, 0.9f)) {
                int num4 = 1 + e.Next(r2.H - 2);
                a[r2.X][r2.Y + num4] = 35; a[num3][r2.Y + num4] = 35;
            }
            if (ChanceF(e, 0.3f)) {
                int num5 = 1 + e.Next(r2.H - 3);
                int num6 = num5 + 1 + e.Next(r2.H - 2 - num5);
                a[r2.X][r2.Y + num5] = 34; a[r2.X][r2.Y + num6] = 54;
                for (int i = num5 + 1; i < num6; i++) a[r2.X][r2.Y + i] = -1;
            }
            if (ChanceF(e, 0.3f)) {
                int num5 = 1 + e.Next(r2.H - 3);
                int num6 = num5 + 1 + e.Next(r2.H - 2 - num5);
                a[num3][r2.Y + num5] = 34; a[num3][r2.Y + num6] = 54;
                for (int i = num5 + 1; i < num6; i++) a[num3][r2.Y + i] = -1;
            }
            for (int i = 0; i < list2.n; i++) {
                Rect r4 = list2[i];
                if (std::abs(r4.X - r2.X) <= 1 && r4.Y < r2.Bottom() && r2.Y < r4.Bottom()) { list2.RemoveAt(i); i--; }
            }
        }
    }
    /* horizontal beams (16 / 55 / 19, 75, 17 / 18) */
    k = RollK(0, 0.5, 0.699999988079071);
    if (k > 0) {
        Vec<Rect, 1024> list2; bool has = false; Rect r{};
        for (int m = 1; m < 23; m++) {
            for (int l = 1; l < W16; l++) {
                if (has) {
                    if (finalSolids.s[l][m] || a[l][m] != -1) { has = false; continue; }
                    r.W++;
                    if ((l < 15 && finalSolids.s[l + 1][m]) || Check(bg, l, m - 1, 1, 3, true)) {
                        if (r.W >= 4 && r.W <= 8) list2.Add(r);
                        has = false;
                    } else if (!Check(bg, l, m - 1, 1, 3, false) || !Check(finalSolids, l, m - 1, 1, 3, false)) has = false;
                } else if (a[l][m] == -1 && ((Check(bg, l, m - 1, 1, 3, true) && !finalSolids.s[l][m]) ||
                           (finalSolids.s[l - 1][m] && Check(bg, l, m - 1, 1, 3, false) && Check(finalSolids, l, m - 1, 1, 3, false)))) {
                    r = {l, m, 1, 1}; has = true;
                }
            }
            if (has && r.W >= 2 && r.W <= 4) {
                Rect v = r; v.W *= 2;
                bool flag2 = true;
                for (int i = 0; i < v.W; i++) if (a[v.X + i][v.Y] != -1) { flag2 = false; break; }
                if (flag2) list2.Add(v);
            }
            has = false;
        }
        Shuffle(e, list2);
        const int T16a = wide ? 21 : 16;   /* the tile 16 of the centre beam: IL occurrence 12, widened by WiderSet */
        while (k > 0 && list2.n > 0) {
            Rect r2 = list2[0]; list2.RemoveAt(0); k--;
            if (r2.Right() > W16) {
                a[r2.X][r2.Y] = T16a; a[r2.Right() - 1][r2.Y] = 19;
                for (int i = 1; i < r2.W - 1; i++) a[r2.X + i][r2.Y] = 55;
                if (ChanceF(e, 0.9f)) {
                    int num7 = 1 + e.Next((r2.H - 2) / 2);
                    a[r2.X + num7][r2.Y] = 75; a[r2.X + r2.W - 1 - num7][r2.Y] = 75;
                }
                if (ChanceF(e, 0.3f)) {
                    int num5 = 1 + e.Next(r2.W - 3);
                    int num6 = num5 + 1 + e.Next(r2.W - 2 - num5);
                    a[r2.X + num5][r2.Y] = 17; a[r2.X + num6][r2.Y] = 18;
                    for (int i = num5 + 1; i < num6; i++) a[r2.X + i][r2.Y] = -1;
                }
            } else {
                int num3 = W32 - r2.X - r2.W;
                a[r2.X][r2.Y] = 16; a[r2.Right() - 1][r2.Y] = 19;   /* T16: occurrences 13 and 14, left alone */
                for (int i = 1; i < r2.W - 1; i++) a[r2.X + i][r2.Y] = 55;
                a[num3][r2.Y] = 16; a[num3 + r2.W - 1][r2.Y] = 19;
                for (int i = 1; i < r2.W - 1; i++) a[num3 + i][r2.Y] = 55;
                if (r2.W >= 6) {
                    if (ChanceF(e, 0.7f)) {
                        int num7 = 1 + e.Next((r2.H - 2) / 2);
                        a[r2.X + num7][r2.Y] = 75; a[num3 + r2.W - 1 - num7][r2.Y] = 75;
                        num7 = r2.W - r2.W / 2 + e.Next((r2.W - 2) / 2);
                        a[r2.X + num7][r2.Y] = 75; a[num3 + r2.W - 1 - num7][r2.Y] = 75;
                    } else if (ChanceF(e, 0.9f)) {
                        int num7 = 2 + e.Next(r2.W - 4);
                        a[r2.X + num7][r2.Y] = 75; a[num3 + r2.W - 1 - num7][r2.Y] = 75;
                    }
                } else if (ChanceF(e, 0.9f)) {
                    int num7 = 1 + e.Next(r2.W - 2);
                    a[r2.X + num7][r2.Y] = 75; a[num3 + r2.W - 1 - num7][r2.Y] = 75;
                }
                if (ChanceF(e, 0.3f)) {
                    int num5 = 1 + e.Next(r2.W - 3);
                    int num6 = num5 + 1 + e.Next(r2.W - 2 - num5);
                    a[r2.X + num5][r2.Y] = 17; a[r2.X + num6][r2.Y] = 18;
                    for (int i = num5 + 1; i < num6; i++) a[r2.X + i][r2.Y] = -1;
                }
                if (ChanceF(e, 0.3f)) {
                    int num5 = 1 + e.Next(r2.W - 3);
                    int num6 = num5 + 1 + e.Next(r2.W - 2 - num5);
                    a[num3 + num5][r2.Y] = 17; a[num3 + num6][r2.Y] = 18;
                    for (int i = num5 + 1; i < num6; i++) a[num3 + i][r2.Y] = -1;
                }
            }
            for (int i = 0; i < list2.n; i++) {
                Rect r4 = list2[i];
                if (std::abs(r4.Y - r2.Y) <= 1 && r4.X < r2.Right() && r2.X < r4.Right()) { list2.RemoveAt(i); i--; }
            }
        }
    }
    /* bushes (58 / 59 / 78 / 79) */
    k = RollK(0, 0.800000011920929, 0.4000000059604645);
    if (k > 0) {
        Vec<Pt, 1024> list;
        for (int l = 1; l <= 16; l++) {   /* T16: IL occurrence 15, left alone by WiderSet (16 in wide too) */
            if (l == 15) continue;
            for (int m = 2; m < 23; m++)
                if (Check(baseSolids, l - 1, m + 1, 2, 1, true) && Check(baseSolids, l - 1, m - 1, 2, 2, false) && Check(bg, l - 1, m - 1, 2, 2, false) && Empty(l - 1, m - 1, 2, 2))
                    list.Add({l, m});
        }
        Shuffle(e, list);
        while (k > 0 && list.n > 0) {
            Pt p = list[0]; list.RemoveAt(0); k--;
            a[p.X - 1][p.Y - 1] = 58; a[p.X][p.Y - 1] = 59; a[p.X - 1][p.Y] = 78; a[p.X][p.Y] = 79;
            if (p.X != W16) {
                int num3 = W32 - p.X;
                a[num3 - 1][p.Y - 1] = 58; a[num3][p.Y - 1] = 59; a[num3 - 1][p.Y] = 78; a[num3][p.Y] = 79;
            }
            for (int i = 0; i < list.n; i++) {
                Pt q = list[i];
                if (std::abs(q.X - p.X) <= 1 && std::abs(q.Y - p.Y) <= 1) { list.RemoveAt(i); i--; }
            }
        }
    }
    /* hanging chains (37 / 57 / 77) */
    k = RollK(0, 0.800000011920929, 0.6000000238418579);
    if (k > 0) {
        Vec<Rect, 1024> list2;
        for (int l = 1; l < 15; l++)
            for (int m = 22; m > 5; m--)
                if (Check(bg, l, m - 2, 1, 3, false) && Check(baseSolids, l, m - 2, 1, 3, false) && baseSolids.s[l][m + 1] && Empty(l, m - 2, 1, 3)) {
                    Rect v = {l, m - 1, 1, 2};
                    for (int i = 0; i < 2; i++) {
                        if (bg.s[l][v.Y - 2]) break;
                        if (baseSolids.s[l][v.Y - 2]) break;
                        if (a[l][v.Y - 2] != -1) break;
                        v.H++; v.Y--;
                    }
                    list2.Add(v);
                }
        Shuffle(e, list2);
        while (k > 0 && list2.n > 0) {
            Rect r2 = list2[0]; list2.RemoveAt(0); k--;
            Rect r5 = r2; r5.X = W31 - r2.X;
            if (r2.H > 2) {
                int num8 = 2 + e.Next(r2.H - 1);
                int num9 = 2 + e.Next(r2.H - 1);
                r2.Y = r2.Bottom() - num8; r2.H = num8;
                r5.Y = r5.Bottom() - num9; r5.H = num9;
            }
            a[r2.X][r2.Y] = 37; a[r2.X][r2.Bottom() - 1] = 77;
            for (int i = 1; i < r2.H - 1; i++) a[r2.X][r2.Y + i] = 57;
            a[r5.X][r5.Y] = 37; a[r5.X][r5.Bottom() - 1] = 77;
            for (int i = 1; i < r5.H - 1; i++) a[r5.X][r5.Y + i] = 57;
        }
    }
    /* floor tufts (38 / 39) */
    k = RollK(1, 1.0, 0.800000011920929);
    if (k > 0) {
        Vec<Pt, 1024> list;
        for (int l = 0; l < 15; l++) for (int m = 1; m < 23; m++)
            if (baseSolids.s[l][m + 1] && !baseSolids.s[l][m] && !bg.s[l][m] && a[l][m] == -1) list.Add({l, m});
        Shuffle(e, list);
        while (k > 0 && list.n > 0) {
            Pt p = list[0]; list.RemoveAt(0); k--;
            if (ChanceF(e, 0.6f)) {
                int num3 = W31 - p.X;
                if (ChanceF(e, 0.5f)) a[p.X][p.Y] = 38; else a[num3][p.Y] = 38;
            } else {
                int num3 = W31 - p.X;
                a[p.X][p.Y] = 39; a[num3][p.Y] = 39;
                k--;
            }
        }
    }
}

} // namespace levelbg
