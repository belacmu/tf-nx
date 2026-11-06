//! The part of matchbox_socket 0.12's API that ggrs-ffi uses, for the Switch (the browser's twin is
//! netplay/matchbox-browser in the browser repo). WebRTC and the matchbox signaling protocol are tfx-rtc's `Mesh`
//! (signaling, peer roles and the unreliable channel match matchbox, so Switch peers interoperate with PC and browser
//! ones). The Switch's sockets belong to the module (net.cpp, nn::socket), so a socket here only queues: the module asks
//! for sockets to open (`netplay_switch_socket_next_open`), binds a UDP socket and hands its address
//! (`netplay_switch_socket_bind`), then moves WebSocket text and datagrams in and out and drives the mesh from its own
//! thread. ggrs-ffi's side (update_peers, receive, send) runs on the game's thread; each socket's state is behind a mutex.
//!
//! C API, all `netplay_switch_socket_*` (handles are > 0; IPv4 addresses are host-order u32s, as in ggrs-switch's rtc.rs):
//! next_open(url_buf, cap) -> handle | 0, bind(h, ip, port) -> 1 | 0, stun(h, ip, port, txid), signal_in(h, text, len),
//! signal_out(h, buf, cap) -> len | 0, keepalive(h), udp_in(h, src_ip, src_port, dst_ip, dst_port, data, len),
//! udp_out(h, &dst_ip, &dst_port, buf, cap) -> len | 0, drive(h) -> ms until the next drive, closed(h, failed, msg, len)
//! (the WebSocket ended), wants_close(h) -> 1 once ggrs-ffi dropped the socket (the module then closes its sockets and
//! calls release(h)), release(h), log(h, buf, cap) -> 1 | 0.
//! (The open url: TF.EX's room url, e.g. wss://host/room?next=2; the module adds nothing to it.)

use std::collections::{HashMap, HashSet, VecDeque};
use std::ffi::c_char;
use std::fmt;
use std::future::Future;
use std::net::{Ipv4Addr, SocketAddr, SocketAddrV4};
use std::pin::Pin;
use std::sync::{Arc, Mutex, MutexGuard};
use std::task::{Context, Poll};
use std::time::Instant;

use tfx_rtc::str0m::Candidate;
use tfx_rtc::{Mesh, MeshEvent};
pub use uuid::Uuid;

/// A peer's id, assigned by the signaling server.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub struct PeerId(pub Uuid);

impl fmt::Display for PeerId {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}", self.0)
    }
}

pub type Packet = Box<[u8]>;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum PeerState {
    Connected,
    Disconnected,
}

/// Why the socket's message loop ended.
#[derive(Debug)]
pub enum Error {
    ConnectionFailed(String),
    Disconnected(String),
}

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Error::ConnectionFailed(e) => write!(f, "connection to the signaling server failed: {e}"),
            Error::Disconnected(e) => write!(f, "disconnected from the signaling server: {e}"),
        }
    }
}

impl std::error::Error for Error {}

#[derive(Debug)]
pub struct ChannelError;

#[derive(Debug)]
pub struct SendError(&'static str);

impl fmt::Display for SendError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str(self.0)
    }
}

// Bounds (the module's pools rule: a full queue is logged, never silent). A peer sends ~60-120 packets a second.
const MAX_PACKETS: usize = 1024;
const MAX_LOG: usize = 256;

/// Signaling state, as the browser's tfnet_state: 0 open (or opening), 1 closed by us, 2 connection failed, 3 lost.
#[derive(Default)]
struct Inner {
    url: String,
    opened: bool, // handed to the module by next_open
    mesh: Option<Mesh>,
    state: i32,
    error: String,
    id: Option<PeerId>,
    peer_events: VecDeque<(PeerId, PeerState)>,
    packets: VecDeque<(PeerId, Packet)>,
    dropped_packets: u64,
    log: VecDeque<String>,
    dropped: bool, // ggrs-ffi dropped the socket: the module closes its side and releases the handle
}

// Mesh holds a Box<dyn FnMut> (its log sink) and str0m state that isn't Send; every access goes through the mutex.
unsafe impl Send for Inner {}

impl Inner {
    fn log(&mut self, s: String) {
        if self.log.len() < MAX_LOG {
            self.log.push_back(s);
        }
    }

    /// Moves the mesh's events into the queues ggrs-ffi reads.
    fn pump_events(&mut self) {
        let Some(mesh) = self.mesh.as_mut() else { return };
        let mut events = Vec::new();
        while let Some(e) = mesh.poll_event() {
            events.push(e);
        }
        for e in events {
            match e {
                MeshEvent::Id(id) => match Uuid::parse_str(&id) {
                    Ok(u) => self.id = Some(PeerId(u)),
                    Err(_) => self.log(format!("matchbox-switch: our id '{id}' is not a uuid")),
                },
                MeshEvent::Connected(id) => {
                    if let Ok(u) = Uuid::parse_str(&id) {
                        self.peer_events.push_back((PeerId(u), PeerState::Connected));
                    }
                }
                MeshEvent::Disconnected(id) => {
                    if let Ok(u) = Uuid::parse_str(&id) {
                        self.peer_events.push_back((PeerId(u), PeerState::Disconnected));
                    }
                }
                MeshEvent::Data(id, d) => {
                    let Ok(u) = Uuid::parse_str(&id) else { continue };
                    if self.packets.len() >= MAX_PACKETS {
                        self.dropped_packets += 1;
                        if self.dropped_packets.is_power_of_two() {
                            let n = self.dropped_packets;
                            self.log(format!("matchbox-switch: packet queue full ({MAX_PACKETS}), {n} dropped so far"));
                        }
                        continue;
                    }
                    self.packets.push_back((PeerId(u), d.into_boxed_slice()));
                }
            }
        }
    }
}

type Shared = Arc<Mutex<Inner>>;

fn lock(s: &Shared) -> MutexGuard<'_, Inner> {
    s.lock().unwrap_or_else(|p| p.into_inner())
}

static SOCKETS: Mutex<Option<HashMap<i32, Shared>>> = Mutex::new(None);
static NEXT_HANDLE: Mutex<i32> = Mutex::new(1);

fn socket(h: i32) -> Option<Shared> {
    SOCKETS.lock().unwrap_or_else(|p| p.into_inner()).as_ref()?.get(&h).cloned()
}

pub struct WebRtcSocket {
    inner: Shared,
    peers: HashSet<PeerId>,
    channel: Option<WebRtcChannel>,
    closed: bool,
}

/// Completes when the signaling connection ends (like matchbox's message loop future).
pub struct MessageLoopFuture {
    inner: Shared,
}

impl Future for MessageLoopFuture {
    type Output = Result<(), Error>;

    // Doesn't register a waker: ggrs-ffi polls this alongside a timer (on the Switch: from its pump, every frame).
    fn poll(self: Pin<&mut Self>, _cx: &mut Context<'_>) -> Poll<Self::Output> {
        let i = lock(&self.inner);
        match i.state {
            0 => Poll::Pending,
            1 => Poll::Ready(Ok(())),
            2 => Poll::Ready(Err(Error::ConnectionFailed(i.error.clone()))),
            _ => Poll::Ready(Err(Error::Disconnected(i.error.clone()))),
        }
    }
}

impl WebRtcSocket {
    /// A socket with one unreliable channel (unordered, no retransmits) in the given room. It opens once the module
    /// picks it up (netplay_switch_socket_next_open).
    pub fn new_unreliable(room_url: impl Into<String>) -> (WebRtcSocket, MessageLoopFuture) {
        let inner: Shared = Arc::new(Mutex::new(Inner { url: room_url.into(), ..Default::default() }));
        let handle = {
            let mut next = NEXT_HANDLE.lock().unwrap_or_else(|p| p.into_inner());
            let h = *next;
            *next = if h == i32::MAX { 1 } else { h + 1 };
            h
        };
        SOCKETS.lock().unwrap_or_else(|p| p.into_inner()).get_or_insert_with(HashMap::new).insert(handle, inner.clone());
        let channel = WebRtcChannel { inner: inner.clone() };
        (
            WebRtcSocket { inner: inner.clone(), peers: HashSet::new(), channel: Some(channel), closed: false },
            MessageLoopFuture { inner },
        )
    }

    /// Our id, once the signaling server has assigned it.
    pub fn id(&mut self) -> Option<PeerId> {
        lock(&self.inner).id
    }

    /// Peers that connected or disconnected since the last call.
    pub fn update_peers(&mut self) -> Vec<(PeerId, PeerState)> {
        let changes: Vec<_> = lock(&self.inner).peer_events.drain(..).collect();
        for (peer, state) in &changes {
            match state {
                PeerState::Connected => self.peers.insert(*peer),
                PeerState::Disconnected => self.peers.remove(peer),
            };
        }
        changes
    }

    pub fn try_update_peers(&mut self) -> Result<Vec<(PeerId, PeerState)>, Error> {
        let changes = self.update_peers();
        let i = lock(&self.inner);
        match i.state {
            0 => Ok(changes),
            1 => Err(Error::Disconnected("socket closed".into())),
            2 => Err(Error::ConnectionFailed(i.error.clone())),
            _ => Err(Error::Disconnected(i.error.clone())),
        }
    }

    pub fn connected_peers(&self) -> impl Iterator<Item = PeerId> + '_ {
        self.peers.iter().copied()
    }

    pub fn take_channel(&mut self, index: usize) -> Result<WebRtcChannel, ChannelError> {
        if index != 0 {
            return Err(ChannelError);
        }
        self.channel.take().ok_or(ChannelError)
    }

    pub fn channel_mut(&mut self, index: usize) -> &mut WebRtcChannel {
        assert_eq!(index, 0, "only channel 0 exists");
        self.channel.as_mut().expect("channel 0 was taken")
    }

    pub fn close(&mut self) {
        if !self.closed {
            self.closed = true;
            let mut i = lock(&self.inner);
            if i.state == 0 {
                i.state = 1;
            }
            i.dropped = true;
        }
    }
}

impl Drop for WebRtcSocket {
    fn drop(&mut self) {
        self.close();
    }
}

/// A data channel to all peers of a socket.
pub struct WebRtcChannel {
    inner: Shared,
}

impl WebRtcChannel {
    /// Like matchbox on an unreliable channel, a packet to a peer that isn't connected is dropped.
    pub fn try_send(&mut self, packet: Packet, peer: PeerId) -> Result<(), SendError> {
        let mut i = lock(&self.inner);
        if let Some(mesh) = i.mesh.as_mut() {
            mesh.send(&peer.0.to_string(), &packet);
        }
        Ok(())
    }

    pub fn receive(&mut self) -> Vec<(PeerId, Packet)> {
        let mut i = lock(&self.inner);
        i.pump_events();
        i.packets.drain(..).collect()
    }
}

// ---- the module's side ------------------------------------------------------------------------------------------------

fn v4(ip: u32, port: u16) -> SocketAddr {
    SocketAddr::V4(SocketAddrV4::new(Ipv4Addr::from(ip), port))
}

fn copy_out(s: &[u8], buf: *mut u8, cap: i32) -> i32 {
    if buf.is_null() || cap <= 0 {
        return s.len() as i32;
    }
    let n = s.len().min(cap as usize);
    unsafe { std::ptr::copy_nonoverlapping(s.as_ptr(), buf, n) };
    n as i32
}

fn copy_cstr(s: &str, buf: *mut c_char, cap: i32) {
    if buf.is_null() || cap <= 0 {
        return;
    }
    let n = s.len().min(cap as usize - 1);
    unsafe {
        std::ptr::copy_nonoverlapping(s.as_ptr(), buf as *mut u8, n);
        *buf.add(n) = 0;
    }
}

/// The next socket ggrs-ffi wants opened: its handle, its room url in `url` (NUL-terminated); 0 when there is none.
#[no_mangle]
pub extern "C" fn netplay_switch_socket_next_open(url: *mut c_char, cap: i32) -> i32 {
    let g = SOCKETS.lock().unwrap_or_else(|p| p.into_inner());
    let Some(map) = g.as_ref() else { return 0 };
    let mut hs: Vec<_> = map.keys().copied().collect();
    hs.sort();
    for h in hs {
        let mut i = lock(&map[&h]);
        if !i.opened && !i.dropped {
            i.opened = true;
            copy_cstr(&i.url, url, cap);
            return h;
        }
    }
    0
}

/// The module's UDP socket for this handle is bound to `ip:port` (its host candidate): 1, or 0 if unusable.
#[no_mangle]
pub extern "C" fn netplay_switch_socket_bind(h: i32, ip: u32, port: u16) -> i32 {
    let Some(s) = socket(h) else { return 0 };
    let Ok(host) = Candidate::host(v4(ip, port), "udp") else { return 0 };
    // The mesh's log lines go to their own queue (read by netplay_switch_socket_log): the mesh logs while the socket's
    // mutex is held, so the sink can't use the socket's queue.
    let lines: Arc<Mutex<VecDeque<String>>> = Default::default();
    let q = lines.clone();
    let mesh = Mesh::new(
        vec![host],
        Box::new(move |line| {
            if let Ok(mut q) = q.try_lock() {
                if q.len() < MAX_LOG {
                    q.push_back(line.to_string());
                }
            }
        }),
    );
    let mut i = lock(&s);
    i.mesh = Some(mesh);
    let mut logs = MESH_LOGS.lock().unwrap_or_else(|p| p.into_inner());
    logs.retain(|(k, _)| *k != h);
    logs.push((h, lines));
    1
}

static MESH_LOGS: Mutex<Vec<(i32, Arc<Mutex<VecDeque<String>>>)>> = Mutex::new(Vec::new());

fn with_mesh<R>(h: i32, none: R, f: impl FnOnce(&mut Mesh) -> R) -> R {
    let Some(s) = socket(h) else { return none };
    let mut i = lock(&s);
    let r = match i.mesh.as_mut() {
        Some(m) => f(m),
        None => none,
    };
    i.pump_events();
    r
}

/// Sends a STUN binding request (from udp_out) to learn the public address; `txid`: 12 random bytes.
#[no_mangle]
pub extern "C" fn netplay_switch_socket_stun(h: i32, ip: u32, port: u16, txid: *const u8) {
    let mut t = [0u8; 12];
    unsafe { std::ptr::copy_nonoverlapping(txid, t.as_mut_ptr(), 12) };
    with_mesh(h, (), |m| m.stun_start(v4(ip, port), t));
}

/// One text message from the room's WebSocket.
#[no_mangle]
pub extern "C" fn netplay_switch_socket_signal_in(h: i32, text: *const u8, len: i32) {
    let bytes = unsafe { std::slice::from_raw_parts(text, len.max(0) as usize) };
    let Ok(s) = std::str::from_utf8(bytes) else { return };
    with_mesh(h, (), |m| m.on_signal_text(s, Instant::now()));
}

/// The next text message for the WebSocket: its length (copied into `buf`, give 16 KB), or 0 when there is none.
#[no_mangle]
pub extern "C" fn netplay_switch_socket_signal_out(h: i32, buf: *mut u8, cap: i32) -> i32 {
    with_mesh(h, 0, |m| m.poll_signal().map_or(0, |s| copy_out(s.as_bytes(), buf, cap)))
}

/// Queues the "KeepAlive" the server wants every 10 s.
#[no_mangle]
pub extern "C" fn netplay_switch_socket_keepalive(h: i32) {
    with_mesh(h, (), |m| m.signal_out_keepalive());
}

/// One datagram that arrived on the UDP socket.
#[no_mangle]
pub extern "C" fn netplay_switch_socket_udp_in(h: i32, src_ip: u32, src_port: u16, dst_ip: u32, dst_port: u16, data: *const u8, len: i32) {
    let d = unsafe { std::slice::from_raw_parts(data, len.max(0) as usize) };
    with_mesh(h, (), |m| m.on_udp(Instant::now(), v4(src_ip, src_port), v4(dst_ip, dst_port), d));
}

/// The next datagram to send: its length (copied into `buf`) and its destination, or 0 when there is none.
#[no_mangle]
pub extern "C" fn netplay_switch_socket_udp_out(h: i32, dst_ip: *mut u32, dst_port: *mut u16, buf: *mut u8, cap: i32) -> i32 {
    with_mesh(h, 0, |m| loop {
        let Some((_, to, d)) = m.poll_udp() else { return 0 };
        let SocketAddr::V4(a) = to else { continue }; // (IPv6 peers: not on the Switch)
        unsafe {
            *dst_ip = u32::from(*a.ip());
            *dst_port = a.port();
        }
        return copy_out(&d, buf, cap);
    })
}

/// Advances every connection; returns the ms until it wants to be driven again (call it at least every frame).
#[no_mangle]
pub extern "C" fn netplay_switch_socket_drive(h: i32) -> i32 {
    with_mesh(h, 1000, |m| {
        let now = Instant::now();
        m.drive(now).saturating_duration_since(now).as_millis().min(1000) as i32
    })
}

/// The room's WebSocket ended: `failed` 1 = it never connected, 0 = it was lost; `msg` says why.
#[no_mangle]
pub extern "C" fn netplay_switch_socket_closed(h: i32, failed: i32, msg: *const u8, len: i32) {
    let Some(s) = socket(h) else { return };
    let bytes = unsafe { std::slice::from_raw_parts(msg, len.max(0) as usize) };
    let mut i = lock(&s);
    if i.state == 0 {
        i.state = if failed != 0 { 2 } else { 3 };
        i.error = String::from_utf8_lossy(bytes).into_owned();
    }
}

/// 1 once ggrs-ffi dropped this socket (or the handle is unknown): the module closes its sockets and calls release.
#[no_mangle]
pub extern "C" fn netplay_switch_socket_wants_close(h: i32) -> i32 {
    socket(h).map_or(1, |s| lock(&s).dropped as i32)
}

#[no_mangle]
pub extern "C" fn netplay_switch_socket_release(h: i32) {
    if let Some(map) = SOCKETS.lock().unwrap_or_else(|p| p.into_inner()).as_mut() {
        map.remove(&h);
    }
    MESH_LOGS.lock().unwrap_or_else(|p| p.into_inner()).retain(|(k, _)| *k != h);
}

/// The next log line of this socket (NUL-terminated in `buf`): 1, or 0 when there is none.
#[no_mangle]
pub extern "C" fn netplay_switch_socket_log(h: i32, buf: *mut c_char, cap: i32) -> i32 {
    if let Some(s) = socket(h) {
        if let Some(line) = lock(&s).log.pop_front() {
            copy_cstr(&line, buf, cap);
            return 1;
        }
    }
    let q = MESH_LOGS.lock().unwrap_or_else(|p| p.into_inner()).iter().find(|(k, _)| *k == h).map(|(_, q)| q.clone());
    if let Some(line) = q.and_then(|q| q.lock().ok()?.pop_front()) {
        copy_cstr(&line, buf, cap);
        return 1;
    }
    0
}
