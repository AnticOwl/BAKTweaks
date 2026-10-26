BAK Tweaks v5.40 — Arkham Knight

Standalone injector:
- Run BAKInjector.exe. It requests administrator elevation through its embedded UAC manifest; no PowerShell is used.
- The app detects a running Steam/Epic BatmanAK.exe and only offers the sibling BAKTweaks.dll and BAKPoseProbeLauncher.dll.
- It refuses duplicate module loads. Restart the game to replace a loaded BAKTweaks.dll.

Pose tooling:
- In the Pose tab, refresh and select the active player Pawn. “Player #17” is the roster index for the current Catwoman Pawn, not a character name.
- Enable “Freeze selected Pawn pose” to capture the current evaluated SpaceBases pose after EngineTick, then restore it after every following tick. The rest of the game continues running.
- The counters show post-tick passes, valid pose arrays, captured bone count, freeze writes, and selected-bone translation writes. Valid arrays and freeze writes should increase while freeze is enabled.
- To pose one bone on top of the frozen pose, set a small XYZ offset and enable “Apply offset to selected bone”.
- Turn both controls off to return to normal animation.
- First test: freeze Catwoman and report the counters plus whether visible animation stops. If it works, try +10 on one axis for a non-root bone.

This remains an opt-in prototype for the verified Steam build and Pawn-based characters. Rotation editing, descendant propagation, non-Pawn characters, and the viewport gizmo remain in development. Translation offsets are applied in real time; the future gizmo will write to this live state as it is dragged, without an Apply step. No save data is written.
