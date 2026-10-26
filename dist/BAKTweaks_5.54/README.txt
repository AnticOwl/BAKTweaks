BAK Tweaks v5.54 — Arkham Knight

Standalone injector:
- Run BAKInjector.exe. It requests administrator elevation using its embedded UAC manifest; it does not use PowerShell.
- Start Arkham Knight first, then launch BAKInjector.exe and choose BAK Tweaks.
- Restart the game before injecting v5.54 if another BAKTweaks.dll version is currently loaded. The injector refuses duplicate module loads.

Catwoman bone-name check:
- Keep the existing Catwoman save; do not overwrite it.
- This build reads NamesDump.txt from the same Binaries/Win64 directory as BatmanAK.exe. Keep the supplied dump at that location.
- Open Pose and click “Refresh Characters / Skeleton”. The status should say that names were resolved from the game NamesDump; the count should be close to 292/292.
- The list uses Catwoman's live bone indices and parent hierarchy. FName 184176 should show Point_L_Finger3; FName 183304 should show Flappy_L_Arm_Biceps_Prnt; FName 1543 should show Bip01_L_Clavicle.
- Keep Freeze unchecked for this naming check. This build changes name resolution only; it does not change pose writes, freeze behavior, or save data.
- If the count is still 162, send the POSE names line from BAKTweaks.log and the skeleton status text.

If NamesDump.txt is absent or invalid, the tool falls back to the bundled Batman FName dictionary.
