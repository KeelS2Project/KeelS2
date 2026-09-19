"""Build the reference plugins with an installed SDK matching this source tree."""
import argparse
import hashlib
import json
import platform
import subprocess
from pathlib import Path
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parent
REPO = ROOT.parents[1]


def run(*arguments):
    print("+", " ".join(map(str, arguments)), flush=True)
    subprocess.run(list(map(str, arguments)), check=True)


def digest(path):
    return hashlib.sha256(path.read_bytes().replace(b"\r\n", b"\n")).hexdigest()


def verify_sdk(sdk):
    headers = {path.relative_to(REPO / "sdk/include").as_posix(): digest(path)
               for path in sorted((REPO / "sdk/include").rglob("*")) if path.is_file()}
    installed = {path.relative_to(sdk / "include").as_posix(): digest(path)
                 for path in sorted((sdk / "include").rglob("*")) if path.is_file()}

    if installed != headers:
        differences = sorted(name for name in headers.keys() | installed.keys()
                             if headers.get(name) != installed.get(name))
        raise RuntimeError("Installed SDK does not match this source tree: " + ", ".join(differences))

    configs = list(sdk.glob("lib*/cmake/KeelS2/KeelS2Config.cmake"))

    if len(configs) != 1:
        raise RuntimeError("Expected one installed KeelS2 CMake package under the SDK prefix")

    for name in ("KeelS2Plugin.cmake", "KeelS2GameAdapter.cmake", "KeelS2SourceSDK.cmake"):
        if digest(configs[0].parent / name) != digest(REPO / "cmake" / name):
            raise RuntimeError("Installed SDK CMake helper does not match this source tree: " + name)

    return headers, configs[0].parent


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--work", required=True, type=Path)
    parser.add_argument("--sdk", type=Path, help="Matching installed SDK prefix; otherwise install it from this source tree")
    parser.add_argument("--source-sdk", type=Path, help="Existing pinned HL2SDK checkout")
    args = parser.parse_args()
    system = platform.system()

    if system not in ("Linux", "Windows") or platform.machine().lower() not in ("x86_64", "amd64"):
        parser.error("Use native Linux x64 or Windows x64")

    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    result_path = work / "result.json"
    result_path.unlink(missing_ok=True)
    source_flags = [f"-DKEELS2_SOURCE_SDK_ROOT={args.source_sdk.resolve()}"] if args.source_sdk else []
    generator_flags = ["-A", "x64"] if system == "Windows" else []
    sdk = args.sdk.resolve() if args.sdk else work / "sdk"

    if not args.sdk:
        run("cmake", "-S", REPO, "-B", work / "sdk-build", "-DBUILD_TESTING=OFF",
            "-DCMAKE_BUILD_TYPE=Release", f"-DCMAKE_INSTALL_PREFIX={sdk}", *source_flags, *generator_flags)
        run("cmake", "--install", work / "sdk-build", "--config", "Release")

    headers, config = verify_sdk(sdk)
    build = work / "build"
    run("cmake", "-S", ROOT, "-B", build, "-DCMAKE_BUILD_TYPE=Release", "-DBUILD_TESTING=ON",
        f"-DKeelS2_DIR={config}", *source_flags, *generator_flags)
    run("cmake", "--build", build, "--config", "Release", "--parallel", "4")
    modules = [Path(line) for line in (build / "reference-modules-Release.txt").read_text().splitlines()]

    if not modules or len(modules) != len(set(modules)) or any(not path.is_file() for path in modules):
        raise RuntimeError("A configured reference module is missing or duplicated")

    report = work / "tests.xml"
    run("ctest", "--test-dir", build, "-C", "Release", "--output-on-failure", "--output-junit", report)
    tests = [case.attrib["name"] for case in ET.parse(report).iter("testcase")]

    if not tests:
        raise RuntimeError("No installed SDK contracts ran")

    abi_tests = [name for name in tests if name.endswith("_abi")]
    cpp_tests = [name for name in tests if name.endswith("_cpp")]

    if len(abi_tests) + len(cpp_tests) != len(tests):
        raise RuntimeError("An SDK example test has no recorded C ABI or C++ category")

    revision = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=REPO, text=True).strip()
    dirty = bool(subprocess.check_output(["git", "status", "--porcelain"], cwd=REPO))
    sources = sorted(path for folder in (ROOT, REPO / "samples", REPO / "tests/fixtures/adapter")
                     for path in folder.rglob("*") if path.suffix in (".cpp", ".h"))
    record = {
        "status": "compiled", "version": "1.0.0", "source_revision": revision, "dirty": dirty,
        "platform": system, "sdk_headers": headers,
        "source_sha256": {path.relative_to(REPO).as_posix(): digest(path) for path in sources},
        "contract_source_sha256": {path.relative_to(REPO).as_posix(): digest(path)
                                   for path in sorted((REPO / "tests").glob("*_c_abi.c"))},
        "abi_contracts": abi_tests,
        "cpp_examples": cpp_tests,
        "modules": {path.name: hashlib.sha256(path.read_bytes()).hexdigest() for path in modules},
        "game_execution": "Not performed by this build; requires a matching runtime and real server."
    }
    result_path.write_text(json.dumps(record, indent=2) + "\n")
    print(f"Built {len(modules)} reference/sample modules; {len(tests)} installed-SDK contracts passed. Results: {result_path}")


if __name__ == "__main__":
    main()
