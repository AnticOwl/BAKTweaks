BAK Tweaks v5.48 — Arkham Knight

Standalone injector:
- Run BAKInjector.exe. It requests administrator elevation using its embedded UAC manifest; it does not use PowerShell.
- Start Arkham Knight first, then launch BAKInjector.exe and choose BAK Tweaks.
- If an older BAKTweaks.dll is already loaded, restart the game before injecting v5.48. The injector refuses duplicate module loads.

Pose diagnostics:
- Load the Catwoman DLC gameplay scene and open the Pose tab.
- Click “Refresh Characters / Skeleton”. Select the active player Pawn if needed.
- The rows list SkeletalMeshComponents found in the selected Pawn's reflected Components and AllComponents arrays, plus Pawn.Mesh if absent from those arrays.
- “bPauseAnims readback” reports the reflected bit immediately after the tool writes it and after the original game tick.
- Each mesh row reports its current SpaceBases bone count and how many sampled poses changed across ticks. Pose changes do not identify the writer or distinguish animation from physics/procedural updates.
- The dispatcher caller counters identify which known Steam callsites invoke the hooked animation dispatcher. Send a screenshot of this line after Refresh and after enabling Freeze for about two seconds.
- v5.47 Catwoman evidence: bPauseAnims stayed SET, yet the selected 292-bone pose changed on 541/542 samples; two additional attached SkeletalMeshComponents with 204 and 297 bones changed as well. Therefore a successful Catwoman freeze is not claimed.
- State whether Catwoman visibly moves. No save data is written.

The viewport gizmo and posing while the whole game is paused remain untested.
