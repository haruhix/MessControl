# Native main-menu blocking

Open `/Game/UI/MainMenu3D/L_MainMenu3D` in Unreal 5.8 and use solo PIE.
This is an isolated functional preview; the project's existing startup map remains unchanged.

The flow is Main Menu → Play → Select Day. Three mouth panels share one16:9
screen. A selected mouth opens through native skeletal morph targets, neighboring
rows move, and the existing Current character plays its existing Hello1 animation
inside the known mouths. Mouse wheel and Up/Down select one adjacent day.
Back collapses the active mouth, then returns to Main. Enter opens or activates a
known selected day while preserving focused Back-button behavior.

There are seven panel slots. Days1–3 are Human, Predator and Vampire; Days4–7
reuse neutral blocking geometry and remain Coming Soon with no launch button.
The inner Play opens the existing connection menu. Selected-day gameplay rules
and game travel are not integrated by this preview.

The scene uses texture-free, lit gray blocking overrides and actual 3D meshes,
separate cheek/tongue volumes, native morphs and lights. The imported DAY03 mesh
still has historical radiance materials in its default slots, so their dependencies
are retained in the commit. They are not the approved final rendering direction.
The three imported skeleton packages are saved alongside their meshes; DAY03's
missing skeleton binding was restored from the identical exported rig hierarchy.

## Included authoring sources

`ArtSource/UI/MainMenu3D/20261015_refB_v1/Assembly/UnrealHandoff` contains the
current FBX handoffs and editable morph-source Blender files. DAY03's symmetric
compound, the shared interior source and the archived family geometry used for
functional DAY01/02 are also included. These are geometry/blocking sources, not
final texture or artwork approval. Rejected intermediate art, render sequences,
Painter experiments and browser prototypes are not part of this checkpoint.

`Tools/Unreal/Menu3D` retains the current accordion authoring, saved-package
audit and native review helpers with their graph DSL/compile evidence.
`Tools/Blender/Menu3D_20261015` retains the native FBX exporters.
Open the saved assets directly for review; the scripts are development helpers,
not a verified clean-room scene bootstrap.

## Validation and remaining work

- The final Widget Blueprint compiled with warnings treated as errors and was saved.
- Solo PIE displayed three mouths together, native opening and the original character.
- A physical wheel detent selected one adjacent day in the earlier interaction run.
- Required saved package dependencies were audited; all task dependency files are included.
- After saving/restoring the task skeletons, a short solo PIE confirmed the DAY03 scene still renders.
- The final complete seven-day scroll route, input edge cases and automated route-recording helper remain unverified.
- Final reference-quality geometry/material finish, startup-menu integration, selected-day game rules, build/cook/package and network validation remain deferred.

This checkpoint is functional blocking awaiting visual/interaction feedback,
not the requested final1:1 art.
