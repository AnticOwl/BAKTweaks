BAK Tweaks v5.56 — Arkham Knight

Standalone injector:
- Run BAKInjector.exe. It requests administrator elevation using its embedded UAC manifest; it does not use PowerShell.
- Start Arkham Knight first, then launch BAKInjector.exe and choose BAK Tweaks.
- Restart the game before injecting v5.56 if another BAKTweaks.dll version is currently loaded. The injector refuses duplicate module loads.

First pose edit check — Catwoman only:
- Keep the existing Catwoman save; do not overwrite it. NamesDump.txt must remain beside BatmanAK.exe.
- Open Pose and click “Refresh Characters / Skeleton”. Confirm Catwoman has 292 bones and 292 resolved names.
- Select bone 007, “Bip01_L_Clavicle”. Keep “Freeze selected Pawn pose” unchecked.
- Check “Apply offsets to selected bone”. Set Rotation offset Pitch / Yaw / Roll to 0 / 0 / 10 degrees. Observe the left shoulder/arm for a live change.
- Use “Reset selected-bone offsets” to return the bone to the live animated pose, then uncheck “Apply offsets to selected bone”.
- This prototype writes the selected bone's component-space quaternion after the animation dispatcher. Rotation is recalculated from each live evaluated pose; the entered angle should not accumulate. No save data is written.
- Send a screenshot with the selected-bone transform and write counter if the visible pose does not match the change.

This is a first numeric rotation control, not the finished gizmo. Catwoman's visual result must be confirmed before expanding pose editing to other characters. Freeze behavior is unchanged and should remain off for this test.
