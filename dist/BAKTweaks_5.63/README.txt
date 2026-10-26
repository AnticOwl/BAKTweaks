BAK Tweaks v5.63 — Arkham Knight

Run BAKInjector.exe after BatmanAK.exe is running and choose BAK Tweaks. Restart the game before injecting.

Catwoman pose test:
1. Refresh Characters / Skeleton and use Catwoman's live bone list.
2. Select bone 007, Bip01_L_Clavicle, to test the left shoulder/arm branch. Keep “Include descendants” enabled.
3. Keep “Freeze selected Pawn pose” enabled. Set Roll to 15 degrees, then enable “Apply offsets to selected bone / branch”.
4. The +0x3288A0 helper is skipped for captured Pawn buffers; the pose offset is applied once after EngineTick from the captured pose. Other actor buffers still use the original helper.
5. Report whether the arm moves, whether the rest stays frozen, and the displayed FPS. This edit path is awaiting visual confirmation.

Keep NamesDump.txt beside BatmanAK.exe for Catwoman's runtime bone names. Do not reuse Batman's Head index.
PM, IGCS/free camera, and the built-in debug camera remain unverified.
