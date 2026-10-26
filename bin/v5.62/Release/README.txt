
=== v5.23 Rain ENGINE ONLY + UE3 Input Block fix ===
Rain now mirrors Batman_Arkham_Knight_Rain_BmSDK_v0.5_WRITE_ENGINE_ONLY.CT exactly:
- scan only 0x0D000000..0x10000000
- AOB FF FF 00 00 00 00 00 00 00 00 00 00 ?? ?? ?? ?? 00 00 80 44
- RainEngineBase = hit - 0x210
- ParticleCount = RainEngineBase + 0x21C (hit + 0x0C)
- Apply/Reset write ENGINE ONLY. No Console write and no Engine->Console sync.

Input blocking is now enforced at UE3 PlayerController level on the game thread while BAK Tweaks is open:
IgnoreMoveInput(true), IgnoreLookInput(true), IgnoreButtonInput(true), reasserted every tick.
Original ignore states are captured and restored when BAK Tweaks closes. Win32/RAWINPUT/XInput hooks remain as secondary guards.

BAK Tweaks v5.2 - TRUE IN-GAME IMGUI OVERLAY
=============================================

This build does NOT create a visible external UI window.
The ImGui interface is rendered directly inside Batman: Arkham Knight's D3D11 Present() call.

Controls
--------
F10 : Show / hide BAK Tweaks

Tabs
----
Cape
- Reads the active RCapeStateConfig automatically.
- Values are read from the game and captured once as session defaults.
- Reset Cape restores those startup/session values.

Rain
- ParticleCount is read from the Engine copy.
- Engine -> Console/Config copy is synchronized automatically.
- IMPORTANT: after changing ParticleCount, press F5 / Restart Level before the visual rain changes.
- Reset Rain restores the ParticleCount captured at startup.

Reset Global restores both Cape and Rain session defaults.

Background
----------
The Batman JPEG is embedded into BAKTweaks.dll as a Windows RCDATA resource.
No external JPG is required at runtime.

Build (Visual Studio 2022 x64)
-----------------------------
1. Open PowerShell in this folder.
2. Run:

   Set-ExecutionPolicy -Scope Process Bypass
   .\Fetch-Dependencies.ps1
   .\Build-Release.ps1

Output:
   build\Release\BAKTweaks.dll

Notes
-----
- The tiny hidden HWND created during initialization is ONLY used to discover the D3D11 swap-chain vtable. It is never shown.
- The actual BAK Tweaks UI is drawn in BatmanAK.exe's own swap chain.
- Present and ResizeBuffers are both hooked.

v5.3 diagnostic changes
-----------------------
- UI starts OPEN on first successful in-game Present. This is intentional for diagnosis.
- F10 is polled with GetAsyncKeyState inside Present, so it no longer depends only on the game's WndProc.
- BAKTweaks.log is created next to BAKTweaks.dll.
- If no overlay appears, send BAKTweaks.log. The key lines are:
  D3D11 hooks enabled
  HookPresent: first in-game Present hit
  ImGui initialized on game swap chain

v5.3.2 UI Scale
- Added in-game UI Scale slider (1.00x to 3.00x)
- Default scale: 1.75x for high-resolution displays such as 6K
- Font and ImGui widget sizes scale together

v5.4 Cape Wind
- Cape UI reduced to confirmed/useful controls only:
  Wind X / Y / Z, Max Wind Speed, Force Strength, Max Force Cap.
- Editing Wind X/Y/Z automatically enables bEnableWindEffect + bOverrideWindDirection.
- Cape startup/session snapshot now also stores/restores Flags and Wind X/Y/Z.
- Reset Cape restores the complete captured cape baseline, including original flags.

v5.7 - BatLit Native
--------------------
BatLit is now implemented directly inside BAKTweaks.dll.
There is NO BmSDK companion .cs file and NO shared-memory bridge.

BatLit workflow:
- Create Spot
- Create Point
- every spawned light gets its own record: Spot 1, Spot 2, Point 1, etc.
- each record has an Edit button
- Edit opens a separate ImGui window for that light only
- every light keeps independent values

Per-light editor currently exposes the documented scalar/boolean BmSDK light fields, including:
- Enabled
- Color
- Intensity (UE3 property name: Brightness)
- Radius / falloff / quadratic falloff
- position
- Spot inner/outer cone and rotation
- shadow controls and shadow quality
- radiosity / culling / behaviour flags
- lighting channels
- bloom / occlusion
- gel / effector values
- NVIDIA light shafts / volumetric values
- opacity-shadow values

Actions are queued from ImGui and executed from Batman's EngineTick hook, rather than
calling UE3 spawn/property functions from the D3D11 Present thread.

Native UE3 offsets are guarded by a BatmanAK.exe build check. The expected executable
matches the BatmanAK.exe supplied during development (PE TimeDateStamp 0x5F8AA2F9,
SizeOfImage 0x04398000). If the build differs, BatLit refuses to install its native hook
instead of using unverified offsets.

First test order:
1. Open F10 -> BatLit.
2. Create Spot.
3. Confirm "Spot 1" appears.
4. Click Edit.
5. Test Color + Intensity, then Apply.
6. Test Move to Camera.
7. Then Radius, cones and shadows.
8. Create Spot 2 and verify Spot 1 / Spot 2 retain different values.

F5 remains reserved for Restart Level.
F10 remains the BAK Tweaks UI toggle.


v5.8
----
- Cursor / F10 fix after resolution or fullscreen-mode changes:
  ResizeBuffers now refreshes the game HWND/WndProc ImGui binding, drops stale
  ClipCursor state, recreates ImGui DX11 device objects, and re-acquires UI input.
- F10 async polling is now only a fallback if the game WndProc hook was replaced.
- BatLit queued commands now have a second game-thread dispatcher through
  UObject::ProcessInternal, matching the Arkham City approach in spirit.
  EngineTick remains primary; ProcessInternal can service pending Spawn/Edit
  commands while normal world ticking is paused.
- Still one DLL only. No C# companion / no shared-memory bridge.


v5.9
----
Misc tab added.

Window / Focus:
- Pause on Loss of Focus
  Backed directly by Engine.Engine.bPauseOnLossOfFocus
  (flags at Engine + 0x5B4, mask 0x00800000).
- Reset Misc restores the value captured from the live Engine instance.
- Reset Global also restores this setting.

No config file and no C# companion are required.


v5.10
-----
F10 regression fixed.

F10 is now handled only by GetAsyncKeyState from Present, exactly once.
The WndProc no longer owns the F10 toggle. This avoids relying on Arkham
Knight delivering WM_KEYUP for F10 and also avoids double-toggle races.

The v5.8/v5.9 resolution-change cursor/WndProc rebinding remains in place.


v5.12
-----
- Misc tab removed completely.
- Pause on Loss of Focus experiment removed completely.
- Reset Global is back to Cape + Rain only.
- BatLit native, F10 handling, resolution/cursor rebinding and paused-light work remain unchanged.


v5.13 SAFE SPAWN
----------------
This version deliberately narrows BatLit creation to the minimum proven path.

Changes:
- Removed the generic ProcessInternal dispatcher completely.
  A queued spawn could otherwise execute while UE3 was already inside script VM
  execution, which is an unsafe re-entry point even when the game is not paused.
- Spawn commands execute only from the top-level FEngineLoop::Tick hook.
- UWorld is now obtained exactly like BmSDK Game.SpawnActorInternal:
    GetWorldInfo().Outer.Outer
  The previous PlayerController->Outer guess has been removed.
- Create Spot / Create Point performs NO property writes after spawning.
  The freshly spawned light's live values are only captured as its defaults/state.
- No hard-coded Intensity / Radius / cone / shadow preset is applied on creation.

FIRST TEST:
F10 -> BatLit -> Create Spot.
Do not press Edit/Apply yet. We first validate native SpawnActor alone.


v5.14 BmSDK PATH
----------------
BatLit spawn now reproduces the known-working BmSDK path instead of guessing
the live WorldInfo object.

World resolution is a direct C++ transliteration of generated
Engine.WorldInfo.GetWorldInfo():
- GetDefaultObject(Engine.WorldInfo)
- temporarily clear FUNC_Native
- set FUNC_Defined
- set iNative = 0
- ProcessEvent on the WorldInfo CDO
- restore FunctionFlags and iNative
- read returned WorldInfo pointer
- world = WorldInfo.Outer.Outer

SpawnActor parameters are kept exactly as Framework.Game.SpawnActorInternal:
Template=0
bNoCollisionFail=1
bRemoteOwned=0
Owner=0
Instigator=0
bNoFail=1
OverrideLevel=0

Extra stage logging was added so if Create Spot still hangs we can identify
whether it stops before GetWorldInfo or inside UWorld::SpawnActor.

FIRST TEST:
F10 -> BatLit -> Create Spot.
No editing yet.


v5.15
-----
- Main ImGui title now shows: BAK Tweaks v5.15
- About header also shows v5.15
- Startup log explicitly records: BAK Tweaks v5.15 loaded

This build otherwise keeps the v5.14 BmSDK-path spawn test unchanged so the
version number alone makes it obvious which DLL/source build is being tested.


v5.17
-----
The TRACE log showed the freeze at:
TRACE 10: GetPlayerView begin

That means SpawnActor had not even been reached.

Cause removed:
the native code was scanning BM4's 785000-entry inline GObjObjects array to
find RPlayerController. That scan is extremely expensive in this implementation
because every entry/class walk performs guarded VirtualQuery checks.

v5.17 now follows BmSDK's actual GetPlayerController path:
Engine.Engine.GetEngine()
 -> Engine.GameViewport @ +700
 -> GameViewportClient.FindPlayerByControllerId(0)
 -> LocalPlayer return @ params+4
 -> Player.Actor @ +92
 -> PlayerController.GetPlayerViewPoint()

Expected trace:
TRACE 10
TRACE 11
TRACE 13
TRACE 14
TRACE 15
TRACE 16
then the WorldInfo / SpawnActor trace.


v5.18
-----
- Title bar: BAK Tweaks v5.18
- BatLit Move to Camera is now a dedicated operation.
- It no longer calls ApplyState().
- Point/Spot: SetTranslation(camera position) only.
- Spot: SetRotation(camera rotation) only.
- Added MOVE 00..60 logging and immediate position/rotation readback.


v5.19
-----
Move to Camera now reads PlayerController.PlayerCamera -> CameraCache.POV first.
Offsets from BmSDK:
PlayerController.PlayerCamera = +0x478
Camera.CameraCache = +0x570
CameraCache.POV = +0x04
POV Location = +0x00, Rotation = +0x0C, FOV = +0x18

GetPlayerViewPoint remains only as fallback.
CAM CMP logs the difference between the two camera sources.


v5.20
-----
Compile fix only:
- added a forward declaration for GetPlayerController() before
  GetRenderedCameraFromCache().
- no CameraCache/Move-to-Camera behavior change from v5.19.


v5.21
-----
Link fix:
- v5.20 accidentally referenced a non-existent GetPlayerController().
- CameraCache now calls the already-existing GetPlayerController_BmSDKPath().
- Removed the bad forward declaration.
- No behavioral change beyond fixing the linker error.


v5.22
-----
Move-to-Camera root-cause fix.

The camera read was valid. The previous build moved the LightComponent with
PointLightComponent.SetTranslation / SpotLightComponent.SetRotation. Those are
component transforms, not the movable light Actor's world transform.

v5.22 resolves and calls:
- Engine.Actor.SetLocation(camera world position)
- Engine.Actor.SetRotation(camera world rotation)

on the spawned SpotLightMovable / PointLightMovable Actor itself.

Actor readback:
- Actor.Location +0x0C4
- Actor.Rotation +0x0D0

CameraCache path and GetPlayerViewPoint comparison remain logged.


v5.23
-----
Rain resolver reworked.

The old resolver scanned fixed low virtual-memory ranges (0x08000000..0x10000000).
That is not reliable for this x64 runtime / ASLR session.

v5.23 resolves Engine.RRainComponent through the existing UE3/BmSDK object system:
- FindObject("Engine.RRainComponent")
- enumerate GObjects for live RRainComponent instances
- ParticleCount = +0x21C
- resolve the RRainComponent class default object (CDO) with GetDefaultObject()
- Rain Amount writes every live RRainComponent plus the CDO/template

The UI label is now "Rain Amount".
Restart Level is still required for the visible particle-count change.

Environment additions (current build):
- Location-aware Pollen / Dust / Snow live component controls.
- Location-aware Exponential Height Fog controls: Density, Start Distance, Max Opacity, Height Falloff, Height, plus Fog ON/OFF.
- Detective / XRay ON/OFF using live BmGame.RForensicsDevice SDK functions TurnOnXray / Deactivate.

v5.23 Lightning VFX test
------------------------
Environment tab now includes direct controls for the two natural RLightningActor VFX:
- Small Lightning (LightningStrike)
- Vertical Lightning (Vertical_LightningStrike)

Each variant has its own Duration (seconds), Distance, and Trigger button.
The implementation reuses the natural renderer-registered ParticleSystemComponent and updates the baked ParticleModuleLifetime lookup before retriggering it in front of the current camera.


v5.28 Pose Inspector foundation
-------------------------------
- Adds a read-only Pose tab for enumerating current-world Pawn characters and their SkeletalMesh bones.
- Character discovery follows the SDK WorldInfo.PawnList / Pawn.NextPawn chain. It does not sweep the global GObjects array.
- Controller.Pawn, Pawn.Mesh, WorldInfo.PawnList and Pawn.NextPawn offsets are resolved from SDK reflection and checked against the BmSDK-AK generated layouts before use.
- Bone names and parent relationships are read through SkeletalMeshComponent.GetBoneNames / GetParentBone. Parent queries are spread over EngineTick calls.
- Selecting a bone requests component-space location/quaternion reads through the BmSDK getters. In the first Catwoman runtime check, the values did not appear; v5.30 adds diagnostics to identify why.
- This version does not write bone transforms. Pose editing waits for a confirmed writable transform path and animation-update timing.
- BmSDK-AK API inventory (`src/api-reference.json`): `GetBoneNames`, `GetParentBone`, and bone transform reads are exposed, but no `SetBoneLocation` / `SetBoneRotation` setter is listed for `SkeletalMeshComponent`. `SetRBPosition` / `SetRBRotation` target physics bodies in `PHYS_RigidBody`, so they are not used as a general animation-pose writer.
- Earlier Cheat Engine work recorded in the ChatGPT conversation “Vérifier les bones Arkham” successfully froze Batman and then Head-only through a `SpaceBases` writer. This is the primary write-path lead to recover; it was validated outside the current BAKTweaks code.
- The old test used Batman Head index 7 and Eyes 47/48 in a 297-bone list. The runtime pointer still needs resolving after level reloads; candidate register R13 changed repeatedly, while RSI was observed as the SkeletalMeshComponent at the writer around game RVA +0x2F732C (`mov rax,[rsi+0x404]`).
- The BmSDK exposes `AnimTree.SkelControlLists`, `AnimTree.FindSkelControl(s)`, `SetUseSavedPose`, and `SkelControlSingleBone` rotation/translation fields as a secondary route. The current pose UI still performs reads only; neither write path is enabled in BAKTweaks yet.
- See `experiments/pose-writer/RESULTS.md` for the recovered findings and the next resolver/restore checks. Keep all writes on EngineTick and scoped to a selected character/bone.
- The first pass covers Pawn-based characters with a valid SkeletalMesh. Non-Pawn character actors may need a separate discovery path.
- Runtime validation on the Steam build: Catwoman is found as Player #15 through the Pawn chain, with a valid SkeletalMesh and 292 bones. Parent indices and bone transforms are readable; displayed names are still FName IDs.
- The first Catwoman save was loaded for this check and was not overwritten. No save data was changed.


v5.29 Pose focus option
-----------------------
- Adds an opt-in control in the Pose tab to keep the game running while its window is unfocused.
- The option resolves Engine.Engine.bPauseOnLossOfFocus through UE3 reflection and requires the expected +0x5B4 property layout for the supported game builds.
- When enabled, only the pause-on-focus-loss bit is cleared; the original value is captured and restored when the option is disabled or Reset Global is used.
- The setting is off by default and engine writes are dispatched from EngineTick.
- Runtime check on the Steam build: the user confirmed the option keeps gameplay running when the game window loses focus.
- BAK Tweaks does not support safe hot-unloading; restarting the game resets the setting if the DLL remains active.
- This option does not change save data.

v5.30 Pose read diagnostics
---------------------------
- Adds a per-selection status and one BAKTweaks.log entry for GetBoneLocation / GetBoneQuaternion, including return validity.
- Getter argument and return offsets follow BmSDK-AK Generated/Engine/SkeletalMeshComponent.g.cs.
- No pose writes or save changes are introduced.
- Catwoman runtime check found 292 bones. The selected-bone trace reports GetBoneLocation success and GetBoneQuaternion failure.

v5.31 Pose matrix fallback
--------------------------
- If GetBoneQuaternion fails, reads the selected bone's FMatrix with SkeletalMeshComponent.GetBoneMatrix and converts it through Core.Object.MatrixGetRotator.
- The UI reports quaternion rotation when available, otherwise matrix-derived pitch/yaw/roll in degrees.
- Runtime check: both GetBoneQuaternion and GetBoneMatrix fail while GetBoneLocation succeeds; the fallback is not yet working.
- This remains read-only; no bone values or save data are written.

v5.32 Pose read alignment diagnostics
-------------------------------------
- Aligns FQuat and FMatrix parameter buffers to 16 bytes before ProcessEvent.
- BAKTweaks.log records the structured exception code for the quaternion, matrix, and matrix-to-rotator calls.
- This remains read-only; no bone values or save data are written.

v5.33 Automatic focus-pause handling and reloadable pose probe
-------------------------------------------------------------
- Automatically disables Engine.Engine.bPauseOnLossOfFocus when BAK Tweaks starts, so gameplay continues while the window is unfocused.
- The Pose checkbox can restore the original pause behavior; Reset Global returns to the automatic enabled default.
- Adds BAKPoseProbeLauncher.dll, a one-shot launcher that asks BAK Tweaks to repeat the selected-bone read on EngineTick, then unloads itself.
- The launcher requires BAK Tweaks v5.32 or newer. Its request/result notes are written to BAKPoseProbeLauncher.log and BAKTweaks.log.
- Probe reads are read-only and do not alter pose values or save data.

v5.34 — Pose space diagnostic
-----------------------------
- Pose Inspector now reads GetBoneLocation and GetBoneQuaternion for numeric Space values 0, 1, and 2 on the selected bone.
- The UI displays numeric spaces without asserting their enum meaning. This corrects v5.33's unverified "Component" labels.
- Read-only only: no bone values are written and no save files are modified.
- The automatic keep-running-on-focus-loss behavior is retained.

v5.35 - Pose buffer diagnostic
------------------------------
- Labels observed getter results as World (0) and Component (1); Space 2 remains visible as an alias check because it matched World in Catwoman tests.
- Resolves SpaceBases and LocalAtoms offsets from live UProperty metadata and reads the selected element from both arrays for comparison.
- No pose data is written. This step identifies the buffer to use before building the pose editor.

BAK injector helper
-------------------
Run `bin\\v5.56\\Release\\BAKInjector.exe` or `dist\\BAKTweaks_5.56\\BAKInjector.exe` for the standalone native injector. It detects a running Steam/Epic BatmanAK.exe and offers the one-shot pose probe and BAK Tweaks. It requests UAC elevation itself and has no PowerShell runtime dependency.

The pose probe unloads itself after it queues a read request. BAKTweaks.dll currently has no safe hot-unload shutdown path; to replace an already-loaded main DLL, restart the game. The injector refuses duplicate module loads rather than attempting an unsafe FreeLibrary.

v5.36 - Correct FBoneAtom pose buffers
---------------------------------------
- Corrects the diagnostic stride: both SpaceBases and LocalAtoms are 32-byte FBoneAtom arrays.
- SpaceBases now displays the selected component-space translation/quaternion; LocalAtoms displays the selected local transform.
- Confirms the component-space buffer candidate before a reversible pose edit is introduced. Read-only.

v5.37 - Reversible component-space translation prototype
--------------------------------------------------------
- Adds an opt-in, selected-bone translation offset after the verified Steam SkeletalMesh pose-finalizer routine.
- Offset starts at zero and is reapplied to the live SpaceBases atom after animation evaluation; turning the option off disables the hook and restores the normal animation path.
- The hook is installed only after the option is enabled, checks the expected Steam function signature, selected mesh, bone range, TArray layout, and writable memory.
- This is an experimental first write path for Pawn-based characters. Rotation editing, child propagation, other character classes, and Epic offsets are not implemented yet.
- Requires an in-game visual test on a non-root bone with a small translation offset. It does not modify save data.

v5.38 - Freeze evaluated pose and add writer diagnostics
--------------------------------------------------------
- Adds an opt-in full-pose freeze by capturing the selected Pawn's component-space SpaceBases and restoring the snapshot after each animation update.
- Retains the selected-bone translation offset, which can now be applied on top of a frozen pose.
- Displays finalizer call/match counts, frozen bone count, and write counts to show whether the hook reaches the selected mesh.
- Freeze and edit controls are off by default. Turning both off disables the hook and returns the skeleton to animation. No save data is written.

v5.39 - Standalone injector executable
---------------------------------------
- Replaces the PowerShell launcher dependency with a native 64-bit Windows GUI executable.
- Requests administrator elevation, detects a running Steam/Epic BatmanAK.exe, restricts injection to the project BAKTweaks/probe DLLs, and refuses duplicate module loads.
- The pose freeze and realtime component-space translation prototype are carried forward unchanged from v5.38.

v5.39.1 - Injector UTF-8 display fix
------------------------------------
- Compiles the native injector source and UI strings as UTF-8 so French accents render correctly.
- The already-loaded BAKTweaks DLL does not need to be replaced for this injector-only correction.

v5.40 - Post-tick evaluated pose freeze
---------------------------------------
- Uses the existing EngineTick hook after the original engine tick to capture and reapply SpaceBases, avoiding a Steam internal function that Catwoman's active animation path did not call.
- The Pose tab reports post-tick passes and valid pose-array reads alongside freeze and edit write counts.
- Snapshot capture and selected-bone translation are applied each engine tick for realtime updates.
- Catwoman runtime result: the snapshot-capture log appears, but visible animation continues. This implementation is not a working freeze; the next iteration must recover the earlier CE timing at the evaluated SpaceBases writer before requesting another user test.

v5.41 - Pose handler variants
-----------------------------
- Static Steam disassembly shows the engine dispatches three separate SpaceBases evaluation handlers for modes 1, 2, and 5. v5.39 only hooked handler 2, which explains its zero callbacks on Catwoman; v5.40 wrote after EngineTick, where the new pose could be overwritten later.
- Hooks all three verified handler entry points and reapplies the captured SpaceBases snapshot immediately after the selected mesh's handler returns.
- Displays per-handler call counts, selected-mesh matches, valid arrays, freeze writes, and the active hook mask to identify Catwoman's actual branch.
- Hooks are installed only while freeze or editing is enabled, and are disabled when both controls are off. No game pause or save write is used.
- Catwoman runtime log caught a signature-length mistake on variant 3 (mode 5), so that hook was not installed; v5.42 corrects the signature and logs each successful installation.

v5.42 - Correct mode 5 signature

v5.43 - Match the active pose buffer
Catwoman's v5.42 counters showed that a finalizer ran but never matched the selected mesh, leaving the freeze at zero writes. The writer now accepts a shared SpaceBases buffer when its data pointer and bone count match the selected mesh, and logs a small number of pointer/array diagnostics when they do not. This should distinguish mesh aliases from a buffer mismatch on the next run.

v5.44 - Hook the common animation dispatcher
The v5.43 Catwoman run showed that its 292-bone mesh used an animation branch outside the three hooked SpaceBases handlers. Static disassembly identified the common dispatcher at Steam RVA +0x94F970; it routes modes 1, 2, 5, 3, and 4, and its mode 3/4 path calls a separate evaluator. v5.44 hooks this dispatcher after the selected component's active evaluator returns, so it can capture the component's final evaluated pose regardless of branch. The prior handler hooks remain for diagnostics and compatibility.

v5.45 - Keep a stable pose snapshot across buffers
The live v5.44 Catwoman run matched the selected 292-bone component and wrote the pose repeatedly, but the capture log showed recaptures and the visible pose still varied. The freeze snapshot is now keyed to the stable selected mesh component rather than the SpaceBases data pointer, so buffer changes reuse the original captured pose.

v5.46 - Pause only the selected mesh animation
Use the SDK-reflected SkeletalMeshComponent.bPauseAnims flag while Freeze is enabled. Preserve the original bit and restore it when Freeze is disabled, the selected character changes, or the world reloads. This leaves game time and other characters running; the evaluated-pose writer remains available for edits.

v5.47 - Trace actual character mesh updates
The v5.46 pause label reflected a tool-side flag and Catwoman continued moving. v5.47 reads bPauseAnims immediately after the write and after the original game tick. It samples SpaceBases changes on each SkeletalMeshComponent found through the selected Pawn's reflected Components and AllComponents arrays. The Pose tab reports both bit readbacks and per-mesh transform-change counts. This is diagnostic instrumentation; a successful Catwoman freeze is not claimed.

v5.48 - Identify the live animation dispatcher caller
The v5.48 Catwoman screenshot confirmed bPauseAnims stayed set while the selected 292-bone SpaceBases buffer changed on 1140/1141 samples; two other attached skeletal meshes also changed. Its four statically known dispatcher caller counters were all zero while `other` reached 4340, so the expected callsites did not identify the live path. v5.49 captures up to eight exact runtime return addresses, displays their game-relative RVAs and counts, and logs each newly observed address to BAKTweaks.log.

v5.49 - Capture actual dispatcher callers
On Catwoman, enable Freeze long enough for the live caller rows to populate, then send a screenshot of those rows and the `POSE CALLER` lines in BAKTweaks.log. These addresses establish the caller before further disassembly; the freeze itself remains unconfirmed and ineffective in the v5.48 capture.

v5.50 - Trace the full skeletal component buffers
Extends per-component diagnostics to SpaceBases, LocalAtoms, CachedSpaceBases, and CachedLocalAtoms. Reports the selected component's live dispatcher mode distribution plus ParentAnimComponent, AttachedToSkelComponent, ParentBoneMap, AnimTreeTemplate, Animations, AnimNodePose, animation tick arrays, and SkelControlInstances. This is tracing only; it does not add new pose writes.

v5.51 - Trace Catwoman's mode-0 skeletal evaluator
Adds a read-only hook at the confirmed mode-0/1 control evaluator (+0x304340). Reports calls for modes 0/1, calls on the selected Pawn mesh, and whether each call changes its SpaceBases hash. Catwoman's v5.50 capture shows the selected 292-bone mesh uses mode 0 while its 204/297-bone child meshes use mode 1. This build traces the direct evaluator and does not add new pose writes.

v5.52 - Separate evaluator argument and dispatcher mode
Clarifies that +0x304340's argument 0/1 is not the animation dispatcher's mode byte. Splits the clipped UI counters across short lines and compares the selected SpaceBases hash before/after the dispatcher, after the pose writer, and across consecutive post-writer calls.

v5.53 - Resolve bone names using the organized Batman FName table
Uses the 297-entry organized Batman FName dictionary as a shared name dictionary, matching each live bone by its exact (FName index, number) pair. The list still follows the active character's own GetBoneNames order and GetParentBone hierarchy, so Catwoman's 292-bone indices are not treated as Batman indices. Displays resolved names and name-derived body categories; unmapped entries retain their raw FName ID. In the observed Catwoman table, bone 7 resolves to Bip01_L_Clavicle, so Batman's Head[7] label must not be reused for Catwoman. This is name resolution only: pose writing and freeze behavior are unchanged.

v5.54 - Resolve pose FNames from the game's NamesDump
Loads `NamesDump.txt` from the BatmanAK.exe directory on the BAK Tweaks worker thread and resolves each live bone's FName index to the exact global UE3 name. Keeps the bundled 297-entry Batman table as fallback if the dump is absent or invalid. The Catwoman screenshot IDs now independently resolve to game names including `Point_L_Finger3` (184176), `Flappy_L_Arm_Biceps_Prnt` (183304), and `Bip01_L_Clavicle` (1543). Pose indices and parent indices still come from the active character. This changes naming only; no pose writes or freeze behavior are changed.

v5.55 - Resolve numbered UE3 FNames and refine bone groups
Applies the UE3 FName numbered-instance rule: for a global name index with Number greater than zero, display the base name plus suffix `Number - 1`. This resolves Catwoman's remaining `Whip_Bone` chain from FName `184648:11` onward (for example, `Whip_Bone_10`) while retaining its own live indices and parents. Refines categories so fingers and hands are not grouped under arms and labels whip bones explicitly. Naming/category changes only; no pose writer or freeze changes.

v5.56 - Additive live rotation control for the selected Catwoman bone
Adds Pitch / Yaw / Roll component-space rotation offsets to the selected-bone pose editor. The quaternion delta is recomputed from the evaluated SpaceBases pose after the existing animation dispatcher, so repeated updates do not accumulate rotation drift. Numeric offsets start at zero and writes remain disabled until “Apply offsets to selected bone” is checked. Adds a reset button for translation and rotation offsets. This is the first rotation-edit test path; Catwoman requires visual validation before it is extended to other characters or paired with the gizmo.

v5.57 - Rotate and translate a complete bone branch
Fixes rotation so descendant component-space positions orbit the selected bone's pivot, not only their orientations. Builds a selected-bone descendant list from Catwoman's live parent hierarchy and applies the same component-space delta to the entire branch; translation offsets move the branch together. Adds a branch-size readout and an option to limit edits to the selected bone. Editing should be tested with “Freeze selected Pawn pose” enabled while gameplay continues running. A full game pause stops the update path and cannot apply live pose changes.

Pose-tool compatibility targets
The final pose tool must work in PM / Photo Mode, François Bouma's IGCS/free camera, and Arkham Knight's built-in debug camera. The application path must account for their pause/tick and render order; these contexts remain untested until posing is functional.

v5.61 - Remove the high-frequency SpaceBases helper detour
The v5.60 Catwoman trace showed +0x3288A0 being called 1,218,811 times, matching Pawn arrays 959,617 times and copying entire transform arrays each time. v5.61 no longer hooks this helper. Freeze restores all tracked SpaceBases arrays once after EngineTick and LocalAtoms at +0x2F7370. This should remove the measured source of severe frame-rate loss while retaining the user-confirmed complete Catwoman freeze; performance and freeze persistence require a fresh runtime check.

v5.62 - Skip Rocksteady combine calls for captured Pawn buffers
Restores the verified +0x3288A0 hook, but checks the output buffer before calling the original helper. If it is one of the captured Pawn SpaceBases arrays and bone count matches, the helper is skipped and the captured pose remains in the array. Other actors continue through the original helper. This avoids repeated full-array copies and avoids doing the combine work for the frozen target. The v5.61 test showed that removing the hook entirely also removed Catwoman's freeze; v5.62 needs a fresh test for both freeze and frame rate.
--------------------------------
- Corrects the mode 5 handler signature to the actual four-byte sign-extended immediate in the function prologue.
- Logs the installation of each mode 1, 2, and 5 finalizer hook so a failed or missed handler is visible without relying on hidden UI counters.

v5.59 - Apply freeze at Rocksteady's component-space bone-combine helper
Hooks the verified Steam helper at RVA +0x3288A0, which contains the previously CE-tested SpaceBases quaternion store at +0x328AC4 and the translation store at +0x328ACC. After the original helper runs, restores captured transforms only when its output array matches a tracked skeletal mesh attached to the selected Pawn. Reports helper calls, matched Pawn arrays, and snapshot restores. This is a targeted prototype; Catwoman visual validation is required. If the body continues to move, use the new counters and component pose-change diagnostics to trace the next writer or render buffer. Restart the game before injecting because the prior DLL is already loaded.

v5.60 - Restore Catwoman LocalAtoms at the Rocksteady control-update boundary
Adds a checked Steam hook at the Rocksteady LocalAtoms update wrapper (+0x2F7370). While Freeze is enabled, captures and restores the tracked Pawn components' LocalAtoms after that original wrapper returns, alongside the existing +0x3288A0 SpaceBases restore and post-EngineTick restore. Adds LocalAtoms helper calls/matches/writes counters. This targets a measured v5.59 condition: the tracked child components' LocalAtoms changed on 2972/2973 samples even while SpaceBases snapshots were being restored. The effect on Catwoman's head/arms needs visual confirmation; this does not claim that LocalAtoms are the final render source.
