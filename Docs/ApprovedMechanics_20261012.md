# Approved mechanics — 2026-10-12

The user approved this scope before implementation. Current source and saved
Unreal packages are authoritative. Existing unrelated work is preserved.

1. Separate tutorial map. Host checkbox **Начать с обучения** chooses tutorial
   first or direct normal play. Tutorial completion travels to the normal map.
2. Normal gameplay uses its own map, `/Game/Maps/L_Mouth`.
3. Director chooses nearest-player attacks or a deliberate temporary boss focus.
4. Watergun pushes players.
5. Timed jumps evade pain and frost ground waves.
6. Deactivate Buffer/perforator in tools and booster rewards; retain its assets.
7. Ordinary friendly attacks cause no HP damage. Chili is the explicit exception:
   its explosion damages every player and bosses in range.
8. Prevent food from spawning/landing in inaccessible zones; recover settled
   inaccessible food without interrupting held or airborne items.
9. Watergun tickles the uvula with a cosmetic animation.
10. Spoiled food emits visible stink. Nearby players react with facial disgust
    and, when hands are free, a short hand-near-nose gesture.
11. Larger cleaning stains appear near player activity, away from distant green
    delivery areas. F3 can spawn one huge shared cleaning stain on the tongue.
12. Spectator has an independent mouse orbit around the observed player.
13. Carry and throw chili. Its absolute fuse continues through carrying and
    throwing; a late throw can explode in air. Add a small explosion effect,
    knockback and a pain wave.
14. RMB drags any player gently; multiple draggers add their forces. Future
    transport of frozen players to warming zones remains a separate extension.
15. Restore small food fragments and the existing coin-style hand stack. Carry
    several fragments together into the correct delivery zone for immediate
    completion and one checkmark per hand stack, without suction. Each fragment
    still contributes its own delivery count and reward. Fragments do not block or knock
    down players; floor and scenery collision remains.
16. Crossing the open throat's actual aperture kills that player. Players must
    hold their position during the wave.
17. Animated central event alarms for cola, bosses and special/dangerous events,
    inspired by the supplied horizontal red Palworld warning banner.
18. During Frost Event, brushing ice underfoot creates a temporary warming
    patch. The patch refreezes over time and can be maintained by brushing.

## Validation scope

Use short local solo PIE checks and brief genuine gameplay videos where useful.
Network tests and Windows build/cook/package require a separate user request.
The user subsequently authorized a regular **MessControlEditor** build with
Unreal closed and continued solo PIE checks and videos. This authorization does
not include cook, packaging or network tests.

## Integration and validation

The 18 approved changes are integrated in native source and saved packages.
`/Game/Maps/L_Tutorial` is a separate saved map using the existing mouth arena;
`/Game/Maps/L_Mouth` remains the normal-play map. The frost material
`/Game/Gameplay/Cold/Frost/M_TongueFrost` now uses vertex alpha to reveal brushed
warming patches. Existing unrelated workspace changes are preserved.

### Changelog

| Task | Resulting behavior |
| --- | --- |
| 1 | The host's **Начать с обучения** checkbox routes to the tutorial or directly to normal play. Tutorial completion travels to the normal map. |
| 2 | Tutorial and normal play have separate map assets and gameplay entry paths. |
| 3 | The Director chooses a nearby eligible player or maintains a short deliberate boss focus; vulnerable players receive relief. |
| 4 | Watergun hits apply physical knockback to players. |
| 5 | Pain and frost ground-wave checks use actual clearance above the tongue; a successful jump evades the wave. |
| 6 | Buffer/perforator activation and tool/booster rewards are disabled. Its implementation and assets remain available for future work. |
| 7 | Ordinary player attacks retain physical effects but cause no teammate HP damage. Chili remains an explicit damage exception. |
| 8 | Food placement uses reachable tongue locations. Settled inaccessible food is recovered without interrupting held or airborne food. |
| 9 | Watergun contact triggers a short cosmetic uvula tickle animation. |
| 10 | Spoiled food has a visible looping smell effect. Nearby players blend into a disgusted expression and perform a procedural hand-near-nose gesture when their hands are free. |
| 11 | Normal stains are larger and placed near activity. F3 adds **Эксперимент — огромное пятно на языке** for a single shared tongue stain. |
| 12 | The spectator camera supports independent orbit and zoom around the observed player, with state restored after respawn. |
| 13 | Chili can be picked up and thrown while its original fuse keeps running. Detonation occurs at the current position, including in air, with a small burst effect, knockback and pain wave; players and bosses can take damage. |
| 14 | Holding RMB drags a player gently. Forces from multiple draggers add together. Frozen-player rescue transport remains future scope. |
| 15 | Cutting restores small fragments. The approved follow-up restores the existing coin-style hand stack and makes fragment contact harmless to players. Carrying the stack into the matching delivery zone completes its fragments immediately, with one checkmark for the stack and each piece still credited. See the follow-up validation below. |
| 16 | Entering the actual open throat aperture kills the player who crossed it; the wave still requires players to hold their position. |
| 17 | Central animated danger banners cover cola, bosses and special events, with queued display and duplicate suppression. |
| 18 | Brushing ice underfoot creates and maintains a warming patch; the patch shrinks as it refreezes after brushing stops. |

### Checks performed

- **Editor build passed:** `MessControlEditor Win64 Development`, 83 actions,
  352.12 seconds. Log: `Saved/Checks/ApprovedMechanicsEditorBuild.log`.
  This compiles the changed native implementation and test sources; no broad
  automation suite was run.
- **Tutorial transition and HUD passed:** actual tutorial-to-mouth travel,
  normal-map tutorial bypass, first-perk menu resolution and three queued HUD
  alarms. Report: `Saved/Checks/ApprovedFlowAndAlerts.json`.
- **Pain-wave jump passed:** standing took 12 HP; the timed native jump took
  0 HP, with maximum sole clearance 99.0 cm.
- **Chili passed:** real F3 chili, real pickup and throw 0.233 seconds before
  expiry, unchanged fuse deadline, native detonation 172.78 cm above the floor.
  Report: `Artifacts/ApprovedCombatSolo_1791762110/Combat.json`.
- **Environment passed:** brushing grew the patch to a 175 cm radius and made
  the player safe. Six seconds after release its radius was 123.74 cm. Spoiled
  food produced its attached smell effect, facial reaction and nose gesture.
  F3 spawned exactly one huge stain (1172.64 cm half-size), and real brushing
  cleaned 16 samples. Report: `Saved/Checks/ApprovedEnvironmentSolo/Report.json`.
- **Spectator passed:** native death entered spectator mode, public orbit input
  changed camera yaw by 31.82 degrees, zoom reduced target distance by 153.73 cm,
  and real respawn restored the view target. The observed player's camera was
  unchanged. Report: `Saved/Checks/ApprovedSpectatorSolo/Report.json`.
- **Frost nova jump passed:** the actual IceEvent legacy nova froze the standing
  player's legs. A timed native jump avoided freezing, including after landing;
  sole clearance at impact was 75.31 cm. Report:
  `Saved/Checks/ApprovedFrostJumpSolo/Report.json`. This check used
  `bUseAnimatedBossAttacks=false` only in PIE and scheduled the exposed nova
  warning/impact fields; actual IceEvent Tick executed `ResolveNova`.

### Fixture limits and deferred validation

These were short, isolated solo PIE checks. The travel check skipped tutorial
lessons by completing the tutorial fixture, then exercised actual travel. HUD
examples were explicitly queued; that verifies the widget and queue rather than
every event trigger. The normal-day scheduler was paused where needed to isolate
the mechanic. The frost-brush fixture temporarily extended unrelated attack
timings. Combat captures used an actual SceneCapture2D with mouth walls hidden
for readability; the jump and detonation were native gameplay. The spectator
target was a local bot, and input used the public orbit/zoom API, so hardware
mouse delivery was not tested. Fixture edits were confined to PIE.

Network behavior, cook and packaging were not tested. Full manual tutorial
lessons, host-session routing, every boss target choice, team dragging and damage
exceptions, broad food reachability/fragment disposal, uvula interaction, and
open-throat gameplay remain outside the completed runtime checks. Animated
radial frost-wave traversal also remains deferred; the completed frost check
covers the legacy nova branch. Their native changes compiled successfully.
This distinction applies even where a related
visual or solo check passed. The subsequent fragment-stack follow-up below
adds a passing isolated Egg pickup/delivery check, rather than a broad food sweep.

### Videos

Short clips use genuine Unreal frames and show HUD alarms, pain-wave jumping,
late chili detonation, frost nova jumping, frost brushing/refreezing, stink
reactions, the huge stain and spectator orbit. Files and capture limitations
are listed in
`Artifacts/ApprovedMechanics_20261012/README.md`.

## Fragment stack follow-up — 2026-10-12

Approved clarification: reuse the existing coin-style hand stack for fragments,
carry multiple pieces into the correct zone for immediate completion, and make
fragment contact harmless to players while retaining floor/scenery collision.
The subsequent approved clarification shows one success checkmark for a hand
stack, while preserving the individual pieces' delivery rewards and count.

Implementation: fragments now enter FoodCollection through the existing E input,
with the existing limit of six pieces and arrangement. E toggles collection
and Q throws it. The existing LMB drop applies with a cleaning tool selected.
The dedicated FoodFragment collision
channel is ignored by player capsules and skeletal/Blueprint colliders. Native
impact and collection-contact guards also prevent fragment-induced knockdown.
Held fragments overlap loose fragments rather than pushing the pile during
pickup. Whole-food and mixed-load contact behavior remains unchanged.
Throat delivery uses the carrier position and detaches each fragment before
its immediate score/disposal, leaving no stale entries in the stack. Success
feedback is emitted once when the last eligible fragment from that carrier's
stack is accepted. Both the throat and base disposal path use this grouping.

Validation recorded for this follow-up:

- **Editor build passed:** `MessControlEditor Win64 Development`, 34 actions,
  115.26 seconds. Log: `Saved/Checks/FragmentStackEditorBuild.log`.
- **Live Coding passed:** the subsequent held-stack/loose-fragment collision
  response adjustment was integrated successfully at 00:26:42 UTC. Log:
  `Saved/Checks/FragmentStackLiveCoding.log`.
- **Focused local regression passed:** one test,
  `MessControl.Food.Stack.FragmentsDoNotKnockPlayers`. Log:
  `Saved/Checks/FragmentContactRegression.log`. This verifies fragment contact
  handling for ordinary players, rather than a multiplayer gameplay scenario.
- **Solo stack delivery passed:** the real F3 path spawned an Egg. Native primary
  tool input created five fragments without awarding delivery rewards. The
  normal E collection function gathered all five into the existing stack, and
  ordinary walking delivered them into the actual throat marker. All five
  disposed in the same sampled update, leaving zero collection entries.
  `FoodSwallowed` increased from 0 to 5, player points from 0 to 25 and team XP
  from 50 to 75. No absorption/swallow transit phase was observed.
- **Loose-fragment contact passed:** a fragment moving at 1300 cm/s crossed the
  standing player without HP loss or knockdown. Runtime collision responses were
  Pawn Ignore and floor Block. Report: `Saved/Checks/FragmentStackSolo/Report.json`;
  preserved run:
  `Saved/Checks/FragmentStackSolo/Runs/20261012T004006Z_30712db7/Report.json`.

The solo fixtures used one Egg selected from actual bounds, temporary parent
health of 1 for a short native cut and supported parent/pile positions. The
earlier contact probe also set an initial loose-fragment velocity. Fragment mesh,
scale, count and collision were not changed for the probes. Collection used the
native E RPC; movement and delivery used ordinary walking. The camera was a
temporary PIE camera with native HUD retained. Settings were restored after the
owned PIE sessions.

No network or ragdoll PIE check was run for the follow-up. The dedicated channel
is ignored by the ragdoll components in source, but that is not runtime proof.
No cook or packaging was performed. The short follow-up video and exact probe
limits are recorded in `Artifacts/FragmentStack_20261012/README.md`.

### Single-checkmark refinement

The previous passing solo run established fragment generation, pickup, harmless
contact and delivery rewards; it displayed individual checkmarks. Source now
groups success feedback for the delivered hand stack in both throat and base
disposal delivery paths.

- **Incremental Editor build passed:** `MessControlEditor Win64 Development`,
  five actions, 30.69 seconds. Log:
  `Saved/Checks/FragmentSingleCheckmarkEditorBuild.log`.
- **Focused green-zone solo check passed:** five native Egg fragments collected
  in the hand stack and delivered through ordinary walking. All five disposed
  in the same sampled update, leaving `Pieces=0`; `FoodSwallowed` increased
  0→5, player points 0→25 and team XP 50→75.
- **One success effect observed:** exactly one new zero-damage `AMCReactionVFX`
  was created at disposal, 17.2 game seconds, at the actual fragment feedback
  position. Its success-effect identity is derived from the native feedback
  spawn path, birth time, position and zero damage; the private effect enum was
  not read. Report: `Saved/Checks/FragmentSingleCheckmarkSolo/Report.json`;
  preserved run:
  `Saved/Checks/FragmentSingleCheckmarkSolo/Runs/20261012T005416Z_1ca06bef/Report.json`.
- **Video replaced:** `Artifacts/FragmentStack_20261012/FragmentStack.mp4`,
  7.667 seconds, 1280×720 H.264, 41 genuine source frames captured at 6 Hz.
  Full decode and visual review passed: the stack and single success effect are
  readable. The Editor was restored to `/Game/Maps/L_MainMenu`, with PIE inactive,
  no dirty maps, and probe settings restored.

This fresh run omitted the fast-fragment crossing fixture; the earlier
1300 cm/s harmless-contact result remains a separate check. The red disposal
zone has source coverage for grouping but no focused runtime check. Network,
ragdoll PIE, broad food/spawn balance, cook and packaging remain deferred.
