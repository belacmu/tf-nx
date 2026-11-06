/* Rollback-safe game changes requested between simulated ticks (TFEX-DIFF: S-rollback; docs/ws-tfex-subclass.md "Phase 2").
 *
 * TF.EX changes UI from network callbacks, between ticks (Notification.Create / Clear...); its TF.State snapshot doesn't hold that UI,
 * so a rollback leaves it alone. Here a rollback restores all of the game's memory: a change made between ticks is erased by a rollback
 * to an earlier frame and nothing makes it again. So, in a Level, such a change is a request (kept in our module, not rolled back),
 * carried out at the start of the next tick's Scene.Update, inside the simulation; the number of the last request carried out lives in
 * GC memory, so a rollback past it takes back the change *and* that record, and the resimulation makes it again at the same tick.
 * Outside a Level (menus: no rollback) and from inside a tick, Run calls fn at once.
 *
 * Independent of any one class: Notification uses it; a generated (translated) class can wrap its static entry points the same way. */
#pragma once
#include <cstddef>

namespace tfex::BetweenTicks {

using Action = void (*)(void* scene, const void* payload);
constexpr size_t kPayload = 112;
/* fn(scene, payload) now, or (a Level, outside a tick) at the start of the next tick, replayed by resimulations. payload is copied
 * (<= kPayload bytes, plain data: no GC pointers, they would not survive a rollback). Returns true if it ran now. */
bool Run(void* scene, Action fn, const void* payload, size_t size);
bool InTick();          /* inside a Level's Scene.Update (a simulated tick, first pass or resimulation) */
/* Makes the GC cell and installs the Scene.Update hook (+0x944130). Call once where no rollback window is open (a menu). */
bool Enable();

} // namespace tfex::BetweenTicks
