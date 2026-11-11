#!/usr/bin/env python3
"""Regenerate the StPalette showcase files and ZIP using SideTracker's native encoder."""
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import zipfile


def main():
    root = Path(__file__).resolve().parents[1]
    source = root / "presets/stpalette"
    pack = root / "build/StPaletteShowcase"
    settings = json.loads((source / "presets.json").read_text())
    if settings.get("format") != "PTPR v1" or settings.get("source") != "Current sample":
        raise ValueError("Expected PTPR v1 presets with the current sample as source")
    presets = settings["presets"]
    # Quoted strings and numeric fields form data for stdin, never shell/C++ code.
    lines = [str(len(presets))]
    for preset in presets:
        slot = preset["slot"]
        if type(slot) is not int:
            raise ValueError("Preset slot must be an integer")
        mix = preset.get("mix", 100)
        if type(mix) is not int or not 0 <= mix <= 100:
            raise ValueError("Preset Mix must be an integer from 0 to 100")
        lines.append(f'{slot} {json.dumps(preset["name"])} {len(preset["stages"])} {mix}')
        for stage in preset["stages"]:
            values = [stage[key] for key in ("amount", "macro", "drift", "tail")]
            if any(type(value) not in (int, float) for value in values):
                raise ValueError("Amount, Macro, Drift and Tail must be numbers")
            lines.append(" ".join([
                json.dumps(stage["category"]), json.dumps(stage["type"]),
                *(str(value) for value in values[:3]), json.dumps(stage["sync"]), str(values[3]),
            ]))
    # Reuse the portable core library and its dependency tracking. No SDL or ARM
    # toolchain is needed; the release library avoids sanitizer runtime requirements.
    subprocess.run([
        "make", "-s", "-C", str(root / "tests"), "-j8", "SANITIZE=", "STEREO=0",
        "../build/host-tests-release/libcore.a",
    ], check=True)
    build = root / "build/stpalette-preset-generator"
    build.mkdir(parents=True, exist_ok=True)
    binary = build / "generate_stpalette_presets"
    subprocess.run([
        *shlex.split(os.environ.get("CXX", "c++")), "-std=c++20", "-O2",
        "-I", str(root / "core/effector"),
        str(root / "tools/generate_stpalette_presets.cpp"),
        str(root / "build/host-tests-release/libcore.a"), "-pthread", "-o", str(binary),
    ], check=True)
    directory = pack / "Effects/Presets"
    subprocess.run([str(binary), str(directory)], input="\n".join(lines) + "\n",
                   text=True, check=True)
    archive = root / "build/StPaletteShowcase.zip"
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as output:
        for name in ("README.md", "presets.json"):
            shutil.copyfile(source / name, pack / name)
            output.write(pack / name, name)
        for preset in presets:
            path = directory / f'{preset["name"]}.ptpreset'
            output.write(path, path.relative_to(pack))
    print(f"Generated {len(presets)} presets in {directory}")
    print(f"Pack: {archive}")


if __name__ == "__main__":
    main()
