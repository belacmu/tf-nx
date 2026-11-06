#!/usr/bin/env node
// Joins the first open public lobby whose host claims Platform "Switch", the way a TF.EX browser joiner does (LOBBY-PROTOCOL.md 4.2):
// Identify, GetLobbies (polled), JoinLobby, the full UpdatePlayer (not ready), then ready with archer 1. Logs every server message and
// exits 0 on StartLobby (or 2 after --timeout seconds).
//   node tools/lobby-joiner.mjs [--server ws://127.0.0.1:3100] [--timeout 180]
const args = process.argv.slice(2);
const arg = (n, d) => { const i = args.indexOf(`--${n}`); return i >= 0 ? args[i + 1] : d; };
const server = arg("server", "ws://127.0.0.1:3100");
const timeout = Number(arg("timeout", 180));
const name = "JOINER";
const ws = new WebSocket(`${server}/ws`);
ws.binaryType = "arraybuffer";
let me = null, joined = false;
const send = (o) => ws.send(new TextEncoder().encode(JSON.stringify(o)));   // TF.EX sends binary frames of UTF-8 JSON
const player = (ready) => ({ Name: name, ArcherIndex: 1, ArcherAltIndex: 0, Ready: ready, RoomPeerId: me?.RoomPeerId ?? "", IsHost: false,
	Seat: me?.Seat ?? 1, Team: -1, CustomArcherId: "", ArcherMods: [], CustomVariants: [], UseInstantReplay: false,
	HaveModifiedGameFiles: false, Platform: "Windows", InputDelay: 2 });
const log = (...a) => console.log(new Date().toISOString().slice(11, 23), ...a);
setTimeout(() => { log("timeout"); process.exit(2); }, timeout * 1000);
ws.onopen = () => { log("connected"); send({ Identify: { player_id: crypto.randomUUID(), name } }); poll(); };
function poll() { if (!joined) { send({ GetLobbies: {} }); setTimeout(poll, 1000); } }
ws.onmessage = (ev) => {
	const text = typeof ev.data === "string" ? ev.data : new TextDecoder().decode(ev.data);
	const msg = JSON.parse(text), tag = Object.keys(msg)[0], body = msg[tag];
	if (tag === "KeepAlive") { send(msg); return; }
	if (tag === "GetLobbiesResponse") {
		const l = body.lobbies.find((x) => !x.InGame && x.Players.some((p) => p.IsHost && p.Platform === "Switch"));
		if (l && !joined) { joined = true; log("joining", l.Name, l.RoomId); send({ JoinLobby: { room_id: l.RoomId, name, is_player: true } }); }
		return;
	}
	log(tag, JSON.stringify(body).slice(0, 400));
	if (tag === "JoinLobbyResponse" && body.success) {
		me = body.lobby.Players.find((p) => p.RoomPeerId === body.room_peer_id);
		send({ UpdatePlayer: { player: player(false) } });
		setTimeout(() => send({ UpdatePlayer: { player: player(true) } }), 1500);
	}
	if (tag === "StartLobby") { log("StartLobby received: the match would start now"); setTimeout(() => process.exit(0), 500); }
};
ws.onclose = (e) => { log("closed", e.code, e.reason); process.exit(1); };
