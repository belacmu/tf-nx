/* A small JSON reader and writer without heap allocation, for TF.EX's lobby messages (JSON objects whose first key is the message
 * name; TF.EX parses JSON through MessagePack, so numbers that are integers stay integers). */
#pragma once
#include <cstdint>
#include <cstddef>

namespace tfex::json {

/* ---- Reading: parse into a caller-provided node array (a DOM in place; strings point into the source, still escaped) ---- */
enum class Type : uint8_t { Null, Bool, Number, String, Array, Object };
struct Node {
    Type type;
    bool b;
    double num;
    const char* str;      /* String: the raw characters between the quotes (escapes not resolved); Object member: the key */
    int strLen;
    int first;            /* Array/Object: index of the first child, -1 if empty */
    int next;             /* the next sibling, -1 at the end */
    const char* key;      /* when this node is an object member */
    int keyLen;
    const char* src;      /* Object/Array: its whole text in the source, brackets included */
    int srcLen;
};
struct Doc {
    Node* nodes;
    int cap, count;
    bool Parse(const char* text, int len);   /* false on a syntax error or when out of nodes */
    const Node* Root() const { return count ? &nodes[0] : nullptr; }
    const Node* Get(const Node* obj, const char* key) const;   /* member by key, or nullptr */
    const Node* At(const Node* arr, int i) const;
    int Size(const Node* arr) const;
    const Node* Child(const Node* n) const { return n && n->first >= 0 ? &nodes[n->first] : nullptr; }
    const Node* Next(const Node* n) const { return n && n->next >= 0 ? &nodes[n->next] : nullptr; }
    /* typed getters with defaults */
    int64_t Int(const Node* obj, const char* key, int64_t def = 0) const;
    bool Bool(const Node* obj, const char* key, bool def = false) const;
    /* copies a string member, unescaped (\" \\ \/ \n \t, \uXXXX as '?' when not ASCII), always terminated; returns its length */
    int Str(const Node* obj, const char* key, char* out, int cap) const;
};
int Unescape(const char* s, int n, char* out, int cap);

/* ---- Writing into a fixed buffer ---- */
struct Writer {
    char* buf;
    int cap, len;
    bool ok;
    bool needComma[32];
    int depth;
    void Init(char* b, int c) { buf = b; cap = c; len = 0; ok = true; depth = 0; needComma[0] = false; buf[0] = 0; }
    void Raw(const char* s, int n);
    void Raw(const char* s);
    void Sep();
    void BeginObject(const char* key = nullptr);
    void EndObject();
    void BeginArray(const char* key = nullptr);
    void EndArray();
    void Key(const char* key);
    void String(const char* key, const char* value);
    void Int(const char* key, int64_t value);
    void Double(const char* key, double value);   /* %.17g (it reads back the same); NaN / infinity: null (JSON has none) */
    void Bool(const char* key, bool value);
    void Null(const char* key);
    void Str(const char* value);   /* an array element */
};

} // namespace tfex::json
