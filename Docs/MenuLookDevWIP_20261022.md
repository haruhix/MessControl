# Menu look development checkpoint — WIP

The private preview is `/Game/UI/MenuShowcase/AnatomyV2/L_MenuAnatomyV2`.
Its saved draft uses the Round2/B light and material treatment: a neutral front
key, isolated warm rim, reduced broad facial AO, neutral working-tooth enamel,
and separate private hero/tongue material settings. Geometry, object and camera
transforms, the cleaning animation and normal startup routing were preserved.

This is unfinished art, subjectively about 6–6.5/10 against the concept. The
hands, facial shape, surface response and final lighting still need refinement.
The soft-contact shadow variant was captured for comparison and was not chosen
for this checkpoint because it adds an unattractive brow/chin shadow.

Native solo PIE stills were compared at the same Sequencer frame 217, with
1600×900 requested output and TAA150. See
`Artifacts/MenuLookDev_20261022/Preview_Before.png`, `Preview_WIP.png` and
`WIP_Save.json`. Material graph compilation and parameter readback passed;
saving left no dirty packages. Reload of the saved look, complete motion polish,
a fresh full-project Editor build, cook/package and multiplayer validation are
pending. No runtime rendering migration to SM6, Lumen or Path Tracer is included.

The authoring recipes and profile are under
`Tools/Unreal/UI/MenuLookDev20261022/`. They are opt-in editor tools. Sampling
can be previewed with `Tools/Unreal/UI/preview_menu_render_quality.py`; restore
its process-wide settings before testing another map. Imported Content assets,
compact final import inputs and the Branch9 source files belong to the WIP;
raw frame sequences, local DCC archives and caches do not.
