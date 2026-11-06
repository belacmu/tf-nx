# Root cause of the 2026-10-18 hardware faults, and the fix

Goal (the user): get rid of our custom implementations; the Switch runs TF.EX.

## What the faults have in common
Every confirmed fault today sits on the seam between TF.EX's translated code and code we wrote, not in TF.EX's logic:
- **Every lobby match aborted on frame 0** (fixed 8a5ffb5): the generated `NetplayManager.IsDisconnected` was a constant `true`.
- **The 11:19 rematch hang** (fixed 155badc): `NetplayManager.IsSynchronized` was a constant `true`.
- Both came from one rule, `tools/tfex/overrides.toml:416`: `"TF.EX.Domain.Externals.GGRSFFI.*" = { noop = true }`. Every call TF.EX
  makes into its native netplay library (29 functions; 7 have a rule) silently becomes a no-op with a default result, and the
  translator's report doesn't list them. So TF.EX's NetplayManager runs, but every decision it takes from the session state reads
  an invented value.
- **Boot crash 171f0de**: our runtime allocated from the game's GC in exl_main (fixed f3562a9).
- **WiderSet "MISSING VALUE"** on joining a browser lobby: TF.EX's own check; its `bool.TryParse` on the lobby's value fails on the
  Switch (binding `m_Boolean_TryParse_9ee6` = 0x410f90, or the value conversion). Open.
- **Crash on every kill** (8a5ffb5, fixed 104c97b): B2's genstate gate in rnd.cpp declared `GenStateOn` inside `namespace rnd`, so
  LoseHatHook / AddArrowHook called a function nothing defined. build-probe only checked Rust symbols; it now fails on any
  unresolved symbol of ours. Ryujinx runs without a death (vsstop 300, the smoke lobby) don't reach it.
- **Menu crash** (efe8f6c, open): our `svc::EnsureLocalControllerSeat` (tfex/runtime/tfexgen_services.cpp:100-118) swaps the local
  controller into `TFGame.PlayerInputs[seat]` and leaves a null in its old slot; the game's MInput.UpdateJoysticks then calls
  UpdateStyle on that null for the attached JoyCon. Another hand rule standing in for TF.EX's InputService.

## Why this keeps happening: the netplay core is ours, not TF.EX's
TF.EX = C# (menus, matchmaking, NetplayManager, TF.State) + **ggrs_ffi**, its native Rust library (GGRS + matchbox_socket:
session, inputs, save/load requests, events, desync detection). The **browser already runs TF.EX's own ggrs_ffi**: it builds it for
wasm and replaces only the socket (matchbox_socket -> the page's WebRTC) (Towerfall Browser docs/MULTIPLAYER.md).

The Switch doesn't. It has its own Rust library (`ggrs-switch`, `tfx_*` API) and its own session driver (xplay.cpp, lobbymatch.cpp,
rollback glue), and TF.EX's NetplayManager is connected to it by hand rules (`svc::IsSynchronized`, `svc::IsDisconnected`,
`netplay_events` = empty, `netplay_reset` = `xplay::EndLobbyMatch`, ...) plus the no-op wildcard. Each rule is a small
re-implementation of what ggrs_ffi does; each wrong or missing one is a bug found on the console.

## The fix (in order)
1. **Port TF.EX's ggrs_ffi to the Switch, the browser's way.** Build Fcornaire/ggrs-ffi (the browser's `vendor/netplay-src/ggrs-ffi`
   + its patch) for the Switch target ggrs-switch already uses (aarch64 musl, no TLS); replace only matchbox_socket with the
   Switch's WebRTC transport (ggrs-switch's `rtc.rs`, already matchbox-compatible). Then every `GGRSFFI.*` call maps 1:1 onto
   the real `netplay_*` function: remove the wildcard and the hand rules.
   The Switch-specific part that stays: the game-state save/load. TF.EX's `netplay_save_game_state` gets TF.State's
   serialized state; on the Switch the state is the memory snapshot (rollback.cpp, write tracking). That is the one TFEX-DIFF
   (performance), behind the same FFI calls.
2. **Retire the custom session driver** that the port makes redundant: xplay's GGRS session, lobbymatch's session setup,
   ggrs-switch's `tfx_*` API, the `svc::` session-state rules. TF.EX's NetplayManager then drives the session as on PC.
3. **No silent defaults in the translator**: an external call without an explicit rule fails the translation (no wildcard
   no-ops); `check-gen.sh` fails on an unlisted no-op or fixed value. Review the existing 61 "no-op / fixed value" rules once.
4. **Then TF.State (genstate)**: gameplay patches as TF.EX's code, replacing the hand ports (in progress; off by default).

Turning on `genstate 1` alone fixes none of today's faults: they are in the netplay seam and the menus, not the gameplay patches.

## 2026-10-18 19:00 console test of the switch-over (73c6218): no match synchronizes
- **Every lobby / quick play match: the map loads, no archers, back to the menu after 30 s.** The transport works (ICE connected,
  data channel open, ggrs_ffi "Starting p2p session"), but nothing polls the session. TF.EX's NativeInit is
  `IsInInit = true; try { return netplay_init(..); } finally { IsInInit = false; }`, and the translator emitted the try and finally
  bodies as two plain blocks (tfexgen_NetplayManager.cpp:339-349): the `return` skips the finally, `ffi::InInit()` stays true, and
  netplay_gate.cpp never calls NetplayManager.Poll. **Translator bug: try/finally loses its semantics** (TF.EX has try/finally
  in 14 files). Fix: finally as a scope guard (gen::MakeDefer, as for `using`), regenerate, check every translated try/finally.
- **WiderSet "MISSING VALUE"**: the binding `m_Boolean_TryParse_9ee6` resolved to the wrong method: binding.json has owner
  System.ValueType, an instance method, body 0x410f90, which stores an 8-byte out value and splits on '.' (a Version-style parse).
  Boolean.TryParse(string, out bool) is static on System.Boolean. **bind.py picked the wrong overload/owner.** Fix in bind.py (and
  check other bindings whose owner differs from the C# type), not by hand.
- **Logs:** no prev-hw-log.txt upload at boot since 15:39 (boots at 15:59 and 18:55 sent none), so each session's full log is lost
  when the console restarts. To fix with the above.
