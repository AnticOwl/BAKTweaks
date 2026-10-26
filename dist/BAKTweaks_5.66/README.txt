BAK Tweaks v5.66 — Arkham Knight

Run BAKInjector.exe after BatmanAK.exe is running and choose BAK Tweaks. Restart before injecting.

Catwoman pose controls:
- “Freeze all Pawn skeletal meshes” stops the captured Pawn animation pose while the world keeps running.
- The “Current Component XYZ/PYR” lines are read-only references to the selected bone's current pose.
- “Delta XYZ” and “Delta Pitch / Yaw / Roll” are editable offsets from that captured pose. Set a small nonzero value, keep Freeze on, and enable “Apply offsets” to pose manually.
- Select Catwoman bone 007 Bip01_L_Clavicle with descendants on to test the left shoulder/arm. Diagnostics “ParentBoneMap branch edits” reports child-mesh writes.
- Unfreezing clears Apply offsets and disables writer/diagnostic hooks.

This edit path needs visual confirmation. PM, IGCS/free camera, and the built-in debug camera remain unverified.
