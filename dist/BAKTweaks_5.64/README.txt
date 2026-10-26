BAK Tweaks v5.64 — Arkham Knight

Run BAKInjector.exe after BatmanAK.exe is running and choose BAK Tweaks. Restart the game before injecting.

Catwoman bone movement test:
1. Refresh Characters / Skeleton and keep Catwoman selected.
2. Select bone 007, Bip01_L_Clavicle, and leave “Include descendants” checked.
3. Keep “Freeze selected Pawn pose” on. Set Roll to 15 degrees, then enable “Apply offsets to selected bone / branch”.
4. The +0x3288A0 helper is skipped only for captured Pawn buffers. The offset is applied after EngineTick to the root and mapped child meshes using their live ParentBoneMap arrays.
5. Check whether the left arm moves, the rest remains frozen, and FPS stays acceptable. In Diagnostics, “ParentBoneMap branch edits” should show child meshes/bones edited.

Keep NamesDump.txt beside BatmanAK.exe for Catwoman's runtime bone names. Do not reuse Batman's Head index.
PM, IGCS/free camera, and the built-in debug camera remain unverified.
