#!/usr/bin/env python3
"""Exercise the real Sample Editor UI and inspect the committed PTI audio."""
import argparse
import csv
import json
from pathlib import Path
import struct
import subprocess
import tempfile
import wave


def desktop_args(binary: Path):
    # Test in firmware coordinates to keep software rendering time bounded.
    return [str(binary.resolve()), "--scale", "1"]


def waveform_center(path: Path):
    """Read a stable waveform region from an SDL screenshot (without a UI dependency)."""
    data = path.read_bytes()
    offset = struct.unpack_from("<I", data, 10)[0]
    width, height = struct.unpack_from("<ii", data, 18)
    depth = struct.unpack_from("<H", data, 28)[0]
    if depth not in (24, 32):
        raise AssertionError("Unexpected screenshot pixel format")
    scale = width // 800
    pixel_bytes = depth // 8
    stride = (width * pixel_bytes + 3) & ~3
    return b"".join(
        data[offset + (abs(height) - 1 - y if height > 0 else y) * stride + 190 * scale * pixel_bytes:
             offset + (abs(height) - 1 - y if height > 0 else y) * stride + 610 * scale * pixel_bytes]
        for y in range(170 * scale, 270 * scale))


def check_chain_frames(path: Path):
    frames = []
    for line in path.read_text().splitlines():
        if line == "frame":
            frames.append([])
        elif frames:
            x, y, value = line.split("\t", 2)
            frames[-1].append((int(x), int(y), value))
    headers = {text for frame in frames for x, y, text in frame
               if 300 <= x < 500 and 31 <= y < 60}
    if not {"Chain", "Save Patch", "Load Patch"} <= headers:
        raise AssertionError("Chain or Save/Load Patch heading was missing")
    checked = 0
    values = {"Low": "0.000 dB", "Low freq": "90 Hz", "Low Width": "1.000",
              "Mid": "0.000 dB", "Mid freq": "800 Hz", "Mid Width": "1.000",
              "High": "0.000 dB", "High freq": "12000 Hz", "High Width": "1.000"}
    for frame in frames:
        if any(text == "Choose Generator" for _, _, text in frame):
            raise AssertionError("Actions still offers a separate generator picker")
        menu = any(300 <= x < 500 and 31 <= y < 60 and text in
                   {"Actions", "Save Patch", "Load Patch", "Render to"} for x, y, text in frame)
        back_positions = [x for x, y, text in frame if 424 <= y < 480 and text == "Back"]
        if back_positions and back_positions != [550]:
            raise AssertionError(f"Sample Editor Back appeared outside F6: {back_positions}")
        if menu:
            if back_positions != [550]:
                raise AssertionError("Sample Editor menu did not show Back on F6")
            if any(x >= 500 and 31 <= y < 424 and text for x, y, text in frame):
                raise AssertionError("Parameter/Value columns remained visible over the patch menu")
            if any(x == 750 and 424 <= y < 480 and text for x, y, text in frame):
                raise AssertionError("Value footer remained visible over the patch menu")
            continue
        if not any(y < 31 and text.startswith("FX Chain") for x, y, text in frame):
            continue
        if any(300 <= x < 500 and 60 <= y < 424 and text[:2].isdigit() and text[2:3] == " "
               for x, y, text in frame):
            raise AssertionError("An effect row displayed a leading stage number")
        toolbar = {}
        for x, y, text in frame:
            if 424 <= y < 480 and text:
                toolbar.setdefault(x, []).append(text)
        if toolbar.get(150) == ["Select"]:
            continue  # Context/saved-patch/destination menu.
        for x, choices in [(50, {"Preview", "Stop"}), (150, {"Add", "Use src", "Replace"}),
                           (350, {"Apply"}), (450, {"Actions"}), (550, {"Back"}), (650, {"Mix"})]:
            if not toolbar.get(x) or toolbar[x][0] not in choices:
                raise AssertionError(f"Partially drawn chain toolbar at x={x}: {toolbar}")
        if toolbar[350] != ["Apply"]:
            raise AssertionError(f"Apply button should have a single label: {toolbar[350]}")
        if toolbar.get(250):
            raise AssertionError(f"Third chain button should be empty: {toolbar}")
        value = toolbar.get(750, [])
        expected = {values[value[0]]} if value and value[0] in values else None
        if value and value[0] == "Low freq": expected = {"80 Hz", "90 Hz"}
        if expected is not None and (len(value) != 2 or value[1] not in expected):
            raise AssertionError(f"Parameter and value changed in separate frames: {value}")
        checked += 1
    if checked < 10:
        raise AssertionError("Frame audit did not exercise chain transitions")


def check_desktop_plugins(binary: Path, output: Path, input_wav: Path):
    # These metadata-only ARM images must never be executed by the desktop.
    with tempfile.TemporaryDirectory(prefix="ce-ptfx-ui-") as directory:
        card = Path(directory)
        (card / "input.wav").write_bytes(input_wav.read_bytes())
        (card / "Effects").mkdir()
        def plugin(payload, generator=False):
            header = bytearray(472)
            struct.pack_into("<IHH", header, 0, 0x58465450, 1, int(generator))
            name = b"Metadata Generator" if generator else b"Metadata Effect"
            header[8:8 + len(name)] = name
            header[56] = 0 if generator else 2
            struct.pack_into("<IIII", header, 60, 0, 472, len(payload), 4)
            if not generator:
                header[88:92] = b"Gain"
                struct.pack_into("<ffff", header, 96, 0, 4, 1, 0.25)
                header[118] = ord("f")
                header[120:124] = b"Mode"
                struct.pack_into("<ffff", header, 128, 0, 2, 0, 1)
                header[144:147] = bytes([4, 0, 3])
                header[150] = ord("l")
            return bytes(header) + payload
        valid = plugin(bytes(4) + b"Clean\0Warm\0Bright\0")
        (card / "Effects/00_effect.ptfx").write_bytes(valid)
        # Discovery accepts its header, but image validation must reject it.
        (card / "Effects/01_invalid.ptfx").write_bytes(plugin(bytes(4) + b"Clean\0Warm\0Bright"))
        (card / "Effects/02_gen.ptfx").write_bytes(plugin(bytes(4), True))
        script = card / "plugins.txt"
        audit = output / "sample-fx-plugins.tsv"
        script.write_text("""audit-frames PLUGIN_AUDIT
import 0 /input.wav
button S. Editor
button 5
wait 100
encoder 13
button 1
button 1
assert-chain-size 2
button 7
encoder 2
button Left
button Down
button Right
encoder 1
snapshot PLUGIN_SCREEN
button Left
button Left
button Insert
encoder 7
button Insert
button Insert
button 5
wait 100
button 0
wait 100
assert-preview stopped
button 0
wait 100
button 5
encoder 1
button 1
assert-chain-size 2
encoder 1
button 1
assert-chain-size 2
button 7
encoder 10
button Left
button Left
button Insert
encoder 1
button Insert
button Down
button Insert
button 5
wait 100
assert-chain closed
""".replace("PLUGIN_AUDIT", str(audit.resolve()))
            .replace("PLUGIN_SCREEN", str((output / "sample-fx-plugin-params.bmp").resolve())))
        result = subprocess.run(
            [*desktop_args(binary), "--sd", str(card), "--script", str(script),
             "--screenshot", str((output / "sample-fx-plugins.bmp").resolve())],
            capture_output=True, text=True, timeout=90)
        if result.returncode:
            raise RuntimeError(result.stdout + result.stderr)
        saved = (card / "Effects/Patches/Patch01.patch").read_bytes()
        if saved[5] != 2:
            raise AssertionError("Desktop could not add repeated PTFX stages")
        for index, expected in [(1, (1.0, 0.0)), (2, (1.5, 1.0))]:
            node = 16 + index * 100
            if saved[node] != 255 or struct.unpack_from("<ff", saved, node + 51) != expected:
                raise AssertionError("PTFX parameter/list edits were not preserved independently")
        generated = (card / "Effects/Patches/Patch02.patch").read_bytes()
        if generated[16] != 255 or generated[19] != 1 or struct.unpack_from("<I", generated, 8)[0] != 48510:
            raise AssertionError("Desktop could not select/configure a PTFX generator")
        frames = audit.read_text()
        if "PTFX: Tracker only" not in frames:
            raise AssertionError("Desktop did not explain unsupported ARM execution")
        if "Cannot load plugin" not in frames:
            raise AssertionError("Malformed PTFX image was not rejected")
        if "Warm" not in frames:
            raise AssertionError("PTFX list metadata was not displayed")


def check_editor_tools(binary: Path, output: Path):
    for name, index in [("normalize", 2), ("fade-in", 3), ("fade-out", 4)]:
        with tempfile.TemporaryDirectory(prefix="ce-tool-") as directory:
            card = Path(directory)
            frames = 8192 if name == "fade-in" else 1024
            with wave.open(str(card / "input.wav"), "wb") as wav:
                wav.setparams((1, 2, 44100, frames, "NONE", "not compressed"))
                wav.writeframes(struct.pack("<h", 2000) * frames)
            script = card / "tool.txt"
            commands = f"""import 0 /input.wav
button S. Editor
button 6
snapshot TOOLS_SCREEN
button 5
button 6
encoder {index}
button 0
"""
            if name == "normalize":
                commands += "encoder -50\n"
            if name == "fade-in":
                commands += """button 6
assert-sample 0 100 2000
button 5
assert-sample 0 100 2000
button 6
button 0
"""
            commands += "button 7\nwait 300\n"
            if name == "normalize":
                commands += "assert-sample 0 100 16382\n"
            commands += """button Master
wait 500
assert-view Master
"""
            script.write_text(commands.replace("TOOLS_SCREEN", str((output / "sample-editor-tools.bmp").resolve())))
            result = subprocess.run(
                [*desktop_args(binary), "--sd", str(card), "--script", str(script),
                 "--screenshot", str((output / f"sample-tool-{name}.bmp").resolve())],
                capture_output=True, text=True, timeout=90)
            if result.returncode:
                raise RuntimeError(result.stdout + result.stderr)
            data = (card / "Workspace/instruments/instrument_01.pti").read_bytes()
            if (card / "Workspace/instruments/instrument_02.pti").exists():
                raise AssertionError("A tool Apply created a new sample slot")
            samples = struct.unpack(f"<{frames}h", data[-2 * frames:])
            if name == "normalize":
                if not all(16382 <= value <= 16383 for value in samples):
                    raise AssertionError("Normalizer tool did not apply the edited 50% target")
            elif name == "fade-in":
                if samples[0] != 0 or samples[-1] < 1990 or any(a > b for a, b in zip(samples, samples[1:])):
                    raise AssertionError("Fade In tool did not apply a rising sample envelope")
            else:
                if samples[0] != 2000 or samples[-1] > 2 or any(a < b for a, b in zip(samples, samples[1:])):
                    raise AssertionError("Fade Out tool did not apply a falling sample envelope")


def check_apply_to_current_slot(binary: Path, output: Path):
    for blocked in (False, True):
        with tempfile.TemporaryDirectory(prefix="ce-apply-") as directory:
            card = Path(directory)
            pcm = struct.pack("<h", 2000) * 16384
            with wave.open(str(card / "input.wav"), "wb") as wav:
                wav.setparams((1, 2, 44100, 16384, "NONE", "not compressed"))
                wav.writeframes(pcm)
            script = card / "apply.txt"
            imports = "import 1 /input.wav\nimport 0 /input.wav\n"
            if blocked:
                # Create a valid workspace before obstructing the edit's staging file.
                script.write_text(imports)
                setup = subprocess.run(
                    [*desktop_args(binary), "--sd", str(card), "--script", str(script),
                     "--screenshot", str(card / "setup.bmp")],
                    capture_output=True, text=True, timeout=90)
                if setup.returncode:
                    raise RuntimeError(setup.stdout + setup.stderr)
                (card / "Workspace/instruments/edit.tmp").mkdir()
            script.write_text(imports + """button S. Editor
button 5
button 1
button 7
encoder 200
button 0
assert-preview live
assert-sample 0 100 2000
button 3
wait 2000
""" + ("""assert-chain open
assert-chain-size 1
assert-sample 0 100 2000
button 5
assert-chain closed
button 6
encoder 3
button 0
button 7
wait 1000
assert-sample 0 100 2000
button 5
button Pattern
assert-view Pattern
""" if blocked else """assert-chain closed
assert-sample 0 100 6000
assert-sample 1 100 2000
button Pattern
assert-view Pattern
button S. Editor
button 4
wait 2000
assert-sample 0 100 2000
assert-sample 1 100 2000
"""))
            result = subprocess.run(
                [*desktop_args(binary), "--sd", str(card), "--script", str(script),
                 "--screenshot", str((output / f"sample-apply-{'failed' if blocked else 'undo'}.bmp").resolve())],
                capture_output=True, text=True, timeout=90)
            if result.returncode:
                raise RuntimeError(result.stdout + result.stderr)
            instruments = card / "Workspace/instruments"
            if sorted(path.name for path in instruments.glob("*.pti")) != ["instrument_01.pti", "instrument_02.pti"]:
                raise AssertionError("Apply changed the set of sample slots")
            if (instruments / "instrument_01.pti").read_bytes()[-len(pcm):] != pcm:
                raise AssertionError("Undo or failed Apply changed the original workspace PCM")


def check_native_stpalette(binary: Path, output: Path):
    with tempfile.TemporaryDirectory(prefix="ce-stpalette-ui-") as directory:
        card = Path(directory)
        pcm = struct.pack("<h", 2000) * 44100
        with wave.open(str(card / "input.wav"), "wb") as wav:
            wav.setparams((1, 2, 44100, 44100, "NONE", "not compressed"))
            wav.writeframes(pcm)
        script = card / "native.txt"
        audit = output / "sample-stpalette-frames.tsv"
        script.write_text(f"""audit-frames {audit.resolve()}
import 0 /input.wav
button S. Editor
button 5
wait 100
encoder 12
button 1
button 7
button Up
button Down
button Up
encoder 5
button 0
wait 100
assert-preview live
encoder -5
wait 100
assert-preview live
snapshot {(output / 'sample-stpalette-type.bmp').resolve()}
button 0
button Left
button Left
button Left
encoder -3
button 1
assert-chain-size 2
button Right
button Insert
encoder 7
button Insert
button Insert
button Insert
encoder 8
button Insert
button 5
wait 100
button 5
assert-chain-size 2
button 4
encoder 100
button Insert
wait 5000
button Insert
wait 2000
assert-chain closed
""")
        result = subprocess.run([*desktop_args(binary), "--sd", str(card), "--script", str(script),
                                 "--screenshot", str((output / "sample-stpalette-render.bmp").resolve())],
                                capture_output=True, text=True, timeout=90)
        if result.returncode:
            raise RuntimeError(result.stdout + result.stderr)
        saved = (card / "Effects/Patches/Patch01.patch").read_bytes()
        if saved[:5] != b"PTPR\x01" or saved[5] != 2 or saved[116] != 35 or saved[216] != 32:
            raise AssertionError("Native category/Type patch was not saved as PTPR v1")
        with (card / "Effects/STPaletteCPU.csv").open() as report:
            rows = list(csv.DictReader(report))
        if len(rows) != 3 or any(row["category"] != "Character" or int(row["max_ticks"]) <= 0 for row in rows):
            raise AssertionError("Native CPU report did not include all quality profiles")
        rendered = (card / "Workspace/instruments/instrument_02.pti").read_bytes()
        if rendered[-len(pcm):] != pcm:
            raise AssertionError("Neutral Filter and bypassed-amount Drive altered rendered PCM")
        frames = audit.read_text()
        for text in ["ST Character", "ST Movement", "ST Diffusion", "ST Texture",
                     "ST Character: Drive", "ST Texture: Filter", "Halo", "Filter"]:
            if text not in frames:
                raise AssertionError(f"Native category/Type UI did not display {text}")
        values = []
        for frame in frames.split("frame\n"):
            footer = []
            for line in frame.splitlines():
                fields = line.split("\t", 2)
                if len(fields) == 3 and fields[0] == "750" and 424 <= int(fields[1]) < 480:
                    footer.append(fields[2])
            if len(footer) == 2 and footer[0] == "Type" and (not values or values[-1] != footer[1]):
                values.append(footer[1])
        if values[:4] != ["Filter", "Squash", "Filter", "Halo"]:
            raise AssertionError(f"Type arrows moved in the wrong direction: {values}")


def check_named_patches(binary: Path, output: Path):
    root = Path(__file__).resolve().parents[1]
    staged = root / "build/SD/Effects/Patches"
    settings = json.loads((root / "patches/stpalette/patches.json").read_text())
    patches = {patch["name"]: patch for patch in settings["patches"]}
    with tempfile.TemporaryDirectory(prefix="ce-named-patches-") as directory:
        card = Path(directory)
        saved = card / "Effects/Patches"
        saved.mkdir(parents=True)
        for name in patches:
            (saved / f"{name}.patch").write_bytes((staged / f"{name}.patch").read_bytes())
        legacy_dir = card / "Effects/Presets"
        legacy_dir.mkdir()
        names = sorted(patches)
        edited = names[-1]
        original = (saved / f"{edited}.patch").read_bytes()
        (legacy_dir / "Old Folder.patch").write_bytes(original)
        (saved / "Old Extension.ptpreset").write_bytes(original)
        (saved / "Ignored.patch").mkdir()
        (saved / "notes.txt").write_text("not a patch")
        with wave.open(str(card / "input.wav"), "wb") as wav:
            wav.setparams((1, 2, 44100, 44100 * 4, "NONE", "not compressed"))
            wav.writeframes(struct.pack("<h", 2000) * (44100 * 4))
        audit = output / "sample-named-patches.tsv"
        commands = [f"audit-frames {audit.resolve()}", "import 0 /input.wav",
                    "button S. Editor", "button 5", "button Right"]
        for index, name in enumerate(names):
            stages = patches[name]["stages"]
            commands += ["button Insert", f"encoder {0 if index == 0 else 6}", "button Insert",
                         f"encoder {index}", "button Insert", f"assert-chain-size {len(stages)}",
                         "button 0", "wait 100", "assert-preview live", "button 0", "wait 50"]
        # Edit the last loaded chain and replace its named patch.
        amount = patches[edited]["stages"][0]["amount"]
        delta = -1 if amount == 100 else 1
        commands += ["button Right", "button Down", "button Right", f"encoder {delta}",
                     "button Left", "button Left", "button Insert", "encoder 7", "button Insert",
                     "button Insert"]
        # The installed pack still leaves room to save the chain as a new patch.
        commands += ["button Insert", "encoder 7", "button Insert", "encoder 1", "button Insert",
                     "button Master", "assert-chain closed", "assert-view Master"]
        script = card / "named.txt"
        script.write_text("\n".join(commands) + "\n")
        result = subprocess.run([*desktop_args(binary), "--sd", str(card), "--script", str(script),
                                 "--screenshot", str((output / "sample-named-patches.bmp").resolve())],
                                capture_output=True, text=True, timeout=120)
        if result.returncode:
            raise RuntimeError(result.stdout + result.stderr)
        frames = audit.read_text()
        for name in names:
            if f"\t{name}\n" not in frames:
                raise AssertionError(f"Saved patch name was not displayed: {name}")
        for ignored in ("Ignored", "notes", "Old Folder", "Old Extension"):
            if f"\t{ignored}\n" in frames:
                raise AssertionError(f"Saved-patch browser included {ignored}")
        actual = (saved / f"{edited}.patch").read_bytes()
        if struct.unpack_from("<f", actual, 16 + 100 + 51 + 4)[0] != amount + delta:
            raise AssertionError("Named patch parameter edit was not saved")
        expected_files = {f"{name}.patch" for name in [*names, "Patch01"]}
        if {path.name for path in saved.glob("*.patch") if path.is_file()} != expected_files:
            raise AssertionError("Save Patch did not add exactly one numbered destination")
        if (saved / "Patch01.patch").read_bytes() != actual:
            raise AssertionError("The numbered patch did not store the edited chain")
        for name in patches:
            if name != edited and (saved / f"{name}.patch").read_bytes() != (staged / f"{name}.patch").read_bytes():
                raise AssertionError("Saving a named patch replaced a different patch")


def check_chain_mix(binary: Path, output: Path):
    for percent in (0, 50, 100):
        with tempfile.TemporaryDirectory(prefix="ce-patch-mix-") as directory:
            card = Path(directory)
            pcm = struct.pack("<h", 2000) * (44100 * 4)
            with wave.open(str(card / "input.wav"), "wb") as wav:
                wav.setparams((1, 2, 44100, 44100 * 4, "NONE", "not compressed"))
                wav.writeframes(pcm)
            audit = output / f"sample-patch-mix-{percent}.tsv"
            delta = -1 if percent == 100 else 1
            arrows = ["Down", "Up", "Left", "Right"] if percent == 100 else ["Up", "Down", "Right", "Left"]
            commands = [f"audit-frames {audit.resolve()}", "import 0 /input.wav", "button S. Editor", "button 5",
                        "button 1", "button 7", "encoder 100", "button Left", "button Left", "button Left",
                        "button 1", "button 7", "encoder 100", "press 6", f"encoder {percent - 100}",
                        *[f"button {arrow}" for arrow in arrows], "release 6",
                        "button 0", "wait 100", "assert-preview live", "press 6", f"encoder {delta}", "wait 50",
                        "assert-preview live", f"encoder {-delta}", "assert-preview live", "release 6", "button 0"]
            if percent == 50:
                commands += ["button Left", "button Left", "button 4", "encoder 7", "button Insert",
                             "button Insert", "press 6", "encoder -50", "release 6", "button 4", "encoder 6",
                             "button Insert", "button Insert"]
            commands += [f"snapshot {(output / f'sample-patch-mix-{percent}-control.bmp').resolve()}", "button 4", "encoder 100", "button Insert", "wait 20000", "button Insert", "wait 20000", "assert-chain closed"]
            script = card / "mix.txt"; script.write_text("\n".join(commands) + "\n")
            result = subprocess.run([*desktop_args(binary), "--sd", str(card), "--script", str(script),
                                     "--screenshot", str((output / f"sample-patch-mix-{percent}.bmp").resolve())],
                                    capture_output=True, text=True, timeout=120)
            if result.returncode: raise RuntimeError(result.stdout + result.stderr)
            rendered = (card / "Workspace/instruments/instrument_02.pti").read_bytes()
            expected = struct.pack("<h", 2000 + 60 * percent) * (44100 * 4)
            if rendered[-len(expected):] != expected:
                raise AssertionError(f"Global {percent}% Mix did not blend the entire two-stage chain")
            if (card / "Workspace/instruments/instrument_01.pti").read_bytes()[-len(pcm):] != pcm:
                raise AssertionError("Chain Mix changed the source sample")
            if percent == 50 and (card / "Effects/Patches/Patch01.patch").read_bytes()[7] != 50:
                raise AssertionError("Patch Mix was not saved")
            frames = audit.read_text()
            if f"\t{percent}%\n" not in frames:
                raise AssertionError("Chain Mix percentage was not displayed")
            for frame in frames.split("frame\n")[1:]:
                if "650\t441\tMix\n" not in frame:
                    continue
                for line in frame.splitlines():
                    fields = line.split("\t", 2)
                    if len(fields) == 3 and (fields[2] == "Mix" or fields[2].endswith("%")) and fields[0] != "650":
                        raise AssertionError("Mix appeared outside its toolbar button")
            if percent == 50:
                saved = (card / "Effects/Patches/Patch01.patch").read_bytes()
                for index in (1, 2):
                    if struct.unpack_from("<f", saved, 16 + index * 100 + 51)[0] != 2:
                        raise AssertionError("Holding Mix changed an effect parameter")
    # Whole-input analysis uses prepared playback. Repeated mix passes must use
    # the cached wet audio and original snapshot, without blending twice.
    for kind in ("normalize", "latency"):
        with tempfile.TemporaryDirectory(prefix="ce-prepared-mix-") as directory:
            card = Path(directory); saved = card / "Effects/Patches"; saved.mkdir(parents=True)
            blob = bytearray(916); blob[:5] = b"PTPR\x01"; blob[5] = 1; blob[7] = 100
            struct.pack_into("<I", blob, 8, 44100)
            for index in range(9): blob[16 + index * 100] = 254
            if kind == "latency":
                blob[6] = 1
                blob[116] = 32; blob[117] = 6; blob[116 + 99] = 1  # Economy Drive, neutral Amount.
                struct.pack_into("<6f", blob, 116 + 51, 0, 0, 50, 0, 0, 0)
            else:
                blob[116] = 0; blob[117] = 1  # Legacy Normalizer at 50% target.
                struct.pack_into("<f", blob, 116 + 51, 50)
            checksum = 2166136261
            for byte in blob[:12] + blob[16:]: checksum = ((checksum ^ byte) * 16777619) & 0xffffffff
            struct.pack_into("<I", blob, 12, checksum)
            (saved / f"{kind}.patch").write_bytes(blob)
            pcm = struct.pack("<h", 2000) * 16001
            with wave.open(str(card / "input.wav"), "wb") as wav:
                wav.setparams((1, 2, 44100, 16001, "NONE", "not compressed")); wav.writeframes(pcm)
            script = card / "prepared.txt"
            script.write_text("""import 0 /input.wav
button S. Editor
button 5
button Right
button 4
button Insert
button Insert
assert-chain-size 1
button 4
encoder 100
button Insert
wait 5000
button 5
button 0
press 6
encoder -50
wait 1000
encoder -50
wait 1000
encoder 50
wait 1000
release 6
button 4
encoder 100
button Insert
button Insert
wait 5000
assert-chain closed
""")
            result = subprocess.run([*desktop_args(binary), "--sd", str(card), "--script", str(script),
                                     "--screenshot", str((output / f"sample-prepared-{kind}-mix.bmp").resolve())],
                                    capture_output=True, text=True, timeout=90)
            if result.returncode: raise RuntimeError(result.stdout + result.stderr)
            rendered = (card / "Workspace/instruments/instrument_02.pti").read_bytes()
            if kind == "latency":
                expected = bytes(14 * 2) + pcm
                if rendered[-len(expected):] != expected:
                    raise AssertionError("Rendered global Mix did not align the dry path to the STPalette economy profile")
            else:
                samples = struct.unpack("<16001h", rendered[-len(pcm):])
                # The legacy normalizer's 50% target rounds to 16382–16383.
                if not all(9191 <= value <= 9192 for value in samples):
                    raise AssertionError(f"Prepared Mix passes did not preserve the original wet cache: {samples[:8]}")


def check(binary: Path, output: Path):
    output.mkdir(parents=True, exist_ok=True)
    check_native_stpalette(binary, output)
    check_named_patches(binary, output)
    check_chain_mix(binary, output)
    with tempfile.TemporaryDirectory(prefix="ce-fx-chain-") as directory:
        card = Path(directory)
        samples = [((index * 97) % 24000) - 12000 for index in range(8001)]
        original = struct.pack("<" + "h" * len(samples), *samples)
        processed = struct.pack("<" + "h" * len(samples), *(value * 2 for value in samples))
        with wave.open(str(card / "input.wav"), "wb") as wav:
            wav.setparams((1, 2, 44100, len(samples), "NONE", "not compressed"))
            wav.writeframes(original)
        # A valid metadata-only plugin fixture makes the generator visible in
        # the browser. Its ARM payload is never executed by the desktop test.
        (card / "Effects").mkdir()
        plugin = bytearray(476)
        struct.pack_into("<IHH", plugin, 0, 0x58465450, 1, 1)
        plugin[8:8 + len(b"Catalog Generator")] = b"Catalog Generator"
        struct.pack_into("<III", plugin, 60, 0, 472, 4)
        (card / "Effects/catalog_gen.ptfx").write_bytes(plugin)
        # Reverse twice through Tools, append two effects from the independent
        # browser, and edit only the second. Browsing the generator must not
        # replace either stage or the source. Then save/load, bypass, and render.
        script = card / "patch.txt"
        script.write_text("""audit-frames FRAME_AUDIT
import 0 /input.wav
button S. Editor
button 6
button Down
button 0
button 7
wait 300
button 6
button 0
button 7
wait 300
snapshot MAIN_SCREEN
button 5
wait 100
assert-chain open
button 4
button 5
button Left
button 1
button 1
button 7
encoder 100
button 4
button 5
button Right
button 4
button 5
button 4
snapshot ACTIONS_SCREEN
button Down
button Insert
button 4
button Down
button Down
button Insert
button Left
encoder 13
snapshot BROWSER_SCREEN
button Right
button Right
button Right
snapshot VALUE_SCREEN
button Left
button Left
button Up
button Up
button Insert
encoder 7
button Insert
button Insert
button 0
wait 200
button 4
encoder 100
button Insert
wait 2000
button 5
assert-chain open
assert-chain-size 2
button 4
encoder 100
button Insert
wait 2000
button Insert
wait 2000
assert-chain closed
# Rendering selects the new instrument. Switch back to the original source.
button Pattern
import 0 /input.wav
button S. Editor
button 5
wait 100
button 4
button Insert
button Down
button Insert
assert-chain-size 2
button 0
wait 200
button 4
encoder 100
button Insert
button Insert
wait 2000
assert-chain closed
button 5
wait 100
assert-chain open
encoder -13
button 1
button Right
button Insert
button Down
button Down
button Down
button Down
button Down
button Insert
encoder 6
button 1
button 4
encoder 5
button 1
button Right
button Down
button Right
encoder 10
snapshot COLUMNS_SCREEN
button Left
encoder 7
button Up
button Up
button Up
button Up
button Up
button Up
button Up
button 5
wait 200
assert-chain closed
assert-chain-size 1
button 5
assert-chain open
assert-chain-size 1
button Master
wait 200
assert-view Master
assert-chain closed
button S. Editor
button 5
assert-chain open
assert-chain-size 1
button 5
wait 200
assert-chain closed
""".replace("FRAME_AUDIT", str((output / "sample-fx-frames.tsv").resolve()))
            .replace("MAIN_SCREEN", str((output / "sample-editor-waveform.bmp").resolve()))
            .replace("BROWSER_SCREEN", str((output / "sample-fx-browser.bmp").resolve()))
            .replace("VALUE_SCREEN", str((output / "sample-fx-value.bmp").resolve()))
            .replace("COLUMNS_SCREEN", str((output / "sample-fx-columns.bmp").resolve()))
            .replace("ACTIONS_SCREEN", str((output / "sample-fx-actions.bmp").resolve())))
        result = subprocess.run(
            [*desktop_args(binary), "--sd", str(card), "--script", str(script),
             "--screenshot", str((output / "sample-fx-chain.bmp").resolve())],
            capture_output=True, text=True, timeout=90)
        if result.returncode:
            raise RuntimeError(result.stdout + result.stderr)
        check_chain_frames(output / "sample-fx-frames.tsv")
        instruments = card / "Workspace/instruments"
        for index, expected in [(1, original), (2, processed), (3, processed)]:
            actual = (instruments / f"instrument_{index:02d}.pti").read_bytes()
            if actual[-len(expected):] != expected:
                raise AssertionError(f"Instrument {index} audio differs")
        definition = card / "Effects/Patches/Patch01.patch"
        if not definition.is_file():
            raise AssertionError("Patch was not saved")
        saved = definition.read_bytes()
        if saved[5] != 2:
            raise AssertionError("Repeated Add did not append two stages")
        if saved[16] != 254:
            raise AssertionError("Browsing a generator changed the source")
        check_desktop_plugins(binary, output, card / "input.wav")
        # First entry during pattern playback must show the confirmation
        # safely, including Cancel and Yes. Then exercise real live audition.
        with tempfile.TemporaryDirectory(prefix="ce-live-ui-") as live_directory:
            live_card = Path(live_directory)
            with wave.open(str(live_card / "long.wav"), "wb") as wav:
                frames = 44100 * 4
                wav.setparams((1, 2, 44100, frames, "NONE", "not compressed"))
                wav.writeframes(struct.pack("<h", 2000) * frames)
            live_script = live_card / "live.txt"
            live_script.write_text("""button Play
wait 100
assert-sequencer playing
button S. Editor
wait 100
assert-sequencer playing
snapshot STOP_PROMPT
button 6
assert-sequencer playing
button S. Editor
button 7
assert-sequencer stopped
button Pattern
import 0 /long.wav
button S. Editor
button Master
assert-view Master
button S. Editor
button 0
wait 100
snapshot FIRST_PLAIN_PREVIEW
wait 4500
button 0
wait 100
snapshot SECOND_PLAIN_PREVIEW
button Master
assert-view Master
button S. Editor
button 5
wait 100
button 1
button 7
encoder 100
button 0
wait 100
assert-preview live
encoder -50
assert-preview live
snapshot LIVE_SCREEN
button 5
wait 200
assert-chain closed
assert-chain-size 1
button 0
wait 100
assert-preview live
assert-chain closed
button 1
wait 200
assert-preview stopped
snapshot MAIN_PREVIEW_SCREEN
button 0
wait 100
assert-preview live
button Master
wait 200
assert-view Master
assert-preview stopped
assert-chain closed
button S. Editor
button 5
assert-chain open
assert-chain-size 1
button 0
assert-preview live
button Master
wait 200
assert-view Master
assert-preview stopped
button S. Editor
button 0
wait 4500
assert-preview stopped
button 5
assert-chain open
assert-chain-size 1
button 5
wait 200
assert-chain closed
""".replace("STOP_PROMPT", str((output / "sample-editor-stop-pattern.bmp").resolve()))
                .replace("LIVE_SCREEN", str((output / "sample-fx-live.bmp").resolve()))
                .replace("MAIN_PREVIEW_SCREEN", str((output / "sample-editor-chain-preview.bmp").resolve()))
                .replace("FIRST_PLAIN_PREVIEW", str((output / "sample-editor-first-preview.bmp").resolve()))
                .replace("SECOND_PLAIN_PREVIEW", str((output / "sample-editor-second-preview.bmp").resolve())))
            live_result = subprocess.run(
                [*desktop_args(binary), "--sd", str(live_card), "--script", str(live_script),
                 "--screenshot", str((output / "sample-fx-live-closed.bmp").resolve())],
                capture_output=True, text=True, timeout=90)
            if live_result.returncode:
                raise RuntimeError(live_result.stdout + live_result.stderr)
            first = waveform_center(output / "sample-editor-first-preview.bmp")
            second = waveform_center(output / "sample-editor-second-preview.bmp")
            if len(first) != len(second) or sum(a != b for a, b in zip(first, second)) > len(first) * 0.02:
                raise AssertionError("Repeated main-view Preview displayed a dialog over the waveform")
            untouched = live_card / "Workspace/instruments/instrument_01.pti"
            if untouched.read_bytes()[-frames * 2:] != struct.pack("<h", 2000) * frames:
                raise AssertionError("Leaving or previewing the draft changed the source sample")

        check_editor_tools(binary, output)
        check_apply_to_current_slot(binary, output)
        # A blocked temporary directory simulates a source-snapshot write failure.
        # Preview must fail without creating a destination or changing the source.
        with tempfile.TemporaryDirectory(prefix="ce-fx-chain-failure-") as failing_directory:
            failing = Path(failing_directory)
            (failing / "input.wav").write_bytes((card / "input.wav").read_bytes())
            (failing / "Workspace").mkdir()
            (failing / "Workspace/FxChainTemp").write_text("not a directory")
            failure_script = failing / "failure.txt"
            failure_script.write_text("""import 0 /input.wav
button S. Editor
button 5
wait 100
assert-chain open
button 0
button 5
wait 100
assert-chain closed
""")
            failure = subprocess.run(
                [*desktop_args(binary), "--sd", str(failing), "--script", str(failure_script),
                 "--screenshot", str((output / "sample-fx-chain-failure.bmp").resolve())],
                capture_output=True, text=True, timeout=90)
            if failure.returncode:
                raise RuntimeError(failure.stdout + failure.stderr)
            source = failing / "Workspace/instruments/instrument_01.pti"
            if source.read_bytes()[-len(original):] != original:
                raise AssertionError("Snapshot failure changed the source")
            if (failing / "Workspace/instruments/instrument_02.pti").exists():
                raise AssertionError("Snapshot failure created an instrument")
        print("Sample FX chain: independent browser, repeated Add, exact render, patches, Actions toolbar/menu, whole-chain Mix, bypass, persistent main-view preview, physical view switching, live parameter editing, startup while playing, and failure recovery passed")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("binary", type=Path)
    parser.add_argument("--output", type=Path, default=Path("build/desktop"))
    args = parser.parse_args()
    check(args.binary, args.output)
