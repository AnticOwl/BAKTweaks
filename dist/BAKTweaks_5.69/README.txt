BAK Tweaks v5.69 — Arkham Knight

Run BAKInjector.exe after BatmanAK.exe is running and choose BAK Tweaks. Restart the game before injecting a new version.

Pose trace:
- Freeze Catwoman while gameplay runs.
- Select bone 007.
- Diagnostics report matching reads at Rocksteady helper +0x328CB0. The log records the source SpaceBases pointer, output LocalAtoms pointer, selected-bone input/output values, and runtime caller stack on the first matching call.
- This is trace-only; it does not change pose writes. Unfreeze after a short capture to restore normal performance.

The +0x328CB0 output is not yet confirmed as the final render palette. PM, IGCS/free camera, and the built-in debug camera remain unverified.
