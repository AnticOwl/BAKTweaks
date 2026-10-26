BAK Tweaks v5.45 — Arkham Knight

Standalone injector:
- Run BAKInjector.exe. It requests administrator elevation through its embedded UAC manifest; no PowerShell is used.
- The app detects a running Steam/Epic BatmanAK.exe and only offers the sibling BAKTweaks.dll and BAKPoseProbeLauncher.dll.
- It refuses duplicate module loads. Restart the game to replace an already loaded BAKTweaks.dll.

Pose freeze:
- Refresh and select the active player Pawn; its number is only its current roster index.
- Enable “Freeze selected Pawn pose”. The game world continues running; this does not pause or stop time.
- v5.45 hooks after the common animation dispatcher and keys the saved pose to the selected mesh component. It reuses that snapshot even if SpaceBases changes buffer address.
- Keep “Apply offset to selected bone” off for the first check. The Pose tab shows dispatcher calls, matched arrays, frozen bones, and writes.
- Turn freeze off to restore animation. No save data is written.

This is an opt-in prototype for player Pawns on the verified Steam build. The viewport gizmo remains in development and must update pose state live during drag without an Apply step.
