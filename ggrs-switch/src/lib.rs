//! The Switch's Rust library (TowerFall native mod), one staticlib so std is linked once: TF.EX's own netplay library ggrs_ffi
//! (../ggrs-ffi-switch: the session TF.EX's generated NetplayManager drives, docs/ws-ggrs-ffi.md), the WebRTC client its sockets and
//! the lobby's ping measurement use (rtc.rs over tfx-rtc), raw DEFLATE for the runtime's zip writer (deflate.rs), and the test-8 spike. The hand GGRS session that was here (`tfx_session_*`,
//! xplay.cpp's driver) was replaced by ggrs_ffi in the 2026-10-18 switch-over.
pub mod rtc;
pub mod ping_measurement;
pub mod deflate;   // raw DEFLATE for the runtime's zip writer (TF.EX's MatchReport)

// TF.EX's ggrs_ffi (netplay_*, netplay_switch_*): linked into this staticlib so the module has its C API.
extern crate ggrs_ffi;

/// Keeps the test-8 entry point (net.cpp's `ggrs` stage) in the one Rust staticlib the probe links.
#[no_mangle]
pub extern "C" fn tfx_keep_spike(ticks: i32, delay: i32, out: *mut u8, cap: usize) -> i32 {
    ggrs_spike::ggrs_spike_run(ticks, delay, out, cap)
}
