/* BRUTE's managed runtime, for classes of ours that are real game classes (tfex/Game.hpp brute::; docs/ws-tfex-subclass.md): used by the
 * generated TF.EX classes (tfex/gen/) and any hand-written one. */
#include "Game.hpp"
#include <cstring>
#include <initializer_list>

namespace tfex::game::brute {
bool MakeSubclass(Subclass& c, uintptr_t baseGetter, uintptr_t baseDesc, uint32_t vtableBytes, uint32_t size,
                  const char16_t* name, const char16_t* ns, const char16_t* fullName, const char16_t* aqName, uint32_t hash) {
    void* base = Type(baseGetter);
    uintptr_t* bdesc = reinterpret_cast<uintptr_t*>(Base() + baseDesc);
    if (!base || reinterpret_cast<void*>(bdesc[0]) != base || vtableBytes > sizeof c.vtable) return false;   /* the descriptor names that type */
    if (At<uint64_t>(base, RtSize) > size) return false;
    uint8_t* t = static_cast<uint8_t*>(Fn<void* (*)(uint64_t)>(MallocUncollectable)(RtRecord));
    if (!t) return false;
    std::memcpy(t, base, RtRecord);                      /* the base's record: flags (holds references), kind, interfaces, builders */
    At<uint64_t>(t, RtSize) = size;
    At<uint32_t>(t, RtHash) = hash;
    At<const char16_t*>(t, RtName) = name; At<const char16_t*>(t, RtNamespace) = ns;
    At<const char16_t*>(t, RtFullName) = fullName; At<const char16_t*>(t, RtAqName) = aqName;
    At<uintptr_t>(t, RtDesc) = reinterpret_cast<uintptr_t>(c.desc);
    At<uintptr_t>(t, RtCctor) = 0;                       /* no class constructor (our statics are made by EnsureClass) */
    At<uintptr_t>(t, RtParent) = Base() + baseGetter;     /* the base type's getter, as MakeType's x7 for a game subclass */
    At<uint16_t>(t, RtFieldCount) = 0; At<uint16_t>(t, RtPropCount) = 0; At<uint16_t>(t, RtMethodCount) = 0;   /* no reflection of ours */
    for (uint32_t cache : {0xc8u, 0xf0u, 0x108u, 0x120u, 0x138u, 0x150u, 0x160u}) At<uintptr_t>(t, cache) = 0;   /* MakeType's lazy caches */
    std::memcpy(c.vtable, reinterpret_cast<void*>(bdesc[1]), vtableBytes);
    c.desc[0] = reinterpret_cast<uintptr_t>(t); c.desc[1] = reinterpret_cast<uintptr_t>(c.vtable); c.desc[2] = bdesc[2];   /* same interface table */
    for (auto& m : c.method) { At<const char16_t*>(m, 0) = name; At<void*>(m, 8) = t; }
    c.type = t; c.base = base;
    return true;
}
void* NewDelegate(Subclass& c, int methodIndex, uintptr_t delegateTypeGetter, void* target, void* fn) {
    void* d = Fn<void* (*)(void*)>(NewObject)(Type(delegateTypeGetter));
    if (!d) return nullptr;
    At<void*>(c.method[methodIndex], 0x18) = fn;
    At<void*>(d, DlgFn) = fn; At<void*>(d, DlgTarget) = target; At<void*>(d, DlgMethod) = c.method[methodIndex];
    return d;
}
void* NewNativeDelegate(uintptr_t delegateTypeGetter, void* target, void* fn) {
    static uint8_t records[32][0x60];   /* one method record per function (Invoke reads its +0x08: the declaring type) */
    static void* fns[32]; static int n = 0;
    int k = 0;
    while (k < n && fns[k] != fn) k++;
    if (k == n) { if (n == 32) k = 31; else n++; fns[k] = fn; }
    std::memset(records[k], 0, 0x60);
    At<void*>(records[k], 8) = Type(Type_Entity); At<void*>(records[k], 0x18) = fn;
    void* d = Fn<void* (*)(void*)>(NewObject)(Type(delegateTypeGetter));
    if (!d) return nullptr;
    At<void*>(d, DlgFn) = fn; At<void*>(d, DlgTarget) = target; At<void*>(d, DlgMethod) = records[k];
    return d;
}
} // namespace tfex::game::brute
