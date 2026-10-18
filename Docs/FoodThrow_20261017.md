# Food throw and physical throat entry — 2026-10-17

Food entering the visible throat aperture is received at its front plane, rather
than requiring its center to travel another45cm. The throat samples after physics
and checks the movement segment, including a food actor's first release frame.
Loose bounced food also qualifies; held food, tools and incoming meal transport
remain excluded. The existing authored aperture outline and tongue floor guard
still reject side misses and routes under the floor. Good food can be received
during the gag cycle; bad ingredients retain the existing spasm/vomit behavior.
Each player-delivered piece now updates the success marker, even when the player
still carries other pieces. Existing disposal/reward guards prevent duplicates.

Player camera aim now solves a gravity arc to the visible opening or ordinary
ray target. When the artist moves the opening below the tongue, the throat's aim
point moves into its visible upper part. Unreachable aim produces an upward toss.
The actual player Blueprint FoodCollection.ThrowSpeed is1800cm/s, previously1400;
the native default agrees. Throw perks remain applied consistently to aim and
launch. Food uses zero air damping and a gentle1.69rad/s initial tumble; ordinary
damping returns on physical contact or pickup. Both hand-stack and legacy food
grips share this launch. Carrier velocity is not added after solving the arc, so
walking does not move the predicted endpoint sideways. Brush throws retain their
previous aiming/launch behavior. Stack cadence and collection settings are unchanged.

Reapply the saved speed with `Tools/Unreal/author_food_throw_speed.py`. One focused
standalone check is `Tools/Unreal/check_food_throw_solo.py`, run after starting PIE.
It resolves the first perk, pauses unrelated directors, creates three small food
fixtures, positions the player/review camera and uses ordinary Q/held-Q input.
No direct delivery calls are used. Two separate loose-food fixtures check shallow
first-frame entry and a45000cm/s crossing; an outside-rim fixture checks rejection.
These are disclosed PIE-only probes, not normal player tuning. The check restores
its camera and engine frame/throttle settings; PIE was stopped after completion.

Validation passed on the user's current unsaved L_Mouth placement:

- One ordinary Q delivers just the top piece, with success feedback while two
  pieces remain held. Held Q receives the remaining two exactly once.
- A loose first-frame entry is received at6.869cm, before the former45cm threshold.
- A very fast first-frame crossing is received; a shot outside the rim is rejected.
- Three actual physical flights measure1793.58–1793.89cm/s at the first sample,
  roughly1.69rad/s spin and about28degrees of rotation before reception.
- Fitted gravity is979.26–979.44cm/s² with sub-millimeter fit residuals, confirming
  the sampled trajectory follows a gravity arc rather than a straight move.
- Saved BP speed, other collection tuning, the user's dirty map/wall/material
  packages, protected asset/config bytes and temporary compiler restoration pass.

Evidence is in `Artifacts/FoodThrow_20261017`: before/source/package backups,
`Solo/Report.json`, `FlightAudit.json`, `FinalAudit.json` and genuine scene-capture
frames. `ThrowPreview.mp4` repeats those frames at their recorded simulation times,
with a short final still; it fully decodes. The side observer view is dark under
the mouth lighting and is diagnostic evidence, not a gameplay-camera presentation.
Live Coding succeeded. No broad automation suite, multiplayer, cook or package
was requested or performed. No unrelated map/content packages were saved.
