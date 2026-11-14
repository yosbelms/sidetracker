#!/usr/bin/env python3
"""Stage the unchanged synth patch banks from the published SideTracker 0.0.3 ZIP."""

import argparse
import hashlib
from io import BytesIO
from pathlib import Path, PurePosixPath
from zipfile import ZipFile

ARCHIVE_SHA256 = "2e42144c574df8801f655aee1fa2074d20a47a7b1e683c16d448d114d49aae82"
BANKS = {"MINI": 200, "OBER": 100}
LICENSES = ("FMini-LICENSE.txt", "Ober-LICENSE.txt", "AnalogPresets.txt")


def stage_patches(archive: Path, staging: Path):
    data = archive.read_bytes()
    if hashlib.sha256(data).hexdigest() != ARCHIVE_SHA256:
        raise ValueError("The synth patch source must be the published SideTracker_0.0.3.zip")

    with ZipFile(BytesIO(data)) as source:
        names = [PurePosixPath(name) for name in source.namelist()]
        patches = []
        for engine, expected in BANKS.items():
            root = PurePosixPath("SD/Patches") / engine
            bank = [name for name in names if name.parent == root and name.suffix == ".patch"]
            if len(set(bank)) != expected or len(bank) != expected:
                raise ValueError(f"Expected {expected} {engine} patches in the release archive")
            patches.extend(bank)

        for name in (*patches, *(PurePosixPath("Licenses") / file for file in LICENSES)):
            destination = staging / name
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(source.read(str(name)))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive")
    parser.add_argument("staging", type=Path)
    args = parser.parse_args()
    if not args.archive:
        parser.error("Provide SYNTH_PATCH_ARCHIVE or make the local synth patch sources available")
    stage_patches(Path(args.archive), args.staging)
