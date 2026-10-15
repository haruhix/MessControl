# Three-mouth native accordion tools

The saved preview is `/Game/UI/MainMenu3D/L_MainMenu3D`.
See `Docs/MainMenu3DBlocking.md` for scope, controls and validation limits.

Current authoring helpers, run only against task-owned assets with PIE stopped:

- `author_functional_blocking.py`: initial gray-material/known-mouth setup; apply the compact stage afterward.
- `compact_accordion_stage.py`: current per-row clipping and original character instances.
- `extend_seven_day_blocking_scene.py`: unassigned future-mouth instances and known-day cavity copies.
- `implement_three_mouth_accordion.py`: current native seven-row graph/layout authoring.
- `finalize_accordion_enter_focus.py`: compact control fonts and focused Back/Enter handling.
- `save_menu_skeletons.py`: persist the three required imported skeleton packages.
- `audit_saved_menu_dependencies.py`: read-only registry dependency closure into Evidence.

`menu_native_client.py` uses the already tracked `Tools/Unreal/native_mcp_client.py`.
Editor Python scripts use `Tools/Unreal/remote_python.py`. No engine build is needed
to open the saved map. These tools use the task's original local source paths.

Review helpers include `drive_blocking_review.py`, `inspect_native_pie.py`,
`capture_native_blocking.py` and `record_accordion_route.py`. The last helper's
complete seven-day route was prepared but has not been executed/validated.
Capture and action receipts in Evidence are historical observations, not claims
that the final route or final artwork has passed.
