BAK Tweaks v5.58 — Arkham Knight

Standalone injector:
- Run BAKInjector.exe from this folder after BatmanAK.exe is running. Choose BAK Tweaks.
- If another BAKTweaks.dll is already loaded, restart the game before injecting this version.
- Keep NamesDump.txt beside BatmanAK.exe for complete live bone names.

Catwoman pose prototype:
- Run gameplay normally; keep the game update loop running. A full game pause stops the post-tick pose update.
- Open Pose, click Refresh Characters / Skeleton, and wait for “Skeleton ready”.
- Enable “Freeze all Pawn skeletal meshes” to capture and restore the tracked skeletal component poses after each EngineTick.
- Clear Freeze to release the snapshots. Use “Apply offsets to selected bone / branch” for a visible bone edit; reset offsets and disable Apply afterward.

v5.58 changes the write boundary and captures all tracked skeletal mesh components. This prototype is not yet visually validated. Please check whether the complete Catwoman body stops moving, then whether a small clavicle/arm branch edit is visible in real time. PM, IGCS/free camera, and the built-in debug camera remain unverified.
