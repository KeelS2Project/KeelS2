import argparse
import pathlib
import re
import shutil
import subprocess
import sys


EXPECTED = {
    "bootstrap": {"CreateInterface"},
    "host": {"KeelHost_Start", "KeelHost_CompleteStartup", "KeelHost_Stop"},
    "adapter": {"KeelGameAdapter_Query"},
    "plugin": {"KeelPlugin_Query", "KeelPlugin_Load", "KeelPlugin_Unload"},
}


def fail(message):
    raise SystemExit(message)


def run(command):
    result = subprocess.run(command, text=True, capture_output=True, check=False)
    if result.returncode != 0:
        fail(result.stderr.strip() or result.stdout.strip() or "export inspection failed")
    return result.stdout


def linux_exports(path):
    program = shutil.which("nm") or shutil.which("llvm-nm")
    if not program:
        fail("nm or llvm-nm is required")
    output = run([program, "-D", "--defined-only", "--format=posix", str(path)])
    return {line.split()[0].split("@@", 1)[0] for line in output.splitlines() if line.split()}


def windows_exports(path):
    program = shutil.which("dumpbin") or shutil.which("llvm-readobj")
    if not program:
        fail("dumpbin or llvm-readobj is required")
    if pathlib.Path(program).name.lower().startswith("dumpbin"):
        output = run([program, "/nologo", "/exports", str(path)])
        return {
            match.group(1)
            for line in output.splitlines()
            if (match := re.match(r"\s*\d+\s+[0-9A-Fa-f]+\s+[0-9A-Fa-f]+\s+(\S+)", line))
        }
    output = run([program, "--coff-exports", str(path)])
    return {
        match.group(1)
        for line in output.splitlines()
        if (match := re.match(r"\s*Name:\s*(\S+)", line))
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("entries", nargs="+")
    arguments = parser.parse_args()
    inspect = windows_exports if sys.platform == "win32" else linux_exports
    seen = set()
    for value in arguments.entries:
        role, separator, path_text = value.partition("=")
        if not separator or role not in EXPECTED or role in seen:
            fail(f"invalid export-contract entry: {value}")
        path = pathlib.Path(path_text)
        if not path.is_file():
            fail(f"binary not found: {path}")
        actual = inspect(path)
        if actual != EXPECTED[role]:
            fail(
                f"{role} exports differ: expected={sorted(EXPECTED[role])} "
                f"actual={sorted(actual)}"
            )
        seen.add(role)
    if seen != set(EXPECTED):
        fail(f"missing export-contract roles: {sorted(set(EXPECTED) - seen)}")


if __name__ == "__main__":
    main()
