BAK Tweaks v5.72

Pose trace update for Batman: Arkham Knight (Steam).

The read-only +0x328CB0 reader hook remains active with Freeze unchecked and matches the currently inspected mesh's live SpaceBases array. It now records the exact immediate caller return address, reports input/output bone counts, and retains the call-site RVA in the UI even if stack unwinding returns no frames. Freeze writers and animation controls remain inactive unless Freeze is enabled.

Restart the game before injecting this version. Load Catwoman, refresh Characters / Skeleton, keep Freeze unchecked, and inspect the SpaceBases reader diagnostics. The exact caller RVA and output count will distinguish the known finalizer call from other readers.
