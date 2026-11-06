# Network probe (VIABILITY-TESTS 7a, 7b and 8)

Can code injected into the TowerFall process reach the network? It runs on its own thread 15 s
after boot, installs no hooks, and doesn't touch the game.

- **7a:** plain TCP: 20 round trips to an echo server, with timings.
- **7c:** `relay ...`: 60 packets/s of 100 bytes through a WebSocket relay to an echoing peer, with round-trip percentiles and stall gaps (`../tools/relay-server.mjs`, `relay-peer.mjs`; the Node twin is `relay-probe.mjs`).
- **8:** `ggrs <ticks> <delay>`: two GGRS sessions in-process (see `../ggrs-spike/`).
- **7b:** TLS WebSocket to the signaling server (`/room/<uuid>?peer=<uuid>`). Pass = the first
  message is `{"IdAssigned": ...}`.

## Files

```
exlaunch-overlay/   Drop over its own exlaunch checkout (~/Code/tfx-net-exlaunch)
  config.mk                  adds -nodefaultlibs and the Rust library (EXTRA_LDFLAGS, EXTRA_LIBS)
  misc/mk/common.mk          exlaunch's, with LIBS and LDFLAGS taking those two
  source/program/main.cpp
  source/program/shim.c      libc functions the game doesn't export (see VIABILITY-TESTS test 8)
../ggrs-spike/             The Rust side of test 8 and its build script
../tools/echo-server.py    The 7a server: python3 tools/echo-server.py 9000
```

Build:
```
./ggrs-spike/build.sh        # once, and after changing the Rust (needs ../../Towerfall Browser's Rust toolchain)
cd ~/Code/tfx-net-exlaunch && DEVKITPRO=/opt/devkitpro ./exlaunch.sh build
```
(The Makefile doesn't fail on a missing library: undefined symbols in a shared module only show up
when it loads. After changing the link setup check `aarch64-none-elf-nm -D tfx-net-exlaunch.elf | grep ' U '`.)

## Config and output (both on the SD card)

`sdmc:/xplay/net.txt`, one stage per line:

```
tcp <ipv4> <port>
tls <host> <port> [<ipv4>]
ggrs <ticks> <delay>
relay <host> <port> <tls 0|1> <seconds> [<ipv4>]
```

The optional address on a `tls` line is used only if name resolution fails (it does in Ryujinx).
Results go to `sdmc:/xplay/net-log.txt`, rewritten after every line, so after a hang it shows
where the probe stopped. They also go to Ryujinx's log as `[net] ...`.

## In Ryujinx

```
python3 tools/echo-server.py 9000 &
printf 'tcp 127.0.0.1 9000\ntls <your-server> 443 104.21.88.33\n' \
  > ~/Library/Application\ Support/Ryujinx/sdcard/xplay/net.txt
XPLAY_EXLAUNCH=~/Code/tfx-net-exlaunch XPLAY_MOD=net-probe XPLAY_TAG=net tools/run-probe.sh 70
```

Ryujinx maps the guest's sockets to the host's, so `127.0.0.1` is the Mac.

## On the console

The probe is a native mod, so it takes the exefs slot. **Only one exefs mod works at a time**
(see `../../docs/SWITCH-SD-INSTALL.md`). Back up whatever is in
`sdmc:/atmosphere/contents/0100A1C00359C000/exefs/` first, then:

1. Copy `~/Code/tfx-net-exlaunch/deploy/subsdk9` and `main.npdm` to that `exefs/` folder.
2. Make `sdmc:/xplay/net.txt` with the Mac's LAN address for `tcp` (the Mac and the console on
   the same network) and the host name for `tls`:
   ```
   tcp <the Mac's LAN IP> 9000
   tls <your-server> 443
   ```
3. On the Mac: `python3 tools/echo-server.py 9000` (allow incoming connections if macOS asks).
4. Boot TowerFall and wait about 40 s (15 s of that is the probe's own delay). Quit.
5. Read `sdmc:/xplay/net-log.txt`.
6. Put the original `exefs/` back.

The console must be online (Wi-Fi or dock LAN) before the game starts.
