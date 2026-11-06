//! Spike (VIABILITY-TESTS test 8): two GGRS P2P sessions in one process over an in-memory socket,
//! with delay and loss, driving a trivial deterministic "game". Passes if both end in the same
//! state with no desync events. Built for the Mac (a binary) and for the Switch (a staticlib
//! called from the exlaunch probe).
use ggrs::{
    Config, DesyncDetection, GgrsEvent, GgrsRequest, Message, NonBlockingSocket, PlayerType,
    PredictRepeatLast, SessionBuilder, SessionState,
};
use std::cell::RefCell;
use std::collections::VecDeque;
use std::rc::Rc;

pub struct Cfg;
impl Config for Cfg {
    type Input = u32;
    type InputPredictor = PredictRepeatLast;
    type State = u64;
    type Address = u32;
}

/// Two mailboxes. A message sent at poll `t` is delivered at poll `t + delay`, and every 7th is dropped.
struct Net {
    boxes: [VecDeque<(u64, u32, Message)>; 2],
    now: u64,
    sent: u64,
}
struct MemSocket {
    me: usize,
    net: Rc<RefCell<Net>>,
    delay: u64,
    loss_every: u64,
}
impl NonBlockingSocket<u32> for MemSocket {
    fn send_to(&mut self, msg: &Message, _addr: &u32) {
        let mut n = self.net.borrow_mut();
        n.sent += 1;
        if self.loss_every != 0 && n.sent % self.loss_every == 0 {
            return; // lost
        }
        let at = n.now + self.delay;
        let other = 1 - self.me;
        n.boxes[other].push_back((at, self.me as u32, msg.clone()));
    }
    fn receive_all_messages(&mut self) -> Vec<(u32, Message)> {
        let mut n = self.net.borrow_mut();
        let now = n.now;
        let me = self.me;
        let mut out = Vec::new();
        while let Some((at, _, _)) = n.boxes[me].front() {
            if *at > now {
                break;
            }
            let (_, from, m) = n.boxes[me].pop_front().unwrap();
            out.push((from, m));
        }
        out
    }
}

fn step(state: u64, inputs: &[u32]) -> u64 {
    let mut s = state.wrapping_mul(6364136223846793005).wrapping_add(1442695040888963407);
    for i in inputs {
        s = s.rotate_left(13) ^ (*i as u64).wrapping_mul(0x9E3779B97F4A7C15);
    }
    s
}

fn local_input(player: usize, frame: i32) -> u32 {
    let x = (frame as u32).wrapping_mul(2654435761).wrapping_add(player as u32 * 40503);
    (x >> 7) & 0xff
}

/// Returns 0 on success; the second value is a one-line report.
pub fn run(ticks: i32, delay: u64, loss_every: u64) -> (i32, String) {
    run_cfg(ticks, delay, loss_every, [(2, 8), (2, 8)])
}

/// `cfg[me] = (input delay, max prediction window)`. A window of 0 is lockstep: that peer only advances
/// on confirmed inputs, so it never rolls back (and needs no snapshots).
pub fn run_cfg(ticks: i32, delay: u64, loss_every: u64, cfg: [(usize, usize); 2]) -> (i32, String) {
    let net = Rc::new(RefCell::new(Net { boxes: [VecDeque::new(), VecDeque::new()], now: 0, sent: 0 }));
    let mut sessions = Vec::new();
    for me in 0..2usize {
        let builder = SessionBuilder::<Cfg>::new()
            .with_num_players(2)
            .unwrap()
            .with_input_delay(cfg[me].0)
            .with_max_prediction_window(cfg[me].1)
            .with_desync_detection_mode(DesyncDetection::On { interval: 10 });
        let builder = (0..2usize).fold(builder, |b, p| {
            let t = if p == me { PlayerType::Local } else { PlayerType::Remote(p as u32) };
            b.add_player(t, p).unwrap()
        });
        let sock = MemSocket { me, net: net.clone(), delay, loss_every };
        sessions.push(builder.start_p2p_session(sock).unwrap());
    }
    let mut state = [0u64; 2];
    let mut hist: [std::collections::HashMap<i32, u64>; 2] = Default::default();
    let (mut loads, mut desyncs, mut synced) = (0, 0, 0);
    let mut loads_by = [0u32; 2];
    let mut stalls = [0u32; 2];
    let mut sim_frame = [0i32; 2];
    let mut hist2: [std::collections::HashMap<i32, u64>; 2] = Default::default();
    let started = std::time::Instant::now();
    let cur = |s: &ggrs::P2PSession<Cfg>| s.current_frame();
    while (cur(&sessions[0]) < ticks || cur(&sessions[1]) < ticks) && started.elapsed().as_secs() < 30 {
        std::thread::sleep(std::time::Duration::from_micros(500));
        net.borrow_mut().now += 1;
        for me in 0..2usize {
            let s = &mut sessions[me];
            s.poll_remote_clients();
            for ev in s.events() {
                match ev {
                    GgrsEvent::DesyncDetected { .. } => desyncs += 1,
                    GgrsEvent::Synchronized { .. } => synced += 1,
                    _ => {}
                }
            }
            if s.current_state() != SessionState::Running || s.current_frame() >= ticks {
                continue;
            }
            let f = s.current_frame();
            s.add_local_input(me, local_input(me, f)).unwrap();
            let reqs = match s.advance_frame() {
                Ok(r) => r,
                Err(_) => { stalls[me] += 1; continue } // prediction window full: wait
            };
            for r in reqs {
                match r {
                    GgrsRequest::SaveGameState { cell, frame } => {
                        hist[me].insert(frame, state[me]);
                        cell.save(frame, Some(state[me]), Some(state[me] as u128));
                    }
                    GgrsRequest::LoadGameState { cell, frame } => {
                        state[me] = cell.load().unwrap();
                        sim_frame[me] = frame;
                        loads += 1;
                        loads_by[me] += 1;
                    }
                    GgrsRequest::AdvanceFrame { inputs } => {
                        let v: Vec<u32> = inputs.iter().map(|(i, _)| *i).collect();
                        state[me] = step(state[me], &v);
                        sim_frame[me] += 1;
                        hist2[me].insert(sim_frame[me], state[me]);
                    }
                }
            }
        }
    }
    // Let the last inputs arrive, so frames up to the end are confirmed on both sides.
    for _ in 0..400 {
        std::thread::sleep(std::time::Duration::from_micros(500));
        net.borrow_mut().now += 1;
        for me in 0..2usize {
            sessions[me].poll_remote_clients();
            for ev in sessions[me].events() {
                if let GgrsEvent::DesyncDetected { .. } = ev {
                    desyncs += 1;
                }
            }
        }
    }
    let confirmed = sessions[0].confirmed_frame().min(sessions[1].confirmed_frame());
    let mut compared = 0;
    let mut mismatches = 0;
    let mut first_bad = -1;
    let mut last_bad = -1;
    // The last few frames are never re-simulated here (the harness stops stepping), so they stay
    // as predicted; a real game keeps stepping. Compare up to the prediction window before the end.
    for f in 0..=confirmed.min(ticks - 10) {
        if let (Some(a), Some(b)) = (hist[0].get(&f), hist[1].get(&f)) {
            compared += 1;
            if a != b {
                mismatches += 1;
                if first_bad < 0 {
                    first_bad = f;
                }
                last_bad = f;
            }
        }
    }
    let mut c2 = 0; let mut m2 = 0; let mut fb2 = -1;
    for f in 1..=(ticks - 10) { if let (Some(a), Some(b)) = (hist2[0].get(&f), hist2[1].get(&f)) { c2 += 1; if a != b { m2 += 1; if fb2 < 0 { fb2 = f; } } } }
    let reached = cur(&sessions[0]).min(cur(&sessions[1]));
    let ok = reached >= ticks && desyncs == 0 && synced == 2 && mismatches == 0 && m2 == 0 && c2 > 0;
    let report = format!(
        "reached frame {} synced {} rollbacks {} (peer0 {} peer1 {}) stalled polls (peer0 {} peer1 {}) desyncs {} confirmed {} compared {} mismatches {} (frames {}..{}) final-state compare: {} frames, {} mismatches (first {})",
        reached, synced, loads, loads_by[0], loads_by[1], stalls[0], stalls[1], desyncs, confirmed, compared, mismatches, first_bad, last_bad, c2, m2, fb2
    );
    (if ok { 0 } else { 1 }, report)
}

#[no_mangle]
pub extern "C" fn ggrs_spike_run(ticks: i32, delay: i32, out: *mut u8, cap: usize) -> i32 {
    let (rc, report) = run(ticks, delay as u64, 0);
    let b = report.as_bytes();
    let n = b.len().min(cap.saturating_sub(1));
    unsafe {
        std::ptr::copy_nonoverlapping(b.as_ptr(), out, n);
        *out.add(n) = 0;
    }
    rc
}
