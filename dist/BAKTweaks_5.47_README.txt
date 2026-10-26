BAK Tweaks v5.47 — Arkham Knight

Standalone injector:
- Run BAKInjector.exe. It requests administrator elevation using its embedded UAC manifest; it does not use PowerShell.
- Start Arkham Knight first, then launch BAKInjector.exe and choose BAK Tweaks.
- If an older BAKTweaks.dll is already loaded, restart the game before injecting v5.47. The injector refuses duplicate module loads.

Pose diagnostics:
- Load the Catwoman DLC gameplay scene and open the Pose tab.
- Click “Refresh Characters / Skeleton”. Select the active player Pawn if needed.
- The diagnostic rows list SkeletalMeshComponents found in the selected Pawn's reflected Components and AllComponents arrays, plus Pawn.Mesh if absent from those arrays.
- “bPauseAnims readback” reports the actual reflected bit immediately after the tool writes it and after the original game tick.
- Each mesh row reports its current SpaceBases bone count and how many sampled poses changed across game ticks. These changes do not identify the writer or distinguish animation from physics/procedural updates.
- Enable “Freeze selected Pawn pose” only to collect the pause-bit and pose-buffer readings. Catwoman freeze is not yet confirmed; her continued movement under v5.46 is recorded as a failed test.
- Send a screenshot of the full Pose diagnostics after Refresh and after enabling Freeze for about two seconds, and state whether Catwoman visibly moves.

No save data is written. The viewport gizmo and posing while the whole game is paused remain untested.
