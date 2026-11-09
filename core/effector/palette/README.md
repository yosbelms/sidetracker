# Native Palette

Native Sample Editor effects shared by Original, Plus, Mini, and desktop. No
runtime dependency on `temp/`, Move, PTFX, or downloaded resources.

## Interface and layout

`Palette.h` is the CE integration boundary. `Registry.cpp` owns the four stable
category IDs (32–35), their six Type choices, parameter metadata/capabilities,
memory admission, and provisional CPU estimates. `Processor.cpp` owns one
selected algorithm per instance, arena state, smoothing, resampling, level
matching, and bounded tails. `groups/` contains the 24 kernels; `dsp/` and
`tables/` contain shared helpers and immutable data. Only the selected Type's
delay history and reverb tank are allocated. Instances never share mutable DSP.

The browser labels are Palette - Character, Palette - Movement, Palette -
Diffusion, and Palette - Texture; chain rows retain category and Type names.
Up/Down move to the preceding/following item in Type and Sync lists, while the
encoder keeps its normal list direction.

Parameters have stable indices Type, Amount, Macro, Drift, Sync, Tail. Hidden
Sync/Tail values remain in chains. Type and Sync changes rebuild live processing
after a short fade, outside the audio callback; ordinary controls are smoothed.
Changing project tempo updates sync without reallocating. Reverse Delay is an
audio effect; the destructive Reverse tool remains separate.

The same processor, stage index seed, quality and 128-frame boundaries drive
render and preview. Profile resampling delays are included in output lengths so
the final source samples are flushed. Source and individual stages remain
quantized at the existing 16-bit chain boundaries.

## Optimization and profiles

- Tanh/Fold use quantized lookup curves and float ADAA integrals, including a
  local small-difference branch. No software double math is used by native DSP.
- Existing CE sine data is shared for oscillators, windows and appropriate
  shaping. Sweeten and mu-law companding use precomputed compound curves.
- Phaser interpolates coefficients and uses 12/8/4 sections. Halo uses 6/4/3
  resonators. Squash updates its gain computer every 1/4/8 samples.
- Supported half-rate wet paths use stateful symmetric half-band filters,
  aligned full-rate dry audio, scaled timing and retimed filter coefficients.
  Reduced-rate paths intentionally have less high-frequency bandwidth.
- Large buffers/workspaces live in the editor arena. Delay history is bounded,
  headroom-scaled 16-bit storage; Clouds retains its encoded tank format. Mono
  does not allocate a second channel history.

`processingQuality()` maps requested profiles to supported implementations.
Cheap effects remain full-rate. Known losing resampling profiles are excluded
from automatic selection: merely lowering sample rate is not a CPU saving once
filtering is included. Current enabled alternate rates are conservative host
profiling choices, not certified MK66 timings. The table will need recalibration
from physical-device reports.

Automatic admission targets 1 ms per chain with 25% estimate headroom. The device
watchdog checks consecutive chain blocks against 1 ms and the preceding complete
audio callback against 70% of the block period. Downgrades fade and rebuild on
the main loop, never allocate from an interrupt, and do not upgrade during
playback. If no supported lower profile fits, preview stops and Render remains
available. These safeguards do not constitute timing certification.

Chains containing Palette use PTCH v2: category IDs plus six stored parameters,
quality in each node's previously unused last byte, and a resolved-quality flag.
Legacy-only chains retain v1. Render honors the last resolved preview quality.
The host’s global Mix blends after the chain, compensates known profile latency,
and defaults to 100% wet for older files (reserved header byte 7 is zero).

The [Palette showcase pack](../../../presets/palette/README.md) covers all 24
algorithms in eight chains. Its guide documents the settings, installation and
`python3 tools/generate_palette_chains.py` command for rebuilding the files and ZIP.

## Profiling and verification

Select a Palette stage in Chain, press physical Insert, then **Measure CPU**.
Playback stops and the selected Type is tested with deterministic input and
parameter sweeps at each requested profile. `/Effects/PaletteCPU.csv` is replaced
with the report. It contains effective profile, maximum/average timer ticks,
ticks per microsecond, and actual arena use. Divide ticks by ticks-per-us on
hardware; desktop reports describe the desktop, not the Tracker. This measures
the selected kernel/adapters rather than certifying an entire mixed chain.

`Profiling.h` exposes the same bounded benchmark for device tools through a
caller-supplied arena and cycle clock. Host and Cortex-M4 build targets are:

```sh
make -C tests palette-bench
make -C tests palette-bench-m4-build
make -C tests palette-bench-m4  # requires qemu-system-arm
make -C tests test
make -C tests STEREO=1 test
make desktop-check
```

QEMU instruction timing and host timing are supporting data. Physical Original
Tracker timing, listening checks, and complete-chain stress tests remain
required before claiming a realtime-safe effect count or certified quality
budget. No hardware measurements have been recorded in this implementation.

Regenerate immutable curve/integral tables with:

```sh
python3 core/effector/palette/tools/generate_tables.py
```

The Python generator uses high precision; generated production tables and DSP
use 16-bit samples/single precision. `tools/reference_fold.txt` preserves the
upstream curve for reproducible generation, independently of the temporary
source checkout.

## Attribution

Adapted from PALETTE, copyright 2026 Filliformes, under the MIT license in
`LICENSE`. The group kernels contain Filliformes original DSP and adaptations
of Airwindows (Chris Johnson), including Density/Swell/Tremolo/Pressure4/DeRez2,
and Mutable Instruments Warps/Clouds (Emilie Gillet). Warps contributes the fold
curve and Hilbert poles; only required Clouds headers are vendored. Reverb
changes parameterize delay lengths for native internal rates. Existing source
copyright notices are preserved. See `vendor/AIRWINDOWS_LICENSE.txt` and
`vendor/MUTABLE_LICENSE.txt` for the required third-party notices. CE's existing
sine data remains under its existing license.
