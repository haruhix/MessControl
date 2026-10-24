# MessControl Unreal project context

*Engine association: UE 5.8 · Local engine: 5.8.1 · Updated: 2026-10-23*

This context is derived from the current checkout. Current source and saved Unreal packages are authoritative; indexes are derived evidence. For the human-facing Russian documentation, start with [Docs/README.md](../Docs/README.md) and [Docs/Architecture.md](../Docs/Architecture.md).

## Engine and project

- Root: `E:/DEVGAME/MessControl`; descriptor: `MessControl.uproject`.
- Association: `5.8`. Local engine: `E:/UE/UE_5.8`; `Engine/Build/Build.version` records 5.8.1, CL 56057345, branch `++UE5+Release-5.8`. Local engine modifications/distribution provenance: not audited.
- Category Games, project name `Mess Control`, version `0.1.0`. Cooperative third-person game with a mouth arena and a shared hub. The descriptor's seven-day tagline is product intent, not evidence of seven completed destinations.
- Win64 Game/Editor tooling exists. No explicit TargetPlatforms array; final shipping platforms/certification remain unverified.

## Modules and build targets

| Module | Host / phase | Dependencies |
|---|---|---|
| `MessControl` | Runtime / Default | Public: Core, CoreUObject, Engine, InputCore, EnhancedInput, UMG, Slate, SlateCore, PhysicsControl, PhysicsCore, NetCore, ProceduralMeshComponent, Niagara, AIModule, GameplayTasks, DeveloperSettings, OnlineBase, OnlineSubsystem, OnlineSubsystemUtils |
| `MessControl` | Private dependencies | NavigationSystem, AnimationCore, AnimGraphRuntime, KawaiiPhysics, Chaos, ChaosCore, RHI, RenderCore, ApplicationCore, AudioExtensions, Voice, SignalProcessing, MoviePlayer, LevelSequence, MovieScene, MovieSceneTracks, CinematicCamera |
| `MessControl` | Conditional / dynamic | Private StylizedDynamics only if its local source directory exists; `WITH_MC_STYLIZED_DYNAMICS=0/1`. Editor-only AssetRegistry, NiagaraShader and Niagara/NiagaraShader Internal include paths. Dynamically loaded OnlineSubsystemSteam. Win64 system libraries ole32.lib, uuid.lib |
| `MessControlEditor` | Editor / PostEngineInit | Public: Core, CoreUObject, Engine. Private: MessControl, UnrealEd, NavigationSystem, AIModule, AssetRegistry, PhysicsCore, PhysicsUtilities, MeshDescription, StaticMeshDescription, Chaos, ChaosCore, UMG, UMGEditor, Slate, SlateCore, InputCore, RenderCore, RHI, ImageCore, PropertyEditor, Kismet, MovieScene, GraphEditor, BlueprintGraph, KismetWidgets, Merge, SourceControl, LevelEditor, ToolMenus, WorkspaceMenuStructure, LevelSequence |

Targets: `MessControl` (Game) and `MessControlEditor` (Editor); BuildSettingsVersion.V7 and EngineIncludeOrderVersion.Unreal5_8. No separate Client/Server target files were found in Source. Both modules use ExplicitOrSharedPCHs.

## Plugins

Explicit enabled entries in `.uproject`: KawaiiPhysics, optional StylizedDynamics, DeformerGraph, Metasound, OnlineSubsystemSteam, SteamSockets, Niagara, EnhancedInput, PhysicsControl, ProceduralMeshComponent.

Editor-target-only enabled entries: MessControlProjectIndex, PythonScriptPlugin, EditorScriptingUtilities, SequencerScripting, MCPClientToolset, ModelContextProtocol, AllToolsets. AndroidFileServer is explicitly disabled.

Local plugin descriptors:

- KawaiiPhysics 1.21.0: KawaiiPhysics Runtime/PostConfigInit, KawaiiPhysicsSequencer Runtime/PreDefault, KawaiiPhysicsEd UncookedOnly/PreDefault. Descriptor marks Beta=false, Experimental=false. Vendored snapshot with a documented test include-order patch; the optional KawaiiPhysicsTests module is omitted from its descriptor. See `Plugins/KawaiiPhysics/UPSTREAM.md`.
- MessControlProjectIndex 1.0: Editor/Default, read-only saved-package exporter, no content, depends on Niagara. Private modules: Core, CoreUObject, Engine, AssetRegistry, Json, JsonUtilities, UnrealEd, BlueprintGraph, Niagara, NiagaraEditor. Not part of packaged game targets.
- StylizedDynamics 1.0.0 is installed locally, but excluded by `.git/info/exclude`; it is not guaranteed in a clean clone. Its descriptor marks Beta=true, Experimental=false, with StylizedDynamics Runtime/PreDefault and StylizedDynamicsEditor UncookedOnly/PreDefault. Build.cs enables its integration by directory existence. Its comment records unresolved upstream licensing for this optional local install.
- Maturity and effective transitive enablement of engine plugins were not exhaustively inspected. Do not infer runtime use solely from an enabled descriptor entry.

## Gameplay framework and maps

| Role | Class / configuration |
|---|---|
| Arena GameMode / GameState | `AMCGameMode` / `AMCGameState` |
| Hub GameMode / GameState | `AMCHubGameMode` / `AMCHubGameState` |
| Main menu GameMode / controller | `AMCMainMenuGameMode` / `AMCMainMenuPlayerController` |
| Gameplay controller / player state | `AMCPlayerController` / `AMCPlayerState` |
| Pawn | `AMCToothCharacter`; arena/hub constructors prefer `/Game/Blueprints/BP_PlayerCharacter` if found |
| GameInstance | Engine `UGameInstance` |
| Global configured GameMode | `/Game/Blueprints/BP_MouthGameMode.BP_MouthGameMode_C` |
| Editor startup / game default map | `/Game/Maps/L_Mouth` / `/Game/Maps/L_MainMenu` |

No fresh disk exporter ran in this audit. Native defaults in MCGameMode: bUseSingleDayLoop=false, bUseDayOnePlan=true, bUseAdaptiveDirector=true. Clean live loaded BP_MouthGameMode CDO showed SingleDay=true, DayOne=true, Adaptive=true, DA_SingleDay/DA_Day01/DA_SingleDayDirector and BP_PlayerCharacter_C. Loaded editor state does not certify current dirty source binaries. BeginPlay can enable SingleDay using MCHubRun/MCHubDay and the destination profile, or -MCSingleDay; legacy diagnostic flags can disable it.

Hub normal selection is in the existing Hub world: Gathering → Selecting → Travelling. `L_HubSelection` remains a supported separate map path, not the mandatory current selection route. Destinations are read from `/Game/Gameplay/Hub/DT_HubDestinations`; fallback enables Day 1/L_Mouth only. Clean live loaded rows also showed Day01 enabled with L_Mouth/DA_SingleDay, Day02–07 disabled with no map/profile.

Clean live DA_SingleDay: Nuts → Jellies → Ice → Fog, support 120s per slot, legacy finale=false, portions [3,5,8] then [5,8,9], target 25–35min. Clean live DA_RunRules: MouthHealth=100, Days=7, MaxPlayers=4, InitialArenaTeeth=12 (native default 8); available sockets still limit spawned reserves. Live economy rules: Calcium=false, coins cap 1000000, pending boosters 32, owned cosmetics 256; raw catalog/reward enabled flags do not override runtime eligibility.

## Subsystems and state

- GameInstance: `UMCSteamSessionSubsystem`, `UMCTravelLoadingSubsystem`, `UMCHubProfileSubsystem`, `UMCVoiceChatSubsystem`, `UMCStudioSplashSubsystem` (one-session startup ident gate, declared in MCStudioSplashWidget.h).
- World: `UMCWorldPreloadSubsystem` (tickable), `UMCGroundImpactSubsystem` (tickable), `UMCAnimationComparisonSubsystem`; validation/front-end validation tickable fixtures are diagnostic.
- PlayerState owns Perks and Economy plus score/fish/appearance/session state. CopyProperties handles state carried through travel. `UMCProgressionComponent` remains an inert compatibility shell, not a live XP loop.
- Character owns movement/physics/status, brush contact, grip, fishing, cosmetics, collection, expression, inventory, gaze, feedback and camera components.

## Networking, input and AI

- Authoritative server gameplay, actor/property replication, owned character/controller RPCs, custom CharacterMovement prediction and corrections for stamina/swimming. No Iris/ReplicationGraph configuration was identified in the inspected project configs; the entire engine default stack was not audited.
- Listen-server/Steam room contract up to four players; Steam App ID 480, `MC_GAME=MessControl_Coop_v1`, SteamSockets with IpNetDriver fallback. Steam subsystem path is excluded in PIE/commandlets. No complete host-migration/reconnect guarantee.
- EnhancedInput default PlayerInput/InputComponent. The character creates/binds its input actions and mapping in C++; do not assume a required authored IMC asset. Mouse/right-stick orbit and zoom exist.
- Voice: Voice/OSS + UVOIPTalker, PTT V by default, OpenMic, mute/levels/test; separate MCVoiceSoundClass. Input device selection API returns false; system input settings are available.
- AI: custom BossAIController and PlaytestBotController, navigation via AIModule/NavigationSystem. Configured Recast agents Default (35 radius /144 height) and Boss (60/220). No assumption of BehaviorTree/StateTree/Mass ownership from module availability.
- GAS: GameplayAbilities is not explicitly enabled or linked in the inspected project descriptor/Build.cs. Perks/status/economy use project-specific components; no verified ASC/AttributeSet setup.

## UI, resources and audio

- UMG/native + editable widget packages; shared `UMCGameplayUITheme`, UI data/localization and factories. No explicit CommonUI/MVVM dependency in inspected project module definitions.
- Main menu, StudioSplash, loading overlay, gameplay HUD, hub selection/shop/wardrobe and actor-attached indicators are distinct presentation owners. WorldStatus uses a screen-space WidgetComponent, not world-space rendering. User edits in current source may differ from existing binaries.
- Presentation owners create UI/dynamic material variants; `UMCWorldPreloadSubsystem`, `MCPreloadManifest` and per-map settings discover/retain their resources and wait for readiness. Controller ACK/barriers gate play. Every changed runtime visual must be reachable and cooked. Editor shader quietness is not proof of packaged PSO completeness.
- Palette SFX and scene Music/Ambience use UMCSoundPalette / UMCSceneAudioComponent. Two current/fading layers cap scene voices at four. Current saved sound assignments are not established by source alone.

## Saves and local preferences

- `UMCHubProfileSave` Version=1 wraps economy save Version=1 and fish count. Authoritative host saves in `Saved/HubProfiles/PIE` or `Saved/HubProfiles/Players` under SHA1(identity). Authenticated UniqueId or local LocalProfile is used; unauthenticated remote guests do not get a persistent wallet by name.
- `-MCHubTransientProfile` disables persistent-profile paths for diagnostics. Unreadable/version-mismatched saves are protected from overwrite. Store uses `.tmp` then Move and caches only success.
- Economy contains coins, owned/equipped cosmetics, pending boosters, last applied run ID, calcium charges and pending grill reward. Native calcium rule is false; saved values still need preservation.
- `UMCGameUserSettings` holds local graphics/audio/language/camera/voice/bag-color preferences. This is separate from authoritative economy and not a complete saved campaign/run-resume system.

## Rendering and cook configuration

AutoExposure=false, MotionBlur=false, AA method=2, dynamic GI=0, reflections=2, virtual shadows=0, Substrate=true. Physics substepping: 1/120 max step, 32 substeps, MaxPhysicsDeltaTime≈0.2667. Saved postprocess/lights can override project settings.

Configured cook maps: L_Mouth, L_Tutorial, L_MainMenu, Hub, L_HubSelection. AlwaysCook directories: Data, Art, Audio, Gameplay, Hub/VFX, UI/MainMenu3D, Cataclysm. Packaging config is Development, FullRebuild=true, InternationalizationPreset=All with explicit ru/de/fr/es/pt-BR/zh-Hans/zh-Hant/ja/ko cultures. Cook reachability is not proof of resource readiness or packaged runtime success.

## Conventions and tooling

- Public/Private module layout; F/U/A/E/I-style names and per-declaration MESSCONTROL_API / MESSCONTROLEDITOR_API. Reflected owned object references frequently use TObjectPtr, while raw/weak pointers also exist. This is observed practice, not a newly imposed universal rule. Formal assertion/review policy: unknown beyond AGENTS.md.
- Source/configuration/package authoring: prefer relevant MCP if exposed/live. `Tools/Unreal/remote_python.py` is the existing editor-Python fallback; preserve unrelated unsaved application work.
- Saved index outputs: `Saved/ProjectIndex`, excluded from commits. Resolve graph project with list_projects: auto-selected `E-DEVGAME-MessControl`; never create a second name for the same checkout.
- Graph-first structural exploration, then actual source and check_index_coverage. Reflection/export/test macros cause partial parsing; resolver edges can misidentify common names. Index absence never proves dead code.
- Audit state: code index ready but cited-path freshness metadata_changed; asset catalog 3158, 1343 snapshots schema_changed, build_current=false, capture 2026-10-21. Disk has 3559 content packages. Unreal tools were unavailable initially, then a read-only live connection confirmed this project, L_MainMenu, PIE=false. Bounded clean loaded-object/CDO/table observations are recorded in Docs/ProjectAudit.md; they do not refresh the stale exporter or certify current binaries. No refresh/build/gameplay check was performed.

## Working rules and remaining unknowns

All agent-created files/temp/cache/media go to E:, with explicit process TEMP/TMP where applicable. Preserve user changes and application state. Gameplay changes normally receive a short solo PIE check; network tests need a separate explicit request. Windows build/cook/package is a separate user-requested task. Documentation-only work does not justify those runs. No commit or branch is assumed.

For VFX, research and retain reusable sources in Docs/VFXReferences.md before new/substantial authoring. Resource readiness applies to all runtime visuals, including widgets and dynamic materials. See AGENTS.md for the complete project rules.

Unverified beyond the bounded live reads in the audit: remaining Blueprint defaults/DataTable contents/graphs, complete material preload coverage, first-use runtime behavior of dirty source, full campaign, current network/WAN state, release PSO coverage, packaged save/reload after the latest fix, shipping certification and complete engine-plugin maturity/use. HubSystemsCheckpoint_20261023 retains unfinished packaged validation and PSO/GPU issues.
