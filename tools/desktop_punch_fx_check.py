#!/usr/bin/env python3
"""Exercise the Performance pads, track banks, and Master mix controls."""
import argparse
from pathlib import Path
import struct
import subprocess
import tempfile


def run(binary, card, commands):
    script = card / "test.txt"
    script.write_text(commands)
    result = subprocess.run(
        [str(binary), "--sd", str(card), "--scale", "1", "--script", str(script),
         "--screenshot", str(card / "screen.bmp")],
        capture_output=True, text=True, timeout=90,
    )
    if result.returncode:
        raise RuntimeError(result.stdout + result.stderr)


def make_old_config(path):
    # The last sized group is common. Version 10 stored two uint8_t values
    # there, after its uint16_t size. Remove the v11 mix field and padding.
    data = bytearray(path.read_bytes())
    total, = struct.unpack_from("<H", data, 8)
    offset = 12
    for _ in range(8):
        size, = struct.unpack_from("<H", data, offset)
        offset += size
    size, = struct.unpack_from("<H", data, offset)
    assert size == 6
    data[7] = 10
    struct.pack_into("<H", data, 8, total - 2)
    struct.pack_into("<H", data, offset, 4)
    del data[offset + 4:offset + 6]
    path.write_bytes(data)


def check(binary):
    with tempfile.TemporaryDirectory(prefix="ce-punch-fx-") as directory:
        card = Path(directory)
        audit = card / "frames.tsv"
        run(binary, card, f"""audit-frames {audit}
button Perform
assert-view Perform
assert-perf-mix 100
press pad 0
assert-punch 0 0
press pad 36
assert-punch 0 3
release pad 36
assert-punch 0 0
press pad 1
press pad 2
press pad 3
assert-punch 0 0
assert-punch 1 0
assert-punch 2 0
assert-punch 3 -1
release pad 1
assert-punch 1 -1
assert-punch 3 0
button Delete
assert-punch 0 -1
assert-punch 2 -1
assert-punch 3 -1
release pad 0
release pad 2
release pad 3
press pad 7
assert-punch 7 0
press pad 43
assert-punch 7 3
release pad 43
assert-punch 7 0
release pad 7
assert-punch 7 -1
button 0
assert-muted 0 1
button Perform
button 0
assert-muted 8 1
button 0
assert-muted 8 0
button Perform
button 0
assert-muted 0 0
button Play
assert-sequencer playing
press pad 6
assert-punch 6 0
button Play
assert-sequencer stopped
assert-punch 6 -1
release pad 6
press pad 11
assert-punch 11 0
button Master
assert-view Master
assert-punch 11 -1
release pad 11
button Master
button Master
encoder -12
assert-perf-mix 100
press 4
encoder -36
assert-perf-mix 64
button Right
button Down
assert-perf-mix 64
button Delete
assert-perf-mix 100
encoder -36
release 4
assert-perf-mix 64
wait 5100
""")
        # Effect names use rotated glyphs, which are absent from the text
        # audit. Check the four visible value labels in all twelve columns.
        frames = audit.read_text()
        assert "Performance FX" in frames
        assert "Global Mixer 3/3" in frames
        text_draws = [line.split("\t", 2) for line in frames.splitlines() if "\t" in line]
        values = [
            ("1/8", "1/16", "1/32", "1/64"),
            ("1/8T", "1/16T", "1/32T", "1/64T"),
            ("20%", "47%", "73%", "100%"),
            ("1/4", "1/8", "1/16", "1/32"),
            ("+3 st", "+5 st", "+7 st", "+12 st"),
            ("-3 st", "-5 st", "-7 st", "-12 st"),
            ("80%", "60%", "40%", "20%"),
            ("4kHz", "2kHz", "800Hz", "250Hz"),
            ("1x/2", "2x/4", "3.5x/6", "5x/8"),
            ("25%", "50%", "75%", "100%"),
            ("4cuts", "5cuts", "6cuts*", "8cuts*"),
            ("1x", "1.1x", "1.2x", "1.3x"),
        ]
        for column, expected in enumerate(values):
            x = str(42 + column * 65)
            rows = {text for px, y, text in text_draws if px == x and 270 <= int(y) < 423}
            assert set(expected) <= rows, (column, rows)
        assert any(x == "350" and text == "Line In" for x, _, text in text_draws)
        assert any(x == "450" and text == "Perf Mix" for x, _, text in text_draws)
        assert any(x == "450" and text == "64%" for x, _, text in text_draws)
        run(binary, card, "assert-perf-mix 64\n")
        make_old_config(card / ".desktop-config")
        run(binary, card, "assert-perf-mix 100\n")
    print("PunchFX grid, pad behavior, mute banks, Master Mix, and configuration migration passed.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("binary", type=Path)
    check(parser.parse_args().binary.resolve())
