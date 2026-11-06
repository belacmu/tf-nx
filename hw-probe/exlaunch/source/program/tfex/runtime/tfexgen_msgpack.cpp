/* TF.EX's MessagePackSerializer as JSON bytes (tfexgen_msgpack.hpp). */
#include "tfexgen_msgpack.hpp"

namespace tfex::gen {

Arr<uint8_t> BytesOf(const char* s, int n, uintptr_t byteArrayGetter) {
    auto a = NewArr<uint8_t>(byteArrayGetter, n);
    if (a.data && n > 0) std::memcpy(a.data, s, (size_t)n);
    return a;
}

void* JsonString(Arr<uint8_t> bytes) {   /* UTF-8 -> a managed UTF-16 string: count, allocate, fill (BMP; others as '?') */
    const uint8_t* s = bytes.data;
    int n = bytes.data ? bytes.len : 0, k = 0;
    for (int i = 0; i < n; k++) { unsigned c = s[i]; i += c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 1; }
    void* r = NewString(k);
    if (!r) return nullptr;
    char16_t* out = const_cast<char16_t*>(Chars(r));
    k = 0;
    for (int i = 0; i < n;) {
        unsigned c = s[i];
        if (c < 0x80) { out[k++] = (char16_t)c; i++; }
        else if ((c >> 5) == 6 && i + 1 < n) { out[k++] = (char16_t)(((c & 0x1f) << 6) | (s[i + 1] & 0x3f)); i += 2; }
        else if ((c >> 4) == 14 && i + 2 < n) { out[k++] = (char16_t)(((c & 0x0f) << 12) | ((s[i + 1] & 0x3f) << 6) | (s[i + 2] & 0x3f)); i += 3; }
        else { out[k++] = u'?'; i += (c >> 3) == 30 ? 4 : 1; }
    }
    return r;
}

Arr<uint8_t> JsonUtf8(const void* str, uintptr_t byteArrayGetter) {
    int len = Len(str), n = 0;
    const char16_t* c = Chars(str);
    for (int i = 0; str && i < len; i++) n += c[i] < 0x80 ? 1 : c[i] < 0x800 ? 2 : 3;
    auto a = NewArr<uint8_t>(byteArrayGetter, n);
    if (!a.data) return a;
    int k = 0;
    for (int i = 0; i < len; i++) {
        unsigned u = c[i];
        if (u < 0x80) a.data[k++] = (uint8_t)u;
        else if (u < 0x800) { a.data[k++] = (uint8_t)(0xc0 | u >> 6); a.data[k++] = (uint8_t)(0x80 | (u & 0x3f)); }
        else { a.data[k++] = (uint8_t)(0xe0 | u >> 12); a.data[k++] = (uint8_t)(0x80 | ((u >> 6) & 0x3f)); a.data[k++] = (uint8_t)(0x80 | (u & 0x3f)); }
    }
    return a;
}

const Doc* ParseJsonBytes(Arr<uint8_t> bytes) {   /* one document for generated code (the game thread); the net thread has its own */
    static Node nodes[4096];
    static Doc doc{ nodes, 4096, 0 };
    if (!bytes.data || bytes.len <= 0) return nullptr;
    return doc.Parse(reinterpret_cast<const char*>(bytes.data), bytes.len) ? &doc : nullptr;
}

} // namespace tfex::gen
