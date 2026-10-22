# Menu look development checkpoint — WIP

The active main menu is now `/Game/Maps/L_MainMenu`. Its scene was promoted
from `/Game/UI/MenuShowcase/AnatomyV2/L_MenuAnatomyV2`; that private preview
remains available. The saved draft uses the Round2/B light and material treatment: a neutral front
key, isolated warm rim, reduced broad facial AO, neutral working-tooth enamel,
and separate private hero/tongue material settings. Geometry, object and camera
transforms and the cleaning animation were preserved. Startup, return-to-menu
and cook keep the existing `L_MainMenu` address. The theme's `MainMenu` role
now uses `/Game/UI/MenuShowcase/AnatomyV2/UI/WBP_WashNSmashMenu_AdaptiveV2`.
The active map uses a separate `LS_MainMenuCleaningV2`, with all eight
bindings rebound to `L_MainMenu` and the original GUIDs, tracks and keys kept.
Other UI roles and the palette are unchanged.

This is unfinished art, subjectively about 6–6.5/10 against the concept. The
hands, facial shape, surface response and final lighting still need refinement.
The soft-contact shadow variant was captured for comparison and was not chosen
for this checkpoint because it adds an unattractive brow/chin shadow.

Native solo PIE stills were compared at the same Sequencer frame 217, with
1600×900 requested output and TAA150. See
`Artifacts/MenuLookDev_20261022/Preview_Before.png`, `Preview_WIP.png` and
`WIP_Save.json`. Material graph compilation and parameter readback passed;
saving left no dirty packages. The promoted map and sequence were subsequently
saved and reloaded with all actor/component bindings resolved. Complete motion
polish, a fresh full-project Editor build, cook/package and multiplayer validation
are pending. No runtime rendering migration to SM6, Lumen or Path Tracer is included.

The authoring recipes and profile are under
`Tools/Unreal/UI/MenuLookDev20261022/`. They are opt-in editor tools. Sampling
can be previewed with `Tools/Unreal/UI/preview_menu_render_quality.py`; restore
its process-wide settings before testing another map. Imported Content assets,
compact final import inputs and the Branch9 source files belong to the WIP;
raw frame sequences, local DCC archives and caches do not.

The one-time promotion recipe is `Tools/Unreal/UI/promote_menu_anatomy_v2.py`.
It refuses an existing live sequence and backs up overwritten packages before
authoring. The active and preview maps share art assets but have independent
map layout and sequence bindings; later preview layout changes require a
reviewed update of the active map too. Reports and original package backups
are in `E:/CodexWork/MessControlMenuPromotion_20261022/`.

The short solo PIE integration check passed: native studio intro and loading
gate, all six main actions present, Settings/Join/Credits/Exit-confirmation
pages and Back callbacks, Play to Hub, Ready guidance and return to the new
menu without replaying the studio intro. All eight animation bindings resolve
in the PIE world; the brush moved and coffee cleanup progressed from about
0.06 to 1.0. Buttons were exercised through their actual OnClicked delegates;
physical pointer/gamepad delivery was not tested. The check used the existing
Editor DLL, which predates the colleague's fishing merge. See
`Artifacts/MenuMainMenuPromotion_20261022/Validation.json` and `ActiveMenu.png`.
