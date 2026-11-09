#!/usr/bin/env python3
"""Regenerate the Palette showcase files and ZIP using CE's native encoder."""
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import zipfile


def main():
    root = Path(__file__).resolve().parents[1]
    source = root / "presets/palette"
    pack = root / "build/PaletteShowcase"
    settings = json.loads((source / "chains.json").read_text())
    if settings.get("format") != "PTCH v2" or settings.get("source") != "Current sample":
        raise ValueError("Expected PTCH v2 chains with the current sample as source")
    chains = settings["chains"]
    # Quoted strings and numeric fields form data for stdin, never shell/C++ code.
    lines = [str(len(chains))]
    for chain in chains:
        slot = chain["slot"]
        if type(slot) is not int:
            raise ValueError("Chain slot must be an integer")
        mix = chain.get("mix", 100)
        if type(mix) is not int or not 0 <= mix <= 100:
            raise ValueError("Chain Mix must be an integer from 0 to 100")
        lines.append(f'{slot} {json.dumps(chain["name"])} {len(chain["stages"])} {mix}')
        for stage in chain["stages"]:
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
    build = root / "build/palette-chain-generator"
    build.mkdir(parents=True, exist_ok=True)
    binary = build / "generate_palette_chains"
    subprocess.run([
        *shlex.split(os.environ.get("CXX", "c++")), "-std=c++20", "-O2",
        "-I", str(root / "core/effector"),
        str(root / "tools/generate_palette_chains.cpp"),
        str(root / "build/host-tests-release/libcore.a"), "-pthread", "-o", str(binary),
    ], check=True)
    directory = pack / "Effects/Chains"
    subprocess.run([str(binary), str(directory)], input="\n".join(lines) + "\n",
                   text=True, check=True)
    # Remove the former numbered outputs only when they are exact duplicates.
    # Other user files in the pack directory are left intact.
    for chain in chains:
        legacy = directory / f'Chain{chain["slot"]:02d}.ptchain'
        named = directory / f'{chain["name"]}.ptchain'
        if legacy.is_file() and legacy.read_bytes() == named.read_bytes():
            legacy.unlink()
    archive = root / "build/PaletteShowcase.zip"
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as output:
        for name in ("README.md", "chains.json"):
            shutil.copyfile(source / name, pack / name)
            output.write(pack / name, name)
        for chain in chains:
            path = directory / f'{chain["name"]}.ptchain'
            output.write(path, path.relative_to(pack))
    print(f"Generated {len(chains)} chains in {directory}")
    print(f"Pack: {archive}")


if __name__ == "__main__":
    main()
