#!/usr/bin/env python3
"""Build, tag, publish, and verify a SideTracker release."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
import time
from urllib.parse import urlencode
from zipfile import ZipFile


ROOT = Path(__file__).resolve().parents[1]
MODELS = ("PolyendTracker", "TrackerPlus", "TrackerMini")
VERSION_PATTERN = re.compile(r"(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)")
USER_AGENT = "SideTracker release tool"


def run(*args, capture=False):
    result = subprocess.run(args, cwd=ROOT, check=True, text=True, capture_output=capture)
    return result.stdout.strip() if capture else None


def git(*args):
    return run("git", *args, capture=True)


def model_versions(text):
    versions = []
    for model in ("ORIGINAL", "PLUS", "MINI"):
        values = []
        for field in ("MAJOR", "MINOR", "PATCH"):
            match = re.search(rf"^{model}_VER_{field}\s*:=\s*(\d+)$", text, re.M)
            if match is None:
                raise ValueError(f"Missing {model}_VER_{field} in version.mk")
            values.append(int(match[1]))
        versions.append(tuple(values))
    if len(set(versions)) != 1:
        raise ValueError("Firmware versions must match across all three models")
    return versions[0]


def bump_version(version, current, text):
    for part, number in zip(("MAJOR", "MINOR", "PATCH"), version):
        text = re.sub(
            rf"^((?:ORIGINAL|PLUS|MINI)_VER_{part}\s*:=\s*)\d+$",
            rf"\g<1>{number}", text, flags=re.M,
        )
    (ROOT / "version.mk").write_text(text)
    readme = ROOT / "README.md"
    old = ".".join(map(str, current))
    new = ".".join(map(str, version))
    readme.write_text(readme.read_text().replace(f"_ST_{old}.ptf", f"_ST_{new}.ptf"))
    run("git", "add", "--", "version.mk", "README.md")
    run("git", "commit", "-m", f"Release SideTracker {new}")


def verify_archive(path, version):
    with ZipFile(path) as bundle:
        if bad_entry := bundle.testzip():
            raise ValueError(f"Corrupt archive entry: {bad_entry}")
        for model in MODELS:
            name = f"{model}_ST_{version}.ptf"
            firmware = bundle.read(name)
            if not firmware.startswith(b":") or firmware != bundle.read(f"SD/Firmware/{name}"):
                raise ValueError(f"Invalid or mismatched firmware: {name}")
        for folder, suffix in (("Themes", ".json"), ("Fonts", ".ptfont")):
            for source in (ROOT / folder.lower()).glob(f"*{suffix}"):
                if bundle.read(f"SD/System/{folder}/{source.name}") != source.read_bytes():
                    raise ValueError(f"Archive contains the wrong {source.name}")
        for effect in (ROOT / "editor-fx-sdk/effects").glob("*.c"):
            if not bundle.getinfo(f"SD/System/Effects/{effect.stem}.ptfx").file_size:
                raise ValueError(f"Empty effect plugin: {effect.stem}")
    print(f"Verified {path.name}", flush=True)


def api(repo, path):
    response = subprocess.run(
        ["curl", "--silent", "--show-error", "--connect-timeout", "8", "--max-time", "20",
         "-H", "Accept: application/vnd.github+json", "-A", USER_AGENT,
         "--write-out", "\n%{http_code}", f"https://api.github.com/repos/{repo}/{path}"],
        check=False, text=True, capture_output=True,
    )
    if response.returncode:
        raise OSError(response.stderr.strip())
    body, status = response.stdout.rsplit("\n", 1)
    if status == "404":
        return {}
    if status != "200":
        raise OSError(f"GitHub returned HTTP {status}")
    return json.loads(body)


def wait_for_release(repo, tag, commit, archive_name):
    query = urlencode({"event": "push", "head_sha": commit})
    deadline = time.monotonic() + 35 * 60
    previous_status = None
    while time.monotonic() < deadline:
        try:
            runs = api(repo, f"actions/workflows/build.yml/runs?{query}").get("workflow_runs", [])
            workflow = next((item for item in runs if item["head_branch"] == tag), None)
            if workflow:
                status = workflow["status"]
                if status == "completed":
                    if workflow["conclusion"] != "success":
                        raise RuntimeError(f"GitHub release workflow {workflow['conclusion']}: {workflow['html_url']}")
                    release = api(repo, f"releases/tags/{tag}")
                    if release and not release["draft"]:
                        for asset in release["assets"]:
                            if asset["name"] == archive_name and asset["state"] == "uploaded" and asset["size"]:
                                return release, asset
                message = f"GitHub workflow {status}: {workflow['html_url']}"
            else:
                message = f"Waiting for the GitHub workflow for {tag}"
        except OSError as error:
            message = f"Waiting for GitHub: {error}"
        if message != previous_status:
            print(message, flush=True)
            previous_status = message
        time.sleep(45)
    raise RuntimeError(f"Timed out waiting for {tag}; check GitHub Actions")


def verify_download(asset, version):
    with tempfile.TemporaryDirectory(prefix="sidetracker-release-") as directory:
        archive = Path(directory) / asset["name"]
        run("curl", "--fail", "--location", "--silent", "--show-error", "--retry", "2",
            "--connect-timeout", "8", "--max-time", "60", "-A", USER_AGENT,
            "--output", str(archive), asset["browser_download_url"])
        if archive.stat().st_size != asset["size"]:
            raise ValueError("Downloaded archive size differs from the published asset")
        digest = asset.get("digest")
        if digest and digest != "sha256:" + hashlib.sha256(archive.read_bytes()).hexdigest():
            raise ValueError("Downloaded archive failed the GitHub checksum")
        verify_archive(archive, version)


def release(args):
    version = tuple(map(int, args.version.split(".")))
    tag = f"v{args.version}"
    ref = f"refs/tags/{tag}"
    notes_file = args.notes.resolve()
    if not notes_file.read_text().strip():
        raise ValueError("Supply a nonempty release notes file")
    if git("branch", "--show-current") != "main" or git("status", "--porcelain"):
        raise ValueError("Start the release from a clean main branch")

    remote = git("remote", "get-url", "origin")
    repo = re.fullmatch(r"(?:https://github\.com/|git@github\.com:|ssh://git@github\.com/)([\w.-]+/[\w.-]+)",
                        remote.removesuffix(".git"))
    if not repo:
        raise ValueError("Origin must be a GitHub repository")
    run("git", "fetch", "--no-tags", "origin", "main")
    git("merge-base", "--is-ancestor", "FETCH_HEAD", "HEAD")

    text = (ROOT / "version.mk").read_text()
    current = model_versions(text)
    local_tag = git("tag", "--list", tag)
    remote_tag = git("ls-remote", "--tags", "origin", ref)
    if args.resume:
        if current != version:
            raise ValueError("--resume requires version.mk to already match the requested version")
        if local_tag and not remote_tag and git("rev-parse", f"{tag}^{{commit}}") != git("rev-parse", "HEAD"):
            raise ValueError(f"{tag} belongs to a different commit")
        if remote_tag and (not local_tag or remote_tag.split()[0] != git("rev-parse", ref)):
            raise ValueError(f"The remote {tag} differs from the local tag")
    else:
        if local_tag or remote_tag or version <= current:
            raise ValueError("Choose a new version greater than the current version")
        bump_version(version, current, text)

    archive_name = f"SideTracker_{args.version}.zip"
    commit = git("rev-parse", f"{tag}^{{commit}}" if remote_tag else "HEAD")
    if not remote_tag:
        run("make", f"-j{args.jobs}", "host-test")
        run("make", f"-j{args.jobs}", "desktop-check")
        run("make", "release-zip", f"RELEASE_VERSION={args.version}", f"NPROC={args.jobs}")
        verify_archive(ROOT / "build" / archive_name, args.version)
        if git("rev-parse", "HEAD") != commit or git("status", "--porcelain"):
            raise ValueError("The checkout changed while preparing the release")
        if not local_tag:
            run("git", "tag", "-a", tag, "-F", str(notes_file), commit)
        run("git", "push", "--atomic", "origin", "HEAD:refs/heads/main", ref)

    published, asset = wait_for_release(repo[1], tag, commit, archive_name)
    verify_download(asset, args.version)
    print(published["html_url"], flush=True)
    print(asset["browser_download_url"], flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("version", help="New firmware version, for example 0.0.10")
    parser.add_argument("--notes", required=True, type=Path, help="Markdown release notes")
    parser.add_argument("--jobs", type=int, default=min(8, os.cpu_count() or 1))
    parser.add_argument("--resume", action="store_true", help="Continue an already bumped version")
    args = parser.parse_args()
    if not VERSION_PATTERN.fullmatch(args.version) or any(int(part) > 255 for part in args.version.split(".")):
        parser.error("version must be MAJOR.MINOR.PATCH with components from 0 to 255")
    if args.jobs < 1:
        parser.error("--jobs must be at least 1")
    try:
        release(args)
    except (OSError, ValueError, RuntimeError, KeyError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"Release stopped: {error}\n")


if __name__ == "__main__":
    main()
