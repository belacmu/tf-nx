fn main() {
    if std::env::args().any(|a| a == "asym") { asym(); return; }
    for (delay, loss) in [(0u64, 0u64), (2, 0), (6, 0), (2, 11), (6, 5)] {
        let t = std::time::Instant::now();
        let (rc, report) = ggrs_spike::run(600, delay, loss);
        println!("delay {delay} loss 1/{loss}: rc {rc}: {report} ({:?})", t.elapsed());
    }
}

// (appended) asymmetric: peer 0 lockstep (the Switch), peer 1 predicts and rolls back (the browser)
#[allow(dead_code)]
fn asym() {
    for (net_delay, d0) in [(0u64, 2usize), (2, 4), (6, 8), (12, 14), (12, 3)] {
        let t = std::time::Instant::now();
        let (rc, report) = ggrs_spike::run_cfg(600, net_delay, 0, [(d0, 0), (2, 8)]);
        println!("lockstep peer0 delay {d0}, one-way net delay {net_delay} polls: rc {rc}: {report} ({:?})", t.elapsed());
    }
}
