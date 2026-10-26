BAK Tweaks v5.26 — World-anchored gizmo

- Corrected camera roll sign in the UE3 world-to-screen basis.
- Gizmo origin reads the light actor's actual world position, not the editor's pending position. Camera movement never writes light coordinates.
- Existing world-space sphere/cone construction is preserved.
- Steam/Epic build profiles and live editing from v5.25 are preserved.

Validation: Release x64 compilation passed; DLL resource version 5.26.0.0 verified.
In the running Steam session, a read-only comparison confirmed that the PlayerCamera used by the gizmo is the same object intercepted by IGCS (0x612A6260). No alternate camera pointer is required for that session.
Visual camera-motion validation of this new DLL remains pending. The roll correction is a concrete projection defect; it does not prove that every previously reported drift has the same cause. Epic runtime validation remains pending.

The currently loaded v5.25 has not been replaced or unloaded. Restart the game before injecting this DLL; do not load both versions together. The original build/Release DLL and the v5.25 release are preserved.
