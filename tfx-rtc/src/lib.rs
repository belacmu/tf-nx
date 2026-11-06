//! A matchbox (TF.EX) peer over WebRTC, sans-IO, for the Switch.
//!
//! It speaks what the browser's netplay/tfnet.js and TF.EX's matchbox_socket speak:
//! - signaling: JSON text over the room's WebSocket. Server -> peer: {"IdAssigned":id},
//!   {"NewPeer":id} (the receiver makes the offer), {"PeerLeft":id},
//!   {"Signal":{"sender":id,"data":S}}; peer -> server: {"Signal":{"receiver":id,"data":S}} and
//!   "KeepAlive", where S is {"Offer":sdp} | {"Answer":sdp} | {"IceCandidate":json or "null"};
//! - one data channel: negotiated, id 0, unordered, no retransmits; packets are raw binary.
//!
//! The caller owns the WebSocket and the UDP socket: it passes in signaling text and datagrams and
//! takes out signaling text and datagrams to send (str0m does no I/O of its own).

use std::collections::VecDeque;
use std::net::SocketAddr;
use std::time::{Duration, Instant};

use serde_json::{json, Value};
use str0m::change::{SdpAnswer, SdpOffer, SdpPendingOffer};
use str0m::channel::{ChannelConfig, ChannelId, Reliability};
use str0m::net::{Protocol, Receive};
use str0m::{Candidate, Event, IceConnectionState, Input, Output, Rtc};

pub use str0m;

pub const CHANNEL_LABEL: &str = "matchbox_socket_0";

/// The most a channel may hold unsent or unacknowledged before `send` refuses packets (~1 s of GGRS at 120 packets of ~60 B).
pub const MAX_BUFFERED: usize = 8 * 1024;

/// Counters since the last `take_stats` (a window): what went through the mesh and the time spent inside it.
#[derive(Debug, Clone, Default)]
pub struct Stats {
    /// Datagrams handed in by `on_udp`, and their bytes.
    pub dg_in: u64,
    pub bytes_in: u64,
    /// Datagrams queued for the UDP socket, their bytes, and the largest one.
    pub dg_out: u64,
    pub bytes_out: u64,
    pub max_out: u64,
    /// Data channel packets received / written / refused by the channel.
    pub data_in: u64,
    pub data_out: u64,
    pub data_refused: u64,
    /// Time inside `on_udp` (DTLS, SCTP and their replies), `drive` and `send`, in microseconds.
    pub us_in: u64,
    pub us_drive: u64,
    pub us_send: u64,
    /// The largest per-call time of each, in microseconds.
    pub max_us_in: u64,
    pub max_us_drive: u64,
    /// At the window's end: the channels' SCTP buffered amount (bytes written, not yet acknowledged or abandoned), the
    /// datagrams and events waiting in the mesh's queues.
    pub buffered: u64,
    pub udp_queued: u64,
    pub events_queued: u64,
    /// STUN datagrams (ICE checks and consent) in and out, of the datagrams above.
    pub stun_in: u64,
    pub stun_out: u64,
}

#[derive(Debug, Clone, PartialEq)]
pub enum MeshEvent {
    /// The server assigned this peer's id.
    Id(String),
    /// The data channel to this peer opened.
    Connected(String),
    /// This peer left or its connection failed.
    Disconnected(String),
    /// A packet from this peer.
    Data(String, Vec<u8>),
}

struct Peer {
    id: String,
    rtc: Rtc,
    chan: Option<ChannelId>,
    open: bool,
    pending_offer: Option<SdpPendingOffer>,
    remote_set: bool,
    pending_candidates: Vec<String>,
    gone: bool,
}

pub struct Mesh {
    local: Vec<Candidate>,
    /// The UDP socket's own address (the base of a server-reflexive candidate).
    base: Option<SocketAddr>,
    /// The transaction id of the STUN binding request in flight.
    stun: Option<[u8; 12]>,
    peers: Vec<Peer>,
    signal_out: VecDeque<String>,
    udp_out: VecDeque<(SocketAddr, SocketAddr, Vec<u8>)>,
    events: VecDeque<MeshEvent>,
    id: Option<String>,
    log: Box<dyn FnMut(&str)>,
    stats: Stats,
}

fn us_since(t: Instant) -> u64 {
    t.elapsed().as_micros() as u64
}

fn channel_config() -> ChannelConfig {
    ChannelConfig {
        label: CHANNEL_LABEL.into(),
        ordered: false,
        reliability: Reliability::MaxRetransmits { retransmits: 0 },
        negotiated: Some(0),
        protocol: String::new(),
    }
}

impl Mesh {
    /// `local`: the candidates to offer (host addresses of the UDP socket, server-reflexive ones).
    pub fn new(local: Vec<Candidate>, log: Box<dyn FnMut(&str)>) -> Mesh {
        static INIT: std::sync::Once = std::sync::Once::new();
        INIT.call_once(|| str0m_rust_crypto::default_provider().install_process_default());
        let base = local.first().map(|c| c.addr()); // (the host candidate)
        Mesh { local, base, stun: None, peers: Vec::new(), signal_out: VecDeque::new(), udp_out: VecDeque::new(), events: VecDeque::new(), id: None, log, stats: Stats::default() }
    }

    pub fn id(&self) -> Option<&str> {
        self.id.as_deref()
    }

    fn new_rtc(&self, now: Instant) -> Rtc {
        let mut rtc = Rtc::builder().build(now);
        for c in &self.local {
            rtc.add_local_candidate(c.clone());
        }
        rtc
    }

    fn signal(&mut self, receiver: &str, data: Value) {
        self.signal_out.push_back(json!({ "Signal": { "receiver": receiver, "data": data } }).to_string());
    }

    fn peer_index(&self, id: &str) -> Option<usize> {
        self.peers.iter().position(|p| p.id == id && !p.gone)
    }

    fn add_peer(&mut self, id: &str, now: Instant) -> usize {
        let rtc = self.new_rtc(now);
        self.peers.push(Peer { id: id.into(), rtc, chan: None, open: false, pending_offer: None, remote_set: false, pending_candidates: Vec::new(), gone: false });
        self.peers.len() - 1
    }

    /// One text message from the signaling WebSocket.
    pub fn on_signal_text(&mut self, text: &str, now: Instant) {
        let Ok(msg) = serde_json::from_str::<Value>(text) else { return };
        if let Some(id) = msg.get("IdAssigned").and_then(Value::as_str) {
            self.id = Some(id.into());
            self.events.push_back(MeshEvent::Id(id.into()));
        } else if let Some(id) = msg.get("NewPeer").and_then(Value::as_str) {
            self.offer(id, now);
        } else if let Some(id) = msg.get("PeerLeft").and_then(Value::as_str) {
            if let Some(i) = self.peer_index(id) {
                self.drop_peer(i);
            }
        } else if let Some(sig) = msg.get("Signal") {
            let (Some(sender), Some(data)) = (sig.get("sender").and_then(Value::as_str), sig.get("data")) else { return };
            let sender = sender.to_string();
            if let Some(sdp) = data.get("Offer").and_then(Value::as_str) {
                self.accept(&sender, sdp, now);
            } else if let Some(sdp) = data.get("Answer").and_then(Value::as_str) {
                self.answered(&sender, sdp);
            } else if let Some(c) = data.get("IceCandidate").and_then(Value::as_str) {
                let i = match self.peer_index(&sender) {
                    Some(i) => i,
                    None => self.add_peer(&sender, now),
                };
                if self.peers[i].remote_set {
                    self.add_candidate(i, c.to_string());
                } else {
                    self.peers[i].pending_candidates.push(c.to_string());
                }
            }
        }
    }

    fn offer(&mut self, id: &str, now: Instant) {
        let i = self.add_peer(id, now);
        let mut api = self.peers[i].rtc.sdp_api();
        let chan = api.add_channel_with_config(channel_config());
        let Some((offer, pending)) = api.apply() else { return };
        self.peers[i].chan = Some(chan);
        self.peers[i].pending_offer = Some(pending);
        (self.log)(&format!("[rtc] offer to {id}"));
        self.signal(id, json!({ "Offer": offer.to_sdp_string() }));
    }

    fn accept(&mut self, id: &str, sdp: &str, _now: Instant) {
        let i = match self.peer_index(id) {
            Some(i) => i,
            None => self.add_peer(id, _now),
        };
        let offer = match SdpOffer::from_sdp_string(sdp) {
            Ok(o) => o,
            Err(e) => return (self.log)(&format!("[rtc] bad offer from {id}: {e}")),
        };
        let answer = match self.peers[i].rtc.sdp_api().accept_offer(offer) {
            Ok(a) => a,
            Err(e) => return (self.log)(&format!("[rtc] offer from {id} refused: {e}")),
        };
        let chan = self.peers[i].rtc.direct_api().create_data_channel(channel_config());
        self.peers[i].chan = Some(chan);
        self.remote_set(i);
        (self.log)(&format!("[rtc] answer to {id}"));
        self.signal(id, json!({ "Answer": answer.to_sdp_string() }));
    }

    fn answered(&mut self, id: &str, sdp: &str) {
        let Some(i) = self.peer_index(id) else { return };
        let Some(pending) = self.peers[i].pending_offer.take() else { return };
        let answer = match SdpAnswer::from_sdp_string(sdp) {
            Ok(a) => a,
            Err(e) => return (self.log)(&format!("[rtc] bad answer from {id}: {e}")),
        };
        if let Err(e) = self.peers[i].rtc.sdp_api().accept_answer(pending, answer) {
            return (self.log)(&format!("[rtc] answer from {id} refused: {e}"));
        }
        self.remote_set(i);
    }

    fn remote_set(&mut self, i: usize) {
        self.peers[i].remote_set = true;
        for c in std::mem::take(&mut self.peers[i].pending_candidates) {
            self.add_candidate(i, c);
        }
    }

    // `json`: an RTCIceCandidateInit as JSON, or "null" (end of candidates).
    fn add_candidate(&mut self, i: usize, json: String) {
        let Ok(v) = serde_json::from_str::<Value>(&json) else { return };
        let Some(line) = v.get("candidate").and_then(Value::as_str) else { return };
        if line.is_empty() {
            return;
        }
        match Candidate::from_sdp_string(line.strip_prefix("a=").unwrap_or(line)) {
            Ok(c) => self.peers[i].rtc.add_remote_candidate(c),
            Err(e) => (self.log)(&format!("[rtc] ignoring candidate {line}: {e}")),
        }
    }

    fn drop_peer(&mut self, i: usize) {
        let p = &mut self.peers[i];
        if p.gone {
            return;
        }
        p.gone = true;
        p.rtc.disconnect();
        self.events.push_back(MeshEvent::Disconnected(p.id.clone()));
    }

    /// Asks a STUN server for this socket's public address (a server-reflexive candidate, which
    /// later offers and answers carry and earlier connections get trickled).
    pub fn stun_start(&mut self, server: SocketAddr, txid: [u8; 12]) {
        let Some(base) = self.base else { return };
        self.stun = Some(txid);
        self.udp_out.push_back((base, server, stun::request(&txid)));
    }

    /// The server-reflexive address once the STUN server answered.
    pub fn reflexive(&self) -> Option<SocketAddr> {
        self.local.iter().find(|c| c.kind() == str0m::CandidateKind::ServerReflexive).map(|c| c.addr())
    }

    fn on_stun_answer(&mut self, mapped: SocketAddr) {
        let Some(base) = self.base else { return };
        self.stun = None;
        if mapped == base || self.reflexive().is_some() {
            return;
        }
        let Ok(c) = Candidate::server_reflexive(mapped, base, "udp") else { return };
        (self.log)(&format!("[rtc] public address {mapped}"));
        self.local.push(c.clone());
        let mut trickle = Vec::new();
        for p in self.peers.iter_mut().filter(|p| !p.gone) {
            if let Some(added) = p.rtc.add_local_candidate(c.clone()) {
                trickle.push((p.id.clone(), added.to_sdp_string()));
            }
        }
        for (id, line) in trickle {
            let init = json!({ "candidate": line, "sdpMid": "0", "sdpMLineIndex": 0 }).to_string();
            self.signal(&id, json!({ "IceCandidate": init }));
        }
    }

    /// A datagram that arrived on the UDP socket.
    pub fn on_udp(&mut self, now: Instant, source: SocketAddr, destination: SocketAddr, data: &[u8]) {
        let t = Instant::now();
        self.stats.dg_in += 1;
        self.stats.bytes_in += data.len() as u64;
        self.stats.stun_in += data.first().is_some_and(|b| *b < 2) as u64;
        self.on_udp_inner(now, source, destination, data);
        let us = us_since(t);
        self.stats.us_in += us;
        self.stats.max_us_in = self.stats.max_us_in.max(us);
    }

    fn on_udp_inner(&mut self, now: Instant, source: SocketAddr, destination: SocketAddr, data: &[u8]) {
        if let Some(txid) = self.stun {
            if let Some(mapped) = stun::answer(data, &txid) {
                return self.on_stun_answer(mapped);
            }
        }
        let Ok(recv) = Receive::new(Protocol::Udp, source, destination, data) else { return };
        let input = Input::Receive(now, recv);
        let Some(i) = self.peers.iter().position(|p| !p.gone && p.rtc.accepts(&input)) else { return };
        if let Err(e) = self.peers[i].rtc.handle_input(input) {
            (self.log)(&format!("[rtc] input from {source}: {e}"));
        }
        self.pump(i); // (DTLS and SCTP queue only a few datagrams: take their output now)
    }

    // Everything one connection has to say until it next wants a timeout; returns that time.
    fn pump(&mut self, i: usize) -> Option<Instant> {
        loop {
            if self.peers[i].gone {
                return None;
            }
            let out = match self.peers[i].rtc.poll_output() {
                Ok(o) => o,
                Err(e) => {
                    (self.log)(&format!("[rtc] {}: {e}", self.peers[i].id));
                    self.drop_peer(i);
                    return None;
                }
            };
            match out {
                Output::Timeout(t) => return Some(t),
                Output::Transmit(t) => {
                    let n = t.contents.len() as u64;
                    self.stats.dg_out += 1;
                    self.stats.bytes_out += n;
                    self.stats.max_out = self.stats.max_out.max(n);
                    self.stats.stun_out += t.contents.first().is_some_and(|b| *b < 2) as u64;
                    self.udp_out.push_back((t.source, t.destination, t.contents.to_vec()))
                }
                Output::Event(e) => self.on_event(i, e),
            }
        }
    }

    /// Advances every connection to `now`; returns when to call again at the latest.
    pub fn drive(&mut self, now: Instant) -> Instant {
        let t = Instant::now();
        let next = self.drive_inner(now);
        let us = us_since(t);
        self.stats.us_drive += us;
        self.stats.max_us_drive = self.stats.max_us_drive.max(us);
        next
    }

    fn drive_inner(&mut self, now: Instant) -> Instant {
        let mut next = now + Duration::from_millis(100);
        for i in 0..self.peers.len() {
            if self.peers[i].gone {
                continue;
            }
            if let Err(e) = self.peers[i].rtc.handle_input(Input::Timeout(now)) {
                (self.log)(&format!("[rtc] timeout: {e}"));
            }
            if let Some(t) = self.pump(i) {
                next = next.min(t);
            }
        }
        next
    }

    fn on_event(&mut self, i: usize, e: Event) {
        let id = self.peers[i].id.clone();
        match e {
            Event::IceConnectionStateChange(s) => {
                (self.log)(&format!("[rtc] {id}: ICE {s:?}"));
                if s == IceConnectionState::Disconnected {
                    self.drop_peer(i);
                }
            }
            Event::ChannelOpen(c, label) => {
                (self.log)(&format!("[rtc] {id}: channel {label} open"));
                if Some(c) == self.peers[i].chan && !self.peers[i].open {
                    self.peers[i].open = true;
                    self.events.push_back(MeshEvent::Connected(id));
                }
            }
            Event::ChannelData(d) => {
                self.stats.data_in += 1;
                self.events.push_back(MeshEvent::Data(id, d.data.to_vec()))
            }
            Event::ChannelClose(_) => self.drop_peer(i),
            _ => {}
        }
    }

    /// Sends one packet on the data channel; false if that peer's channel isn't open.
    pub fn send(&mut self, peer: &str, data: &[u8]) -> bool {
        let t = Instant::now();
        let ok = self.send_inner(peer, data);
        self.stats.us_send += us_since(t);
        if ok {
            self.stats.data_out += 1;
        } else {
            self.stats.data_refused += 1;
        }
        ok
    }

    fn send_inner(&mut self, peer: &str, data: &[u8]) -> bool {
        let Some(i) = self.peer_index(peer) else { return false };
        let p = &mut self.peers[i];
        let (true, Some(c)) = (p.open, p.chan) else { return false };
        match p.rtc.channel(c) {
            // Unreliable packets that SCTP can't send yet (congestion window) wait in its pending queue, which has no bound: past
            // MAX_BUFFERED a packet is refused instead (GGRS resends what isn't acknowledged), so a stall can't queue up seconds of
            // stale packets that then go out ahead of the current ones.
            Some(mut ch) => ch.buffered_amount() <= MAX_BUFFERED && ch.write(true, data).unwrap_or(false),
            None => false,
        }
    }

    /// Queues the "KeepAlive" the server wants every 10 s.
    pub fn signal_out_keepalive(&mut self) {
        self.signal_out.push_back("\"KeepAlive\"".into());
    }

    pub fn poll_signal(&mut self) -> Option<String> {
        self.signal_out.pop_front()
    }

    /// (source, destination, bytes)
    pub fn poll_udp(&mut self) -> Option<(SocketAddr, SocketAddr, Vec<u8>)> {
        self.udp_out.pop_front()
    }

    pub fn poll_event(&mut self) -> Option<MeshEvent> {
        self.events.pop_front()
    }

    /// This window's counters (and the queues' state now); starts the next window.
    pub fn take_stats(&mut self) -> Stats {
        let mut buffered = 0;
        for p in self.peers.iter_mut().filter(|p| !p.gone && p.open) {
            if let Some(mut ch) = p.chan.and_then(|c| p.rtc.channel(c)) {
                buffered += ch.buffered_amount() as u64;
            }
        }
        let mut s = std::mem::take(&mut self.stats);
        s.buffered = buffered;
        s.udp_queued = self.udp_out.len() as u64;
        s.events_queued = self.events.len() as u64;
        s
    }
}

/// A STUN binding request and its answer (RFC 5389), just enough for the public address.
pub mod stun {
    use std::net::{IpAddr, Ipv4Addr, Ipv6Addr, SocketAddr};

    const COOKIE: u32 = 0x2112_a442;

    pub fn request(txid: &[u8; 12]) -> Vec<u8> {
        let mut v = vec![0x00, 0x01, 0x00, 0x00];
        v.extend_from_slice(&COOKIE.to_be_bytes());
        v.extend_from_slice(txid);
        v
    }

    /// The mapped address of a binding success response to `txid`.
    pub fn answer(d: &[u8], txid: &[u8; 12]) -> Option<SocketAddr> {
        if d.len() < 20 || d[0..2] != [0x01, 0x01] || d[4..8] != COOKIE.to_be_bytes() || &d[8..20] != txid {
            return None;
        }
        let len = u16::from_be_bytes([d[2], d[3]]) as usize;
        let body = d.get(20..20 + len)?;
        let (mut i, mut plain) = (0, None);
        while i + 4 <= body.len() {
            let t = u16::from_be_bytes([body[i], body[i + 1]]);
            let n = u16::from_be_bytes([body[i + 2], body[i + 3]]) as usize;
            let v = body.get(i + 4..i + 4 + n)?;
            match t {
                0x0020 => return addr(v, true, txid),
                0x0001 => plain = addr(v, false, txid),
                _ => {}
            }
            i += 4 + (n + 3) / 4 * 4;
        }
        plain
    }

    fn addr(v: &[u8], xor: bool, txid: &[u8; 12]) -> Option<SocketAddr> {
        if v.len() < 8 {
            return None;
        }
        let c = COOKIE.to_be_bytes();
        let port = u16::from_be_bytes([v[2], v[3]]) ^ if xor { (COOKIE >> 16) as u16 } else { 0 };
        let ip = match v[1] {
            1 => {
                let mut b = [v[4], v[5], v[6], v[7]];
                if xor {
                    for k in 0..4 {
                        b[k] ^= c[k];
                    }
                }
                IpAddr::V4(Ipv4Addr::from(b))
            }
            2 if v.len() >= 20 => {
                let mut b = [0u8; 16];
                b.copy_from_slice(&v[4..20]);
                if xor {
                    for k in 0..16 {
                        b[k] ^= if k < 4 { c[k] } else { txid[k - 4] };
                    }
                }
                IpAddr::V6(Ipv6Addr::from(b))
            }
            _ => return None,
        };
        Some(SocketAddr::new(ip, port))
    }
}

#[cfg(test)]
mod tests {
    use super::stun;

    #[test]
    fn stun_xor_mapped_v4() {
        // RFC 5769 2.2 (IPv4 response): 192.0.2.1:32853.
        let txid = [0xb7, 0xe7, 0xa7, 0x01, 0xbc, 0x34, 0xd6, 0x86, 0xfa, 0x87, 0xdf, 0xae];
        let mut d = vec![0x01, 0x01, 0x00, 0x0c, 0x21, 0x12, 0xa4, 0x42];
        d.extend_from_slice(&txid);
        d.extend_from_slice(&[0x00, 0x20, 0x00, 0x08, 0x00, 0x01, 0xa1, 0x47, 0xe1, 0x12, 0xa6, 0x43]);
        assert_eq!(stun::answer(&d, &txid), Some("192.0.2.1:32853".parse().unwrap()));
        assert_eq!(stun::answer(&d, &[0; 12]), None);
    }
}
