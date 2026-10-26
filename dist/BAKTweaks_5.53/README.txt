BAK Tweaks v5.53 — Arkham Knight

Standalone injector:
- Run BAKInjector.exe. It requests administrator elevation using its embedded UAC manifest; it does not use PowerShell.
- Start Arkham Knight first, then launch BAKInjector.exe and choose BAK Tweaks.
- Restart the game before injecting v5.53 if another BAKTweaks.dll version is currently loaded. The injector refuses duplicate module loads.

Catwoman bone-name check:
- Load the existing Catwoman save without overwriting it, open Pose, and click “Refresh Characters / Skeleton”.
- Check that the list shows resolved bone names and body categories, with each live Catwoman index and parent index retained.
- Catwoman bone 7 should resolve as Bip01_L_Clavicle from FName 1543:0. Batman's Head[7] index must not be applied to Catwoman.
- Unknown FNames remain visible as raw IDs. Send a screenshot of the list around bones 0–15 and a later section if names remain unresolved.
- Keep “Freeze selected Pawn pose” unchecked for this name-resolution check. This build does not change freeze or pose writing.

The 297-entry Batman table is used only as an exact FName-name dictionary. The active character's own GetBoneNames order and GetParentBone hierarchy remain authoritative. No save data is written by this feature.
