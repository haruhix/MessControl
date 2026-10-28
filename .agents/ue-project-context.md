# MessControl Unreal project context

*Engine association: UE 5.8 · Local engine: 5.8.1 · Updated: 2026-10-28 (nine current passives, F3 passive page and brush footprint; other sections retain their dated audit scope)*

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
- PlayerState owns Perks and Economy plus score/fish/appearance/session state. `UMCPerkComponent` owns four active/consumable slots (instance identity, charges and cooldown), separate unique passive IDs without an eight-item cap, and separate tool upgrades. `ActivePerks`/`BoosterQueue` are compatibility projections. CopyProperties carries the canonical loadout and remaining cooldown through pawn/travel handoff; run finalization clears runtime skills while retaining coins. `UMCProgressionComponent` remains an inert compatibility shell, not a live XP loop. Current contract and extension hooks: [Docs/RoguelikeCore.md](../Docs/RoguelikeCore.md).
- Character owns movement/physics/status, brush contact, grip, fishing, cosmetics, collection, expression, inventory, gaze, feedback and camera components.

## Networking, input and AI

- Authoritative server gameplay, actor/property replication, owned character/controller RPCs, custom CharacterMovement prediction and corrections for stamina/swimming. No Iris/ReplicationGraph configuration was identified in the inspected project configs; the entire engine default stack was not audited.
- Listen-server/Steam room contract up to four players; Steam App ID 480, `MC_GAME=MessControl_Coop_v1`, SteamSockets with IpNetDriver fallback. Steam subsystem path is excluded in PIE/commandlets. No complete host-migration/reconnect guarantee.
- EnhancedInput default PlayerInput/InputComponent. The character creates/binds its input actions and mapping in C++; do not assume a required authored IMC asset. Keyboard 1–4 activate skill slots; mouse wheel switches tools, Shift + wheel zooms. Holding either Shift sprints without a dash; either Ctrl requests one dash per fresh press. L3 retains sprint + dash, and the existing D-pad tool bindings remain. Mouse/right-stick orbit exists. The old G/B FIFO activation bindings are retired; a gamepad active-skill layout was not validated by this iteration. The 2026-10-28 Ctrl/Shift split passed 17 focused native tests and an engine-key-injected solo PIE; hardware and multiplayer were not checked.
- Voice: Voice/OSS + UVOIPTalker, PTT V by default, OpenMic, mute/levels/test; separate MCVoiceSoundClass. Input device selection API returns false; system input settings are available.
- AI: custom BossAIController and PlaytestBotController, navigation via AIModule/NavigationSystem. Configured Recast agents Default (35 radius /144 height) and Boss (60/220). No assumption of BehaviorTree/StateTree/Mass ownership from module availability.
- GAS: GameplayAbilities is not explicitly enabled or linked in the inspected project descriptor/Build.cs. Perks/status/economy use project-specific components; no verified ASC/AttributeSet setup.

## UI, resources and audio

- UMG/native + editable widget packages; shared `UMCGameplayUITheme`, UI data/localization and factories. No explicit CommonUI/MVVM dependency in inspected project module definitions.
- Main menu, StudioSplash, loading overlay, gameplay HUD, hub selection/shop/wardrobe and actor-attached indicators are distinct presentation owners. WorldStatus uses a screen-space WidgetComponent, not world-space rendering. User edits in current source may differ from existing binaries.
- The stable `/Game/Gameplay/Roguelike/DT_Perks` is the canonical skill definition table: Kind, metadata/translation keys/icon, rarity, initial charges, cooldown and activation hooks/adapters. Chest, shop and `UMCSkillLoadoutWidget` resolve stable IDs there; the latter is a functional current-theme HUD, not the future final art.
- Presentation owners create UI/dynamic material variants; `UMCWorldPreloadSubsystem`, `MCPreloadManifest` and per-map settings discover/retain their resources and wait for readiness. Controller ACK/barriers gate play. Every changed runtime visual must be reachable and cooked. Editor shader quietness is not proof of packaged PSO completeness.
- Palette SFX and scene Music/Ambience use UMCSoundPalette / UMCSceneAudioComponent. Two current/fading layers cap scene voices at four. Current saved sound assignments are not established by source alone.

## Saves and local preferences

- `UMCHubProfileSave` Version=1 wraps economy save Version=2 (v1 is accepted for migration) and fish count. Authoritative host saves in `Saved/HubProfiles/PIE` or `Saved/HubProfiles/Players` under SHA1(identity). Authenticated UniqueId or local LocalProfile is used; unauthenticated remote guests do not get a persistent wallet by name.
- `-MCHubTransientProfile` disables persistent-profile paths for diagnostics. Unreadable/version-mismatched saves are protected from overwrite. Store uses `.tmp` then Move and caches only success.
- Economy contains coins, owned/equipped cosmetics, pending next-run purchases with slot/instance/charge data, exact retained legacy booster entries, per-run shop stock IDs/epoch/seed, last applied run ID, calcium charges and pending grill reward. One weighted assortment persists for a run; at most four passive positions is a storefront cap, not an ownership cap. New grill rolls pay coins only. Known legacy item receipts convert by authored DuplicateCoins; unknown receipts/purchases are retained with a notice. v1 originals get a byte-exact backup before overwrite; backup/import failure protects the original. Native calcium remains disabled.
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

## Roguelike foundation validation — 2026-10-27

Normal Editor builds succeeded. The targeted native run completed 31 tests (30 clean, one with warnings, zero failures); the warnings concern CPU access on the test Plane fixture. Owned solo PIE checked four slots, nine coexisting passives, charge use, independent cooldown, personal chest choice, full-slot confirmation and actual replacement. A separate owned Hub PIE checked E/open, exact price debit, next-run preparation, close/reopen and stable stock. All use transient profiles and explicit diagnostic grants/wallet/player placement, not an earned full run. Evidence is on E:/CodexWork/MessControl/RoguelikeFoundation_20261027; network, microphone, packaged run and a real-file legacy-profile restart were not checked. Full pre-refactor saved export covered 3618 current assets out of 3619; the old EUB_SnapToActor editor utility failed compilation and is excluded. Post-refactor refreshes are scoped to the relevant tables/WBP/maps; do not assume all other cached snapshots are current.

## Current passives and F3 — 2026-10-28

Production DT_Perks now has109 rows: only9 B1_* passives enabled, all100 previous definitions disabled. This deliberately disables old active/consumable/tool rewards; four active slots remain infrastructure and currently empty. Default tools and cosmetic purchases remain. Use Tools/Unreal/author_batch1_passives.py; historical catalog authoring recipes can re-enable retired content and must not be rerun blindly. Core contracts, exact IDs and remaining18 passive plans: Docs/RoguelikeCore.md sections8.6–8.7.

New modifier stats are appended without changing old enum ordinals: StaminaCost, FoodPickupRange, FoodThrowRange, SprayRange, SprayWidth. CarryCapacity supports actual base6→8 (safety cap12); +20% food range uses the ballistic support plane, not a velocity multiplier. Ordinary spray treatment now shares the existing18deg visual cone; pressure increases reach/width independently and preserves blockers. Retired owned state is inert and quarantined; pending paid legacy purchases remain unapplied, never silently mapped/refunded. Shop repairs stale retired stock once in its existing run epoch; valid stock stays stable. Calcium remains disabled and grill rewards remain coins.

F3 -> Passives opens a separate scroll page with current enabled usable Passive rows, descriptions, owned state and typed grant/remove buttons. Mutation uses existing local-host/non-Shipping AMCGameMode guard, no new RPC; Hub/client view is read-only. Stable row tokens reject stale/disabled/ambiguous commands. No director/day/wallet/purchase reset. Skill HUD is intentionally hidden with the cursor while F3 is open; confirm chip changes after closing it. Passives UI reuses master theme/fonts and procedural rounded Slate brushes, no new runtime assets.

Build07 successful, Native04 all16 targeted tests passed without warnings. SoloD arena, HubShopB and F3SoloD passed using actual handlers/Slate clicks and disclosed transient diagnostic fixtures; root reviewed screenshots. F3 verified9 rows, real grant/remove and 100→120→100, HUD after closing, Back/F3/reopen without auto-grant. Remaining six individual perk icons/final art, full earned run, network, packaged/clean-cache validation and real legacy profile restart are deferred. See Docs/Validation/PassivesBatch1_20261028.md; media/reports live on E:/CodexWork/MessControl/PassivesBatch1_20261028. Four relevant saved tables are refreshed after the C++ batch; other snapshots remain stale rather than presumed current.

## Brush footprint validation — 2026-10-28

Ordinary floor/enamel cleaning now uses a 60cm mask radius, replacing36cm. `UMCEquipmentProfile.BrushCleaningRadius` is the authored value (finite1–120cm, shared60cm fallback). `UMCInventoryComponent::CleaningRadius` retains Mesha/perk scaling; it does not alter brush contact acquisition reach, the existing5/3dose or fivefold broken-tooth repair duration. New default is verified on both the loaded saved DA_Equipment and the native CDO. No visual resources or source packages changed. The editor-only equipment review API swaps cached profiles only for the local pawn in the active standalone PIE; it does not mutate source assets or gameplay masks/readiness.

Final Editor build and17 focused native tests passed (3 test-Plane warning successes). Complete, matched gameplay pairs including native clean status and natural liquid hiding measured9.72→6.87s,16.27→11.69s,83.78→38.96s (28–54% shorter, not a universal40%). Use `E:/CodexWork/MessControl/BrushCleanupRegression_20261028/Results.json` and the latest Docs/Validation.md entry for exact accepted cases. The initial narrow footprint test and overpacked synthetic spawn plane were corrected without changing runtime spawn rules. Earlier harness failures remain failed; H's large baseline exceeded60s and was repeated successfully in J at120s. All scenarios are disclosed local fixtures with ordinary primary/movement input, not hardware/network/full-run certification. Media stays local on E:, outside Git.
