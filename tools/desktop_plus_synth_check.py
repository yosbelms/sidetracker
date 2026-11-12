#!/usr/bin/env python3
"""Exercise the Tracker+ desktop Instrument picker, synths and patch storage."""

from pathlib import Path
import struct
import subprocess
import sys
import tempfile

from port_analog_synth_presets import fixed_float, integer, message


def select_part(part):
    return f"""button Pattern
press Instrument
wait 250
button pad {part}
release Instrument
button I. Params
"""


def run(binary, card, name, commands):
    script = card / f"{name}.txt"
    frames = card / f"{name}.tsv"
    script.write_text(f"audit-frames {frames}\n{commands}")
    subprocess.run(
        [str(binary), "--sd", str(card), "--script", str(script), "--scale", "1",
         "--screenshot", str(card / f"{name}.bmp")],
        check=True, timeout=60,
    )
    return {line.split("\t", 2)[2] for line in frames.read_text().splitlines()
            if line.count("\t") >= 2}


def check_badge(path, label, x, y, width, height):
    data = path.read_bytes()
    offset = struct.unpack_from("<I", data, 10)[0]
    bmp_width, bmp_height = struct.unpack_from("<ii", data, 18)
    pixel_size = struct.unpack_from("<H", data, 28)[0] // 8
    stride = (bmp_width * pixel_size + 3) & ~3
    bright_pixels = 0
    for row in range(y, y + height):
        bitmap_row = bmp_height - 1 - row if bmp_height > 0 else row
        start = offset + bitmap_row * stride + x * pixel_size
        for pixel in range(start, start + width * pixel_size, pixel_size):
            bright_pixels += min(data[pixel:pixel + 3]) >= 128
    if bright_pixels < 100:
        raise AssertionError(f"Missing {label} bitmap in {path.name}")


def check_bundled_patches(binary, card):
    patches = binary.parent.parent / "SD" / "Patches"
    for part, (model, count) in enumerate((("MINI", 200), ("OBER", 100))):
        folder = patches / model
        if not folder.is_dir():
            continue
        names = {patch.stem for patch in folder.glob("*.patch")}
        if len(names) != count:
            raise AssertionError(f"Expected {count} {model} patches, found {len(names)}")
        if uppercase := sorted(name for name in names if name.isupper()):
            raise AssertionError(f"All-uppercase {model} patch names: {uppercase}")
        commands = (select_part(part) + "button 6\nwait 250\n"
                    + "encoder 8\nwait 250\n" * ((count + 8) // 8)
                    + """reset-audio-peak
press 5
wait 200
assert-audio-peak 1
release 5
button 7
""")
        frames = run(binary, card, f"{model}-collection", commands)
        if missing := names - frames:
            raise AssertionError(f"Missing {model} patches in browser: {sorted(missing)}")


def check(binary, card):
    patches = card / "Patches" / "MINI"
    patches.mkdir(parents=True)
    patch_name = "1 Desktop Pulse"
    saved_patch = patches / f"{patch_name}.patch"
    fixture = integer(1, 9) + message(6, fixed_float(1, 2.0))
    saved_patch.write_bytes(fixture)
    for part, synth in enumerate(("MINI", "OBER", "CHIP")):
        frames = run(binary, card, synth, select_part(part) + """reset-audio-peak
press pad 12
wait 200
assert-audio-peak 100
release pad 12
""")
        expected = {f"Synth {part + 1} ({synth})", "Synth Parameters 1/2"}
        if part == 0:
            expected.update(("Synth 2 (OBER)", "Synth 3 (CHIP)"))
            for label, y in (("CHIP", 45), ("MINI", 95), ("OBER", 145)):
                check_badge(card / "MINI.bmp", label, 10, y, 80, 27)
        if missing := expected - frames:
            raise AssertionError(f"Missing Instrument picker or synth editor labels: {missing}")

    frames = run(binary, card, "load-and-save", select_part(0) + """button 6
reset-audio-peak
press 5
wait 200
assert-audio-peak 100
release 5
button 7
button 7
button 0
wait 200
""")
    if not {f"Edit Patch: {patch_name}", "Patch saved"} <= frames:
        raise AssertionError("The native patch was not loaded and saved through the patch editor")

    saved = saved_patch.read_bytes()
    if not saved.startswith(fixture) or len(saved) <= len(fixture):
        raise AssertionError("The saved patch lost its imported waveform")
    (card / "Workspace" / "0.patch").unlink()

    frames = run(binary, card, "reload", select_part(0) + """button 6
button 7
button 7
reset-audio-peak
press pad 16
wait 200
assert-audio-peak 100
release pad 16
""")
    if f"Edit Patch: {patch_name}" not in frames:
        raise AssertionError("The saved native patch did not reload")
    check_badge(card / "reload.bmp", "MINI patch header", 60, 49, 110, 40)

    bundled_card = card / "bundled"
    bundled_card.mkdir()
    check_bundled_patches(binary, bundled_card)


if __name__ == "__main__":
    with tempfile.TemporaryDirectory(prefix="sidetracker-desktop-plus-") as directory:
        check(Path(sys.argv[1]).resolve(), Path(directory))
    print("Tracker+ desktop Instrument picker, synth badges, audio, and patch round trip passed.")
