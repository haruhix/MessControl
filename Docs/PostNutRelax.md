# Second Relax between nuts and jellies

The approved handoff is completion-driven:

1. Finish the nut encounter.
2. Serve five whole ordinary foods, then eight, then nine. Each portion must
   finish its food processing, transport/swallowing and unresolved food hazards
   before the next portion starts.
3. After the final portion, finish all remaining food work and brush every
   remaining floor/tongue stain and available dirty arena tooth.
4. Drop a small mint onto the tongue. Its first cold front starts three to five
   game seconds after actual landing.
5. The mint repeats fronts until players break it with pickaxes.
6. Start the authored jelly key event after mint destruction.

There is no coffee between these portions and no randomly scheduled support
hazard during this Relax or its mint encounter. The existing opening Relax
remains three/five/eight with its own coffee sequence; see `FirstRelax.md`.
Counts describe whole ingredients for the team, not DataTable fragment counts.
The existing weighted ordinary-food menu chooses what to serve.

Intermediate portions may leave dirt for later brushing. The final barrier
checks global outstanding food, carried fragments and food hazards, both
ordinary and painted floor stains, and dirty available arena teeth. It does
not limit cleanup to this meal's batch. Dirt on player avatars is not an
arena-cleanup completion condition. Existing admission limits still apply:
six simultaneous whole foods in solo, increasing with workers, and the
remaining authored foods are issued as processing makes space.

## Profile settings and compatibility

The run profile is `/Game/Gameplay/CoreLoop/DA_SingleDay`:

- `AfterNutMealPortions`: `[5, 8, 9]`.
- `AfterNutMealDropSeconds`: `1` by default, between individual food drops.
- `SmallMint`: the mint settings described below.
- `KeyEvents`: the native default order is NutEncounter, Jellies, IceEvent,
  FogBrawl. Existing event IDs, variants and completion rewards remain authored.

The second Relax is inserted only when the completed authored slot is
NutEncounter and the immediately following slot is Jellies, with
`bLegacyTimedFinale` disabled. Custom sequences with another next event retain
their ordinary handoff. Legacy timed-finale profiles do not enter this new
path. The old one/two-slot prototype extensions are separate compatibility
behavior; this change does not silently insert jellies into every custom run.
Saved profile contents must be inspected/applied in the matching Editor build;
native defaults alone are not evidence that an older saved profile was updated.

The saved default profile was updated on 2026-10-20 to NutEncounter, Jellies,
IceEvent, FogBrawl. Its old saved Nut-only form previously gained Ice/Fog in
the runtime copy. Authoring now materializes that existing compatibility tail
before inserting Jellies, preserving the original row, tuning and variants.
The source package backup and field-preservation report are retained under
`E:/CodexWork/MessControlNextRelax_20261020/Authoring/` (Combined02 and Final06).

`PostNutMeal`, `PostNutMint` and `Jellies` append new stage values; existing stage
numeric identities are preserved. `Jellies` also appends to the key-event kind.
The current portion, serving target, foods issued, remaining cleanup and mint
health are published through the existing replicated director/HUD state.

## Small mint behavior

`AMCSmallMintCandy` reuses the existing winter-candy mesh at a default diameter
of 135 cm. It falls from 850 cm over 1.4 seconds onto a queried safe tongue
surface. Its default health is 180 per living controlled worker, counted when
it starts. It has no timed victory condition.

The first deadline uses `FirstWaveMinSeconds`/`FirstWaveMaxSeconds`, default
three/five seconds from `LandedAt`, not from the start of the fall. Later
deadlines use the previous emission time plus `WaveTravelSeconds` and a random
`WaveRestMinSeconds`/`WaveRestMaxSeconds`. Defaults are 2.8 seconds of travel and
three to five seconds of recovery. Default maximum radius is 2600 cm.

Each emitted `AMCHazardWave` uses the new leg-freeze payload:

- Grounded feet receive `FootIceHealth`, default 80.
- The front deals no health damage, knockback or ragdoll impulse.
- The existing ground-wave escape check requires a falling character whose
  capsule sole clears the actual floor by more than 12 cm. It uses the capsule,
  not animated body bobbing. Landing behind an already crossed front does not
  cause a delayed hit from that same front.
- Frozen legs stop locomotion while retaining tool use. Ordinary timed pickaxe
  swings can free the player's own feet or a reachable ally through the
  existing rescue mechanics. Another front can freeze a rescued player again.
- Normal pickaxe contacts damage the active mint. Breaking it ends the attack,
  removes its owned fronts and clears leg ice on the workers recorded as hit
  by its fronts. Cancel/EndPlay performs the same owned-front cleanup.

The existing candy mesh, shard mesh and ice-shatter effect are reused. The
travelling front now uses isolated Frost Nova materials and a two-emitter
Niagara release; its research and visual iterations are in `VFXReferences.md`.
No new imported mesh or animation is required. The director stops
and removes its own post-nut food/dirt batch, mint and jelly event when the run
is stopped. Normal mint completion dispatches the next authored Jellies slot;
the existing jelly event owns its subsequent enemy and cleanup completion.

## Validation status

Source implementation and three focused tests are prepared in
`Source/MessControl/Private/Tests/MCPostNutRelaxTests.cpp`, under
`MessControl.SingleDay.PostNutRelax`:

- Preservation of opening three/five/eight and the new five/eight/nine defaults.
- Actual food issuance and carried-food/floor/available-tooth gates before mint.
- Actual mint landing/pulse deadlines, repeated swept fronts, jumping, unchanged
  health/no knockdown, normal timed pickaxe self-rescue and destruction cleanup.

The combined Editor build passed. All three PostNutRelax tests passed again
after the held-pickaxe fix, along with the four loading-manifest and thirteen
selected existing SingleDay.Sequence tests (20 successful test records).
Evidence: `E:/CodexWork/MessControlResourceLoading_20261020/Tests/20261020_043031/Report/index.json`.

The self-rescue regression now uses one held-input edge, real character ticks
and ordinary contact timers. It verifies multiple confirmed contacts, zero
remaining foot ice, and no further combat swings while still holding after
rescue. The production fix permits automatic repeats only for chopping food or
breaking one's own frozen feet; ordinary combat retains press-edge input.

The controlled automation fixtures supply completed-combat/food-removal signals
and advance the engine clock; they are not a full player playthrough.

The final rendering solo fixture is
`E:/CodexWork/MessControlNextRelax_20261020/SoloMint06/Report.json` (12 checks,
113 original window frames). It waits for the real preload and ordinary first
perk, then isolates one mint in L_Mouth by stopping only the sequence actor's
runtime tick. It keeps the real clock, saved settings, health, wave schedule,
range and normal character camera. Public ordinary input callbacks perform
held-LMB pickaxe self-rescue, a successful jump (measured sole clearance about
104 cm), and five ordinary timed pickaxe contacts to break the mint. It verifies
repeat pulses, no health damage, source/front cleanup and no resumed attack.
The owned PIE leaves no dirty packages and its Editor exits with native code 0.

The final visual preview is under
`E:/CodexWork/MessControlNextRelax_20261020/Previews/FinalMint03/`. It shows the
mint fixture and VFX/tool behavior, not a manually completed 5/8/9 meal run.
Sampled PNGs use their recorded wall timing for the video. Physical keyboard/
mouse delivery, audio quality, multiplayer and cook/package are not validated.
