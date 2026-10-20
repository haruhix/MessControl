# First Relax before the nut encounter

The approved opening is a completion-driven meal sequence for the whole team:

1. Serve three whole ordinary foods.
2. When that portion's food, carried fragments, swallowing and food hazards are
   resolved, run one existing ordinary coffee wave.
3. After the wave finishes, wait a director-selected five to ten game seconds.
4. Serve five foods, finish their food work, then repeat coffee and the pause.
5. Serve eight foods. Finish all remaining food work and clean the tongue,
   floor stains and available arena teeth before the existing pre-nut pause.

The first food arrives before any coffee. A drink never overlaps new serving.
Coffee and meal dirt remain available for brushing between portions. Dirt does
not block the second or third portion; it blocks completion of the full Relax.
Unrelated dirt on player avatars is not an arena-cleanup completion condition.
Personal perk choices retain the existing barrier before the nut event starts.

The food pool and its selection weights remain in the existing meal menu. The
director randomly chooses eligible ordinary food; the portion count is a count
of whole products, not the number of fragments from their DataTable rows.
Existing chopping, transport, throw, sound and coffee visuals are reused.

## Designer settings

Edit `/Game/Gameplay/CoreLoop/DA_SingleDay`:

- `OpeningMealPortions`: `[3, 5, 8]` for the approved sequence.
- `BetweenMealPauseMinSeconds`: `5`.
- `BetweenMealPauseMaxSeconds`: `10`.
- `OpeningMealDropSeconds`: the existing serving interval within a portion.
- `BeforeNutsPauseSeconds`: the existing short pause after the final cleanup.

All three portions and both drinks retain one Relax batch. The director exposes
the current portion number, count, whole-food target and foods served to clients.
If coffee has filled the safe stain field, serving may reuse an existing dirty
surface tagged by the same food/coffee batch. It preserves that surface's mask,
owner and cleanup debt; lack of room for a separate stain cannot cancel food.
With no such real dirty surface, the existing placement-failure guard remains.
`MealCoffeeWave` and `BetweenMealsPause` are appended to the existing stage enum
so previously serialized stage values keep their numeric identities.

Legacy opening-wave and single-portion properties keep their serialized names
for compatibility. The new opening path uses the portion array and drink pauses.
The coffee actor uses the existing ordinary wave path; no raft or jelly event is
launched as a substitute for the drink.

## Authoring and validation

`Tools/Unreal/author_first_relax.py` defaults to a read-only plan. With a matching
Editor build, explicit `run('apply')` backs up and changes only the three approved
fields in `DA_SingleDay`, verifies other reflected designer properties, and saves
that profile alone. Existing generic single-day authoring also uses these fields.

## Validation on 2026-10-20

- The Development Win64 Editor build passed.
- Three focused opening tests passed: outstanding-food explanations, available
  dirty-teeth gating, and portion/coffee/pause/shared-cleanup transitions.
- The transition fixture uses short first and second portions and an eight-food
  final portion. It exercises the actual six-whole-food solo admission limit,
  retained fragments, two real coffee actors, the post-wave five-to-ten-second
  deadlines, remaining global dirt and owned-batch cleanup on stop. Food
  completion and clock advancement are controlled test inputs, not a recording
  of a full player playthrough.

- The saved profile exposes `[3, 5, 8]` and `5`/`10` seconds. Its verified backup
  and saved package hashes match: the new fields already inherit the approved
  native defaults, so saving was idempotent. All other designer fields remain
  identical; no unrelated Content package changed.
- The final normal-start solo check passed nine assertions: the ordinary first
  perk, exactly three naturally issued whole foods in the same batch, real HUD
  `0 / 3`, and ten game seconds waiting without duplicate food or early coffee.
  No food, stage, mask or profile was overridden. Existing orbit and zoom input
  handlers supplied the high gameplay-camera view. The probe retains the normal
  project time step; forcing 60-Hz simulation while taking PNGs is unnecessary
  and can slow game time relative to the capture process's wall-clock timeout.
- The owned PIE stopped, temporary settings and the original editor map were
  restored, and no unsaved packages remained. The 89 recorded PNG frames are
  sampled captures, not a multiplayer, performance or audio recording.

Reports and preview outputs are under
`E:/CodexWork/MessControlFirstRelax_20261020/`:
`FocusedTests_LastRun.json`, `Authoring/20261019T172211Z_59f623dd/Report.json`,
`Solo03/Report.json`, and `Previews/`. The preview shows the start of the first
three-food portion; it does not depict a full manually completed 3/5/8 run.

Live multiplayer and cook/package validation were not run in this task.
The legacy `probe_cleanup_first_relax_solo.py` assumes an opening coffee wave
and is not a validation of the new three-portion flow. Use the new
`check_first_relax_solo.py` for the untouched normal-start smoke instead.
