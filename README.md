# BAK Tweaks for Batman: Arkham Knight

Native C++/ImGui in-game overlay and modding tools for Batman: Arkham Knight. The project contains the DLL source, injector, build scripts, experiment notes, versioned releases, and third-party source dependencies needed to build it.

## Current development snapshot

The current source and packaged DLL are **v5.72**. This and every version newer than v5.27 is **work in progress (WIP)**. Pose tooling and Rocksteady runtime tracing are experimental; pose editing and animation freeze still require in-game validation. Do not treat v5.28+ as a stable live-light release.

## Live Spot lights

**Use v5.27 as the latest documented version for live Spot lights.** The direct in-game confirmation is from **v5.25** on Steam: Spot/Point actor movement updated the light matrices, and the user confirmed the live light and gizmo moved. The v5.26 notes explicitly preserve v5.25 live editing; v5.27 preserves the v5.26 world-gizmo fix. See [`releases/v5.25/README.txt`](releases/v5.25/README.txt), [`releases/v5.26/README.txt`](releases/v5.26/README.txt), and [`releases/v5.27/README.txt`](releases/v5.27/README.txt).

**v5.28 and all newer versions are WIP.** They focus on Pose tooling. BatLit is still present in the source, but Spot live control has not been revalidated on those versions and is not guaranteed. Use the v5.27 package when you need the documented live Spot behavior.

## Build

- Windows x64
- Visual Studio C++ toolchain and Windows SDK
- CMake 3.20 or newer
- Run `Fetch-Dependencies.ps1` if the checked-in `third_party` dependencies are absent, then `Build-Release.ps1`.

The Release output is a single `BAKTweaks.dll` plus the standalone injector. Copy a versioned package from `dist/` or `releases/`; do not use generated `build*/` trees as user-facing releases.

## Project layout

- `src/`, `res/`, `tools/` — implementation, resources, and injector
- `third_party/` — ImGui and MinHook source
- `experiments/` — research notes and measured results
- `releases/`, `bin/`, `dist/` — preserved versioned builds and packages
- `README.txt` — detailed feature/version history
