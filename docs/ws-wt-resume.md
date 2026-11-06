# ws/wt-resume: the write-tracking handler corrupted registers on resume (console crash at match start, 2026-10-16)

Branch `ws/wt-resume`, on top of `coordinator` (f19be68): the code it fixes (wt::Measure, the crash logger, wt::SetFallback) is
on `coordinator` and not on master yet.

## The crash
Console session e88ca7cc (build Oct 16 17:00:34, hw.txt `rbwrite 1`, `rbdemote 120`), lobby match as seat 1: reports up to
`netplay_synchronized`, then the crash. The next boot's report (#474) has the log tail. The Atmosphère report is
`.devserve/results/20261016-183909-crash-01792170015_0100a1c00359c000.log` in the main checkout.
- Log: `[wt] self-test: handler fired 1 time(s), page perm now 0x3, content kept 0: write tracking WORKS`, then `CRASH: user exception`.
  "content kept 0" was the first sign: the page's word wasn't what was written.
- Atmosphère: User Break (abort) on MainThread from nn::os's UserExceptionHandler -> crashlog::Handler -> DefaultUserExceptionHandler.
- The crash logger's UserExceptionInfo words: type 0x101 (data abort). PC = module +0x2a7dc, LR +0x2a7cc. In a rebuild of e054575
  that is `ldr x2, [x0]` in wt::Measure's `*p = *p` loop, right after svcGetSystemTick. x23/x26 = the adrp page of wt's globals,
  x24 = a TowerFall globals page, x25 = a Boehm metadata page: Measure's own registers.
  But **x19 (the loop's end pointer) = 0x1b0d549090, inside the exception context page (User Exception Address 0x1b0d549000), and
  x29 = an address in wt's handler stack.** After the first resumed fault the loop ran past `pages[]` and loaded a null pointer:
  the second fault wasn't in a tracked range, so it went to the crash logger and the abort.

## Cause
svcReturnFromException restores only what the kernel saved at the fault: x0-x8, lr, sp, pc, pstate. x9-x29 and the SIMD
registers still hold whatever the SDK's handler stub and our C++ handler left in them. The handler called
svcReturnFromException(0) from inside a C++ function, so the faulting code resumed with clobbered callee-saved registers.

## Fix
- `wt_HandlerEntry` (asm, wtrack.cpp) is the registered handler. On entry it saves q0-q31, FPCR and FPSR, then calls `wt_Track`
  (the old range check + dirty + RW). For a tracked write it restores the SIMD state, reloads x1-x29 from UserExceptionInfo
  (x[n] at +0x10 + 8n, the layout the crash dump confirmed) and runs `svc 0x28` with x0 = 0. Anything else goes to
  `wt_Unhandled` (the crash logger, then the default handler) as before.
- The self-test writes through `ProbeWrite`, which holds known values in x9-x28 (not x18), d0 and d8 across the fault. It passes
  only if the handler fired once, the page is RW, the written value landed and no register changed. The log line now reads
  `write landed N, registers changed by the resume MASK` (bit n = xn, bit 29 = d0, bit 30 = d8).
- The crash logger (net.cpp) logs the words to +0x128 and adds a decoded line: type, pc/lr with module offsets, sp, esr (EC), far.

## Not verified
Ryujinx doesn't enforce guest page permissions, so this path only runs on the console. It builds, and the disassembly is as intended
(the probe's operands are in x0-x4, the resume reloads x1-x29). On the console the self-test line should say `registers changed by the
resume 0` and the next line `[wt] fault cost: ...`. If the console's sd:/xplay/wt-selftest.txt says "pending" or "died", delete it
to retry: the 2026-10-16 run left "passed", because the crash came after the guard was written.
