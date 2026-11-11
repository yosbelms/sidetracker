# Native STPalette integration plan

Status: native implementation is present in `core/effector/stpalette/`. Simulator/DSP/build validation is covered by the implementation checks. Physical-device cost calibration, listening validation, and realtime timing certification remain outstanding; provisional estimates are not measured Tracker timings. See that folder's README for supported profiles, benchmarks, and the on-device Measure CPU action.

## 1. Goal and workflow

Integrate all 24 processors from `temp/palette-move-master` behind four native category effects, each exposing a Type selector. Optimize first for the original Tracker's Cortex-M4, with shared production DSP on desktop, Original, Plus, and Mini. Realtime preview takes priority over exact reproduction of Palette's implementation; document intentional sound and bandwidth changes.

Retain the existing eight-stage chain, independent duplicate instances, explicit bypass, saved presets, and Preview/Render workflow. Use the implemented layout: Effects 3/8, Chain 2/8, Parameter 2/8, Value 1/8, with the Value footer on the eighth physical button.

Palette's Move UI, four-slot restriction, uniqueness rule, global feedback, randomizers, and factory presets are outside this integration. Existing PTFX support remains available, but the STPalette collection uses no PTFX packaging or execution.

Append exactly four entries to the Effects catalog, labelled **ST Character**, **ST Movement**, **ST Diffusion**, and **ST Texture**. Each instance selects one of its category's six algorithms through Type; the option order below defines stable serialized Type values 0–5.

| Native effect | Type options, in order |
| --- | --- |
| Character | Drive, Sweeten, Fuzz, Howl, Fold, Swell |
| Movement | Doubler, Vibrato, Phaser, Tremolo, Pitch, Shift |
| Diffusion | Cascade, Reels, Collage, Reverse Delay, Space, Bloom |
| Texture | Filter, Squash, Cassette, Broken, Interference, Halo |

Display the selected algorithm in the chain row, for example **Character: Drive** or **Diffusion: Reverse Delay**. Type uses the existing Value-column list presentation, extended to native enum descriptors. Reverse Delay remains distinct from the Reverse tool. Multiple instances of the same category may select the same or different Types without sharing runtime state.

Use stable parameter indices: Type=0, Amount=1, Macro=2, Drift=3, Sync=4, Tail=5. Amount, Macro, and Drift are percentage controls. Preserve algorithm-specific meanings, including Filter's neutral position at 50%. Show Sync and Tail only when the selected Type supports them, mapping visible parameter rows to their stable stored indices. Retain hidden parameter values so switching Type does not corrupt the chain layout.

New instances select the first Type in their category, with Amount=0%, Macro=50%, Drift=0%, Sync=Free, and Tail=2 seconds; Texture starts with Amount=50% so Filter is neutral. A Type change preserves these common parameter values, resets the selected algorithm's DSP state, and invalidates prepared output. Use explicit bypass independently of Amount.

Tempo-capable processors receive Sync using STPalette's musical divisions and CE's project tempo. Processors with delayed output receive Tail, default 2 seconds, range 0–10 seconds. Process the tail with zero input and fade its final 10 ms; reject output exceeding the existing sample capacity rather than truncating it silently.

## 2. Native DSP and interfaces

- Keep all new STPalette production code in the dedicated `core/effector/stpalette/` folder, under the `samplefx::stpalette` namespace. Organize it as follows:

  ```text
  core/effector/stpalette/
    STPaletteEffects.h           minimal public API for CE integration
    Processor.*         instance lifecycle, Type switching, and quality handling
    Registry.*          four categories, Type options, descriptors, and factories
    groups/             Character, Movement, Diffusion, and Texture algorithms
    dsp/                shared waveshaper, oscillators, filters, delays, resampling
    tables/             generated immutable curves, integrals, and coefficients
    vendor/             only required third-party DSP and its license notices
    tools/              offline Python table generators
    README.md           internal architecture, attribution, and generation steps
  ```

  CE's existing UI/chain files contain only integration glue calling the public API; keep kernels, tables, category metadata, and STPalette-specific lifecycle logic inside this folder. Keep regression tests in the existing `tests/` tree and the integration plan in `docs/`. Firmware and desktop already discover nested effector `.cpp` sources; explicitly add the portable sources to host-test and benchmark builds. Do not add a second independently compiled copy of shared DSP or vendor tables.
- Add a portable native processor interface for preparation, parameter publication, block processing, reset, memory requirements, tail length, supported quality profiles, and measured processing cost. Share it between realtime preview and offline rendering.
- Add a shared descriptor/factory for category names, native enum choices, parameter definitions/defaults, per-Type capabilities, processor creation, and chain validation. Bridge existing effects through their current adapters. Preserve legacy effect IDs, including Modular. Assign category IDs Character=32, Movement=33, Diffusion=34, Texture=35; identify each algorithm by category ID plus Type, not by its own chain effect ID.
- Instantiate, allocate history for, and execute only the selected Type in each stage. Loading all algorithms into firmware does not require six live processors per category. Keep each stage's state independent and allocate it during preparation through the existing editor arena. Reset identically for preview and Render. Derive deterministic drift seeds from category ID, Type, and chain position.
- Treat Type changes as structural edits, including while preview is playing: fade out, stop and release live processing safely, rebuild from the main loop, recalculate capabilities/memory/economy profiles, and restart preview through the existing resume path. Never allocate or initialize a new Type from the audio parameter callback. Ordinary Amount/Macro/Drift edits retain smoothed next-block publication.
- Keep external audio at CE's existing rate and 128-frame blocks. Apply the same stage-boundary quantization, internal resampling, and partial-block handling in preview and Render. Do not add a preview-only floating-point path that changes rendered audio.
- Preserve source attribution and required license notices. Copy only required DSP and data out of the temporary source tree; firmware builds must not depend on `temp/` or fetch external dependencies.

## 3. Cheaper DSP replacements

Apply targeted replacements first. Reduced sample rates and reduced effect complexity are additional economy options, not substitutes for removing avoidable mathematical work.

### Shared waveshaper

Use a shared table-based waveshaper for Drive, Fuzz, Fold, and applicable saturation stages. Drive keeps its clean bass split, bias, and tone path; Fuzz keeps two cascaded nonlinear stages and its filters; Fold retains its folding curve.

- Use single-precision, first-order antiderivative antialiasing (ADAA) around piecewise-linear lookup curves. Evaluate same-segment differences analytically and use a local small-difference branch to avoid cancellation. Use prefix integrals for wider crossings so processing never loops over an unbounded number of table segments.
- Share a 2,049-point signed 16-bit tanh curve between Drive and Fuzz, with a single-precision prefix-integral table and bounded analytic tails. Approximately 12 KiB of constant table storage is shared across all instances, not allocated per stage. Retain the original Fold curve's 4,096 positions, quantize its samples to signed 16-bit, and use a single-precision integral table; this needs approximately 24 KiB in flash, replacing the current approximately 48 KiB of curve/integral storage.
- Generate curve and integral constants offline using high precision, then store immutable arrays in flash. Double precision is permitted in table generation and reference tests, never in the production audio processing path.
- Do not replace antialiased distortion with a plain curve lookup solely to meet a benchmark. Add aliasing and small-input numerical tests before admitting each replacement.

This design follows the piecewise-polynomial ADAA approach in [Werner and Azelborn, DAFx 2023](https://www.dafx.de/paper-archive/2023/DAFx23_paper_61.pdf). The table dimensions and integration choices above are CE design decisions, not performance claims from that paper.

### Alternatives by processor

| Processor | Default optimized implementation | Additional automatic economy option | Expected tradeoff |
| --- | --- | --- | --- |
| Drive, Fuzz | Shared antialiased tanh waveshaper; preserve bias compensation, filters, and stage count. | Run the nonlinear wet path at half/quarter rate with input antialiasing, output reconstruction, and rate-correct filters. | Reduced high-frequency bandwidth; nonlinear aliasing still requires validation. |
| Fold | Quantized reference curve, float integral, stable local ADAA calculation. | Half/quarter-rate nonlinear wet processing with the same antialiasing requirements. | Darker folding; no double-precision table or arithmetic. |
| Sweeten | Precompute its combined `sin(x * abs(x)^0.25) / abs(x)^0.25` curve, including the zero limit, over its bounded input domain. Apply parameter-controlled input/output gains around the table. | Half/quarter-rate nonlinear wet path if needed. | Small curve approximation error; fewer expensive sine/square-root operations. |
| Interference | A 1,025-point positive-domain mu-law table with sign restoration, replacing per-sample logarithms; shared sine lookup for the carrier. | Keep the intentional crusher behavior; use the common reduced-rate wet profile only if it provides a measured saving. | Small companding approximation error; existing crushing remains intentional. |
| Phaser | Calculate sweep/filter targets every 16 samples and interpolate stable allpass coefficients; shared sine oscillator. Retain 12 allpass sections in Full. | Cap the topology at 8 sections in Economy and 4 in Minimum; map Amount smoothly within each cap. | Fewer moving notches; no per-sample `exp2f`/`tanf`. |
| Shift | Keep the 17-section quadrature network, but use shared phase-accumulator sine/cosine lookup. | Half/quarter-rate wet processing with proper filtering and rate-correct shift increments. | Narrower usable band. Preserve true single-sideband frequency shifting; do not substitute ring modulation or truncate the coefficient list. |
| Halo | Compute base resonator delay and detune targets every 16 samples, interpolate tap positions, and multiply by constant inverse voice ratios. Keep inexpensive rational soft clipping in the feedback. | 6 voices in Full; root/fifth/octave/major-tenth in Economy; root/fifth/octave in Minimum. | Reduced chord density; avoid six per-sample delay divisions. |
| Space | Retain the existing compact Clouds tank and encoded delay storage; share modulation helpers and move block workspaces into the arena. | Half/quarter-rate wet tank with rate-scaled delays, damping, modulation, and decay. | Darker tail. A replacement reverb algorithm is unnecessary initially. |
| Bloom | Reuse Space's tank, shared Hann-window lookup for the two shimmer taps, and inexpensive rational feedback saturation. | Half/quarter-rate wet tank and shimmer; keep the external dry path full-rate and delay-aligned. | Darker shimmer and tail, while retaining octave-up regeneration. |
| Pitch, Reverse Delay, Collage | Keep time-domain delay/grain processing, linear interpolation, and shared window lookup. Allocate only the required history; no FFT engine. | Half/quarter-rate wet processing with filtering and physical time/pitch ratios preserved. | Reduced bandwidth; no change to musical timing. |
| Doubler, Vibrato, Cascade, Reels, Cassette, Broken | Shared phase-accumulator LFOs, control-rate drift targets, interpolated delay reads, and bounded ring-buffer indexing. Use existing cheap polynomial/rational saturation where suitable. | Reduced-rate wet processing where measured worthwhile. | Darker wet audio; retain full-rate operation when resampling would cost more than it saves. |
| Squash | Retain full-rate peak detection and hardware single-precision square roots. Precompute fixed coefficients, interpolate control gains, and replace its final sine shaper with a lookup. | Update the gain computer every 4 samples in Economy and every 8 in Minimum, with interpolated gain and a full-rate peak guard. | Less precise envelope tracking; validate transient behavior and clipping. |
| Filter, Swell, Tremolo, Howl | Full-rate processing with control-rate coefficients and lookup/polynomial oscillation or saturation. | Share Full when a distinct economy profile has no useful measured saving. | Keep inexpensive effects simple and responsive. |

### Shared helpers and constraints

- Reuse CE's existing 257-entry Q15 sine table with fractional interpolation and a bounded phase accumulator. Obtain cosine by a quarter-cycle phase offset; derive Hann/squared-sine windows from the same lookup. Avoid separate per-instance trigonometric tables. [Arm's fast sine implementation](https://arm-software.github.io/CMSIS-DSP/latest/group__sin.html) uses the same lookup/interpolation principle; CE will reuse its existing table rather than import another one.
- Precompute smoothing constants at preparation for each internal rate. Update slow random drift and coefficients every 16 samples and interpolate their targets; keep oscillators and signal-dependent feedback sample-accurate.
- Replace callback-time RMS-to-dB gate calculations with equivalent linear-domain thresholds and use square-root/reciprocal approximations or short tables for slow level matching where measurement justifies it. Preserve the envelope time constants and gain limits.
- Use single-precision states for filters, envelopes, and feedback. Use 16-bit long-delay history with an explicit headroom scale and saturating writes; retain floating-point history for short sensitive feedback paths where quantization would impair stability or decay. Size each history for its actual channel count and maximum read position.
- Keep final delay reads, including modulation, within a two-second maximum. Use bounded conditional wrap logic instead of per-sample general division/modulo where buffer geometry permits.
- Half/quarter-rate profiles must contain stateful low-pass decimation and reconstruction, preserve dry/wet alignment, and adjust all sample-rate-dependent constants. Include filter CPU/memory and latency in the profile's measured cost. Simply skipping samples is not an admissible economy implementation. See [Arm's FIR decimator documentation](https://arm-software.github.io/CMSIS-DSP/latest/group__FIR__decimate.html).
- Reducing a nonlinear processor's internal rate can increase aliasing. Such profiles still need ADAA and spectral validation; input/output low-pass filters alone do not remove aliases generated inside the waveshaper.
- Preserve the quadrature structure of Shift and validate unwanted-sideband suppression at each profile's usable bandwidth. [Wardle's frequency-shifter paper](https://www.dafx.de/paper-archive/1998/WAR19.PS.pdf) explains why this differs from ordinary ring modulation.

## 4. Budgets, automatic quality, and persistence

- Use Full, Economy, and Minimum as effect-specific profiles. Selective section/voice/control-rate reductions take precedence over blanket downsampling. Every offered downgrade must have a measured net CPU or memory benefit; effects without a verified cheaper profile keep Full and offer no downgrade.
- Target a maximum 1 ms for the complete preview chain per 128-frame block, and total audio callback time below 70% of the hardware block period. These remain acceptance targets until measured on an original Tracker.
- Measure worst-case stage costs per selected Type across parameter extremes, modulation, silence, tails, channel counts, and quality profiles. Charge the selected algorithm's cost rather than the category's most expensive algorithm or the sum of all six. Include SDRAM access and resampling overhead. Add 25% headroom for admission decisions; use the original Tracker model by default on desktop.
- Start preparation at Full. Repeatedly downgrade the stage yielding the greatest measured saving, breaking ties by chain order, until CPU and arena limits fit. Show Economy whenever any stage is downgraded. Bypassed effects have no processing cost.
- Monitor actual callback cost. Request fade-out and reprepare from the main loop if further reduction is necessary; no reinitialization or allocation in the audio callback. Do not automatically upgrade during playback. If Minimum still exceeds limits, stop preview with an explanation and retain Render.
- Render uses the last resolved preview profiles and deterministic seeds. Without a previous preview, run the same admission resolver. A quality change invalidates prepared audio so Render cannot reuse a stale result.
- Store category ID and the six stable parameter values, including Type, in each STPalette chain node. Validate Type as an integer in 0–5 and resolve its per-Type capabilities before execution. Save per-node quality, resolved-quality state, and whole-chain Mix through the PTPR v1 preset codec in `SampleFxPreset`.
- Use mono processing on Original wherever the topology permits. For its shared reverb tank, combine the tank's outputs for mono; Plus/Mini retain stereo. Do not allocate an unused second mono history.
- Allocate all state, history, resampling buffers, and block workspaces through bounded preparation. No heap allocation, SD access, large clearing, or substantial initialization in the callback. Audit against Original's 4 KiB stack, targeting at most 512 bytes of local stack per new DSP function.

## 5. Delivery and acceptance

First build the shared waveshaper/oscillator infrastructure and performance prototypes for **Fuzz, Drive, and Bloom**. Measure their optimized Full and economy paths on the original Tracker before expanding the catalog. These prototypes are a feasibility gate, not a separate UI or plugin release.

Then integrate and validate the collection in these batches:

| Batch | Effects |
| --- | --- |
| 1 | Filter, Swell, Tremolo |
| 2 | Space, Doubler, Cascade, Cassette |
| 3 | Sweeten, Howl, Vibrato, Phaser, Pitch, Shift, Reels, Collage, Reverse Delay, Squash, Broken, Interference, Halo |
| 4 | Drive, Fuzz, Fold, Bloom |

Each batch must pass:

- Sanitized mono/stereo production-DSP tests: bounds, silence, impulses, extreme parameters, near-identical adjacent samples, segment boundaries, clipping, reset, duplicate instances, partial blocks, and tails. Check allocation and stack budgets.
- Category/Type tests: all four selectors and 24 algorithms, stable enum values, malformed/out-of-range Type rejection, conditional Sync/Tail visibility, independent repeated category instances, and Type changes during preview. Verify only the selected algorithm allocates and processes, ordinary parameter values survive a switch, and no initialization occurs in the audio callback.
- Exact preview-versus-Render comparisons for fixed parameters, profiles, and seeds, including economy profiles and rate-conversion latency. Repeat after stop/restart and preset reload.
- Reference comparisons for frequency response, level, transient behavior, aliasing, modulation timing, pitch, resonator tuning, reverb decay, and Shift's unwanted sideband. Document intentional differences per profile. Include listening checks at high drive and on quiet decays.
- Native M4 disassembly/symbol inspection confirming no software double helpers or general-purpose transcendental calls in new sample-processing loops. Control-rate single-precision calls are allowed when bounded and included in measurements.
- Automatic-quality tests: deterministic selection/ties, resampler overhead, parameter extremes, insufficient memory, runtime downgrade, stale-result invalidation, preset round trips, and failure at Minimum. Exercise duplicate expensive effects and representative eight-stage mixed chains.
- Existing desktop workflow checks for Add, reordering, bypass, duplication, parameter editing, presets, retained drafts, repeated preview, and physical view switching. Verify exactly four STPalette browser entries, the selected Type in each chain row, native enum editing, and actual native execution in the simulator.
- Original Tracker worst-case cycle measurements for each offered profile, plus firmware builds and flash/internal-RAM/arena/stack reports for Original, Plus, and Mini. Host/QEMU figures are supporting evidence, not device timing certification.

Completion requires all 24 effects to have an individually safe realtime profile on the original Tracker. Eight structural slots remain available; measured combined cost determines the economy level needed for a particular chain. Report measured costs and profile tradeoffs with the implementation rather than predicting an arbitrary safe effect count.
