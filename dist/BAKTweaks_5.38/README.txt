BAK Tweaks v5.38 — Arkham Knight

Pose tooling:
- In the Pose tab, refresh and select the Catwoman Pawn and a bone.
- Enable “Freeze selected Pawn pose” to capture and reapply all evaluated component-space SpaceBases after each animation update.
- The counters show finalizer calls, selected-mesh matches, captured bone count, freeze writes, and selected-bone translation writes. A zero match count means the runtime callback did not reach the selected mesh.
- To pose one bone on top of the frozen pose, set a small XYZ offset and enable “Apply offset to selected bone”.
- Turn both controls off to disable the hook and restore normal animation.
- First test: freeze the current pose and report the counters plus whether the character stops animating. If it works, try +10 on one axis for a non-root bone.

This is an opt-in prototype for the verified Steam build and Pawn-based characters. Rotation editing, descendant propagation, non-Pawn characters, and Epic offsets are not implemented yet. It does not write save data. No visual test of this version has been completed.

To inject, run tools\BAKInjector.cmd. Replacing a loaded BAKTweaks DLL requires restarting the game. The one-shot pose probe self-unloads.
