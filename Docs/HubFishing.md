# Hub fishing rod

The supplied `SM_fishrod_high.fbx` is imported as three independent meshes in
`/Game/Hub/Fishing`: the rod, reel and lure. Its existing base color, normal and
packed occlusion/roughness/metallic textures are connected to `M_FishingRod`.
The source FBX is preserved. The line is generated at runtime.

`UMCFishingRodComponent` belongs to the player character and presents the rod
only in the Hub. Players spawn with the rod stowed. Enter an enabled
`AMCHubFishingZone` on the pier and press **E** to equip it. Leaving the zone,
pressing **5**, or selecting an ordinary tool stows it and ends the attempt.
The rod temporarily disappears while
swimming, climbing, carrying another object, using a chest, emoting or ragdolling.
The existing basketball interaction has priority over reeling near the ball.

After pulling the native fishing changes, rebuild `MessControlEditor` before
testing. A pre-fishing DLL has neither the component nor its Five binding; the
log then reports `MCFishingRodComponent ... does not exist in memory`. The
current integration also uses the specific `FishingSimulationStep` constant
name so unity builds do not collide with other source files' local `Step`
variables. The fixed simulation interval remains `1.f/120.f`.

The carry pose holds the handle with the right hand and the upper reel with the
left. Face the pond, hold **left mouse** to charge the cast, and release it to
throw. The vertical gauge fills as the desired range grows. **Right mouse**
can preview the lower fishing pose. Hold **E** to abandon a bite or reel in an
empty line. Walking ends the attempt and restores the carry pose and short
hanging line.

The charge reaches its cap after 1.25 seconds, with a requested range of 300
to 1,000 cm. If that point is blocked or dry, the cast uses the closest clear
water found along the same aim line.

The pier access box and pond water serve separate purposes. Move/rotate/scale
`AMCHubFishingZone` in the editor to match the walkable pier. The server checks
the pawn center in the actual oriented boxes every 0.1 seconds and at fishing
RPCs, using their union so overlapping boxes do not retrigger equipment.
Initial overlap, teleport and hub recovery are handled without stored overlap
counters. An uninterruptible tool action delays automatic equipping until tool
switching is safe. Clients receive the authoritative access/equipment state.

`Tools/Unreal/Hub/author_hub_fishing_zone.py` prepares the box from an explicitly
selected pier actor. Its default plan is read-only; apply backs up the Hub on
E:, preserves existing designer placement, and saves only the Hub map.
The reports below describe earlier versions of the pier and fishing controls.

The existing `Hub_SwimmingWater` PhysicsVolume is tagged `FishingPond`. Casts
require this tagged water, actual open water below the destination, and a clear
arc. The volume also covers dry banks, so footprint overlap alone cannot permit
a cast. Facing land or standing too far from the pond rejects both the cast and
the fishing-ready pose. Validation runs on the server; the HUD shows when the
player needs to approach the pond or turn toward its water.

The line uses a fixed-step Verlet chain, anchored through the three mesh guide
rings, with gravity, constraints, damping and a swept collision check at the
lure. A validated ballistic arc drives the lure during flight and holds it at
the pond surface after landing. Its length increases during the cast and
decreases during reeling. The
artist's rod geometry is copied to a procedural component so its tip can bend
under a damped spring driven by movement and line direction. The base stays
rigid. The original static mesh, UVs and material are preserved.

After the lure settles, the server chooses a fresh delay of **4–9 seconds**.
The bite starts a challenge in the vertical gauge. Each click raises the hook,
which falls under gravity between clicks. The target band moves and narrows.
It turns green while the hook is inside it, where catch progress builds.
Progress drains outside the band. Rarer catches use narrower, faster bands
and require more time inside. The fish escapes if the player fails to fill
the progress meter within 10 seconds.

A successful challenge commits the catch and starts automatic retrieval. The
existing catch card and fish wallet update when the catch is collected. An
escaped or abandoned bite awards nothing. An interruption can collect an
already committed catch once. The existing Hub profile preserves the fish
wallet (`AMCPlayerState::FishCaught`) across pawn recovery and sessions.

The Hub readiness barrier prepares the rod, line and catch material shader
maps before their first visible use in Editor. The meter uses Slate geometry.

The component exposes carry/fishing transforms, both grip points, hanging length,
maximum line length, bite delay bounds, fish mesh and fish material for tuning.

Authoring and verification evidence is in `Saved/HubFishing_20261022`, including
the initial editor state, mesh bounds, source/package backups and the solo PIE
report. Only a completed report establishes a passed check. No multiplayer,
cook or packaged game validation is implied.

## Verification on 2026-10-22

The Development Win64 Editor compile and 22 checks in a short standalone PIE
session passed. The check exercised the actual native primary/brace/handle input
callbacks, walking and turning, both grips, cast payout, spring bending, reeling,
basketball pickup/release and tool switching. During the walking check a 90 cm
line stayed within its length limit; the cast paid out approximately 572 cm.
The check uses native callbacks rather than injected hardware input. PIE stopped,
the editor camera and temporary play/performance settings were restored, all
eight fishing assets were saved, and no dirty packages remained.

The current pond restrictions are verified separately in
`Saved/HubFishing_20261022/PondRules/solo_report.json` by
`Tools/Unreal/Hub/check_fishing_pond_solo.py`. The original
`check_fishing_rod_solo.py` entry point now runs this updated check. It tests dry
ground and landward rejection, an actual pond landing, walking recovery and the
reversed reel rotation, then restores the temporary play/performance settings.
The Editor compile and all 25 focused pond checks passed on 2026-10-22. The lure
landed at the real pond surface; carry grip error after walking was below 0.01 cm,
and a 0.1-second reeling sample confirmed the reversed rotation. No multiplayer
or packaged-game check was run.

The subsequent catch check, `Tools/Unreal/Hub/check_fishing_catches_solo.py`, passed
two catches with the normal 4–9 second bounds, automatic visible cube retrieval,
one award per catch, HUD updates to 1 then 2, and count retention after actual Hub
pawn recovery. Cancellation and empty reeling awarded no fish. Sampled bite
delays were 5.12 and 7.65 seconds. The counter was visually reviewed in the game
window. The completed report, screenshot and backups are in
`Saved/HubFishing_20261022/Catches`. The Editor compile passed; multiplayer and
packaging were not run.

## Automatic pier access verification — 2026-10-22

The saved `Hub_PierFishingZone` follows the actual `Cube_006_003` pier mesh;
the older `SM_pirs` level actor has no mesh. Spawned characters start with the
rod stowed. The GameMode and owning camera face the nearest enabled Ready zone.
The movable access box is independent of pond cast validation.

The Editor compile and 19 focused standalone checks passed in
`E:/CodexWork/HubWardrobeRewards_20261022/PierSolo01/Report.json`. The player
walked onto/off the real pier with normal movement input. Entry equipped the
rod; exit stowed it, cleared fishing state, and blocked Five outside. Five and
the regular tool action worked inside; reentry equipped the rod again.
Input actions were injected through Enhanced Input, rather than physical keys.
The source gameplay check did not teleport the player or bypass zone checks.

Five videos and a contact sheet are in `Artifacts/HubPierFishing_20261022`.
Capture cadence was approximately 6 Hz; encoded 30 FPS repeats these native
frames without interpolation. This check does not repeat a catch, multiplayer,
cook/package, or a full level. The initial camera pitch was subsequently
adjusted from -25 to -12 degrees for a clearer view of Ready; the yaw and access
logic are unchanged.
