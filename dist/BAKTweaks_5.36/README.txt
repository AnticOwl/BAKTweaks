BAK Tweaks v5.36 — Arkham Knight

Pose Inspector correction:
- Reads both SpaceBases and LocalAtoms as 32-byte FBoneAtom arrays.
- Displays the selected atom's translation and quaternion for each buffer. SpaceBases can be compared directly with Component (1).
- Read-only. Does not write pose data or modify saves.
- Keeps automatic pause-on-focus-loss handling.

Build note: the prior v5.35 SpaceBases diagnostic used an incorrect 64-byte matrix stride. v5.36 corrects the stride based on the live TArray count and a direct read-only comparison with Component (1).

To test: restart into v5.36, load the Catwoman save without overwriting it, refresh the skeleton, select bone 133, and compare Component (1) with SpaceBases. No pose editing is enabled yet.
