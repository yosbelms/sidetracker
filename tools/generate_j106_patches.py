#!/usr/bin/env python3
"""Stage J106 patches, or convert a local FM-106 raw JSON bank."""
import argparse
import csv
import json
from pathlib import Path
import re

from port_analog_synth_presets import write_patches

ROOT = Path(__file__).resolve().parents[1]
ENGINE = ROOT / "core" / "synth" / "engines"
ENGINE_ID = 64


def definitions(file, directive):
    for line in (ENGINE / file).read_text().splitlines():
        match = re.fullmatch(rf"{directive}\((.*)\)", line.strip())
        if match:
            yield [field.strip() for field in next(csv.reader([match[1]], skipinitialspace=True))]


def convert_bank(bank):
    for patch in bank:
        if not isinstance(patch, dict) or not isinstance(patch.get("name"), str):
            raise ValueError("Expected FM-106 records with a name and a raw parameter array")
        raw = patch.get("raw")
        if not isinstance(raw, list) or len(raw) < 36 or any(
            type(value) is not int or not 0 <= value <= 127 for value in raw[:36]
        ):
            raise ValueError(f"Invalid FM-106 parameters for {patch['name']!r}")

        def norm(index):
            return raw[index] / 127

        yield patch["name"], {
            "BendPitch": 12 * norm(0),
            "BendFilter": 5 * norm(1),
            "LfoRate": 0.05 * 600 ** norm(3),
            "LfoDelay": 4 * norm(4) ** 2,
            "PitchLfo": 12 * norm(5) ** 2,
            "Pwm": norm(6),
            "SubLevel": norm(7),
            "Noise": norm(8),
            "Hpf": raw[9],
            "Cutoff": 20 * 800 ** norm(10),
            "Resonance": norm(11),
            "FilterEnv": (-1 if raw[34] else 1) * 6 * norm(12),
            "FilterLfo": 6 * norm(13),
            "KeyTrack": norm(14),
            "Level": norm(15),
            "Attack": 0.001 * 4000 ** norm(16),
            "Decay": 0.004 * 3000 ** norm(17),
            "Sustain": norm(18),
            "Release": 0.004 * 3000 ** norm(19),
            "Pulse": int(bool(raw[23])),
            "Saw": int(bool(raw[24])),
            "Sub": int(bool(raw[25])),
            "Chorus": int(bool(raw[27])) + 2 * int(bool(raw[28])),
            "Range": raw[29],
            "PwmMode": int(bool(raw[33])),
            "Vca": int(bool(raw[35])),
        }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sd", type=Path, default=ROOT / "build" / "SD")
    parser.add_argument("--fm106-bank", type=Path, help="Local JSON bank with raw parameter arrays")
    args = parser.parse_args()

    schema = [
        (name, float(low), float(high), float(default))
        for name, _, _, _, low, high, default, _ in definitions("J106Params.def", "J106_PARAM")
    ]
    macros = [
        (name, param, float(amount.rstrip("f")))
        for _, name, param, amount in definitions("J106Macros.def", "J106_MACRO")
    ]
    if args.fm106_bank:
        bank = json.loads(args.fm106_bank.read_text())
        if not isinstance(bank, list):
            parser.error("The FM-106 bank must contain a JSON list")
        presets = list(convert_bank(bank))
        folder = args.sd / "Patches" / "J106" / "FM-106"
    else:
        presets = json.loads((ROOT / "patches" / "j106" / "patches.json").read_text()).items()
        folder = args.sd / "Patches" / "J106"
    write_patches(folder, ENGINE_ID, schema, presets, macros=macros)


if __name__ == "__main__":
    main()
