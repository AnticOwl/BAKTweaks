BAK Tweaks v5.46 — Arkham Knight

Standalone injector:
- Run BAKInjector.exe. It requests administrator elevation through its embedded UAC manifest; no PowerShell is used.
- The app detects a running Steam/Epic BatmanAK.exe and only offers the sibling BAKTweaks.dll and BAKPoseProbeLauncher.dll.
- It refuses duplicate module loads. Restart the game to replace an already loaded BAKTweaks.dll.

Pose freeze:
- Refresh and select the active player Pawn; its number is only its current roster index.
- Enable “Freeze selected Pawn pose”. This sets the selected SkeletalMeshComponent's SDK-reflected bPauseAnims flag and restores its original value when disabled or when the level/target changes.
- Only the selected character's animation is paused. The game world and other characters keep running; the game itself is not paused.
- Keep “Apply offset to selected bone” off during the first check. The Pose tab reports whether the mesh animation flag is active.
- No save data is written. Support for posing while the whole game is paused is a later step.

This is an opt-in prototype for player Pawns on the verified Steam build. The viewport gizmo remains in development and must update pose state live during drag without an Apply step.
