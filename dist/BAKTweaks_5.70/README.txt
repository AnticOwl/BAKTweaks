BAK Tweaks v5.70

Pose trace update for Batman: Arkham Knight (Steam).

The +0x328CB0 reader trace now matches the currently inspected mesh's live SpaceBases array while Catwoman is animating, with Freeze off. It records the selected bone's input/output atom and caller stack once per selected mesh. Existing freeze-array matches remain supported. The hook is observational; it does not change pose output.

Keep the game running in gameplay, refresh Characters / Skeleton, select a bone, and inspect the Pose diagnostics at the bottom. The +0x328CB0 calls and Pawn-array matches should increase. Once a match is observed, the log records the runtime caller stack. Restart the game before injecting this version.
