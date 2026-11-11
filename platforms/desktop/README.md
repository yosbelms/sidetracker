# SideTracker Desktop

The desktop build compiles the Original Tracker variant of the CE application core
with the host compiler. It calls the real Tracker startup, update, and audio
entry points. The SDL front panel sends button, pad, and encoder events to CE,
which owns the screens, soft-key actions, editing, and sequencer.

## Build and run

Use GNU Make, a C++20 compiler, and SDL2 discoverable through pkg-config.

```sh
make -j8 desktop
./build/desktop/tracker-ce-desktop
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

The check boots the firmware on a temporary card and captures Pattern, Config,
and Song in `build/desktop`. Config and Song are opened by sending firmware
button presses through the same input adapter. A single capture can be made
with `--sd DIR --screenshot FILE.bmp --tap-button Config`. Add `--panel` for
the full front panel.

## Current scope

The display adapter implements the common FT812 drawing commands, custom
font bitmaps, rotated text, and cached display fragments. Rasterization is approximate.
JPEG, FT812 screenshot commands, and some graphics state and bitmap transforms
are incomplete. Desktop audio uses the real CE mixer and sequencer with SDL
stereo output. Physical line-in, radio, USB, MIDI ports, low-level SD sector
access, and hardware firmware updates are unimplemented. This target uses
the Original Tracker feature set, so Tracker+ synths are outside its scope.

PTFX files contain Cortex-M ARM code. Desktop supports discovery, adding effects/generators to the chain, parameter editing (including list choices), and chain saving/loading without executing the plugin. Preview/render of an active PTFX stage displays `PTFX: Tracker only`; use Tracker hardware to process it. Bypassed PTFX stages do not prevent desktop preview of the remaining built-in chain.
