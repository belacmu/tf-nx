#!/usr/bin/env node
// The Node twin of the Switch's 7c test (net-probe, "relay" stage): 60 packets/s of 100 bytes through
// a relay to an echoing peer, round trip per packet, gaps between echoes. Same statistics and pass
// line (p99 within 50 ms of the median, no gap over 150 ms), so it measures the relay and network
// from the Mac, and a baseline for the emulator and console numbers.
//   node tools/relay-probe.mjs ws://127.0.0.1:8800 [seconds=60] [room=relaytest]
const base = process.argv[2] ?? "ws://127.0.0.1:8800";
const seconds = Number(process.argv[3] ?? 60);
const room = process.argv[4] ?? "relaytest";
const ws = new WebSocket(`${base}/relay/${room}?peer=${crypto.randomUUID()}`);
ws.binaryType = "arraybuffer";
const rtt = [];
let sent = 0, lastRx = 0, maxGap = 0, bigGaps = 0;
const seen = new Set();
ws.onmessage = (e) => {
	const v = new DataView(e.data);
	if (e.data.byteLength < 16 || v.getUint32(0, true) !== 0x474e5058) return; // "XPNG"
	const seq = v.getUint32(4, true);
	if (seen.has(seq)) return;
	seen.add(seq);
	const now = performance.now();
	rtt.push(now - v.getFloat64(8, true));
	if (lastRx) { const gap = now - lastRx; maxGap = Math.max(maxGap, gap); if (gap > 150) bigGaps++; }
	lastRx = now;
};
ws.onopen = () => {
	console.log(`joined ${room}; ${seconds} s`);
	const start = performance.now();
	let next = start;
	const timer = setInterval(() => {
		const now = performance.now();
		if (now - start >= seconds * 1000) {
			clearInterval(timer);
			return setTimeout(finish, 3000);
		}
		while (now >= next) {
			const b = new ArrayBuffer(100), v = new DataView(b);
			v.setUint32(0, 0x474e5058, true); v.setUint32(4, sent++, true); v.setFloat64(8, performance.now(), true);
			ws.send(b);
			next += 1000 / 60;
		}
	}, 1);
};
ws.onerror = () => { console.log("error"); process.exit(1); };
function finish() {
	if (!rtt.length) { console.log("FAIL: nothing echoed (is a peer in the room?)"); process.exit(1); }
	const s = [...rtt].sort((a, b) => a - b);
	const pct = (p) => s[Math.min(s.length - 1, Math.floor(p * s.length))];
	const f = (x) => x.toFixed(2);
	console.log(`sent ${sent}, echoed ${s.length}, lost ${sent - s.length}`);
	console.log(`round trip ms: min ${f(s[0])} p50 ${f(pct(0.5))} p90 ${f(pct(0.9))} p99 ${f(pct(0.99))} p99.9 ${f(pct(0.999))} max ${f(s.at(-1))}`);
	console.log(`p99 - p50 = ${f(pct(0.99) - pct(0.5))} ms; longest gap between echoes ${maxGap.toFixed(0)} ms; gaps over 150 ms: ${bigGaps}`);
	const pass = sent === s.length && pct(0.99) - pct(0.5) < 50 && maxGap < 150;
	console.log(pass ? "PASS" : "FAIL");
	ws.close();
	process.exit(0);
}
