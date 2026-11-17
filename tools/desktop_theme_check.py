#!/usr/bin/env python3
"""Apply custom themes through Config and check the resulting display."""

from pathlib import Path
import json
import shutil
import struct
import sys
import tempfile
import wave

from desktop_perf_fx_check import run

ROOT = Path(__file__).resolve().parent.parent
OPEN_THEME = "button Config\nbutton Down\nbutton Right\nbutton Down\nbutton Down\nbutton Right\n"


def pixel(bitmap, x, y):
    offset, = struct.unpack_from("<I", bitmap, 10)
    width, height = struct.unpack_from("<ii", bitmap, 18)
    depth, = struct.unpack_from("<H", bitmap, 28)
    pixel_bytes = depth // 8
    stride = (width * pixel_bytes + 3) & ~3
    row = height - 1 - y if height > 0 else y
    start = offset + row * stride + x * pixel_bytes
    return int.from_bytes(bitmap[start:start + 3], "little")


def check_win98_waveform(binary, card):
    with wave.open(str(card / "waveform.wav"), "wb") as sample:
        sample.setparams((1, 2, 44100, 0, "NONE", "not compressed"))
        sample.writeframes(struct.pack("<hh", -16000, 16000) * 22050)

    playback = card / "playback.bmp"
    run(binary, card, f"""import 0 /waveform.wav
button S. Playback
wait 300
snapshot {playback}
button S. Editor
wait 300
""")
    for view, path in (("Sample Editor", card / "screen.bmp"), ("Sample Playback", playback)):
        bmp = path.read_bytes()
        # The selection adds its tint to the waveform. Sample a row away from
        # the center line to distinguish an actual waveform from a flat line.
        assert sum(pixel(bmp, x, 200) == 0x1828BC for x in range(50, 550)) > 100, view


def check_win98_toolbar(binary, card):
    theme_file = card / "Themes" / "Win98.json"
    theme = json.loads(theme_file.read_text())
    # Give the screen and the toolbar different colors so gaps are measurable.
    theme["background"] = "#008684"
    theme_file.write_text(json.dumps(theme))

    instrument = card / "instrument.bmp"
    config = card / "config.bmp"
    run(binary, card, f"""button I. Params
wait 300
snapshot {instrument}
button Config
wait 300
snapshot {config}
button S. Playback
wait 300
""")
    for view, path in (
        ("Instrument Parameters", instrument),
        ("Config", config),
        ("Sample Playback", card / "screen.bmp"),
    ):
        bmp = path.read_bytes()
        assert pixel(bmp, 0, 70) == 0x008684, f"{view} teal page"
        for x, y in ((0, 424), (0, 479), (799, 424), (799, 479), (30, 426), (790, 426)):
            assert pixel(bmp, x, y) == 0xC0C0C0, f"{view} toolbar at ({x}, {y})"


def check(binary, card, name):
    (card / "Themes").mkdir(parents=True)
    shutil.copy(ROOT / "themes" / f"{name}.json", card / "Themes")
    script = OPEN_THEME + "button Down\n" * 3 + "button 7\n"
    (card / "Fonts").mkdir()
    shutil.copy(ROOT / "fonts" / f"{name}.ptfont", card / "Fonts")
    script += "button Down\nbutton Right\nbutton Down\nbutton Down\nbutton 7\n"
    run(binary, card, script + "button Pattern\nwait 300\n")
    themed = (card / "screen.bmp").read_bytes()

    if name == "Win98":
        assert pixel(themed, 6, 4) != pixel(themed, 796, 4), "title gradient"
        assert pixel(themed, 796, 4) == 0x1084D0, "98.css gradient end"
        assert pixel(themed, 2, 437) == 0xFFFFFF, "raised button outer highlight"
        assert pixel(themed, 3, 437) == 0xDFDFDF, "raised button inner highlight"
        assert pixel(themed, 97, 437) == 0x0A0A0A, "raised button outer shadow"
    else:
        assert pixel(themed, 6, 4) == pixel(themed, 796, 4) == 0x3E2B68

    # Reopen the same card and boot with the saved theme and font.
    run(binary, card, "wait 300\n")
    assert (card / "screen.bmp").read_bytes() == themed, f"{name} reboot"
    if name == "Win98":
        check_win98_waveform(binary, card)
        check_win98_toolbar(binary, card)
    run(binary, card, OPEN_THEME + "button Up\n" * 3 + "button 7\nbutton Pattern\nwait 300\n")
    assert pixel((card / "screen.bmp").read_bytes(), 796, 4) == 0x080808, "return to Original"


if __name__ == "__main__":
    with tempfile.TemporaryDirectory(prefix="st-themes-") as directory:
        for theme in ("Win98", "Snes"):
            check(Path(sys.argv[1]).resolve(), Path(directory) / theme, theme)
    print("Desktop themes passed")
