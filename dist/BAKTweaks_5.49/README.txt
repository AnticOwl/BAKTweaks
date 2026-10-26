BAK Tweaks v5.49 — Arkham Knight

Standalone injector:
- Run BAKInjector.exe. It requests administrator elevation using its embedded UAC manifest; it does not use PowerShell.
- Start Arkham Knight first, then launch BAKInjector.exe and choose BAK Tweaks.
- If an older BAKTweaks.dll is already loaded, restart the game before injecting v5.49. The injector refuses duplicate module loads.

Pose diagnostics:
- Load the Catwoman DLC gameplay scene and open the Pose tab.
- Click “Refresh Characters / Skeleton”. Select the active player Pawn if needed.
- The rows list SkeletalMeshComponents found in the selected Pawn's reflected Components and AllComponents arrays, plus Pawn.Mesh if absent from those arrays.
- “bPauseAnims readback” reports the reflected bit immediately after the tool writes it and after the original game tick.
- Each mesh row reports its current SpaceBases bone count and how many sampled poses changed across ticks.
- v5.48 evidence: the selected 292-bone mesh had bPauseAnims SET while its pose changed on 1140/1141 samples; tracked 204- and 297-bone meshes had the bit CLEAR and also changed on 1140/1141 samples. This is consistent with a partial freeze, but the rendered-body mapping is not yet verified.
- v5.48's four known dispatcher caller counters were zero while other callers reached 4340. v5.49 records up to eight exact runtime return addresses and RVAs, displays their counts, and logs each first-seen address as `POSE CALLER` in BAKTweaks.log.
- Please enable Freeze for about two seconds and send a screenshot showing the live caller rows and mesh diagnostics, plus the `POSE CALLER` lines from BAKTweaks.log. Also report which visible body parts remain in motion.
- No save data is written. A successful full-character freeze is not claimed.

The viewport gizmo and posing while the whole game is paused remain untested.
