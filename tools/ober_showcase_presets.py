"""Original SideTracker patches for the OBER synth."""

OFF, SAW, PULSE, SAW_PULSE = range(4)
LP12, HP12, BP12, NOTCH, SEM, LADDER = range(6)
TRIANGLE, SQUARE, SINE, RAMP, SAMPLE_HOLD, SMOOTH_RANDOM = 0, 2, 3, 4, 5, 6
QUARTER, SIXTEENTH, EIGHTH_TRIPLET, EIGHTH_DOTTED, TWO_BARS = 3, 5, 6, 7, 8
OSC_1, OSC_2 = range(2)
OCTAVE_DOWN, OCTAVE_UP = 2, 4

OBER_SHOWCASE = (
    # SEM and notch pads and chords.
    ("Vowel Canopy", dict(
        Wave1=SAW, Wave2=PULSE, Pulse2=0.27, Level1=0.55, Level2=0.45, Tune2=0.07,
        Filter=SEM, Morph=0.59, Cutoff=1040, Resonance=0.66, KeyTrack=0.39,
        ContourAmount=1.3, FilterAttack=0.42, FilterDecay=1.2, FilterSustain=0.23,
        AmpAttack=0.25, AmpDecay=1.8, AmpSustain=0.72, AmpRelease=1.0,
        LfoWave=SINE, Rate=0.29, LfoFilter=0.72, Level=0.83,
    )),
    ("Hollow Halo", dict(
        Wave1=PULSE, Pulse=0.49, Level1=0.62, Wave2=PULSE, Pulse2=0.41, Tune2=-0.15, Level2=0.49,
        Filter=NOTCH, Cutoff=980, Resonance=0.76, KeyTrack=0.14, Drive=0.04,
        ContourAmount=0.6, FilterAttack=0.24, FilterDecay=0.9, FilterSustain=0.3,
        AmpAttack=0.36, AmpDecay=2.0, AmpSustain=0.81, AmpRelease=1.3,
        LfoWave=TRIANGLE, Rate=0.23, LfoFilter=1.15, PWM=0.21, PwmTarget=OSC_2, Level=0.77,
    )),
    ("Velvet Formant", dict(
        Wave1=SAW, Level1=0.67, Wave2=SAW, Range2=OCTAVE_DOWN, Tune2=-0.08, Level2=0.30,
        Filter=SEM, Morph=0.27, Cutoff=1850, Resonance=0.45, KeyTrack=0.34,
        ContourAmount=-2.6, FilterAttack=0.025, FilterDecay=0.84, FilterSustain=0.46,
        AmpAttack=0.08, AmpDecay=1.05, AmpSustain=0.75, AmpRelease=0.67,
        LfoWave=SINE, Rate=0.19, LfoAmp=0.17, Level=0.76,
    )),
    ("Frosted Strings", dict(
        Wave1=SAW, Level1=0.55, Wave2=SAW_PULSE, Pulse2=0.18, Tune2=0.18, Level2=0.46,
        Filter=NOTCH, Cutoff=2700, Resonance=0.49, KeyTrack=0.46, Drive=0.02,
        ContourAmount=-0.85, FilterAttack=0.28, FilterDecay=0.66, FilterSustain=0.58,
        AmpAttack=0.13, AmpDecay=0.79, AmpSustain=0.80, AmpRelease=0.50,
        LfoWave=SINE, Rate=4.7, LfoPitch=0.13, PitchTarget=OSC_2, Level=0.70,
    )),
    ("Air Lantern", dict(
        Wave1=PULSE, Pulse=0.46, Level1=0.57, Wave2=SAW, Range2=OCTAVE_UP, Tune2=0.04,
        Level2=0.16, Noise=0.10, Filter=SEM, Morph=0.91, Cutoff=670, Resonance=0.23,
        KeyTrack=0.47, Drive=0.02, ContourAmount=-1.15, FilterAttack=0.52,
        FilterDecay=1.25, FilterSustain=0.51, AmpAttack=0.59, AmpDecay=2.0,
        AmpSustain=0.74, AmpRelease=1.7, LfoWave=SMOOTH_RANDOM, Rate=0.39,
        PWM=0.17, PwmTarget=OSC_1, LfoAmp=0.25, Level=0.82,
    )),
    ("Glass Choir", dict(
        Wave1=PULSE, Pulse=0.22, Level1=0.62, Wave2=PULSE, Pulse2=0.32,
        Tune2=7.02, Level2=0.25, Filter=SEM, Morph=0.51, Cutoff=1290,
        Resonance=0.78, KeyTrack=0.60, Drive=0.08, ContourAmount=0.95,
        FilterAttack=0.62, FilterDecay=0.83, FilterSustain=0.61,
        AmpAttack=0.38, AmpDecay=1.7, AmpSustain=0.78, AmpRelease=1.17,
        LfoWave=SINE, Rate=3.6, LfoPitch=0.08, PitchTarget=OSC_1, Level=0.84,
    )),

    # High-pass and band-pass keys, stabs, and noise.
    ("Paper Clav", dict(
        Wave1=PULSE, Pulse=0.16, Level1=0.71, Wave2=SAW, Range2=OCTAVE_UP, Tune2=0, Level2=0.23,
        Filter=HP12, Cutoff=690, Resonance=0.53, KeyTrack=0.75, ContourAmount=-0.87,
        FilterAttack=0.002, FilterDecay=0.17, FilterSustain=0.05,
        AmpAttack=0.002, AmpDecay=0.22, AmpSustain=0.24, AmpRelease=0.07, Level=0.80,
    )),
    ("Silver Chops", dict(
        Wave1=SAW, Level1=0.64, Wave2=SAW, Tune2=0.19, Level2=0.60,
        Filter=HP12, Cutoff=430, Resonance=0.30, KeyTrack=0.28,
        ContourAmount=-1.0, FilterAttack=0.013, FilterDecay=0.33, FilterSustain=0.22,
        AmpAttack=0.014, AmpDecay=0.23, AmpSustain=0.11, AmpRelease=0.10, Level=0.76,
    )),
    ("Quill Keys", dict(
        Wave1=PULSE, Pulse=0.26, Level1=0.75, Wave2=PULSE, Pulse2=0.39, Tune2=0.03, Level2=0.39,
        Filter=BP12, Cutoff=690, Resonance=0.78, KeyTrack=0.94, Drive=0.10,
        ContourAmount=2.1, FilterAttack=0.005, FilterDecay=0.14, FilterSustain=0.22,
        AmpAttack=0.006, AmpDecay=0.41, AmpSustain=0.27, AmpRelease=0.14, Level=0.84,
    )),
    ("Tin Radio", dict(
        Wave1=SAW_PULSE, Pulse=0.21, Level1=0.54, Wave2=SAW, Range2=OCTAVE_DOWN, Level2=0.17,
        Noise=0.17, Filter=BP12, Cutoff=1610, Resonance=0.66, KeyTrack=0.20,
        Drive=0.39, ContourAmount=-1.35, FilterAttack=0.23, FilterDecay=0.59,
        FilterSustain=0.51, AmpAttack=0.058, AmpDecay=0.71, AmpSustain=0.61, AmpRelease=0.25,
        LfoWave=SMOOTH_RANDOM, Rate=2.6, LfoFilter=0.27, Level=0.80,
    )),
    ("Wind Needle", dict(
        Wave1=PULSE, Pulse=0.11, Level1=0.25, Wave2=OFF, Level2=0, Noise=0.58,
        Filter=HP12, Cutoff=1880, Resonance=0.68, KeyTrack=0.87,
        ContourAmount=1.25, FilterAttack=0.27, FilterDecay=0.70, FilterSustain=0.43,
        AmpAttack=0.17, AmpDecay=0.52, AmpSustain=0.65, AmpRelease=0.65,
        LfoWave=SINE, Rate=0.77, LfoAmp=0.42, Level=0.87,
    )),

    # Tempo-synced modulation using several waveforms and divisions.
    ("Metered Silk", dict(
        Wave1=PULSE, Pulse=0.35, Level1=0.68, Wave2=SAW, Range2=OCTAVE_DOWN, Tune2=-0.07, Level2=0.29,
        Filter=LP12, Cutoff=1320, Resonance=0.29, ContourAmount=0.42,
        AmpAttack=0.061, AmpDecay=0.62, AmpSustain=0.81, AmpRelease=0.33,
        Sync=EIGHTH_DOTTED, LfoWave=TRIANGLE, PWM=0.70, PwmTarget=OSC_1,
        LfoFilter=0.23, Retrigger=1, Level=0.77,
    )),
    ("Quarter Shutter", dict(
        Wave1=SAW_PULSE, Pulse=0.34, Level1=0.64, Wave2=PULSE, Pulse2=0.52, Tune2=7.0, Level2=0.32,
        Filter=LP12, Cutoff=2380, Resonance=0.19, ContourAmount=0.54,
        AmpAttack=0.021, AmpDecay=0.82, AmpSustain=0.78, AmpRelease=0.24,
        Sync=QUARTER, LfoWave=SQUARE, LfoAmp=0.88, LfoFilter=0.32, Retrigger=1, Level=0.78,
    )),
    ("Broken Compass", dict(
        Wave1=SAW, Level1=0.69, Wave2=PULSE, Pulse2=0.23, Range2=OCTAVE_UP, Level2=0.34,
        Filter=BP12, Cutoff=780, Resonance=0.61, KeyTrack=0.38, ContourAmount=-0.23,
        AmpAttack=0.012, AmpDecay=0.62, AmpSustain=0.82, AmpRelease=0.21,
        Sync=SIXTEENTH, LfoWave=SAMPLE_HOLD, LfoFilter=1.85, LfoPitch=1.6, PitchTarget=OSC_2, Level=0.86,
    )),
    ("Orbit Pulse", dict(
        Wave1=PULSE, Pulse=0.48, Level1=0.62, Wave2=PULSE, Pulse2=0.25,
        Range2=OCTAVE_DOWN, Tune2=-0.04, Level2=0.42,
        Filter=NOTCH, Cutoff=1250, Resonance=0.70, ContourAmount=0.27,
        AmpAttack=0.18, AmpDecay=1.3, AmpSustain=0.82, AmpRelease=0.69,
        Sync=TWO_BARS, LfoWave=SMOOTH_RANDOM, PWM=0.42, PwmTarget=OSC_2,
        LfoFilter=0.94, LfoAmp=0.3, Level=0.76,
    )),
    ("Sideways Gate", dict(
        Wave1=SAW, Level1=0.78, Wave2=SAW, Range2=OCTAVE_UP, Tune2=0.12, Level2=0.27,
        Filter=HP12, Cutoff=530, Resonance=0.43, KeyTrack=0.50, ContourAmount=-0.26,
        AmpAttack=0.019, AmpDecay=0.57, AmpSustain=0.8, AmpRelease=0.17,
        Sync=EIGHTH_TRIPLET, LfoWave=RAMP, LfoFilter=1.23, LfoAmp=0.68, Retrigger=1, Level=0.80,
    )),

    # Cross-modulated and hard-sync timbres.
    ("Bronze Droplets", dict(
        Wave1=PULSE, Pulse=0.42, Level1=0.73, Wave2=SAW, Range2=OCTAVE_UP,
        Tune2=19.02, Level2=0.07, CrossMod=0.46, Filter=BP12, Cutoff=2330,
        Resonance=0.60, KeyTrack=0.67, Drive=0.10, ContourAmount=-0.77,
        FilterAttack=0.005, FilterDecay=0.17, FilterSustain=0.25,
        AmpAttack=0.0025, AmpDecay=0.76, AmpSustain=0.10, AmpRelease=0.42, Level=0.91,
    )),
    ("Porcelain Wire", dict(
        Wave1=SAW, Level1=0.65, Wave2=PULSE, Pulse2=0.17, Range2=OCTAVE_UP, Tune2=5.38,
        Level2=0.18, CrossMod=0.57, Osc2Contour=-8.5, Filter=HP12, Cutoff=750,
        Resonance=0.37, KeyTrack=0.61, ContourAmount=0.95, FilterAttack=0.014,
        FilterDecay=0.27, FilterSustain=0.10, AmpAttack=0.048, AmpDecay=0.28,
        AmpSustain=0.37, AmpRelease=0.22, Level=0.79,
    )),
    ("Magnetic Tines", dict(
        Wave1=PULSE, Pulse=0.49, Level1=0.71, Wave2=SAW, Range2=OCTAVE_UP,
        Tune2=12.02, Level2=0.02, CrossMod=0.25, Osc2Contour=2.9,
        Filter=LP12, Cutoff=1020, Resonance=0.34, KeyTrack=0.86, Drive=0.08,
        ContourAmount=1.2, FilterAttack=0.003, FilterDecay=0.093, FilterSustain=0.13,
        AmpAttack=0.0035, AmpDecay=1.3, AmpSustain=0.17, AmpRelease=0.31,
        LfoWave=SINE, Rate=4.8, LfoAmp=0.25, Level=0.80,
    )),
    ("Sync Prism", dict(
        Wave1=PULSE, Pulse=0.49, Level1=0.25, Wave2=SAW_PULSE, Pulse2=0.24,
        Range2=OCTAVE_UP, Tune2=4.2, Level2=0.71, HardSync=1, Osc2Contour=15.0,
        Filter=SEM, Morph=0.68, Cutoff=1480, Resonance=0.31, ContourAmount=-0.93,
        FilterAttack=0.38, FilterDecay=0.77, FilterSustain=0.28,
        AmpAttack=0.13, AmpDecay=1.0, AmpSustain=0.72, AmpRelease=0.67,
        LfoWave=SINE, Rate=0.37, LfoPitch=1.6, PitchTarget=OSC_2, Level=0.79,
    )),

    # Short tuned percussion and noise.
    ("Tuned Wood", dict(
        Wave1=PULSE, Pulse=0.38, Level1=0.79, Wave2=OFF, Level2=0, Noise=0.16,
        Filter=BP12, Cutoff=460, Resonance=0.83, KeyTrack=0.99, Drive=0.06,
        ContourAmount=0.32, FilterAttack=0.001, FilterDecay=0.044, FilterSustain=0.0,
        AmpAttack=0.001, AmpDecay=0.14, AmpSustain=0.0, AmpRelease=0.075, Level=0.87,
    )),
    ("Rubber Tom", dict(
        Wave1=OFF, Level1=0, Wave2=PULSE, Pulse2=0.49, Range2=OCTAVE_DOWN,
        Tune2=0, Level2=0.82, Osc2Contour=14.0, Noise=0.08,
        Filter=LP12, Cutoff=380, Resonance=0.64, KeyTrack=0.72, ContourAmount=1.30,
        FilterAttack=0.0015, FilterDecay=0.060, FilterSustain=0.0,
        AmpAttack=0.0013, AmpDecay=0.25, AmpSustain=0.0, AmpRelease=0.11, Level=0.80,
    )),
    ("Clock Pluck", dict(
        Wave1=PULSE, Pulse=0.10, Level1=0.69, Wave2=SAW, Range2=OCTAVE_UP,
        Tune2=7.0, Level2=0.22, Filter=NOTCH, Cutoff=1260, Resonance=0.77, KeyTrack=0.8,
        ContourAmount=-2.7, FilterAttack=0.0016, FilterDecay=0.095, FilterSustain=0.0,
        AmpAttack=0.0008, AmpDecay=0.11, AmpSustain=0.0, AmpRelease=0.055, Level=0.75,
    )),
    ("Dust Snip", dict(
        Wave1=OFF, Wave2=OFF, Level1=0, Level2=0, Noise=0.76,
        Filter=HP12, Cutoff=2170, Resonance=0.53, KeyTrack=0.25, Drive=0.26,
        ContourAmount=2.8, FilterAttack=0.0008, FilterDecay=0.047, FilterSustain=0.0,
        AmpAttack=0.0008, AmpDecay=0.084, AmpSustain=0.0, AmpRelease=0.057, Level=0.82,
    )),

    # Mono legato leads with distinct glide speeds and filter movement.
    ("Slow Copper", dict(
        Wave1=SAW, Level1=0.68, Wave2=PULSE, Pulse2=0.38, Tune2=-0.08, Level2=0.38,
        Filter=SEM, Morph=0.18, Cutoff=890, Resonance=0.36, KeyTrack=0.55, Drive=0.19,
        ContourAmount=1.7, FilterAttack=0.31, FilterDecay=0.49, FilterSustain=0.49,
        AmpAttack=0.043, AmpDecay=0.68, AmpSustain=0.86, AmpRelease=0.40,
        LfoWave=SINE, Rate=5.2, LfoPitch=0.23, PitchTarget=OSC_2, Mode=1, Glide=0.28, Level=0.84,
    )),
    ("Velcro Slide", dict(
        Wave1=SAW_PULSE, Pulse=0.23, Level1=0.79, Wave2=SAW, Range2=OCTAVE_DOWN, Tune2=0,
        Level2=0.39, Filter=LADDER, Cutoff=970, Resonance=0.74, KeyTrack=0.64, Drive=0.49,
        ContourAmount=-2.0, FilterAttack=0.016, FilterDecay=0.26, FilterSustain=0.24,
        AmpAttack=0.005, AmpDecay=0.57, AmpSustain=0.91, AmpRelease=0.12,
        Mode=1, Glide=0.050, Level=0.76,
    )),
    ("Ion Ribbon", dict(
        Wave1=PULSE, Pulse=0.45, Level1=0.33, Wave2=SAW, Range2=OCTAVE_UP,
        Tune2=7.0, Level2=0.69, HardSync=1, Osc2Contour=10.8,
        Filter=HP12, Cutoff=760, Resonance=0.39, KeyTrack=0.48, ContourAmount=0.75,
        FilterAttack=0.008, FilterDecay=0.31, FilterSustain=0.18,
        AmpAttack=0.017, AmpDecay=0.39, AmpSustain=0.82, AmpRelease=0.25,
        LfoWave=TRIANGLE, Rate=4.2, LfoPitch=0.41, PitchTarget=OSC_2, Mode=1, Glide=0.12, Level=0.79,
    )),

    # Sustained organ, reed, and bowed patches.
    ("Pulse Chapel", dict(
        Wave1=PULSE, Pulse=0.49, Level1=0.63, Wave2=PULSE, Pulse2=0.31,
        Range2=OCTAVE_UP, Tune2=0.01, Level2=0.38, Filter=LP12,
        Cutoff=3090, Resonance=0.21, KeyTrack=0.63, Drive=0.04, ContourAmount=0.21,
        AmpAttack=0.009, AmpDecay=0.13, AmpSustain=0.96, AmpRelease=0.064,
        LfoWave=SINE, Rate=6.4, LfoPitch=0.09, LfoAmp=0.24, Level=0.74,
    )),
    ("Double Reed", dict(
        Wave1=PULSE, Pulse=0.16, Level1=0.72, Wave2=SAW, Tune2=-0.11, Level2=0.27, Noise=0.045,
        Filter=BP12, Cutoff=820, Resonance=0.58, KeyTrack=0.76, ContourAmount=0.53,
        FilterAttack=0.10, FilterDecay=0.38, FilterSustain=0.67, AmpAttack=0.069,
        AmpDecay=0.27, AmpSustain=0.89, AmpRelease=0.20,
        LfoWave=SINE, Rate=4.1, LfoFilter=0.19, LfoPitch=0.11, Level=0.88,
    )),
    ("Bowed Glass", dict(
        Wave1=SAW, Level1=0.58, Wave2=SAW_PULSE, Pulse2=0.29, Tune2=-0.19, Level2=0.39, Noise=0.075,
        Filter=LP12, Cutoff=880, Resonance=0.53, KeyTrack=0.58, ContourAmount=1.61,
        FilterAttack=0.56, FilterDecay=0.95, FilterSustain=0.58, FilterRelease=0.46,
        AmpAttack=0.48, AmpDecay=1.2, AmpSustain=0.85, AmpRelease=0.57,
        LfoWave=SMOOTH_RANDOM, Rate=0.66, LfoPitch=0.16, PitchTarget=OSC_2, Level=0.78,
    )),
)
