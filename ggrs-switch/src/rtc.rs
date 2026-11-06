//! C API of tfx-rtc's `Mesh` (a TF.EX/matchbox WebRTC peer, ../../tfx-rtc): the module owns the room's WebSocket and
//! one UDP socket and moves text and datagrams between them and this. IPv4 addresses are host-order u32s
//! (a.b.c.d = a << 24 | b << 16 | c << 8 | d). Peer ids are the server's uuids, as NUL-terminated text.
use std::collections::VecDeque;
use std::ffi::CStr;
use std::net::{Ipv4Addr, SocketAddr, SocketAddrV4};
use std::os::raw::c_char;
use std::sync::{Arc, Mutex};
use std::time::Instant;

use tfx_rtc::str0m::Candidate;
use tfx_rtc::{Mesh, MeshEvent};

pub struct Rtc {
    mesh: Mesh,
    log: Arc<Mutex<VecDeque<String>>>,
}

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

/// A mesh whose UDP socket is bound to `ip:port` (its host candidate). Null if the address is unusable.
#[no_mangle]
pub extern "C" fn tfx_rtc_new(ip: u32, port: u16) -> *mut Rtc {
    let Ok(host) = Candidate::host(v4(ip, port), "udp") else { return std::ptr::null_mut() };
    let log: Arc<Mutex<VecDeque<String>>> = Default::default();
    let sink = log.clone();
    let mesh = Mesh::new(
        vec![host],
        Box::new(move |s| {
            if let Ok(mut q) = sink.lock() {
                if q.len() < 256 {
                    q.push_back(s.to_string());
                }
            }
        }),
    );
    Box::into_raw(Box::new(Rtc { mesh, log }))
}

#[no_mangle]
pub extern "C" fn tfx_rtc_free(r: *mut Rtc) {
    if !r.is_null() {
        drop(unsafe { Box::from_raw(r) });
    }
}

/// Sends a STUN binding request (from `tfx_rtc_udp_out`) to learn the public address; `txid`: 12 random bytes.
#[no_mangle]
pub extern "C" fn tfx_rtc_stun(r: *mut Rtc, ip: u32, port: u16, txid: *const u8) {
    let r = unsafe { &mut *r };
    let mut t = [0u8; 12];
    unsafe { std::ptr::copy_nonoverlapping(txid, t.as_mut_ptr(), 12) };
    r.mesh.stun_start(v4(ip, port), t);
}

/// The public address once known: 1 and `*ip`/`*port`, else 0.
#[no_mangle]
pub extern "C" fn tfx_rtc_reflexive(r: *mut Rtc, ip: *mut u32, port: *mut u16) -> i32 {
    let r = unsafe { &mut *r };
    match r.mesh.reflexive() {
        Some(SocketAddr::V4(a)) => {
            unsafe {
                *ip = u32::from(*a.ip());
                *port = a.port();
            }
            1
        }
        _ => 0,
    }
}

/// One text message from the room's WebSocket.
#[no_mangle]
pub extern "C" fn tfx_rtc_signal_in(r: *mut Rtc, text: *const u8, len: i32) {
    let r = unsafe { &mut *r };
    let bytes = unsafe { std::slice::from_raw_parts(text, len.max(0) as usize) };
    if let Ok(s) = std::str::from_utf8(bytes) {
        r.mesh.on_signal_text(s, Instant::now());
    }
}

/// The next text message for the WebSocket: its length (copied into `buf`, at most `cap`), or 0 when there is none.
/// A message longer than `cap` is truncated (offers and answers are a few KB: give 16 KB).
#[no_mangle]
pub extern "C" fn tfx_rtc_signal_out(r: *mut Rtc, buf: *mut u8, cap: i32) -> i32 {
    let r = unsafe { &mut *r };
    match r.mesh.poll_signal() {
        Some(s) => copy_out(s.as_bytes(), buf, cap),
        None => 0,
    }
}

/// Queues the "KeepAlive" the server wants every 10 s.
#[no_mangle]
pub extern "C" fn tfx_rtc_keepalive(r: *mut Rtc) {
    unsafe { &mut *r }.mesh.signal_out_keepalive();
}

/// One datagram that arrived on the UDP socket (from `src_ip:src_port`).
#[no_mangle]
pub extern "C" fn tfx_rtc_udp_in(r: *mut Rtc, src_ip: u32, src_port: u16, dst_ip: u32, dst_port: u16, data: *const u8, len: i32) {
    let r = unsafe { &mut *r };
    let d = unsafe { std::slice::from_raw_parts(data, len.max(0) as usize) };
    r.mesh.on_udp(Instant::now(), v4(src_ip, src_port), v4(dst_ip, dst_port), d);
}

/// The next datagram to send: its length (copied into `buf`) and its destination, or 0 when there is none.
#[no_mangle]
pub extern "C" fn tfx_rtc_udp_out(r: *mut Rtc, dst_ip: *mut u32, dst_port: *mut u16, buf: *mut u8, cap: i32) -> i32 {
    let r = unsafe { &mut *r };
    loop {
        let Some((_, to, d)) = r.mesh.poll_udp() else { return 0 };
        let SocketAddr::V4(a) = to else { continue }; // (IPv6 peers: not on the Switch)
        unsafe {
            *dst_ip = u32::from(*a.ip());
            *dst_port = a.port();
        }
        return copy_out(&d, buf, cap);
    }
}

/// Advances every connection; returns the ms until it wants to be driven again (call it at least every frame).
#[no_mangle]
pub extern "C" fn tfx_rtc_drive(r: *mut Rtc) -> i32 {
    let r = unsafe { &mut *r };
    let now = Instant::now();
    let next = r.mesh.drive(now);
    next.saturating_duration_since(now).as_millis().min(1000) as i32
}

/// The next event: 1 = id assigned (`peer` = this peer's id), 2 = peer connected, 3 = peer gone, 4 = packet from peer
/// (copied into `buf`, `*len` = its length). Returns 0 when there is none. `peer` gets the id (give 64 bytes).
#[no_mangle]
pub extern "C" fn tfx_rtc_event(r: *mut Rtc, peer: *mut c_char, peer_cap: i32, buf: *mut u8, cap: i32, len: *mut i32) -> i32 {
    let r = unsafe { &mut *r };
    let (kind, id, data) = match r.mesh.poll_event() {
        None => return 0,
        Some(MeshEvent::Id(id)) => (1, id, None),
        Some(MeshEvent::Connected(id)) => (2, id, None),
        Some(MeshEvent::Disconnected(id)) => (3, id, None),
        Some(MeshEvent::Data(id, d)) => (4, id, Some(d)),
    };
    copy_cstr(&id, peer, peer_cap);
    if !len.is_null() {
        unsafe { *len = data.as_deref().map_or(0, |d| copy_out(d, buf, cap)) };
    }
    kind
}

/// Sends one packet to `peer` over the data channel; 1 if it went out, 0 if that peer isn't connected.
#[no_mangle]
pub extern "C" fn tfx_rtc_send(r: *mut Rtc, peer: *const c_char, data: *const u8, len: i32) -> i32 {
    let r = unsafe { &mut *r };
    let Ok(id) = unsafe { CStr::from_ptr(peer) }.to_str() else { return 0 };
    let d = unsafe { std::slice::from_raw_parts(data, len.max(0) as usize) };
    r.mesh.send(id, d) as i32
}

/// The next log line (NUL-terminated in `buf`): 1, or 0 when there is none.
#[no_mangle]
pub extern "C" fn tfx_rtc_log(r: *mut Rtc, buf: *mut c_char, cap: i32) -> i32 {
    let r = unsafe { &mut *r };
    let Some(s) = r.log.lock().ok().and_then(|mut q| q.pop_front()) else { return 0 };
    copy_cstr(&s, buf, cap);
    1
}

/// This window's counters (then the next window starts), into `out[0..n]`; returns how many it wrote. Order: datagrams in,
/// bytes in, datagrams out, bytes out, largest datagram out, data packets in, out, refused; us in udp_in, drive, send; the
/// largest udp_in and drive call (us); SCTP buffered bytes, datagrams and events still queued.
#[no_mangle]
pub extern "C" fn tfx_rtc_stats(r: *mut Rtc, out: *mut u64, n: i32) -> i32 {
    let r = unsafe { &mut *r };
    let s = r.mesh.take_stats();
    let v = [
        s.dg_in, s.bytes_in, s.dg_out, s.bytes_out, s.max_out, s.data_in, s.data_out, s.data_refused, s.us_in, s.us_drive, s.us_send,
        s.max_us_in, s.max_us_drive, s.buffered, s.udp_queued, s.events_queued,
    ];
    let k = v.len().min(n.max(0) as usize);
    unsafe { std::ptr::copy_nonoverlapping(v.as_ptr(), out, k) };
    k as i32
}
