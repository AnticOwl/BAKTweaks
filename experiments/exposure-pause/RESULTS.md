# Exposure pause diagnosis — 2026-10-20

Read-only inspection of the running Steam process (PID 15384, v5.26) showed:

| Item | Value |
|---|---|
| Override wanted/applied | 1 / 1 |
| Requested value | 0.35 |
| LocalPlayer | 0x343D9170 |
| LevelPP exposure, +0x474 | 0.35 |
| CurrentPP exposure, +0x258 | 1.00 |
| Blend store, Steam RVA 0x9F3779 | eight NOP bytes |

The v5.26 linker map was reconstructed with the original link options and object files. Text, data, pdata, resources and relocations matched byte for byte; rdata differs due to the output export-library name. No process writes were made for this inspection. The exposure update implementation was unchanged from the preserved v5.24 source; the observation does not establish that the gizmo edit introduced this cache behavior.

SDK definitions confirm LocalPlayer.CurrentPPInfo at 244, LevelPPInfo at 784, both with LastSettings at zero, and PostProcessSettings.ExposureBaseOffset at 356. Thus exposure addresses are +0x258 and +0x474. The native blend-store instruction writes [RBX+0x164] and is not intrinsically restricted to LevelPP. Blocking that instruction unconditionally also blocks its other destinations.

v5.27 replaces the unconditional NOP with a targeted store hook. Both active-player cache addresses receive the requested exposure; all other destinations retain the engine's XMM1 value. Game-thread updates seed both caches immediately. Reset disables the hook and restores the saved cache values. Reflected cache offsets must match before activation.

TestWriter.exe tests the actual production assembly with real MinHook installation on the exact eight-byte MOVSS instruction. Passed: LevelPP, CurrentPP, unrelated writes, changed desired value, zero targets, original instruction restored; RAX/RDX/RFLAGS/XMM1 preserved. Release x64 DLL compilation passed. Visual validation of v5.27 while paused and Epic runtime testing remain pending. The running v5.26 was not hot-replaced.

The user separately confirmed that the v5.26 gizmo stays in place; that correction is preserved.

Sources: [LocalPlayer SDK](https://github.com/Team-BmSDK/BmSDK-AK/blob/main-bm4/src/BmSDK/Generated/Engine/LocalPlayer.g.cs), [PostProcessVolume SDK](https://github.com/Team-BmSDK/BmSDK-AK/blob/main-bm4/src/BmSDK/Generated/Engine/PostProcessVolume.g.cs).
