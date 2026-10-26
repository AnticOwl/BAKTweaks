BAK Tweaks v5.60 — Arkham Knight

Standalone injector:
- Run BAKInjector.exe from this folder after BatmanAK.exe is running. Choose BAK Tweaks.
- Restart the game before injecting v5.60 if another BAKTweaks.dll version is loaded. The injector refuses duplicate module loads.
- Keep NamesDump.txt beside BatmanAK.exe for live bone names.

Catwoman pose prototype:
- Keep gameplay running; full engine pause prevents the game update path from running.
- Open Pose, refresh Characters / Skeleton, and wait for Skeleton ready.
- Enable “Freeze all Pawn skeletal meshes”. v5.60 captures LocalAtoms and SpaceBases for the tracked Pawn components. It restores LocalAtoms after Rocksteady's control-update helper (+0x2F7370), SpaceBases after its bone-combine helper (+0x3288A0), and checks both again after EngineTick.
- Read the counters for each helper. If any visible parts still move, report the LocalAtoms and SpaceBases calls/matches/writes and the component pose-change rows.
- Do not test offsets until Freeze is visually confirmed. PM, IGCS/free camera, and the built-in debug camera remain unverified.

This prototype is not yet visually validated.
