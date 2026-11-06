# ws/rtc-perf: the console's WebRTC path slows down within a match

## Status (2026-10-17)
- **Fixed** (merged in master 38c752b, published to the console): sctp-proto sent a FORWARD-TSN after every SACK on the
  maxRetransmits-0 channel, and those exchanges piled up when the loop slowed; patched in `tfx-rtc/vendor/sctp-proto`, plus an
  8 KB send-buffer cap in tfx-rtc. Verified in Ryujinx and on the host against Chrome; **not yet confirmed on the console**.
- **Reading the console logs** (every 10 s): loops per 10 s should stay near the match start's for the whole match;
  `np: rtc: dgrams …`: datagrams out per data packet out ~1-1.6 (before the fix it climbed to ~5), buffered well under 8 KB,
  few refused; `np: rtc: socket ms …` and `thread cpu`: wall time far above CPU time means waiting on socket calls, not work.
- **Open:** (1) socket calls per datagram: a poll before every recvfrom (an O_NONBLOCK socket would save one IPC each) and a TLS
  read of the room socket every loop; (2) the network thread shares core 1 at priority 12 with rollback compare worker 0
  (rollback.cpp StartWorkers; Horizon doesn't time-slice equal priorities): suspect if loops still fall with the datagram ratio flat.
- Building: rebuild the Rust library (`ggrs-switch/build.sh`) in the main checkout after changing tfx-rtc or the vendored crate.

## The symptom (console, 2026-10-16 22:54-22:56 and 23:07-23:09)
Every lobby match against the browser started with a fast network thread (~10,000 loops per 10 s, "udp in" / "udp out" ~0.3-0.5 s)
and got far slower the longer and busier it ran (200-900 loops, udp in 5-6 s, udp out 3.7-4.2 s), with GGRS ping 100-217 ms on
LAN (16 ms at the start) and more rollbacks. A new match (new rtc, UDP socket, room socket) started fast again; once the packets
stopped (after a match, still in the same session) the loop was fast again.

## Cause: sctp-proto sends a FORWARD-TSN after every SACK on a maxRetransmits-0 channel
The data channel is unordered with `maxRetransmits: 0` (what TF.EX / matchbox / tfnet.js use). str0m's SCTP is sctp-proto 0.10.6
(a port of pion's). Its `check_partial_reliability_status` abandoned a max-retransmits chunk once `nsent >= limit`, and `nsent` is
already 1 at the first send, so **every DATA chunk was abandoned the moment it was sent**. On every SACK (RFC 3758 C2/C3) the
Advanced.Peer.Ack.Point then skipped all chunks still in flight, so the Switch sent a FORWARD-TSN; Chrome SACKs every FORWARD-TSN
(that SACK again finds abandoned chunks in flight: another FORWARD-TSN), and so on. With a fast loop and a short RTT most of these
ride in the same datagrams as data and SACKs and cost little. When the loop slows or the RTT rises (each socket call costs ~0.2-3 ms
on the console; a frame hitch, a Wi-Fi burst) more of these exchanges are in flight at once and they pile up: datagrams in and out
grow ~1:1 with no data in them, each one costs a poll + recvfrom or a sendto, the loop gets slower, more pile up. It only stops when
no data is in flight (the end of a match), and each new session starts clean.

A second, related problem: the abandoned chunks still count against SCTP's congestion window until a FORWARD-TSN round trip
clears them, and after a T3-rtx timeout `cwnd` drops to 1 MTU. When the window can't carry GGRS's ~7 KB/s, packets wait in
sctp-proto's pending queue, which has no bound: the queue (and the ping) only grows.

## Evidence
- Host, no network (`tfx-rtc/examples/soak.rs`: two Meshes in one process, simulated clock, 1% loss, 120 packets/s of 60 B each
  way, 5 min): flat at ~2.6 us per datagram in (also with `--cfg aes_force_soft --cfg polyval_force_soft`: crypto is not the cost).
- Host vs headless Chrome (`examples/peer.rs` with `RATE=120`, `test/page.html?rate=120`; loopback; 0% and 2% loss; 200 s): flat.
- Host vs Chrome with the console's socket cost emulated (`LAG=4000`: 4 ms sleep per datagram received and sent), before the fix:
  the channel's SCTP buffered amount grew without bound (17.6 KB at 11 s -> 78.8 KB at 106 s), str0m time per datagram in
  66 -> 169 us, datagrams out 978 -> 1623 per 10 s for the same data.
- Ryujinx (`tools/lobby-vs-browser.sh`, vsstop 9000, 23:59-00:10; every socket call there costs ~3 ms, so it's the console's late
  state from the start): per 10 s, data packets out fell 560 -> 255 while datagrams out rose 884 -> 1390 and in 1256 -> 1363: from
  ~1.5 datagrams per data packet to ~5 out and ~3.5 in, in ≈ out. Time inside str0m stayed ~35-45 ms per 10 s; the windows' time
  was all in the socket calls (poll ~4 s, recvfrom ~2 s, sendto ~4 s per 10 s).
- The console logs fit: "udp out" is only `tfx_rtc_udp_out` (a queue pop) plus `sendto`, so its 3-4 s per 10 s were socket calls
  for datagrams; the pure-CPU sections ("drive", "send", "events") stayed at us per call. The growth follows traffic, not str0m
  state size.

## The fix
- `tfx-rtc/vendor/sctp-proto` (sctp-proto 0.10.6, patched; `[patch.crates-io]` in tfx-rtc and ggrs-switch): a max-retransmits
  chunk is abandoned only when a retransmission would exceed the limit: at T3-rtx (`abandon_exhausted`, before RFC 3758 A5/C2) and
  in fast retransmit (`rexmit_exhausted`); `check_partial_reliability_status` no longer abandons at the first send. Chunks in flight
  are acknowledged by ordinary SACKs; a FORWARD-TSN goes out only for chunks that were really lost. The 268 crate tests pass, plus
  `test_assoc_unreliable_rexmit_zero_not_abandoned_in_flight` (fails on the original code: all 3 chunks in flight abandoned).
- `tfx-rtc` `Mesh::send`: refuses a packet when the channel holds more than `MAX_BUFFERED` (8 KB, ~1 s of GGRS) unsent or
  unacknowledged, so a stall can't queue seconds of stale packets ahead of the current ones (GGRS resends unacknowledged input;
  these count as "refused by the channel" in the console log).
- Host vs Chrome, `LAG=4000`, after the fix: datagrams out ~750 per 10 s for ~1,240 data packets (before: 978-1,623 and rising),
  buffered 3-8 KB and not growing (the cap refused packets in two of the 11 windows), str0m 35-90 us per datagram in.

- Ryujinx after the fix (same harness and settings, 00:50-01:02, rebased on master 9a19eff): over ~3.5 min of match, datagrams
  out per data packet out stayed 1.5-1.6 (858/540, 884/586, 822/512 late in the match; before: 1.5 rising to 5.3), data packets
  out per 10 s 380-590 (before: falling 560 -> 255), buffered 0.2-3 KB, 0 refused after the first window.
  That run's log comparison failed from tick -264 (spawn positions swapped, rng differs, before any input): the same signature as
  another checkout's run without this change at 01:00 (/tmp/xplay-runs-07a0ba51-B, 297 of 297 differing): a harness / shared
  state problem since ~00:45, not this change. The run before the fix (23:59, master 3684728) compared 0 differing of 4364.

## Instrumentation (stays in)
Every 10 s the console's rtc thread now also logs:
- `np: rtc: dgrams in N (B) out N (B, max M), data in/out/refused; str0m us: in (max) drive (max) send; buffered B, queued` —
  `tfx_rtc_stats` (tfx-rtc `Mesh::take_stats`).
- `np: rtc: socket ms: poll (count, max) recvfrom (…) sendto (…); thread cpu X ms of 10 s, core K` — the socket calls alone and the
  network thread's own CPU time (`svcGetInfo` ThreadTickCount): wall time far above CPU time means waiting (IPC, or another thread on
  core 1), not work.
- On the console, check: datagrams out per data packet out (should stay ~1-1.6), buffered (should stay well under 8 KB), loops per
  10 s staying ~10,000 for a whole match, GGRS ping staying near the start's.

## Not done / open
- Not yet run on the console. If the loop still slows there with datagrams per data packet flat, the socket calls themselves get
  slower (the `socket ms` line and thread CPU say which): then look at the network thread sharing core 1 at priority 12 with
  rollback compare worker 0 (rollback.cpp StartWorkers; Horizon doesn't time-slice equal priorities).
- Fewer socket calls per datagram would help anyway: a poll before each recvfrom (MSG_DONTWAIT isn't honoured; an O_NONBLOCK socket
  via fcntl would save one IPC per datagram), and a TLS read of the room socket every loop.
- Not changed: opt-level, LTO, hardware AES (str0m's time is ~1% of the loop).

## Host tests
- `cargo build --release --examples` in tfx-rtc; `target/release/examples/soak [secs] [rate] [size]` (LOSS, DELAY, QUIET).
- Chrome: `node cloudflare/tfex-server/local.mjs --port <p>` (browser repo), serve `tfx-rtc/test` over http, open
  `page.html?room=ws://127.0.0.1:<p>/room/<r>&rate=120&size=60` in headless Chrome, then
  `BIND=127.0.0.1 RATE=120 [LOSS=0.02] [LAG=<us>] target/release/examples/peer ws://127.0.0.1:<p>/room/<r> 1 10 <secs>`.
- Building the Switch lib from a worktree: ggrs-switch's `../../../Towerfall Browser` path dependency only resolves from the main
  checkout's depth; from a worktree, copy ggrs-switch, ggrs-spike and tfx-rtc to a folder at that depth (a `Towerfall Browser`
  symlink three levels up) and run its build.sh with `HW_EXLAUNCH=<worktree>/hw-probe/exlaunch`; then delete the mirror's .elf.
