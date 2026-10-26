BAK Tweaks v5.67 — Arkham Knight

Run BAKInjector.exe after BatmanAK.exe is running and choose BAK Tweaks. Restart the game before injecting a new version.

Catwoman pose controls:
- Enable “Freeze all Pawn skeletal meshes” while gameplay continues.
- Select a bone. Position XYZ and Rotation Pitch / Yaw / Roll show its Component-space pose.
- The SpaceBases read-target line shows the selected atom address for a Cheat Engine data breakpoint.
- Edit those absolute coordinates while Freeze is on. The tool applies the pose live from the frozen snapshot; “Apply pose target” is enabled automatically when a coordinate changes.
- Enable “Include descendants” to pose the whole branch. Catwoman bone 007 is Bip01_L_Clavicle, the left shoulder/arm root.
- “Reset target to read pose” restores the displayed starting coordinates.
- Unfreezing clears Apply and restores the animation path.

The absolute field mapping and visible branch result need live validation. PM, IGCS/free camera, and the built-in debug camera remain unverified.
