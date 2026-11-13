#!/usr/bin/env python3
"""Exercise the Performance pads, track banks, and Master mix controls."""
import argparse
import math
from pathlib import Path
import struct
import subprocess
import tempfile
import wave
import zlib


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
    run(binary, card, "assert-perf-fx-mix 100\n")
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
    run(binary, card, "assert-perf-fx-mix 37\n")
    assert config.read_bytes() == saved
    assert mix_file.read_bytes() == bytes([37])

    # Older firmware can replace the shared config without losing the CE mix.
    config.write_bytes(saved)
    run(binary, card, "assert-perf-fx-mix 37\n")


def check_mix_save_during_playback(binary, card):
    mix_file = card / ".tracker-ce-perf-mix"
    edit_while_playing = """button Master
button Master
button Master
button Play
assert-sequencer playing
press 4
encoder 12
release 4
assert-perf-fx-mix 49
wait 5100
assert-sequencer playing
"""
    run(binary, card, edit_while_playing)
    assert mix_file.read_bytes() == bytes([37])

    run(binary, card, edit_while_playing + """button Play
assert-sequencer stopped
wait 100
""")
    assert mix_file.read_bytes() == bytes([49])


def read_wave(path):
    with wave.open(str(path), "rb") as audio:
        return audio.getparams(), audio.readframes(audio.getnframes())


def check_pattern_fx(binary):
    with tempfile.TemporaryDirectory(prefix="st-pattern-perf-fx-") as directory:
        card = Path(directory)
        audit = card / "frames.tsv"
        rate = 44100
        samples = [int(5000 * math.sin(i * math.tau * 3800 / rate) +
                       4000 * math.sin(i * math.tau * 257 / rate)) for i in range(rate * 5)]
        with wave.open(str(card / "input.wav"), "wb") as audio:
            audio.setparams((1, 2, rate, len(samples), "NONE", "not compressed"))
            audio.writeframes(struct.pack(f"<{len(samples)}h", *samples))
        run(binary, card, f"""audit-frames {audit}
import 0 /input.wav
button Pattern
button Rec
press FX 1
button pad 45
release FX 1
button pad 32
button Up
button pad 40
button Up
button Down
wait 12000
""")
        assert "Low Pass / 800Hz" in audit.read_text()
        assert "Cassette / Chewed" in audit.read_text()
        path = card / "Workspace/patterns/pattern_01.mtp"
        saved = path.read_bytes()
        assert len(saved) == 16432 and saved[8] == 6

        # Version 6 has a 28-byte header and 16 tracks of 128 eight-byte steps.
        # Each track starts with its length, and the two FX slots are stored FX2 first.
        def step(data, track, row, note=-1, fx1=(0, 0), fx2=(0, 0)):
            struct.pack_into("<bBBBBBbb", data, 29 + track * 1025 + row * 8,
                             note, 0, *fx2, *fx1, -1, -1)

        assert saved[33:35] == bytes((51, 1))  # XBR1, selected by type pad.
        assert saved[41:43] == bytes((51, 20))  # XCA4, selected by value pad.

        def write_pattern(data, target=path):
            struct.pack_into("<I", data, len(data) - 4, zlib.crc32(data[:-4]))
            target.write_bytes(data)

        dry = bytearray(saved)
        step(dry, 0, 0, note=60)
        step(dry, 0, 1)
        wet = bytearray(dry)
        step(wet, 0, 0, 60, (51, 35), (51, 14))  # LP3 and DE2 on the first row.
        step(wet, 0, 2, fx1=(51, 5), fx2=(51, 45))  # Cutter and waiting Unison.
        step(wet, 0, 4, fx1=(51, 57))  # LP-
        step(wet, 0, 8, fx1=(51, 0))   # All Off
        step(wet, 1, 1, fx1=(51, 21), fx2=(4, 0))  # Reverse with zero Chance.
        step(wet, 2, 1, fx1=(51, 37))  # High Pass on a muted track.
        write_pattern(wet)
        run(binary, card, """button Perform
button 2
assert-muted 2 1
button Pattern
press 4
assert-perf-fx 8 2
assert-perf-fx 3 1
release 4
assert-sequencer stopped
assert-perf-fx 8 -1
button Play
assert-perf-fx 8 2
assert-perf-fx 3 1
assert-perf-fx 5 -1
assert-perf-fx 9 -1
wait 170
assert-perf-fx 1 0
assert-perf-fx 11 -1
wait 300
assert-perf-fx 8 -1
assert-perf-fx 3 1
assert-perf-fx 11 0
wait 560
assert-perf-fx 3 -1
assert-perf-fx 1 -1
assert-perf-fx 11 -1
button Play
assert-sequencer stopped
""")

        def render_stems():
            run(binary, card, """button File
button 5
button 3
wait 7000
""")
            master, = (card / "Export").rglob("master.wav")
            return {file.name: read_wave(file) for file in master.parent.glob("*.wav")}

        processed = render_stems()
        (card / "Export").rename(card / "processed")
        write_pattern(dry)
        baseline = render_stems()
        assert processed.keys() == baseline.keys()
        for name in baseline.keys() - {"master.wav"}:
            assert processed[name] == baseline[name], name
        before, dry_pcm = baseline["master.wav"]
        after, wet_pcm = processed["master.wav"]
        assert before == after
        frame_bytes = before.nchannels * before.sampwidth
        assert wet_pcm[:rate * frame_bytes // 10] != dry_pcm[:rate * frame_bytes // 10]
        assert wet_pcm[rate * frame_bytes * 2:] == dry_pcm[rate * frame_bytes * 2:]

        # Exercise the master-only writer for each mode as well as the stems writer.
        (card / "Export").rename(card / "baseline")
        exports = {
            "pattern": "button File\nbutton 5\nbutton 2\n",
            "song": "button File\nbutton 5\nbutton 0\n",
            "selection": "button Pattern\nbutton Rec\nbutton 7\nbutton 0\nbutton 7\n",
        }
        for mode, commands in exports.items():
            results = []
            for name, pattern in (("wet", wet), ("dry", dry)):
                write_pattern(pattern)
                run(binary, card, commands + "wait 7000\n")
                exported, = (card / "Export").rglob("*.wav")
                results.append(read_wave(exported))
                (card / "Export").rename(card / f"{mode}-{name}")
            (wet_params, wet_pcm), (dry_params, dry_pcm) = results
            assert wet_params == dry_params, mode
            assert wet_pcm[:rate * frame_bytes // 10] != dry_pcm[:rate * frame_bytes // 10], mode

        cassette = bytearray(dry)
        step(cassette, 0, 0, note=60, fx1=(51, 20))  # CA4
        step(cassette, 0, 4, fx1=(51, 53))  # CA-
        write_pattern(cassette)
        run(binary, card, """button Play
assert-perf-fx 4 3
wait 620
assert-perf-fx 4 -1
button Play
assert-sequencer stopped
""")

        # A blank second pattern clears the latch. A loop of the first pattern holds it.
        loop = bytearray(dry)
        for track in range(16):
            loop[28 + track * 1025] = 3
        step(loop, 0, 1, fx1=(51, 35))
        empty = bytearray(loop)
        step(empty, 0, 0)
        step(empty, 0, 1)
        write_pattern(loop)
        write_pattern(empty, path.with_name("pattern_02.mtp"))
        run(binary, card, """button Play
wait 430
assert-perf-fx 8 2
press 0
button Right
release 0
wait 700
assert-sequencer playing
assert-perf-fx 8 -1
button Play
assert-sequencer stopped
""")


def check(binary):
    with tempfile.TemporaryDirectory(prefix="ce-perf-fx-") as directory:
        card = Path(directory)
        audit = card / "frames.tsv"
        grid = card / "performance-grid.bmp"
        run(binary, card, f"""audit-frames {audit}
button Perform
assert-view Perform
snapshot {grid}
assert-perf-fx-mix 100
assert-perf-fx 3 -1
assert-perf-fx 4 -1
assert-perf-fx 5 -1
assert-perf-fx 6 -1
assert-perf-fx 7 -1
assert-perf-fx 8 -1
assert-perf-fx 11 -1
press pad 0
assert-perf-fx 0 0
press pad 36
assert-perf-fx 0 3
release pad 36
assert-perf-fx 0 0
press pad 1
press pad 2
press pad 3
assert-perf-fx 0 0
assert-perf-fx 1 0
assert-perf-fx 2 0
assert-perf-fx 3 -1
release pad 1
assert-perf-fx 1 -1
assert-perf-fx 3 0
button Delete
assert-perf-fx 0 -1
assert-perf-fx 2 -1
assert-perf-fx 3 -1
release pad 0
release pad 2
release pad 3
press pad 4
assert-perf-fx 4 0
press pad 40
assert-perf-fx 4 3
release pad 40
assert-perf-fx 4 0
release pad 4
assert-perf-fx 4 -1
press pad 7
assert-perf-fx 7 0
press pad 43
assert-perf-fx 7 3
release pad 43
assert-perf-fx 7 0
release pad 7
assert-perf-fx 7 -1
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
assert-perf-fx 6 0
button Play
assert-sequencer stopped
assert-perf-fx 6 -1
release pad 6
press pad 11
assert-perf-fx 11 0
button Master
assert-view Master
assert-perf-fx 11 -1
release pad 11
button Master
button Master
encoder -12
assert-perf-fx-mix 100
press 4
encoder -36
assert-perf-fx-mix 64
button Right
button Down
assert-perf-fx-mix 64
button Delete
assert-perf-fx-mix 100
encoder -36
release 4
assert-perf-fx-mix 64
wait 5100
""")
        # Effect names use rotated glyphs, which are absent from the text
        # audit. Check the four visible value labels in all twelve columns.
        frames = audit.read_text()
        assert "Perf FX" in frames
        assert "Global Mixer 3/3" in frames
        text_draws = [line.split("\t", 2) for line in frames.splitlines() if "\t" in line]
        for group, heading in enumerate(("Rhythm", "Turntable", "Tone")):
            assert [str(139 + group * 260), "50", heading] in text_draws
        values = [
            ("1/8", "1/8T", "1/16", "1/32"),
            ("80%", "60%", "40%", "20%"),
            ("Skip", "Shuffle", "Flip", "Chop"),
            ("1/8", "1/8D", "1/4", "1/4D"),
            ("Warm", "Wow", "Worn", "Chewed"),
            ("1x", "1.1x", "1.2x", "1.3x"),
            ("Baby", "Swing", "Cut", "Scribble"),
            ("1/4", "1/8", "1/16", "1/32"),
            ("4kHz", "2kHz", "800Hz", "250Hz"),
            ("160Hz", "400Hz", "900Hz", "1.8kHz"),
            ("2x/12b", "4x/10b", "8x/8b", "16x/6b"),
            ("20%", "47%", "73%", "100%"),
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
        bmp = grid.read_bytes()
        offset, = struct.unpack_from("<I", bmp, 10)
        width, height = struct.unpack_from("<ii", bmp, 18)
        bits, = struct.unpack_from("<H", bmp, 28)
        assert width == 800 and abs(height) == 480 and bits in (24, 32)
        pixel_bytes = bits // 8
        stride = (width * pixel_bytes + 3) & ~3
        # Above the preset labels, only the dividers occupy these columns.
        start = offset + (height - 1 - 150 if height > 0 else 150) * stride
        for x in (269, 529):
            pixels = [bmp[start + i * pixel_bytes:start + i * pixel_bytes + 3]
                      for i in range(x - 1, x + 3)]
            assert pixels[1] == pixels[2] and pixels[1] not in (pixels[0], pixels[3]), (x, pixels)
        assert any(x == "350" and text == "Line In" for x, _, text in text_draws)
        assert any(x == "450" and text == "Perf FX Mix" for x, _, text in text_draws)
        assert any(x == "450" and text == "64%" for x, _, text in text_draws)
        run(binary, card, "assert-perf-fx-mix 64\n")
        check_config_round_trip(binary, card)
        check_mix_save_during_playback(binary, card)
        config_audit = card / "config-frames.tsv"
        run(binary, card, f"audit-frames {config_audit}\nbutton Config\nwait 100\n")
        config_frames = config_audit.read_text()
        assert "Config" in config_frames and "Project Settings" in config_frames
    check_pattern_fx(binary)
    print("Perf FX pads, pattern sequencing, master and stem export, mute banks, and configuration passed.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("binary", type=Path)
    check(parser.parse_args().binary.resolve())
