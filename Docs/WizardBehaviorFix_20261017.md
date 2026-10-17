# Wizard walking, rain and teleport correction — 2026-10-17

Walking retains the exact existing MCStyleV6 sequence and its legs/body.
The native animation proxy layers the current idle pose onto both shoulder
subtrees, including twists, wrists and fingers. The layer is weighted only by
walking contribution; spells, damage reactions and death keep their authored
arms. No walking, idle, mesh or skeleton package is rewritten.

The supplied rain action now runs forward once from the beginning of its
windup through its entire 3.266667 s source. It then blends for 0.25 s into the
live idle loop while the existing four-second gameplay storm continues.
The previous deliberately frozen release pose is removed. Actual nut drops,
warnings and damage retain their native clock. Recovery no longer replays the
source exit after the whole animation has already finished.

A successful teleport plays its full exit and blend, then starts a complete
fire-nut cast. This follow-up takes priority over ready rain, summon, melee and
queued hit reactions. The fireball still uses its normal windup, projectile,
damage and subsequent cooldown. A failed destination does not invent a blink.
Scheduled fire-nuts also begin with teleport, waiting for its cooldown when
necessary; the normal AI does not cast fire-nut before this movement.

Teleport distance is now 1200–2000 cm (previously 600–1100). Native defaults,
sanitization fallbacks and the two saved `DA_NutRain` distances agree. The
existing footprint, collision and occupied-destination validation remains.
All other profile tuning, clip bindings, the supplied Blender source and
existing character assets are preserved.

The normal Editor build passed in 57.68 s; the final strict fire-nut scheduling
update also compiled normally in 6.17 s. Three focused native tests passed on
the final code:

- `MessControl.CoreLoop.NutBoss.Animation.WizardRainReturnsToLiveIdle`
- `MessControl.CoreLoop.NutBoss.Animation.WizardArtistFullClips`
- `MessControl.CoreLoop.NutBoss.WizardPressureWaitsForFullCastAndTurnsSmoothly`

The first exercises the final rain source frame, quarter-second blend and a
fully weighted looping idle during the active storm. The combat test checks
the larger displacement and fire-nut priority without cutting teleport short.
The animation fixtures intentionally use a renderless transient mesh; the
combat fixture uses the engine Cube. Their existing render-data/CPU-access
warnings are retained in the Automation report.

All 11 solo gameplay checks passed. The maximum runtime walking-arm difference
from idle was 0.091 degrees; legs retained the original walk within 0.015 degrees.
During the active storm, 32 sampled full-body poses matched idle within 0.094
degrees, and head movement spanned 5.69 cm, proving it remained live rather than
frozen. Three real Pickaxe hits triggered a 1879.63 cm teleport. Fire-nut started
3.60 s after the teleport began, after its complete performance and blend; it
spawned a real projectile and fully completed in another 2.57 s.

Evidence is in `Artifacts/WizardBehaviorFix_20261017`. The genuine preview shows
walking and rain only: the follow camera after the farther teleport is occluded
by the mouth wall, so those frames are omitted rather than presented as a clear
fire-nut review. Native/solo telemetry validates the teleport/fire-nut order,
real projectile and distance. The walking/rain preview repeats 152 genuine
15 Hz frames at 30 fps with the recorded game-clock spacing; complete decoding
passed, without generated/interpolated motion. A second capture was made solely
to improve framing; functional checks passed in both captures.

Existing wizard asset hashes, source Blender bytes, walking binding and all
other profile settings were checked. Only the two saved teleport distances
changed. The editor remains on the original arena with its viewport, selection
and background-tick preference restored. No multiplayer or packaged build is
implied.
