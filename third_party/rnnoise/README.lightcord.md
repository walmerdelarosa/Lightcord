# RNNoise (vendored)

- Upstream: https://github.com/xiph/rnnoise
- Commit: `1cbdbcf1283499bbb2230a6b0f126eb9b236defd` (classic RNNoise with the built-in model in `src/rnn_data.c`,
  so no model download is needed at build time)
- License: BSD-3-Clause (see `COPYING`)

## Local changes

MSVC's C compiler does not support C99 variable-length arrays. `src/common.h` gains an `RNN_VLA(type, name, size)`
macro that expands to a normal VLA on GCC/Clang and to `_alloca()` on MSVC, and the eight VLA declarations in
`src/celt_lpc.c` and `src/pitch.c` use it. No algorithmic changes were made.

Removed from the upstream tree: training scripts (`training/`, `src/rnn_train.py`, `src/compile.sh`), examples
and autotools files, which Lightcord does not use.
