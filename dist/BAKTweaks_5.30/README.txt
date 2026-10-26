BAK Tweaks v5.30 — Pose read diagnostics

Adds a status message and one BAKTweaks.log entry after each selected-bone transform read. The bone getter argument and return offsets follow BmSDK-AK's generated SkeletalMeshComponent wrapper.

Runtime status so far: Catwoman is detected as Player #15 with a valid SkeletalMesh and 292 bones. Selecting a bone did not yet display its position/quaternion in v5.29; v5.30 adds diagnostics to identify the failing stage. This build adds no pose writes and does not change save data.

The optional keep-running-when-unfocused setting remains available in Pose. The user confirmed it works on the Steam build.

Build: MSVC x64 Release
Version resource: 5.30.0.0
