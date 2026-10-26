BAK Tweaks v5.28 — Pose Inspector foundation

Adds a read-only Pose tab for the current world's Pawn-based characters.
Character discovery follows WorldInfo.PawnList / Pawn.NextPawn and avoids a global GObjects scan. The runtime offsets for Pawn and SkeletalMesh properties are resolved and checked through UE3 reflection against BmSDK-AK generated data.

The tab lists Pawns with SkeletalMesh components, reads bone IDs and parent relationships, and reads a selected bone's component-space position and quaternion. Parent lookups are spread across EngineTick calls. This build does not write bone transforms and does not alter the selected character.

Known limits:
- Character entries currently use runtime pointers as labels; human-readable actor and bone names need the Arkham Knight name-table resolver.
- Only Pawn-based characters are enumerated. Other actor classes may require an additional path.
- Pose editing and reversible character mesh swapping remain in development pending runtime confirmation of writable pose handling and the game's swap path.
- In-game validation is pending. Preserve the existing Catwoman save; do not overwrite it during testing.

Build: MSVC x64 Release
Version resource: 5.28.0.0
