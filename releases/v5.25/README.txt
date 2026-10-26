BAK Tweaks v5.25 — Live Lights (Steam/Epic profiles)

Changes
- Version in main title bar and Windows DLL version resource.
- Spot/Point editor sends changed properties automatically on FEngineLoop::Tick.
- Edits are coalesced per light/tick; no Apply buttons and no full advanced-state rewrite on slider drag.
- XYZ and gizmo origin use actor world coordinates. Pitch/Yaw/Roll retain their component-based semantics.
- Reset and Move to Camera remain explicit actions.
- Separate profiles for the supplied Steam and Epic executables. Unknown builds or unexpected code/layouts are rejected.

Validation
- Release x64 build passed.
- Both supplied binaries compared: five functions share their normalized instruction sequences; Epic global object/count addresses cross-checked.
- Steam loaded with IGCS camera already present: resolver and reflected light layout checks passed; title v5.25 visible.
- Steam with IGCS pause: native Point/Spot SetLocation tests updated actor and light matrices without an additional ForceUpdate. User subsequently confirmed live light and gizmo movement; the session log identifies Spot1 opened in Edit.
- The passive editor trace expired before the manual edits; it does not establish which individual controls were exercised. Intensity, every axis and Epic runtime validation are not established by this confirmation.

Limitations
- Pausing the UE3 world can leave FEngineLoop::Tick running. Suspending the whole process cannot support live updates.
- Steam-session fixed particle-object indices are not trusted on Epic. Lightning module resolution is unavailable there until validated; this does not disable the Spot/Point editor.

Use
- Test with one BAKTweaks.dll only. Restart the game to replace an already loaded version.
- Original supplied DLL and source preserved in releases/v5.24-before-live.
- New DLL in releases/v5.25. Source changes in src/BAKTweaks.cpp and src/BAKTweaks.rc.
