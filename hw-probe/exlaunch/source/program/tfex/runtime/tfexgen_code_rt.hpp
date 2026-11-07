/* Runtime support for TF.EX code translated to C++ (tools/tfex/translator CodeGen; copied into tfex/gen/, not edited there).
 * C# on the BRUTE game: game objects are `void*` (managed objects: obj[0] = descriptor), game members are reached by offsets the
 * binder resolved (tfexgen_binding.hpp), FNA value types are PODs with FNA's arithmetic, strings are managed System.String objects
 * (literals: static ones outside the GC heap; built strings: the game's allocator). The platform layer is tfex/Game.hpp. */
#pragma once
#include <type_traits>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <cstdio>
#include <new>
#include <coroutine>
#include "../Game.hpp"
#include "tfexgen_rt.hpp"

/* what the translator couldn't translate (tfexgen_report.txt): a build of such a unit fails here */
namespace tfex::gen { struct Any_ { template <class T> operator T() const { return T{}; } template <class T> const Any_& operator=(const T&) const { return *this; } template <class F> bool operator()(F&&) const { return true; } }; }
/* TF.State's state path (the rollback measurement): a construct that doesn't translate yet is skipped (a default value), and its first
 * hits are logged ("[state] gap ...") with a count, so the measurement says what ran without it */
namespace tfex::gen { void StateGap(const char* fn, int line); }
#define TFEXGEN_STATE_GAP (tfex::gen::StateGap(__func__, __LINE__))
#define TFEXGEN_STATE_GAP_EXPR(...) (tfex::gen::StateGap(__func__, __LINE__), tfex::gen::Any_{})
#ifdef TFEXGEN_COVERAGE   /* tools/tfex/translator/test/compile-gen.sh: compile what translated, around what didn't */
#define TFEXGEN_UNSUPPORTED ((void)0)
#define TFEXGEN_UNSUPPORTED_EXPR(...) (tfex::gen::Any_{})
#else
#define TFEXGEN_UNSUPPORTED static_assert(false, "TF.EX construct the translator can't translate yet: see tfexgen_report.txt")
#define TFEXGEN_UNSUPPORTED_EXPR(...) ([]() -> int { static_assert(false, "TF.EX construct the translator can't translate yet: see tfexgen_report.txt"); return 0; }())
#endif

namespace tfex::gen {
using game::Base;

/* C#'s int[,] (rectangular, numbers only): a local value owning its cells, zeroed as .NET's (FortRise's LevenshteinDistance). No managed
 * references in it: nothing for the GC to scan. Moves, never copies. */
template <class T> struct Grid {
    /* a C# T[,] (rectangular, numbers / bools): a reference, as .NET's. One it makes (new T[r, c]) owns its cells, zeroed as .NET's
     * (FortRise's LevenshteinDistance); a copy is the same array (an alias, never freed by it); a game array (LevelTiles.bitData) is viewed
     * in place (Of). */
    int32_t rows = 0, cols = 0; T* data = nullptr; bool owned = false;
    void* obj = nullptr;   /* the game's array object (Of): what a game field holds (written back by FortRise's patch_LevelTiles.Replace) */
    Grid() = default;
    Grid(int32_t r, int32_t c) : rows(r), cols(c), data(new T[(size_t)r * (size_t)c]()), owned(true) {}
    Grid(const Grid& o) : rows(o.rows), cols(o.cols), data(o.data), owned(false), obj(o.obj) {}
    Grid(Grid&& o) : rows(o.rows), cols(o.cols), data(o.data), owned(o.owned), obj(o.obj) { o.owned = false; }
    Grid& operator=(const Grid& o) { if (this != &o) { if (owned) delete[] data; rows = o.rows; cols = o.cols; data = o.data; owned = false; obj = o.obj; } return *this; }
    Grid& operator=(Grid&& o) { if (this != &o) { if (owned) delete[] data; rows = o.rows; cols = o.cols; data = o.data; owned = o.owned; obj = o.obj; o.owned = false; } return *this; }
    ~Grid() { if (owned) delete[] data; }
    T& operator()(int32_t r, int32_t c) const { return data[(size_t)r * (size_t)cols + (size_t)c]; }
    int32_t GetLength(int32_t d) const { return d == 0 ? rows : cols; }
    bool operator==(std::nullptr_t) const { return !data; }
    bool operator!=(std::nullptr_t) const { return data != nullptr; }
    /* a game T[,] in place: the Switch runtime's layout, as the game's own element access reads it (Monocle.Grid's indexer, +0x90bdd0):
     * the cells' pointer at +0x10, the bounds at +0x20 (dimension 0's length, then dimension 1's), [r, c] at cells + r * cols + c */
    static Grid Of(void* game) {
        Grid g;
        if (!game) return g;
        auto* o = static_cast<uint8_t*>(game);
        const int32_t* b = *reinterpret_cast<const int32_t* const*>(o + 0x20);
        g.obj = game;
        g.data = *reinterpret_cast<T* const*>(o + 0x10);
        g.rows = b ? b[0] : 0; g.cols = b ? b[1] : 0;
        return g;
    }
};

/* ---- calls and fields by offset ---- */
template <class F> inline F Fn(uintptr_t off) { return reinterpret_cast<F>(Base() + off); }
/* (tfexgen_binding.hpp) an unresolved game field TF.State's state path uses (the rollback measurement): 0x7fff0000 + its number; reads and
 * writes go to a dummy and are logged as state gaps. The test folds away for every resolved field (a constant offset). */
constexpr uint32_t kGapField = 0x7fff0000u;
void StateGap(const char* fn, int line);
/* a reference field's stand-in is a zeroed scratch object (not null): its own fields read as zero and a game method may write into it */
extern uint8_t g_gapObject[4096];
template <class T> inline T& GapField(uint32_t n) {
    static T dummy{};
    StateGap("an unresolved game field", (int)n);
    if constexpr (std::is_pointer_v<T>) { __builtin_memset(g_gapObject, 0, sizeof g_gapObject); dummy = reinterpret_cast<T>(g_gapObject); }
    else dummy = T{};
    return dummy;
}
template <class T> inline T& Fld(const void* obj, uint32_t off) {
    if (__builtin_expect((off & 0xffff0000u) == kGapField, 0)) return GapField<T>(off & 0xffffu);
    return *reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(obj) + off);
}
/* a game static at its storage address (rtypes.py's static field records: the value lives there; Game.hpp's hand-found "static
 * slots" are a different thing, a pointer to the storage) */
/* game enums: the PC's numbers <-> the Switch's (UnitEmitter EnumConvert); a value not listed passes unchanged */
template <class T, size_t N> inline T EnumMap(T v, const long long (&from)[N], const long long (&to)[N]) {
    for (size_t i = 0; i < N; i++) if (from[i] == (long long)v) return (T)to[i];
    return v;
}
template <class T, size_t N> inline bool EnumIn(T v, const long long (&vals)[N]) {
    for (size_t i = 0; i < N; i++) if (vals[i] == (long long)v) return true;
    return false;
}
template <class T> inline T& Static(uintptr_t storage) {
    if (__builtin_expect((storage & 0xffff0000u) == kGapField && storage < 0x80000000u, 0)) return GapField<T>(storage & 0xffffu);   /* an unresolved static (TF.State measurement) */
    return *reinterpret_cast<T*>(Base() + storage);
}
template <class R, class... A> inline R Virt(void* obj, uint32_t slot, A... a) { return game::Virt<R>(obj, slot, a...); }
/* ldvirtftn: the function a virtual's vtable slot holds for obj (a delegate over obj.M runs obj's override); null obj: null */
inline void* VirtFn(void* obj, uint32_t slot) {
    if (!obj) return nullptr;
    uintptr_t desc = *reinterpret_cast<uintptr_t*>(obj);
    return reinterpret_cast<void*>((*reinterpret_cast<uintptr_t**>(desc + 8))[slot / 8] & 0x7fffffffffffffffull);
}
/* BRUTE checks beforefieldinit at call sites: run a game type's class constructor before its statics (rtypes "cctor") */
inline void Cctor(uintptr_t runner) { if (runner) Fn<void (*)()>(runner)(); }
inline void* TypeOf(uintptr_t getter) { return Fn<void* (*)()>(getter)(); }
/* typeof(T) as a value: the type record's System.Type object. Type.GetTypeFromHandle (+0x401f70 -> +0x1fb0) returns the one the record caches
 * at +0x160, making it on first use; making it from our code traps (2026-10-22), so only the cached one: null when the game has never made
 * typeof(T), and then no game collection can hold it as a key either (Monocle.Cache's pools) */
inline void* TypeObject(void* handle) { return handle ? *reinterpret_cast<void* volatile*>(reinterpret_cast<uintptr_t>(handle) + 0x160) : nullptr; }
/* C# `obj as T` / `obj is T` (the game's IsInst) */
/* a null type: a TF.EX class whose runtime type isn't built yet (Auto_Fader::RuntimeType() before the first Fader), so no object is one */
/* The answer depends only on the object's runtime type (obj[0][0]) and the target type, and runtime types are never freed (uncollectable),
 * so it is cached per pair: a miss in IsInst walks the base-type chain through calls, and TF.State's save / load test every entity against
 * ~70 types (its GetAll<T> / DeleteAll<T> scans: a third of a save on the console, 2026-10-24 profile). One 64-bit word per entry (a key
 * mixed from both pointers, the answer in bit 0), written and read whole, so another thread's lookup sees an old or a new entry, never half. */
inline uintptr_t RuntimeTypeOf(void* obj) { return obj ? **reinterpret_cast<uintptr_t* const*>(obj) : 0; }   /* obj.GetType()'s runtime type (descriptor word 0) */
extern uint64_t g_asCache[4096];
/* a mod's interop interface (translate.toml [[interop]], FortRise's IModInterop.GetApi<T>): the object GetApi gave is a T by FortRise's
 * contract (its proxy), and on the Switch it may be a service token rather than a managed object, so a test against it passes the value
 * through without reading a type header */
inline void* const kInteropType = reinterpret_cast<void*>(uintptr_t(1));
inline void* As(void* obj, void* type) {
    if (type == kInteropType) return obj;
    if (!obj || !type) return nullptr;
    uintptr_t src = **reinterpret_cast<uintptr_t* const*>(obj);
    if (src == reinterpret_cast<uintptr_t>(type)) return obj;
    uint64_t key = (src ^ (reinterpret_cast<uintptr_t>(type) * 0x9e3779b97f4a7c15ull)) & ~1ull;
    uint64_t& e = g_asCache[(key >> 4 ^ key >> 20) & 4095];
    uint64_t v = __atomic_load_n(&e, __ATOMIC_RELAXED);
    if ((v & ~1ull) == key && v) return (v & 1) ? obj : nullptr;
    void* r = Fn<void* (*)(void*, void*)>(game::brute::IsInst)(obj, type);
    __atomic_store_n(&e, key | (r ? 1 : 0), __ATOMIC_RELAXED);
    return r;
}

/* List<T>.RemoveAll(match) on a game List<T> of references, as .NET does it (the first match, then the rest compacted in order, the tail cleared,
 * _size set, _version bumped when anything was removed; the count returned), with the predicate inline: no Predicate delegate or closure is
 * allocated (the translator's lowering of RemoveAll(lambda)). The list's fields: _items +0x10 (its elements at the array's data), _size +0x18,
 * _version +0x1c. */
template <class P> int32_t GameListRemoveAll(void* list, P&& match) {
    if (!list) return 0;
    int32_t size = Fld<int32_t>(list, 0x18);
    if (size <= 0) return 0;
    void** d = *reinterpret_cast<void***>(Fld<uintptr_t>(list, 0x10) + 0x10);
    int32_t free = 0;
    while (free < size && !match(d[free])) free++;
    if (free >= size) return 0;
    for (int32_t cur = free + 1; cur < size; cur++) if (!match(d[cur])) d[free++] = d[cur];
    for (int32_t k = free; k < size; k++) d[k] = nullptr;
    Fld<int32_t>(list, 0x18) = free;
    Fld<int32_t>(list, 0x1c) += 1;
    return size - free;
}

/* DateTime.TryParseExact(s, format, CultureInfo.InvariantCulture, DateTimeStyles.None, out d) for fixed numeric formats: yyyy, MM, dd, HH,
 * mm, ss and literal characters, the whole string matched; d = the DateTime's ticks (0001-01-01 = 0), DateTime.MinValue (0) when it fails */
bool DateTryParseExact(const void* s, const void* format, int64_t* out);

/* ---- FNA value types (Microsoft.Xna.Framework), same layout and arithmetic as FNA ---- */
struct Vector2 {
    float X, Y;
    friend Vector2 operator+(Vector2 a, Vector2 b) { return {a.X + b.X, a.Y + b.Y}; }
    friend Vector2 operator-(Vector2 a, Vector2 b) { return {a.X - b.X, a.Y - b.Y}; }
    friend Vector2 operator-(Vector2 a) { return {-a.X, -a.Y}; }
    friend Vector2 operator*(Vector2 a, float s) { return {a.X * s, a.Y * s}; }
    friend Vector2 operator*(float s, Vector2 a) { return {a.X * s, a.Y * s}; }
    friend Vector2 operator*(Vector2 a, Vector2 b) { return {a.X * b.X, a.Y * b.Y}; }
    Vector2& operator+=(Vector2 b) { X += b.X; Y += b.Y; return *this; }
    Vector2& operator-=(Vector2 b) { X -= b.X; Y -= b.Y; return *this; }
    friend Vector2 operator/(Vector2 a, float s) { float f = 1.0f / s; return {a.X * f, a.Y * f}; }
    friend Vector2 operator/(Vector2 a, Vector2 b) { return {a.X / b.X, a.Y / b.Y}; }
    friend bool operator==(Vector2 a, Vector2 b) { return a.X == b.X && a.Y == b.Y; }
    friend bool operator!=(Vector2 a, Vector2 b) { return !(a == b); }
    static Vector2 Lerp(Vector2 a, Vector2 b, float t) { return {a.X + (b.X - a.X) * t, a.Y + (b.Y - a.Y) * t}; }
    static Vector2 Normalize(Vector2 v) { float f = 1.0f / __builtin_sqrtf(v.X * v.X + v.Y * v.Y); return {v.X * f, v.Y * f}; }   /* FNA's (no zero check: NaN, as the PC's) */
    static float Dot(Vector2 a, Vector2 b) { return a.X * b.X + a.Y * b.Y; }   /* FNA's */
    float Length() const { return std::sqrt(X * X + Y * Y); }
    float LengthSquared() const { return X * X + Y * Y; }
};
inline constexpr Vector2 Vector2_Zero{0, 0}, Vector2_One{1, 1}, Vector2_UnitX{1, 0}, Vector2_UnitY{0, 1};

struct Color {
    uint32_t packed;   /* R | G << 8 | B << 16 | A << 24 */
    static Color Make(int r, int g, int b, int a) {
        if (((r | g | b | a) & 0xFFFFFF00) != 0) {   /* FNA clamps out-of-range components */
            r = r < 0 ? 0 : r > 255 ? 255 : r; g = g < 0 ? 0 : g > 255 ? 255 : g;
            b = b < 0 ? 0 : b > 255 ? 255 : b; a = a < 0 ? 0 : a > 255 ? 255 : a;
        }
        return {(uint32_t)r | (uint32_t)g << 8 | (uint32_t)b << 16 | (uint32_t)a << 24};
    }
    static Color Make(float r, float g, float b, float a) { return Make((int)(r * 255), (int)(g * 255), (int)(b * 255), (int)(a * 255)); }
    uint8_t R() const { return packed & 0xff; }
    uint8_t G() const { return packed >> 8 & 0xff; }
    uint8_t B() const { return packed >> 16 & 0xff; }
    uint8_t A() const { return packed >> 24; }
    friend Color operator*(Color c, float s) { return Make((int)(c.R() * s), (int)(c.G() * s), (int)(c.B() * s), (int)(c.A() * s)); }
    friend bool operator==(Color a, Color b) { return a.packed == b.packed; }
    friend bool operator!=(Color a, Color b) { return a.packed != b.packed; }
};
inline constexpr Color Color_White{0xffffffffu}, Color_Black{0xff000000u}, Color_Transparent{0u};
/* FNA's named colors (Microsoft.Xna.Framework.Color, all of them; packed R | G << 8 | B << 16 | A << 24) */
constexpr Color ColorRGB(uint32_t r, uint32_t g, uint32_t b) { return {r | g << 8 | b << 16 | 0xff000000u}; }
inline constexpr Color Color_LightGray = ColorRGB(211, 211, 211), Color_Gray = ColorRGB(128, 128, 128), Color_Red = ColorRGB(255, 0, 0),
                       Color_Yellow = ColorRGB(255, 255, 0), Color_Gold = ColorRGB(255, 215, 0), Color_WhiteSmoke = ColorRGB(245, 245, 245),
                       Color_CornflowerBlue = ColorRGB(100, 149, 237), Color_LightGreen = ColorRGB(144, 238, 144),
                       Color_IndianRed = ColorRGB(205, 92, 92), Color_Green = ColorRGB(0, 128, 0), Color_Blue = ColorRGB(0, 0, 255),
                       Color_Orange = ColorRGB(255, 165, 0), Color_DarkGray = ColorRGB(169, 169, 169),
                       Color_AliceBlue = ColorRGB(240, 248, 255), Color_AntiqueWhite = ColorRGB(250, 235, 215), Color_Aqua = ColorRGB(0, 255, 255), Color_Aquamarine = ColorRGB(127, 255, 212),
                       Color_Azure = ColorRGB(240, 255, 255), Color_Beige = ColorRGB(245, 245, 220), Color_Bisque = ColorRGB(255, 228, 196), Color_BlanchedAlmond = ColorRGB(255, 235, 205),
                       Color_BlueViolet = ColorRGB(138, 43, 226), Color_Brown = ColorRGB(165, 42, 42), Color_BurlyWood = ColorRGB(222, 184, 135), Color_CadetBlue = ColorRGB(95, 158, 160),
                       Color_Chartreuse = ColorRGB(127, 255, 0), Color_Chocolate = ColorRGB(210, 105, 30), Color_Coral = ColorRGB(255, 127, 80), Color_Cornsilk = ColorRGB(255, 248, 220),
                       Color_Crimson = ColorRGB(220, 20, 60), Color_Cyan = ColorRGB(0, 255, 255), Color_DarkBlue = ColorRGB(0, 0, 139), Color_DarkCyan = ColorRGB(0, 139, 139),
                       Color_DarkGoldenrod = ColorRGB(184, 134, 11), Color_DarkGreen = ColorRGB(0, 100, 0), Color_DarkKhaki = ColorRGB(189, 183, 107), Color_DarkMagenta = ColorRGB(139, 0, 139),
                       Color_DarkOliveGreen = ColorRGB(85, 107, 47), Color_DarkOrange = ColorRGB(255, 140, 0), Color_DarkOrchid = ColorRGB(153, 50, 204), Color_DarkRed = ColorRGB(139, 0, 0),
                       Color_DarkSalmon = ColorRGB(233, 150, 122), Color_DarkSeaGreen = ColorRGB(143, 188, 139), Color_DarkSlateBlue = ColorRGB(72, 61, 139), Color_DarkSlateGray = ColorRGB(47, 79, 79),
                       Color_DarkTurquoise = ColorRGB(0, 206, 209), Color_DarkViolet = ColorRGB(148, 0, 211), Color_DeepPink = ColorRGB(255, 20, 147), Color_DeepSkyBlue = ColorRGB(0, 191, 255),
                       Color_DimGray = ColorRGB(105, 105, 105), Color_DodgerBlue = ColorRGB(30, 144, 255), Color_Firebrick = ColorRGB(178, 34, 34), Color_FloralWhite = ColorRGB(255, 250, 240),
                       Color_ForestGreen = ColorRGB(34, 139, 34), Color_Fuchsia = ColorRGB(255, 0, 255), Color_Gainsboro = ColorRGB(220, 220, 220), Color_GhostWhite = ColorRGB(248, 248, 255),
                       Color_Goldenrod = ColorRGB(218, 165, 32), Color_GreenYellow = ColorRGB(173, 255, 47), Color_Honeydew = ColorRGB(240, 255, 240), Color_HotPink = ColorRGB(255, 105, 180),
                       Color_Indigo = ColorRGB(75, 0, 130), Color_Ivory = ColorRGB(255, 255, 240), Color_Khaki = ColorRGB(240, 230, 140), Color_Lavender = ColorRGB(230, 230, 250),
                       Color_LavenderBlush = ColorRGB(255, 240, 245), Color_LawnGreen = ColorRGB(124, 252, 0), Color_LemonChiffon = ColorRGB(255, 250, 205), Color_LightBlue = ColorRGB(173, 216, 230),
                       Color_LightCoral = ColorRGB(240, 128, 128), Color_LightCyan = ColorRGB(224, 255, 255), Color_LightGoldenrodYellow = ColorRGB(250, 250, 210), Color_LightPink = ColorRGB(255, 182, 193),
                       Color_LightSalmon = ColorRGB(255, 160, 122), Color_LightSeaGreen = ColorRGB(32, 178, 170), Color_LightSkyBlue = ColorRGB(135, 206, 250), Color_LightSlateGray = ColorRGB(119, 136, 153),
                       Color_LightSteelBlue = ColorRGB(176, 196, 222), Color_LightYellow = ColorRGB(255, 255, 224), Color_Lime = ColorRGB(0, 255, 0), Color_LimeGreen = ColorRGB(50, 205, 50),
                       Color_Linen = ColorRGB(250, 240, 230), Color_Magenta = ColorRGB(255, 0, 255), Color_Maroon = ColorRGB(128, 0, 0), Color_MediumAquamarine = ColorRGB(102, 205, 170),
                       Color_MediumBlue = ColorRGB(0, 0, 205), Color_MediumOrchid = ColorRGB(186, 85, 211), Color_MediumPurple = ColorRGB(147, 112, 219), Color_MediumSeaGreen = ColorRGB(60, 179, 113),
                       Color_MediumSlateBlue = ColorRGB(123, 104, 238), Color_MediumSpringGreen = ColorRGB(0, 250, 154), Color_MediumTurquoise = ColorRGB(72, 209, 204), Color_MediumVioletRed = ColorRGB(199, 21, 133),
                       Color_MidnightBlue = ColorRGB(25, 25, 112), Color_MintCream = ColorRGB(245, 255, 250), Color_MistyRose = ColorRGB(255, 228, 225), Color_Moccasin = ColorRGB(255, 228, 181),
                       Color_NavajoWhite = ColorRGB(255, 222, 173), Color_Navy = ColorRGB(0, 0, 128), Color_OldLace = ColorRGB(253, 245, 230), Color_Olive = ColorRGB(128, 128, 0),
                       Color_OliveDrab = ColorRGB(107, 142, 35), Color_OrangeRed = ColorRGB(255, 69, 0), Color_Orchid = ColorRGB(218, 112, 214), Color_PaleGoldenrod = ColorRGB(238, 232, 170),
                       Color_PaleGreen = ColorRGB(152, 251, 152), Color_PaleTurquoise = ColorRGB(175, 238, 238), Color_PaleVioletRed = ColorRGB(219, 112, 147), Color_PapayaWhip = ColorRGB(255, 239, 213),
                       Color_PeachPuff = ColorRGB(255, 218, 185), Color_Peru = ColorRGB(205, 133, 63), Color_Pink = ColorRGB(255, 192, 203), Color_Plum = ColorRGB(221, 160, 221),
                       Color_PowderBlue = ColorRGB(176, 224, 230), Color_Purple = ColorRGB(128, 0, 128), Color_RosyBrown = ColorRGB(188, 143, 143), Color_RoyalBlue = ColorRGB(65, 105, 225),
                       Color_SaddleBrown = ColorRGB(139, 69, 19), Color_Salmon = ColorRGB(250, 128, 114), Color_SandyBrown = ColorRGB(244, 164, 96), Color_SeaGreen = ColorRGB(46, 139, 87),
                       Color_SeaShell = ColorRGB(255, 245, 238), Color_Sienna = ColorRGB(160, 82, 45), Color_Silver = ColorRGB(192, 192, 192), Color_SkyBlue = ColorRGB(135, 206, 235),
                       Color_SlateBlue = ColorRGB(106, 90, 205), Color_SlateGray = ColorRGB(112, 128, 144), Color_Snow = ColorRGB(255, 250, 250), Color_SpringGreen = ColorRGB(0, 255, 127),
                       Color_SteelBlue = ColorRGB(70, 130, 180), Color_Tan = ColorRGB(210, 180, 140), Color_Teal = ColorRGB(0, 128, 128), Color_Thistle = ColorRGB(216, 191, 216),
                       Color_Tomato = ColorRGB(255, 99, 71), Color_Turquoise = ColorRGB(64, 224, 208), Color_Violet = ColorRGB(238, 130, 238), Color_Wheat = ColorRGB(245, 222, 179),
                       Color_YellowGreen = ColorRGB(154, 205, 50);   /* (the rest of XNA's named colors: any mod's) */
/* FNA MathHelper / Color.Lerp */
inline float MathHelper_Lerp(float a, float b, float t) { return a + (b - a) * t; }
inline float MathHelper_Clamp(float v, float lo, float hi) { v = v > hi ? hi : v; return v < lo ? lo : v; }
inline Color Color_Lerp(Color a, Color b, float t) {
    return Color::Make((int)MathHelper_Lerp(a.R(), b.R(), t), (int)MathHelper_Lerp(a.G(), b.G(), t), (int)MathHelper_Lerp(a.B(), b.B(), t), (int)MathHelper_Lerp(a.A(), b.A(), t));
}
template <class T> inline T Clamp(T v, T lo, T hi) { return v < lo ? lo : v > hi ? hi : v; }   /* Math.Clamp (C# throws if lo > hi) */

struct Rectangle {
    int32_t X, Y, Width, Height;
    bool Intersects(const Rectangle& v) const { return v.X < X + Width && X < v.X + v.Width && v.Y < Y + Height && Y < v.Y + v.Height; }   /* FNA's */
    bool Contains(const Rectangle& v) const { return X <= v.X && v.X + v.Width <= X + Width && Y <= v.Y && v.Y + v.Height <= Y + Height; }   /* FNA's */
    bool Contains(int32_t x, int32_t y) const { return X <= x && x < X + Width && Y <= y && y < Y + Height; }
    int32_t Left() const { return X; } int32_t Right() const { return X + Width; } int32_t Top() const { return Y; } int32_t Bottom() const { return Y + Height; }   /* FNA's */
    bool operator==(const Rectangle& o) const { return X == o.X && Y == o.Y && Width == o.Width && Height == o.Height; }   /* FNA's */
    bool operator!=(const Rectangle& o) const { return !(*this == o); }
};
struct Matrix { float M11, M12, M13, M14, M21, M22, M23, M24, M31, M32, M33, M34, M41, M42, M43, M44; };   /* passed by reference (> 16 bytes), as the game's */
inline constexpr Matrix Matrix_Identity{ 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
/* FNA's Matrix.Lerp: each element m1 + (m2 - m1) * amount, in that order (bit-exact with the PC's) */
inline Matrix Matrix_Lerp(const Matrix& a, const Matrix& b, float t) {
    const float* x = &a.M11; const float* y = &b.M11; Matrix r; float* o = &r.M11;
    for (int i = 0; i < 16; i++) o[i] = x[i] + ((y[i] - x[i]) * t);
    return r;
}
struct Point { int32_t X, Y; bool operator==(const Point& o) const { return X == o.X && Y == o.Y; } bool operator!=(const Point& o) const { return !(*this == o); } };   /* FNA's equality */
/* TowerFall.PlayerInventory on the Switch 1.4.1.6 (its constructor +0xb05a80): Arrows (an ArrowList) at 0, Shield 8, Wings 9, SpeedBoots 10,
 * Invisible 11, 16 bytes: by value it is two x registers (Player..ctor x4/x5; Session.GetPlayerInventory returns it in x0/x1), as AAPCS64
 * passes this struct */
struct PlayerInventory { void* Arrows; bool Shield, Wings, SpeedBoots, Invisible; uint8_t pad_[4]; };
static_assert(sizeof(PlayerInventory) == 16, "PlayerInventory: the Switch's 16 bytes");

/* ---- strings ---- */
/* a C# string literal: a System.String object in our module (immutable; Boehm ignores memory it doesn't own). The class word is
 * the game's, known only at run time: P() fills it on first use. */
struct alignas(16) Lit {
    uintptr_t klass = 0;
    uint64_t zero = 0;
    int32_t len;
    char16_t ch[1];   /* (the generated literal has the right size: LitN<N>) */
    void* P() { if (!klass) klass = Base() + game::brute::Desc_String; return this; }
};
template <int N> struct alignas(16) LitN {
    uintptr_t klass = 0;
    uint64_t zero = 0;
    int32_t len = N - 1;
    char16_t ch[N];
    constexpr LitN(const char16_t (&s)[N]) : ch{} { for (int i = 0; i < N; i++) ch[i] = s[i]; }
    void* P() { if (!klass) klass = Base() + game::brute::Desc_String; return this; }
};
inline int32_t Len(const void* s) { return s ? Fld<int32_t>(s, 0x10) : 0; }
inline const char16_t* Chars(const void* s) { return reinterpret_cast<const char16_t*>(reinterpret_cast<uintptr_t>(s) + 0x14); }
inline char16_t CharAt(const void* s, int i) { return Chars(s)[i]; }
bool StrEq(const void* a, const void* b);                 /* string == string (ordinal) */
bool StrEqI(const void* a, const void* b);                /* ordinal, ignoring ASCII case */
void* Upper(const void* s);                               /* ToUpper/ToUpperInvariant (ASCII letters) */
void* Lower(const void* s);
void* NewString(int32_t len);                             /* the game's allocator (String.InternalAllocateStr), chars to fill */
void* Concat(const void* a, const void* b);               /* a + b (null = "") */
void* Concat(const void* a, const void* b, const void* c);
void* Repeat(char16_t c, int32_t n);                      /* new string(c, n) */
void* FromInt(int64_t v);                                 /* v.ToString() (invariant) */
void* FromIntPad(int64_t v, int width);
/* {v:X2} / {v:x}: hexadecimal with at least `width` digits (C#'s X / x format) */
void* FromIntHex(uint64_t v, int width, bool upper);
void* TypeNameStr(void* obj);                             /* obj.GetType().Name: its runtime type record's short name (+0x20), null for null */                   /* v.ToString("00..."): at least width digits, zeros in front */
/* obj.ToString() through object (C#'s virtual call): the object's own ToString, Object.ToString's vtable slot (+0x18: tools/re vtable.py,
 * every class's); a boxed number gives its digits, a string itself (a direct call of Object.ToString gives the type's name) */
inline void* ObjToString(void* obj) { return obj ? game::Virt<void*>(obj, 0x18) : nullptr; }
void* FromDouble(double v);                               /* double / float .ToString() (invariant, round-trip) */
void* FromAscii(const char* s);
bool IsNullOrEmpty(const void* s);

/* ---- arrays TF.EX keeps to itself (never handed to game code) ---- */
/* a game struct held by value (TowerFall.InputState): its bytes, the Switch's size; fields through bind:: offsets (gen::Fld on its
 * address: the conversion runs on the object itself, so a write lands in it) */
template <int N> struct alignas(4) GameVal { uint8_t b[N]; operator void*() { return b; } };
template <class T> struct Arr {
    T* data;
    int32_t len;
    void* obj = nullptr;   /* the managed array, when it came from the game (handed back as is) */
    T& operator[](int i) const { return data[i]; }
    int32_t Length() const { return len; }
    bool operator==(std::nullptr_t) const { return !data && !obj; }   /* C#'s `arr == null` */
    explicit operator bool() const { return data || obj; }   /* arr?.X: a conditional access */
    bool operator!=(std::nullptr_t) const { return data || obj; }
    /* C#'s reference equality of two arrays (v0.29.0's ConfirmDialog: MenuInput.MenuInputs == _blockedInputs): the same managed array, or
     * (TF.EX's own, no managed array) the same elements */
    friend bool operator==(const Arr& a, const Arr& b) { return (a.obj || b.obj) ? a.obj == b.obj : a.data == b.data; }
    friend bool operator!=(const Arr& a, const Arr& b) { return !(a == b); }
};
/* the managed object of a value a game method takes as its receiver: an array wrapper's (BitConverter.GetBytes(x).CopyTo(...)), a
 * reference as is */
template <class T> inline void* ObjOf(const Arr<T>& a) { return a.obj; }
inline void* ObjOf(void* p) { return p; }

/* new string(char[]) */
inline void* StrFromArr(const Arr<char16_t>& a) {
    void* r = NewString(a.len);
    if (r && a.len) std::memcpy(const_cast<char16_t*>(Chars(r)), a.data, (size_t)a.len * 2);
    return r;
}
/* new string(chars) from n UTF-16 units (new string([.. seq]): a snapshot's items) */
inline void* StrFromChars(const char16_t* p, int32_t n) {
    void* r = NewString(n);
    if (r && n > 0) std::memcpy(const_cast<char16_t*>(Chars(r)), p, (size_t)n * 2);
    return r;
}
/* s.AsSpan(start) / s.AsSpan(start, length) read as a string (string.Concat over spans): s.Substring's characters, clamped */
inline void* SubstringOf(const void* s, int32_t start, int32_t length = -1) {
    int32_t n = Len(s);
    if (start < 0) start = 0;
    if (start > n) start = n;
    if (length < 0 || length > n - start) length = n - start;
    return StrFromChars(Chars(s) + start, length);
}
/* string.CompareOrdinal(a, b) (StringComparer.Ordinal): UTF-16 units in order, null first */
inline int32_t CompareOrdinal(const void* a, const void* b) {
    if (a == b) return 0;
    if (!a) return -1;
    if (!b) return 1;
    int32_t na = Len(a), nb = Len(b), n = na < nb ? na : nb;
    const char16_t *x = Chars(a), *y = Chars(b);
    for (int32_t i = 0; i < n; i++) if (x[i] != y[i]) return (int32_t)x[i] - (int32_t)y[i];
    return na - nb;
}
/* StringComparer.OrdinalIgnoreCase.Compare: .NET compares the units upper-cased (ASCII here: TF.EX's level paths and tags) */
inline int32_t CompareOrdinalIgnoreCase(const void* a, const void* b) {
    if (a == b) return 0;
    if (!a) return -1;
    if (!b) return 1;
    int32_t na = Len(a), nb = Len(b), n = na < nb ? na : nb;
    const char16_t *x = Chars(a), *y = Chars(b);
    auto up = [](char16_t c) { return (int32_t)(c >= u'a' && c <= u'z' ? c - 32 : c); };
    for (int32_t i = 0; i < n; i++) if (up(x[i]) != up(y[i])) return up(x[i]) - up(y[i]);
    return na - nb;
}

/* ---- LINQ over game collections, by internal iteration: a sequence is a callable taking a visitor f(x) -> bool (false: stop) and
 * returning false if it was stopped. Materialized results (ToList/ToArray) are snapshots of at most SnapCap elements, never handed
 * to game code (docs/ws-tfex-subclass.md rows 14-15; C# exceptions such as Single's are not reproduced: row 21). ---- */
constexpr int SnapCap = 64;
/* a fixed-capacity container of the runtime (Snap, ValMap, ValSet, VList) is full: the element is dropped, as before, but logged
 * (the first few per kind, then a count): running out is an error to see on the console log, never silent */
void PoolFull(const char* what, int capacity, void* site = nullptr);
/* a stand-in that fails where .NET throws (the socket: ClientWebSocket's connect / receive / send; [service] rule throws = true) records a
 * fault for its thread; generated code in a try checks it after the call and runs the catch (the translator's ITryOperation). Per thread
 * (Horizon's thread-local register tpidrro_el0, as shim.c's pthread keys): the game thread and TF.EX's listen thread fail apart. */
void Fault(const char* what);
bool Faulted();
void ClearFault();
/* a stand-in with nothing yet (the socket's receive, called from TF.EX's listen loop on the game thread: [service] rule yields = true)
 * marks it; the generated code then returns from the loop's pass, and the pump that called the pass takes the mark */
void SetWouldBlock();
bool WouldBlock();       /* (the generated check: the mark stays) */
bool TakeWouldBlock();   /* (the pump's: the mark, cleared) */   /* site: the caller (crash-stack / addr2line on the build's ELF names it) */
/* a container's count outside 0..capacity (its memory was overwritten: e.g. a runtime container freed by a rollback's heap restore and
 * reused, console 2026-10-18 20:33): logged, the container read as empty. Every loop over a container's elements goes through Live(), so
 * no loop can run past the arrays (and the compiler can't drop a capacity check on the assumption that it doesn't) */
int32_t CorruptCount(const char* what, int32_t count, int capacity);
/* SnapCap elements inline; past that a managed object[] (GC-scanned, collected once no Snap points at it: arr_ keeps it alive), doubling.
 * A game list can be longer than 64 (an explosion's 67-81 counters: TF.State's LoadState snapshots them; dropping the rest desynced
 * TowerForge, 2026-10-24), so a snapshot never drops an element. items points at buf_ or into arr_. */
template <class T> struct Snap {
    int32_t count = 0;
    int32_t cap = SnapCap;
    T* items;
    void* arr_ = nullptr;
    T buf_[SnapCap];
    Snap() : items(buf_) {}
    explicit operator bool() const { return true; }   /* C#'s `seq?.X`: a snapshot is never null */
    Snap(const Snap& o) : items(buf_) { CopyFrom(o); }
    Snap& operator=(const Snap& o) { if (this != &o) { count = 0; cap = SnapCap; items = buf_; arr_ = nullptr; CopyFrom(o); } return *this; }
    int32_t Live() const { return count >= 0 && count <= cap ? count : CorruptCount("Snap", count, cap); }
    void Add(const T& v) { if (count == cap && !Grow(cap * 2)) { PoolFull("Snap (a LINQ snapshot: no memory to grow)", cap, __builtin_return_address(0)); return; } items[count++] = v; }
    int32_t Count() const { return Live(); }
    T& operator[](int i) { return items[i]; }
    template <class F> bool operator()(F&& f) const { for (int i = 0; i < Live(); i++) if (!f(items[i])) return false; return true; }
    bool Grow(int32_t n);   /* below NewArr */
    void CopyFrom(const Snap& o) { int32_t n = o.Live(); if (n > cap && !Grow(n)) n = cap; for (int32_t i = 0; i < n; i++) items[i] = o.items[i]; count = n; }
};
/* Enumerable.SequenceEqual(a, b): both in order, the same length, each pair equal */
template <class T, class A, class B, class E> bool SequenceEqual(A a, B b, E eq) {
    Snap<T> x, y;
    a([&](T v) { x.Add(v); return true; }); b([&](T v) { y.Add(v); return true; });
    if (x.count != y.count) return false;
    for (int i = 0; i < x.count; i++) if (!eq(x.items[i], y.items[i])) return false;
    return true;
}
template <class G> struct Seq {
    G g;
    template <class F> bool operator()(F&& f) const { return g(f); }
};
template <class G> Seq<G> MakeSeq(G g) { return Seq<G>{g}; }
/* a string's characters as a sequence (LINQ over a string: pasted.Where(IsCodeChar)) */
inline auto StrSeq(void* s) { return MakeSeq([s](auto&& f) { int32_t n = Len(s); for (int32_t i = 0; i < n; i++) if (!f(CharAt(s, i))) return false; return true; }); }
/* a game List<T> (get_Count / get_Item bodies: the instantiation's) */
/* a game List<T> in order (count / item: its get_Count / get_Item). Of references, its fields are read directly at each step (_size +0x18, _items
 * +0x10, the elements at the array's data) instead of two game calls per element: the same elements, also when the loop's body changes the list
 * (both are read again each step, as the calls did). Other element types go through the calls. */
template <class T> auto FromList(void* list, uintptr_t count, uintptr_t item) {
    return MakeSeq([=](auto&& f) {
        if constexpr (std::is_pointer_v<T>) {
            (void)count; (void)item;
            for (int32_t i = 0; list && i < Fld<int32_t>(list, 0x18); i++)
                if (!f(reinterpret_cast<T*>(*reinterpret_cast<uintptr_t*>(Fld<uintptr_t>(list, 0x10) + 0x10))[i])) return false;
        } else {
            for (int32_t i = 0; list && i < Fn<int32_t (*)(void*)>(count)(list); i++)
                if (!f(Fn<T (*)(void*, int32_t)>(item)(list, i))) return false;
        }
        return true;
    });
}
template <class S> auto OfType(S src, void* type) {
    return MakeSeq([=](auto&& f) { return src([&](void* x) { void* y = As(x, type); return y ? f(y) : true; }); });
}
template <class S, class P> auto Where(S src, P pred) {
    return MakeSeq([=](auto&& f) { return src([&](auto x) { return pred(x) ? f(x) : true; }); });
}
template <class S, class M> auto Select(S src, M map) {
    return MakeSeq([=](auto&& f) { return src([&](auto x) { return f(map(x)); }); });
}
/* int.Parse(s) (invariant, an optional sign, surrounding white space): C#'s FormatException / OverflowException are fatal here, as the
 * game's own exceptions (gen::Throw) */
int32_t ParseInt(const void* s);
bool TryParseInt(const void* s, int32_t* out);
/* translate.sh with TFEX_PROF_UNITS (a throwaway profiling regeneration, never committed): TFEXGEN_PROF("Unit.Method") at each method's entry; inclusive
 * and self ticks and calls per method, in statert::ProfBegin / ProfEnd (tfexgen_state.cpp), logged by the rollback seam as "[prof] ..." */
struct ProfSlot { const char* name; uint64_t incl, self; uint32_t calls, idx; };   /* constant-initialized (no static-init guard: the module has no TLS for it); idx 0 = not yet registered */
struct ProfScope {
    ProfSlot* s; uint64_t t0, child = 0; ProfScope* parent;
    explicit ProfScope(ProfSlot* slot);
    ~ProfScope();
};
extern ProfScope* g_profTop;
#define TFEXGEN_PROF(n) static ::tfex::gen::ProfSlot _prof_slot = { n, 0, 0, 0, 0 }; ::tfex::gen::ProfScope _prof_scope(&_prof_slot)
void Trace(const char* what);   /* translate.sh with TFEX_TRACE_UNITS: a method entered (the first 400, then every 1000th) */
/* List<T>.FindIndex(pred) over a sequence: the first match's index, or -1 */
template <class S, class P> int32_t FindIndex(S src, P pred) { int32_t i = 0, r = -1; src([&](auto x) { if (pred(x)) { r = i; return false; } i++; return true; }); return r; }
/* Select((x, i) => ...): the element's index too */
template <class S, class M> auto SelectI(S src, M map) {
    return MakeSeq([=](auto&& f) { int32_t i = 0; return src([&](auto x) { return f(map(x, i++)); }); });
}
/* Where((x, i) => ...): the element's index too */
template <class S, class P> auto WhereI(S src, P pred) {
    return MakeSeq([=](auto&& f) { int32_t i = 0; return src([&](auto x) { return pred(x, i++) ? f(x) : true; }); });
}
template <class T, class S> Snap<T> ToSnap(S src) { Snap<T> r; src([&](T x) { r.Add(x); return true; }); return r; }
template <class T, class S, class P> T FirstOrDefault(S src, P pred, T def) { T r = def; src([&](T x) { if (pred(x)) { r = x; return false; } return true; }); return r; }
template <class S, class P> bool Any(S src, P pred) { bool r = false; src([&](auto x) { if (pred(x)) { r = true; return false; } return true; }); return r; }
template <class S> int32_t Count(S src) { int32_t n = 0; src([&](auto) { n++; return true; }); return n; }
template <class S, class F> void ForEach(S src, F f) { src([&](auto x) { f(x); return true; }); }
template <class T> auto FromArr(Arr<T> a) { return MakeSeq([=](auto&& f) { for (int32_t i = 0; i < a.len; i++) if (!f(a.data[i])) return false; return true; }); }
template <class A, class B> auto SeqConcat(A a, B b) { return MakeSeq([=](auto&& f) { return a(f) && b(f); }); }
/* Distinct / Except: snapshots (C#'s are lazy; TF.EX consumes them at once), equality as C#'s default comparer (eq: the translator's) */
template <class T, class S, class E> Snap<T> Distinct(S src, E eq) {
    Snap<T> r;
    src([&](T x) { for (int i = 0; i < r.count; i++) if (eq(r.items[i], x)) return true; r.Add(x); return true; });
    return r;
}
template <class T, class S, class O, class E> Snap<T> Except(S src, O other, E eq) {
    Snap<T> r;
    src([&](T x) {
        bool in = false;
        other([&](T y) { if (eq(x, y)) { in = true; return false; } return true; });
        for (int i = 0; !in && i < r.count; i++) if (eq(r.items[i], x)) in = true;   /* Except is a set operation: distinct too */
        if (!in) r.Add(x);
        return true;
    });
    return r;
}
template <class S, class P> bool All(S src, P pred) { bool r = true; src([&](auto x) { if (!pred(x)) { r = false; return false; } return true; }); return r; }
template <class S> bool AnyAt(S src) { bool r = false; src([&](auto) { r = true; return false; }); return r; }
template <class S, class P> int32_t CountIf(S src, P pred) { int32_t n = 0; src([&](auto x) { if (pred(x)) n++; return true; }); return n; }
template <class T, class S, class E> bool Contains(S src, const T& v, E eq) { bool r = false; src([&](T x) { if (eq(x, v)) { r = true; return false; } return true; }); return r; }
template <class T, class S> T FirstAt(S src, T def) { T r = def; src([&](T x) { r = x; return false; }); return r; }
template <class T, class S, class P> T LastOrDefault(S src, P pred, T def) { T r = def; src([&](T x) { if (pred(x)) r = x; return true; }); return r; }
template <class R, class S, class M> R MaxOf(S src, M sel, R def) { bool any = false; R r = def; src([&](auto x) { R v = sel(x); if (!any || v > r) r = v; any = true; return true; }); return r; }
template <class R, class S, class M> R MinOf(S src, M sel, R def) { bool any = false; R r = def; src([&](auto x) { R v = sel(x); if (!any || v < r) r = v; any = true; return true; }); return r; }
template <class R, class S, class M> R SumOf(S src, M sel) { R r{}; src([&](auto x) { r += sel(x); return true; }); return r; }
/* OrderBy / OrderByDescending: a stable insertion sort of the snapshot by key (C#'s OrderBy is stable too) */
template <class T, class S, class K> Snap<T> OrderBy(S src, K key, bool descending) {
    Snap<T> r = ToSnap<T>(src);
    for (int i = 1; i < r.count; i++)
        for (int j = i; j > 0; j--) {
            auto a = key(r.items[j - 1]), b = key(r.items[j]);
            if (descending ? (a < b) : (b < a)) { T t = r.items[j]; r.items[j] = r.items[j - 1]; r.items[j - 1] = t; } else break;
        }
    return r;
}
/* Order(comparer) / OrderBy(key, comparer): the same stable sort, by a comparer (cmp(a, b) < 0: a first) */
template <class T, class S, class C> Snap<T> OrderByCmp(S src, C cmp) {
    Snap<T> r = ToSnap<T>(src);
    for (int i = 1; i < r.count; i++)
        for (int j = i; j > 0; j--) {
            if (cmp(r.items[j], r.items[j - 1]) < 0) { T t = r.items[j]; r.items[j] = r.items[j - 1]; r.items[j - 1] = t; } else break;
        }
    return r;
}
inline auto Range(int32_t start, int32_t count) { return MakeSeq([=](auto&& f) { for (int32_t i = 0; i < count; i++) if (!f(start + i)) return false; return true; }); }
/* Enumerable.Repeat(value, count): count copies of value */
template <class T> inline auto Repeat(T value, int32_t count) { return MakeSeq([=](auto&& f) { for (int32_t i = 0; i < count; i++) if (!f(value)) return false; return true; }); }
/* Reverse(): the elements backwards (a snapshot first, as LINQ buffers it) */
template <class T, class S> auto ReverseSeq(S src) { Snap<T> r = ToSnap<T>(src); return MakeSeq([=](auto&& f) { for (int i = r.count - 1; i >= 0; i--) if (!f(r.items[i])) return false; return true; }); }
template <class S> auto Take(S src, int32_t n) { return MakeSeq([=](auto&& f) { int32_t k = 0; return src([&](auto x) { if (k++ >= n) return false; return f(x); }) || true; }); }
template <class S> auto Skip(S src, int32_t n) { return MakeSeq([=](auto&& f) { int32_t k = 0; return src([&](auto x) { return k++ < n ? true : f(x); }); }); }

/* a game Dictionary<K, V> (Mono's: linkSlots / keySlots / valueSlots / touchedSlots; a slot is used when its Link.HashCode has the
 * top bit, as Mono's Enumerator.MoveNext tests): its pairs in slot order, as C#'s foreach */
/* Enum.TryParse<E>(s, out e) (case-sensitive, as C#'s default): a literal's name or an integer */
template <class E, int N> bool EnumTryParse(const void* s, const char16_t* const (&names)[N], const int64_t (&vals)[N], E* out) {
    int32_t n = Len(s);
    *out = E{};
    if (n == 0) return false;
    for (int k = 0; k < N; k++) {
        int32_t i = 0;
        while (i < n && names[k][i] && names[k][i] == CharAt(s, i)) i++;
        if (i == n && !names[k][i]) { *out = (E)vals[k]; return true; }
    }
    int64_t v = 0; int32_t i = (CharAt(s, 0) == u'-' || CharAt(s, 0) == u'+') ? 1 : 0;
    if (i == n) return false;
    for (int32_t j = i; j < n; j++) { char16_t c = CharAt(s, j); if (c < u'0' || c > u'9') return false; v = v * 10 + (c - u'0'); }
    *out = (E)(CharAt(s, 0) == u'-' ? -v : v);
    return true;
}
template <class K, class V> struct KV { K Key; V Value; template <class K2, class V2> operator KV<K2, V2>() const { return KV<K2, V2>{(K2)Key, (V2)Value}; } };   /* (a string-keyed map's StrK key to a void* key) */
/* Array.IndexOf(arr, v): the first index of v, -1 */
template <class T, class U, class E> int32_t ArrIndexOf(const Arr<T>& a, const U& v, E eq) { for (int32_t i = 0; i < a.len; i++) if (eq(a.data[i], v)) return i; return -1; }
template <class T> inline T* ArrData(void* arr) { return arr ? *reinterpret_cast<T**>(reinterpret_cast<uintptr_t>(arr) + 0x10) : nullptr; }
template <class K, class V> auto FromDict(void* d, uint32_t link, uint32_t keys, uint32_t vals, uint32_t touched) {
    return MakeSeq([=](auto&& f) {
        if (!d) return true;
        int32_t n = Fld<int32_t>(d, touched);
        const int32_t* L = ArrData<int32_t>(Fld<void*>(d, link));
        const K* ks = ArrData<K>(Fld<void*>(d, keys));
        const V* vs = ArrData<V>(Fld<void*>(d, vals));
        for (int32_t i = 0; i < n; i++)
            if ((uint32_t)L[2 * i] & 0x80000000u)
                if (!f(KV<K, V>{ks[i], vs[i]})) return false;
        return true;
    });
}
/* a game HashSet<T> in its slots' order (Mono's HashSet: links[i].HashCode has the high bit while slot i is used; insertion order while
 * nothing was removed, as Mono's enumerator) */
template <class T> auto FromSet(void* h, uint32_t link, uint32_t slots, uint32_t touched) {
    return MakeSeq([=](auto&& f) {
        if (!h) return true;
        int32_t n = Fld<int32_t>(h, touched);
        const int32_t* L = ArrData<int32_t>(Fld<void*>(h, link));
        const T* xs = ArrData<T>(Fld<void*>(h, slots));
        for (int32_t i = 0; i < n; i++)
            if ((uint32_t)L[2 * i] & 0x80000000u)
                if (!f(xs[i])) return false;
        return true;
    });
}
/* a dictionary over a TF.EX value type (IReadOnlyDictionary<int, NetworkStats>: no code for it in the game): a small map of values,
 * in insertion order (C#'s Dictionary enumerates in insertion order while nothing is removed). Get of a missing key returns V{}
 * (C# throws). Reached through a pointer, like a C# reference. */
/* a string key of a ValMap / ValSet (C#'s HashSet<string>, Dictionary<string, V>): equal by content, as .NET's string comparer; a managed
 * string pointer in and out (implicit), so the call sites are those of a void* key */
struct StrK {
    void* p = nullptr;
    StrK() = default;
    StrK(void* s) : p(s) {}
    operator void*() const { return p; }
    friend bool operator==(const StrK& a, const StrK& b) { return StrEq(a.p, b.p); }
};
template <class K, class V, int N = 64> struct ValMap {
    int32_t count = 0;
    int32_t Live() const { return count >= 0 && count <= N ? count : CorruptCount("ValMap", count, N); }
    K keys[N];
    V vals[N];
    int32_t Count() const { return Live(); }
    int Find(const K& k) const { for (int i = 0; i < Live(); i++) if (keys[i] == k) return i; return -1; }
    bool ContainsKey(const K& k) const { return Find(k) >= 0; }
    bool ContainsValue(const V& v) const { for (int i = 0; i < Live(); i++) if (vals[i] == v) return true; return false; }
    V Get(const K& k) const { int i = Find(k); return i >= 0 ? vals[i] : V{}; }
    bool TryGetValue(const K& k, V* out) const { int i = Find(k); if (out) *out = i >= 0 ? vals[i] : V{}; return i >= 0; }
    void Set(const K& k, const V& v) { int i = Find(k); if (i >= 0) vals[i] = v; else if (count < N) { keys[count] = k; vals[count++] = v; } else PoolFull("ValMap", N, __builtin_return_address(0)); }
    bool Add(const K& k, const V& v) { if (Find(k) >= 0) return false; Set(k, v); return true; }   /* C# throws on a duplicate */
    bool Remove(const K& k) {
        int i = Find(k); if (i < 0) return false;
        for (int j = i; j + 1 < Live(); j++) { keys[j] = keys[j + 1]; vals[j] = vals[j + 1]; }
        count--; return true;
    }
    void Clear() { count = 0; }
    template <class F> bool operator()(F&& f) const { for (int i = 0; i < Live(); i++) if (!f(KV<K, V>{keys[i], vals[i]})) return false; return true; }
};
/* HashSet<T> / List<T> the game never compiled (HashSet<int>, List<Input> ...): the same shapes over values, insertion order */
/* a set's element equality: C++ ==, or for a string key (StrK) of a set made with StringComparer.OrdinalIgnoreCase ignoring ASCII case */
template <class T> inline bool SetEq(const T& a, const T& b, bool) { return a == b; }
inline bool SetEq(const StrK& a, const StrK& b, bool ignoreCase) { return ignoreCase ? StrEqI(a.p, b.p) : StrEq(a.p, b.p); }
/* a big set of plain values (translate.toml [capacity] above 64: AIJimmy's path search visits up to a wide level's 1008 cells, checked
 * for every candidate move) keeps a hash index beside its insertion-ordered items: open addressing over the value's bytes, slot = item
 * index + 1. Small sets, and sets of strings or object references (equality by content / identity), search linearly as before. */
template <class T> inline uint32_t ValBytesHash(const T& v) {
    const unsigned char* b = reinterpret_cast<const unsigned char*>(&v); uint32_t h = 2166136261u;
    for (size_t i = 0; i < sizeof(T); i++) { h ^= b[i]; h *= 16777619u; }
    return h;
}
template <class T, int N = 64> struct ValSet {
    static constexpr bool kIndexed = N > 64 && std::is_trivially_copyable_v<T> && !std::is_pointer_v<T> && !std::is_same_v<T, StrK>;
    static constexpr int kSlots = kIndexed ? 2 * N : 1;
    int32_t count = 0;
    bool ignoreCase = false;   /* ToHashSet(StringComparer.OrdinalIgnoreCase) over strings (TF.EX's ModCollections) */
    int32_t Live() const { return count >= 0 && count <= N ? count : CorruptCount("ValSet", count, N); }
    T items[N];
    int32_t slot[kSlots] = {};   /* (kIndexed) the hash index: 0 empty, else the item's index + 1 */
    int32_t Count() const { return Live(); }
    int Find(const T& v) const {
        if constexpr (kIndexed) {
            for (uint32_t k = ValBytesHash(v) % kSlots, n = 0; n < (uint32_t)kSlots; k = (k + 1) % kSlots, n++) {
                if (slot[k] == 0) return -1;
                if (SetEq(items[slot[k] - 1], v, ignoreCase)) return slot[k] - 1;
            }
            return -1;
        } else {
            for (int i = 0; i < Live(); i++) if (SetEq(items[i], v, ignoreCase)) return i;
            return -1;
        }
    }
    void Index(int i) {
        if constexpr (kIndexed) { uint32_t k = ValBytesHash(items[i]) % kSlots; while (slot[k]) k = (k + 1) % kSlots; slot[k] = i + 1; }
    }
    void Reindex() { if constexpr (kIndexed) { for (int k = 0; k < kSlots; k++) slot[k] = 0; for (int i = 0; i < Live(); i++) Index(i); } }
    bool Contains(const T& v) const { return Find(v) >= 0; }
    bool Add(const T& v) { if (Find(v) >= 0) return false; if (count >= N) { PoolFull("ValSet", N); return false; } items[count] = v; Index(count); count++; return true; }
    bool Remove(const T& v) { int i = Find(v); if (i < 0) return false; for (int j = i; j + 1 < Live(); j++) items[j] = items[j + 1]; count--; Reindex(); return true; }
    /* C#'s IntersectWith(IEnumerable<T>): only the members the sequence also has stay (in their order) */
    template <class S> void IntersectWith(S seq) {
        int w = 0;
        for (int i = 0; i < Live(); i++) { bool in = false; T v = items[i]; seq([&](T x) { if (x == v) { in = true; return false; } return true; }); if (in) items[w++] = v; }
        count = w;
        Reindex();
    }
    void Clear() { count = 0; if constexpr (kIndexed) for (int k = 0; k < kSlots; k++) slot[k] = 0; }
    template <class F> bool operator()(F&& f) const { for (int i = 0; i < Live(); i++) if (!f(items[i])) return false; return true; }
    bool SetEquals(const ValSet* o) const {   /* C#'s SetEquals(IEnumerable): same members (a null set: as an empty one) */
        int n = o ? o->Live() : 0;
        if (n != count) return false;
        for (int i = 0; i < n; i++) if (!Contains(o->items[i])) return false;
        return true;
    }
};
template <class T, int N = 128> struct VList {   /* (128: TF.EX's ScenarioCatalog lists 80-odd runs) */
    int32_t count = 0;
    int32_t Live() const { return count >= 0 && count <= N ? count : CorruptCount("VList", count, N); }
    T items[N];
    int32_t Count() const { return Live(); }
    T Get(int32_t i) const { return items[i]; }
    void Set(int32_t i, const T& v) { items[i] = v; }
    void Add(const T& v) { if (count < N) items[count++] = v; else PoolFull("VList", N); }
    void RemoveAt(int32_t i) { for (int j = i; j + 1 < Live(); j++) items[j] = items[j + 1]; count--; }
    void Insert(int32_t i, const T& v) {   /* List<T>.Insert (the megamod's TOWERBALL into FortRise's GAME MODE list) */
        if (count >= N) { PoolFull("VList", N); return; }
        if (i < 0) i = 0; if (i > Live()) i = Live();
        for (int j = Live(); j > i; j--) items[j] = items[j - 1];
        items[i] = v; count++;
    }
    T Dequeue() { T v = Live() > 0 ? items[0] : T{}; if (Live() > 0) RemoveAt(0); return v; }   /* a Queue<T>'s (front = index 0) */
    bool TryDequeue(T* out) { if (Live() <= 0) return false; *out = Dequeue(); return true; }   /* a ConcurrentQueue<T>'s */
    void Clear() { count = 0; }
    /* List<T>.Sort(Comparison<T>): an insertion sort (stable; .NET's introsort isn't, so equal items may differ in order: no caller
     * relies on it, FortRise's MODS search sorts by score) */
    template <class C> void Sort(C&& cmp) {
        for (int i = 1; i < Live(); i++) { T v = items[i]; int j = i - 1; while (j >= 0 && cmp(items[j], v) > 0) { items[j + 1] = items[j]; j--; } items[j + 1] = v; }
    }
    template <class F> bool operator()(F&& f) const { for (int i = 0; i < Live(); i++) if (!f(items[i])) return false; return true; }
};
/* System.Collections.Concurrent.ConcurrentQueue<T> (TF.EX's MatchmakingService.gameThreadActions: its listen thread queues, the game thread
 * drains): a VList behind a spinlock; running out of room is a pool error (PoolFull), the item is dropped */
template <class T, int N = 128> struct CQueue {
    volatile int lock_ = 0;
    VList<T, N> items;
    void Lock() { while (__atomic_exchange_n(&lock_, 1, __ATOMIC_ACQUIRE)) {} }
    void Unlock() { __atomic_store_n(&lock_, 0, __ATOMIC_RELEASE); }
    int32_t Count() { Lock(); int32_t n = items.Count(); Unlock(); return n; }
    void Add(const T& v) { Lock(); items.Add(v); Unlock(); }
    bool TryDequeue(T* out) { Lock(); bool ok = items.TryDequeue(out); Unlock(); return ok; }
    void Clear() { Lock(); items.Clear(); Unlock(); }
};
/* TF.EX.Domain.Utils.AttributeManager<T> (ledger S-native: "generic C# helper"), over a VList: items, default(T) past the end. TF.EX's
 * one use (GameContext.CurrentInputs) has the factory EmptyInput() = new Input(), i.e. default(T): the mirror makes T{} */
template <class T> struct AttrMgr {
    VList<T> items;
    T At(int32_t i) const { return i >= 0 && i < items.count ? items.items[i] : T{}; }
    void Init(int32_t n) { for (int32_t i = 0; i < n; i++) items.Add(T{}); }
};
/* `new Dictionary<..>()` etc. of those: GC_malloc_uncollectable memory (scanned, never freed), so a game object or a managed array held
 * as a key / value stays alive. A ring of 8 per type for objects made in a method (each make reuses the oldest: 8 live at most, as
 * TF.EX's are built and consumed per call); a static field's (made once) gets its own (NewValStatic). */
/* NewValStatic's memory: GC_malloc_uncollectable (scanned: a container may hold managed references), zeroed. (Before the TF.State rollback
 * switch-over, an arena reserved before each lobby match, since a memory restore freed what the window made; a TF.State load frees nothing.) */
void* RuntimeAlloc(size_t bytes);
template <class T> T* NewValStatic() {
    void* p = RuntimeAlloc(sizeof(T));
    return p ? new (p) T() : nullptr;
}
/* a static of a static TF.EX class that can hold a managed reference (string, game object, delegate, array): its storage in
 * uncollectable memory, made at first use. The module's .bss is no GC root, so a reference kept in a plain C++ static is collected
 * and its memory reused while we still write through it (the multi-genpatch lobby-start crash, Integration B's analysis). */
/* `out _`: scratch storage for an out argument nobody reads (a reference written there is never dereferenced, so unscanned is fine) */
template <class T> T& Discard() { static T t{}; return t; }
/* field = new container (made in the ring): the field's own storage, allocated once and reused, gets the contents (C#'s new object;
 * a holder of the old reference sees the new contents, which TF.EX's code doesn't rely on) */
template <class T> T* KeepVal(T*& slot, T* v) {
    if (!v) { slot = nullptr; return nullptr; }
    if (slot == v) return slot;
    if (!slot) slot = NewValStatic<T>();
    if (slot) *slot = *v;
    return slot;
}
/* an instance field's container (`Dictionary<int, X> RoundData = new()` of a TF.State model, made with every GameState): the game's
 * GC_malloc (+0xde0, what NewObject uses for types with references: scanned, collected with the object that points to it). Not
 * NewValStatic: that memory is never freed, and GetState every tick ran the 1 MB runtime arena out (2026-10-21) */
template <class T> T* NewValGc() {
    void* p = Fn<void* (*)(uint64_t)>(0xde0)(sizeof(T));
    return p ? new (p) T() : nullptr;
}
template <class T> T& RootStatic(T*& slot) { if (!slot) slot = NewValStatic<T>(); return *slot; }
/* System.Runtime.CompilerServices.StrongBox<T> (a ConditionalWeakTable's value, TF.State's owned state per entity): the value in the
 * runtime arena (NewValStatic: allocated before or inside a match, never freed by a restore); one per entity that ever gets a value */
template <class T> struct Box { T Value{}; };
template <class T> Box<T>* NewBox(T v) { auto* b = NewValStatic<Box<T>>(); if (b) b->Value = v; return b; }
template <class T, int N> struct RootArr { T v[N]; };   /* a static array of references, as one RootStatic (EmitField's literal arrays) */
template <class T> T* NewVal() {
    static T* ring[8] = {};
    static int next = 0;
    T*& slot = ring[next++ & 7];
    if (!slot) slot = NewValStatic<T>();
    else *slot = T();
    return slot;
}
/* double.GetHashCode() as .NET (Core) runs it: the bits folded, 0 / -0 and every NaN normalized first (System.Double.GetHashCode) */
inline int32_t DoubleHash(double v) {
    int64_t bits; std::memcpy(&bits, &v, 8);
    if (((bits - 1) & 0x7fffffffffffffffll) >= 0x7ff0000000000000ll) bits &= 0x7ff0000000000000ll;   /* IsNaNOrZero: PositiveInfinityBits */
    return (int32_t)bits ^ (int32_t)(bits >> 32);
}
/* TF.EX's and TF.State's own state attached to game objects by DynamicData (openSeatOrb on a RollcallElement, a coroutine's NAME, a
 * chest's bottomlessCounter, FortRise's fields on a MatchVariants): a member the game class doesn't have. One table for all: (member
 * name, object) -> an 8-byte value, 16384 entries, MonoMod DynamicData's semantics (a ConditionalWeakTable): an entry lives as long as
 * its object, across matches as on the PC (weak keys, the values in scanned memory: tfexgen_code_rt.cpp); full of live entries is a
 * clear error (PoolFull), never an eviction. */
uint64_t* SideSlot(const char* name, void* obj, bool create);
void SideReport();   /* before a lobby match (statert::BeforeMatch): its use in the log */
void PrepareRuntime();   /* on the first main-menu frame (tfexgen_glue's MainMenu.Update hook; never from exl_main: the game's GC isn't up
                          * there): the runtime's own tables made outside any rollback window (the side table). Cheap once made */
/* SpriteBatch.Begin(sortMode, blend, sampler, depthStencil, rasterizer, effect, transform): the Switch's (MonoGame) takes the transform as a
 * Matrix? (the 64-byte Matrix, then hasValue at +0x40, by pointer), where the PC's FNA takes a Matrix: a bare Matrix left hasValue to
 * whatever followed it on the stack, so TF.EX's transform was dropped (its roll-call overlay drawn without the UI camera's +50). */
void SpriteBatchBegin(void* batch, int32_t sortMode, void* blend, void* sampler, void* depthStencil, void* rasterizer, void* effect, const Matrix& transform);
template <class T> T SideGet(const char* name, void* obj) {
    static_assert(sizeof(T) <= 8, "side table values are 8 bytes");
    T v{}; if (uint64_t* s = SideSlot(name, obj, false)) std::memcpy(&v, s, sizeof(T)); return v;
}
template <class T> Opt<T> SideGetOpt(const char* name, void* obj) {   /* `dyn.Get("x") as bool?`: null when never set */
    T v{}; uint64_t* s = SideSlot(name, obj, false); if (!s) return Opt<T>{}; std::memcpy(&v, s, sizeof(T)); return Opt<T>(v);
}
template <class T> void SideSet(const char* name, void* obj, T v) {
    static_assert(sizeof(T) <= 8, "side table values are 8 bytes");
    if (uint64_t* s = SideSlot(name, obj, true)) { *s = 0; std::memcpy(s, &v, sizeof(T)); }
}
/* Monocle.Coroutine over a TF.EX iterator (`Add(new Coroutine(Seq()))`): the iterator's C++20 coroutine twin (Seq_co) stepped once
 * a frame by a glue Tween (GlueRewrites::CoroutineOf), as Coroutine.Update: `yield return n` (an int) skips n frames (Counter at
 * Engine.TimeMult 1, the Switch's fixed ticks), anything else continues next frame, the end removes it. The frame lives in
 * GC_malloc_uncollectable memory (scanned: the iterator's managed locals stay alive). A finished run's frame and record are zeroed
 * (no stale references for the GC) and reused by the next run of that size (CoAlloc / CoFree): memory is bounded by the most runs
 * alive at once (GC_free isn't bound on the Switch). A run whose Tween goes away before the end (its entity left the scene) is never
 * finished: its frame stays, one per abandoned run. */
void* CoAlloc(size_t n);
void CoFree(void* p, size_t n);
struct CoGen {
    struct promise_type {
        int32_t wait = 0;
        CoGen get_return_object() { return CoGen{std::coroutine_handle<promise_type>::from_promise(*this)}; }
        std::suspend_always initial_suspend() { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
        void return_void() {}
        void unhandled_exception() {}
        std::suspend_always yield_value(int32_t frames) { wait = frames; return {}; }
        static void* operator new(size_t n) { return CoAlloc(n); }
        static void operator delete(void* p, size_t n) { CoFree(p, n); }
    };
    std::coroutine_handle<promise_type> h;
};
int64_t CoStart(CoGen g);   /* the run's handle (for the glue's closure) */
bool CoStep(int64_t run);   /* one Coroutine.Update: false once finished */
template <class M> auto MapKeys(const M* m) { return MakeSeq([=](auto&& f) { for (int i = 0, n = m ? m->Live() : 0; i < n; i++) if (!f(m->keys[i])) return false; return true; }); }
template <class M> auto MapValues(const M* m) { return MakeSeq([=](auto&& f) { for (int i = 0, n = m ? m->Live() : 0; i < n; i++) if (!f(m->vals[i])) return false; return true; }); }
template <class M> auto FromValMap(const M* m) { return MakeSeq([=](auto&& f) { return m ? (*m)(f) : true; }); }
/* LINQ GroupBy(key): IGrouping<K, E> as a Group (its Key, its elements in order); the groups in order of their first element, made at
 * once into a ring container (NewVal: TF.EX consumes them within the call: ScenarioSweeper's census). 32 groups of 64 elements at most:
 * more is PoolFull, logged */
template <class K, class E> struct Group { K Key{}; VList<E, 64> items; };
template <class K, class E> struct Groups { VList<Group<K, E>, 32> groups; };
template <class K, class E, class S, class KF, class EQ> auto GroupBy(S src, KF key, EQ eq) {
    auto* gs = NewVal<Groups<K, E>>();
    if (gs) {
        gs->groups.Clear();
        src([&](E x) {
            K k = key(x); int at = -1;
            for (int i = 0; i < gs->groups.Count(); i++) if (eq(gs->groups.items[i].Key, k)) { at = i; break; }
            if (at < 0) { Group<K, E> g; g.Key = k; gs->groups.Add(g); at = gs->groups.Count() - 1; if (at < 0 || gs->groups.items[at].Key != k) return true; }
            gs->groups.items[at].items.Add(x);
            return true;
        });
    }
    return MakeSeq([gs](auto&& f) { for (int i = 0; gs && i < gs->groups.Count(); i++) if (!f(&gs->groups.items[i])) return false; return true; });
}
template <class K, class E> auto GroupItems(Group<K, E>* g) { return MakeSeq([g](auto&& f) { return g ? g->items(f) : true; }); }
/* a collection interface (IReadOnlyList<T>, ICollection<T> ...) over a game object: TF.EX's are a List<T> or a T[] at run time (anything
 * else is fatal, as C# would call through the interface). The List's bodies (its instantiation) and the array type. */
struct IList_ { uintptr_t listType, count, item, arrayType, arrLen; };
[[noreturn]] void Throw(const char* where);
inline bool IsArr(void* o, const IList_& l) {
    if (!o || As(o, TypeOf(l.listType))) return false;
    if (As(o, TypeOf(l.arrayType))) return true;
    Throw("collection interface: neither a List<T> nor a T[]");
}
inline int32_t ICount(void* o, IList_ l) { return !o ? 0 : IsArr(o, l) ? Fn<int32_t (*)(void*)>(l.arrLen)(o) : Fn<int32_t (*)(void*)>(l.count)(o); }
template <class T> T IItem(void* o, IList_ l, int32_t i) { return IsArr(o, l) ? ArrData<T>(o)[i] : Fn<T (*)(void*, int32_t)>(l.item)(o, i); }
/* ICollection<T>.Add / Clear / Remove / Contains through the List<T> body (an array: Contains by its elements; Add/Clear/Remove on an
 * array throw in C# too) */
template <class T> void IAdd(void* o, IList_ l, uintptr_t add, T x) { if (IsArr(o, l)) Throw("ICollection.Add on an array"); Fn<void (*)(void*, T)>(add)(o, x); }
inline void IClear(void* o, IList_ l, uintptr_t clear) { if (IsArr(o, l)) Throw("ICollection.Clear on an array"); Fn<void (*)(void*)>(clear)(o); }
template <class T> bool IRemove(void* o, IList_ l, uintptr_t remove, T x) { if (IsArr(o, l)) Throw("ICollection.Remove on an array"); return Fn<bool (*)(void*, T)>(remove)(o, x); }
template <class T, class E> bool IContains(void* o, IList_ l, uintptr_t contains, T x, E eq) {
    if (!IsArr(o, l)) return Fn<bool (*)(void*, T)>(contains)(o, x);
    int32_t n = ICount(o, l);
    for (int32_t i = 0; i < n; i++) if (eq(IItem<T>(o, l, i), x)) return true;
    return false;
}
template <class T> auto FromIList(void* o, IList_ l) {
    return MakeSeq([=](auto&& f) { int32_t n = ICount(o, l); for (int32_t i = 0; i < n; i++) if (!f(IItem<T>(o, l, i))) return false; return true; });
}
template <class S, class M> auto SelectMany(S src, M inner) {
    return MakeSeq([=](auto&& f) { return src([&](auto x) { return inner(x)(f); }); });
}
/* C# `throw`: the Switch build has no exceptions and the game's own ones are fatal: log and stop */
[[noreturn]] void Throw(const char* where);

/* ---- TF.EX's lobby models in translated code (tfexgen_Models.hpp's structs, reached through pointers: C# references) ---- */
void* ManagedUtf8(const char* s, int n);          /* a managed string from UTF-8 */
/* a log template's value: a null string shows as "(null)" (Microsoft.Extensions.Logging's formatter) */
inline void* OrNullText(void* s) { return s ? s : ManagedUtf8("(null)", 6); }
void* NewGuidString();                            /* Guid.NewGuid().ToString(): a random v4 UUID (netws::MakeUuid) */
int Utf8Of(const void* managed, char* out, int cap);
template <int N> void* Managed(const Str<N>& s) { return s.null ? nullptr : ManagedUtf8(s.s, (int)std::strlen(s.s)); }
/* a model's fixed string (Str<N>, translate.toml [models.caps]) too short for what TF.EX put in it: logged as an ERROR (a fixed-size pool
 * running out is never silent; MatchEnded's 64-character checksum was cut to 47 in a Str<48>, 2026-10-28) */
void StrCut(const void* managed, int written, int cap);
/* .NET's custom numeric format "0" / "0.0" / "0.00"... with the invariant culture: `decimals` digits after '.', midpoints away from zero */
void* FormatFixed(double v, int decimals);
/* DateTimeFormatInfo.InvariantInfo.GetAbbreviatedMonthName(month): "Jan".."Dec" (.NET throws outside 1..13; 13 is "") */
void* MonthAbbrev(int32_t month);
template <int N> void SetStr(Str<N>& s, const void* managed) { if (!managed) s.Set(nullptr); else { StrCut(managed, Utf8Of(managed, s.s, N), N); s.null = false; } }
template <class T> T* RefOf(Opt<T>& o) { return o.has ? &o.value : nullptr; }
template <class T> const T* RefOf(const Opt<T>& o) { return o.has ? &o.value : nullptr; }
template <class T> void SetRef(Opt<T>& o, const T* v) { o.has = v != nullptr; if (v) o.value = *v; }   /* C# assigns a reference; the struct holds a copy */
/* `new Player { ... }`: model objects TF.EX code builds are short-lived (built to send, read at once): a ring of 8 per type */
/* a class of ours' model fields own their storage (NewModel's ring is for temporaries): a single model keeps the last K assigned in its
 * own boxes (`previous = ownLobby; ownLobby = lobby;` still sees the old one), a model list is held by value (assignment copies) */
/* a collection passed for a parameter typed as a lobby model list (UnitEmitter ModelListParamCpp): copied into one of a ring of temporaries */
template <class L, class S> L* TempModelList(S seq) { static L ring[4]; static int next = 0; L* l = &ring[next++ & 3]; SetModelList(*l, seq); return l; }   /* (SetModelList clears it: no list temporary) */
/* a model list passed for a model-list parameter of another capacity (Lobby.SessionPlayers, List<Player, 4>, for MatchReport.Begin's
 * IEnumerable<Player>, List<Player, 16>): the same list when the types agree, else a copy in one of a ring of temporaries (null stays null) */
template <class L, class X> L* ModelListAs(X* x) {
    if constexpr (std::is_same_v<L, X>) return x;
    else {
        if (!x) return nullptr;
        static L ring[4]; static int next = 0; L* l = &ring[next++ & 3];
        l->Clear(); l->null = x->null;
        for (int i = 0; i < x->Count(); i++) l->Add(x->items[i]);
        return l;
    }
}
template <class L> L* ModelListAs(decltype(nullptr)) { return nullptr; }
template <class T, int K> T* KeepModel(T (&box)[K], int32_t& next, T* v) {
    if (!v) return nullptr;
    T* slot = &box[(next++ & 0x7fffffff) % K];
    if (slot != v) *slot = *v;
    return slot;
}
template <class T, int K> T* KeepModel(T (&)[K], int32_t&, decltype(nullptr)) { return nullptr; }
template <class L> void SetModelColl(L& field, const L* v) { if (!v) { field.count = 0; field.null = true; } else if (&field != v) field = *v; }   /* null in place: no list temporary */
template <class L> void SetModelColl(L& field, decltype(nullptr)) { field.count = 0; field.null = true; }
/* (a model can be large: ws::Lobby 36.5 KB, GetLobbiesResponseMessage ~600 KB; reset from a static empty one, never a stack temporary) */
template <class T> T* NewModel() { static T ring[8]; static const T empty{}; static int next = 0; T* r = &ring[next++ & 7]; *r = empty; return r; }
/* a model collection as a LINQ source: class elements as pointers, strings as managed strings, the rest by value */
template <class T> inline auto ModelElem(T& x) { return &x; }
inline int32_t ModelElem(int32_t& x) { return x; }
inline bool ModelElem(bool& x) { return x; }
template <int N> inline void* ModelElem(Str<N>& x) { return Managed(x); }
/* a model's list set from a sequence of managed values (a game List<string>, a LINQ chain, a snapshot): C#'s assignment of another
 * collection object, here a copy of its elements */
template <class E> inline void ModelPut(E& to, void* managed) { SetStr(to, managed); }
inline void ModelPut(int32_t& to, int32_t v) { to = v; }
template <class M> inline void ModelPut(M& to, M* v) { if (v) to = *v; }   /* a model object (NewModel / a list's element): copied in */
inline void ModelPut(bool& to, bool v) { to = v; }
template <class T, int N, class S> void SetModelList(List<T, N>& l, S seq) {
    l.Clear();
    seq([&](auto x) { if (T* p = l.Add()) ModelPut(*p, x); return true; });
}
/* a lobby model's Dictionary<string, string> (CustomMod.Data: StrMap): TryGetValue / ContainsKey / the indexer with managed strings */
template <int N, int K, int V> const Str<V>* StrMapFind(const StrMap<N, K, V>* m, const void* key) {
    if (!m || m->null || !key) return nullptr;
    char k[K]; Utf8Of(key, k, K);
    return m->Get(k);
}
template <int N, int K, int V> bool StrMapTry(const StrMap<N, K, V>* m, const void* key, void** out) {
    const Str<V>* v = StrMapFind(m, key); *out = v ? Managed(*v) : nullptr; return v != nullptr;
}
template <int N, int K, int V> bool StrMapHas(const StrMap<N, K, V>* m, const void* key) { return StrMapFind(m, key) != nullptr; }
template <int N, int K, int V> void StrMapSet(StrMap<N, K, V>* m, const void* key, const void* value) {   /* dict[key] = value / Add */
    if (!m || !key) return;
    char k[K], v[V]; Utf8Of(key, k, K); if (value) Utf8Of(value, v, V); else v[0] = 0;
    m->null = false; m->Set(k, v);
}
template <int N, int K, int V> void* StrMapGet(const StrMap<N, K, V>* m, const void* key) { const Str<V>* v = StrMapFind(m, key); return v ? Managed(*v) : nullptr; }   /* C# throws when absent */
/* the same calls on a KeyMap (Dictionary<string, V>, V not a string): TryGetValue's out is the model's pointer (a model local is ws::T*) or
 * the value itself; the indexer gives the same */
/* a model's KeyMap = a value map of ours with string keys (SeatStats.RawStats = MatchStatsReport.BuildRawStats(...)): entry by entry,
 * a null map a null member; more entries than the model's capacity is a pool error (translate.toml [models.caps]), not a silent cut */
template <int N, int K, class V, class M> void SetKeyMapFrom(KeyMap<N, K, V>& dst, const M* src) {
    dst.null = src == nullptr; dst.count = 0;
    if (!src) return;
    (*src)([&](const auto& kv) {
        if (dst.count >= N) { PoolFull("KeyMap (a model map: translate.toml [models.caps])", N, __builtin_return_address(0)); return false; }
        SetStr(dst.keys[dst.count], kv.Key.p); dst.values[dst.count++] = kv.Value; return true;
    });
}
template <int N, int K, class V> const V* StrMapFind(const KeyMap<N, K, V>* m, const void* key) {
    if (!m || m->null || !key) return nullptr;
    char k[K]; Utf8Of(key, k, K);
    return m->Get(k);
}
template <int N, int K, class V> bool StrMapTry(const KeyMap<N, K, V>* m, const void* key, V** out) {
    const V* v = StrMapFind(m, key); *out = const_cast<V*>(v); return v != nullptr;
}
template <int N, int K, class V> bool StrMapTry(const KeyMap<N, K, V>* m, const void* key, V* out) {
    const V* v = StrMapFind(m, key); *out = v ? *v : V{}; return v != nullptr;
}
template <int N, int K, class V> bool StrMapHas(const KeyMap<N, K, V>* m, const void* key) { return StrMapFind(m, key) != nullptr; }
template <int N, int K, class V> auto StrMapGet(const KeyMap<N, K, V>* m, const void* key) {   /* C# throws when absent */
    const V* v = StrMapFind(m, key);
    if constexpr (std::is_class_v<V>) return const_cast<V*>(v); else return v ? *v : V{};
}
/* a model list's place given a snapshot of models (a List<Player> parameter passed lobby.SessionPlayers.OrderBy(..).ToList()): its elements */
/* LINQ DefaultIfEmpty(value): the sequence, or that one value when it has none */
template <class S, class V> auto DefaultIfEmpty(S seq, V value) {
    return MakeSeq([=](auto&& f) { bool any = false; bool go = seq([&](auto x) { any = true; return f(x); }); return any ? go : f(value); });
}
template <class T> const Snap<T>& FromModelList(const Snap<T>& s) { return s; }
template <class L> auto FromModelList(L* list) {
    return MakeSeq([=](auto&& f) {
        for (int32_t i = 0; list && i < list->Count(); i++) if (!f(ModelElem(list->items[i]))) return false;
        return true;
    });
}

/* new T[n]: a managed array of the game's array type (its type getter: bind::t_<T>__), elements zeroed, made by BRUTE's NewArray
 * (+0x6110: (array runtime type, element size, lengths, rank); data at +0x10; docs/ws-tfex-subclass.md) */
template <class T> inline Arr<T> NewArr(uintptr_t arrayTypeGetter, int32_t n) {
    if (n < 0 || !arrayTypeGetter) return Arr<T>{nullptr, 0, nullptr};
    int32_t len = n;
    void* a = Fn<void* (*)(void*, int32_t, const int32_t*, int32_t)>(0x6110)(TypeOf(arrayTypeGetter), (int32_t)sizeof(T), &len, 1);
    if (!a) return Arr<T>{nullptr, 0, nullptr};
    return Arr<T>{*reinterpret_cast<T**>(reinterpret_cast<uintptr_t>(a) + 0x10), n, a};
}
/* a list of strings inside a model list (ICollection<string[]>: Lobby.RelayedPairs, its relayed pairs) as a LINQ element: a managed
 * string[], the C# element (so Contains / SelectMany over it are an array's); found by ADL from FromModelList */
uintptr_t StringArrayType();   /* System.String[]'s type getter (bind::rt_StringArray) */
template <int N, int M> inline Arr<void*> ModelElem(List<Str<N>, M>& x) {
    auto a = NewArr<void*>(StringArrayType(), x.Count());
    for (int32_t i = 0; a.data && i < x.Count(); i++) a.data[i] = Managed(x.items[i]);
    return a;
}
/* Snap's storage past SnapCap: an object[] of n elements' bytes (System.Object[]'s type getter +0x861530, bind t_Object_49d5) */
template <class T> bool Snap<T>::Grow(int32_t n) {
    static_assert(std::is_trivially_copyable_v<T>, "Snap holds plain values");
    auto a = NewArr<uint64_t>(0x861530, (int32_t)(((int64_t)n * (int64_t)sizeof(T) + 7) / 8));
    if (!a.data) return false;
    T* d = reinterpret_cast<T*>(a.data);
    for (int32_t i = 0; i < count; i++) d[i] = items[i];
    items = d; arr_ = a.obj; cap = n;
    return true;
}
/* Array.Resize(ref a, n): a new array of the same type, the elements that fit copied (.NET's behaviour; null a: a new array) */
template <class T> inline Arr<T> ResizeArr(Arr<T> a, int32_t n, uintptr_t arrayTypeGetter) {
    auto r = NewArr<T>(arrayTypeGetter, n);
    for (int32_t i = 0; r.data && a.data && i < n && i < a.len; i++) r.data[i] = a.data[i];
    return r;
}
/* an array as a game delegate passes it (a managed array: x0 / one register): the Arr's own managed array, or a managed copy of an Arr
 * that has none (UnitEmitter ManagedAbi: through a delegate's Invoke an array crosses as the managed array, not as our 24-byte Arr) */
template <class T> inline void* ManagedOf(const Arr<T>& a, uintptr_t arrayTypeGetter) {
    if (a.obj || !a.data) return a.obj;
    auto r = NewArr<T>(arrayTypeGetter, a.len);
    for (int32_t i = 0; r.data && i < a.len; i++) r.data[i] = a.data[i];
    return r.obj;
}
/* a lambda's captured values (UnitEmitter DelegateObject): copied into a managed object[] (GC-scanned), the delegate's target */
template <class C> inline void* NewClosure(const C& c, uintptr_t objectArrayGetter) {
    auto a = NewArr<uint64_t>(objectArrayGetter, (int32_t)((sizeof(C) + 7) / 8));
    if (!a.data) return nullptr;
    std::memcpy(a.data, &c, sizeof c);
    return a.obj;
}
inline void* ClosureData(void* closure) { return *reinterpret_cast<void**>(reinterpret_cast<uintptr_t>(closure) + 0x10); }
/* s.Split(c): a managed string[] (its type getter: bind::rt_StringArray) */
inline void* Substr(const void* s, int32_t start, int32_t n) {
    void* r = NewString(n);
    if (r && n) std::memcpy(const_cast<char16_t*>(Chars(r)), Chars(s) + start, (size_t)n * 2);
    return r;
}
/* String.Trim / TrimStart / TrimEnd (no chars: C#'s white space, ASCII + U+00A0 here) and StartsWith / EndsWith (ordinal) */
inline bool IsWs(char16_t c) { return c == u' ' || (c >= 9 && c <= 13) || c == 0xa0; }
template <int N> void* Trim(const void* s, const char16_t (&chars)[N], int n, bool start, bool end) {
    if (!s) return nullptr;
    auto hit = [&](char16_t c) { if (n == 0) return IsWs(c); for (int k = 0; k < n; k++) if (chars[k] == c) return true; return false; };
    int32_t a = 0, b = Len(s);
    while (start && a < b && hit(CharAt(s, a))) a++;
    while (end && b > a && hit(CharAt(s, b - 1))) b--;
    return a == 0 && b == Len(s) ? const_cast<void*>(s) : Substr(s, a, b - a);
}
inline bool StartsWith(const void* s, const void* p) { int32_t n = Len(p); if (!s || Len(s) < n) return false; for (int32_t i = 0; i < n; i++) if (CharAt(s, i) != CharAt(p, i)) return false; return true; }
inline bool EndsWith(const void* s, const void* p) { int32_t n = Len(p), m = Len(s); if (!s || m < n) return false; for (int32_t i = 0; i < n; i++) if (CharAt(s, m - n + i) != CharAt(p, i)) return false; return true; }
inline Arr<void*> Split(const void* s, char16_t c, uintptr_t stringArrayGetter) {
    int32_t len = Len(s), parts = 1;
    for (int32_t i = 0; i < len; i++) if (CharAt(s, i) == c) parts++;
    auto a = NewArr<void*>(stringArrayGetter, parts);
    if (!a.data) return a;
    int32_t k = 0, start = 0;
    for (int32_t i = 0; i <= len; i++)
        if (i == len || CharAt(s, i) == c) { a.data[k++] = Substr(s, start, i - start); start = i + 1; }
    return a;
}
/* s.Split(params char[] / ReadOnlySpan<char>) and s.Split(c, RemoveEmptyEntries): at any of the chars, empty parts kept or dropped */
inline Arr<void*> SplitAny(const void* s, const char16_t* cs, int n, bool dropEmpty, uintptr_t stringArrayGetter) {
    auto sep = [&](char16_t c) { for (int k = 0; k < n; k++) if (cs[k] == c) return true; return false; };
    int32_t len = Len(s), parts = 0;
    for (int pass = 0; pass < 2; pass++) {
        Arr<void*> a{};
        if (pass == 1) { a = NewArr<void*>(stringArrayGetter, parts); if (!a.data) return a; }
        int32_t k = 0, start = 0;
        for (int32_t i = 0; i <= len; i++)
            if (i == len || sep(CharAt(s, i))) {
                if (!(dropEmpty && i == start)) { if (pass == 1) a.data[k] = Substr(s, start, i - start); k++; }
                start = i + 1;
            }
        if (pass == 0) parts = k; else return a;
    }
    return Arr<void*>{};
}
/* s.Split(string separator, int count): at most count parts, the last one the rest (C#'s; count < 1 -> none) */
inline Arr<void*> SplitStr(const void* s, const void* sep, int32_t count, uintptr_t stringArrayGetter) {
    int32_t len = Len(s), m = Len(sep);
    auto at = [&](int32_t i) { if (i + m > len || m == 0) return false; for (int32_t j = 0; j < m; j++) if (CharAt(s, i + j) != CharAt(sep, j)) return false; return true; };
    int32_t parts = count < 1 ? 0 : 1;
    for (int32_t i = 0; parts > 0 && parts < count && i < len; i++) if (at(i)) { parts++; i += m - 1; }
    auto a = NewArr<void*>(stringArrayGetter, parts);
    if (!a.data || parts == 0) return a;
    int32_t k = 0, start = 0;
    for (int32_t i = 0; k < parts - 1 && i < len; i++) if (at(i)) { a.data[k++] = Substr(s, start, i - start); start = i + m; i += m - 1; }
    a.data[k] = Substr(s, start, len - start);
    return a;
}
/* System.Version as an int64 whose order is Version's: major << 48 | minor << 32 | (build + 1) << 16 | (revision + 1) (an absent build /
 * revision is -1 in .NET and sorts first); TryParse takes "major.minor[.build[.revision]]" of non-negative numbers below 65535 */
inline bool VersionTryParse(const void* s, int64_t& v) {
    int32_t part[4] = { -1, -1, -1, -1 }; int n = 0, len = Len(s); int64_t cur = -1;
    for (int32_t i = 0; i <= len; i++) {
        char16_t c = i < len ? CharAt(s, i) : u'.';
        if (c >= u'0' && c <= u'9') { cur = (cur < 0 ? 0 : cur) * 10 + (c - u'0'); if (cur > 65534) return false; }
        else if (c == u'.') { if (cur < 0 || n >= 4) return false; part[n++] = (int32_t)cur; cur = -1; }
        else return false;
    }
    if (n < 2) return false;
    v = ((int64_t)part[0] << 48) | ((int64_t)part[1] << 32) | ((int64_t)(part[2] + 1) << 16) | (int64_t)(part[3] + 1);
    return true;
}
inline void* VersionString(int64_t v) {
    char b[48]; int32_t build = (int32_t)((v >> 16) & 0xffff) - 1, rev = (int32_t)(v & 0xffff) - 1;
    int n = std::snprintf(b, sizeof b, "%d.%d", (int)(v >> 48), (int)((v >> 32) & 0xffff));
    if (build >= 0) n += std::snprintf(b + n, sizeof b - n, ".%d", build);
    if (build >= 0 && rev >= 0) n += std::snprintf(b + n, sizeof b - n, ".%d", rev);
    return ManagedUtf8(b, n);
}
/* string.Join(sep, seq): the strings of a sequence (null = "") */
template <class S> inline void* Join(const void* sep, const S& seq) {
    void* acc = nullptr; bool first = true;
    seq([&](void* x) { acc = first ? Concat(acc, x) : Concat(acc, sep, x); first = false; return true; });
    return acc ? acc : Concat(nullptr, nullptr);
}
template <class T> struct IsArrT : std::false_type {};
template <class U> struct IsArrT<Arr<U>> { static constexpr bool value = true; using Elem = U; };
template <class T> inline Arr<T> GameArr(void* managed, uintptr_t getLength) {
    if (!managed) return Arr<T>{nullptr, 0, nullptr};
    if constexpr (IsArrT<T>::value) {
        /* a jagged game array (U[][]: TF.State v0.27's GameStats.Awards): its elements are managed arrays (pointers), each read as a view
         * into the game's array (writes through it land there); the views in the game's GC_malloc memory (+0xde0: scanned, collected) */
        int32_t n = Fn<int32_t (*)(void*)>(getLength)(managed);
        void** el = *reinterpret_cast<void***>(reinterpret_cast<uintptr_t>(managed) + 0x10);
        T* v = n > 0 ? static_cast<T*>(Fn<void* (*)(uint64_t)>(0xde0)(sizeof(T) * (uint64_t)n)) : nullptr;
        if (n > 0 && !v) return Arr<T>{nullptr, 0, managed};
        for (int32_t i = 0; i < n; i++) v[i] = GameArr<typename IsArrT<T>::Elem>(el[i], getLength);
        return Arr<T>{v, n, managed};
    } else {
        return Arr<T>{*reinterpret_cast<T**>(reinterpret_cast<uintptr_t>(managed) + 0x10), Fn<int32_t (*)(void*)>(getLength)(managed), managed};
    }
}
/* a hook's array result (the managed array, void*) passed to a patch's `ref T[] __result` (Teams+'s GetTeamParticleTypes prefix): the
 * patch's gen::Arr view of it, and whatever array the patch leaves there written back when the call's full expression ends */
template <class E> struct ArrRef {
    void*& p; Arr<E> a;
    ArrRef(void*& p_, uintptr_t getLength) : p(p_), a(GameArr<E>(p_, getLength)) {}
    ~ArrRef() { p = a.obj; }
    operator Arr<E>&() { return a; }
};

/* ---- classes of ours (docs/ws-tfex-subclass.md "What a BRUTE class is"): a runtime type record (a copy of the base's), a
 * descriptor and a vtable in our module, made once outside any rollback window. The base is a game class (its getter, descriptor),
 * another class of ours (its Class), or System.Object. ---- */
struct Class {
    void* type = nullptr;
    void* base = nullptr;
    uintptr_t desc[3] = {};
    uintptr_t vtable[96] = {};
    uint32_t vtableBytes = 0;
    uint8_t method[64][0x60] = {};   /* method records for the delegates we make (declaring type = ours), one per site */
};
/* baseType: the base's runtime type; baseDesc: its descriptor (3 words); baseGetter: a function returning baseType (BRUTE's `is` walks
 * it); vtableBytes: ours (the base's + our new virtual methods); refs: instances hold GC references (scanned) */
bool MakeClass(Class& c, void* baseType, const uintptr_t* baseDesc, uintptr_t baseGetter, uint32_t baseVtableBytes, uint32_t vtableBytes,
               uint32_t size, bool refs, const char16_t* name, const char16_t* ns, const char16_t* fullName, const char16_t* aqName, uint32_t hash);
/* a static unit's delegates: their method records name a class made on first use (System.Object's base, no instances) */
Class& HostClass(Class& c, const char16_t* name, uintptr_t objectGetter, uintptr_t objectDesc, uint32_t objectVtableBytes);
inline void Override(Class& c, uint32_t slot, void* fn) { c.vtable[slot / 8] = reinterpret_cast<uintptr_t>(fn); }
inline void* New(Class& c) { return Fn<void* (*)(void*)>(game::brute::NewObject)(c.type); }
/* C# `new Action(target.Method)` / a lambda: a delegate of delegateType calling fn(target, args...) (or fn(args...) without a target) */
void* NewDelegate(Class& c, int methodIndex, uintptr_t delegateTypeGetter, void* target, void* fn);
/* a delegate of a type the game never compiled (AIJimmy's Func<object, Point, Vector2, bool>; made as a stand-in type): only our code
 * invokes it, so its function is called directly, the target first when it has one, as the delegate's Invoke would (game::brute's
 * DlgFn +0x10, DlgTarget +0x20) */
template <class R, class... A> inline R DlgInvoke(void* d, std::type_identity_t<A>... a) {
    void* f = Fld<void*>(d, 0x10); void* t = Fld<void*>(d, 0x20);
    if (t) return reinterpret_cast<R (*)(void*, A...)>(f)(t, a...);
    return reinterpret_cast<R (*)(A...)>(f)(a...);
}

/* ---- System.DateTime / TimeSpan: 64-bit counts of 100 ns ticks, as .NET's. UtcNow counts from boot (the Switch's tick): TF.EX's
 * UI uses differences (timers, deadlines), never the calendar. ---- */
int64_t UtcNowTicks();
constexpr int64_t TicksPerSecond = 10000000LL, TicksPerMillisecond = 10000LL;
inline int64_t TicksFromSeconds(double s) { return (int64_t)(s * TicksPerSecond); }
inline int64_t TicksFromMilliseconds(double ms) { return (int64_t)(ms * TicksPerMillisecond); }
inline double TotalSeconds(int64_t t) { return (double)t / TicksPerSecond; }
inline double TotalMilliseconds(int64_t t) { return (double)t / TicksPerMillisecond; }

/* ---- tuples (System.ValueTuple) ---- */
template <class A, class B> struct Tup2 {
    A Item1; B Item2;
    bool operator==(const Tup2& o) const { return Item1 == o.Item1 && Item2 == o.Item2; }   /* C#'s ValueTuple equality (a value map's key: (int, int)) */
};
/* `using var x`: x's Dispose() when the scope ends */
template <class F> struct Defer { F f; ~Defer() { f(); } };
template <class F> Defer<F> MakeDefer(F f) { return Defer<F>{f}; }
/* ElementAtOrDefault(i) over a sequence */
template <class T, class S> T ElementAtOrDefault(S src, int32_t i, T def) { T r = def; int32_t k = 0; src([&](T x) { if (k++ == i) { r = x; return false; } return true; }); return r; }
template <class A, class B, class C> struct Tup3 { A Item1; B Item2; C Item3; };
template <class A, class B, class C, class D> struct Tup4 {
    A Item1; B Item2; C Item3; D Item4;
    bool operator==(const Tup4& o) const { return Item1 == o.Item1 && Item2 == o.Item2 && Item3 == o.Item3 && Item4 == o.Item4; }   /* as Tup2's (Teams+'s Recolor cache key) */
};
template <class A, class B, class C, class D, class E> struct Tup5 { A Item1; B Item2; C Item3; D Item4; E Item5; };
template <class A, class B, class C, class D, class E, class F> struct Tup6 { A Item1; B Item2; C Item3; D Item4; E Item5; F Item6; };
template <class A, class B, class C, class D, class E, class F, class G> struct Tup7 { A Item1; B Item2; C Item3; D Item4; E Item5; F Item6; G Item7; };

/* ---- platform ---- */
int64_t TickCount64();                                    /* Environment.TickCount64: milliseconds since boot */

} // namespace tfex::gen
