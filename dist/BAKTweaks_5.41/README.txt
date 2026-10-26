BAK Tweaks v5.41 — Arkham Knight

Standalone injector:
- Run BAKInjector.exe. It requests administrator elevation through its embedded UAC manifest; no PowerShell is used.
- The app detects a running Steam/Epic BatmanAK.exe and only offers the sibling BAKTweaks.dll and BAKPoseProbeLauncher.dll.
- It refuses duplicate module loads. Restart the game to replace an already loaded BAKTweaks.dll.

Pose freeze:
- In the Pose tab, refresh and select the active player Pawn. Its displayed number is only its current roster index.
- Enable “Freeze selected Pawn pose”. The game world continues running; this does not pause or stop time.
- v5.41 hooks all three Steam SpaceBases handler variants (modes 1, 2, and 5) and reapplies the captured evaluated pose directly after the matching handler returns.
- Read “SpaceBases handlers 1 / 2 / 5” to see which handler Catwoman uses. While freeze is enabled, Finalizer calls, selected mesh, valid arrays, frozen bones, and writes should increase.
- For the first freeze check, keep “Apply offset to selected bone” off. It tests only pose freeze.
- Turn freeze off to restore animation. No save data is written.

This remains an opt-in prototype for player Pawns on the verified Steam build. Translation/rotation posing, descendant handling, non-Pawn characters, and the viewport gizmo remain in development. The gizmo must update live during drag without an Apply step.
