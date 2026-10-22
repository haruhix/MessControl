# Hub fishing rod

The supplied `SM_fishrod_high.fbx` is imported as three independent meshes in
`/Game/Hub/Fishing`: the rod, reel and lure. Its existing base color, normal and
packed occlusion/roughness/metallic textures are connected to `M_FishingRod`.
The source FBX is preserved. The line is generated at runtime.

`UMCFishingRodComponent` belongs to the player character and presents the rod
only in the Hub. The default is equipped. Key **5** toggles it; selecting an
ordinary tool with **1–4** stows it. The rod temporarily disappears while
swimming, climbing, carrying another object, using a chest, emoting or ragdolling.
The existing basketball interaction has priority over reeling near the ball.

After pulling the native fishing changes, rebuild `MessControlEditor` before
testing. A pre-fishing DLL has neither the component nor its Five binding; the
log then reports `MCFishingRodComponent ... does not exist in memory`. The
current integration also uses the specific `FishingSimulationStep` constant
name so unity builds do not collide with other source files' local `Step`
variables. The fixed simulation interval remains `1.f/120.f`.

The resting/walking pose holds the handle with the right hand and the upper reel
with the left, matching the supplied references. Near the pond and facing its
water, **right mouse** blends into the lower fishing pose and **left mouse**
casts into the pond. Holding **E** reels in the line and rotates the independent
reel mesh in the corrected direction. Starting to walk cancels fishing and
restores the carry pose and short hanging line. Finishing reeling also clears
the fishing pose.

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

After the lure settles in the pond, the server chooses a fresh delay of **4–9
seconds**. A bite hooks a blue cube placeholder and automatically reels it in.
Near the rod, the cube moves toward the character and is picked up. Only then
does the personal **Рыба: N** HUD counter increase by one. Each cast awards at
most one fish; another cast is needed for the next catch.

Walking away, stowing the rod or losing the valid fishing state cancels the
pending catch. Reeling before a bite returns an empty lure without a reward.
`AMCPlayerState::FishCaught` is replicated, survives pawn recovery and is copied
with the PlayerState during travel. It lasts for the current session; no disk
save, fish species, economy or full fish mesh is included.

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
