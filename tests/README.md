# Tests and audio benchmarks

Run `make host-test` (or `make test`) at the repository root. A host C++20
compiler, GNU Make and Python 3 are required. GoogleTest is bundled. Address and
undefined-behavior sanitizers run by default, with an immediate failure on errors.
No SDL or ARM toolchain is needed for these tests.

Each `tests/*_test.cpp` is discovered automatically and built as a separate
executable against `build/host-tests/libcore.a`. The archive contains the actual
CE production modules selected in `tests/Makefile`. Shared setup initializes
CE's block pool, filter lookup, envelope tables and elapsed-time clocks.

The suite covers the audio graph and ownership, filter responses and transitions,
crusher and mixer behavior, gain ramps, clipping, WAV parsing and malformed
files, SD streaming across buffer boundaries, envelopes, scales, beat-slice note
mapping, natural sorting and seeded random values. `perf_tools_test.py` checks
the performance gate's rejection paths.

`fake_devices.h` implements the existing `Timer` and `FileSystem` interfaces.
Tests can control elapsed time and file bytes, simulate SD read latency, remove
the card, fail opens and return short reads. Unsupported directory enumeration
and sector DMA operations report failure. `audio_test_support.h` supplies graph
sources and sinks. Audio nodes stay alive until their executable exits because
CE's scheduler registry assumes firmware lifetime.

Useful commands are listed below.

| Command | Purpose |
| --- | --- |
| `make host-test` | Run all C++ and Python tests with sanitizers |
| `make test TEST_ARGS='--gtest_filter=WavTest.*'` | Filter C++ cases |
| `make -C tests SANITIZE= test` | Run without sanitizers in a separate build directory |
| `make bench` | Report optimized host audio graph timing |
| `make -C tests bench-m4-build` | Cross-compile the Cortex-M4 benchmark |
| `make bench-m4` | Run it with QEMU instruction counting |
| `make perf-gate` | Check output signatures, pool usage and available M4 budgets |
| `make perf-gate HOST_MAX_BLOCK_US=1000` | Also enforce a host-specific maximum block time |
| `make desktop-check` | Boot CE and compare Pattern, Config and Song captures |

The desktop check requires SDL2 discoverable through pkg-config. CI runs the
sanitized tests, performance gate and desktop check before the firmware build,
and retains benchmark records and screen captures as artifacts.

## Benchmark scope and baselines

The benchmark runs eight real CE mono crusher/filter/amplifier/panner chains,
the stereo dry mixers, and both send mixers through `AudioStream::update_all`.
Its five scenarios are idle, dry playback, active filters, filters with crusher
and overdrive, and repeated filter changes. The sources inject deterministic
samples at the player output. Sample interpolation, player envelopes, effect
returns, sequencer work, input capture and DMA are outside the measured graph.
Output checksumming is outside the timed interval.

Host nanosecond timing is informational unless `HOST_MAX_BLOCK_US` is supplied.
The committed baseline gates output signatures and peak pool usage. QEMU's
`mps2-an386` timer with `-icount shift=0` counts executed M4 instructions. An
instruction count is not a device cycle measurement, and does not model the
Tracker's SDRAM stalls or interrupt contention.

The active scenarios' pool limits were reduced from 24 to 17 blocks after
reusing exclusively owned filter input and sharing identical left/right pan
outputs. Their output signatures remain identical to the original baseline.
Filter mode selection now happens once per buffer. The compiled Cortex-M4
low-pass sample loop uses 25 instructions instead of 28, with approximately
370 additional bytes of filter code in the benchmark. Host timing changes
were mixed, so device CPU savings still require measurement on the Tracker.

`make perf-gate` prints an explicit skip when QEMU or the ARM toolchain is
missing. M4 instruction fields initially remain null until measurements are
recorded. When present, they enforce a maximum 5% increase. The initial host
signatures were recorded using Apple Clang on arm64. The M4 binary uses the
hardware DSP instructions and has separate signature fields.

For a deliberate baseline update, first inspect the audio change and test
results, then record the measured reports explicitly.

```sh
mkdir -p build/perf
make -s bench > build/perf/host.jsonl
make -s bench-m4 > build/perf/m4.jsonl
python3 tools/check_perf.py --baseline tests/perf_baseline.json \
  --host build/perf/host.jsonl --m4 build/perf/m4.jsonl --record
```

Omit `--m4` when only recording host signatures. Record M4 baselines with a
consistent ARM compiler and QEMU version. Baseline updates belong in the same
change as the deliberate output or performance change, with its reason.
