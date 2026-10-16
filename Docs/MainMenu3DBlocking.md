# Native main-menu blocking

Current reference-finish work map: `/Game/UI/MainMenu3D/L_MainMenu3D_RefFinish_v08`
in Unreal5.8. The earlier `/Game/UI/MainMenu3D/L_MainMenu3D` remains available
for the gray blocking checkpoint. Use solo PIE.
This is an isolated functional preview; the project's existing startup map remains unchanged.

## Published reference-finish checkpoint, 2026-10-16

The work map now contains the Human/Predator reference meshes, symmetric
Final96 Vampire, detailed r05 cavity and r06 low tongue bed with their actual
Painter PBR maps. DAY04-07 remain gray placeholders. The existing gameplay
character and Hello1 animation are reused. Final1:1 art is not approved.

The inner cavity remains present while its mouth closes: component Z follows
`.5+.5*ownOpen`, with a row-owned constant clipping mask. The jaw driver retains
its render objects and masks logically hidden rows with `CardHalfHeight=-1`.
A relative .99999/1 alternation on Pose_000 initializes both morph buffers;
other three pose channels are unchanged. No global ForceUpdate, AA, VSM or
project shader configuration is saved by this repair.

Saved Widget/PlayerController graphs compile; actual solo PIE runs15-17 pass
sampled first/next jaw-interior coherence, normal selections/Back, 7→3 return,
immediate Play/Select and explicitly slowed inner-corner closure. Runtime
collision is NO_COLLISION for all seven mouths. The final editor checkpoint
has PIE stopped, no review callbacks, no dirty packages, original CVars and
PIE window preferences restored. This is not a full-frame-rate flicker or
performance pass. Physical wheel was not repeated after Escape stopped
Computer Use. Build/cook/package and multiplayer tests were not run.

The current source handoff state is
`ArtSource/UI/MainMenu3D/20261015_refB_v1/Assembly/UnrealHandoff/v08_ReferenceFinish/work_state.md`.
`Tools/Unreal/Menu3D/Evidence/v09_publish_saved_dependencies.json` records the
saved package closure used for publication. Frozen current Blender/Painter
packets and selected final native evidence are retained; rejected candidate
history remains local. Historical receipt paths below may refer to those
unpublished local experiments.

The prepared C++ settings/connection bridge remains unbuilt, unloaded and
outside this published checkpoint. Production startup/return routing,
complete UI/backend integration, minimized/reveal, final lighting and1:1
reference appearance remain deferred.

The flow is Main Menu → Play → Select Day. Three mouth panels share one 16:9
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

## Interaction implementation

The PlayerController updates the 3D stage and widget layout once per game tick.
One stored scroll position drives both native world placement and the ScrollBox;
there is no separately deferred ScrollIntoView animation. Hover requires actual
pointer motion and cannot switch days merely because a row moved under the cursor.
Returning to the catalog resets selection, the counter and wheel accumulation.
The connection overlay preserves the selected mouth and its opening pose.
The mouth skeletal components explicitly tick after the menu PlayerController,
refresh pose/bones while hidden and disable animation update-rate skipping.
The original character animations are preserved. Synthetic zero-delta pointer
events cannot queue hover intent.

DAY03 now uses constant uniform XYZ scale0.5376540384. Rebuilding the two jaws
removes the need for the earlier pose-dependent width correction. Projection at
the native camera predicts full widths675.4px closed and674px open, against the
approximately673px Human baseline. `WidthFit_v06_01` records the first native
candidate; source projection is supplemented by actual native screenshots.

`v06_SymmetricRemodel` rebuilds the previously uneven upper row (7 left/9 right)
as8 matched pairs, keeps8 lower pairs and two matching fangs, and replaces the
flat gum slabs with rounded tissue seated around the roots. The old lower gum
hid the central lower crowns through the first half of opening. Independent
ray checks and actual Run08 frames establish that defect.

`v07_LowerRowReadability` additionally raises the lower arch by0.8 source units
times openness. Closed bite, upper geometry, tooth dimensions, XY, UV and rig are
unchanged from v06. Independent nine-sample source/fresh-FBX checks pass symmetry,
root, morph and material-slot gates; at the fitted native scale the panel mask
adds no central lower-crown clipping. `SK_DAY03_LowerReadable_v07` is the current
native candidate, retaining the existing skeleton and gray material overrides.
Native opening/closing with tongue/character and normal transitions were reviewed
in the actual solo PIE viewport. Inner DAY03 controls are18designpx higher and
appear only above openness0.98, then hide as closing starts. Their hidden state
retains layout participation, avoiding a one-frame text jump on first reveal.
Existing cheek/tongue children fit the new arch with relative X0.90 and a75%
vertical card mask; crown geometry and the existing character are preserved.
This remains functional geometry, without final material approval.

## Validation and remaining work

- Current v07: independent source/fresh-FBX checks pass all nine symmetry/pose samples and the lower-row visibility gate. Actual solo PIE `LowerRow_v07_Run04` passed18 phase-qualified state checks across13 scheduled commands, including slow open/close, normal3↔4 transitions, Back/Main/Play and catalog reset. Its270 real screenshots span25.94s. Subsequent7-second `LowerRow_v07_Run05` isolates the final Hidden-layout repair; first visible UI text is aligned and Back clears the lower crowns. Tongue stays behind enamel and the existing character does not cover the central row in reviewed frames.
- These are controlled shared-function render reviews, with pointer hit delivery temporarily isolated and game dilation0.15 in the slow segments. Normal dilation1 and interactive root visibility were verified restored afterward. They do not repeat the earlier physical wheel/keyboard checks or prove every rendered frame. Run01's raw mixed-phase FAIL and Run02's off-schedule selection are retained; they are not claimed as route passes. Run04 separately classifies command-time observations made before the next world tick.
- The Widget Blueprint and PlayerController compiled with warnings treated as errors and were saved.
- Historical solo PIE Run08 with v05 passed all19 settled-state checks: Main/Play, all seven days, known-mouth opening, connection/Back, future-day launch rejection, reverse selection, collapse and catalog reset. Actor and component visibility both match the three-row window. It does not validate the later v07 geometry.
- Historical Run08 frames confirm three mouths together, native opening and the existing animated character in Days1–3. Earlier recordings exposed both an out-of-phase Slate capture and a skeletal morph tick delay. Run08 visual review found matching labels/geometry, no missing jaws and clean footer edges. WidthFit03 passed the3% v05 width check. Sampling at about10fps does not establish every rendered frame or normal gameplay frame rate.
- A physical wheel detent selected DAY03 → DAY04 after the input fixes. A second physical reverse test was interrupted by concurrent user input; it is not recorded as a pass.
- Native Slate keyboard input confirmed Down selection, Enter on a focused inner Back button collapsing the mouth, and Escape returning to Main.
- Required saved package dependencies were audited; all task dependency files are included.
- After saving/restoring the task skeletons, a short solo PIE confirmed the DAY03 scene still renders.
- Boundary visibility culling is compiled and saved; Run04 confirms exactly three visible mouth actors at each settled catalog/connection checkpoint. Direct fractional-wheel and hover-input checks remain pending; the interrupted reverse physical-input attempt is explicitly marked as unverified.
- Final reference-quality geometry/material finish, startup-menu integration, selected-day game rules, build/cook/package and network validation remain deferred.

This checkpoint is functional blocking awaiting visual/interaction feedback,
not the requested final1:1 art.

The actual50-second Run08 recording, measured capture timing, native samples and
`route_validation.json` are under
`ArtSource/UI/MainMenu3D/20261015_refB_v1/Assembly/Review/Unreal_FunctionalBlocking_v03_FullRoute/Run08`.
Frames are requested through `Shot SHOWUI` and processed after the engine draws
the actual 3D viewport; the older direct Slate redraw can pair fresh UI with an
earlier viewport texture and is retained only as diagnostic evidence.
The video crops window chrome only and repeats captured frames to encode at30fps;
it contains no generated or invented intermediate poses. Current task graphs are
saved locally; the earlier pushed checkpoint is commit `806d2619`.
The latest lower-row video and first-reveal frames are under
`Assembly/Review/Unreal_FunctionalBlocking_v03_FullRoute/LowerRow_v07_Run05`;
the longer normal-transition route is Run04. These later revisions are local.
