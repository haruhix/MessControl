# Project intelligence

The source of truth is the current source and saved Unreal packages. Indexes are derived evidence.

- `codebase_memory` is codebase-memory-mcp 0.11.0, scoped to this project. Resolve the project's name with `list_projects` before graph queries. Use call/dependency graphs for structural exploration, then check the cited source before edits or absence/dead-code claims. Use `check_index_coverage` for cited paths and relevant scopes: UE reflection/export/test macros still produce partial parsing, and some header classes can be mislabeled. Search the actual declarations with rg when a graph result is missing or ambiguous.
- Use the auto-selected code project (`E-DEVGAME-MessControl` in this checkout). Do not create a second index for the same path using a different `name`; upstream may watch only the auto-selected project. Omit `name` when manually calling `index_repository`.
- `messcontrol_assets` indexes `/Game` assets. `asset_search` covers catalog metadata and exported contents; `asset_read` returns properties, Blueprint defaults, graphs/pins/links, subobjects and DataTable rows.
- `asset_references` accepts an asset path, native class path or C++ symbol such as `UMCEquipmentProfile` or `UMCInventoryComponent::ServerSelect`. Combine its results with the code graph for refactors. Package dependencies, catalog references and exported function/object references are distinct evidence types.
- Check `asset_status`, freshness, coverage and pagination. Saved transitive /Game dependency changes invalidate snapshots too (including inherited Blueprint defaults). A stale read queues a refresh when the C++ build is current. When current saved asset evidence is needed after code/configuration/binaries changed, call `asset_refresh(build=true)` and poll `asset_job_status`. Never use an unfinished or failed refresh as current evidence.
- The asset watcher refreshes catalog changes and cached snapshots after saved files settle. Changed C++ invalidates snapshots; it does not trigger a background rebuild during editing. Before a broad refactor, request `asset_refresh(export_all=true)` and await completion; after the refactor, rebuild and refresh.
- The exporter reads saved disk state. Unsaved editor changes, native non-reflected payloads, graphs in external packages and rendered/runtime results require the Unreal MCP or explicit engine inspection. Do not infer exhaustive impact from absent results.
- Use the existing `unreal_epic` connection to inspect/change a running editor. It requires Unreal to be running. Use headless commands when the editor is closed. Package modifications belong in Unreal, with Blueprint compilation, Core Redirects/migration where applicable, builds and relevant gameplay/network checks.
- The editor-only export plugin is `Plugins/MessControlProjectIndex`. It never saves source assets and is excluded from packaged game targets. Generated databases, snapshots and logs live in `Saved/ProjectIndex`; do not commit them.
- CLI fallback: `python Tools/ProjectIndex/index.py status`, `refresh`, `read --asset /Game/...`, `search --query ...`, or `references --query ...`. Setup: `Tools/ProjectIndex/Setup.ps1`.

# Unreal and DCC MCP tools

Codex has project-configured MCP connections for Unreal Engine, Blender, Adobe Substance 3D Painter and Adobe Substance 3D Designer. Prefer the relevant MCP for application inspection and authoring; discover its tools and check the live connection before choosing a fallback.

- `unreal_epic`: inspect and edit the running Unreal Editor, including assets, materials, Blueprints and editor/runtime state. Use `messcontrol_assets` alongside it for saved package evidence; the asset index does not replace live editor inspection.
- `blender`: inspect and author meshes, UVs, scene objects and Blender materials; execute Blender Python and capture/render previews. Blender must be running with its MCP add-on connected.
- `substance_painter`: inspect texture sets and layers, author fills/masks/materials, bake mesh maps and export textures through the running Painter bridge.
- `substance_designer`: inspect and author procedural graphs, connect nodes, tune parameters, compute and export texture outputs through the running Designer bridge.
- MCP configuration does not guarantee a live connection or that every advertised tool is exposed in the current session. Check available tools/status, report a concrete connection limitation, and use an appropriate engine/CLI fallback when necessary. Preserve unrelated open projects and unsaved application edits.

# Validation scope

Implement and integrate small mechanics promptly. Collect gameplay, network and regression checks into larger batches when testing is requested or a substantial batch is ready. Do not automatically start broad test suites, repeated playthroughs, builds or full index refreshes after each small update. Perform only the authoring steps needed to make the change usable, such as saving packages, compiling an edited Blueprint or baking changed collision. Report deferred validation honestly. Windows build/cook/package is a separate task and runs only when the user requests it.
