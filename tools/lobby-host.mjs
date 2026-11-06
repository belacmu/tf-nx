#!/usr/bin/env node
// Hosts one public lobby the way a TF.EX PC host does (LOBBY-PROTOCOL.md 4.1: Identify, CreateLobby) and keeps it open, for the
// Switch's lobby browser to list. --wide: the lobby carries WiderSet with IsWide True (MainMenuPatch.AddWiderSetLobbyMod), which a
// standard Switch refuses ("WIDERSET MOD ON WIDE REQUIRED"). Logs every server message; exits after --timeout seconds.
//   node tools/lobby-host.mjs [--server ws://127.0.0.1:3100] [--timeout 180] [--wide] [--name PCHOST]
const args = process.argv.slice(2);
const arg = (n, d) => { const i = args.indexOf(`--${n}`); return i >= 0 ? args[i + 1] : d; };
const server = arg("server", "ws://127.0.0.1:3100");
const timeout = Number(arg("timeout", 180));
const name = arg("name", "PCHOST");
const wide = args.includes("--wide");
const ws = new WebSocket(`${server}/ws`);
ws.binaryType = "arraybuffer";
const send = (o) => ws.send(new TextEncoder().encode(JSON.stringify(o)));   // TF.EX sends binary frames of UTF-8 JSON
const log = (...a) => console.log(new Date().toISOString().slice(11, 23), ...a);
setTimeout(() => { log("timeout"); process.exit(0); }, timeout * 1000);
const mods = [{ Name: "TF.EX", Data: { Version: "1" } }];
mods.push({ Name: "Teuria.WiderSet", Data: { IsWide: wide ? "True" : "False", Version: "0.9.0" } });
const lobby = { Name: name, RoomId: crypto.randomUUID(), MaxPlayers: 2,
	Players: [{ Name: name, ArcherIndex: 0, ArcherAltIndex: 0, Ready: false, RoomPeerId: "", IsHost: true, Seat: 0, Team: -1,
		CustomArcherId: "", ArcherMods: [], CustomVariants: [], UseInstantReplay: false, HaveModifiedGameFiles: false,
		Platform: "Windows", InputDelay: 2 }],
	Spectators: [], GameData: { MapId: 0, Mode: 3, MatchLength: 2, Variants: [], Seed: 0, BestOf: 0 },
	EndGameChoice: [], Mods: mods, InGame: false, Kind: "Standard", JoinCode: "", Series: null, MissingVariants: [] };
ws.onopen = () => {
	log("connected");
	send({ Identify: { player_id: crypto.randomUUID(), name } });
	send({ CreateLobby: { lobby } });
};
ws.onmessage = (ev) => {
	const text = typeof ev.data === "string" ? ev.data : new TextDecoder().decode(ev.data);
	const msg = JSON.parse(text), tag = Object.keys(msg)[0], body = msg[tag];
	if (tag === "KeepAlive") { send(msg); return; }
	log(tag, JSON.stringify(body).slice(0, 300));
};
ws.onclose = (e) => { log("closed", e.code, e.reason); process.exit(1); };
