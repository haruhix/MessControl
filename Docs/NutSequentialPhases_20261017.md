# Sequential walnut boss phases — 2026-10-17

After the opening nut rain, only the Walnut Knight appears. Defeating it splits
its head and transforms the same actor into the Walnut Wizard. The Wizard
receives its own full, player-scaled health pool. The encounter ends only after
the Wizard is defeated.

## Implementation

- `MCNutRainEvent::SpawnBossEncounter` creates one Knight and tracks one living
  boss through both forms. Existing ground nuts and summon ownership remain in
  use. `BossesLeft` stays at one during transformation.
- `AMCNutBoss::ConfigureEncounter` enables `bHasMagePhase` for an event-owned
  Knight. Standalone Knight previews retain their ordinary final death.
- The first lethal hit enters the appended `Transforming` state rather than
  setting `bDefeated`. Pending attacks, projectiles and presentation cues are
  cancelled. The boss is grounded on the tongue if necessary.
- The transition lasts 2.4 seconds. The Knight disappears as the head splits at
  0.42 seconds. At 0.9 seconds the same actor changes to Mage role, receives fresh
  health and the Wizard model begins appearing; its reveal finishes by 1.75
  seconds. Combat resumes after the full transition.
- Tool hits are rejected throughout the split, including after the Wizard's
  health is initialized. Damage beyond the Knight's remaining health cannot
  carry over. No victory, XP or final-death feedback is issued between forms.
- The Wizard uses its normal movement, collision radius, spells and final-death
  behavior. Its first fireball becomes ready 1.25 seconds after the transition,
  followed by its existing summon/rain/teleport schedule.
- The normal event banner shows phase 1/2, the head split, then phase 2/2. HUD
  arrival identity includes the role so the Wizard gets a separate arrival
  alert despite retaining the same actor.

`MCNutCombatEffect::PresentKnightHeadSplit` presents two instances of the existing
`/Game/Gameplay/CoreLoop/SM_Walnut_HalfShell`, opening outward with a small arcane
burst and ring. The cosmetic halves have no collision or damage. Cue age comes
from the existing replicated server clock; event cleanup cancels owned effects.
No skeletal mesh fracture, physics simulation or new animation import is needed.
The new state and cue values are appended to their enums to preserve older
serialized values.

Primary code is in `MCNutBoss`, `MCNutBossTypes`, `MCNutRainEvent`,
`MCNutCombatEffect`, `MCGameplayHUD` and `MCSingleDayDirector`. The F3 tooltip in
`MCDevPanelWidget` describes the two successive forms. Profile values and saved
assets were preserved, including the corrected Knight animations and the mesh
deformer assignments used by the body-volume fix.

## Validation

Evidence is in `Artifacts/NutSequentialPhases_20261017`.

| Check | Result |
| --- | --- |
| Final native Editor compile | Succeeded; `EditorBuildWithPhaseUI.log`. Temporary compiler settings were restored byte-for-byte. |
| `MessControl.CoreLoop.NutRain.DirectedSeriesAndGuaranteedBossGate` | Success, zero errors. Two warnings concern the Engine Cube test fixture's missing CPU-access flag. |
| Final standalone solo PIE in `L_Mouth` | All 11 checks passed, including native rain, one opening Knight, lethal hit, complete split, full Wizard HP, attempted hit during split, native Wizard fireball and final victory. |
| Animation/asset preservation | SHA-256 comparison of 110 protected files; no differences from the task baseline. |
| Motion review | 127 genuine engine frames; head split and Wizard emergence inspected in the contact sheet. Encoded video fully decoded without errors. |

The focused native test ran before the final UI-only patch; the final solo check
used the final compiled binary. Its tool is
`Tools/Unreal/check_nut_sequential_phases_solo.py`. The older
`check_nut_fight_redesign_solo.py` records the historical simultaneous-boss flow
and is superseded for this encounter-order check.

The solo fixture uses the real F3 event and ordinary Pickaxe input. For a short
check, it reduces each form to 40 HP only immediately before its lethal hit,
removes the Knight shield, holds initial attack readiness while positioning the
observer, and restores observer HP. Before the Wizard HP reduction, the probe
verifies its normal fresh pool of 1100 HP and observes its native fireball.
Neither role nor transition state is fabricated. These are temporary PIE values;
the normal game does not receive the fixture's health restoration or reductions.
The first fixture attempt selected Brush after F3 reset the inventory; it was
corrected to reacquire the live pawn and select Pickaxe. The failed setup evidence
is retained in `Attempt1`, separately from the passing `SoloCheck.json`.

`preview.mp4` repeats genuine 15 Hz captures at 30 fps using simulation timestamps,
with captions and no sound. It uses a review camera under `L_Mouth` lighting and
the player camera's post-process settings. It demonstrates phase timing and
presentation, rather than full-fight balance. Encode via
`Tools/Unreal/encode_nut_sequential_phases.py`.

The fixture restored fixed-frame-rate and background-throttling settings to its
baseline and stopped its owned F3 event. The editor log records PIE teardown and
a normal editor shutdown; no Unreal Editor process remained at final inspection.
The editor had been closed before this task's authoring session. No Windows game
build, cook/package or multiplayer validation was performed.
