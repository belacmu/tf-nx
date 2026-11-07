# ggrs-switch/vendor

- `ggrs/`: GGRS as TF.EX's ggrs-ffi uses it: [Fcornaire/ggrs](https://github.com/Fcornaire/ggrs) at c510c22 (MIT / Apache-2.0, its
  LICENSE files), with the browser port's 5-line change: `wasm32` code paths limited to non-Emscripten targets, and
  `usize::is_multiple_of` written as `%` for the older toolchain. Used by ggrs-switch and ggrs-ffi-switch (one copy, one GGRS).
  The logo is left out.
