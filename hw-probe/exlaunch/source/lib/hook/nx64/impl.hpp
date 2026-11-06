#pragma once

#include "common.hpp"
#include "inline_impl.hpp"

namespace exl::hook::nx64 {

    void Initialize();

    uintptr_t Hook(uintptr_t hook, uintptr_t callback, bool do_trampoline = false);
    int TrampolinesUsed();   /* trampolines handed out so far (lazy installs add more at run time) */
    int TrampolinesMax();
    int InlineHooksUsed();   /* inline hooks (HOOK_DEFINE_INLINE) installed / the inline pool's size (setting::InlinePoolSize) */
    int InlineHooksMax();
    void HookInline(uintptr_t hook, uintptr_t callback, bool capture_floats);
}