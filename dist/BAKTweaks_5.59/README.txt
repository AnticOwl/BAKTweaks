BAK Tweaks v5.59 — Arkham Knight

Standalone injector:
- Run BAKInjector.exe from this folder after BatmanAK.exe is running. Choose BAK Tweaks.
- Restart the game before injecting v5.59 if another BAKTweaks.dll version is loaded. The injector refuses duplicate module loads.
- Keep NamesDump.txt beside BatmanAK.exe for live bone names.

Catwoman pose prototype:
- Keep gameplay running; full engine pause prevents the game update path from running.
- Open Pose, refresh Characters / Skeleton, and wait for Skeleton ready.
- Enable “Freeze all Pawn skeletal meshes”. v5.59 restores captured SpaceBases from inside Rocksteady's bone-combine helper (+0x3288A0), only for buffers matched to a skeletal component tracked on the selected Pawn. The existing post-EngineTick restore remains as a secondary pass.
- Read the helper calls / target matches / snapshot writes counter. If the body still moves, report those values and which visible areas move.
- To test editing after freeze is confirmed: select Catwoman's Bip01_L_Clavicle, keep Include descendants enabled, set Roll to 10 degrees, then enable Apply offsets. Reset offsets and disable Apply afterward.

This build has compiled but has not been visually validated. PM, IGCS/free camera, and the built-in debug camera remain unverified.
