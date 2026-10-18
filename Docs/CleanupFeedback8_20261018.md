# Feedback 8: cleanup completion and visible dirt

Integrated in the Editor, with accepted central-placement solo footage and
successful focused native checks, including the final overhanging-test retry.

## Diagnosis

The first meal uses `AMCSingleDayDirector::TickFirstMeal` and
`MCCountDirectedFoodDirt(World, FirstMealBatch)`. That gate checks every dirty
`AMCMouthSurface` carrying the batch and `MCFoodServedDirt` tag. Both the ordinary
seeded puddle and a painted wave canvas participate. It does not limit the tooth
count to eight and does not read tooth cleanliness for this particular gate.

There is a concrete unmatched coverage path: coffee-wave accumulation retains
unsupported side cells for coating neighboring teeth. `DepositResidue` could copy
their wet coverage into a floor paint canvas. Its visual mesh is clipped to real
upward tongue triangles, but its `IsClean()` formerly counted every wet canvas
byte, including areas outside those triangles. Such invisible bytes could keep
the meal dirt gate open. The supported-mask regression and actual L_Mouth
first-meal cleanup now provide the bounded evidence below.

## Change

Paint deposition now clips texel centers to the same upward tissue triangles as
the visual mesh. Supported dirt remains brushable and counted; unsupported paint
is rejected before it becomes task debt. The cache is built by triangle
rasterization, with no collision rays or cooks; ordinary Z-only breathing does
not invalidate it. The source's monotonic ledger still prevents an old wave from
refilling a cleaned stroke.

Future meal and wave puddles now prefer the open central tongue. Shared
`MCDirtPlacement` retains the gameplay X bands and the central 58% of each
half-width, with a 65 cm brush approach. Its footprint query requires supported
tissue and clear static space above the whole patch. Meal dirt no longer follows
a worker standing at a risky edge; a bounded size fallback searches safe space
without inventing a completed task. Wave footprints are planned centrally before
arrival, while the existing swept-front accumulation and tooth coating remain
unchanged. The paint support cache also excludes the outer work-area strips.
Existing stains are not teleported to new positions.

Legacy cleanup counts a surface using `IsClean()`, matching the first meal and
adaptive Director, instead of relying on the compatibility care counter. The HUD
uses actual remaining surface coverage, excludes dead actors from tooth dirt,
and reserves 100% for a truly clean scene. Remaining coverage is cached until a
mask write, avoiding a new 64x64 scan every HUD update.

No native `MCArenaToothGrime` fallback was changed. Empty tooth projections need
actual arena evidence before changing the brush or silently cancelling a tooth
task. Physical-tooth dirt is counted independently of the reserve HUD's layout.

## Focused regression names

- `MessControl.Cleanup.OverhangingWavePaintHasNoInvisibleDebt`: an overhanging
  wave canvas receives supported paint, keeps a real cleaning task, is erased
  through real brush acquisition/time/contact, then releases the exact
  `MCCountDirectedFoodDirt` gate; a passed source cannot refill it.
- `MessControl.Cleanup.BothStainKindsAndTwelveTeethAreObserved`: both floor types
  remain counted even when the compatibility counter is stale, and dirty arena
  teeth 9–12 participate independently of HUD presentation.
- `MessControl.Cleanup.CentralDirtRejectsRimsAndStaticCover`: open central tissue
  accepts a footprint, while the risky rim and an actual static covering box
  reject it; removing the box restores the same valid placement.

The central-placement native checks passed the two-kind/twelve-tooth,
static-cover and jelly regressions; the final overhanging-test retry also
passed. Its observer is at X-40, facing the 448 retained central cells from the
open side. Every retained cell is within 113.09 cm of a 210 cm brush reach.
Approach delay, actual contact, mask change, completion, gate release and
no-refill assertions are retained; no runtime rule was relaxed for the fixture.

Also retain `MessControl.Liquid.PaintTileRealBrush` as the contact/continuous
stroke regression if the focused checks reveal a contact failure.

## Solo footage for feedback 8

1. Use one standalone PIE on saved L_Mouth; do not run multiplayer automatically.
2. Let the opening coffee wave pass and the first meal finish spawning. Keep the
   HUD, current task and cleaning counter visible in the recording.
3. Before cleaning, record all batch-tagged surfaces with their batch, kind,
   world location, wet byte count, care counter, visible liquid mesh section
   count and count of wet centers without tongue support. Keep a report under
   `Saved/Feedback5_11_20261018/Cleanup8/` on E:.
4. Use actual held brush input to erase one ordinary puddle and a wave-painted
   patch. Show a close shot of the bristles removing the last coverage, then the
   actual task transition after every food actor/hazard and tagged stain is
   resolved. Do not force `TasksLeft`, clear masks, destroy stains or advance the
   stage to make the video pass.
5. Verify the snapshot includes all twelve physical teeth. If any tooth still
   needs coffee care with zero visible/coatable geometry, capture that tooth ID
   and defer accepting the overall cleanup until it is explained.
6. Capture a short final shot showing no hidden dirt debt and the next phase.
   State separately that first-meal tooth dirt is not itself part of its gate;
   the adaptive/legacy cleanup and the mouth-cleanliness HUD still include it.

Suggested deliverable: `08_cleanup_completion.mp4` plus the runtime snapshot and
test report. The accepted footage and its evidence are recorded below. No performance measurement is claimed.

### Prepared short first-meal video fixture

`Tools/Unreal/probe_cleanup_first_relax_solo.py` owns one standalone PIE and
observes the real `TickFirstMeal` path through its opening wave, issued food,
brush debt and automatic `FirstMeal -> MealRest` transition. It tunes only the
Director's private PIE run-profile copy to one portion and short pauses. The
naturally issued portion is broken by ordinary tool input; actual E collection
and held Q resolve its native fragments. No stage/task/care/mask value is set,
and no dirt actor is destroyed to produce completion.

Two camera clips show the ordinary brush reducing the actual mask
of each native surface kind. The existing F3 MeshaBrush reward then bounds the
remaining sweep, still using real held brush input and bristle validation.
The final clip shows the last visible residue and the native transition.
Position-only observer fixtures, the one-portion profile and the later brush
upgrade are recorded explicitly, so this does not claim default difficulty or
full-run timing. Read-only debt snapshots include all twelve physical teeth
and unsupported wet-mask/visible-mesh checks before and after cleaning.

The global `MCCountDirectedFoodDirt` function is not reflected to Python. Its
actual native caller and resulting stage transition provide gameplay evidence;
the focused C++ regression supplies the separate direct function assertion.
Reports and frames stay under `Saved/Feedback5_11_20261018/Cleanup8/`.
Current accepted solo run:
`E:/DEVGAME/MessControl/Saved/Feedback5_11_20261018/Cleanup8/Runs/20261018T083313Z_c312dd32/Report.json`.
Both recorded placement snapshots contain nine central floor stains, 6761 wet
texels and zero wet texels outside the permitted work area. Native tool input
produced ten fragments; E collection and real held Q delivered all ten
(`FoodSwallowed` 0 -> 10). The ordinary brush reduced the ordinary mask by
126328 byte units and the wave-painted mask by 107527, with native contact for
each. The disclosed later F3 brush upgrade finished the remaining residue.
The actual Director transitioned `FIRST_MEAL -> MEAL_REST`, with native
TasksLeft observed from 10 to 0; no stage/task/mask assignment produced that
transition.

Lead review accepted the current genuine frames and decoded videos. Placement,
native food cutting, both brush types and final completion are encoded under
`E:/DEVGAME/MessControl/Saved/Feedback5_11_20261018/Previews/CentralDirt/Cleanup/`.
The exact source clips and checksums are retained in `08_ExportManifest.json`;
the final clip is `08_08_cleanup_completion.mp4` and the placement clip is
`08_08_central_floor_placement.mp4`.

This proves the first-meal floor-cleanup gate, not 100% overall mouth cleanliness:
all twelve physical teeth were observed, they retain visible dirt, and the
final overall cleanliness HUD reads 41%. The earlier accepted run
`20261018T075522Z_00586cdc` remains historical evidence before central placement;
its footage does not validate the current layout. Network, packaged-client
parity and performance were not validated by these captures.

### Later spacing correction: layout-only follow-up

The user then requested a wider, less predictable layout. The safe work width
is now 72% rather than 58%; full footprint, 65 cm brush clearance and actual
tissue/static checks remain. Wave stains use continuous random XY instead of
nearest-centre sorted grid origins; ordinary stains no longer average samples
toward the centre. The wet-front/deposition and cleanup rules are unchanged.
The matching Editor build passed (`Build/Console_Scatter.log`).

Two genuine layout-only runs, `DirtScatter/Runs/20261018T085450Z_74f4d2b0` and
`DirtScatter/Runs/20261018T085550Z_83268ffb`, each completed with
`status=subset_passed`, `passed=null`, `cleanup_passed=null` and 23 PNGs. Both
show nine naturally generated stains. Their census tested 7139/7382 actual wet
texels with zero outside, zero ordinary corners outside and zero ordinary
static footprint/approach blocks. Lead review accepted the wider irregular
layouts. Different observer camera sides were chosen; this is not a fixed-view
A/B or a statistical randomness/performance measurement. The full cleanup
evidence above predates this placement-only correction and was not rerun.

Portable reviewed previews: [Feedback 5–11 video bundle](../Artifacts/Feedback5_11_20261018/README.md). Raw capture files and full logs remain local under Saved/.
