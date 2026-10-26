BAK Tweaks v5.37 — Arkham Knight

Pose edit prototype:
- The Pose tab includes an opt-in component-space XYZ translation offset for the currently selected bone on the selected Pawn mesh.
- It starts disabled with a zero offset. The guarded Steam-only pose-finalizer hook is installed only when the option is enabled.
- To return to the animated pose, turn off “Apply offset to selected bone”; the hook is disabled and the next normal animation update restores the generated transform.
- First visual test: select a non-root bone, apply +10 to one axis, observe the character, then turn the option off. Tell Codex what moved and whether it returned to animation.
- Avoid large offsets. The tool currently edits translation only; rotation, descendant propagation, non-Pawn characters, and Epic offsets are not implemented.

The pose hook checks the executable signature, selected mesh, reflected SpaceBases TArray bounds, and writable array memory. It does not modify save data. This prototype still needs an in-game visual test before pose editing is expanded.

To inject, run tools\BAKInjector.cmd. The one-shot pose probe self-unloads. Replacing a loaded BAKTweaks DLL requires restarting the game.
