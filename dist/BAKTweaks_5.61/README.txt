BAK Tweaks v5.61 — Arkham Knight

Standalone injector:
- Run BAKInjector.exe from this folder after BatmanAK.exe is running. Choose BAK Tweaks.
- Restart the game before injecting v5.61 if another BAKTweaks.dll version is loaded. The injector refuses duplicate module loads.
- Keep NamesDump.txt beside BatmanAK.exe for live bone names.

Catwoman pose prototype:
- Keep gameplay running; full engine pause prevents the game update path from running.
- Open Pose, refresh Characters / Skeleton, and wait for Skeleton ready.
- Enable “Freeze selected Pawn pose” to hold the complete visible Catwoman pose.
- v5.61 leaves the extremely hot Rocksteady helper +0x3288A0 untouched. It restores LocalAtoms after +0x2F7370 and SpaceBases after EngineTick, avoiding hundreds of thousands of full-array copies per minute.
- After restart and injection, check that the complete character remains frozen and report the displayed FPS. Offset editing still needs a separate visual validation.
- PM, IGCS/free camera, and the built-in debug camera remain unverified.
