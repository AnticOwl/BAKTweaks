BAK Tweaks v5.55 — Arkham Knight

Standalone injector:
- Run BAKInjector.exe. It requests administrator elevation using its embedded UAC manifest; it does not use PowerShell.
- Start Arkham Knight first, then launch BAKInjector.exe and choose BAK Tweaks.
- Restart the game before injecting v5.55 if another BAKTweaks.dll version is currently loaded. The injector refuses duplicate module loads.

Catwoman bone-name check:
- Keep the existing Catwoman save; do not overwrite it.
- This build reads NamesDump.txt from the same Binaries/Win64 directory as BatmanAK.exe.
- Open Pose and click “Refresh Characters / Skeleton”. The status should report 292 bones and 292 names resolved from game NamesDump.
- Bone 28 should be categorized “Fingers L” and named Point_L_Finger3. The bottom of the list should show the Whip_Bone chain, including bone 265 as Whip_Bone_10 and bone 285 as Whip_Bone_30.
- Keep Freeze unchecked for this naming check. Pose writes, freeze behavior, and save data are unchanged.
- If any FNames remain unresolved, send the status line and one screenshot of the unresolved rows. The POSE READ lines are in BAKTweaks.log beside the injected DLL.

If NamesDump.txt is absent or invalid, the tool falls back to the bundled Batman FName dictionary.
