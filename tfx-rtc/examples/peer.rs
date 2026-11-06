//! Host test: joins a matchbox room, connects to every peer there over WebRTC, sends each one
//! "ping <n>" every 200 ms and echoes "pong <n>" back to what it receives. Exits 0 once it has
//! received `want` pongs from every one of `peers` peers (or 1 after `secs` seconds).
//! Usage: peer <ws://host:port/room/<id>> <peers> [want=10] [secs=30]
//! Soak mode (RATE=<packets/s>, SIZE=<bytes>, default 60): instead of pings, sends RATE packets a second to every
//! connected peer for `secs` seconds and prints, every 10 s, the time spent in on_udp / send / drive per call and the
//! datagrams and bytes each way (a GGRS-like load, for growth in the per-packet cost); exits 0 at the end.

use std::net::{SocketAddr, UdpSocket};
use std::time::{Duration, Instant};

use str0m::Candidate;
use tungstenite::Message;
use tfx_rtc::{Mesh, MeshEvent};

fn main() {
    let a: Vec<String> = std::env::args().collect();
    let room = &a[1];
    let peers: usize = a[2].parse().unwrap();
    let want: usize = a.get(3).map_or(10, |s| s.parse().unwrap());
    let secs: u64 = a.get(4).map_or(30, |s| s.parse().unwrap());

    // The LAN address (connecting a UDP socket sends nothing).
    let probe = UdpSocket::bind("0.0.0.0:0").unwrap();
    probe.connect("8.8.8.8:80").unwrap();
    // BIND=<ip> overrides it (127.0.0.1 when the macOS firewall drops UDP to unsigned binaries).
    let ip = std::env::var("BIND").map_or_else(|_| probe.local_addr().unwrap().ip(), |s| s.parse().unwrap());
    let udp = UdpSocket::bind(SocketAddr::new(ip, 0)).unwrap();
    udp.set_nonblocking(true).unwrap();
    let local = udp.local_addr().unwrap();
    eprintln!("[peer] udp {local}");

    let url = format!("{room}?peer={}", uuid::Uuid::new_v4());
    let (mut ws, _) = tungstenite::connect(&url).expect("signaling connect");
    match ws.get_mut() {
        tungstenite::stream::MaybeTlsStream::Plain(s) => s.set_nonblocking(true).unwrap(),
        _ => unreachable!(),
    }

    let mut mesh = Mesh::new(vec![Candidate::host(local, "udp").unwrap()], Box::new(|s| eprintln!("{s}")));
    // STUN=ip:port asks that server for the public address first.
    if let Ok(s) = std::env::var("STUN") {
        mesh.stun_start(s.parse().unwrap(), *b"tfxrtcspike1");
    }
    let start = Instant::now();
    let mut connected: Vec<(String, usize, u32)> = Vec::new(); // id, pongs received, next ping
    let mut last_ping = Instant::now();
    let mut last_keepalive = Instant::now();
    let mut buf = [0u8; 2048];
    let rate: u64 = std::env::var("RATE").map_or(0, |s| s.parse().unwrap());
    let soak = vec![0x5au8; std::env::var("SIZE").map_or(60, |s| s.parse().unwrap())];
    let mut next_send = Instant::now();
    let mut st = Soak::default();
    // LOSS=<p>: drops that share of the datagrams each way (from when the channel is open), as a lossy link would
    let loss: f64 = std::env::var("LOSS").map_or(0.0, |s| s.parse().unwrap());
    // LAG=<us>: sleeps that long per datagram received and sent (the console's socket calls cost ~0.2-3 ms each)
    let lag = Duration::from_micros(std::env::var("LAG").map_or(0, |s| s.parse().unwrap()));
    let mut rng = 0x9e3779b97f4a7c15u64;
    let mut lose = move || {
        rng ^= rng << 13;
        rng ^= rng >> 7;
        rng ^= rng << 17;
        ((rng >> 11) as f64 / (1u64 << 53) as f64) < loss
    };
    loop {
        let now = Instant::now();
        if rate > 0 && now - start > Duration::from_secs(secs) {
            eprintln!("[peer] soak done");
            std::process::exit(0);
        }
        if now - start > Duration::from_secs(secs) {
            eprintln!("[peer] FAIL: timed out; {connected:?}");
            std::process::exit(1);
        }
        loop {
            match ws.read() {
                Ok(Message::Text(t)) => mesh.on_signal_text(&t, now),
                Ok(_) => {}
                Err(tungstenite::Error::Io(e)) if e.kind() == std::io::ErrorKind::WouldBlock => break,
                Err(e) => panic!("signaling: {e}"),
            }
        }
        loop {
            match udp.recv_from(&mut buf) {
                Ok(_) if !connected.is_empty() && lose() => {}
                Ok((n, from)) => {
                    if !lag.is_zero() {
                        std::thread::sleep(lag);
                    }
                    let t = Instant::now();
                    mesh.on_udp(now, from, local, &buf[..n]);
                    st.t_in += t.elapsed();
                    st.n_in += 1;
                    st.b_in += n;
                }
                Err(e) if e.kind() == std::io::ErrorKind::WouldBlock => break,
                Err(e) => panic!("udp: {e}"),
            }
        }
        let t = Instant::now();
        mesh.drive(now);
        st.t_drive += t.elapsed();
        st.n_drive += 1;
        while let Some(e) = mesh.poll_event() {
            match e {
                MeshEvent::Id(id) => eprintln!("[peer] id {id}"),
                MeshEvent::Connected(id) => {
                    eprintln!("[peer] connected to {id} after {:?}", start.elapsed());
                    connected.push((id, 0, 0));
                }
                MeshEvent::Disconnected(id) => eprintln!("[peer] {id} left"),
                MeshEvent::Data(_, _) if rate > 0 => st.got += 1,
                MeshEvent::Data(id, d) => {
                    let s = String::from_utf8_lossy(&d).to_string();
                    if let Some(n) = s.strip_prefix("ping ") {
                        mesh.send(&id, format!("pong {n}").as_bytes());
                    } else if s.starts_with("pong ") {
                        if let Some(c) = connected.iter_mut().find(|c| c.0 == id) {
                            c.1 += 1;
                        }
                    }
                }
            }
        }
        if rate > 0 {
            while next_send <= now {
                next_send += Duration::from_micros(1_000_000 / rate);
                for c in &connected {
                    let t = Instant::now();
                    st.sent += mesh.send(&c.0, &soak) as u64;
                    st.t_send += t.elapsed();
                    st.n_send += 1;
                }
            }
            if connected.is_empty() {
                next_send = now;
            }
            if now - st.win > Duration::from_secs(10) {
                st.report(now - start);
                let m = mesh.take_stats();
                println!("        mesh: dg in {} (stun {}) out {} (stun {}, max {} B) data in {} out {} refused {} buffered {}", m.dg_in, m.stun_in, m.dg_out, m.stun_out, m.max_out, m.data_in, m.data_out, m.data_refused, m.buffered);
            }
        } else if now - last_ping > Duration::from_millis(200) {
            last_ping = now;
            for c in connected.iter_mut() {
                mesh.send(&c.0, format!("ping {}", c.2).as_bytes());
                c.2 += 1;
            }
        }
        if now - last_keepalive > Duration::from_secs(10) {
            last_keepalive = now;
            mesh.signal_out_keepalive();
        }
        while let Some(s) = mesh.poll_signal() {
            ws.send(Message::Text(s.into())).expect("signaling send");
        }
        let _ = ws.flush();
        while let Some((_, to, d)) = mesh.poll_udp() {
            if !connected.is_empty() && lose() {
                continue;
            }
            st.n_out += 1;
            st.b_out += d.len();
            let _ = udp.send_to(&d, to);
            if !lag.is_zero() {
                std::thread::sleep(lag);
            }
        }

        if connected.len() >= peers && connected.iter().all(|c| c.1 >= want) {
            eprintln!("[peer] OK: {want} round trips with each of {peers} peers in {:?}", start.elapsed());
            std::process::exit(0);
        }
        std::thread::sleep(Duration::from_millis(2));
    }
}

struct Soak {
    win: Instant,
    t_in: Duration,
    t_send: Duration,
    t_drive: Duration,
    n_in: u64,
    n_send: u64,
    n_drive: u64,
    n_out: u64,
    b_in: usize,
    b_out: usize,
    sent: u64,
    got: u64,
}

impl Default for Soak {
    fn default() -> Soak {
        let z = Duration::ZERO;
        Soak { win: Instant::now(), t_in: z, t_send: z, t_drive: z, n_in: 0, n_send: 0, n_drive: 0, n_out: 0, b_in: 0, b_out: 0, sent: 0, got: 0 }
    }
}

impl Soak {
    fn report(&mut self, at: Duration) {
        let us = |d: Duration, n: u64| if n == 0 { 0.0 } else { d.as_secs_f64() * 1e6 / n as f64 };
        println!(
            "t={:4}s  in {:5} dg {:7} B {:6.1} us  out {:5} dg {:7} B  send {:5} x {:5.1} us  drive {:6} x {:5.1} us  sent {} got {}",
            at.as_secs(),
            self.n_in,
            self.b_in,
            us(self.t_in, self.n_in),
            self.n_out,
            self.b_out,
            self.n_send,
            us(self.t_send, self.n_send),
            self.n_drive,
            us(self.t_drive, self.n_drive),
            self.sent,
            self.got
        );
        let (sent, got) = (self.sent, self.got);
        *self = Soak::default();
        (self.sent, self.got) = (sent, got);
    }
}
