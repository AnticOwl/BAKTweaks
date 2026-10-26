BAK Tweaks v5.43 — Arkham Knight

Standalone injector:
- Run BAKInjector.exe. It requests administrator elevation through its embedded UAC manifest; no PowerShell is used.
- The app detects a running Steam/Epic BatmanAK.exe and only offers the sibling BAKTweaks.dll and BAKPoseProbeLauncher.dll.
- It refuses duplicate module loads. Restart the game to replace an already loaded BAKTweaks.dll.

Pose freeze:
- In the Pose tab, refresh and select the active player Pawn. Its displayed number is only its current roster index.
- Enable “Freeze selected Pawn pose”. The game world continues running; this does not pause or stop time.
- v5.43 matches a SpaceBases array shared by handler and selected mesh pointers, even when the component pointers differ. It logs handler/target pointers and array counts when they do not match.
- Keep “Apply offset to selected bone” off. The next run reports exact writer counters and matching diagnostics in BAKTweaks.log.
- Turn freeze off to restore animation. No save data is written.

This is an opt-in prototype for player Pawns on the verified Steam build. The viewport gizmo remains in development and must update pose state live during drag without an Apply step.
