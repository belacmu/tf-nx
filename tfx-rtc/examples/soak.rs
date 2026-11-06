//! Host soak test, no network: two `Mesh`es in one process, signaling and datagrams handed across in memory on a
//! simulated clock. Each side sends `rate` packets of `size` bytes a second (GGRS's pattern: 2 a frame) for `secs`
//! simulated seconds; datagrams are dropped with probability LOSS (default 0.01) and delayed DELAY ms (default 3).
//! Prints, per 10 simulated seconds, the wall time A spent in on_udp / drive / send per call, so per-packet cost growth
//! shows. Usage: soak [secs=300] [rate=120] [size=60]
use std::collections::VecDeque;
use std::net::SocketAddr;
use std::time::{Duration, Instant};

use str0m::Candidate;
use tfx_rtc::{Mesh, MeshEvent};

struct Rng(u64);
impl Rng {
    fn f(&mut self) -> f64 {
        self.0 ^= self.0 << 13;
        self.0 ^= self.0 >> 7;
        self.0 ^= self.0 << 17;
        (self.0 >> 11) as f64 / (1u64 << 53) as f64
    }
}

fn main() {
    let a: Vec<String> = std::env::args().collect();
    let secs: u64 = a.get(1).map_or(300, |s| s.parse().unwrap());
    let rate: u64 = a.get(2).map_or(120, |s| s.parse().unwrap());
    let size: usize = a.get(3).map_or(60, |s| s.parse().unwrap());
    let loss: f64 = std::env::var("LOSS").map_or(0.01, |s| s.parse().unwrap());
    let delay = Duration::from_millis(std::env::var("DELAY").map_or(3, |s| s.parse().unwrap()));
    let quiet = std::env::var("QUIET").is_ok();

    let addr: [SocketAddr; 2] = ["10.0.0.1:1000".parse().unwrap(), "10.0.0.2:2000".parse().unwrap()];
    let ids = ["a", "b"];
    let mut m: Vec<Mesh> = (0..2)
        .map(|i| {
            let tag = ids[i];
            Mesh::new(vec![Candidate::host(addr[i], "udp").unwrap()], Box::new(move |s| if !quiet { eprintln!("{tag}: {s}") }))
        })
        .collect();
    let t0 = Instant::now();
    let mut now = t0;
    m[0].on_signal_text(r#"{"IdAssigned":"a"}"#, now);
    m[1].on_signal_text(r#"{"IdAssigned":"b"}"#, now);
    m[0].on_signal_text(r#"{"NewPeer":"b"}"#, now);

    let mut rng = Rng(0x9e3779b97f4a7c15);
    let mut wire: VecDeque<(Instant, usize, SocketAddr, Vec<u8>)> = VecDeque::new(); // arrival, to side, from, bytes
    let mut open = [false, false];
    let step = Duration::from_micros(1_000_000 / rate);
    let mut next_send = now;
    let end = t0 + Duration::from_secs(secs);
    let (mut win, mut n_in, mut n_drive, mut n_send) = (t0, 0u64, 0u64, 0u64);
    let (mut w_in, mut w_drive, mut w_send) = (Duration::ZERO, Duration::ZERO, Duration::ZERO);
    let (mut got, mut sent, mut bytes_out) = ([0u64; 2], [0u64; 2], 0usize);
    let pkt = vec![0x5au8; size];
    while now < end {
        now += Duration::from_millis(1);
        // signaling
        for i in 0..2 {
            while let Some(s) = m[i].poll_signal() {
                if s.contains("KeepAlive") {
                    continue;
                }
                let v: serde_json::Value = serde_json::from_str(&s).unwrap();
                let data = &v["Signal"]["data"];
                let txt = serde_json::json!({"Signal": {"sender": ids[i], "data": data}}).to_string();
                m[1 - i].on_signal_text(&txt, now);
            }
        }
        // the wire
        while wire.front().is_some_and(|w| w.0 <= now) {
            let (_, to, from, d) = wire.pop_front().unwrap();
            let t = Instant::now();
            m[to].on_udp(now, from, addr[to], &d);
            if to == 0 {
                w_in += t.elapsed();
                n_in += 1;
            }
        }
        // sends
        if open[0] && open[1] {
            while next_send <= now {
                next_send += step;
                for i in 0..2 {
                    let t = Instant::now();
                    if m[i].send(ids[1 - i], &pkt) {
                        sent[i] += 1;
                    }
                    if i == 0 {
                        w_send += t.elapsed();
                        n_send += 1;
                    }
                }
            }
        } else {
            next_send = now;
        }
        for i in 0..2 {
            let t = Instant::now();
            m[i].drive(now);
            if i == 0 {
                w_drive += t.elapsed();
                n_drive += 1;
            }
            while let Some((_, to, d)) = m[i].poll_udp() {
                if i == 0 {
                    bytes_out += d.len();
                }
                if rng.f() < loss {
                    continue;
                }
                let j = if to == addr[0] { 0 } else { 1 };
                wire.push_back((now + delay, j, addr[i], d));
            }
            while let Some(e) = m[i].poll_event() {
                match e {
                    MeshEvent::Connected(_) => open[i] = true,
                    MeshEvent::Data(_, _) => got[i] += 1,
                    MeshEvent::Disconnected(p) => eprintln!("{}: {p} gone at {:?}", ids[i], now - t0),
                    _ => {}
                }
            }
        }
        if now - win >= Duration::from_secs(10) {
            win = now;
            let us = |d: Duration, n: u64| if n == 0 { 0.0 } else { d.as_secs_f64() * 1e6 / n as f64 };
            println!(
                "t={:4}s  A: in {:6} x {:7.1} us  send {:6} x {:6.1} us  drive {:6} x {:6.1} us  out {:7} B   sent {:?} got {:?}",
                (now - t0).as_secs(),
                n_in,
                us(w_in, n_in),
                n_send,
                us(w_send, n_send),
                n_drive,
                us(w_drive, n_drive),
                bytes_out,
                sent,
                got
            );
            (n_in, n_drive, n_send, bytes_out) = (0, 0, 0, 0);
            (w_in, w_drive, w_send) = (Duration::ZERO, Duration::ZERO, Duration::ZERO);
        }
    }
}
