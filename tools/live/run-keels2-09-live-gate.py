#!/usr/bin/env python3

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
import threading
import time
import zipfile
from contextlib import contextmanager
from pathlib import Path, PurePosixPath


class GateFailure(RuntimeError):
    pass


ACTION_TIMEOUT = 600.0


class Transcript:
    def __init__(self, path: Path, echo: bool):
        self.path = path
        self.lock = threading.Condition()
        self.text = ""
        self.echo = echo

    def append(self, data: bytes) -> None:
        value = data.decode("utf-8", errors="replace")
        with self.lock:
            self.text += value
            with self.path.open("a", encoding="utf-8", newline="") as handle:
                handle.write(value)
            if self.echo:
                sys.stdout.write(value)
                sys.stdout.flush()
            self.lock.notify_all()

    def set_echo(self, enabled: bool) -> bool:
        with self.lock:
            previous = self.echo
            self.echo = enabled
            return previous

    def position(self) -> int:
        with self.lock:
            return len(self.text)

    def wait(
        self,
        marker: str,
        after: int,
        timeout: float,
        process: subprocess.Popen[bytes],
        fail_on_timeout: bool = True,
    ) -> bool:
        deadline = time.monotonic() + timeout
        with self.lock:
            while marker not in self.text[after:]:
                if process.poll() is not None:
                    raise GateFailure(
                        f"server exited with status {process.returncode} while waiting for: {marker}")
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    if fail_on_timeout:
                        raise GateFailure(f"timed out waiting for: {marker}")
                    return False
                self.lock.wait(min(remaining, 0.25))
        return True


class Server:
    def __init__(self, command: list[str], cwd: Path, transcript: Transcript):
        self.transcript = transcript
        self.master: int | None = None
        self.process: subprocess.Popen[bytes]
        if os.name == "nt":
            self.process = subprocess.Popen(
                command,
                cwd=cwd,
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                bufsize=0,
                creationflags=subprocess.CREATE_NEW_PROCESS_GROUP,
            )
            if self.process.stdout is None:
                raise GateFailure("could not capture the Windows server console")
            self.reader = threading.Thread(
                target=self._read_pipe,
                args=(self.process.stdout,),
                daemon=True,
            )
        else:
            import pty

            master, slave = pty.openpty()
            self.master = master
            self.process = subprocess.Popen(
                command,
                cwd=cwd,
                stdin=slave,
                stdout=slave,
                stderr=slave,
                bufsize=0,
                close_fds=True,
                start_new_session=True,
            )
            os.close(slave)
            self.reader = threading.Thread(target=self._read_pty, daemon=True)
        self.reader.start()

    def _read_pipe(self, stream) -> None:
        while True:
            data = stream.read(4096)
            if not data:
                return
            self.transcript.append(data)

    def _read_pty(self) -> None:
        if self.master is None:
            return
        while True:
            try:
                data = os.read(self.master, 4096)
            except OSError:
                return
            if not data:
                return
            self.transcript.append(data)

    def send(self, command: str) -> int:
        if self.process.poll() is not None:
            raise GateFailure(f"server exited with status {self.process.returncode}")
        position = self.transcript.position()
        data = (command + "\n").encode("utf-8")
        if self.master is not None:
            os.write(self.master, data)
        else:
            if self.process.stdin is None:
                raise GateFailure("Windows server console input is unavailable")
            self.process.stdin.write(data)
            self.process.stdin.flush()
        return position

    def expect(self, command: str, marker: str, timeout: float = 30.0) -> None:
        position = self.send(command)
        self.transcript.wait(marker, position, timeout, self.process)

    def poll(
        self,
        command: str,
        marker: str,
        timeout: float = 30.0,
        interval: float = 0.5,
    ) -> None:
        position = self.transcript.position()
        deadline = time.monotonic() + timeout
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise GateFailure(f"timed out waiting for: {marker}")
            self.send(command)
            if self.transcript.wait(
                    marker,
                    position,
                    min(interval, remaining),
                    self.process,
                    False):
                return

    def stop(self) -> int | None:
        if self.process.poll() is None:
            try:
                self.send("quit")
                self.process.wait(timeout=30)
            except (GateFailure, subprocess.TimeoutExpired):
                self.process.terminate()
                try:
                    self.process.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    self.process.kill()
                    self.process.wait(timeout=10)
        if self.master is not None:
            try:
                os.close(self.master)
            except OSError:
                pass
            self.master = None
        self.reader.join(timeout=2)
        return self.process.returncode


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def fnv1a64(path: Path) -> tuple[int, int]:
    value = 14695981039346656037
    size = 0
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(65536), b""):
            size += len(chunk)
            for byte in chunk:
                value ^= byte
                value = (value * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return size, value


def verify_manifest(bundle: Path) -> None:
    manifest = bundle / "MANIFEST.txt"
    if not manifest.is_file():
        raise GateFailure("MANIFEST.txt is missing")
    seen: set[str] = set()
    for line in manifest.read_text(encoding="utf-8").splitlines():
        match = re.fullmatch(r"([0-9a-f]{64})  ([^\\\r\n]+)", line)
        if not match:
            raise GateFailure("MANIFEST.txt contains an invalid entry")
        relative = PurePosixPath(match.group(2))
        if relative.is_absolute() or any(part in ("", ".", "..") for part in relative.parts):
            raise GateFailure("MANIFEST.txt contains an unsafe path")
        name = relative.as_posix()
        if name in seen:
            raise GateFailure("MANIFEST.txt contains a duplicate path")
        seen.add(name)
        target = bundle / Path(*relative.parts)
        if not target.is_file() or sha256(target) != match.group(1):
            raise GateFailure(f"bundle verification failed: {name}")


def build_id(server_root: Path, explicit: str | None) -> str:
    if explicit:
        return explicit
    candidates = (
        server_root / "steamapps" / "appmanifest_730.acf",
        server_root.parent / "steamapps" / "appmanifest_730.acf",
    )
    for candidate in candidates:
        if not candidate.is_file():
            continue
        match = re.search(r'"buildid"\s+"([0-9]+)"', candidate.read_text(
            encoding="utf-8", errors="replace"))
        if match:
            return match.group(1)
    raise GateFailure("could not determine app 730 build ID; pass --build-id")


def server_paths(server_root: Path, platform_key: str) -> tuple[Path, Path, list[str]]:
    if platform_key == "windows-x86_64":
        module = server_root / "game" / "csgo" / "bin" / "win64" / "server.dll"
        executable = server_root / "game" / "bin" / "win64" / "cs2.exe"
        command = [str(executable)]
    else:
        module = server_root / "game" / "csgo" / "bin" / "linuxsteamrt64" / "libserver.so"
        executable = server_root / "game" / "cs2.sh"
        command = [str(executable)]
    if not module.is_file():
        raise GateFailure(f"genuine server module was not found: {module}")
    if not executable.is_file():
        raise GateFailure(f"dedicated server launcher was not found: {executable}")
    return module, executable, command


def safe_addon_path(server_root: Path) -> Path:
    addon = (server_root / "game" / "csgo" / "addons" / "keels2").resolve()
    expected_parent = (server_root / "game" / "csgo" / "addons").resolve()
    if addon.name != "keels2" or addon.parent != expected_parent:
        raise GateFailure("refusing an unsafe add-on path")
    return addon


def install_gameinfo_path(gameinfo: Path) -> None:
    raw = gameinfo.read_bytes()
    text = raw.decode("utf-8-sig")
    if re.search(r"^\s*Game\s+csgo/addons/keels2\s*$", text, re.MULTILINE | re.IGNORECASE):
        return
    newline = "\r\n" if "\r\n" in text else "\n"
    lines = text.splitlines()
    search_index = next(
        (index for index, line in enumerate(lines) if re.match(r'^\s*"?SearchPaths"?\s*$', line)),
        None,
    )
    if search_index is None:
        raise GateFailure("SearchPaths was not found in gameinfo.gi")
    brace_index = next(
        (index for index in range(search_index + 1, len(lines)) if "{" in lines[index]),
        None,
    )
    if brace_index is None:
        raise GateFailure("SearchPaths opening brace was not found in gameinfo.gi")
    lines.insert(brace_index + 1, "\t\t\tGame\tcsgo/addons/keels2")
    temporary = gameinfo.with_name(gameinfo.name + ".keels2.tmp")
    temporary.write_text(newline.join(lines) + newline, encoding="utf-8", newline="")
    os.replace(temporary, gameinfo)


def stage(fixture_root: Path, plugin_root: Path, source: str, target: str) -> Path:
    source_path = fixture_root / source
    target_path = plugin_root / target
    if not source_path.is_file():
        raise GateFailure(f"fixture is missing: {source}")
    shutil.copy2(source_path, target_path)
    return target_path


@contextmanager
def action(
    transcript: Transcript,
    number: int,
    title: str,
    instructions: tuple[str, ...],
):
    previous_echo = transcript.set_echo(False)
    print()
    print("=" * 78)
    print(f"ACTION REQUIRED {number}/4: {title}")
    print("=" * 78)
    for index, instruction in enumerate(instructions, 1):
        print(f"  {index}. {instruction}")
    print()
    print("No Enter key is needed. The runner will continue as soon as it detects completion.")
    sys.stdout.flush()
    completed = False
    try:
        yield
        completed = True
    finally:
        if completed:
            print(f"ACTION {number}/4: PASS")
            sys.stdout.flush()
        transcript.set_echo(previous_echo)


def archive_evidence(source: Path, output: Path, platform_key: str) -> None:
    if platform_key == "windows-x86_64":
        with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
            for path in sorted(source.rglob("*")):
                if path.is_file():
                    archive.write(path, (Path(source.name) / path.relative_to(source)).as_posix())
    else:
        with tarfile.open(output, "w:gz", compresslevel=9) as archive:
            archive.add(source, arcname=source.name, recursive=True)


def damage_result(text: str) -> dict[str, int] | None:
    matches = list(re.finditer(
        r"status ready=true seen=(\d+) blocked=(\d+) invalid=(\d+) "
        r"non_player_victim=(\d+) non_player_source=(\d+) self=(\d+) unrelated=(\d+) "
        r"result_errors=(\d+)",
        text,
    ))
    if not matches:
        return None
    values = [int(value) for value in matches[-1].groups()]
    keys = (
        "seen", "blocked", "invalid", "non_player_victim", "non_player_source",
        "self", "unrelated", "result_errors",
    )
    return dict(zip(keys, values))


def self_test() -> None:
    with tempfile.TemporaryDirectory(prefix="keels2-live-runner-test-") as temporary_text:
        temporary = Path(temporary_text)
        gameinfo = temporary / "gameinfo.gi"
        gameinfo.write_text(
            '"GameInfo"\n{\n\tSearchPaths\n\t{\n\t\tGame\tcsgo\n\t}\n}\n',
            encoding="utf-8")
        install_gameinfo_path(gameinfo)
        install_gameinfo_path(gameinfo)
        if gameinfo.read_text(encoding="utf-8").count("Game\tcsgo/addons/keels2") != 1:
            raise GateFailure("gameinfo installation self-test failed")
        payload = temporary / "payload.bin"
        payload.write_bytes(b"abc")
        if fnv1a64(payload) != (3, 0xE71FA2190541574B):
            raise GateFailure("fingerprint self-test failed")
        status = damage_result(
            "status ready=true seen=2 blocked=1 invalid=0 non_player_victim=0 "
            "non_player_source=1 self=0 unrelated=0 result_errors=0")
        if not status or status["blocked"] != 1 or status["non_player_source"] != 1:
            raise GateFailure("damage result self-test failed")
        evidence = temporary / "evidence"
        evidence.mkdir()
        (evidence / "result.json").write_text("{}\n", encoding="utf-8")
        archive_evidence(evidence, temporary / "evidence.tar.gz", "linux-x86_64")
        archive_evidence(evidence, temporary / "evidence.zip", "windows-x86_64")
        if not (temporary / "evidence.tar.gz").is_file() or not (temporary / "evidence.zip").is_file():
            raise GateFailure("evidence archive self-test failed")
    print("KeelS2 live runner self-test: PASS")


def run_gate(args: argparse.Namespace) -> int:
    bundle = Path(__file__).resolve().parent
    verify_manifest(bundle)
    config = json.loads((bundle / "GATE.json").read_text(encoding="utf-8"))
    platform_key = str(config["platform"])
    if (os.name == "nt") != (platform_key == "windows-x86_64"):
        raise GateFailure(f"this bundle targets {platform_key}")
    server_root = Path(args.server_root).expanduser().resolve()
    if not server_root.is_dir():
        raise GateFailure(f"server root was not found: {server_root}")
    actual_build = build_id(server_root, args.build_id)
    if actual_build != str(config["build_id"]):
        raise GateFailure(
            f"CS2 build changed: bundle={config['build_id']} server={actual_build}; recapture profiles")
    module, _, command = server_paths(server_root, platform_key)
    size, fnv = fnv1a64(module)
    if size != int(config["server_size"]) or fnv != int(str(config["server_fnv1a64"]), 16):
        raise GateFailure(
            f"server fingerprint changed: size={size} fnv1a64={fnv:016x}; recapture profiles")

    timestamp = dt.datetime.now(dt.timezone.utc).strftime("%Y%m%d-%H%M%S")
    evidence_name = (
        f"keels2-09-{config['platform_label']}-live-gate-{config['revision']}-"
        f"{timestamp}-evidence")
    work = Path(tempfile.mkdtemp(prefix="keels2-live-gate-"))
    evidence = work / evidence_name
    evidence.mkdir()
    transcript = Transcript(evidence / "server.log", args.verbose_server_output)
    print("KeelS2 0.9 live gate")
    print(f"Server: {server_root}")
    print(f"Client port: {args.port}")
    if args.verbose_server_output:
        print("Dedicated-server output is visible except while an action is required.")
    else:
        print("Dedicated-server output is hidden here and preserved in the evidence archive.")
    result: dict[str, object] = {
        "schema": 1,
        "build_id": actual_build,
        "profile": config["profile"],
        "platform": platform_key,
        "revision": config["revision"],
        "started_utc": dt.datetime.now(dt.timezone.utc).isoformat(),
        "passed": False,
    }
    (evidence / "server.sha256").write_text(
        f"{sha256(module)}  {module.name}\n", encoding="utf-8")
    (evidence / "fingerprint.txt").write_text(
        f"size={size}\nfnv1a64={fnv:016x}\n", encoding="utf-8")
    (evidence / "environment.txt").write_text(
        f"python={sys.version}\nos={os.name}\nplatform={sys.platform}\n", encoding="utf-8")
    shutil.copy2(bundle / "MANIFEST.txt", evidence / "BUNDLE_MANIFEST.txt")
    shutil.copy2(bundle / "GATE.json", evidence / "GATE.json")

    addon = safe_addon_path(server_root)
    gameinfo = server_root / "game" / "csgo" / "gameinfo.gi"
    if not gameinfo.is_file():
        raise GateFailure(f"gameinfo.gi was not found: {gameinfo}")
    backup = work / "backup"
    backup.mkdir()
    old_addon = backup / "keels2"
    old_gameinfo = backup / "gameinfo.gi"
    shutil.copy2(gameinfo, old_gameinfo)
    had_addon = addon.exists()
    if had_addon:
        shutil.copytree(addon, old_addon, symlinks=True)
    server: Server | None = None
    server_status: int | None = None
    failure: str | None = None
    try:
        if addon.exists():
            shutil.rmtree(addon)
        shutil.copytree(bundle / "payload" / "addons" / "keels2", addon)
        install_gameinfo_path(gameinfo)
        plugin_root = addon / "plugins" / str(config["platform_directory"])
        plugin_root.mkdir(parents=True, exist_ok=True)
        fixture_root = bundle / "fixtures"
        extension = str(config["plugin_extension"])
        retry_path = stage(fixture_root, plugin_root, "failing" + extension, "01_retry" + extension)
        stage(fixture_root, plugin_root, "keelhook_target" + extension, "01_keelhook_target" + extension)
        stage(fixture_root, plugin_root, "keelhook_peer" + extension, "02_keelhook_peer" + extension)
        stage(fixture_root, plugin_root, "published_provider" + extension, "03_published_provider" + extension)
        stage(fixture_root, plugin_root, "published_consumer" + extension, "04_published_consumer" + extension)
        stage(fixture_root, plugin_root, "source2_live" + extension, "05_source2_live" + extension)
        stage(fixture_root, plugin_root, "schema_live" + extension, "06_schema_live" + extension)
        stage(fixture_root, plugin_root, "lifecycle_live" + extension, "07_lifecycle_live" + extension)

        mutated = work / module.name
        shutil.copy2(module, mutated)
        with mutated.open("ab") as handle:
            handle.write(b"\0")
        mutated_size, mutated_fnv = fnv1a64(mutated)
        if mutated_size == size and mutated_fnv == fnv:
            raise GateFailure("stale-fingerprint mutation did not change the compatibility identity")
        result["stale_fingerprint_rejected"] = True

        command.extend([
            "-dedicated", "-console", "-usercon", "-insecure", "-nobots",
            "-port", str(args.port), "+game_type", "0", "+game_mode", "0",
            "+sv_hibernate_when_empty", "0", "+sv_cheats", "1",
            "+map", args.map,
        ])
        print()
        print("AUTOMATED PHASE 1/3: Starting CS2 and validating KeelS2")
        server = Server(command, server_root / "game", transcript)
        start = 0
        transcript.wait(
            f"selected compatibility profile: {config['profile']}", start, 180, server.process)
        transcript.wait("host started for cs2", start, 180, server.process)
        server.expect("keel inspect hooks", "Hook inspection complete")
        transcript.wait("Source 2 interface gateway load validation passed", start, 60, server.process)
        transcript.wait("schema field resolution passed", start, 60, server.process)
        transcript.wait("64 player server started", start, 60, server.process)
        server.expect("keel inspect hooks", "Hook inspection complete")
        transcript.wait("[Lifecycle Test] live GameFrame observed", start, 60, server.process)
        transcript.wait("versioned service consumed", start, 60, server.process)
        transcript.wait("resolver and incompatible-prototype checks passed", start, 60, server.process)

        server.expect("keel status", "KeelS2 status: running")
        server.expect("keel inspect profile", f"Compatibility identity: {config['profile']}")
        server.expect("keel inspect interfaces", "Source 2 interfaces")
        server.expect("keel inspect services", "Built-in services")
        server.expect("keel inspect resources", "Commands:")
        server.expect("keel inspect hooks", "Hook inspection complete")
        print("AUTOMATED PHASE 1/3: PASS")

        print()
        print("AUTOMATED PHASE 2/3: Exercising reload, dependency, and KeelHook behavior")
        shutil.copy2(fixture_root / ("basic" + extension), retry_path)
        server.expect(
            'keel plugins retry "Failing Test Plugin"',
            "plugin retry succeeded: KeelS2 Basic")
        for cycle in range(100):
            server.expect(
                'keel plugins reload "KeelS2 Basic"',
                "plugin reloaded transactionally: KeelS2 Basic")
            if (cycle + 1) % 10 == 0:
                print(f"Transactional reload cycles: {cycle + 1}/100")
        server.expect("keel_test hundred_cycle", "KeelS2 0.9.0 is active")
        shutil.copy2(fixture_root / ("failing" + extension), retry_path)
        for _ in range(5):
            server.expect(
                'keel plugins reload "KeelS2 Basic"',
                "plugin reload failed; previous image restored: KeelS2 Basic")
        shutil.copy2(fixture_root / ("basic" + extension), retry_path)
        server.expect(
            'keel plugins reload "KeelS2 Basic"',
            "plugin reloaded transactionally: KeelS2 Basic")
        server.expect('keel plugins pause "KeelS2 Basic"', "plugin paused:")
        server.expect(
            'keel plugins reload "KeelS2 Basic"',
            "plugin reloaded transactionally: KeelS2 Basic")
        server.expect('keel plugins resume "KeelS2 Basic"', "plugin resumed:")

        server.expect(
            'keel plugins pause "Published Service Provider"',
            "plugin pause is blocked by running dependent Published Service Consumer")
        server.expect(
            'keel plugins reload "Published Service Provider"',
            "plugin reload is blocked by running dependent Published Service Consumer")
        server.expect(
            'keel plugins unload "Published Service Provider"',
            "plugin unload is blocked by running dependent Published Service Consumer")
        server.expect("published_release", "service lease released")
        server.expect(
            'keel plugins unload "Published Service Provider"',
            "provider unloaded after publication withdrawal")
        server.expect("published_verify_gone", "withdrawn service is no longer queryable")
        server.expect('keel plugins unload "Published Service Consumer"', "plugin unloaded:")

        server.expect(
            "kh_run",
            "detour, virtual scopes, aggregate calls, ordering, recursion, action semantics, explicit control, and concurrency passed",
            90)
        server.expect('keel plugins pause "KeelHook Target Fixture"', "plugin paused:")
        server.expect('keel plugins resume "KeelHook Target Fixture"', "plugin resumed:")
        server.expect('keel plugins unload "KeelHook Peer Fixture"', "peer unload callback ran")
        server.expect("kh_after_peer", "peer cleanup and last-callback restoration passed")
        server.expect("kh_restore_retry", "callback restoration retry semantics passed")
        server.expect("kh_prepare_unload", "concurrent unload probe armed")
        server.expect(
            'keel plugins unload "KeelHook Target Fixture"',
            "automatic target-owner cleanup passed before module unload")
        print("AUTOMATED PHASE 2/3: PASS")

        if args.skip_gameplay:
            result["operator_gate_passed"] = True
            raise GateFailure("operator gate passed, but --skip-gameplay leaves the live gate incomplete")

        print()
        print("AUTOMATED PHASE 3/3: Preparing live-client validation")
        server.expect("keel_schema_entity_live snapshot", "entity snapshot captured")
        callback_files = (
            ("callback_observer" + extension, "08_callback_observer" + extension, "KeelS2 0.5E Observer"),
            ("callback_decision_a" + extension, "09_callback_decision_a" + extension, "KeelS2 0.5E Decision A"),
            ("callback_decision_b" + extension, "10_callback_decision_b" + extension, "KeelS2 0.5E Decision B"),
        )
        for source, target, name in callback_files:
            stage(fixture_root, plugin_root, source, target)
            server.expect(f'keel plugins load "{Path(target).stem}"', f"plugin loaded: {name}")
        no_damage_target = "11_no_damage" + extension
        stage(fixture_root, plugin_root, "no_damage" + extension, no_damage_target)
        server.expect(
            f'keel plugins load "{Path(no_damage_target).stem}"',
            "ready target=cs2.base_entity.take_damage policy=direct-player-weapons")

        connection_position = transcript.position()
        with action(transcript, 1, "Connect twice", (
            f"Connect to 127.0.0.1:{args.port} and allow the intentional rejection.",
            "Reconnect to the same address.",
            "Join Counter-Terrorists and wait until you are alive in-game.",
        )):
            transcript.wait(
                "NETWORK_DISCONNECT_REJECTED_BY_GAME",
                connection_position,
                ACTION_TIMEOUT,
                server.process)
            print("  DETECTED: intentional first-connection rejection")
            transcript.wait(
                "SIGNONSTATE_FULL",
                connection_position,
                ACTION_TIMEOUT,
                server.process)
            transcript.wait(
                f"verb=jointeam argument=3 slot={args.client_slot}",
                connection_position,
                ACTION_TIMEOUT,
                server.process)
        server.expect(f"s2_check {args.client_slot}", "Source 2 live runtime validation passed message_id=118")
        server.poll(
            "keel_schema_entity_live capture",
            "entity creation, lookup, and typed read passed",
            60)
        server.send("mp_limitteams 0")
        server.send("mp_autoteambalance 0")
        server.send("mp_friendlyfire 1")
        server.send("mp_freezetime 0")
        server.send("sv_cheats 1")
        server.send("bot_stop 1")
        server.send("bot_zombie 1")
        bot_position = server.send("bot_add_t")
        target_bot_position = server.send("bot_add_ct")
        server.send("mp_warmup_end")
        transcript.wait("<BOT><TERRORIST>", bot_position, 90, server.process)
        transcript.wait("<BOT><CT>", target_bot_position, 90, server.process)
        transcript.wait("event=round_start", bot_position, 90, server.process)

        status_position = server.send("keel_no_damage_status")
        transcript.wait("status ready=true", status_position, 30, server.process)
        baseline_damage = damage_result(transcript.text)
        if not baseline_damage:
            raise GateFailure("could not read the initial damage-hook counters")
        baseline_passthrough = (
            baseline_damage["non_player_source"] +
            baseline_damage["self"] +
            baseline_damage["unrelated"])
        command_rejection = (
            "[05E Decision A] ClientCommand priority=20 verb=keels2_blocked "
            f"argument= slot={args.client_slot} decision=reject")
        gameplay_position = transcript.position()
        reported: set[str] = set()
        with action(transcript, 2, "Run the gameplay probes", (
            "Open the CS2 developer console.",
            "Enter exactly: cmd keels2_blocked",
            "Close the console and shoot the stationary CT bot near your spawn once.",
            "Open the console and enter: hurtme 10",
            "Wait here; the runner verifies all three results automatically.",
        )):
            deadline = time.monotonic() + ACTION_TIMEOUT
            while True:
                status_position = server.send("keel_no_damage_status")
                transcript.wait("status ready=true", status_position, 10, server.process)
                current_damage = damage_result(transcript.text)
                if not current_damage:
                    raise GateFailure("could not read the damage-hook counters")
                if current_damage["result_errors"] != 0:
                    raise GateFailure("the damage hook could not set its superseding result")
                command_ok = command_rejection in transcript.text[gameplay_position:]
                blocked_ok = current_damage["blocked"] > baseline_damage["blocked"]
                passthrough = (
                    current_damage["non_player_source"] +
                    current_damage["self"] +
                    current_damage["unrelated"])
                passthrough_ok = passthrough > baseline_passthrough
                checks = (
                    ("command", command_ok, "client-command rejection"),
                    ("blocked", blocked_ok, "player weapon damage blocked"),
                    ("passthrough", passthrough_ok, "self damage passed through"),
                )
                for key, passed, label in checks:
                    if passed and key not in reported:
                        reported.add(key)
                        print(f"  DETECTED: {label}")
                if command_ok and blocked_ok and passthrough_ok:
                    break
                if time.monotonic() >= deadline:
                    missing = [label for _, passed, label in checks if not passed]
                    raise GateFailure(
                        "timed out waiting for gameplay probes: " + ", ".join(missing))
                time.sleep(1)

        disconnect_position = transcript.position()
        with action(transcript, 3, "Disconnect", (
            "Disconnect the client from the server.",
            "Remain disconnected while the runner validates the retired entity handle.",
        )):
            transcript.wait(
                "SIGNONSTATE_FULL -> SIGNONSTATE_NONE",
                disconnect_position,
                ACTION_TIMEOUT,
                server.process)
        server.poll(
            "keel_schema_entity_live stale",
            "entity destruction invalidation passed",
            60)

        reconnect_position = transcript.position()
        with action(transcript, 4, "Reconnect", (
            f"Reconnect to 127.0.0.1:{args.port}.",
            "Join Counter-Terrorists and wait until you are alive in-game.",
        )):
            transcript.wait(
                "SIGNONSTATE_FULL",
                reconnect_position,
                ACTION_TIMEOUT,
                server.process)
            transcript.wait(
                f"verb=jointeam argument=3 slot={args.client_slot}",
                reconnect_position,
                ACTION_TIMEOUT,
                server.process)
        server.poll(
            "keel_schema_entity_live replacement",
            "replacement entity validation passed",
            60)
        next_map = "de_inferno" if args.map != "de_inferno" else "de_dust2"
        map_position = server.send(f"changelevel {next_map}")
        transcript.wait("map epoch invalidation passed", map_position, 180, server.process)
        transcript.wait("LevelShutdown", map_position, 180, server.process)
        transcript.wait("LevelInit", map_position, 180, server.process)
        server.poll(
            "keel_schema_entity_live world",
            "post-reload world lookup and typed read passed",
            90)

        for name in (
            "KeelS2 0.5E Decision B",
            "KeelS2 0.5E Decision A",
            "KeelS2 0.5E Observer",
            "Source2 Service Test",
            "Schema Entity Live Gate",
            "Lifecycle Test",
            "KeelS2 No Player Damage",
            "KeelS2 Basic",
        ):
            server.expect(f'keel plugins unload "{name}"', "plugin unloaded:")

        text = transcript.text
        required = (
            "[05E Observer] ClientConnect priority=50",
            "[05E Observer] ClientCommand priority=50 verb=keels2_blocked "
            f"argument= slot={args.client_slot} decision=accept",
            command_rejection,
            "[05E Decision B] ClientCommand priority=20 verb=keels2_blocked "
            f"argument= slot={args.client_slot} decision=accept",
            "event=round_start",
            "dispatch benchmark ns/call: no-hook=",
            "concurrent callback retained host API access during unload",
        )
        missing = [marker for marker in required if marker not in text]
        connection_patterns = (
            (
                "Decision A connection rejection",
                r"\[05E Decision A\] ClientConnect priority=20 [^\r\n]* decision=reject",
            ),
            (
                "Decision B connection rejection",
                r"\[05E Decision B\] ClientConnect priority=20 [^\r\n]* decision=reject",
            ),
        )
        missing.extend(
            label for label, pattern in connection_patterns if not re.search(pattern, text))
        damage = damage_result(text)
        if missing:
            raise GateFailure(f"required live markers were not observed: {missing}")
        if not damage or damage["blocked"] < 1:
            raise GateFailure("no direct player-weapon damage was blocked")
        passthrough = damage["non_player_source"] + damage["self"] + damage["unrelated"]
        if passthrough < 1 or damage["result_errors"] != 0:
            raise GateFailure("world, fall, self, or unrelated damage passthrough was not demonstrated")
        result["damage"] = damage
        result["operator_gate_passed"] = True
        result["gameplay_gate_passed"] = True
        result["passed"] = True
        print("AUTOMATED PHASE 3/3: PASS")
    except Exception as error:
        failure = str(error)
        result["failure"] = failure
    finally:
        cleanup_errors: list[str] = []
        if server is not None:
            try:
                server_status = server.stop()
            except Exception as error:
                cleanup_errors.append(f"server shutdown: {error}")
        result["server_exit_status"] = server_status
        result["finished_utc"] = dt.datetime.now(dt.timezone.utc).isoformat()
        text = transcript.text
        result["successful_reload_count"] = text.count(
            "plugin reloaded transactionally: KeelS2 Basic")
        result["rollback_count"] = text.count(
            "plugin reload failed; previous image restored: KeelS2 Basic")
        forbidden = (
            "Segmentation fault",
            "core dumped",
            "plugin reload and rollback both failed",
            "automatic target-owner cleanup failed",
            "Source 2 live runtime validation failed",
            "map epoch invalidation failed",
            "profile-backed damage hook registration failed",
            "Convar 'bot_stop' is cheat protected, change ignored",
            "Convar 'bot_zombie' is cheat protected, change ignored",
            "unsupported cs2 server module",
            "could not load host",
        )
        observed_forbidden = [marker for marker in forbidden if marker in text]
        result["forbidden_markers"] = observed_forbidden
        if observed_forbidden:
            result["passed"] = False
            if not failure:
                failure = f"forbidden markers were observed: {observed_forbidden}"
                result["failure"] = failure
        if result.get("successful_reload_count", 0) < 102 or result.get("rollback_count", 0) < 5:
            result["passed"] = False
            if not failure:
                failure = "transactional reload counts did not reach 102 successes and 5 rollbacks"
                result["failure"] = failure
        if result.get("passed") and server_status not in (0, None):
            result["passed"] = False
            failure = f"server exited with status {server_status}"
            result["failure"] = failure

        try:
            if addon.exists():
                shutil.rmtree(addon)
            if had_addon:
                shutil.copytree(old_addon, addon, symlinks=True)
        except Exception as error:
            cleanup_errors.append(f"add-on restoration: {error}")
        try:
            shutil.copy2(old_gameinfo, gameinfo)
        except Exception as error:
            cleanup_errors.append(f"gameinfo restoration: {error}")
        result["installation_restored"] = not cleanup_errors
        if cleanup_errors:
            result["cleanup_errors"] = cleanup_errors
            result["passed"] = False
            cleanup_failure = "; ".join(cleanup_errors)
            failure = f"{failure}; cleanup failed: {cleanup_failure}" if failure else f"cleanup failed: {cleanup_failure}"
            result["failure"] = failure
        (evidence / "result.json").write_text(
            json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        suffix = ".zip" if platform_key == "windows-x86_64" else ".tar.gz"
        archive = bundle.parent / (evidence_name + suffix)
        try:
            archive_evidence(evidence, archive, platform_key)
        except Exception as error:
            print(f"Evidence directory: {evidence}")
            print(f"FAIL: could not create evidence archive: {error}")
            return 1
        digest = sha256(archive)
        print()
        print(f"Evidence archive: {archive}")
        print(f"SHA-256: {digest}")
        print("PASS" if result.get("passed") else f"FAIL: {failure or 'gate failed'}")
        shutil.rmtree(work)
    return 0 if result.get("passed") else 1


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("server_root", nargs="?")
    parser.add_argument("--build-id")
    parser.add_argument("--client-slot", type=int, default=0)
    parser.add_argument("--port", type=int, default=27035)
    parser.add_argument("--map", default="de_dust2")
    parser.add_argument("--skip-gameplay", action="store_true")
    parser.add_argument("--verbose-server-output", action="store_true")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        try:
            self_test()
            return 0
        except (GateFailure, OSError) as error:
            print(f"FAIL: {error}", file=sys.stderr)
            return 1
    if not args.server_root:
        parser.error("server_root is required")
    if not 1 <= args.port <= 65535 or args.client_slot < 0 or not re.fullmatch(r"[a-z0-9_]+", args.map):
        print("FAIL: invalid port, client slot, or map", file=sys.stderr)
        return 64
    try:
        return run_gate(args)
    except (GateFailure, OSError, ValueError, json.JSONDecodeError) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
