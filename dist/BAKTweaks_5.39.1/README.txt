BAK Tweaks v5.39 — Arkham Knight

Standalone injector:
- Run BAKInjector.exe. It requests administrator elevation through its embedded UAC manifest; no PowerShell is used.
- The app detects a running Steam/Epic BatmanAK.exe and only offers the sibling BAKTweaks.dll and BAKPoseProbeLauncher.dll.
- It refuses duplicate module loads. Restart the game to replace a loaded BAKTweaks.dll.

Pose tooling:
- In the Pose tab, refresh and select the Catwoman Pawn and a bone.
- Enable “Freeze selected Pawn pose” to capture and reapply all evaluated component-space SpaceBases after each animation update.
- The counters show finalizer calls, selected-mesh matches, captured bone count, freeze writes, and selected-bone translation writes. A zero match count means the runtime callback did not reach the selected mesh.
- To pose one bone on top of the frozen pose, set a small XYZ offset and enable “Apply offset to selected bone”.
- Turn both controls off to disable the hook and restore normal animation.
- First test: freeze the current pose and report the counters plus whether the character stops animating. If it works, try +10 on one axis for a non-root bone.

This is an opt-in prototype for the verified Steam build and Pawn-based characters. Rotation editing, descendant propagation, non-Pawn characters, the viewport gizmo, and Epic offsets remain to be implemented. Translation offsets are applied on each pose update; the future gizmo will write to this live state as it is dragged, without an Apply step. No save data is written.

Injector patch 1.0.1: UTF-8 UI text rendering fix. The DLL remains BAK Tweaks v5.39.
