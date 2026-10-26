BAK Tweaks v5.57 — Arkham Knight

Standalone injector:
- Run BAKInjector.exe. It requests administrator elevation using its embedded UAC manifest; it does not use PowerShell.
- Start Arkham Knight first, then launch BAKInjector.exe and choose BAK Tweaks.
- Restart the game before injecting v5.57 if another BAKTweaks.dll version is currently loaded. The injector refuses duplicate module loads.

Catwoman branch pose edit:
- Keep the existing Catwoman save; do not overwrite it. Keep NamesDump.txt beside BatmanAK.exe.
- Run gameplay normally; do not pause the whole game. Open Pose and click Refresh Characters / Skeleton. Wait for “Skeleton ready” and the parent hierarchy to finish reading.
- Select Catwoman bone 007, Bip01_L_Clavicle. The branch count should be greater than 1.
- Check Freeze selected Pawn pose. This pauses the selected Pawn's animation while the game update loop continues.
- Check Include descendants (move whole branch), then Apply offsets to selected bone / branch.
- Reset translation offsets to 0 / 0 / 0 and rotation to 0 / 0 / 15 degrees. Observe the shoulder and arm branch.
- Use Reset selected-bone offsets, then uncheck Apply offsets and Freeze.

The edit is applied to the evaluated component-space pose after the animation dispatcher. Branch rotation moves descendant positions around the selected bone's pivot and rotates their orientations. A full game pause halts the update path, so live posing cannot update while the game is globally paused. Visual confirmation on Catwoman is still required; PM/IGCS/debug-camera compatibility remains unverified.
