BAK Tweaks v5.65 — Arkham Knight

Run BAKInjector.exe after BatmanAK.exe is running and choose BAK Tweaks. Restart before injecting.

Frame-rate restoration:
- When you release Freeze, the tool also turns off Apply offsets, the SpaceBases helper, LocalAtoms helper, dispatcher and control-evaluator hooks.
- v5.64 could leave the dispatcher/evaluator diagnostic hooks running after unfreeze. They hash animation arrays on every call and are a likely cause of the remaining low FPS.
- Freeze/edit hooks are only active while Freeze is checked. Check the displayed FPS after unfreezing.

Catwoman bone test:
- Freeze Catwoman, select bone 007 Bip01_L_Clavicle, keep descendants on, set Roll to 15 degrees and enable Apply offsets.
- v5.64 branch propagation through child ParentBoneMap is included. Report whether the arm moves and the “ParentBoneMap branch edits” diagnostic.

PM, IGCS/free camera, and the built-in debug camera remain unverified.
