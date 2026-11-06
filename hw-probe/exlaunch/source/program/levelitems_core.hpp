/* The PC's LevelRandomItems.AddItems (TowerFall 1.3.3.3, the build TF.EX runs; decompiled into switch-online/.cache/pc-src),
 * transcribed line for line. Cataclysm's procedural items: exploding orbs, mud, jump pads (carved into the solids), crumble walls
 * (carved), a spike ball or a moving platform; entities appended to the level's <Entities> in the PC's order.
 *
 * Header-only and environment-agnostic, so the same code runs in the game (levelitems.cpp: the Switch's Calc.Random through rnd.cpp's
 * .NET Random, its Math.Pow, its XmlDocument) and on the host (tools/test-levelitems.cpp, against tools/re/pcgen.py, which matches
 * the browser's grid). Env provides:
 *   int Next(int max); double Sample();               Calc.Random.Next(max) / NextDouble()
 *   double Pow(double a, double b);                   Math.Pow
 *   H El(const char* name); void Attr(H, const char* name, int v); void AttrS(H, const char* name, const char* v);
 *   void Append(H child);                             entities.AppendChild(child)
 *   void AppendTo(H parent, H child);                 parent.AppendChild(child)
 *   void MovingPlatform(int x, int y, int w, int h);  MovingPlatforms.Add(new Rectangle(x, y, w, h))
 * The PC's spawn rectangles (TeamSpawnA/B, PlayerSpawn) are built but never used, so they aren't read here. */
#pragma once
#include <cstdint>
#include <cstdlib>

namespace levelitems {

struct Rect {
    int X, Y, W, H;
    int Right() const { return X + W; }
    int Bottom() const { return Y + H; }
    bool Contains(int x, int y) const { return X <= x && x < X + W && Y <= y && y < Y + H; }
    bool Intersects(const Rect& o) const { return o.X < X + W && X < o.X + o.W && o.Y < Y + H && Y < o.Y + o.H; }
};
struct Pt { int X, Y; };

template <class T, int N> struct Vec {   /* List<T> with the operations AddItems uses (fixed capacity: the grid is up to 42x24) */
    T v[N]; int n = 0;
    void Add(const T& t) { if (n < N) v[n++] = t; }
    void RemoveAt(int i) { for (int k = i; k < n - 1; k++) v[k] = v[k + 1]; n--; }
    T& operator[](int i) { return v[i]; }
};

struct Grid { bool s[42][24]; };   /* columns 0..31 (wide: 0..41) used */

template <class Env, class T, int N> inline void Shuffle(Env& e, Vec<T, N>& l) {   /* Calc.Shuffle: Fisher-Yates from the end */
    int num = l.n;
    while (--num > 0) { T value = l[num]; int index = e.Next(num + 1); l[num] = l[index]; l[index] = value; }
}
template <class Env> inline float NextFloat(Env& e) { return (float)e.Sample(); }
template <class Env> inline bool ChanceD(Env& e, double c) { return (double)NextFloat(e) < c; }   /* Chance(double) */
template <class Env> inline bool ChanceF(Env& e, float c) { return NextFloat(e) < c; }            /* Chance(float) */
template <class Env> inline bool ChooseBool(Env& e, bool a, bool b) { return e.Next(2) == 0 ? a : b; }

template <int N> inline bool Collide(Vec<Rect, N>& rects, int x, int y) { for (int i = 0; i < rects.n; i++) if (rects[i].Contains(x, y)) return true; return false; }
template <int N> inline bool Collide(Vec<Rect, N>& rects, const Rect& r) { for (int i = 0; i < rects.n; i++) if (rects[i].Intersects(r)) return true; return false; }
inline bool Collide(const Grid& g, const Rect& r, int ax = 0, int ay = 0, bool wide = false) {
    for (int i = r.X + ax; i < r.X + ax + r.W; i++)
        for (int j = r.Y + ay; j < r.Y + ay + r.H; j++)
            if (i >= 0 && j >= 0 && i < (wide ? 42 : 32) && j < 24 && g.s[i][j]) return true;
    return false;
}

/* solids: in/out (carved). base: BaseSolids = solids before (the caller keeps the managed clone). */
template <class Env> void AddItems(Env& e, Grid& solids, const Grid& base, bool teams, bool wide = false) {
    using H = decltype(e.El(""));
    auto& S = solids.s; const auto& B = base.s;
    /* WiderSet's LevelRandomItemsHooks (wide): every ldc.i4 16, 15, 17, 11 in AddItems + 5, 32, 31, 30, 29 + 10, "310" / "160" + 100 / + 50;
     * Collide(bool[,], Rectangle): its 32 + 10. Each K() below is one such IL constant (the PC's IL has 20 x 16, 4 x 15, 8 x 32, 4 x 31). */
    auto K = [wide](int v) { return !wide ? v : (v == 16 || v == 15 || v == 17 || v == 11) ? v + 5 : v + 10; };
    auto CollideB = [wide](const Grid& g, const Rect& r, int ax = 0, int ay = 0) { return Collide(g, r, ax, ay, wide); };
    Vec<Rect, 64> list2;
    int i;
    for (i = 0; ChanceD(e, 0.20000000298023224 * e.Pow(0.8999999761581421, i)); i++) {}
    if (i > 0) {
        Vec<Pt, 768> list3;
        for (int j = 2; j <= K(16); j++)
            for (int k = 2; k <= 23; k++) {
                if (S[j][k] || S[j - 1][k] || S[j - 1][k - 1] || S[j][k - 1]) continue;
                bool flag = true;
                for (int l = 1; l < 8; l++) if (S[j][(k + l) % 24] || S[j - 1][(k + l) % 24]) { flag = false; break; }
                if (flag) list3.Add({ j, k });
            }
        Shuffle(e, list3);
        while (i > 0 && list3.n > 0) {
            Pt point = list3[0]; list3.RemoveAt(0); i--;
            list2.Add({ point.X - 1, point.Y - 1, 2, 2 });
            for (int m = 0; m < list3.n; m++) if (std::abs(list3[m].X - point.X) <= 1) { list3.RemoveAt(m); m--; }
            H x = e.El("ExplodingOrb"); e.Attr(x, "x", point.X * 10); e.Attr(x, "y", point.Y * 10); e.Append(x);
            if (point.X != K(16)) { x = e.El("ExplodingOrb"); e.Attr(x, "x", (K(32) - point.X) * 10); e.Attr(x, "y", point.Y * 10); e.Append(x); }
        }
    }
    for (i = 0; ChanceD(e, 0.20000000298023224 * e.Pow(0.5, i)); i++) {}
    if (i > 0) {
        Vec<Rect, 768> list5;
        bool has = false; Rect rc{};
        for (int k = 2; k < 23; k++) {
            for (int j = 1; j < K(16); j++) {
                if (!has) { if (!S[j][k] && S[j - 1][k] && S[j][k + 1]) { rc = { j, k, 1, 1 }; has = true; } }
                else if (S[j][k] && S[j][k + 1]) { list5.Add(rc); has = false; }
                else if (S[j][k + 1]) rc.W++;
                else has = false;
            }
            if (has) { list5.Add(rc); has = false; }
        }
        for (int m = 0; m < list5.n; m++) {
            if (list5[m].Right() == K(16)) { if (list5[m].W <= 1) { list5.RemoveAt(m); m--; } }
            else if (list5[m].W <= 2) { list5.RemoveAt(m); m--; }
        }
        Shuffle(e, list5);
        for (int m = 0; m < list5.n && m < i; m++) {
            Rect item = list5[m];
            if (item.Right() == K(16)) {
                H x = e.El("Mud"); e.Attr(x, "x", item.X * 10); e.Attr(x, "y", item.Y * 10 + 5); e.Attr(x, "width", item.W * 10 * 2); e.Append(x);
                continue;
            }
            H x = e.El("Mud"); e.Attr(x, "x", item.X * 10); e.Attr(x, "y", item.Y * 10 + 5); e.Attr(x, "width", item.W * 10); e.Append(x);
            x = e.El("Mud"); e.Attr(x, "x", (K(32) - item.X - item.W) * 10); e.Attr(x, "y", item.Y * 10 + 5); e.Attr(x, "width", item.W * 10); e.Append(x);
        }
    }
    Vec<Rect, 1> rects;   /* the PC's `rects`: never filled */
    for (i = 0; ChanceD(e, 0.5 * e.Pow(0.8500000238418579, i)); i++) {}
    if (i > 0) {
        Vec<Rect, 768> list6;
        bool has = false; Rect rc{};
        for (int k = 4; k < 24; k++) {
            for (int j = 1; j < K(16); j++) {
                if (S[j][k] && !S[j][k - 1] && !Collide(rects, j, k)) {
                    if (has) rc.W++;
                    else { rc = { j, k, 1, 1 }; has = true; }
                } else if (has) {
                    if (rc.W > 1 || rc.X == K(15)) list6.Add(rc);
                    has = false;
                }
            }
            if (has) { if (rc.W > 1 || rc.X == K(15)) list6.Add(rc); has = false; }
        }
        Vec<Rect, 768> list7;
        for (int m = 0; m < list6.n; m++) {
            Rect r2 = list6[m];
            bool arr[42];
            for (int j = 0; j < r2.W; j++) {
                arr[j] = true;
                for (int k = -1; k > -7; k--) if (S[r2.X + j][(r2.Y + k + 24) % 24]) { arr[j] = false; break; }
            }
            bool flag2 = false; Rect item2{};
            for (int j = 0; j < r2.W + 1; j++) {
                if (j < r2.W && arr[j]) {
                    if (flag2) { item2.W++; continue; }
                    flag2 = true; item2 = { r2.X + j, r2.Y, 1, 1 };
                } else if (flag2) {
                    flag2 = false;
                    if (item2.W > 1 || item2.Right() == K(16)) list7.Add(item2);
                }
            }
        }
        Shuffle(e, list7);
        for (int m = 0; m < i && m < list7.n; m++) {
            Rect item = list7[m];
            if (item.Right() == K(16)) {
                if (item.W > 1) { int width = item.W; item.W = e.Next(width) + 1; item.X = K(16) - item.W; }
            } else if (item.W > 2) {
                int width = item.W; item.W = e.Next(width - 1) + 2; item.X += e.Next(width - item.W + 1);
            }
            for (int j = item.X; j < item.Right(); j++) { S[j][item.Y] = false; S[K(31) - j][item.Y] = false; }
            if (item.Right() == K(16)) {
                H x = e.El("JumpPad"); e.Attr(x, "x", item.X * 10); e.Attr(x, "y", item.Y * 10); e.Attr(x, "width", item.W * 10 * 2); e.Append(x);
                continue;
            }
            H x = e.El("JumpPad"); e.Attr(x, "x", item.X * 10); e.Attr(x, "y", item.Y * 10); e.Attr(x, "width", item.W * 10); e.Append(x);
            x = e.El("JumpPad"); e.Attr(x, "x", (K(32) - item.X - item.W) * 10); e.Attr(x, "y", item.Y * 10); e.Attr(x, "width", item.W * 10); e.Append(x);
        }
    }
    for (i = 0; ChanceD(e, 0.800000011920929 * e.Pow(0.5, i)); i++) {}
    if (i > 0) {
        Vec<Pt, 768> list3;
        for (int j = 2; j <= K(16); j++) {
            if (j == 1) continue;
            for (int k = 1; k < 22; k++) {
                int num = (j + K(31)) % K(32), num2 = (j + K(30)) % K(32), num3 = (j + K(29)) % K(32);
                if (S[j][k] && S[num][k] && S[j][k + 1] && S[num][k + 1] && S[j][k - 1] && S[num][k - 1]) {
                    int num4 = (int)(!B[num2][k] && !B[num2][k + 1] && !B[num3][k] && !B[num3][k + 1])
                             + (int)(!B[j + 1][k] && !B[j + 1][k + 1] && !B[j + 2][k] && !B[j + 2][k + 1])
                             + (int)(!B[j][k + 2] && !B[num][k + 2]);   /* Calc.Count(true, ...) */
                    if (num4 >= 2) list3.Add({ j, k });
                }
            }
        }
        Shuffle(e, list3);
        while (i > 0 && list3.n > 0) {
            Pt p2 = list3[0]; list3.RemoveAt(0); i--;
            for (int m = 0; m < list3.n; m++)
                if (std::abs(list3[m].X - p2.X) <= 1 && std::abs(list3[m].Y - p2.Y) <= 1) { list3.RemoveAt(m); m--; }
            if (p2.X == 0) {
                if (!teams) {
                    S[0][p2.Y] = false; S[0][p2.Y + 1] = false; S[K(31)][p2.Y] = false; S[K(31)][p2.Y + 1] = false;
                    H x = e.El("CrumbleWall"); e.AttrS(x, "x", wide ? "410" : "310"); e.Attr(x, "y", p2.Y * 10); e.Append(x);
                }
            } else if (p2.X == K(16)) {
                S[K(16)][p2.Y] = false; S[K(16)][p2.Y + 1] = false; S[K(15)][p2.Y] = false; S[K(15)][p2.Y + 1] = false;
                H x = e.El("CrumbleWall"); e.Attr(x, "x", p2.X * 10 - 10); e.Attr(x, "y", p2.Y * 10); e.Append(x);
            } else {
                Pt p3 = p2; p3.X = K(32) - p2.X;
                S[p2.X][p2.Y] = false; S[p2.X][p2.Y + 1] = false; S[p2.X - 1][p2.Y] = false; S[p2.X - 1][p2.Y + 1] = false;
                S[p3.X][p3.Y] = false; S[p3.X][p3.Y + 1] = false; S[p3.X - 1][p3.Y] = false; S[p3.X - 1][p3.Y + 1] = false;
                H x = e.El("CrumbleWall"); e.Attr(x, "x", p2.X * 10 - 10); e.Attr(x, "y", p2.Y * 10); e.Append(x);
                x = e.El("CrumbleWall"); e.Attr(x, "x", p3.X * 10 - 10); e.Attr(x, "y", p3.Y * 10); e.Append(x);
            }
        }
    }
    bool flag3 = false;
    Vec<int, 32> list8;
    for (int k = 6; k < 18; k++) {
        if (!S[K(16)][k] || !S[K(16)][k - 1] || Collide(list2, Rect{ K(11), k - 5, 10, 10 })) continue;
        bool flag4 = true;
        for (int n = 0; n <= 5 && flag4; n++)
            for (int l = -5; l <= 4; l++) {
                bool flag5 = n >= 2 || l > 1 || l < -2;
                if (flag5 && n == 5 && (l == 4 || l == -5)) flag5 = false;
                if (flag5 && B[K(16) + n][k + l]) { flag4 = false; break; }
            }
        if (flag4) list8.Add(k);
    }
    if (list8.n > 0 && ChanceF(e, 0.3f)) {
        flag3 = true;
        Shuffle(e, list8);
        int num5 = list8[0];
        int num6;
        for (num6 = 4; num6 < 6; num6++) {
            bool flag6 = true;
            for (int n = 0; n <= 1 + num6; n++) if (B[K(16) + n][num5 - 2 - num6] || B[K(16) + n][num5 + 1 + num6]) { flag6 = false; break; }
            if (flag6) for (int l = -1 - num6; l <= num6; l++) if (B[K(17) + num6][num5 + l]) { flag6 = false; break; }
            if (!flag6) break;
        }
        H x = e.El("SpikeBall"); e.AttrS(x, "x", wide ? "210" : "160"); e.Attr(x, "y", (num5 - num6) * 10);
        e.AttrS(x, "Explodes", ChooseBool(e, true, false) ? "True" : "False");
        e.Append(x);
        H node = e.El("node"); e.AttrS(node, "x", wide ? "210" : "160"); e.Attr(node, "y", num5 * 10); e.AppendTo(x, node);
    }
    /* MovingPlatforms = new List<Rectangle>() (the caller makes the list) */
    if (flag3) return;
    Vec<Rect, 32> list9; Rect r3{ 7, 8, 9, 6 };
    while (r3.Bottom() < 22) {
        Rect rect = r3; rect.Y -= 3; rect.H += 3; rect.X -= 2; rect.W += 4;
        if (!CollideB(base, rect) && !Collide(list2, rect)) list9.Add(r3);
        r3.Y++;
    }
    Vec<Rect, 32> list10; r3 = { 12, 1, 4, 10 };
    while (r3.Bottom() < 22) {
        Rect rect = r3; rect.X -= 2; rect.W += 4;
        if (!CollideB(base, rect) && !Collide(list2, rect)) list10.Add(r3);
        r3.Y++;
    }
    if ((list9.n <= 0 && list10.n <= 0) || !ChanceF(e, 0.35f)) return;
    bool flag7 = ChooseBool(e, list9.n > 0, list10.n == 0);
    if (list10.n > 0) flag7 = false;
    if (flag7) {
        Shuffle(e, list9);
        Rect r4 = list9[0]; Rect em{ 0, 0, 0, 0 };
        em.W = 3 + e.Next(3); em.H = 2 + e.Next(2); em.X = r4.X + e.Next(3); em.Y = r4.Y + e.Next(7 - em.H);
        Pt p4{ em.X, em.Y }; p4.X = K(32) - em.X - em.W;
        H x = e.El("MovingPlatform"); e.Attr(x, "x", em.X * 10); e.Attr(x, "y", em.Y * 10); e.Attr(x, "width", em.W * 10); e.Attr(x, "height", em.H * 10); e.Append(x);
        H node = e.El("node"); e.Attr(node, "x", p4.X * 10); e.Attr(node, "y", p4.Y * 10); e.AppendTo(x, node);
        e.MovingPlatform(em.X, em.Y, p4.X + em.W, em.H);
        return;
    }
    Shuffle(e, list10);
    Rect r4 = list10[0]; Rect em{ 0, 0, 0, 0 };
    em.W = (1 + e.Next(4)) * 2; em.H = 2 + e.Next(2); em.X = K(16) - em.W / 2; em.Y = r4.Y + e.Next(2);
    Pt p4{ em.X, em.Y }; p4.Y = r4.Bottom() - em.H - e.Next(2);
    if (CollideB(base, em, 0, -2) && !CollideB(base, em, 0, -1)) em.Y--;
    Rect rect2 = em; rect2.Y = p4.Y;
    if (CollideB(base, rect2, 0, 2) && !CollideB(base, rect2, 0, 1)) p4.Y++;
    if (ChooseBool(e, true, false)) { int y = em.Y; em.Y = p4.Y; p4.Y = y; }
    H x = e.El("MovingPlatform"); e.Attr(x, "x", em.X * 10); e.Attr(x, "y", em.Y * 10); e.Attr(x, "width", em.W * 10); e.Attr(x, "height", em.H * 10); e.Append(x);
    H node = e.El("node"); e.Attr(node, "x", p4.X * 10); e.Attr(node, "y", p4.Y * 10); e.AppendTo(x, node);
    e.MovingPlatform(em.X, em.Y, em.W, p4.Y + em.H);
}

} // namespace levelitems
