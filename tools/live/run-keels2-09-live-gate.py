#!/usr/bin/env python3

from __future__ import annotations

import argparse
import ctypes
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


if os.name == "nt":
    from ctypes import wintypes

    class ConsoleCharacter(ctypes.Union):
        _fields_ = [
            ("UnicodeChar", wintypes.WCHAR),
            ("AsciiChar", wintypes.CHAR),
        ]

    class ConsoleKeyEvent(ctypes.Structure):
        _fields_ = [
            ("KeyDown", wintypes.BOOL),
            ("RepeatCount", wintypes.WORD),
            ("VirtualKeyCode", wintypes.WORD),
            ("VirtualScanCode", wintypes.WORD),
            ("Character", ConsoleCharacter),
            ("ControlKeyState", wintypes.DWORD),
        ]

    class ConsoleEvent(ctypes.Union):
        _fields_ = [("KeyEvent", ConsoleKeyEvent)]

    class ConsoleInputRecord(ctypes.Structure):
        _fields_ = [
            ("EventType", wintypes.WORD),
            ("Event", ConsoleEvent),
        ]


class WindowsConsoleInput:
    def __init__(self, target_pid: int):
        if os.name != "nt":
            raise GateFailure("Windows console input is unavailable on this platform")
        self.target_pid = target_pid

    def inject(self, command: str) -> None:
        if os.name != "nt":
            raise GateFailure("Windows console input is unavailable on this platform")
        target_pid = self.target_pid
        self.kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
        self.kernel32.FreeConsole.argtypes = []
        self.kernel32.FreeConsole.restype = wintypes.BOOL
        self.kernel32.AttachConsole.argtypes = [wintypes.DWORD]
        self.kernel32.AttachConsole.restype = wintypes.BOOL
        self.kernel32.CreateFileW.argtypes = [
            wintypes.LPCWSTR,
            wintypes.DWORD,
            wintypes.DWORD,
            wintypes.LPVOID,
            wintypes.DWORD,
            wintypes.DWORD,
            wintypes.HANDLE,
        ]
        self.kernel32.CreateFileW.restype = wintypes.HANDLE
        self.kernel32.WriteConsoleInputW.argtypes = [
            wintypes.HANDLE,
            ctypes.POINTER(ConsoleInputRecord),
            wintypes.DWORD,
            ctypes.POINTER(wintypes.DWORD),
        ]
        self.kernel32.WriteConsoleInputW.restype = wintypes.BOOL
        self.kernel32.CloseHandle.argtypes = [wintypes.HANDLE]
        self.kernel32.CloseHandle.restype = wintypes.BOOL
        self.user32 = ctypes.WinDLL("user32", use_last_error=True)
        self.user32.VkKeyScanW.argtypes = [wintypes.WCHAR]
        self.user32.VkKeyScanW.restype = wintypes.SHORT
        self.user32.MapVirtualKeyW.argtypes = [wintypes.UINT, wintypes.UINT]
        self.user32.MapVirtualKeyW.restype = wintypes.UINT

        self.kernel32.FreeConsole()
        deadline = time.monotonic() + 5
        while not self.kernel32.AttachConsole(target_pid):
            error = ctypes.get_last_error()
            if time.monotonic() >= deadline:
                raise GateFailure(
                    f"could not attach to Windows server console for PID {target_pid}: "
                    f"{ctypes.WinError(error)}")
            time.sleep(0.05)

        console = self.kernel32.CreateFileW(
            "CONIN$", 0xC0000000, 0x00000003, None, 3, 0, None)
        invalid = ctypes.c_void_p(-1).value
        if not console or console == invalid:
            error = ctypes.get_last_error()
            self.kernel32.FreeConsole()
            raise GateFailure(f"could not open Windows server console input: {ctypes.WinError(error)}")

        try:
            text = command + "\r"
            events = (ConsoleInputRecord * (len(text) * 2))()
            for index, character in enumerate(text):
                if character == "\r":
                    virtual_key = 0x0D
                    control_state = 0
                else:
                    translated = self.user32.VkKeyScanW(character)
                    if translated == -1:
                        raise GateFailure(
                            f"Windows console cannot translate command character: {character!r}")
                    virtual_key = translated & 0xFF
                    modifiers = (translated >> 8) & 0xFF
                    control_state = 0
                    if modifiers & 1:
                        control_state |= 0x0010
                    if modifiers & 2:
                        control_state |= 0x0008
                    if modifiers & 4:
                        control_state |= 0x0002
                scan_code = self.user32.MapVirtualKeyW(virtual_key, 0)
                if not scan_code:
                    raise GateFailure(
                        f"Windows console cannot map command character: {character!r}")
                for state in range(2):
                    event = events[index * 2 + state]
                    event.EventType = 0x0001
                    event.Event.KeyEvent.KeyDown = state == 0
                    event.Event.KeyEvent.RepeatCount = 1
                    event.Event.KeyEvent.VirtualKeyCode = virtual_key
                    event.Event.KeyEvent.VirtualScanCode = scan_code
                    event.Event.KeyEvent.Character.UnicodeChar = character
                    event.Event.KeyEvent.ControlKeyState = control_state
            written = wintypes.DWORD()
            if not self.kernel32.WriteConsoleInputW(
                    console, events, len(events), ctypes.byref(written)):
                raise GateFailure(
                    f"could not write Windows server console input: "
                    f"{ctypes.WinError(ctypes.get_last_error())}")
            if written.value != len(events):
                raise GateFailure("Windows server console input was only partially written")
        finally:
            self.kernel32.CloseHandle(console)
            self.kernel32.FreeConsole()

    def send(self, command: str) -> None:
        try:
            result = subprocess.run(
                [
                    sys.executable,
                    str(Path(__file__).resolve()),
                    "--windows-console-inject",
                    str(self.target_pid),
                    command,
                ],
                text=True,
                capture_output=True,
                timeout=10,
                creationflags=subprocess.CREATE_NO_WINDOW,
            )
        except subprocess.TimeoutExpired as error:
            raise GateFailure("Windows server console input helper timed out") from error
        if result.returncode != 0:
            message = result.stderr.strip() or result.stdout.strip()
            raise GateFailure(message or "Windows server console input helper failed")

    @classmethod
    def verify(cls, cwd: Path) -> None:
        shell = os.environ.get("ComSpec") or os.environ.get("COMSPEC")
        if not shell:
            system_root = os.environ.get("SystemRoot") or os.environ.get("SYSTEMROOT")
            if not system_root:
                raise GateFailure("could not locate cmd.exe for the Windows console self-test")
            shell = str(Path(system_root) / "System32" / "cmd.exe")
        startup = subprocess.STARTUPINFO()
        startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        startup.wShowWindow = 0
        process = subprocess.Popen(
            [shell, "/d", "/q"],
            cwd=cwd,
            creationflags=subprocess.CREATE_NEW_CONSOLE,
            startupinfo=startup,
        )
        marker = cwd / f"keels2_console_test_{os.getpid()}_{time.time_ns()}.tmp"
        try:
            cls(process.pid).send(f"copy nul {marker.name}")
            deadline = time.monotonic() + 5
            while not marker.is_file():
                if process.poll() is not None:
                    raise GateFailure(
                        f"Windows console self-test exited with status {process.returncode}")
                if time.monotonic() >= deadline:
                    raise GateFailure("Windows console self-test command was not executed")
                time.sleep(0.05)
            cls(process.pid).send("exit")
            status = process.wait(10)
            if status != 0:
                raise GateFailure(f"Windows console self-test exited with status {status}")
        finally:
            marker.unlink(missing_ok=True)
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(5)


class Server:
    def __init__(self, command: list[str], cwd: Path, transcript: Transcript):
        self.transcript = transcript
        self.master: int | None = None
        self.console_input: WindowsConsoleInput | None = None
        if os.name == "nt":
            self.process = subprocess.Popen(
                command,
                cwd=cwd,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                bufsize=0,
                creationflags=subprocess.CREATE_NEW_PROCESS_GROUP,
            )
            self.console_input = WindowsConsoleInput(self.process.pid)
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
        if self.console_input is not None:
            self.console_input.send(command)
        else:
            if self.master is None:
                raise GateFailure("server console input is unavailable")
            os.write(self.master, (command + "\n").encode("utf-8"))
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
    print("Do not press Enter in PowerShell. Complete the steps in CS2 and the runner advances automatically.")
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


def validate_client_console(text: str, stage: str, revision: str, platform_label: str) -> None:
    text = re.sub(r"^[ \t]*\[Client\][ \t]?", "", text, flags=re.MULTILINE)
    menu = (
        "KeelS2 Menu\n"
        "Usage: keel <command>\n"
        "  plugins  - Show active plugins\n"
        "  credits  - Project credits\n"
        "  version  - Version and build details"
    )
    if menu not in text or "[KeelS2]" in text:
        raise GateFailure("client console usage output is missing or server output was pasted")
    if stage == "information":
        required = (
            "KeelS2 1.1.0", "Built: ", " UTC", f"Git revision: {revision.split('-')[0]}",
            f"Target: {platform_label.capitalize()}/x86_64", "Plugin ABI: 4",
            "Created and developed by Peter Brev", "Official website: https://www.keels2.com/",
            "Listing 8 active plugins:", "KeelS2 Basic", "Source2 Service Test",
        )
    elif stage == "paused":
        required = ("Listing 7 active plugins:",)
        if re.search(r"^\s*\[\d+\].*KeelS2 Basic", text, re.MULTILINE):
            raise GateFailure("the paused plugin appeared in the client list")
    else:
        required = ("No active plugins.", "Plugin ABI: 4")
        if re.search(r"^\s*\[\d+\]", text, re.MULTILINE):
            raise GateFailure("plugins appeared after all plugins were unloaded")
    if any(marker not in text for marker in required):
        raise GateFailure(f"visible client console output is incomplete for {stage}")
    rows = re.findall(r"^\s*\[\d+\].* - (\w+)\s*$", text, re.MULTILINE)
    if any(state != "loaded" for state in rows):
        raise GateFailure("a non-running plugin appeared in the client list")
    expected_rows = {"information": 8, "paused": 7, "empty": 0}[stage]
    if len(rows) != expected_rows:
        raise GateFailure(f"expected one client list containing {expected_rows} plugins")


def client_console_check(transcript: Transcript, evidence: Path, config: dict, stage: str) -> None:
    commands = {
        "information": ("keel", "keel plugins", "keel credits", "keel version"),
        "paused": ("keel plugins", 'keel plugins unload "KeelS2 Basic"', "keel version extra"),
        "empty": ("keel plugins", "keel version", "keel inspect"),
    }[stage]
    previous_echo = transcript.set_echo(False)
    path = evidence / f"client-console-{stage}.txt"
    try:
        print()
        print(f"VISIBLE CLIENT CONSOLE CHECK: {stage}")
        print("Run each command directly in the connected CS2 player's developer console:")
        for command in commands:
            print(f"  {command}")
        print("Copy the commands and their visible responses from the CS2 console.")
        print("Paste them HERE in the runner terminal, then type END on a new line.")
        print("Do not paste dedicated-server or RCON output. If nothing appears, type END to record failure.")
        sys.stdout.flush()
        with path.open("w", encoding="utf-8") as output:
            for _ in range(512):
                line = input()
                if line.strip() == "END":
                    break
                output.write(line + "\n")
                output.flush()
            else:
                raise GateFailure("client console evidence exceeded 512 lines")
        validate_client_console(path.read_text(encoding="utf-8"), stage,
                                str(config["revision"]), str(config["platform_label"]))
        print(f"VISIBLE CLIENT CONSOLE CHECK {stage}: PASS (operator-provided client output)")
    finally:
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
        fingerprint_config = {"server_size": 3, "server_fnv1a64": "e71fa2190541574b"}
        validate_fingerprint(fingerprint_config, 3, 0xE71FA2190541574B)
        for candidate_size, candidate_fnv in ((4, 0xE71FA2190541574B), (3, 0xE71FA2190541574A)):
            try:
                validate_fingerprint(fingerprint_config, candidate_size, candidate_fnv)
            except GateFailure:
                pass
            else:
                raise GateFailure("fingerprint guard failed to reject a changed module")
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

        menu = (
            "KeelS2 Menu\n"
            "Usage: keel <command>\n"
            "  plugins  - Show active plugins\n"
            "  credits  - Project credits\n"
            "  version  - Version and build details"
        )
        valid_empty = menu + "\nNo active plugins.\nPlugin ABI: 4\n"
        rows = [f"  [{index:02}] {name} (1) by KeelS2 - loaded" for index, name in enumerate(
            ("Source2 Service Test", "Schema", "Lifecycle", "KeelS2 Basic",
             "Observer", "Decision A", "Decision B", "No Player Damage"), 1)]
        valid_paused = menu + "\nListing 7 active plugins:\n" + "\n".join(rows[:3] + rows[4:])
        for platform in ("linux", "windows"):
            valid_information = "\n".join((
                menu, "KeelS2 1.1.0", "Built: test UTC", "Git revision: test",
                f"Target: {platform.capitalize()}/x86_64", "Plugin ABI: 4",
                "Created and developed by Peter Brev", "Official website: https://www.keels2.com/",
                "Listing 8 active plugins:", *rows))
            for prefix in ("", "[Client] "):
                for stage, valid in (("information", valid_information),
                                     ("paused", valid_paused), ("empty", valid_empty)):
                    decorated = "\n".join(prefix + line for line in valid.splitlines())
                    validate_client_console(decorated, stage, "test", platform)
                for stage, invalid in (
                    ("information", valid_information.replace(rows[-1], "")),
                    ("information", valid_information.replace("- loaded", "- paused", 1)),
                    ("paused", valid_paused.replace(rows[0], rows[3])),
                    ("empty", valid_empty + rows[0]),
                    ("empty", ""),
                    ("empty", valid_empty.replace("No active plugins.", "Listing 1 plugins:")),
                    ("empty", "[KeelS2] " + valid_empty),
                ):
                    decorated = "\n".join(prefix + line for line in invalid.splitlines())
                    try:
                        validate_client_console(decorated, stage, "test", platform)
                    except GateFailure:
                        pass
                    else:
                        raise GateFailure("client console evidence guard accepted invalid output")

        if os.name == "nt":
            if ctypes.sizeof(ConsoleKeyEvent) != 16 or ctypes.sizeof(ConsoleInputRecord) != 20:
                raise GateFailure("Windows console input structure layout self-test failed")
            WindowsConsoleInput.verify(temporary)
    print("KeelS2 live runner self-test: PASS")


def validate_fingerprint(config: dict, size: int, fnv: int) -> None:
    if size != int(config["server_size"]) or fnv != int(str(config["server_fnv1a64"]), 16):
        raise GateFailure(
            f"server fingerprint changed: size={size} fnv1a64={fnv:016x}; recapture profiles")


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
    validate_fingerprint(config, size, fnv)

    timestamp = dt.datetime.now(dt.timezone.utc).strftime("%Y%m%d-%H%M%S")
    evidence_name = (
        f"keels2-10-{config['platform_label']}-live-gate-{config['revision']}-"
        f"{timestamp}-evidence")
    work = Path(tempfile.mkdtemp(prefix="keels2-live-gate-"))
    evidence = work / evidence_name
    evidence.mkdir()
    transcript = Transcript(evidence / "server.log", args.verbose_server_output)
    print("KeelS2 1.0 live gate")
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
        "command_transport": "attached-process-console" if platform_key == "windows-x86_64" else "pty",
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
        try:
            validate_fingerprint(config, mutated_size, mutated_fnv)
        except GateFailure:
            result["stale_fingerprint_rejected"] = True
            result["stale_fingerprint_check"] = "runner preflight guard"
        else:
            raise GateFailure("runner fingerprint guard accepted a changed module")

        command.extend([
            "-dedicated", "-console", "-usercon", "-insecure", "-nobots",
            "-port", str(args.port), "+game_type", "0", "+game_mode", "0",
            "+sv_hibernate_when_empty", "0", "+sv_cheats", "1",
            "+map", args.map,
        ])
        print()
        print("AUTOMATED PHASE 1/3: Starting CS2 and validating KeelS2")
        if platform_key == "windows-x86_64":
            print("Windows process-console transport: verifying")
            WindowsConsoleInput.verify(bundle)
            print("Windows process-console transport: PASS")
        print("CS2 startup: launching server")
        server = Server(command, server_root / "game", transcript)
        start = 0
        print("CS2 startup: waiting for the compatibility profile")
        transcript.wait(
            f"selected compatibility profile: {config['profile']}", start, 180, server.process)
        print("CS2 startup: compatibility profile selected")
        transcript.wait("host started for cs2", start, 180, server.process)
        print("CS2 startup: KeelS2 host ready")
        transcript.wait("Source 2 interface gateway load validation passed", start, 60, server.process)
        transcript.wait("schema field resolution passed", start, 60, server.process)
        transcript.wait("64 player server started", start, 60, server.process)
        print("CS2 startup: game server ready")
        if platform_key == "windows-x86_64":
            print("Windows command channel: verifying")
            server.expect(
                "echo KEELS2_WINDOWS_COMMAND_CHANNEL_READY",
                "KEELS2_WINDOWS_COMMAND_CHANNEL_READY")
            print("Windows command channel: ready")
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
        server.expect("s2_check factories", "managed factory live probes passed")
        result["factory_gate_passed"] = True
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
        server.expect("keel_test hundred_cycle", "KeelS2 1.0.0 is active")
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
        for example in ("stub", "sample"):
            target = "12_" + example
            stage(fixture_root, plugin_root, example + extension, target + extension)
            name = "KeelS2 Stub" if example == "stub" else "KeelS2 Source 2 Sample"
            server.expect(f'keel plugins load "{target}"', f"plugin loaded: {name}")
            if example == "sample":
                server.expect("keel_sample", "caller=-1 int=42 float=1.25")
                server.expect("keel_sample bump", "caller=-1 int=43 float=1.5")
                server.expect("keel_sample invalid", "usage: keel_sample [bump]")
            server.expect(f'keel plugins reload "{name}"', f"plugin reloaded transactionally: {name}")
            if example == "sample":
                server.expect("keel_sample", "caller=-1 int=43 float=1.5")
                server.expect("keel_sample bump", "caller=-1 int=44 float=1.75")
            server.expect(f'keel plugins unload "{name}"', "plugin unloaded:")
            server.expect(f'keel plugins load "{target}"', f"plugin loaded: {name}")
            if example == "sample":
                server.expect("keel_sample", "caller=-1 int=44 float=1.75")
            server.expect(f'keel plugins unload "{name}"', "plugin unloaded:")
        result["examples_gate_passed"] = True
        print("AUTOMATED PHASE 2/3: PASS")

        if args.skip_gameplay:
            result["operator_gate_passed"] = True
            raise GateFailure("operator gate passed, but --skip-gameplay leaves the live gate incomplete")

        print()
        print("AUTOMATED PHASE 3/3: Preparing live-client validation")
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
            f"Connect to {args.connect_address or '<reachable-server-address>'}:{args.port} and allow the intentional rejection.",
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
        client_console_check(transcript, evidence, config, "information")
        server.expect('keel plugins pause "KeelS2 Basic"', "plugin paused:")
        client_console_check(transcript, evidence, config, "paused")
        server.expect('keel plugins info "KeelS2 Basic"', "State: paused")
        server.expect('keel plugins resume "KeelS2 Basic"', "plugin resumed:")

        server.send("mp_limitteams 0")
        server.send("mp_autoteambalance 0")
        server.send("mp_friendlyfire 1")
        server.send("mp_freezetime 0")
        server.send("sv_cheats 1")
        server.send("bot_stop 1")
        server.send("bot_zombie 1")
        server.expect("keel_schema_entity_live snapshot", "entity snapshot captured")
        bot_position = server.send("bot_add_t")
        target_bot_position = server.send("bot_add_ct")
        server.send("mp_warmup_end")
        transcript.wait("<BOT><TERRORIST>", bot_position, 90, server.process)
        transcript.wait("<BOT><CT>", target_bot_position, 90, server.process)
        transcript.wait("event=round_start", bot_position, 90, server.process)
        server.poll(
            "keel_schema_entity_live capture",
            "entity creation, lookup, and typed read passed",
            60)

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
            "[05E Decision A] ClientCommand priority=20 verb=jointeam "
            f"argument=2 slot={args.client_slot} decision=reject")
        gameplay_position = transcript.position()
        reported: set[str] = set()
        with action(transcript, 2, "Run the gameplay probes", (
            "Open the CS2 developer console.",
            "Enter exactly: jointeam 2, then press Enter. You should remain Counter-Terrorist.",
            "Close the console and shoot the stationary CT bot near your spawn once.",
            "Open the console, enter exactly: hurtme 10, then press Enter.",
            "Wait here; the runner verifies all three results automatically.",
        )):
            deadline = time.monotonic() + ACTION_TIMEOUT
            next_waiting_report = time.monotonic() + 30
            current_damage = baseline_damage
            blocked_ok = False
            passthrough_ok = False
            while True:
                if not blocked_ok or not passthrough_ok:
                    status_position = server.send("keel_no_damage_status")
                    transcript.wait("status ready=true", status_position, 10, server.process)
                    current_damage = damage_result(transcript.text)
                    if not current_damage:
                        raise GateFailure("could not read the damage-hook counters")
                    if current_damage["result_errors"] != 0:
                        raise GateFailure("the damage hook could not set its superseding result")
                    blocked_ok = current_damage["blocked"] > baseline_damage["blocked"]
                    passthrough = (
                        current_damage["non_player_source"] +
                        current_damage["self"] +
                        current_damage["unrelated"])
                    passthrough_ok = passthrough > baseline_passthrough
                command_ok = command_rejection in transcript.text[gameplay_position:]
                checks = (
                    ("command", command_ok, "client-command rejection"),
                    ("blocked", blocked_ok, "player weapon damage blocked"),
                    ("passthrough", passthrough_ok, "self damage passed through"),
                )
                changed = False
                for key, passed, label in checks:
                    if passed and key not in reported:
                        reported.add(key)
                        print(f"  DETECTED: {label}")
                        changed = True
                if command_ok and blocked_ok and passthrough_ok:
                    break
                missing = [label for _, passed, label in checks if not passed]
                now = time.monotonic()
                if changed or now >= next_waiting_report:
                    print("  WAITING: " + ", ".join(missing))
                    next_waiting_report = now + 30
                if now >= deadline:
                    raise GateFailure(
                        "timed out waiting for gameplay probes: " + ", ".join(missing))
                time.sleep(1)

        disconnect_position = transcript.position()
        with action(transcript, 3, "Disconnect", (
            "Disconnect the client from the server.",
            "Once detected, the runner removes its probe bots automatically.",
            "Remain disconnected while it validates the retired entity handle.",
        )):
            transcript.wait(
                "SIGNONSTATE_FULL -> SIGNONSTATE_NONE",
                disconnect_position,
                ACTION_TIMEOUT,
                server.process)
        print("AUTOMATED: Removing probe bots and validating their retired entity handles")
        server.send("bot_kick all")
        server.poll(
            "keel_schema_entity_live stale",
            "entity destruction invalidation passed",
            60)
        print("AUTOMATED: Retired entity handle validation PASS")

        reconnect_position = transcript.position()
        with action(transcript, 4, "Reconnect", (
            f"Reconnect to {args.connect_address or '<reachable-server-address>'}:{args.port}.",
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
        print("AUTOMATED: Creating a replacement bot and validating its entity handle")
        replacement_position = server.send("bot_add_ct")
        transcript.wait(
            "ClientPutInServer create new player controller",
            replacement_position,
            90,
            server.process)
        restart_position = server.send("mp_restartgame 1")
        transcript.wait("event=round_start", restart_position, 90, server.process)
        server.poll(
            "keel_schema_entity_live replacement",
            "replacement entity validation passed",
            60)
        print("AUTOMATED: Replacement entity handle validation PASS")
        next_map = "de_inferno" if args.map != "de_inferno" else "de_dust2"
        print(f"AUTOMATED: Changing level to {next_map} and validating map-epoch invalidation")
        map_position = server.send(f"changelevel {next_map}")
        transcript.wait("map epoch invalidation passed", map_position, 180, server.process)
        transcript.wait("LevelShutdown", map_position, 180, server.process)
        transcript.wait("LevelInit", map_position, 180, server.process)
        print("AUTOMATED: Map-epoch invalidation PASS")
        print("AUTOMATED: Validating the post-reload world lookup")
        server.poll(
            "keel_schema_entity_live world",
            "post-reload world lookup and typed read passed",
            90)
        print("AUTOMATED: Post-reload world lookup PASS")

        print("AUTOMATED: Unloading live fixtures")
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
        print("AUTOMATED: Live fixture unload PASS")
        client_console_check(transcript, evidence, config, "empty")
        result["client_console_gate_passed"] = True
        result["client_console_evidence"] = "operator-provided visible CS2 developer-console responses"


        text = transcript.text
        required = (
            "managed factory live probes passed engine=observed server=export null=replaced original=forwarded removal=restored",
            "[05E Observer] ClientConnect priority=50",
            "[05E Observer] ClientCommand priority=50 verb=jointeam "
            f"argument=2 slot={args.client_slot} decision=accept",
            command_rejection,
            "[05E Decision B] ClientCommand priority=20 verb=jointeam "
            f"argument=2 slot={args.client_slot} decision=accept",
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
            "managed factory live probes failed",
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
            failure = f"could not create evidence archive: {error}"
            result["failure"] = failure
            result["passed"] = False
            (evidence / "result.json").write_text(
                json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8")
            print(f"Evidence directory: {evidence}")
            print(f"FAIL: {failure}")
        else:
            digest = sha256(archive)
            print()
            print(f"Evidence archive: {archive}")
            print(f"SHA-256: {digest}")
            print("PASS" if result.get("passed") else f"FAIL: {failure or 'gate failed'}")
            shutil.rmtree(work)
    return 0 if result.get("passed") else 1


def main() -> int:
    if len(sys.argv) == 4 and sys.argv[1] == "--windows-console-inject":
        try:
            target_pid = int(sys.argv[2])
            if target_pid <= 0:
                raise GateFailure("Windows server console PID is invalid")
            WindowsConsoleInput(target_pid).inject(sys.argv[3])
            return 0
        except (GateFailure, OSError, ValueError) as error:
            print(f"FAIL: {error}", file=sys.stderr)
            return 1
    parser = argparse.ArgumentParser()
    parser.add_argument("server_root", nargs="?")
    parser.add_argument("--build-id")
    parser.add_argument("--client-slot", type=int, default=0)
    parser.add_argument("--port", type=int, default=27035)
    parser.add_argument("--connect-address", help="server address reachable from the CS2 client")
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
