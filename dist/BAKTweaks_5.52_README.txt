BAK Tweaks v5.52 — Arkham Knight

Standalone injector:
- Run BAKInjector.exe. It requests administrator elevation using its embedded UAC manifest; it does not use PowerShell.
- Start Arkham Knight first, then launch BAKInjector.exe and choose BAK Tweaks.
- Restart the game before injecting v5.52 if v5.51 is currently loaded. The injector refuses duplicate module loads.

Catwoman trace:
- Load the existing Catwoman DLC gameplay save without overwriting it, open the Pose tab, and click “Refresh Characters / Skeleton”.
- With Freeze unchecked, wait three seconds and capture the three component rows plus dispatcher and evaluator counters.
- Then check Freeze, wait three seconds and capture those counters again. Finally uncheck Freeze to restore animation.
- The evaluator line separates its argument from the dispatcher's mode. The selected-mesh hash counters show changes inside the engine dispatcher, whether the pose writer altered the buffer, and changes between consecutive post-writer calls.
- Include `POSE TRACE` lines from BAKTweaks.log if the evaluator hook reports a signature mismatch or does not install.

This build adds read-only counters; with Freeze and bone editing off, it does not write pose buffers or save data. The freeze remains experimental and a full-character pose hold is not claimed.

For the Batman comparison, repeat the same read-only capture with Batman selected in gameplay. The exact Cheat Engine script from the earlier successful Batman pose freeze has not yet been recovered.
