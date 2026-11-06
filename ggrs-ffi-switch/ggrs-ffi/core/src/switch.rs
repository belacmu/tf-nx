//! The Switch build (feature "switch", docs/ws-ggrs-ffi.md). TFEX-DIFF: the Switch's Rust std has no working threads (the
//! module's shim has no pthread_create and futex waits fail), so what ggrs-ffi runs on threads on PC is stepped here,
//! by `netplay_switch_pump`, which the module calls every frame (or more often, from its network thread):
//! - the background poller (lib.rs: poll_remote every 2 ms while a session runs),
//! - the matchbox loop (neplay.rs spawn_matchbox_loop: peers, players, the signaling connection's end; also stepped
//!   while netplay_init waits for the players),
//! - the ping measurements (ping_measurement.rs Run).
//! Same bodies as the threads', one pass per call. And the tracing lines go to a bounded queue the module reads
//! (`netplay_switch_log`) instead of stdout.
use std::collections::{HashSet, VecDeque};
use std::future::Future;
use std::io;
use std::os::raw::c_char;
use std::sync::atomic::Ordering;
use std::sync::{Arc, Mutex};
use std::task::{Context, Poll};

use futures::task::noop_waker_ref;
use ggrs::PlayerType;
use matchbox_socket::PeerId;
use tracing::{error, info};

use crate::transport::{Connection, PeerSocket};
use crate::{
    guard_netplay_instance, mark_matchbox_loop, matchbox_stop_requested, ping_measurement, set_connected_peers,
    set_matchbox_stop, set_netplay_disconnected, POLLER_ALIVE, POLLER_SHOULD_STOP,
};

/// Polls a future once (no waker: the pump polls again next call). Some(output) once it completed.
pub(crate) fn poll_once<F: Future + Unpin>(f: &mut F) -> Option<F::Output> {
    match std::pin::Pin::new(f).poll(&mut Context::from_waker(noop_waker_ref())) {
        Poll::Ready(out) => Some(out),
        Poll::Pending => None,
    }
}

// ---- the matchbox loop ---------------------------------------------------------------------------------------------

struct MatchboxLoop {
    socket: Box<dyn PeerSocket>,
    connection: Option<Connection>,
    shared_players: Arc<Mutex<Vec<PlayerType<PeerId>>>>,
}

static MATCHBOX_LOOP: Mutex<Option<MatchboxLoop>> = Mutex::new(None);

pub(crate) fn start_matchbox_loop(
    socket: Box<dyn PeerSocket>,
    connection: Connection,
    shared_players: Arc<Mutex<Vec<PlayerType<PeerId>>>>,
) {
    info!("Starting matchbox loop (pumped)");
    *MATCHBOX_LOOP.lock().unwrap_or_else(|p| p.into_inner()) =
        Some(MatchboxLoop { socket, connection: Some(connection), shared_players });
}

pub(crate) fn drop_matchbox_loop() {
    let ended = MATCHBOX_LOOP.lock().unwrap_or_else(|p| p.into_inner()).take();
    drop(ended);
    mark_matchbox_loop(false);
}

/// One pass of spawn_matchbox_loop's `while !matchbox_stop_requested()` body.
pub(crate) fn step_matchbox_loop() {
    let mut guard = MATCHBOX_LOOP.lock().unwrap_or_else(|p| p.into_inner());
    if guard.is_none() {
        return;
    }
    if matchbox_stop_requested() {
        *guard = None;
        drop(guard);
        mark_matchbox_loop(false);
        return;
    }
    let lp = guard.as_mut().unwrap();

    lp.socket.update_peers();
    set_connected_peers(lp.socket.connected_peers().into_iter().map(|p| p.0).collect::<HashSet<_>>());

    if let Ok(mut players) = lp.shared_players.lock() {
        *players = lp.socket.players();
    }

    if let Some(connection) = lp.connection.as_mut() {
        if let Some(result) = poll_once(connection) {
            match result {
                Ok(()) => info!("Matchbox thread exited cleanly!"),
                Err(e) => error!("Connection closed: {}", e),
            }
            info!("Matchbox message loop ended!");
            lp.connection = None;

            set_matchbox_stop(true);
            set_netplay_disconnected(true);
        }
    }
}

// ---- the pump ------------------------------------------------------------------------------------------------------

/// Does one pass of everything ggrs-ffi runs on threads on PC. Never blocks on the network; call it every frame, before
/// TF.EX's NetplayManager update, and not from inside another netplay_* call.
#[no_mangle]
pub extern "C" fn netplay_switch_pump() {
    step_matchbox_loop();

    // a p2p init waiting for its peers (netplay_init returned early, neplay.rs PendingInit): one step of its wait; no poll until the
    // session exists. A failed one is a disconnected session (TF.EX's AbortIfSynchronizationFailed ends it, as a failed init on the PC)
    let pending = {
        let mut np = guard_netplay_instance();
        match np.step_pending_init() {
            Some(Ok(())) => info!("p2p init: the peers connected, the session started"),
            Some(Err(e)) => {
                error!("p2p init failed: {}", e);
                set_netplay_disconnected(true);
                POLLER_ALIVE.store(false, Ordering::SeqCst);
            }
            None => {}
        }
        np.init_pending()
    };

    if !pending && POLLER_ALIVE.load(Ordering::SeqCst) && !POLLER_SHOULD_STOP.load(Ordering::Relaxed) {
        if guard_netplay_instance().poll_remote().is_err() {
            POLLER_ALIVE.store(false, Ordering::SeqCst); // (the PC poller thread ends here too)
        }
    }

    ping_measurement::step_all();
}

/// GGRS's confirmed frame (TFEX-DIFF: the Switch's rollback log needs it; TF.EX's C# doesn't ask): -1 without a session.
#[no_mangle]
pub extern "C" fn netplay_switch_confirmed_frame() -> i32 {
    guard_netplay_instance().confirmed_frame()
}

/// The requests ggrs_ffi still holds (kinds: 0 save, 1 load, 2 advance, as NetplayRequest), at most `cap` into `kinds`; returns how many
/// it holds. TFEX-DIFF (Switch): the gate checks TF.EX's request list against this before each advance (a request consumed on one side
/// only made ggrs_ffi refuse the next advance on the console, 2026-10-18 20:41).
#[no_mangle]
pub unsafe extern "C" fn netplay_switch_pending_requests(kinds: *mut i32, cap: i32) -> i32 {
    let reqs = guard_netplay_instance().requests();
    for (i, r) in reqs.iter().enumerate().take(cap.max(0) as usize) {
        if !kinds.is_null() {
            *kinds.add(i) = *r as i32;
        }
    }
    reqs.len() as i32
}

// ---- tracing -------------------------------------------------------------------------------------------------------

const MAX_LOG: usize = 512;

struct Log {
    lines: VecDeque<String>,
    dropped: u64,
}

static LOG: Mutex<Log> = Mutex::new(Log { lines: VecDeque::new(), dropped: 0 });

struct QueueWriter;

impl io::Write for QueueWriter {
    fn write(&mut self, buf: &[u8]) -> io::Result<usize> {
        let text = String::from_utf8_lossy(buf);
        let mut log = LOG.lock().unwrap_or_else(|p| p.into_inner());
        for line in text.lines().filter(|l| !l.is_empty()) {
            if log.lines.len() < MAX_LOG {
                log.lines.push_back(line.to_string());
            } else {
                log.dropped += 1;
            }
        }
        Ok(buf.len())
    }

    fn flush(&mut self) -> io::Result<()> {
        Ok(())
    }
}

pub(crate) fn init_tracing() {
    let _ = tracing_subscriber::fmt()
        .compact()
        .without_time()
        .with_ansi(false)
        .with_target(false)
        .with_max_level(tracing::Level::INFO)
        .with_writer(|| QueueWriter)
        .try_init();
}

/// The next ggrs-ffi log line (NUL-terminated in `buf`): 1, or 0 when there is none. When lines were dropped (the queue
/// holds 512), the next line read says how many.
#[no_mangle]
pub unsafe extern "C" fn netplay_switch_log(buf: *mut c_char, cap: i32) -> i32 {
    if buf.is_null() || cap <= 0 {
        return 0;
    }
    let mut log = LOG.lock().unwrap_or_else(|p| p.into_inner());
    let line = if log.dropped > 0 {
        let n = std::mem::take(&mut log.dropped);
        format!("ggrs-ffi: {n} log lines dropped (queue full)")
    } else {
        match log.lines.pop_front() {
            Some(line) => line,
            None => return 0,
        }
    };
    let n = line.len().min(cap as usize - 1);
    std::ptr::copy_nonoverlapping(line.as_ptr(), buf as *mut u8, n);
    *buf.add(n) = 0;
    1
}
