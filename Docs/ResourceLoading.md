# Resource preparation

The local presentation barrier is shared by the native main menu, Hub, the shared mouth selector, tutorial and arena. `UMCWorldPreloadSubsystem` belongs to one game/PIE world. Preparation never pauses the engine, starts a director, or spawns gameplay actors.

## Loading flow

1. Collect current saved/runtime actor and component references, selected day/profile roots and configured additions. Traverse reflected DA/DT fields, nested structs and containers in asynchronous dependency waves. Keep completed handles and loaded objects referenced until this world is deinitialized. Destination maps and streamed scene audio are not expanded by the visual collector.
2. Wait for requested streaming levels and stable world contents. Prepare actual world primitive bindings and temporary hidden mesh/material proxies. Proxies have no collision, navigation or animation/gameplay tick.
3. In Editor/PIE, process and wait for outstanding asset/shader compilation. Cooked Windows games instead use cooked material shaders and runtime PSO precaching. Wait for outstanding PSO work and quality-appropriate texture/mesh streaming targets, then stable render/RHI fences. Destroy preparation proxies after completion, retain the assets.
4. Only then send the existing gameplay/tutorial/Hub loaded acknowledgement. The existing team barrier controls director startup. The screen distinguishes local preparation from waiting for the other players and uncovers the first perk or tutorial Ready UI when that server state arrives.

`UMCTravelLoadingSubsystem` supplies a separate asset-free Slate/MoviePlayer screen during blocking packaged map travel. It auto-completes when that map load ends; the per-world screen handles the remaining preparation. MoviePlayer is disabled for PIE and does not certify visual readiness. Seamless travel uses the world presentation screen and a new world-scoped coordinator.

Failures and timeouts do not send a ready acknowledgement. The player can retry explicitly or return to the main menu. The asset bar reports discovered resource requests only; it is not a percentage of all GPU preparation. More dependencies can be discovered in a subsequent wave.

## Designer settings

Project Settings → Game → Resource preparation (`UMCPreloadSettings`, Game config):

- `CommonAssets`: additional visual asset or DA/DT roots for every map.
- `Maps`: additions for a short map name, without PIE prefix.
- `TimeoutSeconds`: preparation deadline, default 120 seconds.
- `TargetTextureSize`: initial ordinary texture target, default 1024 pixels, subject to existing quality/cook limits.
- `TextureBudgetMB` / `MeshBudgetMB`: additional streaming request budgets, defaults 256 / 64 MB.

These budgets do not cap all CPU asset memory, total VRAM or previously resident data. Earlier loading can increase memory residency. Existing user quality settings and the texture pool are preserved. Additional config-only soft paths still need valid cook coverage; preload cannot recover a missing packaged asset.

The native manifest includes current day food/profile dependencies, gameplay UI, artist overrides in loaded world components, and current procedural liquid/jelly material paths. It does not scan or load all of Content. Future custom maps should provide their own roots through settings.

## Diagnostic evidence

`LogMCWorldPreload` records `MC_PRELOAD_PHASE`, dependency counts, `MC_PRELOAD_RENDER_WORK`, `MC_PRELOAD_STREAM_TARGETS`, periodic `MC_PRELOAD_WAIT`, exact `MC_PRELOAD_MISSING` paths and `MC_PRELOAD_FAILED` reasons. A non-rendering/NullRHI check explicitly reports `MC_PRELOAD_ASSETS_ONLY` and cannot count as visual validation.

The old Steam 480 package log dated 2026-10-18 contained 50 PSO creation hitches, only 7 precached; it also selected a 1000 MB texture streaming pool. These observations motivate preparation and profiling. They do not establish that all texture pop-in is caused by PSOs or pool exhaustion.

## Validation status and limits

Editor builds passed on 2026-10-20. All four focused `MessControl.Loading.Manifest.*` tests passed, covering food table dependencies/deduplication, excluded destination worlds/audio, UI map/soft-class references and native class defaults. The final combined run also passed all 16 selected SingleDay sequence/post-nut tests (20 successful test records in total).

Rendering solo PIE probes passed for L_Mouth (six checks), Hub (four) and L_MainMenu (four). They observed the actual non-ready overlay, native preparation phases, Ready and the uncovered normal UI; the mouth probe selected the ordinary first perk and observed naturally issued food. The main-menu capture covers the native menu, not the shared 3D mouth selector or a complete travel chain. Root and a second reviewer inspected the actual captures. The silent loading preview preserves the captured wall timing.

A separate rendering solo probe injected one nonexistent path into the unsaved native settings CDO. Nine checks passed: Failed persisted, the error screen exposed Retry, exact settings were restored before normal Retry, then preparation reached Ready and uncovered the real first-perk UI. Configuration hashes stayed unchanged. This checks the normal Retry callback, not a physical button click or Return travel. The fresh run prepared 640 resources including the new mint/Frost Nova dependencies.

Evidence: `E:/CodexWork/MessControlResourceLoading_20261020/FocusedTests_LastRun.json`, `SoloMouth01/Report.json`, `SoloHub01/Report.json`, `SoloMainMenu01/Report.json`, `SoloFailureRetry03/Report.json` and `Previews/LoadingInitial/LoadingInitial.mp4`. Initial probe/tool failures are retained separately and are not reported as gameplay passes.

Still deferred: the complete native main menu → Hub → shared selector → arena travel chain, tutorial entry, Return travel, repeat-travel input restoration, packaged first-launch measurements and multiplayer. The current authorization covers Editor and solo only. Test a cold packaged start separately before claiming packaged shader/texture behavior fixed. Avoid clearing unrelated global caches.

The startup team barrier does not add invulnerability to a player joining an already active hazardous event. Ordinary virtual-texture page residency and every possible procedural/Niagara pipeline are not guaranteed by representative mesh proxies. Existing hazard visibility safeguards (`UsePSOPrecacheRenderProxyDelay=false`) remain in place. Further PSO cache coverage or streaming budget tuning should be based on packaged measurements.

The mint provides a concrete targeted extension: it prepares its exact Frost
Nova Niagara controller and shard/material/instance layout during the candy
fall, before its first pulse. A fresh rendering check exposed a first-visible
ISM artifact despite asset readiness. Retaining its zero-footprint render proxy
and sending incremental instance updates removed that observed dark onset in
`SoloMint06`. Generic representative proxies alone are not a guarantee for
every later procedural effect. See `VFXReferences.md` for the isolated evidence.

## Engine references

- [Epic: PSO precaching](https://dev.epicgames.com/documentation/en-us/unreal-engine/pso-precaching-for-unreal-engine): wait for outstanding precache work, validate missed/late PSOs, assess startup memory/thread cost.
- [Epic: Texture streaming overview](https://dev.epicgames.com/documentation/en-us/unreal-engine/texture-streaming-overview-for-unreal-engine): mip selection depends on viewpoints, quality and the memory pool.
- [Epic: Texture streaming metrics](https://dev.epicgames.com/documentation/en-us/unreal-engine/texture-streaming-metrics-in-unreal-engine): measure required versus available pool before changing budgets.
- API signatures checked against local UE 5.8 source: `ShaderPipelineCache.h`, `StreamableRenderAsset.h`, `StreamableRenderResourceState.h`, `AssetCompilingManager.h`, `ShaderCompiler.h`, `MoviePlayer.h`.
