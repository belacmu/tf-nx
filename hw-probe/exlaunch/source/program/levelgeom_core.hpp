/* The PC's LevelRandomGeometry.GenerateData (TowerFall 1.3.3.3; decompiled into switch-online/.cache/pc-src), transcribed line for
 * line, for TF.EX's wide set (docs/ws-wide.md): WiderSet's LevelRandomGeometryHooks remap every ldc.i4 32, 31, 30 (+ 10) and 16 (+ 5);
 * each K() below is one of those IL constants (the PC's IL has 2 x 32, 4 x 31, 1 x 30, 2 x 16). With wide = false it is the PC's
 * standard GenerateData (the Switch's own body is used there; this one is checked against tools/re/pcgen.py both ways).
 * Same environment as levelitems_core.hpp (Next, Sample, Pow); the grid is levelitems::Grid (42 x 24, columns 0..31 or 0..41 used). */
#pragma once
#include "levelitems_core.hpp"

namespace levelgeom {
using levelitems::Grid; using levelitems::Rect; using levelitems::Vec;

enum Style { HWrap, BottomVWrap, TopVWrap, Single, LeftPocket, RightPocket, LeftWall, RightWall, Ceiling, Floor, Floating, kStyles };

/* rects: the RandomBlock rectangles in tiles (LevelLoaderXML), base: the level's solids; out: the generated grid. */
template <class Env> void GenerateData(Env& e, const Rect* rects, int nRects, const Grid& base, Grid& out, bool wide = false) {
    auto K = [wide](int v) { return !wide ? v : v == 16 ? v + 5 : v + 10; };
    const auto& B = base.s;
    out = Grid{};
    auto& A = out.s;
    const int cols = K(32);                                   /* new bool[32, 24] */
    auto inGrid = [&](int x, int y) { return x >= 0 && y >= 0 && x < cols && y < 24; };
    auto Set = [&](int x, int y, int w = 1, int h = 1) { for (int i = x; i < x + w; i++) for (int j = y; j < y + h; j++) if (inGrid(i, j)) A[i][j] = true; };
    auto Check = [&](int x, int y, int w = 1, int h = 1) { for (int i = x; i < x + w; i++) for (int j = y; j < y + h; j++) if (inGrid(i, j) && A[i][j]) return true; return false; };
    auto All = [&](int x, int y, int w = 1, int h = 1) {
        for (int i = x; i < x + w; i++) for (int j = y; j < y + h; j++) if (!(x >= 0 && i >= 0 && j >= 0 && i < 42 && j < 24 && B[i][j])) return false;
        return true;
    };
    auto Chance = [&](double c) { return levelitems::ChanceD(e, c); };
    auto Choose2 = [&](int a, int b) { return e.Next(2) == 0 ? a : b; };
    Vec<Rect, 64> D[kStyles];
    for (int r = 0; r < nRects; r++) {
        const Rect& rect = rects[r];
        Style key;
        if (rect.X == 0) key = HWrap;
        else if (rect.Y == 0) key = TopVWrap;
        else if (rect.Bottom() == 24) key = BottomVWrap;
        else if (rect.W == 1 && rect.H == 1) key = Single;
        else {
            bool f = All(rect.X - 1, rect.Y, 1, rect.H), f2 = All(rect.Right(), rect.Y, 1, rect.H);
            bool f3 = All(rect.X, rect.Y - 1, rect.W), f4 = All(rect.X, rect.Bottom(), rect.W);
            key = (f && f3 && f4) ? LeftPocket : (f2 && f3 && f4) ? RightPocket : f4 ? Floor : f ? LeftWall : f2 ? RightWall : f3 ? Ceiling : Floating;
        }
        D[key].Add(rect);
    }
    for (int m = 0; m < D[HWrap].n; m++) {
        Rect it = D[HWrap][m];
        bool flag5 = levelitems::ChanceF(e, 0.2f);
        int num = 1 + e.Next(it.W), num2 = e.Next(it.H + 1), x = 0, y;
        if (num2 == it.H - 1 && !flag5) num2 += Choose2(-1, 1);
        if (num2 == it.H - 1) {
            if (num > 0) { int c[4] = { 1, 1, 1, it.W < 2 ? it.W : 2 }; num = c[e.Next(4)]; }
            y = it.Y;
        } else y = Choose2(it.Y, it.Bottom() - num2);
        Set(x, y, num, num2);
    }
    for (int m = 0; m < D[BottomVWrap].n; m++) {
        Rect it = D[BottomVWrap][m];
        bool flag6 = it.W == 1 || (it.X > 0 && !B[it.X - 1][23]) || (it.Right() < K(16) && !B[it.Right()][23]);
        int num = e.Next(it.W + 1);
        if (!flag6 && num == it.W - 1) num += Choose2(1, -1);
        int num2 = 1 + e.Next(it.H);
        int x = Choose2(it.X, it.Right() - num), y = 24 - num2;
        Set(x, y, num, num2); Set(x, 0, num);
    }
    for (int m = 0; m < D[TopVWrap].n; m++) {
        Rect it = D[TopVWrap][m];
        int x = -1;
        for (int i = it.X; i < it.Right(); i++) if (Check(i, 23)) { x = i; break; }
        if (x == -1) continue;
        int num = 1;
        while (Check(x + num, 23) && num < it.W - (x - it.X)) num++;
        int num2 = 1 + e.Next(it.H);
        Set(x, 0, num, num2); Set(x, 23, num);
    }
    for (int m = 0; m < D[Single].n; m++) if (levelitems::ChooseBool(e, true, false)) Set(D[Single][m].X, D[Single][m].Y);
    for (int side = 0; side < 2; side++) {
        Vec<Rect, 64>& L = D[side == 0 ? LeftPocket : RightPocket];
        for (int m = 0; m < L.n; m++) {
            Rect it = L[m];
            int num = e.Next(it.W + 1);
            int x = side == 0 ? it.X : it.Right() - num;
            if (num <= 0) continue;
            int num3 = e.Next(it.H - 2);
            if (num3 > 0) num3 += 2;
            int num4 = e.Next(it.H - num3), num5;
            if (num3 > 0) num5 = -num + 1 + e.Next(it.W);
            else {
                int lim = it.W - num + 1; num5 = -1 + e.Next(3 < lim ? 3 : lim);
                if (num4 == it.H - 1) num4--;
            }
            for (int i = 0; i < it.H; i++) {
                if (i < num4) Set(x, it.Y + i, num);
                else if (i >= num4 + num3) { if (side == 0) Set(x, it.Y + i, num + num5); else Set(x - num5, it.Y + i, num + num5); }
            }
        }
    }
    for (int side = 0; side < 2; side++) {
        Vec<Rect, 64>& L = D[side == 0 ? LeftWall : RightWall];
        for (int m = 0; m < L.n; m++) {
            Rect it = L[m];
            int num6 = e.Next(it.W + 1), num7 = 0;
            for (int i = 0; i < it.H; i++) {
                Set(side == 0 ? it.X : it.Right() - num6, it.Y + i, num6);
                num7++;
                if ((side == 0 || num7 >= 2) && !Chance(e.Pow(0.9, num7))) {
                    int num8 = e.Next(it.W + 1);
                    if (num8 != num6) { num6 = num8; num7 = 0; }
                }
            }
        }
    }
    for (int side = 0; side < 2; side++) {
        Vec<Rect, 64>& L = D[side == 0 ? Ceiling : Floor];
        for (int m = 0; m < L.n; m++) {
            Rect it = L[m];
            int num9 = e.Next(it.H + 1), num7 = 0;
            for (int i = 0; i < it.W; i++) {
                Set(it.X + i, side == 0 ? it.Y : it.Bottom() - num9, 1, num9);
                num7++;
                if (!Chance(e.Pow(side == 0 ? 0.9 : 0.85, num7))) {
                    int num10 = e.Next(it.H + 1);
                    if (num10 != num9) { num9 = num10; num7 = 0; }
                }
            }
        }
    }
    Vec<Rect, 64>& F = D[Floating];
    levelitems::Shuffle(e, F);
    while (F.n >= 5) F.RemoveAt(0);
    for (int m = 0; m < F.n; m++) {
        Rect it = F[m];
        int area = it.W * it.H, q = area / 4;
        int num12 = (q > 1 ? q : 1) + Choose2(0, 1);
        for (int i = 0; i < num12; i++) {
            int num = e.Next(it.W + 1), num2 = e.Next(it.H + 1);
            int x = it.X + e.Next(it.W - num), y = it.Y + e.Next(it.H - num2);
            Set(x, y, num, num2);
        }
    }
    for (int x = 0; x < K(16); x++) for (int y = 0; y < 24; y++) if (A[x][y]) A[K(31) - x][y] = true;
    for (int x = 0; x < K(32); x++) for (int y = 0; y < 24; y++) if (B[x][y]) A[x][y] = true;
    for (int y = 1; y < 23; y++) for (int x = 1; x < K(31); x++) if (!A[x][y] && A[x - 1][y] && A[x][y - 1] && A[x][y + 1]) A[x][y] = true;
    for (int y = 1; y < 23; y++) for (int x = K(30); x > 0; x--) if (!A[x][y] && A[x + 1][y] && A[x][y - 1] && A[x][y + 1]) A[x][y] = true;
    for (int x = 1; x < K(31); x++) for (int y = 1; y < 23; y++) if (!A[x][y] && A[x][y - 1] && A[x - 1][y] && A[x + 1][y]) A[x][y] = true;
    for (int x = 1; x < K(31); x++) for (int y = 22; y > 0; y--) if (!A[x][y] && A[x][y + 1] && A[x - 1][y] && A[x + 1][y]) A[x][y] = true;
}

} // namespace levelgeom
