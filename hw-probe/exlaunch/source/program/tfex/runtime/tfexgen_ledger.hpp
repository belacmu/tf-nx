/* Switch functions the ledger's glue (translator/glue/Ledger.cs) calls that may not be linked yet: weak, checked before the call. */
#pragma once
#include <cstdint>

namespace tfex::Notification { [[gnu::weak]] void* SharedCanvas(int32_t width, int32_t height); }   /* tfex/Notification.cpp (ws/tfex-subclass) */
namespace tfex::gen::ledger {
inline bool HasSharedCanvas() { return &tfex::Notification::SharedCanvas != nullptr; }
}
