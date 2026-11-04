#!/usr/bin/env python3
"""Exercise the real Sample Editor UI and inspect the committed PTI audio."""
import argparse
from pathlib import Path
import struct
import subprocess
import tempfile
import wave


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
        # Add Gain, edit it to 2x, save Chain01, audition and render. Reload the
        # saved file on the new sample, bypass Gain, compare Original, and render.
        script = card / "chain.txt"
        script.write_text("""import 0 /input.wav
button S. Editor
button 4
wait 100
assert-chain open
button 1
button Down
button 2
button 2
encoder 100
button 0
button 3
button Down
button Down
button 2
button 2
button 6
wait 200
button 7
button 2
wait 2000
assert-chain closed
button 4
wait 100
button 5
button Down
button 2
button Down
button 3
button 2
button 6
wait 200
button 4
wait 100
button 6
button 7
button 2
wait 2000
assert-chain closed
button 4
wait 100
assert-chain open
""")
        result = subprocess.run(
            [str(binary.resolve()), "--sd", str(card), "--script", str(script),
             "--screenshot", str((output / "sample-fx-chain.bmp").resolve())],
            capture_output=True, text=True, timeout=90)
        if result.returncode:
            raise RuntimeError(result.stdout + result.stderr)
        instruments = card / "Workspace/instruments"
        for index, expected in [(1, original), (2, processed), (3, processed)]:
            actual = (instruments / f"instrument_{index:02d}.pti").read_bytes()
            if actual[-len(expected):] != expected:
                raise AssertionError(f"Instrument {index} audio differs")
        if not (card / "Effects/Chains/Chain01.ptchain").is_file():
            raise AssertionError("Recipe was not saved")
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
button 4
wait 100
assert-chain open
button 6
button 0
button Down
button 2
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
        print("Sample FX chain: exact render, recipe reload, bypass, A/B, and snapshot-failure recovery passed")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("binary", type=Path)
    parser.add_argument("--output", type=Path, default=Path("build/desktop"))
    args = parser.parse_args()
    check(args.binary, args.output)
