# Perf FX

Hold a pad to apply its row preset to the full mix. Up to three columns sound in press order. Press Perform to switch between the audio and MIDI track mute banks. Delete releases the live punches. Scatter uses four fixed slice patterns.

On **Master → Global Mixer 3/3**, hold **F5** and turn the jog wheel or use the arrows to change **Perf FX Mix**. Delete while holding F5 resets it to 100%. Mix is a global setting. The stereo node runs before the master limiter, with a four-second rolling history and a maximum of two seconds per captured segment. Plus and Mini reserve 752 KiB from the sample pool. Original uses available external RAM.

The DSP is adapted from Vincent Fillion’s Punch-In FX under the [MIT license](LICENSE).

## Validation

```sh
make -C tests test
make -C tests STEREO=1 test
make desktop
python3 tools/desktop_perf_fx_check.py build/desktop/tracker-ce-desktop
make all
```

For hardware timing, enable developer mode with an empty `/.develop` file on the SD card. In Config, Play resets the audio timing peaks. Run a representative project with all eight sample voices, delay and reverb active. On Plus and Mini include synth and USB audio load. In Performance, hold three modulation or pitch presets together, then exercise capture presets, fast row changes, and a fourth waiting column. Return to Config and read the Perf FX peak in microseconds and the total callback maximum percentage. Repeat on each model and check for dropouts. The acceptance limits are 1,000 µs for Perf FX and 70% total callback usage.
