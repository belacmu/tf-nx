//! Raw DEFLATE (RFC 1951) for the runtime's zip writer (hw-probe/exlaunch/source/program/tfex/runtime/tfexgen_bcl.cpp): the entries of
//! .NET's ZipArchive that TF.EX's MatchReport writes with CompressionLevel.SmallestSize (zlib level 9 in .NET), here miniz_oxide's
//! streaming compressor. Streaming into the caller's buffer: the module has no heap for a whole log and its copy (only the
//! compressor's own state is allocated, about 300 KB, freed by `tfx_deflate_end`).
use miniz_oxide::deflate::core::{compress, create_comp_flags_from_zip_params, CompressorOxide, TDEFLFlush, TDEFLStatus};

/// A compressor at `level` (0..10), raw DEFLATE (no zlib header); null if it couldn't be made.
#[no_mangle]
pub extern "C" fn tfx_deflate_begin(level: i32) -> *mut CompressorOxide {
    let flags = create_comp_flags_from_zip_params(level.clamp(0, 10), -15, 0);
    Box::into_raw(Box::new(CompressorOxide::new(flags)))
}

/// Feeds `len` bytes (`finish`: the last of them) and writes compressed bytes to `out` (`cap` free). Sets `*used` (input consumed) and
/// `*wrote` (output written); returns 1 when the stream is complete, 0 to call again (more input, or more room), -1 on an error.
#[no_mangle]
pub unsafe extern "C" fn tfx_deflate_feed(d: *mut CompressorOxide, src: *const u8, len: usize, finish: bool, out: *mut u8, cap: usize,
                                          used: *mut usize, wrote: *mut usize) -> i32 {
    let Some(d) = d.as_mut() else { return -1 };
    if used.is_null() || wrote.is_null() || out.is_null() || (src.is_null() && len > 0) {
        return -1;
    }
    let input = if len == 0 { &[][..] } else { std::slice::from_raw_parts(src, len) };
    let output = std::slice::from_raw_parts_mut(out, cap);
    let (status, consumed, produced) = compress(d, input, output, if finish { TDEFLFlush::Finish } else { TDEFLFlush::None });
    *used = consumed;
    *wrote = produced;
    match status {
        TDEFLStatus::Done => 1,
        TDEFLStatus::Okay => 0,
        _ => -1,
    }
}

#[no_mangle]
pub unsafe extern "C" fn tfx_deflate_end(d: *mut CompressorOxide) {
    if !d.is_null() {
        drop(Box::from_raw(d));
    }
}

/// Raw DEFLATE (RFC 1951) inflated into the caller's buffer (TF.EX's skin stream: an image's DeflatedRgba, its exact size known):
/// returns the bytes written, or -1 when the input is not valid DEFLATE or doesn't fit `cap`. No allocation (the decompressor's state
/// is on the stack, about 11 KB).
#[no_mangle]
pub unsafe extern "C" fn tfx_inflate(src: *const u8, len: usize, out: *mut u8, cap: usize) -> isize {
    if src.is_null() || out.is_null() {
        return -1;
    }
    let input = std::slice::from_raw_parts(src, len);
    let output = std::slice::from_raw_parts_mut(out, cap);
    match miniz_oxide::inflate::decompress_slice_iter_to_slice(output, core::iter::once(input), false, true) {
        Ok(n) => n as isize,
        Err(_) => -1,
    }
}

/// A streaming raw-DEFLATE decompressor (TF.NX's updater, tfnxupdate.cpp: a release zip's entries unpacked onto the SD card a buffer at
/// a time; the module has no heap for a 4 MB file and its copy). Its state (with the 32 KB window) is allocated here, about 43 KB, freed by
/// `tfx_inflate_end`; null if it couldn't be made.
#[no_mangle]
pub extern "C" fn tfx_inflate_begin() -> *mut miniz_oxide::inflate::stream::InflateState {
    Box::into_raw(miniz_oxide::inflate::stream::InflateState::new_boxed(miniz_oxide::DataFormat::Raw))
}

/// Feeds `len` compressed bytes and writes inflated bytes to `out` (`cap` free). Sets `*used` (input consumed) and `*wrote` (output
/// written); returns 1 when the stream is complete, 0 to call again (more input, or more room), -1 on invalid data.
#[no_mangle]
pub unsafe extern "C" fn tfx_inflate_feed(s: *mut miniz_oxide::inflate::stream::InflateState, src: *const u8, len: usize, out: *mut u8,
                                          cap: usize, used: *mut usize, wrote: *mut usize) -> i32 {
    let Some(s) = s.as_mut() else { return -1 };
    if used.is_null() || wrote.is_null() || out.is_null() || (src.is_null() && len > 0) {
        return -1;
    }
    let input = if len == 0 { &[][..] } else { std::slice::from_raw_parts(src, len) };
    let output = std::slice::from_raw_parts_mut(out, cap);
    let r = miniz_oxide::inflate::stream::inflate(s, input, output, miniz_oxide::MZFlush::None);
    *used = r.bytes_consumed;
    *wrote = r.bytes_written;
    match r.status {
        Ok(miniz_oxide::MZStatus::StreamEnd) => 1,
        Ok(_) => 0,
        Err(miniz_oxide::MZError::Buf) => 0,   // (no progress possible this call: more input or room needed)
        Err(_) => -1,
    }
}

#[no_mangle]
pub unsafe extern "C" fn tfx_inflate_end(s: *mut miniz_oxide::inflate::stream::InflateState) {
    if !s.is_null() {
        drop(Box::from_raw(s));
    }
}
