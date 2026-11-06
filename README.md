# TowerFall Switch — online play with the browser port (native mod)

An online mode in which the eShop Switch build (v1.4.1.6, Atmosphère + exlaunch) plays the
browser port (towerfall-browser, PC 1.3.3.3 in .NET WebAssembly) with **rollback** netcode,
limited to what both have in common. Up to now this is a feasibility probe, not a playable mod.

- **▶ RESUME HERE:** [`STATUS.md`](STATUS.md): where it stands and what's next.
- **The plan and every result so far:** [`PLAN.md`](PLAN.md), steps 1–6.
- **What TF.EX does** and what that changes for us: [`TFEX-LESSONS.md`](TFEX-LESSONS.md).

## Layout

```
exlaunch-overlay/          The probe: drop into its own exlaunch checkout (~/Code/tfx-crossplay-exlaunch)
  config.mk                -ffp-contract=off (no fused multiply-add), -Wno-error
  source/program/main.cpp  Math checks + fixes, scripted input, the rollback self-test
  source/program/mathlib/  musl's pow (the browser's), built without the FMA path
net-probe/                 VIABILITY-TESTS 7a/7b/8: TCP, TLS WebSocket and GGRS from inside the game (own overlay)
relay-worker/              The 7c relay as a Cloudflare Worker (deployed as tf-relay-probe; temporary)
hw-probe/                  The one module for the console visit (hooks, network, drill, changed pages) + installer, SD templates, README
ggrs-spike/                The Rust GGRS loopback test (Mac binary and Switch staticlib) and build script
test-scripts/              sdmc:/xplay/script.txt = menu-to-trials.txt + trial-script.txt
  results/                 Per-frame position/speed bits, Switch and browser, 700 frames, identical
tools/
  run-probe.sh             One safe test run (save check, deploy, run, restore)
  echo-server.py           The 7a echo server
  hw-check.py              Summarizes a console visit's logs (hw-log.txt, trial-log.txt)
  relay-server.mjs         The 7c WebSocket relay (stall injection), relay-peer.mjs / relay-probe.mjs: its echo peer and Node twin of the test
  keephidden.swift         Keeps Ryujinx's window hidden (XPLAY_VISIBLE=0 only; it has no headless mode)
  crossplay-analyze.py     Scans the decompiled PC game for the math and ordering constructs (PLAN 1b/1c)
vendor/tfex/               TF.EX's source (submodule): translated to the Switch by tools/tfex
vendor/fortrise/           FortRise's source (submodule, tag 5.5.0-beta.3; MIT, Copyright (c) 2023 Terria Katsura, its LICENSE):
                           its content-mod loader is translated for FortRise mods on the SD card (docs/research-fortrise-mods-on-switch.md)
```

The browser half of the tests stays in the towerfall-browser repo, because it builds into the
browser host. There are three host commands, `floattest`, `mathtest` and `xplay`, in
`web/Determinism.cs` and `web/CrossplayTest.cs`, plus `tools/floattest-reference.mjs`.

## Build and run

1. Copy `exlaunch-overlay/` over `~/Code/tfx-crossplay-exlaunch` (its own checkout, as with the
   other native mods here).
2. Build: `DEVKITPRO=/opt/devkitpro ./exlaunch.sh build`.
3. Run `tools/run-probe.sh <seconds>`. It:
   - refuses to start if Ryujinx is running or the save data has changed;
   - backs up the save data on first use, into `.save-backup/`, which git doesn't track;
   - deploys the build into the `crossplay-probe` mod and activates it;
   - runs TowerFall hidden at a timer-paced 60 fps (Custom vsync 60; App Nap off for Ryujinx), no audio (`XPLAY_VISIBLE=1`: a visible background window);
   - re-activates `combined-mod` and re-checks the save data.

**Rules for test runs:** the window stays hidden (Custom vsync 60, not Unbounded: Unbounded vsync coincided with the MoltenVK/AGX
crashes of 2026-10-15; App Nap disabled for Ryujinx so hidden runs keep 60 fps). One Ryujinx run at a time, each to confirm or find out something. Leave the
save data unchanged (the script checks). Only one native mod is active at a time, and it goes
back to `combined-mod` afterwards. Don't touch the Steam game files.

The isolation rules in [`../eight-player-mod/README.md`](../eight-player-mod/README.md) apply here
too: own tree, own exlaunch checkout, one native mod enabled at a time.
