#!/usr/bin/env python3
"""Build the MINI and OBER preset pack as individual Tracker .patch files."""

import argparse
from itertools import chain
from pathlib import Path
import re
import shutil
import struct
import unicodedata
import zipfile

from ober_showcase_presets import OBER_SHOWCASE


ROOT = Path(__file__).resolve().parents[1]


def read_array(text, name):
    match = re.search(rf"\b{re.escape(name)}\[[^\]]*\](?:\[[^\]]*\])?\s*=\s*\{{(.*?)^\}};",
                      text, re.S | re.M)
    if not match:
        raise ValueError(f"Missing array {name}")
    return match[1]


def numbers(text):
    return [int(value) for value in re.findall(r"-?\d+", text)]


def midi_hz(note):
    return 440 * 2 ** ((note - 69) / 12)


def mini_bank(source):
    firmware = source / "firmware/src"
    presets = (firmware / "eng_fmini_presets.h").read_text()
    filter_table = read_array((firmware / "eng_fmini_filter.h").read_text(), "FMINI_CUT")
    cutoffs = numbers(re.sub(r"/\*.*?\*/", "", filter_table, flags=re.S))
    oscillators = re.findall(r"FGP\(([^)]*)\).*?/\*\s*(\d+)\s+", read_array(presets, "FMINI_FACTORY"))
    voices = re.findall(r'\{"([^"]+)",\s*\{([^}]+)\},\s*\{([^}]+)\}', read_array(presets, "FMINI_PRESETS"))
    extras = re.findall(r"\{\{[^}]+\},\s*\{([^}]+)\},\s*\{[^}]+\},\s*(\d+),\s*\{([^}]+)\},",
                        read_array(presets, "FMINI_X"))
    if len(cutoffs) != 128 or not len(voices) == len(oscillators) == len(extras) == 200:
        raise ValueError("Expected the 200 FMini-1 2.5.12 presets")

    waves = (3, 5, 1, 2, 6, 7, 4)
    lfo_shapes = (3, 0, 1, 2, 5)
    divisions = (0, 9, 8, 1, 2, 3, 4, 6, 5, 10, 11)

    def seconds(value):
        return 0.001 * 12000 ** (value / 127)

    def cents(low, high):
        return (-1 if high & 8 else 1) * (low | (high & 7) << 7)

    for slot, ((osc, number), (name, edit, amp), (lfo, level, mod)) in enumerate(zip(oscillators, voices, extras)):
        osc = numbers(osc)
        edit = numbers(edit)
        amp = numbers(amp)
        lfo = numbers(lfo)
        mod = numbers(mod)
        if int(number) != slot or edit[7] != slot or len(osc) != 24:
            raise ValueError(f"Misaligned FMini-1 preset at slot {slot}")
        tune = (osc[14] - 64) / 63
        lfo_pitch = abs(mod[0]) * 0.1875 + (edit[6] / 127 * 2.85 if osc[12] & 32 else 0)
        settings = {
            "Noise": (osc[11] / 127) ** 2 if osc[12] & 8 else 0,
            "Pink": int(bool(osc[12] & 16)),
            "Cutoff": midi_hz(cutoffs[edit[0]] / 16 + 19 * osc[16] / 127) / 1.578,
            "Resonance": edit[1] / 127,
            "KeyTrack": osc[16] / 127,
            "ContourAmount": 5 * edit[2] / 127,
            "FilterAttack": seconds(edit[3]), "FilterDecay": seconds(edit[4]), "FilterSustain": edit[5] / 127,
            "AmpAttack": seconds(amp[0]), "AmpDecay": seconds(amp[1]),
            # FMini-1's factory records set amp release to the decay time.
            "AmpSustain": amp[2] / 127, "AmpRelease": seconds(amp[1]),
            "LfoWave": lfo_shapes[lfo[1]], "Rate": 0.05 * 800 ** (lfo[0] / 127), "Sync": divisions[lfo[3]],
            "LfoPitch": lfo_pitch, "LfoAmp": abs(mod[1]) / 127,
            "LfoFilter": edit[6] / 127 * 2.85 if osc[12] & 64 else 0,
            "Glide": 0.001 * 12000 ** (osc[17] / 127) if osc[13] & 1 else 0,
            "Level": int(level) / 127,
        }
        detune = (0, cents(osc[18], osc[19]), cents(osc[20], osc[21]))
        for index in range(3):
            key = index + 1
            settings[f"Wave{key}"] = waves[osc[3 + index]] if osc[12] & (1 << index) else 0
            settings[f"Range{key}"] = osc[index]
            settings[f"Tune{key}"] = tune + detune[index] / 100
            settings[f"Level{key}"] = (osc[8 + index] / 127) ** 2
        yield name, settings


def ober_bank(source):
    firmware = source / "firmware/src"
    patches = (firmware / "obv_patches.h").read_text()
    definitions = (firmware / "obv_params.h").read_text()
    symbols = {
        "W_OFF": 0, "W_SAW": 1, "W_PLS": 2, "W_BOTH": 3,
        "F_LP12": 0, "F_HP12": 1, "F_BP12": 2, "F_NOTCH": 3, "F_SEM": 4, "F_LP24": 5,
        "M_POLY": 0, "M_DUAL": 0, "M_UNISON": 1, "M_MONO": 1,
        "L_TRI": 0, "L_SAW": 1, "L_RAMP": 4, "L_SQR": 2, "L_SH": 5, "L_RND": 6,
        "TO_O1": 0, "TO_O2": 1, "TO_BOTH": 2, "LP_SYNC": 1, "LP_FREE": 0, "LP_SPREAD": 0,
    }
    defaults = dict(re.findall(r'\[(P_\w+)\]\s*=\s*\{"[^"]+",\s*[^,]+,\s*(\w+),', read_array(definitions, "PDEF")))
    pairs = re.compile(r"(P_\w+)\s*,\s*(\w+)")
    names = re.compile(r'\{"([^"]+)",\s*(PF_\w+)\}')
    programs = names.findall(read_array(patches, "FACTORY"))
    vibe = re.search(r"FACT_VIBE\[\]\s*=\s*\{(.*?)\};", patches, re.S)
    programs += names.findall(vibe[1])
    if len(programs) != 70:
        raise ValueError("Expected the 70 OberVave 3.3.2 factory programs")
    overrides = dict(re.findall(r"\b(PF_\w+)\[\]\s*=\s*\{([^}]+)\}", patches))

    def seconds(value):
        return 0.0005 * 20000 ** (value / 127)

    for name, key in programs:
        patch = defaults | dict(pairs.findall(overrides[key]))

        def get(key):
            token = patch["P_" + key]
            return int(token) if token.isdecimal() else symbols[token]

        def norm(key):
            return get(key) / 127

        def pw(key):
            return 0.06 + 0.88 * norm(key)

        yield name, {
            "Wave1": get("O1_WAVE"), "Level1": norm("O1_LVL") ** 2, "Pulse": pw("O1_PW"),
            "Wave2": get("O2_WAVE"), "Level2": norm("O2_LVL") ** 2, "Pulse2": pw("O2_PW"),
            "Tune2": get("O2_SEMI") - 24 + (get("O2_FINE") - 64) / 128,
            "Osc2Contour": (get("ENV_O2") - 64) * 97 / 256,
            "Noise": norm("NOISE") ** 2, "HardSync": get("SYNC"), "CrossMod": norm("XMOD") ** 2,
            "Cutoff": midi_hz(get("CUTOFF") + 8), "Resonance": norm("RESO"),
            "Filter": get("FTYPE"), "KeyTrack": get("KEYTRK") / 128,
            "ContourAmount": (get("FENV_AMT") - 64) * 390 / (256 * 12), "Morph": norm("FMORPH"),
            "Drive": min(1, (1 + 15 * norm("DRIVE") ** 2 - 0.8) / 2.5),
            "FilterAttack": seconds(get("FA")), "FilterDecay": seconds(get("FD")),
            "FilterSustain": norm("FS"), "FilterRelease": seconds(get("FR")),
            "AmpAttack": seconds(get("AA")), "AmpDecay": seconds(get("AD")),
            "AmpSustain": norm("AS"), "AmpRelease": seconds(get("AR")),
            "LfoWave": get("LFO_SHAPE"), "Rate": 0.05 * 600 ** norm("LFO_RATE"),
            "LfoPitch": 12 * norm("LFO_PITCH") ** 2, "PWM": norm("LFO_PWM") ** 2,
            "LfoFilter": 4 * norm("LFO_FLT") ** 2, "LfoAmp": norm("LFO_AMP"),
            "PitchTarget": get("LFO_PITCH_TO"), "PwmTarget": get("LFO_PWM_TO"),
            "Retrigger": get("LFO_PHASE"), "Mode": get("MODE"),
            "Glide": 0.005 * 1000 ** norm("GLIDE") if get("GLIDE") else 0,
            "Level": norm("VOLUME") ** 2,
        }


def varint(value):
    result = bytearray()
    while value >= 128:
        result.append((value & 127) | 128)
        value >>= 7
    result.append(value)
    return bytes(result)


def integer(tag, value):
    return varint(tag << 3) + varint(value)


def message(tag, payload=b""):
    return varint(tag << 3 | 2) + varint(len(payload)) + payload


def fixed_float(tag, value):
    return varint(tag << 3 | 5) + struct.pack("<f", value)


def display_name(name):
    if name.casefold() == "init":
        return "Init Patch"  # Synth::kInitPatchName marks an initialized instrument.
    if name.isupper():
        acronyms = {"BP": "BP", "CS": "CS", "EP": "EP", "FX": "FX", "PWM": "PWM", "XMOD": "XMod"}
        return re.sub(r"[A-Z]+", lambda word: acronyms.get(word[0], word[0].capitalize()), name)
    return name


def write_patches(folder, engine, schema, presets):
    ids = {key: i for i, (key, _, _, _) in enumerate(schema)}
    macros = (
        ("Cutoff", "Cutoff", 0.65),
        ("Resonance", "Resonance", 0.65),
        ("Drive", "Drive", 0.60),
        ("Filter Env", "ContourAmount", 0.55),
        ("Osc 3 Mix", "Level3", 0.65) if folder.name == "MINI" else ("PWM", "PWM", 0.65),
        ("Release", "AmpRelease", 0.15),
        ("Velocity", None, 0),
        ("Pressure", None, 0),
    )
    macro_messages = bytearray()
    for label, key, amount in macros:
        payload = message(1, label.encode("ascii"))
        if key is not None:
            modifier = (integer(1, ids[key]) if ids[key] else b"") + fixed_float(2, amount)
            payload += integer(2, 1) + integer(3, 1) + message(4, modifier) + message(4) * 5
        else:
            payload += message(4) * 6
        macro_messages += message(8, payload)

    # Stock patches carry four empty sample slots and one empty 64-step sequence.
    arp = message(1, message(2, integer(1, 3) + integer(2, 1)) + message(3))
    steps = message(2, message(2, bytes(16))) * 64
    trailer = message(7) * 4 + bytes(macro_messages) + message(9, arp + message(2, steps))
    seen = set()
    folder.mkdir(parents=True, exist_ok=True)
    files = []
    for name, settings in presets:
        if unknown := settings.keys() - ids.keys():
            raise ValueError(f"Unknown SideTracker parameters {unknown}")
        original = unicodedata.normalize("NFKD", name).encode("ascii", "ignore").decode()
        original = original.replace("/", "-").replace("\\", "-").strip()[:31]
        name = display_name(original)
        if not name or name.casefold() in seen:
            raise ValueError(f"Empty or duplicate preset name: {name!r}")
        seen.add(name.casefold())
        params = b"".join(message(6, fixed_float(1, min(high, max(low, settings.get(key, initial)))))
                          for key, low, high, initial in schema)
        path = folder / f"{name}.patch"
        data = integer(1, engine) + params + trailer
        old_path = folder / f"{original}.patch"
        if old_path != path and old_path.is_file() and old_path.read_bytes() == data:
            # Also update the directory entry's spelling on case-insensitive disks.
            old_path.rename(path)
        else:
            path.write_bytes(data)
        files.append(path)
    print(f"{len(files)} presets in {folder}")
    return files


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fmini", type=Path, default=ROOT / "temp/fmini-1-2.5.12")
    parser.add_argument("--ober", type=Path, default=ROOT / "temp/obervave-main")
    parser.add_argument("--output", type=Path, default=ROOT / "build/SideTrackerAnalogPresets")
    parser.add_argument("--sd", type=Path, help="Also copy the patches to this SD staging directory")
    args = parser.parse_args()
    source = (ROOT / "core/synth/engines/AnalogSynthVoice.cpp").read_text()
    number = r"-?[\d.]+f?"
    descriptor = rf"\{{P::(\w+),\s*S::\w+,\s*\"[^\"]+\",\s*K::\w+,\s*({number}),\s*({number}),\s*({number})(?=[,}}])"
    output = args.output / "Patches"
    output.mkdir(parents=True, exist_ok=True)
    files = []
    for name, donor, engine, folder, loader, table in (
        ("MINI", "FMini", 9, args.fmini, mini_bank, "kMiniParams"),
        ("OBER", "Ober", 10, args.ober, ober_bank, "kOberParams"),
    ):
        definitions = read_array(source, table)
        schema = [(key, float(lo.rstrip("f")), float(hi.rstrip("f")), float(initial.rstrip("f")))
                  for key, lo, hi, initial in re.findall(descriptor, definitions)]
        if len(schema) != definitions.count("{P::"):
            raise ValueError(f"Could not read all {name} patch parameters")
        programs = loader(folder)
        if name == "OBER":
            programs = chain(programs, OBER_SHOWCASE)
        files.extend(write_patches(output / name, engine, schema, programs))
        shutil.copyfile(folder / "LICENSE", args.output / f"{donor}-LICENSE.txt")
    (args.output / "README.txt").write_text(
        "Copy Patches to the SD card root. Select MINI or OBER, then Load in the Synth editor.\n"
        "Factory preset data from FMini-1 2.5.12 (Guilherme Ruppert) and OberVave 3.3.2 (zvenson).\n"
        "OBER also includes 30 original SideTracker sounds with SEM and notch pads, filtered keys,\n"
        "tempo-synced modulation, metallic timbres, percussion, mono leads, and sustained instruments.\n"
        "Converted patches use title case and each donor's GPL-3.0-only license.\n"
        "Voice stacks, donor arpeggiator, FX sends and other firmware-specific settings are omitted.\n"
    )
    archive = args.output.parent / f"{args.output.name}.zip"
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as package:
        for name in ("README.txt", "FMini-LICENSE.txt", "Ober-LICENSE.txt"):
            package.write(args.output / name, name)
        for patch in files:
            relative = patch.relative_to(args.output)
            package.write(patch, relative)
            if args.sd is not None:
                destination = args.sd / relative
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(patch, destination)
    print(f"Pack: {archive}")


if __name__ == "__main__":
    main()
