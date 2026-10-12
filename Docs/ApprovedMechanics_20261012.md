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
15. Restore small food fragments. Drag into the correct zone for immediate
    completion and checkmark, without a suction stage.
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
| 15 | Cutting restores small fragments. Moving a fragment into its matching delivery zone completes it immediately and shows the completion mark. |
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
visual or solo check passed.

### Videos

Short clips use genuine Unreal frames and show HUD alarms, pain-wave jumping,
late chili detonation, frost nova jumping, frost brushing/refreezing, stink
reactions, the huge stain and spectator orbit. Files and capture limitations
are listed in
`Artifacts/ApprovedMechanics_20261012/README.md`.
