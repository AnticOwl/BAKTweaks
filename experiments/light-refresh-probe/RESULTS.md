# Light refresh experiment — Steam, 20 October 2026

## Scope

One-shot native experiment on the game thread, with BAKTweaks v5.25 and the
Frans Bouma camera already loaded. No UI automation, exposure changes, or
changes to the BAKTweaks production source/DLL were made during this experiment.
The two tested actors were created by the experiment, not existing game lights.

Build: Steam PE timestamp `5F8AA2F9`, SizeOfImage `04398000`.
The IGCS-intercepted UWorld::IsPaused function returned `1` both at setup and
at the beginning of the test. Main game thread ID: `11844`.

## Function checks

Both reflected functions were found in the running game:

- `Engine.ActorComponent.QueueReattach`: parameter storage size 4 bytes.
- `Engine.ActorComponent.ForceUpdate`: parameter storage size 4 bytes.
- Both `bTransformOnly` parameters have offset 0.

Their BmSDK-AK wrappers are public here:
https://github.com/Team-BmSDK/BmSDK-AK/blob/main-bm4/src/BmSDK/Generated/Engine/ActorComponent.g.cs

The SDK identifies LightComponent.SceneInfo at +0x110 and LightToWorld at
+0x160 (matrix translation at +0x190). The component owner matched each
temporary actor.

## Results (both PointLightMovable and SpotLightMovable)

| Operation | Result |
|---|---|
| Actor.SetLocation with delta (64, -32, 16) | Returned true; actor and component LightToWorld already matched the new position BEFORE an explicit ForceUpdate. |
| QueueReattach(true), ForceUpdate(true) | Calls accepted; component flags changed 0x01 -> 0x09 -> 0x01. |
| Direct write to our temporary Actor.Location | LightToWorld did not yet match the new location. |
| Force transform refresh after the direct write | LightToWorld matched the requested delta immediately. |
| Radius/brightness write, then QueueReattach(false), ForceUpdate(false) | Calls accepted; flags 0x01 -> 0x05 -> 0x01; attached state remained true; SceneInfo pointer changed (render-side object rebuilt). |

These are engine-memory and component/render-registration observations, not
a screenshot-based verification of visible light output. Epic was not tested.

## Cleanup

The first Destroy calls returned false. The spawned movable-light defaults
carried `bNoDelete` (Actor +0x100, mask 0x80). A separate cleanup module recovered
only the two unique class/position matches, checked component ownership,
disabled the lights, cleared bNoDelete only on those temporary actors, and
called Destroy again. Both calls returned true; bDeleteMe became true and
SceneInfo was null. No class default or existing game actor was modified.

Each experiment restored the pre-existing tick hook (MinHook status 0).
The probe DLLs remain mapped but their hooks are disabled; restarting the game
removes them. No forced unloading was attempted.

Logs:

- `build/Release/BAKLightRefreshProbe.log`
- `build/Release/BAKLightRefreshCleanup.log`

The initial probe binary predates the cleanup improvement now present in
Probe.cpp. The separate cleanup binary was built from the improved source.

## Conclusion

QueueReattach/ForceUpdate is a feasible immediate refresh path in paused Steam
Arkham Knight. However, the current SetLocation path itself also succeeded
while paused. Missing ForceUpdate alone is therefore NOT an established root
cause of the reported v5.25 editor failure. The next diagnostic should check
that a UI edit reaches ApplyPendingLightEdits and targets the expected actor,
then compare the accepted actor position, LightToWorld, and displayed gizmo.
