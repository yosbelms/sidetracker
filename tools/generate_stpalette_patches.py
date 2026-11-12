#!/usr/bin/env python3
"""Regenerate the STPalette showcase files and ZIP using SideTracker's native encoder."""
import argparse
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import zipfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sd", type=Path, help="Also copy the patches to this SD staging directory")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    source = root / "patches/stpalette"
    pack = root / "build/STPaletteShowcase"
    settings = json.loads((source / "patches.json").read_text())
    if settings.get("format") != "PTPR v1" or settings.get("source") != "Current sample":
        raise ValueError("Expected PTPR v1 patches with the current sample as source")
    patches = settings["patches"]
    # Quoted strings and numeric fields form data for stdin, never shell/C++ code.
    lines = [str(len(patches))]
    for patch in patches:
        slot = patch["slot"]
        if type(slot) is not int:
            raise ValueError("Patch slot must be an integer")
        mix = patch.get("mix", 100)
        if type(mix) is not int or not 0 <= mix <= 100:
            raise ValueError("Patch Mix must be an integer from 0 to 100")
        lines.append(f'{slot} {json.dumps(patch["name"])} {len(patch["stages"])} {mix}')
        for stage in patch["stages"]:
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
    build = root / "build/stpalette-patch-generator"
    build.mkdir(parents=True, exist_ok=True)
    binary = build / "generate_stpalette_patches"
    subprocess.run([
        *shlex.split(os.environ.get("CXX", "c++")), "-std=c++20", "-O2",
        "-I", str(root / "core/effector"),
        str(root / "tools/generate_stpalette_patches.cpp"),
        str(root / "build/host-tests-release/libcore.a"), "-pthread", "-o", str(binary),
    ], check=True)
    directory = pack / "Effects/Patches"
    subprocess.run([str(binary), str(directory)], input="\n".join(lines) + "\n",
                   text=True, check=True)
    archive = root / "build/STPaletteShowcase.zip"
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as output:
        for name in ("README.md", "patches.json"):
            shutil.copyfile(source / name, pack / name)
            output.write(pack / name, name)
        for patch in patches:
            path = directory / f'{patch["name"]}.patch'
            relative = path.relative_to(pack)
            output.write(path, relative)
            if args.sd is not None:
                destination = args.sd / relative
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(path, destination)
    print(f"Generated {len(patches)} patches in {directory}")
    print(f"Pack: {archive}")


if __name__ == "__main__":
    main()
