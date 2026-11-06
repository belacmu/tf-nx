#!/usr/bin/env python3
"""pcgen.py <level.oel> <seed> [grid hex to compare]: the PC's procedural level geometry (LevelRandomGeometry.GenerateData,
TowerFall 1.3.3.3, decompiled into switch-online/.cache/pc-src), with .NET's seeded System.Random, as LevelLoaderXML runs it:
Calc.PushRandom(RandomLevelSeed), the RandomBlock rectangles, the base Solids. Prints the 32x24 grid as the solidsdump hex
(bool[32,24] in memory order: index x*24+y, 4 cells per hex digit), and compares it with a dump if given.
"""
import sys, xml.etree.ElementTree as ET

class NetRandom:   # System.Random(int seed): the legacy subtractive generator (.NET Framework / Net5CompatSeedImpl)
    MBIG, MSEED = 2147483647, 161803398
    def __init__(self, seed):
        sa = [0] * 56
        sub = self.MBIG if seed == -2147483648 else abs(seed)
        mj = self.MSEED - sub; sa[55] = mj; mk = 1
        for i in range(1, 55):
            ii = (21 * i) % 55; sa[ii] = mk; mk = mj - mk
            if mk < 0: mk += self.MBIG
            mj = sa[ii]
        for _ in range(4):
            for i in range(1, 56):
                sa[i] -= sa[1 + (i + 30) % 55]
                sa[i] = (sa[i] + 2**31) % 2**32 - 2**31   # int32 wrap
                if sa[i] < 0: sa[i] += self.MBIG
        self.sa, self.inext, self.inextp = sa, 0, 21
        self.draws = 0; self.log = []
    def internal(self):
        self.draws += 1
        a, b = self.inext + 1, self.inextp + 1
        if a >= 56: a = 1
        if b >= 56: b = 1
        r = self.sa[a] - self.sa[b]
        if r == self.MBIG: r -= 1
        if r < 0: r += self.MBIG
        self.sa[a] = r; self.inext, self.inextp = a, b
        return r
    def _sample(self): return self.internal() * (1.0 / self.MBIG)
    def sample(self):   # NextDouble -> Sample (logged as the Switch's GenDraw: "Sample(e9)" = (long long)(d * 1e9))
        d = self._sample(); self.log.append(("Sample(e9)", 0, int(d * 1e9))); return d
    def next(self, mx):   # Next(int maxValue)
        r = int(self._sample() * mx); self.log.append(("NextMax", mx, r)); return r
    def next_float(self):
        import struct
        return struct.unpack('f', struct.pack('f', self.sample()))[0]   # (float)NextDouble()
    def chance_f(self, p32):
        import struct
        return self.next_float() < struct.unpack('f', struct.pack('f', p32))[0]
    def chance_d(self, p): return self.next_float() < p
    def choose(self, *c): return c[self.next(len(c))]
    def shuffle(self, lst):
        n = len(lst)
        while True:
            n -= 1
            if n <= 0: break
            i = self.next(n + 1); lst[n], lst[i] = lst[i], lst[n]

WIDE = False   # --wide: WiderSet's wide set (docs/ws-wide.md): its IL constant remaps in GenerateData and AddItems, 42 columns
def K(v):      # one C# literal of GenerateData / AddItems / Collide, as WiderSet's transpilers leave it (each K() is one IL constant)
    if not WIDE: return v
    return v + 5 if v in (11, 15, 16, 17) else v + 10
def W(): return 42 if WIDE else 32   # the grid's columns (Calc.GetBitData(.., 32 -> 42, 24) in LevelLoaderXML)
def bitdata(text, w=32, h=24):   # Calc.GetBitData
    a = [[False] * h for _ in range(w)]; x = y = 0
    for ch in text:
        if ch == '1': a[x][y] = True; x += 1
        elif ch == '0': a[x][y] = False; x += 1
        elif ch == '\n': x = 0; y += 1
        if y >= h: break
    return a

class R:
    def __init__(s, x, y, w, h): s.X, s.Y, s.W, s.H = x, y, w, h
    Right = property(lambda s: s.X + s.W); Bottom = property(lambda s: s.Y + s.H)

def Set(d, x, y, w=1, h=1):
    for i in range(x, x + w):
        for j in range(y, y + h): d[i][j] = True
def Check(d, x, y, w=1, h=1):
    for i in range(x, x + w):
        for j in range(y, y + h):
            if d[i][j]: return True
    return False
def All(d, x, y, w=1, h=1):
    for i in range(x, x + w):
        for j in range(y, y + h):
            if not d[i][j]: return False
    return True

def generate(rects, base, rnd):
    A = [[False] * 24 for _ in range(K(32))]
    names = ['HWrap', 'BottomVWrap', 'TopVWrap', 'Single', 'LeftPocket', 'RightPocket', 'LeftWall', 'RightWall', 'Ceiling', 'Floor', 'Floating']
    D = {n: [] for n in names}
    for r in rects:
        if r.X == 0: k = 'HWrap'
        elif r.Y == 0: k = 'TopVWrap'
        elif r.Bottom == 24: k = 'BottomVWrap'
        elif r.W == 1 and r.H == 1: k = 'Single'
        else:
            f1 = All(base, r.X - 1, r.Y, 1, r.H); f2 = All(base, r.Right, r.Y, 1, r.H)
            f3 = All(base, r.X, r.Y - 1, r.W); f4 = All(base, r.X, r.Bottom, r.W)
            if f1 and f3 and f4: k = 'LeftPocket'
            elif f2 and f3 and f4: k = 'RightPocket'
            elif f4: k = 'Floor'
            elif f1: k = 'LeftWall'
            elif f2: k = 'RightWall'
            elif f3: k = 'Ceiling'
            else: k = 'Floating'
        D[k].append(r)
    for it in D['HWrap']:
        f5 = rnd.chance_f(0.2); num = 1 + rnd.next(it.W); num2 = rnd.next(it.H + 1); x = 0
        if num2 == it.H - 1 and not f5: num2 += rnd.choose(-1, 1)
        if num2 == it.H - 1:
            if num > 0: num = rnd.choose(1, 1, 1, min(2, it.W))
            y = it.Y
        else: y = rnd.choose(it.Y, it.Bottom - num2)
        Set(A, x, y, num, num2)
    for it in D['BottomVWrap']:
        f6 = it.W == 1 or (it.X > 0 and not base[it.X - 1][23]) or (it.Right < K(16) and not base[it.Right][23])
        num = rnd.next(it.W + 1)
        if not f6 and num == it.W - 1: num += rnd.choose(1, -1)
        num2 = 1 + rnd.next(it.H); x = rnd.choose(it.X, it.Right - num); y = 24 - num2
        Set(A, x, y, num, num2); Set(A, x, 0, num)
    for it in D['TopVWrap']:
        x = -1
        for i in range(it.X, it.Right):
            if Check(A, i, 23): x = i; break
        if x != -1:
            num = 1
            while Check(A, x + num, 23) and num < it.W - (x - it.X): num += 1
            num2 = 1 + rnd.next(it.H)
            Set(A, x, 0, num, num2); Set(A, x, 23, num)
    for it in D['Single']:
        if rnd.choose(True, False): Set(A, it.X, it.Y)
    for side in ('LeftPocket', 'RightPocket'):
        for it in D[side]:
            num = rnd.next(it.W + 1)
            x = it.X if side == 'LeftPocket' else it.Right - num
            if num <= 0: continue
            num3 = rnd.next(it.H - 2)
            if num3 > 0: num3 += 2
            num4 = rnd.next(it.H - num3)
            if num3 > 0: num5 = -num + 1 + rnd.next(it.W)
            else:
                num5 = -1 + rnd.next(min(3, it.W - num + 1))
                if num4 == it.H - 1: num4 -= 1
            for i in range(it.H):
                if i < num4: Set(A, x, it.Y + i, num)
                elif i >= num4 + num3:
                    if side == 'LeftPocket': Set(A, x, it.Y + i, num + num5)
                    else: Set(A, x - num5, it.Y + i, num + num5)
    for side in ('LeftWall', 'RightWall'):
        for it in D[side]:
            num6 = rnd.next(it.W + 1); num7 = 0
            for i in range(it.H):
                Set(A, it.X if side == 'LeftWall' else it.Right - num6, it.Y + i, num6)
                num7 += 1
                if (side == 'LeftWall' or num7 >= 2) and not rnd.chance_d(0.9 ** num7):
                    n8 = rnd.next(it.W + 1)
                    if n8 != num6: num6 = n8; num7 = 0
    for side, p in (('Ceiling', 0.9), ('Floor', 0.85)):
        for it in D[side]:
            num9 = rnd.next(it.H + 1); num7 = 0
            for i in range(it.W):
                Set(A, it.X + i, it.Y if side == 'Ceiling' else it.Bottom - num9, 1, num9)
                num7 += 1
                if not rnd.chance_d(p ** num7):
                    n10 = rnd.next(it.H + 1)
                    if n10 != num9: num9 = n10; num7 = 0
    fl = D['Floating']; rnd.shuffle(fl)
    while len(fl) >= 5: fl.pop(0)
    for it in fl:
        n12 = max(1, it.W * it.H // 4) + rnd.choose(0, 1)
        for _ in range(n12):
            num = rnd.next(it.W + 1); num2 = rnd.next(it.H + 1)
            x = it.X + rnd.next(it.W - num); y = it.Y + rnd.next(it.H - num2)
            Set(A, x, y, num, num2)
    for x in range(K(16)):
        for y in range(24):
            if A[x][y]: A[K(31) - x][y] = True
    for x in range(K(32)):
        for y in range(24):
            if base[x][y]: A[x][y] = True
    for y in range(1, 23):
        for x in range(1, K(31)):
            if not A[x][y] and A[x - 1][y] and A[x][y - 1] and A[x][y + 1]: A[x][y] = True
    for y in range(1, 23):
        for x in range(K(30), 0, -1):
            if not A[x][y] and A[x + 1][y] and A[x][y - 1] and A[x][y + 1]: A[x][y] = True
    for x in range(1, K(31)):
        for y in range(1, 23):
            if not A[x][y] and A[x][y - 1] and A[x - 1][y] and A[x + 1][y]: A[x][y] = True
    for x in range(1, K(31)):
        for y in range(22, 0, -1):
            if not A[x][y] and A[x][y + 1] and A[x - 1][y] and A[x + 1][y]: A[x][y] = True
    return A, {k: len(v) for k, v in D.items()}

# ---- LevelRandomItems.AddItems (PC), as far as the solids and the draws go (the entities it adds are recorded as tuples) ----
class Rect:
    def __init__(s, x, y, w, h): s.X, s.Y, s.W, s.H = x, y, w, h
    Right = property(lambda s: s.X + s.W); Bottom = property(lambda s: s.Y + s.H)
    def contains(s, x, y): return s.X <= x < s.Right and s.Y <= y < s.Bottom
    def intersects(s, o): return o.X < s.Right and s.X < o.Right and o.Y < s.Bottom and s.Y < o.Bottom
    def copy(s): return Rect(s.X, s.Y, s.W, s.H)
def coll_pts(rects, x, y): return any(r.contains(x, y) for r in rects)
def coll_rects(rects, rect): return any(r.intersects(rect) for r in rects)
def coll_solids(S, r, ax=0, ay=0):
    for i in range(r.X + ax, r.Right + ax):
        for j in range(r.Y + ay, r.Bottom + ay):
            if 0 <= i < K(32) and 0 <= j < 24 and S[i][j]: return True
    return False

def add_items(S, root, teams, rnd):
    ents = []
    B = [col[:] for col in S]   # BaseSolids
    spawns = []
    for e in root.find('Entities'):
        if (teams and e.tag in ('TeamSpawnA', 'TeamSpawnB')) or (not teams and e.tag == 'PlayerSpawn'):
            spawns.append(Rect(int(e.get('x')) // 10 - 1, int(e.get('y')) // 10, 2, 1))
    list2 = []
    i = 0
    while rnd.chance_d(0.20000000298023224 * 0.8999999761581421 ** i): i += 1
    if i > 0:
        l3 = []
        for j in range(2, K(16) + 1):
            for k in range(2, 24):
                if S[j][k] or S[j - 1][k] or S[j - 1][k - 1] or S[j][k - 1]: continue
                if all(not (S[j][(k + l) % 24] or S[j - 1][(k + l) % 24]) for l in range(1, 8)): l3.append((j, k))
        rnd.shuffle(l3)
        while i > 0 and l3:
            p = l3.pop(0); i -= 1
            list2.append(Rect(p[0] - 1, p[1] - 1, 2, 2))
            l3 = [q for q in l3 if abs(q[0] - p[0]) > 1]
            ents.append(f'el ExplodingOrb x={p[0] * 10} y={p[1] * 10}')
            if p[0] != K(16): ents.append(f'el ExplodingOrb x={(K(32) - p[0]) * 10} y={p[1] * 10}')
    i = 0
    while rnd.chance_d(0.20000000298023224 * 0.5 ** i): i += 1
    if i > 0:
        l5 = []; rc = None
        for k in range(2, 23):
            for j in range(1, K(16)):
                if rc is None:
                    if not S[j][k] and S[j - 1][k] and S[j][k + 1]: rc = Rect(j, k, 1, 1)
                elif S[j][k] and S[j][k + 1]: l5.append(rc); rc = None
                elif S[j][k + 1]: rc.W += 1
                else: rc = None
            if rc is not None: l5.append(rc); rc = None
        l5 = [r for r in l5 if not ((r.Right == K(16) and r.W <= 1) or (r.Right != K(16) and r.W <= 2))]
        rnd.shuffle(l5)
        for m in range(min(len(l5), i)):
            it = l5[m]
            if it.Right == K(16): ents.append(f'el Mud x={it.X * 10} y={it.Y * 10 + 5} width={it.W * 20}'); continue
            ents.append(f'el Mud x={it.X * 10} y={it.Y * 10 + 5} width={it.W * 10}')
            ents.append(f'el Mud x={(K(32) - it.X - it.W) * 10} y={it.Y * 10 + 5} width={it.W * 10}')
    rects = []   # the PC passes this always-empty list to Collide (the spawn list above is never used)
    i = 0
    while rnd.chance_d(0.5 * 0.8500000238418579 ** i): i += 1
    if i > 0:
        l6 = []; rc = None
        for k in range(4, 24):
            for j in range(1, K(16)):
                if S[j][k] and not S[j][k - 1] and not coll_pts(rects, j, k):
                    if rc is not None: rc.W += 1
                    else: rc = Rect(j, k, 1, 1)
                elif rc is not None:
                    if rc.W > 1 or rc.X == K(15): l6.append(rc)
                    rc = None
            if rc is not None:
                if rc.W > 1 or rc.X == K(15): l6.append(rc)
                rc = None
        l7 = []
        for r2 in l6:
            arr = []
            for j in range(r2.W):
                ok = True
                for k in range(-1, -7, -1):
                    if S[r2.X + j][(r2.Y + k + 24) % 24]: ok = False; break
                arr.append(ok)
            f2 = False; it2 = None
            for j in range(len(arr) + 1):
                if j < len(arr) and arr[j]:
                    if f2: it2.W += 1; continue
                    f2 = True; it2 = Rect(r2.X + j, r2.Y, 1, 1)
                elif f2:
                    f2 = False
                    if it2.W > 1 or it2.Right == K(16): l7.append(it2)
        rnd.shuffle(l7)
        for m in range(min(i, len(l7))):
            it = l7[m].copy()
            if it.Right == K(16):
                if it.W > 1: w = it.W; it.W = rnd.next(w) + 1; it.X = K(16) - it.W
            elif it.W > 2:
                w = it.W; it.W = rnd.next(w - 1) + 2; it.X += rnd.next(w - it.W + 1)
            for j in range(it.X, it.Right): S[j][it.Y] = False; S[K(31) - j][it.Y] = False
            if it.Right == K(16): ents.append(f'el JumpPad x={it.X * 10} y={it.Y * 10} width={it.W * 20}'); continue
            ents.append(f'el JumpPad x={it.X * 10} y={it.Y * 10} width={it.W * 10}')
            ents.append(f'el JumpPad x={(K(32) - it.X - it.W) * 10} y={it.Y * 10} width={it.W * 10}')
    i = 0
    while rnd.chance_d(0.800000011920929 * 0.5 ** i): i += 1
    if i > 0:
        l3 = []
        for j in range(2, K(16) + 1):
            for k in range(1, 22):
                n1, n2, n3 = (j + K(31)) % K(32), (j + K(30)) % K(32), (j + K(29)) % K(32)
                if S[j][k] and S[n1][k] and S[j][k + 1] and S[n1][k + 1] and S[j][k - 1] and S[n1][k - 1]:
                    c = sum([not B[n2][k] and not B[n2][k + 1] and not B[n3][k] and not B[n3][k + 1],
                             not B[j + 1][k] and not B[j + 1][k + 1] and not B[j + 2][k] and not B[j + 2][k + 1],
                             not B[j][k + 2] and not B[n1][k + 2]])
                    if c >= 2: l3.append((j, k))
        rnd.shuffle(l3)
        while i > 0 and l3:
            p = l3.pop(0); i -= 1
            l3 = [q for q in l3 if not (abs(q[0] - p[0]) <= 1 and abs(q[1] - p[1]) <= 1)]
            x, y = p
            if x == 0:
                if not teams:
                    for (a, b) in ((0, y), (0, y + 1), (K(31), y), (K(31), y + 1)): S[a][b] = False
                    ents.append(f'el CrumbleWall x={410 if WIDE else 310} y={y * 10}')
            elif x == K(16):
                for (a, b) in ((K(16), y), (K(16), y + 1), (K(15), y), (K(15), y + 1)): S[a][b] = False
                ents.append(f'el CrumbleWall x={x * 10 - 10} y={y * 10}')
            else:
                x3 = K(32) - x
                for (a, b) in ((x, y), (x, y + 1), (x - 1, y), (x - 1, y + 1), (x3, y), (x3, y + 1), (x3 - 1, y), (x3 - 1, y + 1)): S[a][b] = False
                ents.append(f'el CrumbleWall x={x * 10 - 10} y={y * 10}'); ents.append(f'el CrumbleWall x={x3 * 10 - 10} y={y * 10}')
    # (SpikeBall / MovingPlatform: entities only, after the solids are final; their draws are listed for the trace)
    l8 = []
    for k in range(6, 18):
        if not S[K(16)][k] or not S[K(16)][k - 1] or coll_rects(list2, Rect(K(11), k - 5, 10, 10)): continue
        ok = True
        for n in range(6):
            for l in range(-5, 5):
                f5 = n >= 2 or l > 1 or l < -2
                if f5 and n == 5 and (l == 4 or l == -5): f5 = False
                if f5 and B[K(16) + n][k + l]: ok = False; break
            if not ok: break
        if ok: l8.append(k)
    if l8 and rnd.chance_f(0.3):
        rnd.shuffle(l8); n5 = l8[0]
        n6 = 4
        while n6 < 6:
            f6 = True
            for n in range(0, 2 + n6):
                if B[K(16) + n][n5 - 2 - n6] or B[K(16) + n][n5 + 1 + n6]: f6 = False; break
            if f6:
                for l in range(-1 - n6, n6 + 1):
                    if B[K(17) + n6][n5 + l]: f6 = False; break
            if not f6: break
            n6 += 1
        ex = 'True' if rnd.choose(True, False) else 'False'
        sx = 210 if WIDE else 160
        ents.append(f'el SpikeBall x={sx} y={(n5 - n6) * 10} Explodes={ex}'); ents.append(f'child node x={sx} y={n5 * 10}')
        return S, ents
    l9 = []; r3 = Rect(7, 8, 9, 6)
    while r3.Bottom < 22:
        rr = Rect(r3.X - 2, r3.Y - 3, r3.W + 4, r3.H + 3)
        if not coll_solids(B, rr) and not coll_rects(list2, rr): l9.append(r3.copy())
        r3.Y += 1
    l10 = []; r3 = Rect(12, 1, 4, 10)
    while r3.Bottom < 22:
        rr = Rect(r3.X - 2, r3.Y, r3.W + 4, r3.H)
        if not coll_solids(B, rr) and not coll_rects(list2, rr): l10.append(r3.copy())
        r3.Y += 1
    if (not l9 and not l10) or not rnd.chance_f(0.35): return S, ents
    f7 = rnd.choose(len(l9) > 0, len(l10) == 0)
    if l10: f7 = False
    if f7:
        rnd.shuffle(l9); r4 = l9[0]
        w = 3 + rnd.next(3); h = 2 + rnd.next(2); x = r4.X + rnd.next(3); y = r4.Y + rnd.next(7 - h)
        px = K(32) - x - w
        ents.append(f'el MovingPlatform x={x * 10} y={y * 10} width={w * 10} height={h * 10}'); ents.append(f'child node x={px * 10} y={y * 10}')
        ents.append(f'mp {x} {y} {px + w} {h}'); return S, ents
    rnd.shuffle(l10); r4 = l10[0]
    w = (1 + rnd.next(4)) * 2; h = 2 + rnd.next(2); x = K(16) - w // 2; y = r4.Y + rnd.next(2)
    py = r4.Bottom - h - rnd.next(2)
    e = Rect(x, y, w, h)
    if coll_solids(B, e, 0, -2) and not coll_solids(B, e, 0, -1): e.Y -= 1
    r2 = e.copy(); r2.Y = py
    if coll_solids(B, r2, 0, 2) and not coll_solids(B, r2, 0, 1): py += 1
    if rnd.choose(True, False): e.Y, py = py, e.Y
    ents.append(f'el MovingPlatform x={e.X * 10} y={e.Y * 10} width={e.W * 10} height={e.H * 10}'); ents.append(f'child node x={e.X * 10} y={py * 10}')
    ents.append(f'mp {e.X} {e.Y} {e.W} {py + e.H}')
    return S, ents

def tohex(A):
    flat = [A[x][y] for x in range(W()) for y in range(24)]
    return ''.join('0123456789abcdef'[flat[i] * 8 + flat[i + 1] * 4 + flat[i + 2] * 2 + flat[i + 3]] for i in range(0, W() * 24, 4))

def load(path):
    root = ET.parse(path).getroot()
    base = bitdata(root.find('Solids').text.replace('\r', ''), W())
    rects = []
    for e in root.find('Entities').iter('RandomBlock'):
        x, y = float(e.get('x')), float(e.get('y'))
        rects.append(R(int(x / 10), int(y / 10), int(e.get('width')) // 10, int(e.get('height')) // 10))
    return rects, base

if __name__ == '__main__':
    WIDE = '--wide' in sys.argv
    rects, base = load(sys.argv[1])
    rnd = NetRandom(int(sys.argv[2]))
    if '--input' in sys.argv:   # input for tools/test-levelgen: the seed, the RandomBlock rectangles, the level's solids (before GenerateData)
        print('seed', sys.argv[2], file=sys.stderr)
        if WIDE: print('wide 1', file=sys.stderr)
        for r in rects: print('rect', r.X, r.Y, r.W, r.H, file=sys.stderr)
        print('base', tohex(base), file=sys.stderr)
    A, kinds = generate(rects, base, rnd)
    if '--state' in sys.argv:   # input for tools/test-levelitems: the Random state and the grid after GenerateData
        print('state', rnd.inext, rnd.inextp, ' '.join(map(str, rnd.sa)), file=sys.stderr)
        print('grid', tohex(A), file=sys.stderr)
    if '--items' in sys.argv:   # then LevelRandomItems.AddItems (teams = False), as LevelLoaderXML does next
        ngen = rnd.draws
        A, ents = add_items(A, ET.parse(sys.argv[1]).getroot(), False, rnd)
        print('generate draws', ngen, 'additems draws', rnd.draws - ngen, file=sys.stderr)
        for e in ents: print(e, file=sys.stderr)
        print('final', tohex(A), file=sys.stderr)
    h = tohex(A)
    print(h); print('blocks', {k: v for k, v in kinds.items() if v}, 'draws', rnd.draws)
    if '--trace' in sys.argv:
        for i, (k, a, r) in enumerate(rnd.log, 1): print(f"draw {i} {k} {a} -> {r}")
    if len(sys.argv) > 3 and not sys.argv[3].startswith('--'):
        ref = sys.argv[3]
        diff = sum(1 for a, b in zip(h, ref) if a != b)
        print('MATCH' if h == ref else f'DIFFERENT ({diff} of 192 hex digits)')
