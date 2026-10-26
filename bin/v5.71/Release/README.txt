BAK Tweaks v5.71

Pose trace update for Batman: Arkham Knight (Steam).

The read-only +0x328CB0 SpaceBases reader hook installs when a mesh is selected and stays enabled with Freeze unchecked. It matches the currently inspected mesh's live SpaceBases array and records the selected bone input/output plus caller stack on its first match. Freeze writers and animation controls remain inactive unless Freeze is enabled.

Restart the game before injecting this version. Load Catwoman, refresh Characters / Skeleton, keep Freeze unchecked, and inspect the SpaceBases reader calls / Pawn-array matches diagnostics. The log records the caller stack when the match occurs.
