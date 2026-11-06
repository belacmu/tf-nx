#!/usr/bin/env node
// A WebSocket relay for VIABILITY-TESTS 7c: peers join a room at /relay/<room>?peer=<id>, and every
// binary or text message one sends goes to all the others, unchanged. No dependencies (Node 18+).
//
//   node tools/relay-server.mjs [port] [--port 8800] [--host 0.0.0.0] [--stall-every 20 --stall-ms 200]
//                               [--delay 30 --jitter 10 --loss 0.01 --reorder] [--dump 20]
//
// --stall-every S --stall-ms MS: every S seconds, hold everything for MS ms and then release it in
// order. That is what a lost packet does on a TCP connection (everything behind it waits), so it
// shows how the probe's numbers and a rollback session react to such stalls.
//
// --delay MS --jitter J --loss P (network simulation, applied to every forwarded message, per direction): each
// message is held MS + a random 0..J ms (so a round trip is about twice that) and dropped with probability P
// (0..1). Delivery stays in order, like the TCP/WebSocket a real peer sits behind (a late message holds back the
// ones after it), unless --reorder: then every message gets its own delay and may overtake others, like UDP.
// The relay logs a summary of what it forwarded and dropped every 10 s while it carries traffic, and on exit.
//
// --dump N: print the first N messages of every peer as hex ("rx <room> <peer> <len> <hex>", what that peer sent), to see the bytes on the wire.
//
// GET /echo.html?room=<room> serves a page that joins the room and echoes every binary message
// back: the browser stands in for the other player.
//
// A minimal RFC 6455 server: messages up to 64 KB, ping/pong, close; no fragmentation.
import { createHash } from "node:crypto";
import { createServer } from "node:http";

const args = process.argv.slice(2);
const arg = (name, fallback) => { const i = args.indexOf(`--${name}`); return i >= 0 ? args[i + 1] : fallback; };
const positionalPort = args.length && /^\d+$/.test(args[0]) ? Number(args[0]) : null; // `relay-server.mjs 18800`
const port = Number(arg("port", positionalPort ?? 8800));
const host = arg("host", "0.0.0.0");
const stallEvery = Number(arg("stall-every", 0)) * 1000;
const stallMs = Number(arg("stall-ms", 0));
const delayMs = Number(arg("delay", 0));
const jitterMs = Number(arg("jitter", 0));
const loss = Number(arg("loss", 0));
const reorder = args.includes("--reorder");
const dump = Number(arg("dump", 0));
const dumped = new Map(); // peer -> messages printed
const sim = delayMs > 0 || jitterMs > 0 || loss > 0;
const counts = { in: 0, forwarded: 0, dropped: 0, bytes: 0 };

let stallUntil = 0;
if (stallEvery && stallMs) setInterval(() => { stallUntil = Date.now() + stallMs; console.log(`stall ${stallMs} ms`); }, stallEvery);

const rooms = new Map(); // room -> Set<Peer>

function frame(opcode, payload) {
	const n = payload.length;
	const head = n < 126 ? Buffer.from([0x80 | opcode, n]) : Buffer.from([0x80 | opcode, 126, n >> 8, n & 255]);
	return Buffer.concat([head, payload]);
}

class Peer {
	constructor(socket, room, id) {
		this.socket = socket; this.room = room; this.id = id; this.buffer = Buffer.alloc(0); this.closed = false;
		socket.setNoDelay(true);
		socket.on("data", (d) => this.receive(d));
		socket.on("close", () => this.finish());
		socket.on("error", () => this.finish());
	}
	write(out) {
		const wait = stallUntil - Date.now();
		if (wait > 0) setTimeout(() => !this.closed && this.socket.write(out), wait); // same delay, so order is kept
		else this.socket.write(out);
	}
	send(opcode, payload) {
		if (this.closed) return;
		counts.in++;
		if (loss > 0 && Math.random() < loss) { counts.dropped++; return; }
		const out = frame(opcode, payload);
		counts.forwarded++;
		counts.bytes += payload.length;
		if (!sim || (delayMs <= 0 && jitterMs <= 0)) return this.write(out);
		let due = Date.now() + delayMs + Math.random() * jitterMs;
		if (!reorder) { due = Math.max(due, this.lastDue ?? 0); this.lastDue = due; } // in order, as TCP would
		setTimeout(() => !this.closed && this.write(out), Math.max(0, due - Date.now()));
	}
	receive(data) {
		this.buffer = Buffer.concat([this.buffer, data]);
		for (;;) {
			const b = this.buffer;
			if (b.length < 2) return;
			const opcode = b[0] & 0x0f;
			let len = b[1] & 0x7f, off = 2;
			if (len === 126) { if (b.length < 4) return; len = b.readUInt16BE(2); off = 4; }
			else if (len === 127) return this.socket.destroy(); // too big
			const masked = (b[1] & 0x80) !== 0;
			if (b.length < off + (masked ? 4 : 0) + len) return;
			const key = masked ? b.subarray(off, off + 4) : null;
			if (masked) off += 4;
			const payload = Buffer.from(b.subarray(off, off + len));
			if (key) for (let i = 0; i < len; i++) payload[i] ^= key[i & 3];
			this.buffer = b.subarray(off + len);
			if (opcode === 8) { this.socket.end(frame(8, payload.subarray(0, 2))); return; }
			if (opcode === 9) { this.socket.write(frame(10, payload)); continue; }
			if (dump && (opcode === 1 || opcode === 2)) {
				const n = dumped.get(this.id) ?? 0;
				if (n < dump) { dumped.set(this.id, n + 1); console.log(`rx ${this.room} ${this.id} ${opcode === 2 ? "bin" : "text"} ${payload.length} ${payload.toString("hex")}`); }
			}
			if (opcode === 1 || opcode === 2) for (const p of rooms.get(this.room) ?? []) if (p !== this) p.send(opcode, payload);
		}
	}
	finish() {
		if (this.closed) return;
		this.closed = true;
		const set = rooms.get(this.room);
		set?.delete(this);
		if (set && !set.size) rooms.delete(this.room);
		console.log(`left ${this.room} ${this.id} (${set?.size ?? 0} left)`);
	}
}

const page = (room) => `<!doctype html><meta charset=utf-8><title>relay echo</title>
<body style="font:16px system-ui;margin:2em"><h3>relay echo peer</h3><p id=s>connecting…</p>
<script>
const room = ${JSON.stringify(room)}, id = crypto.randomUUID();
const ws = new WebSocket((location.protocol === "https:" ? "wss://" : "ws://") + location.host + "/relay/" + room + "?peer=" + id);
ws.binaryType = "arraybuffer";
let n = 0;
ws.onopen = () => s.textContent = "joined " + room + " as " + id;
ws.onmessage = (e) => { ws.send(e.data); n++; if (n % 60 === 0) s.textContent = "echoed " + n + " messages in " + room; };
ws.onclose = () => s.textContent += " (closed after " + n + ")";
</script>`;

const server = createServer((req, res) => {
	const url = new URL(req.url, "http://x");
	if (url.pathname === "/echo.html") { res.writeHead(200, { "content-type": "text/html" }); return res.end(page(url.searchParams.get("room") ?? "relaytest")); }
	res.writeHead(404); res.end("not found");
});
server.on("upgrade", (req, socket) => {
	const url = new URL(req.url, "http://x");
	const m = /^\/relay\/([^/]+)$/.exec(url.pathname);
	const key = req.headers["sec-websocket-key"];
	if (!m || !key) { socket.end("HTTP/1.1 400 Bad Request\r\n\r\n"); return; }
	const accept = createHash("sha1").update(key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").digest("base64");
	socket.write(`HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: ${accept}\r\n\r\n`);
	const room = m[1];
	const peer = new Peer(socket, room, url.searchParams.get("peer") ?? "?");
	if (!rooms.has(room)) rooms.set(room, new Set());
	rooms.get(room).add(peer);
	console.log(`joined ${room} ${peer.id} (${rooms.get(room).size} in room)`);
});
server.listen(port, host, () => console.log(`relay on ${host}:${port}${stallEvery ? `, stalling ${stallMs} ms every ${stallEvery / 1000} s` : ""}${sim ? `, simulating delay ${delayMs} ms + 0..${jitterMs} ms, loss ${loss}${reorder ? ", reordering" : ", in order"}` : ""}`));
const summary = () => `forwarded ${counts.forwarded} messages (${counts.bytes} bytes), dropped ${counts.dropped} of ${counts.in}`;
let lastIn = 0;
setInterval(() => { if (counts.in !== lastIn) { lastIn = counts.in; console.log(summary()); } }, 10000).unref();
for (const sig of ["SIGINT", "SIGTERM"]) process.on(sig, () => { console.log(summary()); process.exit(0); });
