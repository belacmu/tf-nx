#!/usr/bin/env node
// An echo peer for VIABILITY-TESTS 7c (Node 22+ has a WebSocket client built in):
//   node tools/relay-peer.mjs ws://127.0.0.1:8800 [room]
// Joins the room and sends every binary message straight back through the relay.
const base = process.argv[2] ?? "ws://127.0.0.1:8800";
const room = process.argv[3] ?? "relaytest";
const ws = new WebSocket(`${base}/relay/${room}?peer=${crypto.randomUUID()}`);
ws.binaryType = "arraybuffer";
let n = 0;
ws.onopen = () => console.log(`joined ${room}`);
ws.onmessage = (e) => { ws.send(e.data); if (++n % 600 === 0) console.log(`echoed ${n}`); };
ws.onclose = () => { console.log(`closed after ${n}`); process.exit(0); };
ws.onerror = () => { console.log("error"); process.exit(1); };
