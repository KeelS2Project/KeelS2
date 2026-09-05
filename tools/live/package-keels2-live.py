#!/usr/bin/env python3

from __future__ import annotations

import argparse
import datetime as dt
import gzip
import hashlib
import json
import os
import re
import shutil
import stat
import subprocess
import sys
import tarfile
import tempfile
import zipfile
from pathlib import Path


BUILD_ID = "25000182"
PROFILES = {
    "linux-x86_64": {
        "label": "linux",
        "directory": "linuxsteamrt64",
        "extension": ".so",
        "size": 40540568,
        "fnv": "542cc63d17821e66",
        "profile": "cs2-2000899-linuxsteamrt64-40540568-542cc63d17821e66",
        "archive": "tar.gz",
    },
    "windows-x86_64": {
        "label": "windows",
        "directory": "win64",
        "extension": ".dll",
        "size": 33002648,
        "fnv": "43286dc938300339",
        "profile": "cs2-2000899-win64-33002648-43286dc938300339",
        "archive": "zip",
    },
}


class PackageFailure(RuntimeError):
    pass


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def platform_key() -> str:
    if os.name == "nt":
        return "windows-x86_64"
    if sys.platform.startswith("linux"):
        return "linux-x86_64"
    raise PackageFailure(f"unsupported platform: {sys.platform}")


def git_output(repo: Path, *arguments: str) -> str:
    result = subprocess.run(
        ["git", *arguments], cwd=repo, text=True, capture_output=True, check=False)
    if result.returncode != 0:
        raise PackageFailure(result.stderr.strip() or "git command failed")
    return result.stdout.strip()


def revision(repo: Path) -> str:
    head = git_output(repo, "rev-parse", "--short=7", "HEAD")
    status = git_output(repo, "status", "--porcelain", "--untracked-files=all")
    if not status:
        return head
    digest = hashlib.sha256()
    diff = subprocess.run(
        ["git", "diff", "--binary", "HEAD"], cwd=repo, capture_output=True, check=True).stdout
    digest.update(diff)
    untracked = git_output(repo, "ls-files", "--others", "--exclude-standard").splitlines()
    for name in sorted(untracked):
        digest.update(name.encode("utf-8"))
        digest.update(b"\0")
        path = repo / name
        if path.is_file():
            digest.update(path.read_bytes())
    return f"{head}-dirty-{digest.hexdigest()[:8]}"


def commit_epoch(repo: Path) -> int:
    value = git_output(repo, "show", "-s", "--format=%ct", "HEAD")
    try:
        return int(value)
    except ValueError as error:
        raise PackageFailure("invalid Git commit timestamp") from error


def artifact(build: Path, configuration: str, name: str) -> Path:
    candidates = (build / name, build / configuration / name)
    matches = [candidate for candidate in candidates if candidate.is_file()]
    if not matches:
        raise PackageFailure(f"build artifact was not found: {name}")
    return matches[0]


def copy(source: Path, destination: Path) -> None:
    if not source.is_file():
        raise PackageFailure(f"source file was not found: {source}")
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, destination)


def write_manifest(root: Path) -> None:
    paths = sorted(
        path for path in root.rglob("*")
        if path.is_file() and path.name != "MANIFEST.txt")
    text = "".join(
        f"{sha256(path)}  {path.relative_to(root).as_posix()}\n" for path in paths)
    (root / "MANIFEST.txt").write_text(text, encoding="utf-8", newline="\n")


def executable(path: Path) -> bool:
    return path.suffix in (".sh", ".so", ".exe") or path.name == "keels2_compatibility_review"


def zip_time(epoch: int) -> tuple[int, int, int, int, int, int]:
    value = dt.datetime.fromtimestamp(max(epoch, 315532800), tz=dt.timezone.utc)
    return value.year, value.month, value.day, value.hour, value.minute, value.second - value.second % 2


def make_zip(root: Path, archive: Path, epoch: int) -> None:
    with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as target:
        for path in sorted(item for item in root.rglob("*") if item.is_file()):
            name = (Path(root.name) / path.relative_to(root)).as_posix()
            info = zipfile.ZipInfo(name, zip_time(epoch))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.create_system = 3
            mode = 0o755 if executable(path) else 0o644
            info.external_attr = (stat.S_IFREG | mode) << 16
            info.flag_bits |= 0x800
            target.writestr(info, path.read_bytes(), compress_type=zipfile.ZIP_DEFLATED, compresslevel=9)


def make_tar(root: Path, archive: Path, epoch: int) -> None:
    with archive.open("wb") as raw:
        with gzip.GzipFile(filename="", mode="wb", fileobj=raw, compresslevel=9, mtime=epoch) as compressed:
            with tarfile.open(fileobj=compressed, mode="w", format=tarfile.GNU_FORMAT) as target:
                for path in sorted(item for item in root.rglob("*") if item.is_file()):
                    data = path.read_bytes()
                    info = tarfile.TarInfo((Path(root.name) / path.relative_to(root)).as_posix())
                    info.size = len(data)
                    info.mtime = epoch
                    info.mode = 0o755 if executable(path) else 0o644
                    info.uid = 0
                    info.gid = 0
                    info.uname = "root"
                    info.gname = "root"
                    import io
                    target.addfile(info, io.BytesIO(data))


def create_archive(root: Path, output: Path, kind: str, epoch: int) -> Path:
    suffix = ".zip" if kind == "zip" else ".tar.gz"
    archive = output / (root.name + suffix)
    repeated = output / (root.name + ".repeat" + suffix)
    if kind == "zip":
        make_zip(root, archive, epoch)
        make_zip(root, repeated, epoch)
    else:
        make_tar(root, archive, epoch)
        make_tar(root, repeated, epoch)
    if sha256(archive) != sha256(repeated):
        raise PackageFailure(f"archive is not deterministic: {archive.name}")
    repeated.unlink()
    (output / (archive.name + ".sha256")).write_text(
        f"{sha256(archive)}  {archive.name}\n", encoding="utf-8", newline="\n")
    return archive


def profile_bundle(
    repo: Path,
    build: Path,
    output: Path,
    configuration: str,
    key: str,
    source_revision: str,
    epoch: int,
) -> Path:
    profile = PROFILES[key]
    name = f"keels2-cs2-current-{profile['label']}-profile-capture-r1-{source_revision}"
    root = output / name
    if root.exists():
        shutil.rmtree(root)
    root.mkdir(parents=True)
    if key == "windows-x86_64":
        script_name = "capture-keels2-cs2-profile-windows.ps1"
        tool_name = "keels2_compatibility_review.exe"
        run_text = (
            "Expand the ZIP, open PowerShell in this directory, and run:\n"
            ".\\capture-keels2-cs2-profile-windows.ps1 -ServerRoot C:\\path\\to\\cs2_dedi\n")
    else:
        script_name = "capture-keels2-cs2-profile-linux.sh"
        tool_name = "keels2_compatibility_review"
        run_text = (
            "Extract the archive, enter this directory, and run:\n"
            "chmod +x capture-keels2-cs2-profile-linux.sh\n"
            "./capture-keels2-cs2-profile-linux.sh /path/to/cs2_dedi\n")
    copy(repo / "tools" / "live" / script_name, root / script_name)
    copy(build / "package" / "addons" / "keels2" / "tools" /
         str(profile["directory"]) / tool_name, root / tool_name)
    (root / "EXPECTED_BUILD.txt").write_text(BUILD_ID + "\n", encoding="utf-8", newline="\n")
    (root / "RUN.txt").write_text(run_text, encoding="utf-8", newline="\n")
    write_manifest(root)
    return create_archive(root, output, str(profile["archive"]), epoch)


def live_bundle(
    repo: Path,
    build: Path,
    output: Path,
    configuration: str,
    key: str,
    source_revision: str,
    epoch: int,
) -> Path:
    profile = PROFILES[key]
    name = f"keels2-09-{profile['label']}-live-gate-r1-{source_revision}"
    root = output / name
    if root.exists():
        shutil.rmtree(root)
    root.mkdir(parents=True)
    copy(repo / "tools" / "live" / "run-keels2-09-live-gate.py",
         root / "run-keels2-09-live-gate.py")
    if key == "windows-x86_64":
        wrapper = "run-keels2-09-live-gate-windows.ps1"
        run_text = (
            "Expand the ZIP, stop any running server, open PowerShell in this directory, and run:\n"
            ".\\run-keels2-09-live-gate-windows.ps1 -ServerRoot C:\\path\\to\\cs2_dedi\n")
    else:
        wrapper = "run-keels2-09-live-gate-linux.sh"
        run_text = (
            "Extract the archive, stop any running server, enter this directory, and run:\n"
            "chmod +x run-keels2-09-live-gate-linux.sh\n"
            "./run-keels2-09-live-gate-linux.sh /path/to/cs2_dedi\n")
    copy(repo / "tools" / "live" / wrapper, root / wrapper)
    package_root = build / "package" / "addons" / "keels2"
    payload = root / "payload" / "addons" / "keels2"
    for document in ("LICENSE", "THIRD_PARTY_NOTICES.md"):
        copy(package_root / document, payload / document)
    shutil.copytree(package_root / "bin" / str(profile["directory"]),
                    payload / "bin" / str(profile["directory"]))
    shutil.copytree(package_root / "compatibility", payload / "compatibility")
    payload.joinpath("plugins", str(profile["directory"])).mkdir(parents=True)

    extension = str(profile["extension"])
    package_plugins = package_root / "plugins" / str(profile["directory"])
    fixtures = root / "fixtures"
    fixture_sources = {
        "basic" + extension: package_plugins / ("keels2_basic" + extension),
        "no_damage" + extension: package_plugins / ("keels2_no_damage" + extension),
        "failing" + extension: artifact(build, configuration, "keels2_failing_plugin" + extension),
        "keelhook_target" + extension: artifact(build, configuration, "01_keelhook_live_target" + extension),
        "keelhook_peer" + extension: artifact(build, configuration, "02_keelhook_peer" + extension),
        "published_provider" + extension: artifact(
            build, configuration, "keels2_published_service_provider_plugin" + extension),
        "published_consumer" + extension: artifact(
            build, configuration, "keels2_published_service_consumer_plugin" + extension),
        "source2_live" + extension: artifact(build, configuration, "01_source2_live" + extension),
        "schema_live" + extension: artifact(build, configuration, "01_schema_entity_live" + extension),
        "lifecycle_live" + extension: artifact(build, configuration, "01_lifecycle_live" + extension),
        "callback_observer" + extension: artifact(
            build, configuration, "keels2_05e_observer" + extension),
        "callback_decision_a" + extension: artifact(
            build, configuration, "keels2_05e_decision_a" + extension),
        "callback_decision_b" + extension: artifact(
            build, configuration, "keels2_05e_decision_b" + extension),
    }
    for destination, source in fixture_sources.items():
        copy(source, fixtures / destination)

    gate = {
        "schema": 1,
        "build_id": BUILD_ID,
        "platform": key,
        "platform_label": profile["label"],
        "platform_directory": profile["directory"],
        "plugin_extension": extension,
        "profile": profile["profile"],
        "server_size": profile["size"],
        "server_fnv1a64": profile["fnv"],
        "revision": source_revision,
        "successful_reload_requirement": 102,
        "rollback_requirement": 5,
    }
    (root / "GATE.json").write_text(
        json.dumps(gate, indent=2, sort_keys=True) + "\n", encoding="utf-8", newline="\n")
    (root / "EXPECTED_BUILD.txt").write_text(BUILD_ID + "\n", encoding="utf-8", newline="\n")
    (root / "RUN.txt").write_text(run_text, encoding="utf-8", newline="\n")
    write_manifest(root)
    return create_archive(root, output, str(profile["archive"]), epoch)


def self_test() -> None:
    with tempfile.TemporaryDirectory(prefix="keels2-live-package-test-") as temporary_text:
        temporary = Path(temporary_text)
        root = temporary / "bundle"
        root.mkdir()
        (root / "script.sh").write_text("#!/usr/bin/env bash\nexit 0\n", encoding="utf-8")
        (root / "data.txt").write_text("data\n", encoding="utf-8")
        write_manifest(root)
        first = create_archive(root, temporary, "tar.gz", 1785900000)
        if not first.is_file() or len((root / "MANIFEST.txt").read_text().splitlines()) != 2:
            raise PackageFailure("live packaging self-test failed")
        first.unlink()
        (temporary / (first.name + ".sha256")).unlink()
        second = create_archive(root, temporary, "zip", 1785900000)
        if not second.is_file():
            raise PackageFailure("live ZIP packaging self-test failed")
    print("KeelS2 live packaging self-test: PASS")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo")
    parser.add_argument("--build-dir")
    parser.add_argument("--output-dir")
    parser.add_argument("--configuration", default="Release")
    parser.add_argument("--revision")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    try:
        if args.self_test:
            self_test()
            return 0
        if not args.build_dir or not args.output_dir:
            raise PackageFailure("--build-dir and --output-dir are required")
        repo = Path(args.repo).resolve() if args.repo else Path(__file__).resolve().parents[2]
        build = Path(args.build_dir).expanduser().resolve()
        output = Path(args.output_dir).expanduser().resolve()
        if not (repo / ".git").is_dir() or not build.is_dir():
            raise PackageFailure("repository or build directory was not found")
        output.mkdir(parents=True, exist_ok=True)
        key = platform_key()
        source_revision = args.revision or revision(repo)
        if not re.fullmatch(r"[0-9A-Za-z.-]+", source_revision):
            raise PackageFailure("revision contains unsupported characters")
        epoch = commit_epoch(repo)
        profile_archive = profile_bundle(
            repo, build, output, args.configuration, key, source_revision, epoch)
        live_archive = live_bundle(
            repo, build, output, args.configuration, key, source_revision, epoch)
        print(f"Profile capture: {profile_archive}")
        print(f"SHA-256: {sha256(profile_archive)}")
        print(f"Live gate: {live_archive}")
        print(f"SHA-256: {sha256(live_archive)}")
        return 0
    except (PackageFailure, OSError, subprocess.SubprocessError) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
