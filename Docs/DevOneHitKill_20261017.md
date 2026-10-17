# F3 one-hit kill — 2026-10-17

F3's current-test column now starts with **Убийство с одного удара**. The button
shows ВКЛ/ВЫКЛ, updates immediately on click and is repeated in the panel's
status summary. Clicking it again restores ordinary combat damage.

The host-only Development/Editor command stores its state on the current
GameMode actor; it is off in a fresh Play session and is never saved to an
asset, user config or run profile. Starting another F3 event in the same world
retains the mode. Bot-session startup clears it before ordinary bot combat.
Shipping always returns disabled through the same helper.

Only validated player damage received by enemies is amplified to their current
health, after the ordinary perk calculation. Both `AMCNutEnemy` (including
knight, wizard and jelly subclasses) and the independent `AMCBossCharacter`
receiver use it. Existing hit distance, occlusion, phase invulnerability,
feedback, defeat, cleanup and knight-to-wizard transformation remain in charge.
Damage to teammates, arena teeth, food and incoming damage are not amplified.
Environmental hits with no player instigator keep their original damage.

The real solo F3/Pickaxe check passed 12 checks: mode initially off,
button discovery/state updates, ordinary enemy damage 240→200, one-hit defeat
of that enemy, shielded knight defeat into the normal transformation, a living
1100 HP wizard, wizard defeat, toggle off, and ordinary damage restored to 40.
The evidence retains 359 samples and 52 genuine gameplay captures in
`Artifacts/DevOneHitKill_20261017/SoloCheck.json`. The shared cleanup initially
encountered an already collected defeated-actor wrapper after all checks had
completed; the original observations were recovered without alteration. The
probe now saves the report first and skips only collected null wrappers.

The independent generic boss receiver passed five supplemental solo checks in
a freshly launched Editor: ordinary damage, enabling the real F3 action,
unchanged environmental damage, lethal player damage and ordinary damage
restored after disabling. These are receiver calls rather than tool contacts.
`GenericBossCheck.json` records that scope explicitly.

The normal Editor build passed in 54.59 s, after the successful live compile.
The editor was restarted only after confirming there were no unsaved packages;
the original arena, viewport and selection were restored. No packages were
saved by this task. Previous wizard animation bindings remain intact. Final
state and both probes are recorded beside `EditorBuild.log`. No multiplayer
test or packaged game build is part of this request.
