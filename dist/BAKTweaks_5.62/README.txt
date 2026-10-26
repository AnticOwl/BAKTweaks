BAK Tweaks v5.62 — Arkham Knight

Run BAKInjector.exe after BatmanAK.exe is running and choose BAK Tweaks. Restart the game before injecting; the injector refuses duplicate DLL loads.

Catwoman pose test:
- Keep gameplay running and enable “Freeze selected Pawn pose”.
- v5.62 checks the output buffer at Rocksteady helper +0x3288A0. It skips that helper only for a captured Pawn SpaceBases buffer with a matching bone count; other actors still use the original helper.
- LocalAtoms is restored after +0x2F7370. The tool reports helper calls, tracked buffers, and skipped combines.
- Verify that Catwoman stays fully frozen and note the displayed FPS. This path is experimental until this runtime test.
- Keep NamesDump.txt beside BatmanAK.exe for Catwoman's own runtime bone names. Do not apply Batman's Head index to Catwoman.

PM, IGCS/free camera, and the built-in debug camera remain unverified.
