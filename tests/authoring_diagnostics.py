import argparse
from pathlib import Path
import subprocess


def run(command, log):
    result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    log.write_text(result.stdout, encoding="utf-8")
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", required=True)
    parser.add_argument("--build", required=True)
    parser.add_argument("--sdk", required=True)
    parser.add_argument("--source-sdk", required=True)
    parser.add_argument("--generator", required=True)
    parser.add_argument("--platform", default="")
    parser.add_argument("--toolset", default="")
    parser.add_argument("--configuration", required=True)
    args = parser.parse_args()
    build = Path(args.build)
    build.mkdir(parents=True, exist_ok=True)
    command = ["cmake", "-S", args.source, "-B", str(build), "-G", args.generator,
               f"-DKeelS2_DIR={args.sdk}", f"-DKEELS2_SOURCE_SDK_ROOT={args.source_sdk}",
               f"-DCMAKE_BUILD_TYPE={args.configuration}"]
    if args.platform:
        command += ["-A", args.platform]
    if args.toolset:
        command += ["-T", args.toolset]
    configured = run(command, build / "configure.log")
    if configured.returncode:
        raise SystemExit(configured.stdout)
    cases = {
        "positive": None,
        "command": "KeelS2 command callback must be void Plugin::Method(const CCommandContext&, const CCommand&)",
        "event": "KeelS2 game event callback must be void Plugin::Method(IGameEvent*)",
        "hook_return": "KeelS2 hook callback must return Action or void and match the target arguments",
        "hook_argument": "KeelS2 hook callback must return Action or void and match the target arguments",
        "convar": "KeelS2 ConVar<T> supports bool, int32, float, and CUtlString",
        "metadata_missing": "KEELS2_PLUGIN requires static constexpr PluginInfo Info with nonempty",
        "metadata_invalid": "KEELS2_PLUGIN requires static constexpr PluginInfo Info with nonempty",
    }
    for target, diagnostic in cases.items():
        result = run(["cmake", "--build", str(build), "--config", args.configuration,
                      "--target", target], build / f"{target}.log")
        if diagnostic is None:
            if result.returncode:
                raise SystemExit("valid external consumer failed:\n" + result.stdout)
        elif result.returncode == 0 or diagnostic not in result.stdout:
            raise SystemExit(f"{target}: expected a failed compile naming the contract:\n{result.stdout}")
        print(f"{target}: passed")


if __name__ == "__main__":
    main()
