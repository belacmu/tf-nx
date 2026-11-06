// A WebSocket relay for VIABILITY-TESTS 7c, on Cloudflare: Workers + one Durable Object per room.
// The same behaviour as ../tools/relay-server.mjs: peers join /relay/<room>?peer=<uuid>, and every
// message one sends goes to the others unchanged. GET /echo.html?room=<room> serves a page that
// joins and echoes every binary message back (a browser as the other player).
//
// It is open to anyone who knows the address, so it is small on purpose: 4 peers per room, messages
// up to 1 KB, and a connection that sends more than RATE messages a second is closed. Browsers may
// only connect from this Worker's own pages (the Switch and Node send no Origin).
import { DurableObject } from "cloudflare:workers";

const MAX_PEERS = 4;
const MAX_MESSAGE = 1024;
const BURST = 300;
const RATE = 150; // messages per second per connection (the test sends 60)

const PAGE = (room) => `<!doctype html><meta charset=utf-8><title>relay echo</title>
<body style="font:16px system-ui;margin:2em"><h3>relay echo peer</h3><p id=s>connecting…</p>
<script>
const room = ${JSON.stringify(room)}, id = crypto.randomUUID();
const ws = new WebSocket("wss://" + location.host + "/relay/" + encodeURIComponent(room) + "?peer=" + id);
ws.binaryType = "arraybuffer";
let n = 0;
ws.onopen = () => s.textContent = "joined " + room + " as " + id;
ws.onmessage = (e) => { ws.send(e.data); n++; if (n % 60 === 0) s.textContent = "echoed " + n + " messages in " + room; };
ws.onclose = () => s.textContent += " (closed after " + n + ")";
</script>`;

export default {
	async fetch(request, env) {
		const url = new URL(request.url);
		if (url.pathname === "/echo.html") {
			return new Response(PAGE(url.searchParams.get("room") ?? "relaytest"), { headers: { "content-type": "text/html" } });
		}
		const m = /^\/relay\/([A-Za-z0-9_-]{1,64})$/.exec(url.pathname);
		if (!m) return new Response("not found", { status: 404 });
		if (request.headers.get("Upgrade")?.toLowerCase() !== "websocket") return new Response("expected a WebSocket", { status: 426 });
		const origin = request.headers.get("Origin");
		if (origin && origin !== url.origin) return new Response("origin not allowed", { status: 403 });
		const peer = url.searchParams.get("peer") ?? "";
		if (!/^[0-9a-f-]{8,64}$/i.test(peer)) return new Response("peer must be a UUID", { status: 400 });
		return env.RELAY.get(env.RELAY.idFromName(m[1])).fetch(request);
	},
};

export class RelayRoom extends DurableObject {
	async fetch(request) {
		if (this.ctx.getWebSockets().length >= MAX_PEERS) return new Response("room is full", { status: 429 });
		const pair = new WebSocketPair();
		this.ctx.acceptWebSocket(pair[1]);
		pair[1].serializeAttachment({ tokens: BURST, at: Date.now() });
		return new Response(null, { status: 101, webSocket: pair[0] });
	}

	webSocketMessage(ws, message) {
		const size = typeof message === "string" ? message.length : message.byteLength;
		if (size > MAX_MESSAGE) return ws.close(1009, "message too big");
		const now = Date.now();
		const state = ws.deserializeAttachment() ?? { tokens: BURST, at: now };
		state.tokens = Math.min(BURST, state.tokens + ((now - state.at) / 1000) * RATE) - 1;
		state.at = now;
		if (state.tokens < 0) return ws.close(1008, "too many messages");
		ws.serializeAttachment(state);
		for (const other of this.ctx.getWebSockets()) if (other !== ws) other.send(message);
	}

	webSocketClose(ws, code) {
		try { ws.close(code === 1005 || code === 1006 ? 1000 : code); } catch {}
	}
}
