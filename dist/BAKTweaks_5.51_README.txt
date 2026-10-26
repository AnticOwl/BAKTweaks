BAK Tweaks v5.51 — Arkham Knight

Standalone injector:
- Run BAKInjector.exe. It requests administrator elevation using its embedded UAC manifest; it does not use PowerShell.
- Start Arkham Knight first, then launch BAKInjector.exe and choose BAK Tweaks.
- Restart the game before injecting v5.51 if v5.50 is currently loaded. The injector refuses duplicate module loads.

Catwoman evaluator trace:
- Load the existing Catwoman DLC gameplay save without overwriting it, open the Pose tab, and click “Refresh Characters / Skeleton”.
- With “Freeze selected Pawn pose” unchecked, wait about three seconds and capture the component rows plus the lower dispatcher/evaluator counters. This is the read-only baseline.
- Then check Freeze, wait about three seconds, and capture the same counters again. Finally uncheck Freeze to restore animation.
- v5.51 observes the confirmed mode 0/1 evaluator at Steam RVA +0x304340. Its counters show all calls, selected-mesh calls, and whether the selected SpaceBases hash changes inside each call. The dispatcher mode and post-tick buffer counters remain visible for comparison.
- Please include the `POSE TRACE` lines from BAKTweaks.log if the hook reports a signature mismatch or does not install.

With Freeze and bone editing off, the new hook only reads and hashes the selected SpaceBases before and after the original evaluator call. It does not write pose buffers or save data. Freeze remains experimental; a full-character pose hold is not claimed.
