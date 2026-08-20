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

From a clean checkout, the release tool builds, tests, packages, and verifies the current platform in one command:

```bash
python tools/release.py build --project-suffix dev --configuration Release
```

Verified archives and their manifests are written under `out/releases/v0.1.0-dev/`.
