BAK Tweaks v5.50 — Arkham Knight

Standalone injector:
- Run BAKInjector.exe. It requests administrator elevation using its embedded UAC manifest; it does not use PowerShell.
- Start Arkham Knight first, then launch BAKInjector.exe and choose BAK Tweaks.
- Restart the game before injecting v5.50 if v5.49 is currently loaded. The injector refuses duplicate module loads.

Read-only pose trace:
- Load the Catwoman DLC gameplay scene and open the Pose tab.
- Click “Refresh Characters / Skeleton” and select the active player Pawn if needed.
- Leave “Freeze selected Pawn pose” unchecked for this capture. v5.50 keeps the dispatcher observation hook in read-only mode while Freeze is off.
- Wait about two seconds, then send a screenshot of the component rows and dispatcher mode counters. Include the `POSE: buffers` and `POSE: links` offset lines from BAKTweaks.log if available; the live change counts are shown in the UI.
- The component diagnostics report SpaceBases, LocalAtoms, CachedSpaceBases, and CachedLocalAtoms bone counts and per-tick hash changes, plus reflected parent/attachment and animation/control references. Dispatcher mode counts are split between all observed components and the selected mesh.

This capture does not write pose buffers or save data when Freeze and bone editing are off. It is diagnostic only; a complete character freeze is not claimed. Keep the Catwoman save unchanged.
