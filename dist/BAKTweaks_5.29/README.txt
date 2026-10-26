BAK Tweaks v5.29 — Pose focus option

Adds an opt-in control in the Pose tab to keep the game running while its window is unfocused. The setting is off by default.

The option resolves Engine.Engine.bPauseOnLossOfFocus through UE3 reflection and only activates for the verified +0x5B4 property layout. On enable, it captures the original setting and clears only the pause-on-focus-loss bit. Turning the option off or using Reset Global restores that original bit. Engine writes run through EngineTick.

This option does not change save data. BAK Tweaks does not support safe hot-unloading; restarting the game resets the setting if the DLL remains active.

Pose inspection remains read-only. It enumerates Pawn-based characters, their bones, parent relationships, and selected bone transforms. Pose editing and character mesh swapping remain in development pending in-game validation.

Build: MSVC x64 Release
Version resource: 5.29.0.0
