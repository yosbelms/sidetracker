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


def config_groups(data):
    groups = []
    offset = 12
    for _ in range(9):
        size, = struct.unpack_from("<H", data, offset)
        groups.append((offset, size))
        offset += size
    assert offset == len(data)
    return groups


def check_config_round_trip(binary, card):
    config = card / ".desktop-config"
    mix_file = card / ".tracker-ce-perf-mix"
    data = bytearray(config.read_bytes())
    groups = config_groups(data)
    firmware, _ = groups[1]
    values, size = groups[3]
    general, _ = groups[4]
    common, common_size = groups[-1]
    assert data[7] == data[firmware + 6] == 10 and common_size == 4
    assert mix_file.read_bytes() == b"\x40"

    # Populate the legacy Performance fields as an older firmware would.
    # The brightness bytes preceding the payload belong to the current UI.
    struct.pack_into("<8H", data, values + 6, 31, 127, 255, 1, 5, 16, 42, 2)
    data[values + 22:values + 34] = bytes(reversed(range(1, 13)))
    struct.pack_into("<48h", data, values + 34, *([0, -90, 127, 200] * 12))
    data[values + 130:values + 142] = bytes([0, 1, 2, 3] * 3)
    data[values + 142:values + 150] = bytes([1, 0] * 4)
    data[general + 7] = 1
    # Changing the previous firmware patch triggers a normal shared config save.
    data[firmware + 4] ^= 1
    config.write_bytes(data)
    mix_file.unlink()
    run(binary, card, "assert-perf-mix 100\n")
    saved = bytearray(config.read_bytes())
    assert config_groups(saved) == groups
    assert saved[values + 6:values + size] == data[values + 6:values + size]
    assert saved[general + 7] == data[general + 7]

    # Reproduce the interim v11 image, then verify migration restores v10 bytes.
    inline = bytearray(saved)
    inline[7] = inline[firmware + 6] = 11
    struct.pack_into("<H", inline, common, 6)
    inline.extend((37, 0))
    config.write_bytes(inline)
    run(binary, card, "assert-perf-mix 37\n")
    assert config.read_bytes() == saved
    assert mix_file.read_bytes() == bytes([37])

    # Older firmware can replace the shared config without losing the CE mix.
    config.write_bytes(saved)
    run(binary, card, "assert-perf-mix 37\n")


def check(binary):
    with tempfile.TemporaryDirectory(prefix="ce-punch-fx-") as directory:
        card = Path(directory)
        audit = card / "frames.tsv"
        run(binary, card, f"""audit-frames {audit}
button Perform
assert-view Perform
assert-perf-mix 100
assert-punch 3 -1
assert-punch 4 -1
assert-punch 5 -1
assert-punch 6 -1
assert-punch 7 -1
assert-punch 8 -1
assert-punch 11 -1
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
            center = 42 + column * 65
            draws = [(int(x), int(y), text) for x, y, text in text_draws
                     if abs(int(x) - center) < 32 and 270 <= int(y) < 423]
            rows = {text for x, _, text in draws if x == center}
            for value in expected:
                if value in rows:
                    continue
                whole, point, fraction = value.partition(".")
                assert point and any(
                    left == whole + point and right == fraction and ly == ry and lx < rx
                    for (lx, ly, left), (rx, ry, right) in zip(draws, draws[1:])
                ), (column, value, rows)
        assert any(x == "350" and text == "Line In" for x, _, text in text_draws)
        assert any(x == "450" and text == "Perf Mix" for x, _, text in text_draws)
        assert any(x == "450" and text == "64%" for x, _, text in text_draws)
        run(binary, card, "assert-perf-mix 64\n")
        check_config_round_trip(binary, card)
        config_audit = card / "config-frames.tsv"
        run(binary, card, f"audit-frames {config_audit}\nbutton Config\nwait 100\n")
        config_frames = config_audit.read_text()
        assert "Config" in config_frames and "Project Settings" in config_frames
    print("PunchFX grid, pad behavior, mute banks, Master Mix, Config screen, and configuration migration passed.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("binary", type=Path)
    check(parser.parse_args().binary.resolve())
