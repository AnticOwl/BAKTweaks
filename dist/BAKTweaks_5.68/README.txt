BAK Tweaks v5.68 — Arkham Knight

Run BAKInjector.exe after BatmanAK.exe is running and choose BAK Tweaks. Restart the game before injecting a new version.

Pose trace:
- Freeze Catwoman while gameplay runs.
- Select bone 007 to keep the same SpaceBases read target.
- The diagnostics show calls and Pawn-array matches for Rocksteady helper +0x328CB0. The log records the source SpaceBases pointer, output LocalAtoms pointer, selected-bone input/output, and runtime call stack on the first match.
- This version adds tracing only; it does not change the pose writer. Unfreeze after a short capture to restore normal performance.

The +0x328CB0 output is not yet confirmed as the final render palette. PM, IGCS/free camera, and the built-in debug camera remain unverified.
