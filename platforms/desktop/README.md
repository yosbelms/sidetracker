# SideTracker Desktop

The desktop build compiles the Tracker+ variant of the application core
with the host compiler. It calls the real Tracker startup, update, and audio
entry points. The SDL front panel sends button, pad, and encoder events to CE,
which owns the screens, soft-key actions, editing, and sequencer.

## Build and run

Use GNU Make, a C++20 compiler, and SDL2 discoverable through pkg-config.

```sh
make -j8 desktop
./build/desktop/sidetracker-desktop
```

The default virtual card is in SDL's `TrackerCE/Desktop/SD` preferences
directory. On first launch, Desktop moves an existing `Simulator/SD` card
there if the new card folder does not exist. Pass `--sd /path/to/card-copy`
to open a different card.
The firmware writes projects, its workspace, and settings there. Pass
`--no-audio` to disable the host audio output. The render scale defaults to 3×. Use `--scale 1` or `--scale 2` to lower it.
The window remains resizable. At 3×, headless screenshots are 2400 × 1440
for the display or 3900 × 2790 with the panel. The firmware still lays out its
UI in 800 × 480 coordinates and uses the original bitmap fonts.

Hold **Instrument** and select one of the first three entries for MINI, OBER,
or CHIP. Open **I. Params** to change the model, load a preset, or edit a patch.
With both donor source folders under `temp/`, `make desktop` generates the full
MINI and OBER presets as individual `.patch` files in
`build/SD/Patches/`. The simulator opens them automatically.
Files in the virtual SD card's `Patches/MINI/` and `Patches/OBER/` directories
take precedence. Copy the generated `Patches/` directory to the SD root to use
the presets on Tracker+ or Mini hardware.

To build the Original Tracker simulator explicitly, run
`make desktop DESKTOP_MODEL=original` and launch
`./build/desktop-original/sidetracker-desktop-original`.
It uses a separate card at `TrackerCE/DesktopOriginal/SD`.

| Input | CE control |
| --- | --- |
| F1–F8 | Context-sensitive screen keys |
| F9 / F10 | Pattern / Song |
| Arrows, Shift, Space, Insert, Delete | Navigation, Shift, Play, Insert, Delete |
| R / C / P | Rec / Copy / Pattern |
| N / I / X / V | Note / Instrument / FX 1 / FX 2 |
| 1–0 in Command mode | Module buttons, in panel order |
| Tab or Caps Lock | Toggle Command and Pad modes |
| Letter and digit rows in Pad mode | Pads, following OpenTracker's key map |
| Page Up / Page Down or mouse wheel | Jog wheel |
| Left-click and hold a panel control | Press until release |
| Right-click Shift | Latch or release Shift |
| Esc | Close the desktop app |

The bottom-right two pads are mouse-only. Click either side of the jog wheel
to turn it by one detent.

## Headless check

```sh
make desktop-check
```

The check captures Pattern, Config, and Song in `build/desktop` and exercises
the Sample Editor Effects workflow. It also opens the Instrument picker, plays
all three synths, and checks that a native patch can be saved and
reloaded. A single capture can be made
with `--sd DIR --screenshot FILE.bmp --tap-button Config`. Add `--panel` for
the full front panel.

## Current scope

The display adapter implements the common FT812 drawing commands, custom
font bitmaps, rotated text, and cached display fragments. Rasterization is approximate.
JPEG, FT812 screenshot commands, and some graphics state and bitmap transforms
are incomplete. Desktop audio uses the real CE mixer and sequencer with SDL
stereo output. Physical line-in, radio, USB, MIDI ports, low-level SD sector
access, and hardware firmware updates are unimplemented. Polyend supplies the
six factory synth engines as ARM libraries, so the desktop model picker offers
the native MINI, OBER, and CHIP engines. A project that uses a factory model
loads the desktop default for that slot. Desktop saves synth patches in the
Tracker `.patch` format under the synth's named folder.

PTFX files contain Cortex-M ARM code. Desktop supports discovery, adding effects/generators to the chain, parameter editing (including list choices), and chain saving/loading without executing the plugin. Preview/render of an active PTFX stage displays `PTFX: Tracker only`; use Tracker hardware to process it. Bypassed PTFX stages do not prevent desktop preview of the remaining built-in chain.
