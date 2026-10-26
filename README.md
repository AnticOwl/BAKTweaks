# BAK Tweaks for Batman: Arkham Knight

Native C++/ImGui in-game overlay and modding tools for Batman: Arkham Knight. The project contains the DLL source, injector, build scripts, experiment notes, versioned releases, and third-party source dependencies needed to build it.

## Current development snapshot

The current source and packaged DLL are **v5.72**. The Pose tab and its Rocksteady runtime tracing are experimental; pose editing and animation freeze still require in-game validation.

## Live light editing

**Latest release whose notes explicitly carry forward live light editing: v5.27.** The runtime-tested baseline is **v5.25**: during a Steam session, Point/Spot actor movement updated the light matrices and the user confirmed live light and gizmo movement. The v5.26 notes say live editing from v5.25 is preserved; v5.27 preserves the v5.26 world-gizmo fix. See [`releases/v5.25/README.txt`](releases/v5.25/README.txt), [`releases/v5.26/README.txt`](releases/v5.26/README.txt), and [`releases/v5.27/README.txt`](releases/v5.27/README.txt).

Later versions focus on other features, especially Pose tooling. BatLit remains in the current source, but live light editing has not been revalidated on v5.72. Use the v5.27 package as the latest documented live-light build until a newer runtime test confirms it.

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
