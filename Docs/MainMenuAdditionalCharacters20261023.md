# Three additional main-menu characters — 2026-10-23

`L_MainMenu` retains both existing characters and adds three ordinary
SkeletalMeshActors under `Menu/Characters`:

- `MENU | Dance 1`: existing `A_Teeth_Dance1`, 1.433333 s, independent repeat.
- `MENU | Dance 2`: existing `A_Teeth_Dance2`, 1.833333 s, independent repeat.
- `MENU | Love`: new `A_Menu_Love_Face_Loop`, 6 s, fixed reference body with
  the authored `Eyes_delight` expression and two blinks. The delight morph
  yields during blinking. The first and last face values match.

The love mesh and skeleton are private copies under
`/Game/UI/MenuShowcase/AdditionalCharacters20261023`, so the two new morph
curve metadata entries do not modify the gameplay skeleton. Dancers reuse
the current gameplay mesh and original clips. Each actor has saved playing
and looping animation data; no menu Sequence retiming is required. The actors
have no gameplay components, collision, overlap events or replication.
All mesh, animation and material dependencies are reflected map references.

The artist camera, FOV, original actors, lighting and postprocessing are
preserved. Temporary lighting/postprocess experiments were reverted at the
user's request. The added characters therefore retain the existing background
depth blur. Their placement is projected onto the passive tongue surface.

The final short solo PIE passed: five total character actors, three passive
additions, five Dance 1 wraps, four Dance 2 wraps and one love-cycle wrap.
The love body's sampled hand remained fixed while delight/blink curves changed.
The actual menu widget and original camera were active. Preload reached ready
with 300 resources and 131 streaming targets in 1.49 s. A warm Editor run does
not validate a cold packaged launch. No build/cook/package or network test ran.

Recipes: `Tools/Unreal/UI/add_menu_characters_20261023.py` and
`Tools/Unreal/UI/check_menu_characters_20261023.py`.
Backups, integration report, validation samples and preview are in
`Saved/MenuAdditionalCharacters20261023`.

## Standing observer follow-up

`MENU | Love` now uses an 18.266667 s cycle with a fixed root, feet, hands and
body. Only `head_x`, `c_eye_l` and `c_eye_r` have changing bone tracks. Its gaze
is baked toward the cleaner brush/hand positions from the existing cleaning
animation. Two smooth camera glances occur at 5.1–7.85 s and 13.0–15.4 s,
then attention returns to the work. Five brief blinks preserve `Eyes_delight`
between closures. The existing menu clip reference remains in place.

The current artist placements (including the observer below the cleaner),
camera, lights and postprocess are preserved. Re-run
`Tools/Unreal/UI/author_menu_observer_gaze_20261023.py` after moving gaze targets
to rebake their positions. `check_menu_observer_gaze_20261023.py` verifies the
saved layout and a natural solo menu cycle. The observed body movement was
0 cm, camera gaze error 0.0224 degrees, head rotation range 44.86 degrees and
blink peak 0.9656. A full loop wrap and normal menu readiness passed.

The first attempt to change sampling rate hit UE's animation compression
assertion for a fractional compression frame. The artist scene and prior clip
were restored from backups, and the cycle was recreated at the existing 30 fps
with 548 complete frames. Saving and the final solo run then passed. No source
code, build, network test, lighting or postprocess change was required.
Evidence and before packages: `Saved/MenuObserverGaze20261023`.

## Eyes-only correction

At the user's request, `head_x` is now fixed in the reference pose throughout
the cycle. Only `c_eye_l` and `c_eye_r` rotate between the work target and camera;
the existing blink/delight curves and timing are retained. The solo check now
asserts a stationary head and moving eyes. Earlier head-turn figures above
refer to the superseded version. Before packages: `BeforeEyesOnly` in the same
evidence folder.
