BAK Tweaks v5.34 — Arkham Knight

Pose Inspector change:
- For the selected bone, reads GetBoneLocation and GetBoneQuaternion with numeric Space values 0, 1, and 2.
- Displays numeric labels only; this game build's enum semantics remain under investigation.
- Read-only diagnostic. It does not write pose data or modify saves.
- Keeps automatic pause-on-focus-loss handling from v5.33.

To test: load BAKTweaks.dll, enter gameplay, click Refresh Characters / Skeleton, then select a bone. The Pose tab should show one line per valid Space value. Report which rows appear and their positions/quaternions. No save slot needs to be selected or overwritten.
