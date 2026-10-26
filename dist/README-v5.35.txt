BAK Tweaks v5.35 — Arkham Knight

Pose Inspector updates:
- Labels getter spaces as World (0) and Component (1), based on observed Catwoman output and the UE two-space convention. Space 2 remains a diagnostic alias check.
- Resolves SpaceBases and LocalAtoms offsets through live UProperty metadata.
- Displays the selected element's matrix origin from SpaceBases and the translation/quaternion from LocalAtoms for comparison with Component-space getter output.
- Read-only only: does not write pose data or alter saves.
- Keeps automatic pause-on-focus-loss handling.

To test: load v5.35, refresh the character/skeleton, and select a bone. Compare Component (1) with the SpaceBases and LocalAtoms values. Send a screenshot of the selected bone, the three getter rows, and both array diagnostics. Do not select or overwrite a save slot for this test.
