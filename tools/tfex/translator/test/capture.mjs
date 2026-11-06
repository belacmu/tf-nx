// Captures real lobby-server messages for the translator's model test: runs a scripted session of two TF.EX-shaped clients
// against a tfex-server (the browser repo's cloudflare/tfex-server/local.mjs) and writes every message, both directions, as
// <dir>/<nn>-<from>-<Message>.json. Usage: node capture.mjs ws://127.0.0.1:<port>/ws <dir>
import { mkdirSync, writeFileSync } from "node:fs";
const [url, dir] = process.argv.slice(2);
mkdirSync(dir, { recursive: true });
let n = 0;
const save = (from, text) => {
  const name = Object.keys(JSON.parse(text))[0];
  writeFileSync(`${dir}/${String(++n).padStart(2, "0")}-${from}-${name}.json`, text);
};
const player = (name, extra = {}) => ({
  Name: name, ArcherIndex: 0, ArcherAltIndex: 0, Ready: false, RoomPeerId: "", IsHost: false, Seat: 0, Team: -1,
  CustomArcherId: "", ArcherMods: [], CustomVariants: [], UseInstantReplay: true, HaveModifiedGameFiles: false,
  Platform: "Windows", InputDelay: null, ...extra,
});
const lobby = {
  Name: "Translator \"test\" lobby", RoomId: "5f0c2a8e-3b1d-4c55-9e1a-7d2f0b6a9c41", MaxPlayers: 4, Players: [player("ALICE", { IsHost: true, ArcherIndex: 3 })], Spectators: [],
  GameData: { MapId: -1, Mode: 3, MatchLength: 2, Variants: ["ClumsyArchers", "SlowShots"], Seed: 0, BestOf: 0 },
  EndGameChoice: [], Mods: [{ Name: "Teuria.WiderSet", Data: { IsWide: "True", Version: "0.9.0" } }, { Name: "DShad.TF.EX", Data: { Version: "0.21.0" } }],
  InGame: false, Kind: "Standard", JoinCode: "", Series: null, RelayedPairs: [], IsCheckingConnections: false, IsReportEnabled: false,
  MatchPlayers: [], MissingVariants: [],
};
function client(tag) {
  const ws = new WebSocket(url);
  const inbox = [];
  let wake = null;
  ws.onmessage = async (e) => {
    const text = typeof e.data === "string" ? e.data : Buffer.from(await e.data.arrayBuffer()).toString();
    save(`s2${tag}`, text); inbox.push(JSON.parse(text)); wake?.();
  };
  const opened = new Promise((r) => (ws.onopen = r));
  return {
    opened,
    send(obj) { const t = JSON.stringify(obj); save(`${tag}2s`, t); ws.send(t); },
    async next(name, ms = 3000) {
      const t0 = Date.now();
      for (;;) {
        const i = inbox.findIndex((m) => name in m);
        if (i >= 0) return inbox.splice(i, 1)[0][name];
        if (Date.now() - t0 > ms) throw new Error(`${tag}: no ${name}`);
        await new Promise((r) => { wake = r; setTimeout(r, 100); });
      }
    },
    close() { ws.close(); },
  };
}
const a = client("a"), b = client("b");
await a.opened; await b.opened;
a.send({ Identify: { player_id: "translator-a", name: "ALICE" } });
b.send({ Identify: { player_id: "translator-b", name: "BOB" } });
b.send({ GetLobbies: {} }); await b.next("GetLobbiesResponse");
a.send({ CreateLobby: { lobby } });
const created = await a.next("CreateLobbyResponse");
b.send({ GetLobbies: {} }); await b.next("GetLobbiesResponse");
b.send({ JoinLobby: { room_id: created.lobby.RoomId, name: "BOB", is_player: true } });
const joined = await b.next("JoinLobbyResponse"); await a.next("LobbyUpdate");
b.send({ UpdatePlayer: { player: player("BOB", { RoomPeerId: joined.room_peer_id, Seat: 1, Ready: true, ArcherIndex: 5, InputDelay: 2, Platform: "Switch" }) } });
await a.next("LobbyUpdate");
a.send({ UpdateLobbySettings: { max_players: 4, game_data: { ...lobby.GameData, MapId: 7, Mode: 5 }, mods: lobby.Mods } });
await a.next("LobbyUpdate");
a.send({ UpdatePlayer: { player: player("ALICE", { RoomPeerId: created.lobby.Players[0].RoomPeerId, IsHost: true, Ready: true, Team: 0 }) } });
await a.next("LobbyUpdate");
b.send({ UpdatePlayer: { player: player("BOB", { RoomPeerId: joined.room_peer_id, Seat: 1, Ready: true, Team: 1, InputDelay: 2, Platform: "Switch" }) } });
await a.next("LobbyUpdate");
a.send({ StartLobbyChoice: {} });
await a.next("StartLobby");
a.send({ MatchEnded: { winner_seat: 1, frame: 4321, checksum: null, scores: [3, 5] } });
a.send({ RematchLobbyChoice: {} }); b.send({ RematchLobbyChoice: {} });
await a.next("RematchLobby", 5000).catch(() => {});
b.send({ LeaveLobby: {} }); await b.next("LeaveLobbyResponse");
await a.next("LobbyUpdate").catch(() => {});
await new Promise((r) => setTimeout(r, 300));
a.close(); b.close();
console.error(`capture: ${n} messages`);
process.exit(0);
