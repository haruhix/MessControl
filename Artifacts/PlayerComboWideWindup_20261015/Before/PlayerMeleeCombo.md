# Player melee combo

Ordinary knife and pickaxe primary attacks use a three-press chain:

1. The right hand winds across to the **left shoulder**, then cuts sideways to the right.
2. A descending curved stroke travels from **upper right to lower left** across
   the front. The cutting edge (or lower pick point) leads along the curve;
   its orientation is solved through the actual equipment grip after body lean.
3. The tool rises above the crown and chops down in front.

The crown leans forward and twists with the hand; the free hand counterbalances.
The existing accepted-hit flash, particles, sound, camera accent and cosmetic
contact hold remain active on each actual hit.

Each primary press is an edge, not a held-repeat request. Two extra presses can
be buffered during the first stroke, so three rapid clicks still play all three
steps. Releasing between clicks does not discard those deliberate presses.
No fourth stroke is queued inside the chain. After the third recovery, another
new press starts step one. After a 0.38 s gap following a stroke's recovery, the
next press also restarts at step one. Losing the ability to work, using another
tool or entering an interaction cancels queued steps. Brush cleaning, aimed
calculus work and continuous upgraded tools retain their existing input paths.

At normal attack rate, knife strokes take 0.58 / 0.62 / 0.78 s and pickaxe
strokes take 0.60 / 0.64 / 0.80 s. Contact is at 36% / 42% / 46% respectively.
Rogue attack rate scales these attack and contact clocks. Damage per contact
retains the ordinary tool damage; the finisher does not add a damage multiplier.

`MCMeleeCombo` owns a weak-keyed per-character native state. Authority accepts
and consumes buffered presses; the existing reliable swing multicast carries
the step in the upper nibble of its tool byte. Its reflected signature and
saved Blueprint interfaces are unchanged. Pose sampling is procedural in the
existing tooth animation proxy, with per-step wrist rotation and translation.
The actor's movement and input remain active while attacking.

`mc.Combo.Debug 1` logs accepted steps and queued presses for local diagnosis.
Validation evidence and genuine camera captures are under
`Artifacts/PlayerCombo_20261015`. No multiplayer, cook or packaging validation
is implied by a solo check.

The descending second-stroke correction, actual tool geometry inspection,
cutting-edge alignment samples and updated gameplay preview are under
`Artifacts/PlayerComboDiagonal_20261015`.
