# tfx-rtc (was webrtc-spike)

Feasibility spike for 3-4 player online on the Switch: a TF.EX/matchbox peer over WebRTC
(str0m, sans-IO) that the Switch module can drive with its own WebSocket and UDP socket.

Proven on the Mac (2026-10-16), against the local tfex-server's `/room/<id>` signaling:
- Rust <-> Rust, and Rust + 3 headless Chromes (a 4-peer mesh), with the Rust peer both answering
  (joined last) and offering (joined first). Data channel: negotiated id 0, unordered, no
  retransmits, as netplay/tfnet.js and matchbox_socket set it up. `test/page.html` copies
  tfnet.js's peer setup.
- Cross-builds as a staticlib for `aarch64-unknown-linux-musl-notls` with the current nightly
  (`-Zjson-target-spec`; the target JSON here is regenerated for it). ggrs-switch builds with the
  same toolchain, so both can share one std.

Notes for the Switch:
- `vendor/str0m-rust-crypto`: patched so the DTLS certificate is made in pure Rust (p256 +
  x509-cert); upstream uses rcgen, which pulls in aws-lc (C). No extensions on the certificate:
  a Root profile's key usage lacks digitalSignature and Chrome refuses the handshake.
- New libc symbols vs. the current libggrs_spike.a: clock_nanosleep dup fchmodat fstatat
  gethostname gettid killpg log log10 mkdirat mkfifo pow renameat setsid strnlen utimensat.
- The module's getrandom shim is xorshift on the tick; DTLS keys and ICE credentials should come
  from nn::os::GenerateRandomBytes instead.
- str0m doesn't gather candidates: the Switch needs its own STUN binding (server-reflexive) and has
  no TURN (str0m has no TURN client), so a peer behind a symmetric NAT needs the other side's relay.

Run the host test (BIND=127.0.0.1 when the macOS firewall drops UDP to freshly built binaries; STUN=<ip:port> to fetch the public address): `cargo build --example peer`, start `node cloudflare/tfex-server/local.mjs
--port 13390` (browser repo), then `target/debug/examples/peer ws://127.0.0.1:13390/room/x <peers>`.
