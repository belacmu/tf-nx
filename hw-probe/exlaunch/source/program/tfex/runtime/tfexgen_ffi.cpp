/* tfexgen_ffi.hpp's functions (the C side of TF.EX's GGRS FFI wrappers). */
#include "tfexgen_ffi.hpp"
#include "tfexgen_binding.hpp"
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include "../../hwlog.hpp"

namespace tfex::gen::ffi {
gen::Arr<void*> EventStrings(const st::Events& events) {
    const char* const* s = reinterpret_cast<const char* const*>(events.data);
    int32_t n = s && events.len > 0 ? events.len : 0;
    auto a = gen::NewArr<void*>(bind::rt_StringArray, n);
    for (int32_t i = 0; a.data && i < n; i++) a.data[i] = s[i] ? gen::ManagedUtf8(s[i], (int)std::strlen(s[i])) : nullptr;
    return a;
}
void* StatusInfo(const st::Status& s) {
    const char* c = reinterpret_cast<const char*>(s.info);
    return c ? gen::ManagedUtf8(c, (int)std::strlen(c)) : nullptr;
}
namespace {
constexpr int kBufs = 2, kBufBytes = 4096;
struct Buf { bool used; uint64_t len; char data[kBufBytes]; };
Buf g_bufs[kBufs];
}
void* BytesOfString(void* managed) {
    for (Buf& b : g_bufs) {
        if (b.used) continue;
        int n = managed ? gen::Utf8Of(managed, b.data, kBufBytes) : 0;
        if (managed && n >= kBufBytes - 1) hw::Log("[ffi] BytesOfString: %d bytes don't fit the %d-byte buffer (truncated)", n, kBufBytes);
        b.used = true; b.len = (uint64_t)std::strlen(b.data);
        { static int logged = 0; if (logged++ < 16) hw::Log("[ffi] netplay_init config: %s", b.data); }   /* (the switch-over check: Fps 60, the players, the room) */
        return &b;
    }
    hw::Log("[ffi] POOL FULL: BytesOfString: all %d buffers in use", kBufs);
    return nullptr;
}
st::SafeBytesFFI BytesFFI(void* bytes) {
    auto* b = static_cast<Buf*>(bytes);
    return b ? st::SafeBytesFFI{reinterpret_cast<intptr_t>(b->data), (uintptr_t)b->len} : st::SafeBytesFFI{0, 0};
}
void FreeBytes(void* bytes) { if (auto* b = static_cast<Buf*>(bytes)) b->used = false; }
gen::VList<st::Input>* InputList(const st::Inputs& in) {
    auto* v = gen::NewVal<gen::VList<st::Input>>();
    const st::Input* d = InputsData(in);
    for (int32_t i = 0; d && i < in.len; i++) v->Add(d[i]);
    return v;
}
gen::VList<int32_t>* RequestList(const st::NetplayRequets& r) {
    auto* v = gen::NewVal<gen::VList<int32_t>>();
    const int32_t* d = reinterpret_cast<const int32_t*>(r.data);   /* ggrs_ffi's NetplayRequest: #[repr(C)] enum (Save 0, Load 1, Advance 2) */
    for (int32_t i = 0; d && i < r.len; i++) v->Add(d[i]);
    return v;
}
namespace {
struct Raw { st::SafeBytesFFI sb; bool fromFfi; };
Raw g_raw[8];
int g_rawNext = 0;
/* TF.EX's PtrToBytes (a new byte[] of the state ggrs_ffi handed back): one growing buffer, used at once (LoadForGgrs deserializes it
 * before the next load); a state is TF.State's serialized GameState, about 1-3 KB */
uint8_t* g_rawCopy = nullptr;
size_t g_rawCopyCap = 0;
}
void* RawOfArr(const gen::Arr<uint8_t>& bytes) {
    Raw* r = &g_raw[g_rawNext++ & 7];
    *r = Raw{ st::SafeBytesFFI{ reinterpret_cast<intptr_t>(bytes.data), (uintptr_t)(bytes.len > 0 ? bytes.len : 0) }, false };
    return r;
}
void* RawOfFFI(st::SafeBytesFFI bytes) {
    Raw* r = &g_raw[g_rawNext++ & 7];
    *r = Raw{ bytes, true };
    return r;
}
st::SafeBytesFFI RawFFI(void* raw) { return raw ? static_cast<Raw*>(raw)->sb : st::SafeBytesFFI{ 0, 0 }; }
gen::Arr<uint8_t> RawToArr(void* raw) {
    auto* r = static_cast<Raw*>(raw);
    size_t n = r ? (size_t)r->sb.size : 0;
    if (n > g_rawCopyCap) {
        size_t cap = n < 16384 ? 16384 : n * 2;
        uint8_t* nb = static_cast<uint8_t*>(std::realloc(g_rawCopy, cap));
        if (!nb) { hw::Log("[ffi] ERROR: no memory for a loaded state of %zu bytes", n); return gen::Arr<uint8_t>{ nullptr, 0 }; }
        g_rawCopy = nb; g_rawCopyCap = cap;
        hw::Log("[ffi] loaded-state buffer %zu KB (a state of %zu bytes)", cap >> 10, n);
    }
    if (n) std::memcpy(g_rawCopy, reinterpret_cast<const void*>(r->sb.ptr), n);
    return gen::Arr<uint8_t>{ g_rawCopy, (int32_t)n };
}
void FreeRaw(void* raw) {
    auto* r = static_cast<Raw*>(raw);
    if (r && r->fromFfi && r->sb.ptr) { netplay_free_game_state(r->sb); r->sb = st::SafeBytesFFI{ 0, 0 }; }
}
namespace { volatile bool g_inInit = false; }
bool InInit() { return __atomic_load_n(&g_inInit, __ATOMIC_ACQUIRE); }
void SetInInit(bool v) { __atomic_store_n(&g_inInit, v, __ATOMIC_RELEASE); }
void StatusText(const st::Status& s, char* buf, int cap) {
    if (!buf || cap <= 0) return;
    const char* t = s.info ? reinterpret_cast<const char*>(s.info) : "";
    std::snprintf(buf, (size_t)cap, "%s", t);
}
void Log(const char* line) { static int n = 0; if (n++ < 8) hw::Log("%s", line); }
}
