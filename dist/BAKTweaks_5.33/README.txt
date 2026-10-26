BAK Tweaks v5.33 — Arkham Knight

- Automatically disables pause-on-focus-loss when BAK Tweaks starts. Uncheck “Keep game running when unfocused” in Pose to restore the original setting. Reset Global returns to the automatic enabled default.
- Keeps the v5.32 aligned bone transform buffers and exception-code diagnostics.
- Includes a one-shot, reloadable pose probe launcher. It asks BAK Tweaks to read the selected bone on EngineTick, then unloads itself. This probe is read-only and does not alter pose values or save data.

Live pose probe
1. Load BAK Tweaks v5.33 and enter gameplay. Updating the main DLL from an older version requires restarting the game once.
2. In Pose, refresh Characters / Skeleton and select a bone.
3. Inject BAKPoseProbeLauncher.dll using the project helper:
   .\tools\Load-TestDll.ps1 -Edition Steam -DllPath .\experiments\pose-probe\build\BAKPoseProbeLauncher.dll
   Change Steam to Epic for that installation.
4. BAKPoseProbeLauncher.log beside the launcher records whether the request was queued. BAKTweaks.log beside BAKTweaks.dll records the game-thread read and exception codes.

Previous version packages and the first Catwoman save are not modified by this diagnostic.
