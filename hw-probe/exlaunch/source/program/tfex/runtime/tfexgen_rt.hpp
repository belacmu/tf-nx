/* Runtime support for the code generated from TF.EX (tools/tfex/translator; copied into tfex/gen/ by the translator, not edited
 * there). C# reference semantics on fixed storage: a string, a collection or a class-typed property can be null, so each holder
 * carries a null flag. JSON as TF.EX reads and writes it (MessagePack ConvertFromJson / ConvertToJson), on tfex/Json.hpp. */
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include "../Json.hpp"

namespace tfex::gen {
using json::Doc;
using json::Node;
using json::Writer;

/* a model string kept as a managed System.String (translate.toml [models.caps] "Class.Prop" = "managed": TF.EX's SkinChunk.Data, up
 * to 262 KB, where an inline Str<N> in every ring copy of the model would cost megabytes). The reference sits in a GC-scanned
 * uncollectable slot (made at the first Set, one per model object, never freed: the models are fixed rings). code_rt implements these. */
void** NewRootSlot();
void* ManagedFromJsonString(const char* raw, int n);    /* a JSON string's raw text (escapes undone) as a managed string */
void JsonWriteManaged(Writer& w, const char* key, const void* managed);
struct MStr {
    void** slot = nullptr;
    MStr() = default;
    MStr(void* v) { Set(v); }
    MStr(const char*) {}   /* a C# initializer ("" for SkinChunk.Data): models are made before the GC exists, so null until set */
    void Set(void* v) { if (!slot) { if (!v) return; slot = NewRootSlot(); if (!slot) return; } *slot = v; }
    void* Get() const { return slot ? *slot : nullptr; }
    MStr& operator=(void* v) { Set(v); return *this; }
    MStr(const MStr& o) { Set(o.Get()); }
    MStr& operator=(const MStr& o) { if (this != &o) { if (!o.Get() && slot) *slot = nullptr; else Set(o.Get()); } return *this; }
    operator void*() const { return Get(); }
};
inline void JRead(const Doc&, const Node* n, MStr& v) {
    if (!n) return;
    if (n->type == json::Type::Null) { v.Set(nullptr); return; }
    if (n->type == json::Type::String) v.Set(ManagedFromJsonString(n->str, n->strLen));
}
inline void JWrite(Writer& w, const char* key, const MStr& v) { JsonWriteManaged(w, key, v.Get()); }

/* string (UTF-8, at most N-1 bytes; longer values are cut) */
template <int N> struct Str {
    bool null = false;
    char s[N] = {};
    Str() = default;
    Str(const char* v) { Set(v); }
    static Str Null() { Str r; r.null = true; return r; }
    void Set(const char* v) {
        null = !v;
        int i = 0;
        if (v) for (; v[i] && i < N - 1; i++) s[i] = v[i];
        s[i] = 0;
    }
    const char* c_str() const { return null ? nullptr : s; }
    bool IsNullOrEmpty() const { return null || !s[0]; }
    bool operator==(const char* o) const { return o ? !null && !std::strcmp(s, o) : null; }
    template <int M> bool operator==(const Str<M>& o) const { return null == o.null && !std::strcmp(s, o.s); }
    template <class O> bool operator!=(const O& o) const { return !(*this == o); }
};

void PoolFull(const char* what, int capacity, void* site);   /* tfexgen_code_rt.cpp: the logged pool error */
/* ICollection<T> / List<T> / T[] (at most N elements, translate.toml [models.caps]; one more is a pool error: logged, dropped) */
template <class T, int N> struct List {
    bool null = false;
    int count = 0;
    T items[N];
    static List Null() { List r; r.null = true; return r; }
    T* Add() {
        if (count >= N) { PoolFull("a model List (translate.toml [models.caps])", N, __builtin_return_address(0)); return nullptr; }
        static const T empty{};   /* a model item can be large: no stack temporary */
        items[count] = empty;
        return &items[count++];
    }
    void Add(const T& v) { if (T* p = Add()) *p = v; }
    int Count() const { return null ? 0 : count; }
    void Clear() { count = 0; null = false; }
    T& operator[](int i) { return items[i]; }
    const T& operator[](int i) const { return items[i]; }
    T* begin() { return items; }
    T* end() { return items + Count(); }
    const T* begin() const { return items; }
    const T* end() const { return items + Count(); }
};

/* Nullable<T>, and a class-typed property (a reference: null or an object) */
/* C#'s Nullable<T> in the game's own layout: BRUTE is Mono's corlib, whose Nullable<T> is { T value; bool has_value; } (the PC's .NET has
 * hasValue first). Game fields are read and written as gen::Opt in place (Fld<gen::Opt<float>>: PlayerGhost.lastDir, value at +0x1f0, the
 * flag at +0x1f4), so the order matters: with the flag first, TF.State read a float's byte as HasValue and its load wrote the flag into
 * the float (a ghost's state misread, a crash in the next load; the parity sweep, 2026-10-24) */
template <class T> struct Opt {
    T value{};
    bool has = false;
    Opt() = default;
    Opt(const T& v) : value(v), has(true) {}
    Opt(decltype(nullptr)) {}   /* C#: x = null on a Nullable<T> */
    template <class U> static Opt From(const Opt<U>& o) { Opt r; r.has = o.has; if (o.has) r.value = (T)o.value; return r; }
    static Opt Some() { Opt r; r.has = true; return r; }
    bool HasValue() const { return has; }
    T* operator->() { return &value; }
    const T* operator->() const { return &value; }
    T& operator*() { return value; }
    const T& operator*() const { return value; }
};
/* the game's Nullable<T> layout (Mono: the value, then the flag after it), checked at build time for the shapes the game uses */
static_assert(offsetof(Opt<float>, value) == 0 && offsetof(Opt<float>, has) == 4 && sizeof(Opt<float>) == 8, "gen::Opt<float> must be Mono's Nullable<float>");
static_assert(offsetof(Opt<double>, has) == 8 && sizeof(Opt<double>) == 16, "gen::Opt<double> must be Mono's Nullable<double>");
static_assert(offsetof(Opt<int32_t>, has) == 4 && sizeof(Opt<int32_t>) == 8, "gen::Opt<int> must be Mono's Nullable<int>");

/* Dictionary<string, string> (at most N entries) */
template <int N, int K, int V> struct StrMap {
    bool null = false;
    int count = 0;
    Str<K> keys[N];
    Str<V> values[N];
    int Count() const { return null ? 0 : count; }
    const Str<V>* Get(const char* k) const {
        for (int i = 0; i < Count(); i++) if (keys[i] == k) return &values[i];
        return nullptr;
    }
    void Set(const char* k, const char* v) {
        for (int i = 0; i < count; i++) if (keys[i] == k) { values[i].Set(v); return; }
        if (count < N) { keys[count].Set(k); values[count].Set(v); count++; }
    }
};

/* Dictionary<string, V> for any other value (a model: PlayerProfile.Stats' StatTotal; a number: SeatStats.RawStats), at most N entries.
 * More entries than N in a message: the rest dropped and logged (MapFull: a fixed-size store running out is an ERROR, never silent) */
void MapFull(int cap, int got);
template <int N, int K, class V> struct KeyMap {
    bool null = false;
    int count = 0;
    Str<K> keys[N];
    V values[N];
    int Count() const { return null ? 0 : count; }
    const V* Get(const char* k) const {
        for (int i = 0; i < Count(); i++) if (keys[i] == k) return &values[i];
        return nullptr;
    }
};

/* ---- JSON reading: a missing member leaves the C# initializer's value; null sets null where C# can hold it ---- */
inline bool IsNull(const Node* n) { return n && n->type == json::Type::Null; }
inline void JRead(const Doc&, const Node* n, int32_t& v) { if (n && n->type == json::Type::Number) v = (int32_t)(int64_t)n->num; }
inline void JRead(const Doc&, const Node* n, uint32_t& v) { if (n && n->type == json::Type::Number) v = (uint32_t)(int64_t)n->num; }
inline void JRead(const Doc&, const Node* n, int64_t& v) { if (n && n->type == json::Type::Number) v = (int64_t)n->num; }
inline void JRead(const Doc&, const Node* n, bool& v) { if (n && n->type == json::Type::Bool) v = n->b; }
inline void JRead(const Doc&, const Node* n, double& v) { if (n && n->type == json::Type::Number) v = n->num; }
template <int N> void JRead(const Doc&, const Node* n, Str<N>& v) {
    if (!n) return;
    if (IsNull(n)) { v.Set(nullptr); return; }
    if (n->type == json::Type::String) { json::Unescape(n->str, n->strLen, v.s, N); v.null = false; }
}
template <class T, int N> void JRead(const Doc& d, const Node* n, List<T, N>& v) {
    if (!n) return;
    v.Clear();
    if (IsNull(n)) { v.null = true; return; }
    for (const Node* c = d.Child(n); c; c = d.Next(c)) {
        T* t = v.Add();
        if (!t) break;
        JRead(d, c, *t);
    }
}
template <class T> void JRead(const Doc& d, const Node* n, Opt<T>& v) {
    if (!n) return;
    if (IsNull(n)) { v.has = false; return; }
    v.has = true;
    v.value = T{};
    JRead(d, n, v.value);
}
template <int N, int K, class V> void JRead(const Doc& d, const Node* n, KeyMap<N, K, V>& v) {
    if (!n) return;
    v.count = 0;
    v.null = IsNull(n);
    int got = 0;
    for (const Node* c = d.Child(n); c; c = d.Next(c), got++) {
        if (v.count >= N) continue;
        json::Unescape(c->key, c->keyLen, v.keys[v.count].s, K);
        v.keys[v.count].null = false;
        v.values[v.count] = V{};
        JRead(d, c, v.values[v.count]);
        v.count++;
    }
    if (got > N) MapFull(N, got);
}
template <int N, int K, int V> void JRead(const Doc& d, const Node* n, StrMap<N, K, V>& v) {
    if (!n) return;
    v.count = 0;
    v.null = IsNull(n);
    for (const Node* c = d.Child(n); c && v.count < N; c = d.Next(c)) {
        json::Unescape(c->key, c->keyLen, v.keys[v.count].s, K);
        v.keys[v.count].null = false;
        v.values[v.count] = Str<V>{};
        JRead(d, c, v.values[v.count]);
        v.count++;
    }
}

/* ---- JSON writing (key nullptr = an array element) ---- */
/* a string as MessagePack's ConvertToJson writes it: \" \\ \b \f \n \r \t, other control characters \uXXXX, the rest as is (UTF-8) */
inline void JString(Writer& w, const char* k, const char* v) {
    w.Key(k);
    w.Raw("\"");
    for (const char* p = v; *p; p++) {
        const char* e = nullptr;
        switch (*p) {
            case '"': e = "\\\""; break;
            case '\\': e = "\\\\"; break;
            case '\b': e = "\\b"; break;
            case '\f': e = "\\f"; break;
            case '\n': e = "\\n"; break;
            case '\r': e = "\\r"; break;
            case '\t': e = "\\t"; break;
        }
        if (e) { w.Raw(e); continue; }
        if ((unsigned char)*p < 0x20) {
            static const char hex[] = "0123456789abcdef";
            char u[7] = { '\\', 'u', '0', '0', hex[(*p >> 4) & 0xf], hex[*p & 0xf], 0 };
            w.Raw(u);
            continue;
        }
        w.Raw(p, 1);
    }
    w.Raw("\"");
}
inline void JWrite(Writer& w, const char* k, int32_t v) { w.Int(k, v); }
inline void JWrite(Writer& w, const char* k, uint32_t v) { w.Int(k, v); }
inline void JWrite(Writer& w, const char* k, int64_t v) { w.Int(k, v); }
inline void JWrite(Writer& w, const char* k, bool v) { w.Bool(k, v); }
inline void JWrite(Writer& w, const char* k, double v) { w.Double(k, v); }
template <int N> void JWrite(Writer& w, const char* k, const Str<N>& v) {
    if (v.null) w.Null(k); else JString(w, k, v.s);
}
template <class T, int N> void JWrite(Writer& w, const char* k, const List<T, N>& v) {
    if (v.null) { w.Null(k); return; }
    w.BeginArray(k);
    for (int i = 0; i < v.count; i++) JWrite(w, nullptr, v.items[i]);
    w.EndArray();
}
template <class T> void JWrite(Writer& w, const char* k, const Opt<T>& v) {
    if (!v.has) w.Null(k); else JWrite(w, k, v.value);
}
template <int N, int K, class V> void JWrite(Writer& w, const char* k, const KeyMap<N, K, V>& v) {
    if (v.null) { w.Null(k); return; }
    w.BeginObject(k);
    for (int i = 0; i < v.count; i++) JWrite(w, v.keys[i].s, v.values[i]);
    w.EndObject();
}
template <int N, int K, int V> void JWrite(Writer& w, const char* k, const StrMap<N, K, V>& v) {
    if (v.null) { w.Null(k); return; }
    w.BeginObject(k);
    for (int i = 0; i < v.count; i++) JWrite(w, v.keys[i].s, v.values[i]);
    w.EndObject();
}

} // namespace tfex::gen
