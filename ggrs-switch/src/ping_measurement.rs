//! TF.EX's standalone ping measurement (ggrs_ffi `core/src/ping_measurement.rs`, github.com/Fcornaire/ggrs-ffi 2fe64d9,
//! the direct variant; the copy in Towerfall Browser/vendor/netplay-src/ggrs-ffi is the same before the relay one): a second
//! matchbox WebRTC socket on `<server>/ping_measurement/<room>?peer=<own>`, every 500 ms a PING `[0, stamp u64 LE]` to each
//! connected peer, which answers PONG `[1, stamp]`; per peer the last 24 outcomes (an RTT, or a loss after 1.5 s), stats =
//! median RTT, 90th percentile minus median ("spike"), loss %. Here only the bookkeeping: the module's net.cpp (pm::Run)
//! owns the sockets and a tfx-rtc mesh (rtc.rs) and calls these with its clock in microseconds. The relay variant
//! (`isRelay`, ggrs_ffi's relay.rs transport) is not ported (S-transport).
use std::collections::{HashMap, VecDeque};
use std::ffi::CStr;
use std::os::raw::c_char;
use std::sync::Mutex;
use std::time::Duration;

const PING_INTERVAL: Duration = Duration::from_millis(500);
const SAMPLE_WINDOW: usize = 24;
const LOSS_TIMEOUT: Duration = Duration::from_millis(1500);
const SPIKE_PERCENTILE: usize = 90;
const HALF_MILLISECOND: Duration = Duration::from_micros(500);
const PACKET_LEN: usize = 1 + std::mem::size_of::<u64>();

const PING: u8 = 0;
const PONG: u8 = 1;

#[derive(Default)]
struct PeerSamples {
    outcomes: VecDeque<Option<Duration>>,
    pending: VecDeque<u64>,
}

impl PeerSamples {
    fn record(&mut self, outcome: Option<Duration>) {
        if self.outcomes.len() == SAMPLE_WINDOW {
            self.outcomes.pop_front();
        }
        self.outcomes.push_back(outcome);
    }

    fn expire_pending(&mut self, now: Duration) {
        while let Some(&stamp) = self.pending.front() {
            if now.saturating_sub(Duration::from_micros(stamp)) < LOSS_TIMEOUT {
                break;
            }
            self.pending.pop_front();
            self.record(None);
        }
    }
}

#[repr(C)]
pub struct PingStats {
    pub rtt: i32,   // median
    pub spike: i32, // slow rtt
    pub loss_percent: i32,
    pub samples: i32,
}

impl PingStats {
    pub fn none() -> Self {
        Self { rtt: -1, spike: 0, loss_percent: 0, samples: 0 }
    }
}

/// One measurement (TF.EX's `PingMeasurement` without the thread: the module's thread drives it). Peer ids are the server's
/// uuids as text.
pub struct PingMeasurement {
    samples: Mutex<HashMap<String, PeerSamples>>,
    next_ping: Mutex<Duration>,
}

fn to_millis(duration: Duration) -> i32 {
    (duration + HALF_MILLISECOND).as_millis() as i32
}

//Stamp so that we dont have to deal with clock divergence across computer
fn encode(kind: u8, stamp: u64, out: &mut [u8]) -> usize {
    if out.len() < PACKET_LEN {
        return 0;
    }
    out[0] = kind;
    out[1..PACKET_LEN].copy_from_slice(&stamp.to_le_bytes());
    PACKET_LEN
}

fn decode(packet: &[u8]) -> Option<(u8, u64)> {
    if packet.len() != PACKET_LEN {
        return None;
    }
    let stamp = u64::from_le_bytes(packet[1..PACKET_LEN].try_into().ok()?);
    Some((packet[0], stamp))
}

impl PingMeasurement {
    pub fn new() -> Self {
        Self { samples: Mutex::new(HashMap::new()), next_ping: Mutex::new(Duration::ZERO) }
    }

    fn lock(&self) -> std::sync::MutexGuard<'_, HashMap<String, PeerSamples>> {
        self.samples.lock().unwrap_or_else(|p| p.into_inner())
    }

    /// PeerState::Disconnected: the peer's samples go.
    pub fn disconnected(&self, peer: &str) {
        self.lock().remove(peer);
    }

    /// `handle_packet`: a PING gets its PONG (written to `reply`, returns its length), a PONG records the RTT.
    pub fn handle_packet(&self, peer: &str, packet: &[u8], now: Duration, reply: &mut [u8]) -> usize {
        let Some((kind, stamp)) = decode(packet) else { return 0 };
        match kind {
            PING => encode(PONG, stamp, reply),
            PONG => {
                let rtt = now.saturating_sub(Duration::from_micros(stamp));
                let mut samples = self.lock();
                let peer_samples = samples.entry(peer.to_string()).or_default();
                match peer_samples.pending.iter().position(|pending| *pending == stamp) {
                    Some(index) => {
                        peer_samples.pending.remove(index);
                        peer_samples.record(Some(rtt));
                    }
                    None if peer_samples.outcomes.is_empty() => peer_samples.record(Some(rtt)),
                    None => {}
                }
                0
            }
            _ => 0,
        }
    }

    /// `if Instant::now() >= next_ping { next_ping = now + PING_INTERVAL; ... }`: true when a round of pings is due; the
    /// stamp to send is `now` in microseconds.
    pub fn ping_due(&self, now: Duration) -> bool {
        let mut next = self.next_ping.lock().unwrap_or_else(|p| p.into_inner());
        if now < *next {
            return false;
        }
        *next = now + PING_INTERVAL;
        true
    }

    /// One peer of the round: expire its pending pings, and remember this one if it went out.
    pub fn pinged(&self, peer: &str, stamp: u64, sent: bool, now: Duration) {
        let mut samples = self.lock();
        if let Some(peer_samples) = samples.get_mut(peer) {
            peer_samples.expire_pending(now);
            if sent {
                peer_samples.pending.push_back(stamp);
            }
        }
    }

    pub fn stats(&self, peer: &str) -> PingStats {
        let samples = self.lock();
        let Some(peer_samples) = samples.get(peer) else { return PingStats::none() };
        let outcomes = &peer_samples.outcomes;
        if outcomes.is_empty() {
            return PingStats::none();
        }
        let mut received: Vec<Duration> = outcomes.iter().flatten().copied().collect();
        received.sort_unstable();
        let lost = outcomes.len() - received.len();
        let loss_percent = (lost * 100 / outcomes.len()) as i32;
        if received.is_empty() {
            return PingStats { loss_percent, samples: outcomes.len() as i32, ..PingStats::none() };
        }
        let median = received[received.len() / 2];
        let slow = received[(received.len() * SPIKE_PERCENTILE / 100).min(received.len() - 1)];
        PingStats { rtt: to_millis(median), spike: to_millis(slow - median), loss_percent, samples: outcomes.len() as i32 }
    }
}

impl Default for PingMeasurement {
    fn default() -> Self {
        Self::new()
    }
}

// ---- C API (the module's net.cpp) ----

unsafe fn peer_str<'a>(p: *const c_char) -> Option<&'a str> {
    if p.is_null() {
        return None;
    }
    CStr::from_ptr(p).to_str().ok()
}

#[no_mangle]
pub extern "C" fn tfx_pm_new() -> *mut PingMeasurement {
    Box::into_raw(Box::new(PingMeasurement::new()))
}

#[no_mangle]
pub extern "C" fn tfx_pm_free(pm: *mut PingMeasurement) {
    if !pm.is_null() {
        unsafe { drop(Box::from_raw(pm)) };
    }
}

#[no_mangle]
pub unsafe extern "C" fn tfx_pm_disconnected(pm: *const PingMeasurement, peer: *const c_char) {
    if let (Some(pm), Some(peer)) = (pm.as_ref(), peer_str(peer)) {
        pm.disconnected(peer);
    }
}

/// A packet from `peer`; returns the length of the reply written to `reply` (a PONG), 0 for none.
#[no_mangle]
pub unsafe extern "C" fn tfx_pm_packet(pm: *const PingMeasurement, peer: *const c_char, data: *const u8, len: i32, now_us: u64,
                                       reply: *mut u8, cap: i32) -> i32 {
    let (Some(pm), Some(peer)) = (pm.as_ref(), peer_str(peer)) else { return 0 };
    if data.is_null() || len <= 0 || reply.is_null() || cap <= 0 {
        return 0;
    }
    let packet = std::slice::from_raw_parts(data, len as usize);
    let reply = std::slice::from_raw_parts_mut(reply, cap as usize);
    pm.handle_packet(peer, packet, Duration::from_micros(now_us), reply) as i32
}

/// 1 when a ping round is due; then `out` (9 bytes) holds the PING to send to every connected peer.
#[no_mangle]
pub unsafe extern "C" fn tfx_pm_ping_due(pm: *const PingMeasurement, now_us: u64, out: *mut u8, cap: i32) -> i32 {
    let Some(pm) = pm.as_ref() else { return 0 };
    if out.is_null() || cap < PACKET_LEN as i32 || !pm.ping_due(Duration::from_micros(now_us)) {
        return 0;
    }
    encode(PING, now_us, std::slice::from_raw_parts_mut(out, cap as usize));
    1
}

#[no_mangle]
pub unsafe extern "C" fn tfx_pm_pinged(pm: *const PingMeasurement, peer: *const c_char, now_us: u64, sent: i32) {
    if let (Some(pm), Some(peer)) = (pm.as_ref(), peer_str(peer)) {
        pm.pinged(peer, now_us, sent != 0, Duration::from_micros(now_us));
    }
}

#[no_mangle]
pub unsafe extern "C" fn tfx_pm_stats(pm: *const PingMeasurement, peer: *const c_char, stats: *mut PingStats) {
    if stats.is_null() {
        return;
    }
    *stats = match (pm.as_ref(), peer_str(peer)) {
        (Some(pm), Some(peer)) => pm.stats(peer),
        _ => PingStats::none(),
    };
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn ping_pong_and_stats() {
        let a = PingMeasurement::new();
        let b = PingMeasurement::new();
        let mut ping = [0u8; 9];
        let mut pong = [0u8; 9];
        // a pings b every 500 ms; b answers after 30 ms; every 4th answer is lost
        for i in 0..30u64 {
            let t = Duration::from_millis(500 * i);
            assert!(a.ping_due(t));
            let stamp = t.as_micros() as u64;
            encode(PING, stamp, &mut ping);
            a.pinged("b", stamp, true, t);   // no entry yet for "b" until the first PONG: TF.EX does the same
            assert_eq!(b.handle_packet("a", &ping, t, &mut pong), 9);
            if i % 4 != 3 {
                a.handle_packet("b", &pong, t + Duration::from_millis(30), &mut [0u8; 9]);
            }
        }
        let s = a.stats("b");
        assert_eq!(s.rtt, 30);
        assert_eq!(s.samples, SAMPLE_WINDOW as i32);
        assert!(s.loss_percent > 15 && s.loss_percent < 30, "loss {}", s.loss_percent);
        assert_eq!(a.stats("nobody").rtt, -1);
    }
}
