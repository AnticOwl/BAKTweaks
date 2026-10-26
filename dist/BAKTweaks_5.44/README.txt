BAK Tweaks v5.44 — Arkham Knight

Standalone injector:
- Run BAKInjector.exe. It requests administrator elevation through its embedded UAC manifest; no PowerShell is used.
- The app detects a running Steam/Epic BatmanAK.exe and only offers the sibling BAKTweaks.dll and BAKPoseProbeLauncher.dll.
- It refuses duplicate module loads. Restart the game to replace an already loaded BAKTweaks.dll.

Pose freeze:
- In the Pose tab, refresh and select the active player Pawn. Its displayed number is only its current roster index.
- Enable “Freeze selected Pawn pose”. The game world continues running; this does not pause or stop time.
- v5.44 adds a hook after the common animation dispatcher returns. It covers modes 0, 1, 2, 3, 4, and 5, including Catwoman's branch that the prior hooks did not observe.
- Keep “Apply offset to selected bone” off during the first check. The Pose tab shows dispatcher calls separately; the log records hook installation and writes.
- Turn freeze off to restore animation. No save data is written.

This is an opt-in prototype for player Pawns on the verified Steam build. The viewport gizmo remains in development and must update pose state live during drag without an Apply step.
