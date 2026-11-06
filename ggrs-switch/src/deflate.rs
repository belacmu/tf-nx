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
