BAK Tweaks v5.31 — Pose matrix fallback

v5.30 runtime diagnostics on Catwoman found that GetBoneLocation succeeds but GetBoneQuaternion fails. v5.31 adds a read-only fallback: SkeletalMeshComponent.GetBoneMatrix followed by Core.Object.MatrixGetRotator. The UI displays pitch/yaw/roll degrees if the quaternion getter fails.

The inspector found Catwoman's Pawn and 292 bones. This build does not write bone transforms or change save data. Keep-running-when-unfocused remains available; the user confirmed it works on the Steam build.

Build: MSVC x64 Release
Version resource: 5.31.0.0
