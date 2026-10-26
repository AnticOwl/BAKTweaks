BAK Tweaks v5.32 — Arkham Knight

- v5.32: 16-byte aligned transform buffers and SEH exception codes for the bone quaternion/matrix getters.
- Includes a reloadable, one-shot pose probe launcher. It queues a read of the currently selected bone on BAK Tweaks' EngineTick thread, then unloads itself.
- This probe is read-only. It does not write transforms or save data.

Live probe workflow
1. Load BAK Tweaks v5.32 and enter gameplay. The one-time main DLL update from v5.31 requires a game restart.
2. In Pose, refresh Characters / Skeleton and select a bone.
3. Inject experiments/pose-probe/build/BAKPoseProbeLauncher.dll with:
   .\tools\Load-TestDll.ps1 -Edition Steam -DllPath .\experiments\pose-probe\build\BAKPoseProbeLauncher.dll
   Use -Edition Epic for the Epic install.
4. The launcher writes BAKPoseProbeLauncher.log beside itself and unloads automatically. Check BAKTweaks.log beside the loaded BAKTweaks.dll for POSE PROBE and POSE READ lines.

Previous version packages are retained. No save data is changed by this diagnostic.
