BAK Tweaks v5.42 — Arkham Knight

Standalone injector:
- Run BAKInjector.exe. It requests administrator elevation through its embedded UAC manifest; no PowerShell is used.
- The app detects a running Steam/Epic BatmanAK.exe and only offers the sibling BAKTweaks.dll and BAKPoseProbeLauncher.dll.
- It refuses duplicate module loads. Restart the game to replace an already loaded BAKTweaks.dll.

Pose freeze:
- In the Pose tab, refresh and select the active player Pawn. Its displayed number is only its current roster index.
- Enable “Freeze selected Pawn pose”. The game world continues running; this does not pause or stop time.
- v5.42 hooks all three Steam SpaceBases handler variants (modes 1, 2, and 5), with the corrected mode 5 signature. It reapplies the captured evaluated pose immediately after the matching handler returns.
- BAKTweaks.log records which mode handlers installed. The Pose tab shows per-handler counts, selected-mesh matches, valid arrays, frozen bones, and freeze writes.
- For this check, keep “Apply offset to selected bone” off. It tests pose freeze alone.
- Turn freeze off to restore animation. No save data is written.

This is an opt-in prototype for player Pawns on the verified Steam build. The viewport gizmo remains in development and must update pose state live during drag without an Apply step.
