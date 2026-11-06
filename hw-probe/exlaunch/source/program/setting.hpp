#pragma once

#include "common.hpp"

#define EXL_MODULE_NAME "exlaunch"

#define EXL_DEBUG
#define EXL_USE_FAKEHEAP

/*
#define EXL_SUPPORTS_REBOOTPAYLOAD
*/

namespace exl::setting {
    /* How large the fake .bss heap will be. */
    constexpr size_t HeapSize = 0x5000;

    /* How large the JIT area will be for hooks.
     * 0x1000 = 20 trampolines max; the three-team mod's 21st hook aborted at
     * boot (AllocForTrampoline, 2026-07-23). 0x2000 = 40; 0x6000 = 122, which the hw probe outgrew with the music hooks
     * (a late install aborted, 2026-10-16); 0x8000 = 163, outgrown by TF.EX's input-lock gates (AllocForTrampoline abort at boot,
     * 2026-10-17, Integration B); 0x10000 = 327, 269 used before the megamod's ~78 (2026-10-30); 0x14000 = 409. */
    constexpr size_t JitSize = 0x14000;

    /* How large the area will be inline hook pool. */
    constexpr size_t InlinePoolSize = 0x1000;

    /* How large the formatting buffer should be for logging. The buffer will be on the stack. */
    constexpr size_t LogBufferSize = 512;

    /* Sanity checks. */
    static_assert(ALIGN_UP(JitSize, PAGE_SIZE) == JitSize, "");
    static_assert(ALIGN_UP(InlinePoolSize, PAGE_SIZE) == InlinePoolSize, "");
}
