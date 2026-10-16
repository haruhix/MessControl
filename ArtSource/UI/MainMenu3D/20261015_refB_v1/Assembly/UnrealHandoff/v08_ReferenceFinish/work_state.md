# Native reference finish

## Current checkpoint (supersedes the historical progress below)

2026-10-16 final narrow interior-continuity checkpoint: **native sampled
geometry continuity PASS; final appearance/1:1 and motion-look remain HOLD**.
The r06 tongue/Painter import and own-jaw cavity envelope described below are
saved. A second problem was then isolated: all three CPU morph maps were
correct, while the first rendered selected jaw could retain its closed pose
and expose the already-moved tongue beneath the gum. Diagnostics06-12 did
not fix that mismatch. The temporary direct `OverrideAnimationData` node from
10 was rolled back, compiled and saved; PostUpdateWork, FXAA, SingleNode and
thread-lag experiments are not production policy.

Source-backed controls13/14 found that runtime ForceUpdate1 cleared the sampled
boundary mismatch, while startup-only ForceUpdate did not. Saved candidate15
uses no global renderer override: `DriveMouthDay` keeps jaw actor/component
render state alive, expresses logical Hidden as existing `CardHalfHeight=-1`,
and modulates only the Pose_000 channel by a relative factor .99999/1 from
frame parity. Other three morph channels retain canonical weights. Conservative
world displacement is below .00105cm. Cavity/tongue visibility/envelope and
character drivers are unchanged by this second repair. See
`Tools/Unreal/Menu3D/Evidence/v09_morph_buffer_source_analysis.md` and
`v09_morph_buffer_keepalive_candidate.json`; canonical driver DSL is updated.

Independent actual-frame review15 passes first/next boundaries with ForceUpdate0.
Fresh16 `LocalFullRoute` passes normal selections/StepDay, jump7→return3,
Back→Main→immediate catalog/Select1 and repeated transitions without the stale
jaw/external-tongue strip. Its214 frames span21.46s (median99ms/max173ms).
Fresh17 `LocalSlowCorners` passes inspected left inner-wall corners during
Vampire→Predator and Back closures: no abrupt shell hide, dangling arcs or lower
closed tabs in those samples. It contains52 frames/~12s with explicit .15 time
dilation. These are sampled continuity gates, not high-frame-rate flicker or
performance proof. Gray DAY04-07 remain placeholders. Actual screenshots,
timed videos and independent notes are in each run's `QA_NativeLook` directory
under `Assembly/Review/Unreal_FunctionalBlocking_v03_FullRoute`.

Actual runtime policy confirms all seven jaw collisions NO_COLLISION and
masked future rows at -1. PIE is now stopped, callbacks cleared, both task-owned
menu Blueprints saved, no dirty maps/content, renderer CVars and original PIE
window preferences restored. Receipts: `v09_mouth_keepalive_runtime_policy.json`
and `v09_interior_continuity_saved_editor_state.json`. Physical wheel was not
exercised after Computer Use was stopped by Escape. Minimized/reveal and
performance are unverified. Session preview was SM6 with temporary VSM1;
project SM5/renderer configuration and production startup/return are unchanged.
No C++ build/cook/network test was run. Full art, UI and backend integration
remain unapproved.

### Earlier own-jaw envelope checkpoint

Latest interior repair, 2026-10-16: r06 Refine03 tongue geometry and all five
new Painter maps are imported, saved and bound to the three existing row-owned
tongue instances. Cavity geometry/maps and all material scalar/vector defaults
are unchanged. Receipt: `r06_refine03_native_binding_report.json` in
`Tools/Unreal/Menu3D/Evidence`. Actual native front/oblique inspection
`OrganicDetail_v08_03_r06` shows a continuous low tongue bed; those two macro
views temporarily disable light shadows for geometry inspection and restore
camera/light/visibility state. They are not the final menu-lighting approval.

The original Open<.05 shell hide and the first linear-to-zero repair are
rejected. Native `LowerRow_v09_InteriorCollapse01` proved the latter leaves
black corner gaps. A row-height envelope removed the abrupt hide, but actual
`LowerRow_v09_InteriorEnvelope03_r06/frame_0123.png` exposed small red rim arcs
outside the all-closed jaw silhouette. The saved follow-up now drives real
component Z by `.5+.5*ownOpen`. The intermediate inverse row-height mask in
`LowerRow_v09_InteriorEnvelope04_OwnJaw_r06` still left small tabs below the
all-closed gum. The final local mask is constant for each row:
`370/(2.6666666666666665*parentUniformScale)`. Geometry and clipping therefore
follow that mouth's opening, independently of neighboring row sizes; at
Open=0 they exactly match the previously clean closed-neighbor state. Each
inner shell is culled only with its row. X/Y, jaw morphs, camera and input are
preserved. Receipts: `v09_interior_own_jaw_envelope_report.json` and
`v09_interior_own_jaw_mask_report.json`.

Actual fresh `LowerRow_v09_InteriorEnvelope05_OwnMask_r06` contains166 native
frames over16.89s, mixed normal/explicitly slowed transitions and an all-closed
hold. Root and independent QA reviewed partial corners and closed silhouettes:
the reported cavity pop, detached slivers, dangling arcs and lower-side tabs
are cleared in the inspected frames. This narrow visual gate passes; it does
not approve final1:1 appearance or high-frame-rate flicker. The video preserves
actual screenshot timing (median100.7ms/max139.2ms), with no invented poses.
The separate no-screenshot solo check passed six-component visibility/own-jaw
scale invariants with43/33/77 partial observations for days1/2/3. Its maximum
sample interval328ms is recorded; it is not a performance or flicker pass.
Evidence: `v09_interior_own_jaw_mask_continuity_solo.json`.
`v09_interior_own_jaw_mask_final_checkpoint.json` confirms PIE stopped, no active
review callbacks, no dirty maps/content, VSM restored0 and session window
preferences restored. Physical wheel was not exercised after computer-use
was stopped by Escape. Production startup/return and project renderer config
are unchanged; no C++ build/cook/network tests were run.

2026-10-16 continuation. Final 1:1 appearance, normal-speed motion, complete UI,
runtime/backend integration and startup/return routing remain **HOLD**.
The fragmented Smart Project UV, the radial 64-chart candidate,
and the global enamel boundary-smoothing candidate are rejected and preserved.
The accepted outer packet is the immutable sibling directory:
`v08b_SeamUV/Candidates/Final96_GumMinStretch_LocalFangRepair`.

- Source SHA256: e5b14557b090f955065050f8426011e9e66fb07eb33cbcf88738c734148fb3b7.
- FBX SHA256: 41f93635dbf1b44513e0df8f8066f4b9a4f9a724641f112ae1a6132bc8e805f1.
- 96 coherent enamel charts and 6 gum charts; no overlap, collapsed charts or
  minority winding. Facial enamel density p05-p95 is approximately 238-278,
  and gum distortion p95 improved to 1.188. Root inspected atlas/checker images.
- Independent source / fresh-FBX / Painter-packet agreement and nine-pose
  symmetry/readability passed. Evidence is `QA_Independent/final_outer_gate.md`
  in that immutable directory. Native cavity, materials and UI are excluded
  from that acceptance.

The final FBX is imported, saved and assigned as
`/Game/UI/MainMenu3D/Meshes/SK_DAY03_Reference_v08b` in the isolated work map
`/Game/UI/MainMenu3D/L_MainMenu3D_RefFinish_v08`. Production startup and return
still use `/Game/Maps/L_MainMenu`; they were not redirected. Actual outer PBR
maps are bound as recorded below; native appearance is not approved.

Painter outer 4K maps are frozen and bound in Unreal. Manifest SHA256:
740cc6fd09959eb9dc040dcf74e34e9d5fe680c5776b81600fa13300557efe6f.
All ten source PNG hashes/dimensions/color spaces and actual material bindings
passed independent QA. Actual Painter front/oblique/grazing views show coherent
highlights without facial UV seams. Normal detail p99 is 2.03 degrees enamel /
3.55 degrees gum. Final native appearance is still HOLD: original lights produce
candy-red gums and overly pale uniform enamel. Runtime-only C/D candidates in
PBRCalibration_v08b_02 improve this; neither is a final 1:1 approval, and neither
was initially saved to asset instances. A later bounded tissue-tone candidate
is now saved consistently to all three rows: v08_organic_look_candidate01.json.
It restrains interior subsurface brightness and deepens the throat; texture
pixels, geometry and camera are unchanged. Dynamic shadows remain enabled in
the saved map pending a separate isolation/soft-shadow solution.

Two narrow Blueprint fixes compiled and saved: a single PC-owned UpdateStage
driver is enforced by removing the connected WBP Tick path; footer DaysBack
now calls the existing collapse-first Back function. Original compiled double
execution was not proved. New controlled solo PIE LowerRow_v08b_PBR01 passed:
first footer Back collapses, second returns Main, catalog resets, next selection
opens. Independent after-world-tick checks found no pose/height/counter errors;
first visible popup is aligned and lower enamel/center seam reads throughout.
Evidence: `Tools/Unreal/Menu3D/Evidence/v08_tick_owner_report.json` and
`v08_footer_back_report.json`. Input/morph/layout function bodies were retained.

Actual PBR01 closing frame0030 shows orange triangular gingiva blotches. This
remains a motion-look HOLD. A settled fixed .5 pose stays clean with original,
flat-normal, opaque-thickness, white-AO or hidden-interior variants. FXAA motion
capture skipped intermediate poses, so it is inconclusive. Slowed high-quality
diagnostic shows readable intermediate rows; a final normal-speed capture after
the old cavity replacement remains necessary. The earlier diagnostic overrides
were restored. Organic A/B06 confirms real tissue detail but not final quality.
No permanent antialiasing, scalability or project renderer change was made.

User explicitly reiterated that the interior must also be detailed. Organic_r05
has a real concave throat recess, rounded side arches, palatal rugae and one
rounded tongue body with dorsal lobes and sulcus. Root viewed front/native crop /
oblique and accepted shape for fitting. UV_Organic_r05_Candidate01 passed actual
checker/layout and independent source/fresh-FBX/OBJ units/normals/UV/manifold /
anchor gates. Cavity3/tongue2 charts, no overlap/folds; anatomy priorities and
unused tongue packing space are deliberate and measured. Source SHA0d2d1bb0…;
packet5b3e50d1…. Painter finished a separate editable interior/tongue SPP and
released actual surface maps on exactly that immutable packet. Geometry is
imported as SM_DAY03_Interior_Organic_v08 / SM_DAY03_Tongue_Organic_v08; actual
PBR is bound to MI_DAY03_Interior_v08 / MI_DAY03_Tongue_v08 in the work map.
Manifest SHA256: 4bf82cf5d3b78147ab38466f93b59853895ba557c34a6e5cfd45fd60fb72e075.
Receipts: `Tools/Unreal/Menu3D/Evidence/v08_organic_interior_import_report.json`
and `pbr_interior_day03_binding_report.json`. Native clip/fit, character contact,
normal-speed motion and final appearance remain pending. The same detailed
meshes/PBR are now bound to Human and Predator with separate per-day instances;
v09_organic_family_binding_report.json records cavity X=.90/.88, tongue X=.90.
Four narrow UpdateStage denominator literals changed from2 to2.6666667 for
the organic75-percent mask; compiled/saved with PIE stopped, no other graph
change (v09_organic_family_mask_report.json).

Actual native macro OrganicDetail_v08_01/03_detail_oblique_inspection.png
exposed r05's two-hill tongue failure; r05 remains NOT art-approved. Root selected
the continuous low-bed Refine03 form. The frozen replacement is
`Interior/UV_r06_Refine03_Candidate01`; geometry release manifest SHA256:
535bf9e152190f64ab2663851278278c81df5214f79d1db547e418e28bac0ccb.
Independent `Interior/QA_r06_NativeLook/independent_gate.md` passed source /
fresh-FBX / OBJ / units / UV-transfer / manifold checks for next native import,
excluding final appearance and actual animated character contact. Tongue has
two coherent charts, dorsal kappa p95 1.124 and density spread 1.096. Cavity
geometry/UV/FBX are exact to frozen r05. New tongue masks were regenerated on
the new UV through verified old/new corner correspondence, followed by a fresh
4K Painter MCP mesh-map bake and actual front/oblique/grazing inspection.
Released PBR manifest `Textures/shader_binding.json` SHA256:
b50b31c057f00a0234581b994bc14660c21b06b1f9e8b93d2ce49f2a2f865a64.
Its five new tongue maps retain the physical palette/roughness/papillae level;
five cavity maps directly link frozen r05. Tangent-normal p99 is 7.435 degrees,
roughness .180-.278. Native r06 import/binding is prepared, not executed at this
checkpoint; user-priority cavity popping is being repaired first.
Macro02 front was black and is invalid visual evidence despite a successful
capture callback; front camera helper is corrected for the next fresh run.
Macro03 is an explicitly unshadowed temporary inspection camera, not the final
menu frame. Camera, hidden actors, widget visibility and light flags restored.

IsolatedLighting_v08_01 actual B confirms cross-row shadow contamination;
exclusive row channels reduce it. C centered-key variant creates a broad black
rear band and is rejected.12 runtime light actors were destroyed and original
lights/channels restored. The receipt records failure because5 transient pointer
properties are protected against instance editor writes. The helper now resets
pointer intent through the intended BP function instead; no clean rerun claimed.
The saved baseline's own-fang shadows and normal-speed gingiva appearance remain
HOLD pending the selected renderer/look and motion gate.

Actual solo PIE comparisons `NativeVSM_v08_01` and
`NativeVSM_v08_02_Pigment` used SM6 in this task's editor session only. VSM removes
most large angular shadow artifacts compared with conventional shadows. These
captures did not change project renderer/configuration assets; runtime VSM and
material overrides were restored. A clean normal-speed final gate is outstanding.

DAY03 WarmCoronalBody_01 and stronger WarmCoronalBody_02 are frozen, separate
editable Painter candidates. The roughness reset in the first unfinished export
was repaired; selected data maps match frozen originals. Both exclude the upper
fangs exactly (982600 complete-chart pixels unchanged) and select only a new
ordinary-enamel BaseColor, linking the other nine maps directly to frozen Final96.
Warm02 is closer in the paired native pigment comparison; it is not finally bound
to the saved mouth material and is not a 1:1 approval. Warm01 manifest SHA256:
3ccbba9ec4c90bf391f6946b34b24d2fb6648650161e09666683e36c04376cc3.
Warm02 manifest SHA256:
a8415683b991194a495318129b3509fef8ce9699952ca01282c13738279f9066.

Human DAY01/v03 shape and UV_v02_GumReturns accepted as candidates by root after
actual checker/atlas review. Producer source/fresh-FBX nine poses passed. Imported
as SK_DAY01_Reference_v09 into the work map with its existing skeleton. Human
Painter PBR is released and now bound; manifest SHA256:
520d33f991de291684240e85d51a47fbb6b27e31e1f6123aac650ea1359438f5.
Receipt: `Tools/Unreal/Menu3D/Evidence/pbr_family_day01_binding_report.json`.
Predator DAY02/v02 shape and UV_v01 passed independent source/fresh-FBX/UV/rig /
nine-pose symmetry/lower-coverage gates and are imported as
SK_DAY02_Reference_v09. Its Painter front/close/oblique review has just completed;
the frozen PBR release at DAY02/UV_v01/Textures/release_status.json records
manifest SHA256: 9ba98d3a171727d2c48ef23ff1075b387e5e349c5ba9a2fe2ea7299ac948fb74.
Predator PBR is now bound (pbr_family_day02_binding_report.json); native final
appearance remains pending. Both family meshes'
native width/clip/character/animation fit and final look remain HOLD. Existing
character assets and animations are retained.

Root recovered and saved the WBP scale-only change after an overlapping PIE
compile attempt. `Tools/Unreal/Menu3D/Evidence/v09_family_uniform_fit_report.json`
records exactly two uniform XYZ literals: Human 0.510365, Predator 0.514498; no X
stretch or camera/motion/input rewrite. The earlier outer-only PBR01 evidence
does not validate the new combined scene.

Current user-priority repair: the inner cavity/tongue shell visibly pops during
day transitions. Six existing loops received LinearZ=max(Open,.001) and a .001
visibility cutoff, compiled/saved as recorded in
`Tools/Unreal/Menu3D/Evidence/v09_interior_opening_pop_report.json`. Compilation
is not visual acceptance: actual `LowerRow_v09_InteriorCollapse01` is **FAIL**;
the shell over-shrinks and exposes black corners while closing. A bounded
row-envelope replacement is prepared by root but has not run at this checkpoint.
The next gate is actual transition footage after that fix, before r06 integration.
The physical Computer Use Escape attempt was stopped; a fresh physical wheel
step test is not validated. No new complete input, UI or native motion approval
is claimed.

The independent native finish audit in
`Tools/Unreal/Menu3D/Evidence/native_finish_audit.md` records remaining real-flow,
settings, Back/focus, audio and consistent startup/return work. A reviewed narrow
C++ bridge now exists only as dirty source in MCMainMenuWidget.h/.cpp: embedded
Connection(selectedDay), existing Settings, root Back delegate and optional
native card styling. It is **unbuilt, unloaded and unintegrated**; current Unreal
runtime still uses the existing widget implementation. AGENTS.md requires a
separate user request for Windows build/cook/package; none was run. No network
suite or production startup replacement was performed. Final full UI/backend
and native look/motion approval remain HOLD.

## Historical progress

User approved the complete finish plan on2026-10-16: first a reference-quality
vampire mouth in the actual Unreal3D scene, then Human/Predator, shared motion/UI
polish and existing game-flow integration. Final appearance is compared with
References/B_FullWidth_Approved.png, with anatomy/material targets measured in
mouth-local reference ROIs. Native16:9 keeps three rows; no image stretching.

Current source baseline: v07_LowerRowReadability. Native map L_MainMenu3D, mesh
SK_DAY03_LowerReadable_v07, one-root rig and four morphs, fixed scale0.537654,
gray blocking overrides. Existing character and animations are retained.

Ownership: vampire_remodel owns new v08geometry/UV/export; mouth_textures owns
Painter project/layers/maps; vampire_symmetry_qa owns independent reference and
geometry/look evidence; root owns Unreal materials/lights/camera/integration and
final actual-frame review. No simultaneous writes to a DCC scene. Preserve
unrelated projects and all prior source revisions. No build/cook/package or
network tests without separate user request.

Required geometry:32crowns,8matched pairs per arch,two matching upper fangs,
central seams, rounded cervical seats, real arch/cavity depth, no closed-bite or
nine-pose lower-readability regression. Unique UV0 per material for Painter.
Native contract: same root/morphs/units/axes, enamel0/gum1. Materials use physical
surface maps, not baked concept illumination. Dynamic CardHalfHeight clipping
and existing UI/tick/input fixes must remain usable.

Current phase: USER REJECTED THE FRAGMENTED UV LAYOUT. v08 smart-projected
849-enamel-chart UV is superseded, not approved for baking or final texturing.
Source silhouette/facial-cap cleanup accepted for technical validation; geometry
and nine-pose symmetry passed. Previous frozen packet: source f7a613f17f97ae1681a13325da81fe33488837146d8702536b3b4ff34a7273d6,
FBX 0ae690f36ee0a43683336f916253ee3ab46b9873bf62a85228772e87eb2a3a1b.
No final look accepted. Painter checkpoint/recipe is preserved; final baking and
exports stopped. Producer now owns seam-driven replacement UV under sibling
v08b_SeamUV without geometry/morph changes; interior work temporarily paused.
QA now includes actual UV layout/coherent hero surfaces, distortion, density,
padding and all-surface checker, beyond overlap/finite-value checks.

v08b first seam candidate reduced enamel to64charts/0unsolved/0folds. Root,
producer and texture artist inspected actual front/oblique checker and rejected
its radial dome unwrap: fan-like grid and excessive within-crown density spread.
Producer is testing3logicalcharts (hiddenlingualpanel +rootcap +continuous
facial/incisalbody) without any shape or morph edits. Painter remains held.

Root duplicated the accepted map to /Game/UI/MainMenu3D/L_MainMenu3D_RefFinish_v08
and loaded it. Original map geometry/lights remain intact. Four physical masked
Subsurface material masters (Enamel/Gum/Interior/Tongue) compiled and saved under
/Game/UI/MainMenu3D/ReferenceFinish. They are unbound while art is pending.
Shared WBP header/footer and screen shades now use dark burgundy instead of the
gray blocking palette; graph/input logic was not changed by that pass.
Initial v08 shape imported into SK_DAY03_Reference_v08 with gray blocking
overrides before the UV rejection. This is a rejected UV draft; replacement
v08b will use a separate mesh asset. Physical cavity/tongue default instances are
bound in the look-dev map pending revised source and actual texture maps.
Verified Painter-binding script is prepared, not executed.
Root bound flat physical parameter defaults solely for native shape/lighting
inspection while UV is repaired; no rejected-UV texture maps were used. A bounded
warm key/fill and reduced violet rim candidate was saved in the v08 work map.
Actual evidence: WidthFit_v08_PhysicalDefaults01 and WidthFit_v08_LightCalibration01
under Assembly/Review/Unreal_FunctionalBlocking_v03_FullRoute. Both show the real
native scene; they are not final PBR quality evidence. Remaining saturation,
clean enamel and old cavity shape need actual maps and the planned cavity pass.
PIE is currently stopped. Original L_MainMenu3D geometry/light baseline remains
preserved; startup /Game/Maps/L_MainMenu was not changed.
Blender MCP currently unavailable; isolated headless is the authoring fallback.
Painter has been launched fresh and its bridge works. Geometry agent moves next
to the actual layered cavity and tongue; root owns native lighting/calibration.
