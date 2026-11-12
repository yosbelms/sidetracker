# STPalette showcase patches

Twenty native STPalette patches for Sample Editor → Effects. Tailored as expressive sonic texturizers for synths and sample material, they use three or four stages each and collectively cover all 24 algorithms. Designed around STPalette’s four categories, the collection ranges from warm analog lo-fi degradation and tape saturation to aggressive wavefolding, formant shaping, rhythmic gating, and crystalline granular clouds.

## Install and load

Copy `Effects/Patches/` from `build/SD/` or `build/STPaletteShowcase.zip` to the matching folder on your Tracker SD card or desktop virtual card. Each patch uses its descriptive filename, such as `Worn Cassette.patch` or `Radio Static.patch`. Preserve any file with the same name before replacing it.

Load a sample, open **Sample Editor → Effects**, focus the **Chain** column, press physical **Insert**, choose **Load Patch**, then select the patch below (e.g. **Worn Cassette**, **Radio Static**). Press **Preview** to audition or **Apply** to update the current sample slot.

Patches contain settings only and use the currently selected sample. Synced stages follow the project tempo; 120 BPM is a useful comparison point. Try the same input through multiple patches, then explore their suggested sources. Mono processing works on Original Tracker; spatial differences are more apparent with stereo material on Plus/Mini.

Quality is left unresolved so preview can choose its profile for the device, channel count and available memory. Preview and Render use the shared native DSP; rendering after a preview retains its resolved profiles. These patches have not been timed or listened to on physical Tracker hardware.

## Patches

| # | Name | Character | Processing order |
| --- | --- | --- | --- |
| 01 | Worn Cassette | Analog Lo-Fi Tape | Sweeten → Vibrato → Reels → Cassette |
| 02 | Radio Static | Digital telecom / crunch | Drive → Interference → Squash |
| 03 | Metallic Chimes | Acoustic resonant chimes | Shift → Halo → Space |
| 04 | Kinetic Stutter | Rhythmic slicing pulse | Phaser → Tremolo → Cascade |
| 05 | Abyssal Drone | Dark sub-harmonic ambient | Pitch → Swell → Bloom |
| 06 | Acid Screamer | Resonant wavefolded growl | Howl → Fold → Filter |
| 07 | Glitch Collage | Granular particle spray | Doubler → Collage → Reverse Delay |
| 08 | Motor Breakdown | Tape chew & mechanical stall | Broken → Fuzz → Cassette |
| 09 | Vaporwave VHS | Modulated lo-fi tape chorus | Doubler → Vibrato → Reels → Sweeten |
| 10 | Resonant Formant | Vowel morphing throat filter | Howl → Filter → Squash |
| 11 | Sub Weight | Sub-octave harmonic enhancer | Pitch → Drive → Filter |
| 12 | Crystalline Halo | Celestial shimmer diffusion | Halo → Shift → Bloom |
| 13 | Digital Rust | Wavefolded digital oxidation | Interference → Fold → Cassette |
| 14 | Spectral Shiver | Frequency-shifted liquid phaser | Phaser → Shift → Space |
| 15 | Ambient Bloom Swell | Evolving swell soundscape | Swell → Space → Bloom |
| 16 | Sputter Drone | Velcro fuzz cavern stutter | Fuzz → Broken → Space |
| 17 | Tape Chorus Echo | Multi-head tape slap & width | Doubler → Vibrato → Cascade |
| 18 | Overfolded Drive | Overdriven Buchla wavefolder | Drive → Fold → Sweeten |
| 19 | Granular Warp | Granular pitch cloud & slices | Pitch → Collage → Reverse Delay |
| 20 | Pumping Noise Floor | Breathing tape companding | Interference → Squash → Reels |

## Settings

Amount, Macro and Drift use the editor’s 0–100 scale. Tail is the additional duration in seconds for that stage; stage tails accumulate through the chain. `—` means the control is hidden for that Type (stored as zero). Macro changes meaning with Type: it is not a universal dry/wet control. Mix controls the final linear dry/wet blend across the chain (0–100%). A machine-readable copy of the settings is also recorded in [patches.json](patches.json).

### 01 — Worn Cassette (Mix: 100%)

Warm console saturation, mechanical tape wow/flutter, short tape echoes and cassette degradation.

**Try:** Drums, acoustic guitar, keys or full mixes.

**Explore:** Raise Cassette Drift for more tape warble; raise Vibrato Amount for deeper pitch wow.

| Category | Type | Amount | Macro | Drift | Sync | Tail (s) |
| --- | --- | ---: | ---: | ---: | --- | ---: |
| Character | Sweeten | 45 | 38 | 18 | — | — |
| Movement | Vibrato | 24 | 16 | 35 | Free | 0.05 |
| Diffusion | Reels | 32 | 28 | 25 | Free | 0.8 |
| Texture | Cassette | 58 | 42 | 30 | — | — |

### 02 — Radio Static (Mix: 100%)

Hot input drive, mu-law digital crushing and heavy vari-mu pumping compression.

**Try:** Breaks, synth riffs, basslines and voice samples.

**Explore:** Adjust Interference Macro to shift the aliasing carrier; raise Squash Amount for more aggressive pump.

| Category | Type | Amount | Macro | Drift | Sync | Tail (s) |
| --- | --- | ---: | ---: | ---: | --- | ---: |
| Character | Drive | 42 | 65 | 12 | — | — |
| Texture | Interference | 55 | 40 | 38 | — | — |
| Texture | Squash | 68 | 55 | 15 | — | — |

### 03 — Metallic Chimes (Mix: 85%)

Inharmonic frequency shifting feeding a tuned acoustic Karplus resonator chord bank and diffuse chamber.

**Try:** Plucks, percussion, one-shots, bells or single notes. Halo root defaults to A2.

**Explore:** Halo Macro changes the root chord pitch; Shift Macro shifts metallic sideband overtones.

| Category | Type | Amount | Macro | Drift | Sync | Tail (s) |
| --- | --- | ---: | ---: | ---: | --- | ---: |
| Movement | Shift | 36 | 53.5 | 14 | — | — |
| Texture | Halo | 62 | 50 | 25 | — | 2 |
| Diffusion | Space | 48 | 68 | 20 | — | 2.2 |

### 04 — Kinetic Stutter (Mix: 100%)

Slow sweeping comb filter notches chopped by a synced rhythmic gate and bouncing polyrhythmic delay.

**Try:** Sustained pads, drones, basslines or ambient guitars.

**Explore:** Tremolo Sync changes the chop rate; Cascade Sync sets the delay polyrhythm.

| Category | Type | Amount | Macro | Drift | Sync | Tail (s) |
| --- | --- | ---: | ---: | ---: | --- | ---: |
| Movement | Phaser | 58 | 24 | 22 | 1/1 | — |
| Movement | Tremolo | 82 | 50 | 45 | 1/8 | — |
| Diffusion | Cascade | 42 | 50 | 16 | 1/8D | 1.6 |

### 05 — Abyssal Drone (Mix: 80%)

Pitch-dropped octave sub-harmonics, dynamic slow swell, and an expansive pitch-regenerating shimmer wash.

**Try:** Dry hits, isolated plucks, stabs or sparse vocal fragments.

**Explore:** Pitch Amount balances the deep sub rumble; Bloom Amount controls the ambient wash duration.

| Category | Type | Amount | Macro | Drift | Sync | Tail (s) |
| --- | --- | ---: | ---: | ---: | --- | ---: |
| Movement | Pitch | 62 | 25 | 30 | — | 0.12 |
| Character | Swell | 72 | 72 | 15 | — | — |
| Diffusion | Bloom | 60 | 80 | 25 | — | 3.5 |

### 06 — Acid Screamer (Mix: 90%)

Vocal formant filter-fuzz growl pushed into aggressive West Coast wavefolding and resonant filtering.

**Try:** Synth leads, acid sequences, drum breaks and bass.

**Explore:** Howl Macro sweeps the formant throat; Filter Amount sweeps low-pass (<50) or high-pass (>50).

| Category | Type | Amount | Macro | Drift | Sync | Tail (s) |
| --- | --- | ---: | ---: | ---: | --- | ---: |
| Character | Howl | 70 | 44 | 26 | — | — |
| Character | Fold | 64 | 58 | 20 | — | — |
| Texture | Filter | 42 | 55 | 18 | — | — |

### 07 — Glitch Collage (Mix: 85%)

Stereo micro-detuning feeding an asynchronous granular cloud with randomized pitch and backward delay fragments.

**Try:** Vocal phrases, acoustic instruments, percussion or field recordings.

**Explore:** Collage Drift controls grain unpredictability; Reverse Delay Macro adjusts reversed slice length.

| Category | Type | Amount | Macro | Drift | Sync | Tail (s) |
| --- | --- | ---: | ---: | ---: | --- | ---: |
| Movement | Doubler | 35 | 30 | 28 | — | 0.1 |
| Diffusion | Collage | 66 | 42 | 75 | 1/8 | 1.8 |
| Diffusion | Reverse Delay | 46 | 48 | 18 | 1/4 | 1.1 |

### 08 — Motor Breakdown (Mix: 100%)

Erratic motor drag, tape chewing dropouts, sputtering vintage transistor fuzz and cassette wear.

**Try:** Melodic loops, chords, drum grooves or bass riffs.

**Explore:** Broken Amount sets the stall severity; Fuzz Amount dials in the sputtering transistor grit.

| Category | Type | Amount | Macro | Drift | Sync | Tail (s) |
| --- | --- | ---: | ---: | ---: | --- | ---: |
| Texture | Broken | 76 | 42 | 70 | — | 0.22 |
| Character | Fuzz | 54 | 38 | 22 | — | — |
| Texture | Cassette | 46 | 48 | 32 | — | — |

### 09 — Vaporwave VHS (Mix: 85%)

Lush stereo chorus detuning, analog tape wow, warm reel-to-reel slapback and exciter presence.

**Try:** Synth pads, electric pianos, Rhodes, polyphonic synth stabs.

**Explore:** Raise Vibrato Drift for warped tape tracking; adjust Reels Macro for tape warmth.

| Category | Type | Amount | Macro | Drift | Sync | Tail (s) |
| --- | --- | ---: | ---: | ---: | --- | ---: |
| Movement | Doubler | 48 | 35 | 20 | — | 0.08 |
| Movement | Vibrato | 32 | 20 | 45 | Free | 0.04 |
| Diffusion | Reels | 38 | 24 | 28 | Free | 0.6 |
| Character | Sweeten | 50 | 45 | 15 | — | — |

### 10 — Resonant Formant (Mix: 100%)

Vocal vowel formant peaks driving resonant filtering and heavy companding punch.

**Try:** Sustained synth leads, wavetable sweeps, basslines and organ chords.

**Explore:** Howl Macro sweeps vowel formants; Filter Macro shapes the resonant frequency cutoff.

| Category | Type | Amount | Macro | Drift | Sync | Tail (s) |
| --- | --- | ---: | ---: | ---: | --- | ---: |
| Character | Howl | 65 | 55 | 32 | — | — |
| Texture | Filter | 52 | 60 | 18 | — | — |
| Texture | Squash | 58 | 40 | 10 | — | — |

### 11 — Sub Weight (Mix: 90%)

Sub-octave harmonic generation, warm preamp overdrive and steep low-pass contouring.

**Try:** Synth bass, thin sample plucks, kick drums and mono synth leads.

**Explore:** Pitch Amount balances the sub-octave rumble; Drive Amount adjusts harmonic thickness.

| Category | Type | Amount | Macro | Drift | Sync | Tail (s) |
| --- | --- | ---: | ---: | ---: | --- | ---: |
| Movement | Pitch | 55 | 20 | 12 | — | 0.08 |
| Character | Drive | 48 | 45 | 15 | — | — |
| Texture | Filter | 38 | 40 | 10 | — | — |

### 12 — Crystalline Halo (Mix: 70%)

Tuned Karplus physical resonator chord bank, delicate frequency-shifted detuning and shimmering bloom.

**Try:** Bell synths, electric piano, ambient chords and sparse plucks.

**Explore:** Halo Macro alters the resonator chord root; Bloom Macro controls high-frequency shimmer decay.

| Category | Type | Amount | Macro | Drift | Sync | Tail (s) |
| --- | --- | ---: | ---: | ---: | --- | ---: |
| Texture | Halo | 58 | 64 | 22 | — | 2.4 |
| Movement | Shift | 28 | 52 | 16 | — | — |
| Diffusion | Bloom | 54 | 75 | 20 | — | 3.2 |

### 13 — Digital Rust (Mix: 100%)

Aggressive mu-law downsampling and aliasing, West Coast wavefolding and degraded cassette grit.

**Try:** Drum breaks, digital synths, industrial bass and gritty sample hits.

**Explore:** Interference Macro alters aliasing bit-depth; Fold Amount introduces jagged folded overtones.

| Category | Type | Amount | Macro | Drift | Sync | Tail (s) |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| Texture | Interference | 62 | 48 | 30 | — | — |
| Character | Fold | 52 | 62 | 18 | — | — |
| Texture | Cassette | 50 | 36 | 25 | — | — |

### 14 — Spectral Shiver (Mix: 75%)

Slow sweeping comb filter notches, eerie inharmonic frequency shifting and a vast reverberant space.

**Try:** Synth strings, choir pads, drone textures and atmospheric soundscapes.

**Explore:** Phaser Sync alters sweep cycles; Shift Macro introduces ghostly bell-like dissonances.

| Category | Type | Amount | Macro | Drift | Sync | Tail (s) |
| --- | --- | ---: | ---: | ---: | --- | ---: |
| Movement | Phaser | 60 | 35 | 24 | 1/2 | — |
| Movement | Shift | 32 | 48.5 | 20 | — | — |
| Diffusion | Space | 52 | 62 | 25 | — | 2.6 |

### 15 — Ambient Bloom Swell (Mix: 80%)

Gradual envelope auto-swell, expansive chamber reflections and a rich pitch-shifted bloom shimmer.

**Try:** Percussive clicks, guitar plucks, piano chords and short synth arps.

**Explore:** Swell Amount adjusts the attack swell rise; Bloom Tail sets the ambient decay duration.

| Category | Type | Amount | Macro | Drift | Sync | Tail (s) |
| --- | --- | ---: | ---: | ---: | --- | ---: |
| Character | Swell | 78 | 68 | 14 | — | — |
| Diffusion | Space | 58 | 55 | 18 | — | 2.8 |
| Diffusion | Bloom | 62 | 72 | 26 | — | 3.8 |

### 16 — Sputter Drone (Mix: 90%)

Velcro transistor fuzz, mechanical motor dropouts and deep cavernous reverberation.

**Try:** Analog saw waves, heavy basslines, distorted beats and noise loops.

**Explore:** Fuzz Amount increases sputtering saturation; Broken Drift randomizes dropout stutter rate.

| Category | Type | Amount | Macro | Drift | Sync | Tail (s) |
| --- | --- | ---: | ---: | ---: | --- | ---: |
| Character | Fuzz | 68 | 44 | 25 | — | — |
| Texture | Broken | 64 | 38 | 65 | — | 0.18 |
| Diffusion | Space | 44 | 40 | 20 | — | 2 |

### 17 — Tape Chorus Echo (Mix: 85%)

Wide stereo double-tracking, pitch-drifted tape vibrato and synchronized multi-tap delay bounces.

**Try:** Synth brass, lead synths, arpeggios, electric guitars and vocal tracks.

**Explore:** Doubler Macro alters stereo spread; Cascade Sync sets delay division to the project tempo.

| Category | Type | Amount | Macro | Drift | Sync | Tail (s) |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| Movement | Doubler | 52 | 45 | 22 | — | 0.12 |
| Movement | Vibrato | 28 | 22 | 40 | Free | 0.06 |
| Diffusion | Cascade | 45 | 48 | 20 | 1/4D | 1.8 |

### 18 — Overfolded Drive (Mix: 85%)

Hot analog overdrive pushing into Buchla-style asymmetric wavefolding and high-frequency exciter air.

**Try:** Sine/triangle synth leads, Rhodes chords, 808 subs and acoustic drums.

**Explore:** Drive Amount controls input breakup; Fold Amount dials in rich wavefolded overtones.

| Category | Type | Amount | Macro | Drift | Sync | Tail (s) |
| --- | --- | ---: | ---: | ---: | --- | ---: |
| Character | Drive | 58 | 60 | 15 | — | — |
| Character | Fold | 56 | 52 | 18 | — | — |
| Character | Sweeten | 42 | 50 | 12 | — | — |

### 19 — Granular Warp (Mix: 75%)

Detuned micro-pitch shifting feeding a randomized granular particle cloud and reversed temporal slices.

**Try:** Vocal samples, acoustic instruments, ambient pads and melodic synth lines.

**Explore:** Collage Drift varies grain dispersion; Reverse Delay Sync sets the backward loop division.

| Category | Type | Amount | Macro | Drift | Sync | Tail (s) |
| --- | --- | ---: | ---: | ---: | --- | ---: |
| Movement | Pitch | 40 | 55 | 25 | — | 0.14 |
| Diffusion | Collage | 62 | 50 | 70 | 1/16D | 2 |
| Diffusion | Reverse Delay | 50 | 45 | 20 | 1/8 | 1.4 |

### 20 — Pumping Noise Floor (Mix: 90%)

Cassette RF interference and aliasing grit compressed by an aggressive pumping vari-mu compressor into vintage tape reels.

**Try:** Drum grooves, synth plucks, basslines and minimal percussive sequences.

**Explore:** Squash Amount sets the compression pump depth; Interference Amount adjusts background noise texture.

| Category | Type | Amount | Macro | Drift | Sync | Tail (s) |
| --- | --- | ---: | ---: | ---: | --- | ---: |
| Texture | Interference | 46 | 32 | 35 | — | — |
| Texture | Squash | 75 | 62 | 16 | — | — |
| Diffusion | Reels | 36 | 35 | 24 | 1/8 | 0.9 |


## Regenerate

From the repository root:

```sh
make sd
```

The generator reads `patches.json`, preserves its names and settings, and writes the patches to `build/SD/Effects/Patches/` and `build/STPaletteShowcase.zip`. Python 3, Make and a C++20 host compiler are required.
