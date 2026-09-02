# KeelS2

KeelS2 is a native C++ plugin framework and detour platform for Source 2 dedicated servers.

## Build

Requirements: CMake 3.25+, Python 3, a C++23-capable compiler, and a 64-bit Linux or Windows environment.

### Linux

```bash
cmake -S . -B build -DKEELS2_GAME=cs2 -DCMAKE_BUILD_TYPE=RelWithDebInfo -DBUILD_TESTING=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

### Windows

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DKEELS2_GAME=cs2 -DBUILD_TESTING=ON
cmake --build build --config RelWithDebInfo --parallel
ctest --test-dir build -C RelWithDebInfo --output-on-failure
```

Runtime files are staged under `build/package/addons/keels2/`.

## 0.9 surface

KeelS2 0.9 provides a versioned C plugin ABI with a C++ facade, dependency-aware lifecycle management, transactional reload and retry, plugin-published versioned services, exact Source 2 factory queries, owned commands and ConVars, lifecycle and game-event subscriptions, schema and entity access, and virtual, symbol, address, pattern, and compatibility-profile hooks.

KeelHook supports priority-ordered pre/post callbacks, override and supersede outcomes, typed original calls, changed-argument recall, callback pausing, recursive dispatch, and automatic unload cleanup. The `keels2_no_damage` sample demonstrates the profile-backed CS2 damage target while leaving world, fall, bomb, self, and unrelated damage unchanged.

A successful recall continues at the next pending pre callback with the frame's changed arguments and any earlier override, executes the original at most once, runs the recalled post chain once, and replaces the remainder of the outer dispatch. A successful explicit original call also consumes the dispatch's single original-call opportunity.

Operator commands include `keel plugins load|unload|reload|retry|pause|resume|list|info|cmds` and `keel inspect hooks|interfaces|services|resources|profile`.

The generic host loads a game adapter module. The CS2 adapter is shipped as `keels2_game_cs2`, and the installed `KeelS2::AdapterSDK` target supports out-of-tree adapters.

From a clean checkout, the release tool builds, tests, packages, and verifies the current platform in one command:

```bash
python tools/release.py build --project-suffix dev --configuration Release
```

Verified runtime and standalone SDK archives, content hashes, archive hashes, and build manifests are written under `out/releases/v0.9.0/`. Release archives are deterministic for a fixed source commit and toolchain.

The same release build also creates platform-specific CS2 compatibility-profile capture and 0.9 live-gate archives. The live gate verifies the current build and server fingerprint before deployment, backs up and restores the existing KeelS2 installation and `gameinfo.gi`, exercises 100 successful transactional reloads and five rollbacks, and writes a self-contained evidence archive even when a stage fails. Its guided gameplay phase covers real interface/runtime calls, client callbacks, events, entity invalidation, map changes, and the profile-backed no-player-damage hook.
