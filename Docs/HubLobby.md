# Hub and shared destination selection

## Intended flow

The room owner and joining players enter `/Game/Maps/Hub`. Its own
`AMCHubGameMode` keeps the gameplay director, waves, level objectives and initial
perk choice out of the hub. Players retain the character, tools and emotes.

Every participant, including the host, must stand inside `Hub_ReadyZone`. Entry
marks that player ready; leaving clears readiness. The host can open selection
only after the server confirms that all current participants are ready. Joining
or leaving players change the participant count. A solo room uses the same rule
with one participant.

The whole room travels to `/Game/Maps/L_HubSelection`. This is a dedicated clone
of the existing 3D mouth scene, preserving its artwork and animations. Selection
state belongs to the server: only the host changes the chosen day or confirms
launch; every player presents that same choice locally. Cancelling returns the
room to the hub and clears readiness. Starting travels the entire room to the
selected map, with the gameplay entry barrier waiting for player loading.

This document describes the implemented contract and the intended verification;
only a validation report from a completed run establishes that an individual
check passed. Preparing these files does not run an Editor build or network test.

## Editor setup

`Hub_ReadyZone` is a normal movable actor, not a hardcoded coordinate check.
Move or rotate it in the level editor. Its `ReadyVolume` box is the root, and its
origin is the box center. `SetZoneExtent` changes its half extent and rebuilds
the temporary floor border and text. Default authoring uses a 480 × 480 cm
footprint with a 280 cm tall trigger. The future mouth mesh can be assigned to
the optional `MouthVisual` component. The trigger continues to define readiness.
The temporary outline uses four thin opaque border meshes. Before authoring,
the tool verifies the loaded material exposes the runtime `Color` and
`Roughness` parameters. If the engine material lacks either, it creates the
small owned `/Game/Gameplay/Hub/M_HubReady` material and assigns it to default
border slots. Authored custom materials are preserved. The owned material is
referenced by the saved Hub, so it is a normal cook dependency.

The initial placement must be selected from the real Hub floor, near the upper
right corner in the approved top-down reference. Avoid using the bounds of the
whole decorative scene: the broccoli, mushrooms and lighting extend beyond the
walkable floor. `inspect_hub.py` records the actual actor geometry. The saved
inspection on 2026-10-19 found the floor actor `Plane`, mesh
`/Game/FromBlender11/SM_Plane`, centered at approximately
`(-844.19, 1211.57, -1.28)` with world XY half extents `(2695.41, 3117.13)`.
These figures are evidence, not permanent placement constants.

No PlayerStarts existed in the inspected Hub. The authoring tool can add four
centrally sampled, collision-checked starts only when no starts are authored.
Existing starts, scenery and placed character actors are preserved. The native
Hub mode must account for the two already placed `BP_PlayerCharacter` actors
when managing controlled player pawns; this is a runtime concern, not permission
to delete designer scenery or actors.

## Destination table

`/Game/Gameplay/Hub/DT_HubDestinations` uses `FMCHubDestinationRow`:

| Field | Purpose |
|---|---|
| Row name | Stable identifier, initially `Day01` through `Day07` |
| Day | Day represented by the 3D mouth scene |
| DisplayNameKey / DescriptionKey | Shared localization catalog keys |
| DisplayName / Description | Fallback copy |
| bEnabled | Whether the host may launch this destination |
| Map | Soft reference to the actual gameplay world |
| SingleDayProfile | Soft reference to the gameplay-day configuration |

Only `Day01` is initially enabled, using `/Game/Maps/L_Mouth` and
`/Game/Gameplay/CoreLoop/DA_SingleDay`. The remaining mouths can be browsed but
have no launchable destination until their gameplay maps/profiles are prepared.
Changing the selected visual mouth does not make a disabled destination valid.
The server checks availability again when the host confirms launch.

New copy is appended to `/Game/Gameplay/UI/Data/DT_UI_Localization` under `hub.*`
keys. RU and EN are supplied; the other language columns remain available for
translation and use the shared catalog fallback. Existing localization rows and
their FText serialization are preserved.

## Authoring tools and preservation

- `Tools/Unreal/Hub/inspect_hub.py`: read-only geometry, collision, start and camera
  evidence. Changes no package and starts no PIE or build.
- `Tools/Unreal/Hub/author_hub.py`: default `run()` is a plan. Explicit `apply`
  requires the rebuilt native Hub classes and a clean idle editor.
- `Tools/Unreal/Hub/destinations.json`: seed for missing destination rows.
- `Tools/Unreal/Hub/localization.json`: seed for missing shared-catalog rows.

All reports, verified package backups and temporary files belong on E:, normally
`E:/CodexWork/MessControlHub_20261019/`. Existing target packages are copied and
SHA-256 checked before the first mutation. The tool saves only the existing Hub,
the owned selector clone, the destination table and appended localization rows.
One owned ready-border material is additionally saved only if the engine
material does not meet the native MID parameter contract.
It never invokes SaveAll, modifies the original menu scene, or saves `L_Mouth`.
The original selector package hash is checked again after authoring.
The duplicated selector World is configured and explicitly saved with `save_map`
before any attempt to open it. Its map-open and gameplay checks run in a fresh
owned Editor process. This avoids retaining an unsaved standalone World while
the editor changes maps. The report is persisted after each completed phase,
so a later engine crash does not hide a successful Hub or table save.

Repeated authoring preserves the ready-zone transform, designer table values,
existing starts and existing localization copy. It adds no duplicate ready zone.
An occupied target path, duplicate ready zones, unsaved packages or unsafe floor
samples stop the operation for review. A failed operation retains its backups
and report; do not overwrite loaded packages externally while Unreal is running.

Example invocation after inspecting and approving the actual XY location:

```python
import runpy
api = runpy.run_path('E:/DEVGAME/MessControl/Tools/Unreal/Hub/author_hub.py')
api['run']('apply', placement={
    'ground_actor': 'Plane',
    'fraction': [0.75, 0.80],  # Example only: confirm orientation/clearance first.
    'extent': [240.0, 240.0, 140.0],
})
```

The fractions locate the center inside the real floor's world XY bounds. They
are not screen coordinates. Center and corner ground traces plus capsule
clearance reject a footprint that falls outside the floor or into scenery.

## Validation scope

The short solo check should cover entering/leaving readiness, moving the zone,
host confirmation, unavailable days, the existing 3D mouth transitions, cancel
back to the hub, and launch into the existing Day01 gameplay route. Record the
hub and selector in the gameplay camera when practical.

Identical choice presentation for host and client, late joins, disconnects and
the loading barrier require a separately authorized multiplayer check. Solo
validation cannot establish those results. Cook/package is also separate: both
Hub and L_HubSelection must be listed in the cook configuration, and the reused
3D widget and scene dependencies must be included in the packaged game.

## Controls and final presentation

In the hub, move with the ordinary player controls. Once everyone is inside
`Hub_ReadyZone`, the host presses Enter to open destination selection.
The zone is initially near the upper-right corner of the supplied reference;
its saved center is approximately `(-2730.97, 3455.90, 141.72)` with half extent
`(240, 240, 140)`. Its transform remains editable. Hub characters initialize the
existing orbit camera so the view follows them outside the mouth arena bounds.

In selection, the host clicks a visible mouth, uses the arrow keys, or scrolls
the mouse wheel. The selected mouth opens using the existing stage animation.
Enter or the compact Start button launches the available destination; Esc or
Back returns the team to the hub. Guests see the host's selection and can open
their local pause menu with Esc. The separate seven-day side panel was removed.
The original mouth labels remain; only a compact status and Start/Back row is
added at the bottom. Existing main-menu packages are unchanged.

## Verification on 2026-10-19

- `MessControlEditor` Development Win64 build passed.
- Three focused `MessControl.Hub` rule tests passed: complete-team readiness,
  destination safety/rotated volumes, and host-only selection/loading barriers.
- The final owned standalone PIE run passed 20 checks: actual walking into,
  out of and back into readiness; camera following at the distant corner;
  normal travel to selection; mouth-button callbacks selecting days 2 and 3;
  unavailable-day rejection; return with readiness reset; then normal day-one
  travel, automatic release of the loading barrier, three first-perk cards,
  a real perk choice, and four further gameplay seconds alive. Both initial
  Hub entry and return also verify that only the actual player's pawn exists.
- Final visual review found two authored preview characters auto-possessing
  AI controllers. Hub startup now removes those runtime preview pawns and
  their AI controllers while retaining the saved editor actors. Player-owned
  pawns are protected; spawned orphan recovery retains its one-second grace.
  A read-only diagnostic confirmed the original issue before the fix.
- The final UI was visually reviewed in the gameplay window. Input verification
  exercised the actual native button callbacks and their native-calculated
  CanvasSlot rectangles. It did not inject a physical mouse or keyboard.
  Python copies of `FGeometry` returned zeros in this engine binding, so those
  copies were not used as evidence of the live Slate layout.
- The PIE session stopped cleanly; temporary play settings, console variables
  and the initial editor map were restored; no dirty packages remained.
- Authoring preserved all pre-existing localization rows and the source menu
  scene hash. Existing scenery and starts were preserved during the final
  Ready-zone placement. Reports and verified backups are on E:.

Final runtime report:
`E:/CodexWork/MessControlHub_20261019/Solo_06/Report.json`.
Videos are generated under
`E:/CodexWork/MessControlHub_20261019/Previews/` from the actual recorded frames.
The previews are silent sampled captures, with documented loading cuts.

Network peers, Steam room travel, late joins/disconnects in a live room,
physical pointer delivery, and a cooked Windows package remain untested in
this batch. Replicated rules and cook references are implemented; the solo
results do not establish multiplayer or packaged-game behavior.

## Basketball in Hub (2026-10-21)

The existing decorative `Sphere` is now `Hub_Basketball`, an
`AMCHubBasketball` at the same position with its original mesh/material and
50 cm radius. E picks it up within 220 cm with a clear line of sight; it stays
held after releasing the key. E again drops it. Left mouse or Q throws toward
the center reticle. On a gamepad, X picks up/drops and RT/RB or Y throws.
Contextual controls and the reticle appear in the Hub HUD.

The root uses analytic sphere collision, CCD, a 0.62 kg mass, restitution 0.50,
friction, low air damping and backspin. The existing court/hoop mesh uses
two-sided triangle collision, preserving the opening and actual rim/backboard.
Aiming through the opening solves a descending ballistic arc to that plane;
the released ball remains a free Chaos body, including misses and rebounds.
Character push forces are excluded. A ball lost below/outside the Hub returns
to its saved position. Holding/release and movement have replication support;
this change was checked in solo only.

`Tools/Unreal/Hub/author_basketball.py` authors the prop and collision, backing
up and saving only the two target packages. It preserves designer transforms
and stores the rim center relative to its hoop actor. The short runtime probe
is `check_basketball_solo.py`. Editor compilation and the solo check passed:
pickup/release/re-pickup, following movement, primary/Q throw, passing through
the rim, backboard/rim rebounds and successive floor bounces. With the initial
0.78 restitution setting, observed floor
velocity ratios were approximately 0.79, 0.78 and 0.79. No multiplayer or package
validation was run. Reports and verified originals are in
`Saved/HubBasketball_20261021`; pre-existing unsaved Hub and ground edits were
saved and separately backed up before implementation.

The follow-up reuses the food carry hand pose for the basketball and applies
the same precise-wrist physics mask; tools stay hidden while holding it.
The throw speed ceiling is now 2100 cm/s (previously 1300). Aimed rim shots use
a descending arc with a bounded apex instead of the very tall full-speed high
arc on short shots. Other aimed throws use the increased speed.

A white dashed trajectory updates while holding the ball and aiming with the
right mouse button (gamepad LB), ending at the first
predicted sphere collision. It uses the same launch velocity as the actual
throw, with gravity and the ball's linear damping. The owned actor RPC sends
that velocity and validates it against the held state and speed limit.
Releasing the ball clears the trajectory. Follow-up backups and QA reports
are under `Saved/HubBasketball_20261021/TrajectoryUpdate`.

Follow-up validation passed: Editor compilation, both food-hand pose targets,
hidden tools, matching planned/actual launch velocities, and real primary/Q
shots through the rim from approximately 10.0 and 17.3 m. The preview's spatial
deviation from the actual free flight stayed below 1.2 cm in the measured
pre-contact samples. The white dashed arc was reviewed in the standalone PIE
window. The check stopped cleanly, restored temporary play/performance settings
and the editor camera, and left no dirty packages. Multiplayer remains untested.

Aim is now held on right mouse (LB on gamepad): the crosshair and trajectory
appear on press and disappear on release or dropping/throwing the ball. This
input takes priority over ordinary bracing only while holding the basketball.
Restitution was reduced from 0.78 to 0.50, reducing rebound height by about 60%
for an equal impact. Throw strength and in-flight damping are unchanged.
The focused solo check passed RMB press/release, repeated aiming, drop cleanup
and reticle visibility. Measured floor rebound velocity ratios were 0.505 and
0.493. Editor compilation passed; the editor returned to Hub with temporary
settings restored and no dirty packages. Report: `Saved/HubBasketball_20261021/AimAndBounce/solo_report.json`.
