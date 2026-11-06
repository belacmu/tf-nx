# ggrs-ffi-switch: TF.EX's ggrs_ffi for the Switch

- `ggrs-ffi/`: TF.EX's native netplay library (Fcornaire/ggrs-ffi v2.1.0 2fe64d9 as the browser vendors it, with the browser's
  changes; plus v2.2.0's 2faa81c, the late spectator deferred while a match synchronizes, 2026-10-24), plus the Switch changes behind the cargo feature `switch` (every one marked TFEX-DIFF; see `git log -p -- ggrs-ffi-switch`).
- `matchbox-switch/`: the `matchbox_socket` stand-in (tfx-rtc's WebRTC; the module owns the sockets).

Built by `ggrs-switch/build.sh` as a dependency of ggrs-switch (one staticlib, one Rust std). Status, the save/load
contract and the wiring plan: [docs/ws-ggrs-ffi.md](../docs/ws-ggrs-ffi.md).
