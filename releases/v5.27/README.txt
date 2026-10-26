BAK Tweaks v5.27 — Paused exposure

Preserves the v5.26 world-gizmo fix, confirmed in game by the user.

Exposure now updates the active LocalPlayer's LevelPP and CurrentPP caches. The old unconditional eight-byte NOP is replaced by a targeted blend-store hook: other destinations retain the original engine write. Disabling override restores the original instruction and the two pre-override cached values.

Checks: Release x64 build succeeded; DLL resource version 5.27.0.0 verified. An isolated native test using the exact eight-byte store and real MinHook installation/restoration passed for both target caches, unrelated destinations, value changes, disabled targets and RAX/RDX/RFLAGS/XMM1 preservation.

Steam v5.26 diagnosis: wanted/applied=1, requested exposure=0.35, LevelPP=0.35, CurrentPP=1.00. This is memory evidence of inconsistent caches; visual validation of v5.27 in IGCS pause is still pending. Epic runtime remains pending. Reflected LocalPlayer cache offsets are checked before enabling the override.

Restart the game before injecting this DLL. Do not load alongside v5.26 or another BAKTweaks version. Existing releases are preserved.

SHA256: C8E66171A9C7C84AC9C7F4CEF9424C1B0333FD05B85E1400DDE1DE89082EF25B
