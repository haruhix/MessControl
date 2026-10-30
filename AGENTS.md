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

Implement and integrate small mechanics promptly. By default, validate changes with a short local gameplay check in solo PIE, as they behave in the game. Run network or multiplayer tests only when the user makes a separate explicit request for them; a substantial batch of changes is not authorization. Do not automatically start broad test suites, repeated playthroughs, builds or full index refreshes after each small update. Perform only the authoring steps needed to make the change usable, such as saving packages, compiling an edited Blueprint or baking changed collision. Report deferred validation honestly. Windows build/cook/package is a separate task and runs only when the user requests it.

# Change descriptions and changelog

Every commit description and pull request description must include a concise changelog of what was added or changed. State the resulting behavior, new animations/assets/tools and the validation actually performed. Identify previews awaiting approval and deferred integration or validation explicitly. Update descriptions when the final scope changes; do not describe abandoned work as delivered.

# Resource readiness

- Every new or changed runtime material, material instance, texture, mesh, Niagara effect and widget brush must be reachable by the loading/prewarm manifest for each location that uses it. Prefer reflected references in the relevant map actors, data tables/assets or shared UI theme; register lazy native-only roots explicitly in `MCPreloadManifest` or the per-map preload settings.
- Create and retain required dynamic material variants before that location's readiness barrier completes. Do not compile or first-load a known gameplay visual from its first interaction when it can be prepared during loading.
- For changed visuals, check manifest coverage and the first visible use in a short local run. Report late shader/streaming work and distinguish Editor shader compilation from packaged PSO/resource preparation; a warm local cache alone is not proof of a clean first launch.

# VFX research and authoring

- For new or substantially improved VFX, research practical tutorials and original artist breakdowns before authoring. Prefer original creators and official Unreal documentation; reuse already reviewed references when they still fit the task.
- Choose techniques that can be reproduced in this project's Unreal version with available tools and assets. Explain which visual elements to adapt, the required assets, and any deferred parts.
- Keep a reusable reference library in `Docs/VFXReferences.md`: source links, applicable effects, implementation notes, and the status of techniques actually tried in MessControl.
- Evaluate effects in the gameplay camera and lighting, including clear danger cues, material identity, timing and particle/transparency cost. Use short preview videos when motion is important and recording is practical.

# Character, prop and animation visual acceptance

- Every held tool or active-skill prop needs explicit grip/contact frames and a clear role for each hand. Derive them from the actual mesh and authored holding guides. Check the evaluated visible hands, not only target coordinates or a prop's actor transform.
- Author the complete action: preparation, held/use pose, release/impact or recoil, and recovery. Include appropriate body, feet and facial reactions. Driving and riding need contact with the seat, controls and support surface.
- Check hand position AND orientation throughout movement. Visible hands must stay on their intended grips; props must not float, pass through the body, or disappear before the action's recovery. Preserve authored model scale and proportions.
- Animation ownership must coexist with physical arms and normal tools. Check interruptions, death, replacement and return to ordinary locomotion; release only the current action's presentation/animation ownership.
- Before calling a result review-ready, the primary agent personally inspects actual runtime motion from the gameplay camera and useful front/side views. Retain short videos/GIFs and representative frames. Functional tests and visual acceptance are separate recorded results.
- Detached hands, missing grips, severe clipping or missing action phases are implementation defects to fix before artistic review. Defect recordings may be shared as explicitly labeled diagnostics; they are not completed previews. Give delegated agents concrete visual corrections and inspect the resulting revision.

# Versioned validation evidence

- Store new recordings, screenshots, frame sequences, renders and intermediate validation media under `E:/CodexWork/MessControl/Validation/<task>/` or another explicit task directory on `E:`. Commit concise Markdown/JSON results and source recipes instead of generated media.
- New media under `Artifacts/` is ignored. Version selected review media only when the user explicitly asks to include those media files in Git; a request to record a preview alone is not a request to version its output.
- Keep required game resources in `Content/` and necessary editable masters in their source folders. Do not classify runtime assets as disposable without source and saved-asset reference checks.
- Preserve local files when excluding generated media from an unpublished commit. Existing tracked media and published LFS history are a separate migration; do not rewrite or purge them during an ordinary commit/push.
