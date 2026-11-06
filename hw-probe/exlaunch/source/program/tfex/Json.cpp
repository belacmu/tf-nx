#include "Json.hpp"
#include <cstdio>
#include <cstring>

namespace tfex::json {

namespace {
/* integers, decimals and exponents (no libc strtod: the module links without the C library's extras) */
double Number(const char* p, int n) {
    int i = 0;
    bool neg = i < n && p[i] == '-';
    if (neg || (i < n && p[i] == '+')) i++;
    double v = 0;
    while (i < n && p[i] >= '0' && p[i] <= '9') v = v * 10 + (p[i++] - '0');
    if (i < n && p[i] == '.') {
        i++;
        double f = 0.1;
        while (i < n && p[i] >= '0' && p[i] <= '9') { v += (p[i++] - '0') * f; f *= 0.1; }
    }
    if (i < n && (p[i] == 'e' || p[i] == 'E')) {
        i++;
        bool en = i < n && p[i] == '-';
        if (en || (i < n && p[i] == '+')) i++;
        int e = 0;
        while (i < n && p[i] >= '0' && p[i] <= '9') e = e * 10 + (p[i++] - '0');
        while (e-- > 0) v = en ? v / 10 : v * 10;
    }
    return neg ? -v : v;
}
struct Parser {
    const char* s;
    int n, i;
    Doc* d;
    void Ws() { while (i < n && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) i++; }
    int NewNode(Type t) {
        if (d->count >= d->cap) return -1;
        Node& x = d->nodes[d->count];
        x.type = t; x.b = false; x.num = 0; x.str = nullptr; x.strLen = 0; x.first = -1; x.next = -1; x.key = nullptr; x.keyLen = 0; x.src = s + i; x.srcLen = 0;
        return d->count++;
    }
    bool StrSpan(const char** out, int* outLen) {
        if (i >= n || s[i] != '"') return false;
        int st = ++i;
        while (i < n && s[i] != '"') { if (s[i] == '\\') i++; i++; }
        if (i >= n) return false;
        *out = s + st; *outLen = i - st;
        i++;
        return true;
    }
    int Value() {
        Ws();
        if (i >= n) return -1;
        char c = s[i];
        if (c == '{' || c == '[') {
            bool obj = c == '{';
            int me = NewNode(obj ? Type::Object : Type::Array);
            if (me < 0) return -1;
            i++;
            Ws();
            int prev = -1;
            if (i < n && s[i] == (obj ? '}' : ']')) { i++; d->nodes[me].srcLen = (int)(s + i - d->nodes[me].src); return me; }
            for (;;) {
                const char* k = nullptr; int kl = 0;
                if (obj) {
                    Ws();
                    if (!StrSpan(&k, &kl)) return -1;
                    Ws();
                    if (i >= n || s[i] != ':') return -1;
                    i++;
                }
                int ch = Value();
                if (ch < 0) return -1;
                d->nodes[ch].key = k; d->nodes[ch].keyLen = kl;
                if (prev < 0) d->nodes[me].first = ch; else d->nodes[prev].next = ch;
                prev = ch;
                Ws();
                if (i < n && s[i] == ',') { i++; continue; }
                if (i < n && s[i] == (obj ? '}' : ']')) { i++; d->nodes[me].srcLen = (int)(s + i - d->nodes[me].src); return me; }
                return -1;
            }
        }
        if (c == '"') {
            int me = NewNode(Type::String);
            if (me < 0) return -1;
            if (!StrSpan(&d->nodes[me].str, &d->nodes[me].strLen)) return -1;
            return me;
        }
        if (!std::strncmp(s + i, "true", 4)) { int me = NewNode(Type::Bool); if (me >= 0) d->nodes[me].b = true; i += 4; return me; }
        if (!std::strncmp(s + i, "false", 5)) { int me = NewNode(Type::Bool); i += 5; return me; }
        if (!std::strncmp(s + i, "null", 4)) { int me = NewNode(Type::Null); i += 4; return me; }
        if (c == '-' || (c >= '0' && c <= '9')) {
            int st = i;
            while (i < n && (s[i] == '-' || s[i] == '+' || s[i] == '.' || s[i] == 'e' || s[i] == 'E' || (s[i] >= '0' && s[i] <= '9'))) i++;
            int me = NewNode(Type::Number);
            if (me >= 0) d->nodes[me].num = Number(s + st, i - st);
            return me;
        }
        return -1;
    }
};
bool KeyIs(const Node* n, const char* key) {
    int kl = (int)std::strlen(key);
    return n->key && n->keyLen == kl && !std::memcmp(n->key, key, (size_t)kl);
}
} // namespace

bool Doc::Parse(const char* text, int len) {
    count = 0;
    Parser p{ text, len, 0, this };
    return p.Value() == 0;
}
const Node* Doc::Get(const Node* obj, const char* key) const {
    if (!obj || obj->type != Type::Object) return nullptr;
    for (const Node* c = Child(obj); c; c = Next(c)) if (KeyIs(c, key)) return c;
    return nullptr;
}
const Node* Doc::At(const Node* arr, int i) const {
    const Node* c = arr ? Child(arr) : nullptr;
    for (; c && i > 0; i--) c = Next(c);
    return c;
}
int Doc::Size(const Node* arr) const {
    int k = 0;
    for (const Node* c = arr ? Child(arr) : nullptr; c; c = Next(c)) k++;
    return k;
}
int64_t Doc::Int(const Node* obj, const char* key, int64_t def) const {
    const Node* v = Get(obj, key);
    return v && v->type == Type::Number ? (int64_t)v->num : def;
}
bool Doc::Bool(const Node* obj, const char* key, bool def) const {
    const Node* v = Get(obj, key);
    return v && v->type == Type::Bool ? v->b : def;
}
int Unescape(const char* s, int n, char* out, int cap) {
    int o = 0;
    for (int i = 0; i < n && o < cap - 1; i++) {
        char c = s[i];
        if (c == '\\' && i + 1 < n) {
            char e = s[++i];
            if (e == 'n') c = '\n'; else if (e == 't') c = '\t'; else if (e == 'r') c = '\r'; else if (e == 'b') c = '\b'; else if (e == 'f') c = '\f';
            else if (e == 'u') {
                unsigned v = 0;
                for (int k = 0; k < 4 && i + 1 < n; k++) { char h = s[++i]; v = v * 16 + (unsigned)(h <= '9' ? h - '0' : (h | 32) - 'a' + 10); }
                c = v < 0x80 ? (char)v : '?';
            } else c = e;
        }
        out[o++] = c;
    }
    out[o] = 0;
    return o;
}
int Doc::Str(const Node* obj, const char* key, char* out, int cap) const {
    const Node* v = Get(obj, key);
    if (!v || v->type != Type::String) { if (cap) out[0] = 0; return 0; }
    return Unescape(v->str, v->strLen, out, cap);
}

void Writer::Raw(const char* s, int n) {
    if (len + n >= cap) { ok = false; return; }
    std::memcpy(buf + len, s, (size_t)n); len += n; buf[len] = 0;
}
void Writer::Raw(const char* s) { Raw(s, (int)std::strlen(s)); }
void Writer::Sep() { if (needComma[depth]) Raw(","); needComma[depth] = true; }
void Writer::Key(const char* key) {
    Sep();
    if (!key) return;
    Raw("\""); Raw(key); Raw("\":");
}
void Writer::BeginObject(const char* key) { Key(key); Raw("{"); if (depth < 31) needComma[++depth] = false; }
void Writer::EndObject() { Raw("}"); if (depth) depth--; }
void Writer::BeginArray(const char* key) { Key(key); Raw("["); if (depth < 31) needComma[++depth] = false; }
void Writer::EndArray() { Raw("]"); if (depth) depth--; }
static void Quoted(Writer* w, const char* v) {
    w->Raw("\"");
    for (const char* p = v; *p; p++) {
        char c = *p;
        const char* e = c == '"' ? "\\\"" : c == '\\' ? "\\\\" : c == '\b' ? "\\b" : c == '\f' ? "\\f" : c == '\n' ? "\\n" : c == '\r' ? "\\r" : c == '\t' ? "\\t" : nullptr;
        if (e) w->Raw(e);   /* as MessagePack's ConvertToJson writes them (TF.EX) */
        else if ((unsigned char)c < 0x20) { char e[8]; std::snprintf(e, sizeof e, "\\u%04x", c); w->Raw(e); }
        else w->Raw(&c, 1);
    }
    w->Raw("\"");
}
void Writer::String(const char* key, const char* value) { Key(key); Quoted(this, value ? value : ""); }
void Writer::Str(const char* value) { Sep(); Quoted(this, value ? value : ""); }
void Writer::Int(const char* key, int64_t value) { Key(key); char t[24]; std::snprintf(t, sizeof t, "%lld", (long long)value); Raw(t); }
void Writer::Double(const char* key, double value) {
    if (value != value || value - value != 0.0) { Null(key); return; }
    Key(key); char t[32]; std::snprintf(t, sizeof t, "%.17g", value); Raw(t);
}
void Writer::Bool(const char* key, bool value) { Key(key); Raw(value ? "true" : "false"); }
void Writer::Null(const char* key) { Key(key); Raw("null"); }

} // namespace tfex::json
