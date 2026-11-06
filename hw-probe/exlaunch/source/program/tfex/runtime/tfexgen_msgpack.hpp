/* TF.EX's MessagePackSerializer, the way TF.EX uses it: every message is a model serialized and at once converted to JSON (or the
 * reverse), so the bytes in between only carry the text. Here they carry UTF-8 JSON: Serialize(model) = JWrite into a managed byte[],
 * ConvertToJson(bytes) = a managed string of it, ConvertFromJson(string) = its UTF-8 bytes, Deserialize<T>(bytes) = a model filled by
 * JRead (tfexgen_Models.hpp's, found by argument-dependent lookup). The translator maps the calls (UnitEmitter.Expr.cs MessagePack). */
#pragma once
#include "tfexgen_code_rt.hpp"
#include "tfexgen_rt.hpp"

namespace tfex::gen {
Arr<uint8_t> BytesOf(const char* s, int n, uintptr_t byteArrayGetter);   /* a managed byte[] holding s */
void* JsonString(Arr<uint8_t> bytes);                                    /* ConvertToJson: a managed string of UTF-8 bytes (no length cap) */
Arr<uint8_t> JsonUtf8(const void* str, uintptr_t byteArrayGetter);       /* ConvertFromJson: the string's UTF-8 bytes */
const Doc* ParseJsonBytes(Arr<uint8_t> bytes);                           /* the parsed document (one, reused per call), or null */

char* JsonScratch(int* cap);   /* one 300 KB buffer for every model type (code_rt): a SkinChunk message is ~264 KB */
template <class T> Arr<uint8_t> JsonBytes(const T* v, uintptr_t byteArrayGetter) {
    int cap = 0;
    char* buf = JsonScratch(&cap);
    Writer w; w.Init(buf, cap);
    if (v) JWrite(w, nullptr, *v); else w.Raw("null");
    return BytesOf(buf, w.ok ? w.len : 0, byteArrayGetter);
}
template <class T> T* FromJsonBytes(Arr<uint8_t> bytes) {
    T* r = NewModel<T>();
    if (const Doc* d = ParseJsonBytes(bytes)) JRead(*d, d->Root(), *r);
    return r;
}
} // namespace tfex::gen
