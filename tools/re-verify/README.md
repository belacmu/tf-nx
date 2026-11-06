# Verifying addresses from the metadata resolver

`tools/re/metaresolve.py` (name -> native body) **can land on a neighbouring method**. Found 2026-10-14 while
porting the gameplay RNG whitelist: it gave `ArrowCushion.AddArrow` as the next function (`ReleaseArrows`),
`RoundLogic.SpawnPlayersFFA` and `SpawnPlayersTeams` swapped, and `Arrow.EnforceLimit` as `Arrow.Initialize`
(hooking that one broke arrow creation: the arrow cache was never built). The orb, lava, chest and corpse
methods checked out.

**Never hook a resolver address without verifying it against the PC source.** The method used here:

1. Take constants the PC source uses in that method (`Range(-22, 44)`, `Range(600, 300)`, `X != 160`, a
   360-tick tween, a call to `Arrow.Create`) and check that the function *contains them* (`fnspan.py`:
   `span(start)` is a function's instructions up to the `udf` padding; `locate(guess, immediates, calls)` tries
   the functions around the resolver's answer).
2. Check the first instruction is a real prologue (`sub sp,...` / `stp`/`str ...,[sp,#-N]!`), not the cleanup
   landing pad (`mov x19,x0 ... bl _Unwind_Resume`) that precedes a function.
3. Prefer to also confirm at run time (a hook's own draws, counts, or arguments).

Needs `tools/re/metaresolve.py` and a `main.flat` (`tools/re/nso.py`) in the working directory.
`resolve_req.py` is the list of whitelist methods that was resolved.
