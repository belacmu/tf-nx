/* Every fixed-size pool of the module in one place (2026-10-17: the console's problems that day were all pools running out: net
 * threads, connection slots, hook trampolines). A pool registers a function reporting what it holds; Report writes one line with each
 * pool's use / capacity (peak), at boot and at the end of every lobby match; Exhausted is the one way to say a pool ran out: a clear
 * error line (each time, counted), never a silent failure.
 *
 *   static void MyUse(pools::Use* u) { u->used = g_n; u->cap = kMax; }
 *   pools::Register("my pool", MyUse);
 *   if (g_n == kMax) { pools::Exhausted("my pool", kMax, "what was refused"); return false; }
 */
#pragma once

namespace pools {

struct Use {
    int used = 0;   /* in use now */
    int cap = 0;    /* capacity */
    int peak = -1;  /* the most ever in use (-1: the pool doesn't track it; Report keeps its own peak of the samples) */
};
using UseFn = void (*)(Use* u);

void Register(const char* name, UseFn fn);   /* once per pool (name: a string literal) */
void Report(const char* when);               /* one log line: "[pools] <when>: name used/cap (peak p) ..., FULL: ..." */
void Exhausted(const char* name, int cap, const char* what);   /* "[pools] EXHAUSTED <name> (capacity N): <what>" */
int ExhaustedCount();                        /* how many refusals so far (any pool) */

} // namespace pools
