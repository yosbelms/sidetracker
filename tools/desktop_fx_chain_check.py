#!/usr/bin/env python3
"""Exercise the real Sample Editor UI and inspect the committed PTI audio."""
import argparse
from pathlib import Path
import struct
import subprocess
import tempfile
import wave


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
    checked = 0
    values = {"Low": "0.000 dB", "Low freq": "90 Hz", "Low Width": "1.000",
              "Mid": "0.000 dB", "Mid freq": "800 Hz", "Mid Width": "1.000",
              "High": "0.000 dB", "High freq": "12000 Hz", "High Width": "1.000"}
    for frame in frames:
        if not any(y < 31 and text.startswith("FX Chain") for x, y, text in frame):
            continue
        toolbar = {}
        for x, y, text in frame:
            if 424 <= y < 480 and text:
                toolbar.setdefault(x, []).append(text)
        if toolbar.get(150) == ["Select"]:
            continue  # Context/recipe/destination menu.
        for x, choices in [(50, {"Preview", "Stop"}), (150, {"Add", "Use src", "Replace"}),
                           (250, {"Move Up"}), (350, {"Move Down"}), (450, {"Back"}), (550, {"Render"})]:
            if not toolbar.get(x) or toolbar[x][0] not in choices:
                raise AssertionError(f"Partially drawn chain toolbar at x={x}: {toolbar}")
        if toolbar.get(650):
            raise AssertionError(f"Seventh chain button should be empty: {toolbar}")
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
encoder 9
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
button 4
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
encoder 2
button Insert
button Down
button Insert
button 4
wait 100
assert-chain closed
""".replace("PLUGIN_AUDIT", str(audit.resolve()))
            .replace("PLUGIN_SCREEN", str((output / "sample-fx-plugin-params.bmp").resolve())))
        result = subprocess.run(
            [str(binary.resolve()), "--sd", str(card), "--script", str(script),
             "--screenshot", str((output / "sample-fx-plugins.bmp").resolve())],
            capture_output=True, text=True, timeout=90)
        if result.returncode:
            raise RuntimeError(result.stdout + result.stderr)
        saved = (card / "Effects/Chains/Chain01.ptchain").read_bytes()
        if saved[5] != 2:
            raise AssertionError("Desktop could not add repeated PTFX stages")
        for index, expected in [(1, (1.0, 0.0)), (2, (1.5, 1.0))]:
            node = 16 + index * 100
            if saved[node] != 255 or struct.unpack_from("<ff", saved, node + 51) != expected:
                raise AssertionError("PTFX parameter/list edits were not preserved independently")
        generated = (card / "Effects/Chains/Chain02.ptchain").read_bytes()
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
            with wave.open(str(card / "input.wav"), "wb") as wav:
                wav.setparams((1, 2, 44100, 1024, "NONE", "not compressed"))
                wav.writeframes(struct.pack("<h", 2000) * 1024)
            script = card / "tool.txt"
            commands = f"""import 0 /input.wav
button S. Editor
button 6
snapshot TOOLS_SCREEN
encoder {index}
button 5
"""
            if name == "normalize":
                commands += "encoder -50\nbutton 7\n"
            commands += """wait 300
button Master
wait 500
assert-view Master
"""
            script.write_text(commands.replace("TOOLS_SCREEN", str((output / "sample-editor-tools.bmp").resolve())))
            result = subprocess.run(
                [str(binary.resolve()), "--sd", str(card), "--script", str(script),
                 "--screenshot", str((output / f"sample-tool-{name}.bmp").resolve())],
                capture_output=True, text=True, timeout=90)
            if result.returncode:
                raise RuntimeError(result.stdout + result.stderr)
            data = (card / "Workspace/instruments/instrument_01.pti").read_bytes()
            samples = struct.unpack("<1024h", data[-2048:])
            if name == "normalize":
                if not all(16382 <= value <= 16383 for value in samples):
                    raise AssertionError("Normalizer tool did not apply the edited 50% target")
            elif name == "fade-in":
                if samples[0] != 0 or samples[-1] < 1990 or any(a > b for a, b in zip(samples, samples[1:])):
                    raise AssertionError("Fade In tool did not apply a rising sample envelope")
            else:
                if samples[0] != 2000 or samples[-1] > 2 or any(a < b for a, b in zip(samples, samples[1:])):
                    raise AssertionError("Fade Out tool did not apply a falling sample envelope")


def check(binary: Path, output: Path):
    output.mkdir(parents=True, exist_ok=True)
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
        script = card / "chain.txt"
        script.write_text("""audit-frames FRAME_AUDIT
import 0 /input.wav
button S. Editor
button 6
button Down
button 5
wait 300
button 6
button 5
wait 300
snapshot MAIN_SCREEN
button 5
wait 100
assert-chain open
button 1
button 1
button 7
encoder 100
button 2
button 3
button Left
button Left
button Left
encoder 9
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
button 5
button Insert
wait 2000
assert-chain closed
button 5
wait 100
button Right
button Insert
button Down
button Insert
button Down
button Insert
button Down
button Down
button Insert
button Insert
button 0
wait 200
button 5
button Insert
wait 2000
assert-chain closed
button 5
wait 100
assert-chain open
encoder -9
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
button 4
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
button 4
wait 200
assert-chain closed
""".replace("FRAME_AUDIT", str((output / "sample-fx-frames.tsv").resolve()))
            .replace("MAIN_SCREEN", str((output / "sample-editor-waveform.bmp").resolve()))
            .replace("BROWSER_SCREEN", str((output / "sample-fx-browser.bmp").resolve()))
            .replace("VALUE_SCREEN", str((output / "sample-fx-value.bmp").resolve()))
            .replace("COLUMNS_SCREEN", str((output / "sample-fx-columns.bmp").resolve())))
        result = subprocess.run(
            [str(binary.resolve()), "--sd", str(card), "--script", str(script),
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
        recipe = card / "Effects/Chains/Chain01.ptchain"
        if not recipe.is_file():
            raise AssertionError("Recipe was not saved")
        saved = recipe.read_bytes()
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
button 4
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
button 4
wait 200
assert-chain closed
""".replace("STOP_PROMPT", str((output / "sample-editor-stop-pattern.bmp").resolve()))
                .replace("LIVE_SCREEN", str((output / "sample-fx-live.bmp").resolve()))
                .replace("MAIN_PREVIEW_SCREEN", str((output / "sample-editor-chain-preview.bmp").resolve()))
                .replace("FIRST_PLAIN_PREVIEW", str((output / "sample-editor-first-preview.bmp").resolve()))
                .replace("SECOND_PLAIN_PREVIEW", str((output / "sample-editor-second-preview.bmp").resolve())))
            live_result = subprocess.run(
                [str(binary.resolve()), "--sd", str(live_card), "--script", str(live_script),
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
button 4
wait 100
assert-chain closed
""")
            failure = subprocess.run(
                [str(binary.resolve()), "--sd", str(failing), "--script", str(failure_script),
                 "--screenshot", str((output / "sample-fx-chain-failure.bmp").resolve())],
                capture_output=True, text=True, timeout=90)
            if failure.returncode:
                raise RuntimeError(failure.stdout + failure.stderr)
            source = failing / "Workspace/instruments/instrument_01.pti"
            if source.read_bytes()[-len(original):] != original:
                raise AssertionError("Snapshot failure changed the source")
            if (failing / "Workspace/instruments/instrument_02.pti").exists():
                raise AssertionError("Snapshot failure created an instrument")
        print("Sample FX chain: independent browser, repeated Add, exact render, recipes, bypass, persistent main-view preview, physical view switching, live parameter editing, startup while playing, and failure recovery passed")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("binary", type=Path)
    parser.add_argument("--output", type=Path, default=Path("build/desktop"))
    args = parser.parse_args()
    check(args.binary, args.output)
