# Three-mouth native accordion tools

The current saved preview is `/Game/UI/MainMenu3D/L_MainMenu3D_RefFinish_v08`.
The older `/Game/UI/MainMenu3D/L_MainMenu3D` preserves the gray blocking map.
See `Docs/MainMenu3DBlocking.md` for scope, controls and validation limits.

Current narrow continuity repair: `keep_mouth_morph_buffers_v09.py` journals
changes to the entry-reachable jaw driver; `fit_interior_own_jaw_envelope_v09.py`
and `fit_interior_own_jaw_mask_v09.py` fit the separate cavity/tongue envelope.
These immutable one-shot authoring scripts must not be rerun over their saved
receipts. Read the frozen `work_state.md` and saved Blueprint graphs first.
`record_vampire_lower_review.py` records actual native transitions; final
sampled geometry gates are LocalBuffers15, LocalFullRoute16 and SlowCorners17.
Final1:1 art, high-frame-rate flicker and production/backend integration remain
unapproved. Source evidence is `Evidence/v09_morph_buffer_source_analysis.md`;
runtime/cleanup receipts are `v09_mouth_keepalive_runtime_policy.json` and
`v09_interior_continuity_saved_editor_state.json`.

Current authoring helpers, run only against task-owned assets with PIE stopped:

- `author_functional_blocking.py`: initial gray-material/known-mouth setup; apply the compact stage afterward.
- `compact_accordion_stage.py`: current per-row clipping and original character instances.
- `extend_seven_day_blocking_scene.py`: unassigned future-mouth instances and known-day cavity copies.
- `implement_three_mouth_accordion.py`: current native seven-row graph/layout authoring.
- `fix_boundary_card_cull.py`: narrow saved-graph boundary cull, including attached component hiding; preserves the existing input and transition graphs.
- `fix_menu_tick_order.py`: mouth/character component Tick prerequisites and zero-delta pointer filtering.
- `normalize_vampire_stage_width.py`: historical v05 scale fitting. Do not rerun over the newer symmetric revision; its animated scale coefficients are obsolete.
- `import_vampire_gum_width_v05.py`: historical gum-only mesh import.
- `import_vampire_symmetric_v06.py`: shared new-revision FBX importer with skeleton, morph and material checks; defaults to the frozen v06 candidate.
- `import_vampire_lower_row_v07.py`: bounded lower-jaw lift revision using that shared importer.
- `finalize_accordion_enter_focus.py`: historical compact-control pass; its input graph predates the current accordion fixes. Do not rerun it over the current saved Blueprint.
- `save_menu_skeletons.py`: persist the three required imported skeleton packages.
- `audit_saved_menu_dependencies.py`: read-only registry dependency closure into Evidence.

`menu_native_client.py` uses the already tracked `Tools/Unreal/native_mcp_client.py`.
Editor Python scripts use `Tools/Unreal/remote_python.py`. No engine build is needed
to open the saved map. These tools use the task's original local source paths.

Review helpers include `drive_blocking_review.py`, `inspect_native_pie.py`,
`capture_native_blocking.py` and `record_accordion_route.py`. The latter drives
the shared native Blueprint functions on real engine ticks; it does not prove
physical input delivery. Run08 with the repaired DAY03 mesh passed all19 settled-state checks and was captured
from the actual PIE viewport. `validate_native_route.py` checks settled route
states, `build_transition_contact_sheet.py` aligns actual frame timestamps to the
route, and `build_native_review_video.py` encodes the measured capture timing.
The current route helper requests native engine `Shot SHOWUI` frames after the
viewport render. The older direct Slate screenshot capture is retained for
diagnosis; an independent Slate redraw may pair updated UI with a prior scene.
Physical input has separate receipts. Capture receipts are historical observations,
not final-art approval. See the project document for remaining validation limits.

The lower-row repair uses `record_vampire_lower_review.py` with
`Evidence/lower_review_request.json`: real solo PIE opening/closing at explicitly
slowed game dilation0.15, then normal-speed day transitions and Back/Main/Play.
The request may narrow the schedule/duration for a later bounded visual repair.
Pointer hit delivery is isolated only during the controlled render recording;
the script restores prior root visibility and dilation1 on completion or error.
`validate_lower_row_review.py --phase-qualified` checks the full13-command route
after world updates, retaining command-before-next-tick observations separately.
It does not validate the shorter Run05 schedule. `build_lower_row_contact_sheet.py`
only crops/annotates recorded frames for inspection. `predict_v06_stage_fit.py`
predicts width/mask retention from source geometry; actual native renders and
independent crown/gum ray audits are the acceptance evidence.
Run04 is the current full route, Run05 verifies the final Day3 Hidden-layout
repair on first UI reveal. `verify_vampire_review_runtime.py` confirms that the
ordinary interactive scene is restored; `fit_vampire_cavity_v07.py` fits only
the existing cavity/tongue child volumes. Latest revisions are saved locally.
