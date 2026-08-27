#!/usr/bin/env python3

from __future__ import annotations

import argparse
import datetime as dt
import gzip
import hashlib
import json
import mimetypes
import os
import platform
import re
import shutil
import stat
import struct
import subprocess
import sys
import tarfile
import tempfile
import urllib.error
import urllib.parse
import urllib.request
import zipfile
from pathlib import Path, PurePosixPath


TOOL_VERSION = "1"
SCHEMA_VERSION = 1
PROJECT = "KeelS2"
DEFAULT_BRANCH = "release-engineering"
DEFAULT_BASE = "main"
EXPECTED_REPOSITORY = "KeelS2Project/KeelS2"
WORKFLOW_OLD = """      - name: Configure
        run: cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DKEELS2_GAME=cs2 -DBUILD_TESTING=ON
      - name: Build
        run: cmake --build build --config Release
      - name: Test
        run: ctest --test-dir build -C Release --output-on-failure
"""
WORKFLOW_NEW = """      - name: Build, test, and verify the release path
        run: python tools/release.py build --project-suffix ci --configuration Release
"""
VERSION_PATTERN = re.compile(r"^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(?:-([0-9A-Za-z](?:[0-9A-Za-z.-]*[0-9A-Za-z])?))?$")
PROJECT_PATTERN = re.compile(r"project\(\s*KeelS2\s+VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)\s+LANGUAGES(?:\s+[A-Za-z0-9_+-]+)+\s*\)")


class Stop(RuntimeError):
    pass


def stop(message: str) -> None:
    raise Stop(f"STOP: {message}")


def say(message: str = "") -> None:
    print(message, flush=True)


def run(
    command: list[str],
    cwd: Path | None = None,
    capture: bool = False,
    input_text: str | None = None,
    check: bool = True,
) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(
        command,
        cwd=str(cwd) if cwd else None,
        text=True,
        input=input_text,
        stdout=subprocess.PIPE if capture else None,
        stderr=subprocess.PIPE if capture else None,
        check=False,
    )
    if check and result.returncode != 0:
        if capture:
            if result.stdout:
                sys.stdout.write(result.stdout)
            if result.stderr:
                sys.stderr.write(result.stderr)
        stop(f"command failed with status {result.returncode}: {' '.join(command)}")
    return result


def output(command: list[str], cwd: Path | None = None) -> str:
    return run(command, cwd=cwd, capture=True).stdout.strip()


def require_command(name: str) -> str:
    path = shutil.which(name)
    if not path:
        stop(f"required command is unavailable: {name}")
    return path


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def atomic_write(path: Path, data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary_name = tempfile.mkstemp(prefix=f".{path.name}.", dir=path.parent)
    temporary = Path(temporary_name)
    try:
        with os.fdopen(descriptor, "wb") as handle:
            handle.write(data)
            handle.flush()
            os.fsync(handle.fileno())
        os.replace(temporary, path)
        if os.name != "nt":
            path.chmod(0o644)
    finally:
        if temporary.exists():
            temporary.unlink()


def atomic_text(path: Path, text: str) -> None:
    atomic_write(path, text.encode("utf-8"))


def strict_json(path: Path) -> dict[str, object]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        stop(f"could not read JSON file {path}: {error}")
    if not isinstance(value, dict):
        stop(f"JSON root is not an object: {path}")
    return value


def validate_version(version: str) -> str:
    if not VERSION_PATTERN.fullmatch(version):
        stop(f"invalid release version: {version}")
    if ".." in version or version.endswith("."):
        stop(f"invalid release version: {version}")
    return version


def project_version(repo: Path) -> str:
    cmake = repo / "CMakeLists.txt"
    try:
        content = cmake.read_text(encoding="utf-8")
    except OSError as error:
        stop(f"could not read {cmake}: {error}")
    match = PROJECT_PATTERN.search(content)
    if not match:
        stop("could not determine the KeelS2 project version")
    return match.group(1)


def resolve_version(repo: Path, version: str | None, project_suffix: str | None) -> str:
    base = project_version(repo)
    if project_suffix:
        if not re.fullmatch(r"[0-9A-Za-z](?:[0-9A-Za-z.-]*[0-9A-Za-z])?", project_suffix):
            stop(f"invalid project suffix: {project_suffix}")
        resolved = f"{base}-{project_suffix}"
    elif version:
        resolved = validate_version(version)
    else:
        stop("a release version or project suffix is required")
    if resolved.split("-", 1)[0] != base:
        stop(f"release version {resolved} does not match CMake project version {base}")
    return resolved


def validate_repo(path: Path) -> Path:
    repo = path.expanduser().resolve()
    if not (repo / ".git").exists():
        stop(f"not a Git repository: {repo}")
    if not (repo / "CMakeLists.txt").is_file():
        stop(f"KeelS2 CMakeLists.txt was not found: {repo}")
    project_version(repo)
    return repo


def discover_repo(value: str | None) -> Path:
    if value:
        return validate_repo(Path(value))
    result = run(["git", "rev-parse", "--show-toplevel"], capture=True, check=False)
    if result.returncode != 0:
        stop("run this command inside the KeelS2 repository or pass --repo")
    return validate_repo(Path(result.stdout.strip()))


def git(repo: Path, *arguments: str, capture: bool = True, check: bool = True) -> str:
    result = run(["git", *arguments], cwd=repo, capture=capture, check=check)
    return result.stdout.strip() if capture else ""


def working_tree_status(repo: Path) -> str:
    return run(
        ["git", "status", "--porcelain", "--untracked-files=all"],
        cwd=repo,
        capture=True,
    ).stdout


def git_clean(repo: Path) -> None:
    status_text = working_tree_status(repo)
    if status_text.strip():
        sys.stdout.write(status_text)
        stop("repository is not clean")


def normalize_repository_url(url: str) -> str:
    value = url.strip()
    patterns = (
        r"https?://github\.com/([^/]+)/([^/]+?)(?:\.git)?/?$",
        r"git@github\.com:([^/]+)/([^/]+?)(?:\.git)?$",
        r"ssh://git@github\.com/([^/]+)/([^/]+?)(?:\.git)?/?$",
    )
    for pattern_text in patterns:
        match = re.fullmatch(pattern_text, value, re.IGNORECASE)
        if match:
            return f"{match.group(1)}/{match.group(2)}"
    stop(f"unsupported GitHub origin URL: {url}")
    return ""


def origin_repository(repo: Path) -> str:
    return normalize_repository_url(git(repo, "remote", "get-url", "origin"))


def current_commit(repo: Path) -> str:
    return git(repo, "rev-parse", "HEAD")


def commit_timestamp(repo: Path, commit: str) -> int:
    value = git(repo, "show", "-s", "--format=%ct", commit)
    try:
        result = int(value)
    except ValueError:
        stop(f"invalid commit timestamp: {value}")
    if result < 315532800:
        stop(f"commit timestamp is outside the supported range: {result}")
    return result


def detect_platform() -> tuple[str, str, str]:
    if struct.calcsize("P") != 8:
        stop("release tooling supports only 64-bit hosts")
    if platform.machine().lower() not in ("amd64", "x86_64"):
        stop(f"release tooling supports only x86-64 hosts: {platform.machine()}")
    if sys.platform == "win32":
        return "windows-x86_64", "win64", "zip"
    if sys.platform.startswith("linux"):
        return "linux-x86_64", "linuxsteamrt64", "tar.gz"
    stop(f"unsupported release host: {sys.platform}")
    return "", "", ""


def package_entries(platform_key: str) -> tuple[str, ...]:
    if platform_key == "windows-x86_64":
        return (
            "addons/keels2/LICENSE",
            "addons/keels2/THIRD_PARTY_NOTICES.md",
            "addons/keels2/compatibility/cs2-2000884-linuxsteamrt64.accepted.tsv",
            "addons/keels2/compatibility/cs2-2000884-win64.accepted.tsv",
            "addons/keels2/compatibility/cs2-2000885-linuxsteamrt64.accepted.tsv",
            "addons/keels2/compatibility/cs2-2000885-win64.accepted.tsv",
            "addons/keels2/compatibility/cs2-2000888-linuxsteamrt64.accepted.tsv",
            "addons/keels2/compatibility/cs2-2000888-win64.accepted.tsv",
            "addons/keels2/bin/win64/keels2_host.dll",
            "addons/keels2/bin/win64/server.dll",
            "addons/keels2/plugins/win64/keels2_basic.dll",
            "addons/keels2/plugins/win64/keels2_callbacks.dll",
            "addons/keels2/plugins/win64/keels2_entities.dll",
            "addons/keels2/plugins/win64/keels2_hooks.dll",
            "addons/keels2/plugins/win64/keels2_lifecycle.dll",
            "addons/keels2/plugins/win64/keels2_runtime.dll",
            "addons/keels2/plugins/win64/keels2_sample.dll",
            "addons/keels2/tools/win64/keels2_compatibility_review.exe",
        )
    if platform_key == "linux-x86_64":
        return (
            "addons/keels2/LICENSE",
            "addons/keels2/THIRD_PARTY_NOTICES.md",
            "addons/keels2/compatibility/cs2-2000884-linuxsteamrt64.accepted.tsv",
            "addons/keels2/compatibility/cs2-2000884-win64.accepted.tsv",
            "addons/keels2/compatibility/cs2-2000885-linuxsteamrt64.accepted.tsv",
            "addons/keels2/compatibility/cs2-2000885-win64.accepted.tsv",
            "addons/keels2/compatibility/cs2-2000888-linuxsteamrt64.accepted.tsv",
            "addons/keels2/compatibility/cs2-2000888-win64.accepted.tsv",
            "addons/keels2/bin/linuxsteamrt64/libkeels2_host.so",
            "addons/keels2/bin/linuxsteamrt64/libserver.so",
            "addons/keels2/plugins/linuxsteamrt64/keels2_basic.so",
            "addons/keels2/plugins/linuxsteamrt64/keels2_callbacks.so",
            "addons/keels2/plugins/linuxsteamrt64/keels2_entities.so",
            "addons/keels2/plugins/linuxsteamrt64/keels2_hooks.so",
            "addons/keels2/plugins/linuxsteamrt64/keels2_lifecycle.so",
            "addons/keels2/plugins/linuxsteamrt64/keels2_runtime.so",
            "addons/keels2/plugins/linuxsteamrt64/keels2_sample.so",
            "addons/keels2/tools/linuxsteamrt64/keels2_compatibility_review",
        )
    stop(f"unsupported artifact platform: {platform_key}")
    return ()


def artifact_names(version: str, platform_key: str) -> dict[str, str]:
    prefix = f"KeelS2-v{version}-{platform_key}"
    extension = "zip" if platform_key == "windows-x86_64" else "tar.gz"
    archive = f"{prefix}.{extension}"
    return {
        "archive": archive,
        "checksum": f"{archive}.sha256",
        "contents": f"{prefix}.contents.sha256",
        "manifest": f"{prefix}.build.json",
    }


def release_asset_names(version: str) -> list[str]:
    names: list[str] = []
    for platform_key in ("linux-x86_64", "windows-x86_64"):
        names.extend(artifact_names(version, platform_key).values())
    names.append(f"KeelS2-v{version}-SHA256SUMS.txt")
    return sorted(names)


def verify_package_inventory(package_root: Path, entries: tuple[str, ...]) -> None:
    prefix = PurePosixPath("addons/keels2")
    actual = {
        (prefix / PurePosixPath(path.relative_to(package_root).as_posix())).as_posix()
        for path in package_root.rglob("*")
        if path.is_file()
    }
    expected = set(entries)
    if actual != expected:
        missing = sorted(expected - actual)
        extra = sorted(actual - expected)
        stop(f"package entry set differs; missing={missing} extra={extra}")


def content_hashes(package_root: Path, entries: tuple[str, ...]) -> dict[str, str]:
    result: dict[str, str] = {}
    for entry in entries:
        relative = PurePosixPath(entry)
        source = package_root / Path(*relative.parts[2:])
        if not source.is_file():
            stop(f"required package file was not found: {source}")
        result[entry] = sha256_file(source)
    return result


def content_manifest_text(hashes: dict[str, str]) -> str:
    return "".join(f"{hashes[name]}  {name}\n" for name in sorted(hashes))


def linux_archive_mode(entry: str) -> int:
    if entry.endswith(".so") or entry == "addons/keels2/tools/linuxsteamrt64/keels2_compatibility_review":
        return 0o755
    return 0o644


def parse_hash_manifest(path: Path) -> dict[str, str]:
    result: dict[str, str] = {}
    try:
        lines = path.read_text(encoding="utf-8").splitlines()
    except OSError as error:
        stop(f"could not read hash manifest {path}: {error}")
    if not lines:
        stop(f"hash manifest is empty: {path}")
    for line in lines:
        match = re.fullmatch(r"([0-9a-f]{64})  ([^\r\n]+)", line)
        if not match:
            stop(f"invalid hash-manifest line in {path}: {line}")
        name = match.group(2)
        if name in result:
            stop(f"duplicate hash-manifest entry in {path}: {name}")
        result[name] = match.group(1)
    return result


def zip_datetime(epoch: int) -> tuple[int, int, int, int, int, int]:
    value = dt.datetime.fromtimestamp(max(epoch, 315532800), tz=dt.timezone.utc)
    second = value.second - value.second % 2
    return value.year, value.month, value.day, value.hour, value.minute, second


def create_zip(package_root: Path, archive: Path, entries: tuple[str, ...], epoch: int) -> None:
    descriptor, temporary_name = tempfile.mkstemp(prefix=f".{archive.name}.", dir=archive.parent)
    os.close(descriptor)
    temporary = Path(temporary_name)
    try:
        with zipfile.ZipFile(temporary, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as target:
            for entry in sorted(entries):
                relative = PurePosixPath(entry)
                source = package_root / Path(*relative.parts[2:])
                info = zipfile.ZipInfo(entry, date_time=zip_datetime(epoch))
                info.compress_type = zipfile.ZIP_DEFLATED
                info.create_system = 3
                info.external_attr = (stat.S_IFREG | 0o644) << 16
                info.flag_bits |= 0x800
                target.writestr(info, source.read_bytes(), compress_type=zipfile.ZIP_DEFLATED, compresslevel=9)
        os.replace(temporary, archive)
        if os.name != "nt":
            archive.chmod(0o644)
    finally:
        if temporary.exists():
            temporary.unlink()


def create_tar_gz(package_root: Path, archive: Path, entries: tuple[str, ...], epoch: int) -> None:
    descriptor, temporary_name = tempfile.mkstemp(prefix=f".{archive.name}.", dir=archive.parent)
    os.close(descriptor)
    temporary = Path(temporary_name)
    try:
        with temporary.open("wb") as raw:
            with gzip.GzipFile(filename="", mode="wb", fileobj=raw, compresslevel=9, mtime=epoch) as compressed:
                with tarfile.open(fileobj=compressed, mode="w", format=tarfile.GNU_FORMAT) as target:
                    for entry in sorted(entries):
                        relative = PurePosixPath(entry)
                        source = package_root / Path(*relative.parts[2:])
                        data = source.read_bytes()
                        info = tarfile.TarInfo(entry)
                        info.size = len(data)
                        info.mtime = epoch
                        info.mode = linux_archive_mode(entry)
                        info.uid = 0
                        info.gid = 0
                        info.uname = "root"
                        info.gname = "root"
                        target.addfile(info, io_bytes(data))
            raw.flush()
            os.fsync(raw.fileno())
        os.replace(temporary, archive)
        if os.name != "nt":
            archive.chmod(0o644)
    finally:
        if temporary.exists():
            temporary.unlink()


def io_bytes(data: bytes):
    import io
    return io.BytesIO(data)


def validate_member_name(name: str) -> None:
    if "\\" in name:
        stop(f"archive entry contains a backslash: {name}")
    path = PurePosixPath(name)
    if path.is_absolute() or not path.parts or any(part in ("", ".", "..") for part in path.parts):
        stop(f"unsafe archive entry: {name}")


def archive_payload_hashes(archive: Path, platform_key: str) -> dict[str, str]:
    expected = set(package_entries(platform_key))
    actual: dict[str, str] = {}
    if platform_key == "windows-x86_64":
        try:
            with zipfile.ZipFile(archive, "r") as source:
                for info in source.infolist():
                    name = info.filename
                    validate_member_name(name)
                    if info.is_dir():
                        stop(f"unexpected directory entry in archive: {name}")
                    if name in actual:
                        stop(f"duplicate archive entry: {name}")
                    actual[name] = sha256_bytes(source.read(info))
        except (OSError, zipfile.BadZipFile) as error:
            stop(f"invalid Windows archive {archive}: {error}")
    else:
        try:
            with tarfile.open(archive, "r:gz") as source:
                for member in source.getmembers():
                    name = member.name
                    validate_member_name(name)
                    if not member.isfile():
                        stop(f"unexpected non-file entry in archive: {name}")
                    if member.mode & 0o777 != linux_archive_mode(name):
                        stop(f"unexpected file mode in archive: {name}")
                    if name in actual:
                        stop(f"duplicate archive entry: {name}")
                    handle = source.extractfile(member)
                    if handle is None:
                        stop(f"could not read archive entry: {name}")
                    actual[name] = sha256_bytes(handle.read())
        except (OSError, tarfile.TarError) as error:
            stop(f"invalid Linux archive {archive}: {error}")
    if set(actual) != expected:
        missing = sorted(expected - set(actual))
        extra = sorted(set(actual) - expected)
        stop(f"archive entry set differs; missing={missing} extra={extra}")
    return actual


def sidecar_hash(path: Path, expected_name: str) -> str:
    try:
        text = path.read_text(encoding="utf-8").strip()
    except OSError as error:
        stop(f"could not read checksum sidecar {path}: {error}")
    match = re.fullmatch(r"([0-9a-f]{64})  ([^\r\n]+)", text)
    if not match or match.group(2) != expected_name:
        stop(f"invalid checksum sidecar: {path}")
    return match.group(1)


def verify_platform_set(release_dir: Path, version: str, platform_key: str) -> dict[str, object]:
    names = artifact_names(version, platform_key)
    paths = {key: release_dir / name for key, name in names.items()}
    for path in paths.values():
        if not path.is_file():
            stop(f"required release artifact was not found: {path}")
    archive_hash = sha256_file(paths["archive"])
    if sidecar_hash(paths["checksum"], names["archive"]) != archive_hash:
        stop(f"archive checksum sidecar does not match: {paths['archive']}")
    expected_contents = parse_hash_manifest(paths["contents"])
    if set(expected_contents) != set(package_entries(platform_key)):
        stop(f"content manifest entry set differs: {paths['contents']}")
    actual_contents = archive_payload_hashes(paths["archive"], platform_key)
    if expected_contents != actual_contents:
        stop(f"archive payload hashes do not match: {paths['contents']}")
    manifest = strict_json(paths["manifest"])
    expected_fields: dict[str, object] = {
        "schema": SCHEMA_VERSION,
        "project": PROJECT,
        "version": version,
        "tag": f"v{version}",
        "platform": platform_key,
        "architecture": "x86_64",
        "archive": names["archive"],
        "archive_sha256": archive_hash,
        "contents": names["contents"],
        "contents_sha256": sha256_file(paths["contents"]),
        "tests_passed": True,
    }
    for key, expected in expected_fields.items():
        if manifest.get(key) != expected:
            stop(f"build manifest field {key} differs in {paths['manifest']}")
    if not isinstance(manifest.get("tool_version"), str) or not manifest.get("tool_version"):
        stop(f"invalid tool version in build manifest: {paths['manifest']}")
    if not isinstance(manifest.get("configuration"), str) or not manifest.get("configuration"):
        stop(f"invalid configuration in build manifest: {paths['manifest']}")
    commit = manifest.get("commit")
    if not isinstance(commit, str) or not re.fullmatch(r"[0-9a-f]{40}", commit):
        stop(f"invalid commit in build manifest: {paths['manifest']}")
    return manifest


def cmake_version() -> str:
    first = output(["cmake", "--version"]).splitlines()
    if not first:
        stop("cmake --version returned no output")
    return first[0]


def build_release(args: argparse.Namespace) -> None:
    require_command("git")
    require_command("cmake")
    require_command("ctest")
    repo = discover_repo(args.repo)
    git_clean(repo)
    version = resolve_version(repo, args.version, args.project_suffix)
    tag = f"v{version}"
    platform_key, platform_dir, archive_kind = detect_platform()
    commit = current_commit(repo)
    epoch = commit_timestamp(repo, commit)
    configuration = args.configuration
    build_dir = Path(args.build_dir).expanduser().resolve() if args.build_dir else repo / "out" / "build" / tag / platform_key / configuration.lower()
    release_dir = Path(args.release_dir).expanduser().resolve() if args.release_dir else repo / "out" / "releases" / tag
    build_dir.mkdir(parents=True, exist_ok=True)
    release_dir.mkdir(parents=True, exist_ok=True)
    configure = ["cmake"]
    if args.fresh:
        configure.append("--fresh")
    configure.extend(["-S", str(repo), "-B", str(build_dir), "-DKEELS2_GAME=cs2", "-DBUILD_TESTING=ON"])
    if platform_key == "windows-x86_64":
        configure.extend(["-G", args.generator or "Visual Studio 17 2022", "-A", "x64"])
    else:
        configure.append(f"-DCMAKE_BUILD_TYPE={configuration}")
        if args.generator:
            configure.extend(["-G", args.generator])
    say(f"Configuring {PROJECT} {version} for {platform_key}")
    run(configure, cwd=repo)
    say(f"Building {configuration}")
    run(["cmake", "--build", str(build_dir), "--config", configuration, "--parallel"], cwd=repo)
    say("Running CTest")
    run(
        [
            "ctest",
            "--test-dir",
            str(build_dir),
            "-C",
            configuration,
            "--verbose",
            "--output-on-failure",
            "--stop-on-failure",
            "--parallel",
            "2",
            "--timeout",
            "900",
        ],
        cwd=repo,
    )
    package_root = build_dir / "package" / "addons" / "keels2"
    entries = package_entries(platform_key)
    verify_package_inventory(package_root, entries)
    hashes = content_hashes(package_root, entries)
    names = artifact_names(version, platform_key)
    archive = release_dir / names["archive"]
    contents = release_dir / names["contents"]
    checksum = release_dir / names["checksum"]
    manifest_path = release_dir / names["manifest"]
    atomic_text(contents, content_manifest_text(hashes))
    if archive_kind == "zip":
        create_zip(package_root, archive, entries, epoch)
    else:
        create_tar_gz(package_root, archive, entries, epoch)
    archive_hash = sha256_file(archive)
    atomic_text(checksum, f"{archive_hash}  {archive.name}\n")
    manifest: dict[str, object] = {
        "schema": SCHEMA_VERSION,
        "tool_version": TOOL_VERSION,
        "project": PROJECT,
        "version": version,
        "tag": tag,
        "platform": platform_key,
        "platform_directory": platform_dir,
        "architecture": "x86_64",
        "configuration": configuration,
        "commit": commit,
        "commit_timestamp": epoch,
        "source_date_utc": dt.datetime.fromtimestamp(epoch, tz=dt.timezone.utc).isoformat(),
        "archive": archive.name,
        "archive_sha256": archive_hash,
        "contents": contents.name,
        "contents_sha256": sha256_file(contents),
        "tests_passed": True,
        "cmake": cmake_version(),
        "python": platform.python_version(),
        "host": platform.platform(),
    }
    atomic_text(manifest_path, json.dumps(manifest, indent=2, sort_keys=True) + "\n")
    verify_platform_set(release_dir, version, platform_key)
    say()
    say(f"{platform_key} release artifact: PASS")
    say(f"Commit: {commit}")
    say(f"Archive: {archive}")
    say(f"SHA-256: {archive_hash}")


def verify_release(args: argparse.Namespace) -> dict[str, object]:
    version = validate_version(args.version)
    release_dir = Path(args.release_dir).expanduser().resolve()
    manifests = [
        verify_platform_set(release_dir, version, "linux-x86_64"),
        verify_platform_set(release_dir, version, "windows-x86_64"),
    ]
    commits = {str(manifest["commit"]) for manifest in manifests}
    if len(commits) != 1:
        stop(f"platform artifacts were built from different commits: {sorted(commits)}")
    configurations = {str(manifest.get("configuration")) for manifest in manifests}
    if len(configurations) != 1:
        stop(f"platform artifacts use different configurations: {sorted(configurations)}")
    sums_name = f"KeelS2-v{version}-SHA256SUMS.txt"
    asset_names = release_asset_names(version)
    without_sums = [name for name in asset_names if name != sums_name]
    sums_text = "".join(f"{sha256_file(release_dir / name)}  {name}\n" for name in without_sums)
    atomic_text(release_dir / sums_name, sums_text)
    parsed = parse_hash_manifest(release_dir / sums_name)
    if set(parsed) != set(without_sums):
        stop("top-level checksum manifest entry set differs")
    for name, expected_hash in parsed.items():
        if sha256_file(release_dir / name) != expected_hash:
            stop(f"top-level checksum differs: {name}")
    say("Complete cross-platform release artifact set: PASS")
    say(f"Commit: {next(iter(commits))}")
    say(f"Directory: {release_dir}")
    return {"version": version, "commit": next(iter(commits)), "release_dir": release_dir, "assets": asset_names}


def gh_json(command: list[str], cwd: Path | None = None) -> object:
    result = run(command, cwd=cwd, capture=True)
    try:
        return json.loads(result.stdout)
    except json.JSONDecodeError as error:
        stop(f"GitHub CLI returned invalid JSON: {error}")
    return {}


def gh_api(method: str, endpoint: str, payload: dict[str, object] | None = None, cwd: Path | None = None) -> object:
    command = ["gh", "api", "--method", method, "-H", "Accept: application/vnd.github+json", "-H", "X-GitHub-Api-Version: 2022-11-28"]
    input_text = None
    if payload is not None:
        command.extend(["--input", "-"])
        input_text = json.dumps(payload)
    command.append(endpoint)
    result = run(command, cwd=cwd, capture=True, input_text=input_text)
    if not result.stdout.strip():
        return {}
    try:
        return json.loads(result.stdout)
    except json.JSONDecodeError as error:
        stop(f"GitHub API returned invalid JSON for {endpoint}: {error}")
    return {}


def all_releases(repository: str, repo: Path | None = None) -> list[dict[str, object]]:
    releases: list[dict[str, object]] = []
    page = 1
    while True:
        value = gh_api("GET", f"repos/{repository}/releases?per_page=100&page={page}", cwd=repo)
        if not isinstance(value, list):
            stop("GitHub release list is not an array")
        batch = [item for item in value if isinstance(item, dict)]
        releases.extend(batch)
        if len(batch) < 100:
            break
        page += 1
    return releases


def matching_release(repository: str, tag: str, repo: Path | None = None) -> dict[str, object] | None:
    matches = [release for release in all_releases(repository, repo) if release.get("tag_name") == tag]
    if len(matches) > 1:
        stop(f"multiple GitHub releases use tag {tag}")
    return matches[0] if matches else None


def remote_asset_map(release: dict[str, object]) -> dict[str, dict[str, object]]:
    raw_assets = release.get("assets")
    if not isinstance(raw_assets, list):
        stop("GitHub release assets are not an array")
    result: dict[str, dict[str, object]] = {}
    for raw in raw_assets:
        if not isinstance(raw, dict) or not isinstance(raw.get("name"), str):
            stop("GitHub returned an invalid release asset")
        name = str(raw["name"])
        if name in result:
            stop(f"GitHub release contains duplicate asset name: {name}")
        result[name] = raw
    return result


def expected_asset_hashes(release_dir: Path, asset_names: list[str]) -> dict[str, str]:
    result: dict[str, str] = {}
    for name in asset_names:
        path = release_dir / name
        if not path.is_file():
            stop(f"release asset was not found: {path}")
        result[name] = sha256_file(path)
    return result


def verify_remote_assets(release: dict[str, object], expected: dict[str, str]) -> None:
    remote = remote_asset_map(release)
    if set(remote) != set(expected):
        stop(f"published asset set differs; expected={sorted(expected)} actual={sorted(remote)}")
    for name, expected_hash in expected.items():
        asset = remote[name]
        digest = asset.get("digest")
        state = asset.get("state")
        if state != "uploaded":
            stop(f"published asset is not uploaded: {name} state={state}")
        if digest != f"sha256:{expected_hash}":
            stop(f"published asset digest differs: {name}")


def gh_token() -> str:
    token = output(["gh", "auth", "token", "--hostname", "github.com"])
    if not token:
        stop("GitHub CLI returned an empty authentication token")
    return token


def upload_asset(upload_url: str, path: Path, token: str) -> dict[str, object]:
    base = upload_url.split("{", 1)[0]
    url = f"{base}?{urllib.parse.urlencode({'name': path.name})}"
    content_type = mimetypes.guess_type(path.name)[0] or "application/octet-stream"
    request = urllib.request.Request(
        url,
        data=path.read_bytes(),
        method="POST",
        headers={
            "Accept": "application/vnd.github+json",
            "Authorization": f"Bearer {token}",
            "Content-Type": content_type,
            "X-GitHub-Api-Version": "2022-11-28",
            "User-Agent": "KeelS2-release-tooling",
        },
    )
    try:
        with urllib.request.urlopen(request, timeout=300) as response:
            body = response.read()
    except urllib.error.HTTPError as error:
        detail = error.read().decode("utf-8", errors="replace")
        stop(f"GitHub asset upload failed for {path.name}: HTTP {error.code}: {detail}")
    except urllib.error.URLError as error:
        stop(f"GitHub asset upload failed for {path.name}: {error}")
    try:
        value = json.loads(body)
    except json.JSONDecodeError as error:
        stop(f"GitHub asset upload returned invalid JSON for {path.name}: {error}")
    if not isinstance(value, dict):
        stop(f"GitHub asset upload returned an invalid response for {path.name}")
    return value


def ensure_remote_tag(repo: Path, tag: str, commit: str, title: str) -> None:
    remote = run(["git", "ls-remote", "--tags", "origin", f"refs/tags/{tag}", f"refs/tags/{tag}^{{}}"], cwd=repo, capture=True)
    lines = [line for line in remote.stdout.splitlines() if line.strip()]
    if lines:
        targets = {line.split()[0] for line in lines if line.endswith("^{}")}
        if not targets:
            targets = {line.split()[0] for line in lines}
        if targets != {commit}:
            stop(f"remote tag {tag} targets a different commit")
    local = run(["git", "rev-parse", "-q", "--verify", f"refs/tags/{tag}^{{commit}}"], cwd=repo, capture=True, check=False)
    if local.returncode == 0:
        if local.stdout.strip() != commit:
            stop(f"local tag {tag} targets a different commit")
    elif lines:
        run(["git", "fetch", "origin", f"refs/tags/{tag}:refs/tags/{tag}"], cwd=repo)
    else:
        run(["git", "tag", "-a", tag, commit, "-m", title], cwd=repo)
    if not lines:
        run(["git", "push", "origin", f"refs/tags/{tag}"], cwd=repo)


def status_release(args: argparse.Namespace) -> None:
    require_command("gh")
    repo = discover_repo(args.repo)
    repository = args.repository or origin_repository(repo)
    tag = args.tag
    release = matching_release(repository, tag, repo)
    if not release:
        say(f"No GitHub release exists for {tag}")
        return
    say(f"Tag: {release.get('tag_name')}")
    say(f"Name: {release.get('name')}")
    say(f"Draft: {str(release.get('draft')).lower()}")
    say(f"Prerelease: {str(release.get('prerelease')).lower()}")
    say(f"URL: {release.get('html_url')}")
    for name, asset in sorted(remote_asset_map(release).items()):
        say(f"{name}\t{asset.get('state')}\t{asset.get('digest')}")


def publish_release(args: argparse.Namespace) -> None:
    require_command("git")
    require_command("gh")
    repo = discover_repo(args.repo)
    git_clean(repo)
    repository = args.repository or origin_repository(repo)
    release_dir = Path(args.release_dir).expanduser().resolve()
    verification_args = argparse.Namespace(version=args.version, release_dir=str(release_dir))
    verified = verify_release(verification_args)
    version = str(verified["version"])
    commit = str(verified["commit"])
    tag = f"v{version}"
    if "-" in version and not args.prerelease:
        stop("a semantic prerelease version requires --prerelease")
    title = args.title or f"KeelS2 {tag}"
    notes_path = Path(args.notes_file).expanduser().resolve()
    if not notes_path.is_file():
        stop(f"release notes file was not found: {notes_path}")
    notes = notes_path.read_text(encoding="utf-8")
    if not notes.strip():
        stop("release notes are empty")
    run(["gh", "auth", "status"], cwd=repo)
    say("Fetching origin/main and tags")
    run(["git", "fetch", "origin", f"refs/heads/{DEFAULT_BASE}:refs/remotes/origin/{DEFAULT_BASE}", "--tags"], cwd=repo)
    remote_main = git(repo, "rev-parse", f"origin/{DEFAULT_BASE}")
    if remote_main != commit:
        stop(f"release commit {commit} is not current origin/{DEFAULT_BASE} {remote_main}")
    if current_commit(repo) != commit:
        stop(f"local HEAD is not the release commit: {commit}")
    expected_hashes = expected_asset_hashes(release_dir, list(verified["assets"]))
    existing = matching_release(repository, tag, repo)
    if existing and existing.get("draft") is False:
        verify_remote_assets(existing, expected_hashes)
        if existing.get("prerelease") != args.prerelease:
            stop("published release prerelease state differs")
        say("Published release already matches every local asset")
        say(f"MILESTONE: {tag.upper()} PUBLISHED")
        return
    say()
    say("Release publication preflight: PASS")
    say(f"Repository: {repository}")
    say(f"Commit: {commit}")
    say(f"Tag: {tag}")
    say(f"Assets: {len(expected_hashes)}")
    say(f"Draft recovery: {'yes' if existing else 'new draft'}")
    expected_confirmation = f"PUBLISH {tag}"
    confirmation = input(f"Type {expected_confirmation} to continue: ").strip()
    if confirmation != expected_confirmation:
        stop("publication was not confirmed")
    ensure_remote_tag(repo, tag, commit, title)
    release = matching_release(repository, tag, repo)
    if not release:
        created = gh_api(
            "POST",
            f"repos/{repository}/releases",
            {
                "tag_name": tag,
                "target_commitish": commit,
                "name": title,
                "body": notes,
                "draft": True,
                "prerelease": args.prerelease,
                "generate_release_notes": False,
            },
            cwd=repo,
        )
        if not isinstance(created, dict):
            stop("GitHub returned an invalid created release")
        release = created
    if release.get("draft") is not True:
        stop("matching release changed state during publication")
    if release.get("name") != title or release.get("body") != notes or release.get("prerelease") != args.prerelease:
        stop("existing draft metadata differs; review it manually before resuming")
    release_id = release.get("id")
    upload_url = release.get("upload_url")
    if not isinstance(release_id, int) or not isinstance(upload_url, str):
        stop("draft release is missing its numeric ID or upload URL")
    token = gh_token()
    remote = remote_asset_map(release)
    for name, expected_hash in expected_hashes.items():
        if name in remote:
            asset = remote[name]
            if asset.get("state") == "uploaded" and asset.get("digest") == f"sha256:{expected_hash}":
                say(f"{name}: already uploaded")
                continue
            if asset.get("state") == "starter" and isinstance(asset.get("id"), int):
                gh_api("DELETE", f"repos/{repository}/releases/assets/{asset['id']}", cwd=repo)
            else:
                stop(f"draft asset differs and will not be overwritten: {name}")
        say(f"Uploading {name}")
        uploaded = upload_asset(upload_url, release_dir / name, token)
        if uploaded.get("digest") != f"sha256:{expected_hash}" or uploaded.get("state") != "uploaded":
            stop(f"GitHub did not confirm the expected digest for {name}")
    refreshed = gh_api("GET", f"repos/{repository}/releases/{release_id}", cwd=repo)
    if not isinstance(refreshed, dict):
        stop("GitHub returned an invalid draft release after upload")
    verify_remote_assets(refreshed, expected_hashes)
    published = gh_api(
        "PATCH",
        f"repos/{repository}/releases/{release_id}",
        {"draft": False, "prerelease": args.prerelease},
        cwd=repo,
    )
    if not isinstance(published, dict) or published.get("draft") is not False:
        stop("GitHub did not confirm release publication")
    verify_remote_assets(published, expected_hashes)
    final = gh_api("GET", f"repos/{repository}/releases/tags/{tag}", cwd=repo)
    if not isinstance(final, dict) or final.get("draft") is not False:
        stop("published release could not be resolved by its tag")
    verify_remote_assets(final, expected_hashes)
    say()
    say(f"Release: {final.get('html_url')}")
    say("Published release digest verification: PASS")
    say(f"MILESTONE: {tag.upper()} PUBLISHED")


def self_test(_: argparse.Namespace) -> None:
    validate_version("0.1.0-alpha.2")
    if normalize_repository_url("https://github.com/KeelS2Project/KeelS2") != EXPECTED_REPOSITORY:
        stop("HTTPS repository parsing self-test failed")
    if normalize_repository_url("git@github.com:KeelS2Project/KeelS2.git") != EXPECTED_REPOSITORY:
        stop("SSH repository parsing self-test failed")
    with tempfile.TemporaryDirectory(prefix="keels2-release-self-test-") as temporary_text:
        temporary = Path(temporary_text)
        project_root = temporary / "project"
        project_root.mkdir()
        (project_root / "CMakeLists.txt").write_text(
            "project(KeelS2 VERSION 0.1.0 LANGUAGES C CXX)\n",
            encoding="utf-8",
        )
        if project_version(project_root) != "0.1.0":
            stop("CMake project version parsing self-test failed")
        release_dir = temporary / "release"
        release_dir.mkdir()
        for platform_key in ("linux-x86_64", "windows-x86_64"):
            package_root = temporary / platform_key / "package" / "addons" / "keels2"
            entries = package_entries(platform_key)
            required_documents = {
                "addons/keels2/LICENSE",
                "addons/keels2/THIRD_PARTY_NOTICES.md",
            }
            if not required_documents.issubset(entries):
                stop(f"required package documents are missing for {platform_key}")
            for index, entry in enumerate(entries):
                relative = PurePosixPath(entry)
                target = package_root / Path(*relative.parts[2:])
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(f"{platform_key}:{index}:{entry}\n".encode())
            verify_package_inventory(package_root, entries)
            unexpected = package_root / "unexpected.bin"
            unexpected.write_bytes(b"unexpected\n")
            try:
                verify_package_inventory(package_root, entries)
            except Stop:
                pass
            else:
                stop(f"unexpected package-file self-test failed for {platform_key}")
            unexpected.unlink()
            version = "0.1.0-selftest"
            names = artifact_names(version, platform_key)
            contents = release_dir / names["contents"]
            hashes = content_hashes(package_root, entries)
            atomic_text(contents, content_manifest_text(hashes))
            archive = release_dir / names["archive"]
            if platform_key == "windows-x86_64":
                create_zip(package_root, archive, entries, 1785900000)
            else:
                create_tar_gz(package_root, archive, entries, 1785900000)
            archive_hash = sha256_file(archive)
            atomic_text(release_dir / names["checksum"], f"{archive_hash}  {archive.name}\n")
            manifest = {
                "schema": SCHEMA_VERSION,
                "tool_version": TOOL_VERSION,
                "project": PROJECT,
                "version": version,
                "tag": f"v{version}",
                "platform": platform_key,
                "architecture": "x86_64",
                "configuration": "RelWithDebInfo",
                "commit": "a" * 40,
                "archive": archive.name,
                "archive_sha256": archive_hash,
                "contents": contents.name,
                "contents_sha256": sha256_file(contents),
                "tests_passed": True,
            }
            atomic_text(release_dir / names["manifest"], json.dumps(manifest, indent=2, sort_keys=True) + "\n")
            verify_platform_set(release_dir, version, platform_key)
        verify_release(argparse.Namespace(version="0.1.0-selftest", release_dir=str(release_dir)))
        bad_zip = temporary / "bad.zip"
        with zipfile.ZipFile(bad_zip, "w") as target:
            target.writestr("addons\\keels2\\bad.dll", b"bad")
        try:
            archive_payload_hashes(bad_zip, "windows-x86_64")
        except Stop:
            pass
        else:
            stop("backslash archive self-test failed")
    say("KeelS2 release tooling self-test: PASS")


def doctor(args: argparse.Namespace) -> None:
    repo = discover_repo(args.repo)
    for command in ("git", "cmake", "ctest"):
        require_command(command)
    platform_key, _, _ = detect_platform()
    say("KeelS2 release tooling doctor: PASS")
    say(f"Repository: {repo}")
    say(f"Origin: {origin_repository(repo)}")
    say(f"Commit: {current_commit(repo)}")
    say(f"Project version: {project_version(repo)}")
    say(f"Platform: {platform_key}")
    say(f"CMake: {cmake_version()}")
    say(f"Python: {platform.python_version()}")


def only_install_changes(repo: Path) -> None:
    allowed = {"tools/release.py", ".github/workflows/build.yml"}
    status_text = working_tree_status(repo)
    for line in status_text.splitlines():
        raw_path = line[3:]
        if " -> " in raw_path:
            raw_path = raw_path.split(" -> ", 1)[1]
        if raw_path not in allowed:
            say(status_text)
            stop(f"unexpected change while installing release tooling: {raw_path}")


def finish_tooling_merge(repo: Path, base: str, branch: str) -> None:
    run(["git", "fetch", "origin", f"refs/heads/{base}:refs/remotes/origin/{base}"], cwd=repo)
    if git(repo, "branch", "--show-current") != base:
        run(["git", "switch", base], cwd=repo)
    if current_commit(repo) != git(repo, "rev-parse", f"origin/{base}"):
        run(["git", "merge", "--ff-only", f"origin/{base}"], cwd=repo)
    run([sys.executable, str(repo / "tools" / "release.py"), "self-test"], cwd=repo)
    local_branch = run(["git", "show-ref", "--verify", "--quiet", f"refs/heads/{branch}"], cwd=repo, check=False)
    if local_branch.returncode == 0:
        ancestor = run(["git", "merge-base", "--is-ancestor", branch, base], cwd=repo, check=False)
        if ancestor.returncode == 0:
            run(["git", "branch", "-d", branch], cwd=repo)
    git_clean(repo)
    say()
    say(f"Main: {current_commit(repo)}")
    say("MILESTONE: VERSIONED RELEASE ENGINEERING COMPLETE")


def install_tooling(args: argparse.Namespace) -> None:
    require_command("git")
    require_command("gh")
    repo = validate_repo(Path(args.repo))
    repository = origin_repository(repo)
    if repository != EXPECTED_REPOSITORY and not args.allow_fork:
        stop(f"unexpected origin repository: {repository}")
    source = Path(__file__).resolve()
    target = repo / "tools" / "release.py"
    workflow = repo / ".github" / "workflows" / "build.yml"
    if not workflow.is_file():
        stop(f"build workflow was not found: {workflow}")
    say(f"Fetching origin/{args.base}")
    run(["git", "fetch", "origin", f"refs/heads/{args.base}:refs/remotes/origin/{args.base}"], cwd=repo)
    current_branch = git(repo, "branch", "--show-current")
    if current_branch == args.base:
        git_clean(repo)
        if current_commit(repo) != git(repo, "rev-parse", f"origin/{args.base}"):
            stop(f"local {args.base} is not synchronized with origin/{args.base}")
        if target.is_file() and target.read_bytes() == source.read_bytes() and WORKFLOW_NEW in workflow.read_text(encoding="utf-8"):
            run([sys.executable, str(target), "self-test"], cwd=repo)
            say("MILESTONE: VERSIONED RELEASE ENGINEERING COMPLETE")
            return
        local_branch = run(["git", "show-ref", "--verify", "--quiet", f"refs/heads/{args.branch}"], cwd=repo, check=False)
        if local_branch.returncode == 0:
            run(["git", "switch", args.branch], cwd=repo)
        else:
            remote_branch = run(["git", "ls-remote", "--exit-code", "--heads", "origin", args.branch], cwd=repo, capture=True, check=False)
            if remote_branch.returncode == 0:
                run(["git", "fetch", "origin", f"refs/heads/{args.branch}:refs/remotes/origin/{args.branch}"], cwd=repo)
                run(["git", "switch", "--track", "-c", args.branch, f"origin/{args.branch}"], cwd=repo)
            elif remote_branch.returncode == 2:
                run(["git", "switch", "-c", args.branch, f"origin/{args.base}"], cwd=repo)
            else:
                stop("could not inspect the remote release-tooling branch")
    elif current_branch != args.branch:
        stop(f"unexpected branch: {current_branch}")
    only_install_changes(repo)
    if run(["git", "merge-base", "--is-ancestor", f"origin/{args.base}", "HEAD"], cwd=repo, check=False).returncode != 0:
        stop(f"{args.branch} does not contain current origin/{args.base}")
    target.parent.mkdir(parents=True, exist_ok=True)
    source_bytes = source.read_bytes()
    if target.exists() and target.resolve() != source:
        if target.read_bytes() != source_bytes:
            target_in_head = run(
                ["git", "cat-file", "-e", "HEAD:tools/release.py"],
                cwd=repo,
                capture=True,
                check=False,
            )
            if current_commit(repo) != git(repo, "rev-parse", f"origin/{args.base}") or target_in_head.returncode == 0:
                stop(f"existing committed release tool differs: {target}")
            atomic_write(target, source_bytes)
    elif target.resolve() != source:
        atomic_write(target, source_bytes)
    if os.name != "nt":
        target.chmod(0o755)
    workflow_text = workflow.read_text(encoding="utf-8")
    if WORKFLOW_NEW not in workflow_text:
        if WORKFLOW_OLD not in workflow_text:
            stop("build workflow no longer matches the reviewed integration point")
        workflow_text = workflow_text.replace(WORKFLOW_OLD, WORKFLOW_NEW, 1)
        atomic_text(workflow, workflow_text)
    only_install_changes(repo)
    run([sys.executable, str(target), "self-test"], cwd=repo)
    run(["git", "add", "--", "tools/release.py", ".github/workflows/build.yml"], cwd=repo)
    run(["git", "diff", "--cached", "--check"], cwd=repo)
    staged = git(repo, "diff", "--cached", "--name-only")
    if staged:
        say()
        say("Staged release-engineering changes:")
        run(["git", "diff", "--cached", "--stat"], cwd=repo)
        confirmation = input("Type INSTALL RELEASE TOOLING to commit and push: ").strip()
        if confirmation != "INSTALL RELEASE TOOLING":
            stop("installation was not confirmed")
        run(["git", "commit", "-m", "Add versioned release tooling"], cwd=repo)
    git_clean(repo)
    run(["git", "push", "-u", "origin", args.branch], cwd=repo)
    run(["gh", "auth", "status"], cwd=repo)
    run(["git", "fetch", "origin", f"refs/heads/{args.base}:refs/remotes/origin/{args.base}"], cwd=repo)
    if run(["git", "merge-base", "--is-ancestor", "HEAD", f"origin/{args.base}"], cwd=repo, check=False).returncode == 0:
        finish_tooling_merge(repo, args.base, args.branch)
        return
    pull_requests = gh_json([
        "gh", "pr", "list", "--repo", repository, "--state", "open", "--head", args.branch,
        "--json", "number,url,isDraft,headRefName,baseRefName,headRefOid",
    ], cwd=repo)
    if not isinstance(pull_requests, list):
        stop("GitHub CLI returned an invalid pull-request list")
    if len(pull_requests) > 1:
        stop("multiple open release-tooling pull requests were found")
    if pull_requests:
        pull_request = pull_requests[0]
    else:
        body = (
            "Adds a version-controlled cross-platform release CLI, routes both CI platforms through it, "
            "and makes build, package verification, draft recovery, tag creation, asset upload, digest verification, "
            "and final publication resumable and fail-closed."
        )
        created = output([
            "gh", "pr", "create", "--repo", repository, "--base", args.base, "--head", args.branch,
            "--draft", "--title", "Add versioned release engineering", "--body", body,
        ], cwd=repo)
        pull_request = {"url": created}
    say()
    if isinstance(pull_request, dict) and pull_request.get("url"):
        say(f"Pull request: {pull_request.get('url')}")
    if args.leave_pr:
        say("MILESTONE: VERSIONED RELEASE TOOLING PR READY")
        return
    if not isinstance(pull_request, dict) or not isinstance(pull_request.get("number"), int):
        refreshed = gh_json([
            "gh", "pr", "list", "--repo", repository, "--state", "open", "--head", args.branch,
            "--json", "number,url,isDraft,headRefName,baseRefName,headRefOid",
        ], cwd=repo)
        if not isinstance(refreshed, list) or len(refreshed) != 1 or not isinstance(refreshed[0], dict):
            stop("could not resolve the new pull request")
        pull_request = refreshed[0]
    number = pull_request.get("number")
    if not isinstance(number, int):
        stop("pull request is missing its number")
    if pull_request.get("headRefOid") != current_commit(repo):
        stop("pull request head does not match the local release-tooling commit")
    if pull_request.get("baseRefName") != args.base:
        stop("pull request base branch differs")
    say()
    say("Waiting for Linux and Windows pull-request checks")
    run(["gh", "pr", "checks", str(number), "--repo", repository, "--watch", "--fail-fast", "--interval", "10"], cwd=repo)
    confirmation = input("Type MERGE RELEASE TOOLING to mark the PR ready and merge it: ").strip()
    if confirmation != "MERGE RELEASE TOOLING":
        stop("merge was not confirmed; the draft PR remains available")
    if pull_request.get("isDraft") is True:
        run(["gh", "pr", "ready", str(number), "--repo", repository], cwd=repo)
    run(["gh", "pr", "merge", str(number), "--repo", repository, "--merge", "--delete-branch"], cwd=repo)
    finish_tooling_merge(repo, args.base, args.branch)


def parser() -> argparse.ArgumentParser:
    root = argparse.ArgumentParser(prog="keels2-release", description="KeelS2 cross-platform release engineering")
    subparsers = root.add_subparsers(dest="command", required=True)

    install_parser = subparsers.add_parser("install", help="install this tool on a guarded integration branch")
    install_parser.add_argument("--repo", required=True)
    install_parser.add_argument("--branch", default=DEFAULT_BRANCH)
    install_parser.add_argument("--base", default=DEFAULT_BASE)
    install_parser.add_argument("--allow-fork", action="store_true")
    install_parser.add_argument("--leave-pr", action="store_true")
    install_parser.set_defaults(function=install_tooling)

    self_parser = subparsers.add_parser("self-test", help="run pure release-tooling tests")
    self_parser.set_defaults(function=self_test)

    doctor_parser = subparsers.add_parser("doctor", help="inspect the current release environment")
    doctor_parser.add_argument("--repo")
    doctor_parser.set_defaults(function=doctor)

    build_parser = subparsers.add_parser("build", help="build, test, package, and verify the current platform")
    build_parser.add_argument("--repo")
    version_group = build_parser.add_mutually_exclusive_group(required=True)
    version_group.add_argument("--version")
    version_group.add_argument("--project-suffix")
    build_parser.add_argument("--configuration", default="RelWithDebInfo")
    build_parser.add_argument("--generator")
    build_parser.add_argument("--build-dir")
    build_parser.add_argument("--release-dir")
    build_parser.add_argument("--fresh", action="store_true")
    build_parser.set_defaults(function=build_release)

    verify_parser = subparsers.add_parser("verify", help="verify the complete Windows and Linux artifact set")
    verify_parser.add_argument("--version", required=True)
    verify_parser.add_argument("--release-dir", required=True)
    verify_parser.set_defaults(function=verify_release)

    status_parser = subparsers.add_parser("status", help="inspect a GitHub release without changing it")
    status_parser.add_argument("--repo")
    status_parser.add_argument("--repository")
    status_parser.add_argument("--tag", required=True)
    status_parser.set_defaults(function=status_release)

    publish_parser = subparsers.add_parser("publish", help="verify, resume, and publish a GitHub release")
    publish_parser.add_argument("--repo")
    publish_parser.add_argument("--repository")
    publish_parser.add_argument("--version", required=True)
    publish_parser.add_argument("--release-dir", required=True)
    publish_parser.add_argument("--notes-file", required=True)
    publish_parser.add_argument("--title")
    publish_parser.add_argument("--prerelease", action="store_true")
    publish_parser.set_defaults(function=publish_release)

    return root


def main() -> int:
    try:
        arguments = parser().parse_args()
        arguments.function(arguments)
        return 0
    except Stop as error:
        print(str(error), file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        print("STOP: interrupted", file=sys.stderr)
        return 130


if __name__ == "__main__":
    raise SystemExit(main())
