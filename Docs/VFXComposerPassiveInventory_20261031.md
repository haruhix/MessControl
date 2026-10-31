# Passive VFX → Composer: source audit and migration contract

Date: 2026-10-31. Read-only audit; no runtime code, saved assets, editor state or game settings changed.

## Evidence and limits

- Checkout: `E:/DEVGAME/MessControl`, UE 5.8.1. Code graph project resolved with `list_projects`: `E-DEVGAME-MessControl`.
- Graph queries covered `UMCResultPassives::CompleteCleaning`, `UMCDefenseSprayPassives::ResolveIncomingDamage/ApplySpray`, `UMCPerkComponent::NotifyFoodDelivered`, `AMCPassiveCombatCue::SpawnEcho`; current C++ implementations were then read directly. Five relevant indexed cpp paths reported `metadata_changed`, so graph omissions are not absence evidence.
- The 27 IDs below agree with the explicit current-native-test expected set in `Source/MessControl/Private/Tests/MCDevPassivesTests.cpp:70`.
- The initial read-only SQLite snapshot at **2026-10-31T05:39:25Z** was provisional. It is now superseded by the completed full saved export **`65bcc9cc59894d23ad7b876d4f6fb950`** (finished **2026-10-31T07:07:05Z**): **3,834/3,835** assets current, `build_current=true`. The sole known export exception is the pre-existing unrelated `/Game/NiagaraExamples/Utilities/Blueprints/EUB_SnapToActor` compilation failure; its snapshot is not current evidence.
- Current fully paged `DT_Perks` saved evidence (133 rows; table captured **2026-10-31T07:01:44Z**) positively reads **27 enabled passive rows**, **5 enabled active rows**, and **`VFXProfile=None` on all 27 passives**. Evidence: [SavedEvidence.json](../VFXComposerMigrationAudit_20261031/SavedEvidence.json). These are positive saved property reads, not absence inferred from search; unsaved Editor changes and native rendered payloads still require separate live inspection.
- No `MCPerkProcVFXActor` declaration/file was found in the current source-file inventory or literal source search. The actual native owners are the three classes below; no claim relies on an absent index result.

## Finding

There are **27 passive mechanics, not 27 standalone Niagara effects**. Fourteen rows currently produce dedicated feedback, sharing **13 visual families**: ten `AMCPassiveFeedbackCue` kinds, Lightning, EchoTool and SprayCoating. These dedicated families are procedural/static-mesh renderers, not editable NS assets. Thirteen rows have no separate passive VFX; their presentation is the real tool/food/movement/UI behavior. Existing shared Niagara assets include BrushFoam, SprayMist and WatergunMist.

The current passive presentation path does **not** read `FMCPerkDefinition::VFXProfile`. Changing the row alone cannot make these effects editable. The Composer correctly needs a real gameplay-event adapter before enabling Apply.

## Exact native ownership / call paths

| Native source path | Accepted event and presentation boundary |
|---|---|
| `MCToothStatusComponent.cpp:104` → `UMCDefenseSprayPassives::ResolveIncomingDamage` (`MCDefenseSprayPassives.cpp:111`) | Server checks true incoming damage. ThickEnamel rolls once and can return zero; Shield reduces damage below HP threshold. Cue is spawned only after the successful outcome. |
| Real attack threat/dash notifications → `UMCDefenseSprayPassives::ResolveThreat` (`:142`) | A true dodged, noncancelled source attack and a dash receipt restore stamina; Wind appears only if actual stamina increased. |
| `UMCDefenseSprayPassives::UpdateDashVisibility` (`:169`) | Authority owns the existing hidden-state override, remembers the previous baseline, and emits Vanish/Return only on its own visible transitions. |
| `MCMouthLiquid.cpp:382` / `MCArenaToothGrime.cpp:522` → `UMCResultPassives::CompleteCleaning` (`MCResultPassives.cpp:110`) | Sealed serial receipt, actual dirty→clean transition, live valid contributor. Technical clean excluded; secondary NeatFreak cannot recurse. |
| `MCFoodActor.cpp:421` → `UMCPerkComponent::NotifyFoodDelivered` (`MCRoguePerks.cpp:196`) | Successful delivery updates the existing speed deadline, then emits Speed. |
| Normal weapon path → `WeaponDamage` / `NotifyWeaponHit` (`MCRoguePerks.cpp:145/163`) → `UMCCombatPassiveSubsystem` | Native attack snapshots and accepted-hit receipts own cleave, dash-hit consumption, chain, echo and billiards. Cosmetics do not own damage. |
| `UMCCombatPassiveSubsystem::NotifyWeaponHit` (`MCCombatPassiveSubsystem.cpp:171`) | PowerHit only after the first accepted hit consumed DashScale; not every queried target/empty swing/secondary hit. |
| `UMCCombatPassiveSubsystem::EndAttack` (`:207`) → `Repeat`, `Chain`, delayed `Echoes` | Cleave is physical accepted hits; Thunder uses the native random roll and accepted chain targets; echo is scheduled at the native damage deadline. |
| `UMCCombatPassiveSubsystem::AdvanceLaunch` (`:307`–`:368`) | Billiards cue follows an accepted secondary collision, not the initial launch. |
| `MCInventoryComponent.cpp:743` → `UMCDefenseSprayPassives::ApplySpray` (`MCDefenseSprayPassives.cpp:239`) → `StampTreatment` (`:203`) | Native cone/range/occlusion and 10 Hz dose accounting own treatment. LastingFormula creates/updates an actual native coating receipt. |

## 27-row event matrix

The “Composer events” column distinguishes existing events to migrate from optional new visual hooks. It is not a promise that those hooks already exist.

| ID / name | Native mechanic and source | Current distinct presentation | Composer event plan |
|---|---|---|---|
| `B1_HeavyHit` — Тяжёлый удар | DT WeaponDamage +40%, AttackRate −30%; `MCRoguePerks.cpp:117/145`, `MCInventoryUpgrades.cpp:34` | No dedicated passive cue; actual ordinary tool contact/hit feedback | Optional `AcceptedHit`; default empty. Do not mistake PowerHit for this perk. |
| `B1_LightFoot` — Лёгкая походка | NormalSpeed +8%, StaminaCost +8%; `MCToothMovementComponent.cpp:213/852` | Real speed/stamina/UI, no passive aura | Optional acquisition or moving-status cosmetic only; no existing proc to replace. |
| `B1_DeepBreath` — Глубокий вдох | MaxStamina +20%; movement `:825` | Actual maximum/HUD | Optional acquisition cosmetic; default empty. |
| `B1_BigPortion` — Большая порция | CarryCapacity +2; `MCFoodCollectionComponent.cpp:126` | Actual additional retained food actors/stack pose | Shared pickup/stack context, not a made-up passive spell. |
| `B1_ConfidentThrow` — Уверенный бросок | FoodThrowRange +20%; collection `:212` | Actual throw trajectory | Optional `ThrowReleased` cosmetic uses actual food/carrier frame. |
| `B1_FreshDelivery` — Свежая доставка | SpoilRate −10%; `MCFoodAbsorption.cpp:39` | Actual freshness clock / ordinary food material and indicator | No native distinct VFX; do not manufacture a continuous freshness aura. |
| `B1_GreedyHands` — Руки загребущие | FoodPickupRange +30%; collection `:76` | Actual pickup animation at the true actor | Shared pickup context; optional successful-pickup cosmetic only. |
| `B1_WideBristles` — Широкая щетина | BrushWidth +15%; inventory upgrades `:44` | Shared `NS_BrushFoam` plus native cleaned footprint | Shared brush-contact event; gameplay contact radius is read-only input. |
| `B1_UnderPressure` — Под давлением | SprayRange +25%, SprayWidth +20%; `MCInventoryPassiveTuning.cpp:4/14`, inventory `:770` | Shared `NS_SprayMist` / watergun counterpart and real cone scale | Shared spray loop; native range/width remain authoritative. |
| `B2_ReturnTrip` — Обратный рейс | Accepted delivery → DeliverySpeedUntil; `MCRoguePerks.cpp:196` | Feedback `Speed` | `Delivered` receipt, source PerkID retained. Optional end event needs an actual expiry hook. |
| `B2_CleanBreath` — Чистое дыхание | Full true stain cleanup → +3 HP; results `:125` | Feedback `Heal`, only positive HP delta | `StainHealed`, actual recipient, actual restored amount. |
| `B2_NeatFreak` — Чистюля | Native 20% roll, nearest eligible neighbor, no recursive chain; results `:155` | Feedback `Neighbor`: source→target droplets and arrival | `NeighborCleaned`, actual From/To, optional arrival phase derived from the same receipt, no extra clean call. |
| `B2_LastStroke` — Последний штрих | Actual remaining dirt <15% → ×2 clean rate; results `:91` | No distinct passive cue | Optional contact-accent event, not dirty-target destruction or a new reward. |
| `B2_CleanAcceleration` — Чистый разгон | Full stain cleanup → CleanSpeedUntil; results `:134` | Feedback `Speed` | `StainCompleted`; separate PerkID from ReturnTrip even though current artwork is shared. |
| `B2_RefreshingWork` — Освежающий труд | Dirty→fully clean mouth → heal each live owner of this perk by 20% max HP; results `:141` | Feedback `Heal` on each actual positive-delta recipient | `MouthHealed`; separate PerkID from CleanBreath. No cue at full HP. |
| `B2_ThickEnamel` — Толстая эмаль | True native prevention roll; defense `:111` | Feedback `Enamel` | `DamagePrevented`; no renderer/random roll outside authority. |
| `B2_Shield` — Щит | HP below20%, true damage reduction50%; defense `:117` | Feedback `Shield` | `DamageReduced`; not a permanent shell merely because HP is low. |
| `B2_SecondWind` — Второе дыхание | Registered source attack genuinely dodged while dashing → actual stamina gain; defense `:142` | Feedback `Wind` | `StaminaRestored` with accepted threat identity and amount. |
| `B2_HoudiniDash` — Рывок Гудини | Native dash-owned visibility override and baseline restore; defense `:169` | Feedback `Vanish`, `Return` | `Vanish` / `Return`; renderer/player must remain visible independently of the hidden hero. Death/reset should not emit a false Return. |
| `B2_MightyTooth` — Могучий зуб | Real primary cleave through native attack snapshot; combat `:207/243` | No dedicated cue; ordinary accepted target feedback | Optional `CleaveContact` after accepted Apply, no extra hit query/damage. |
| `B2_StartFromDash` — С места в карьер | Native dash window2s; first accepted ordinary hit +25%; combat `:149/155/171` | Feedback `PowerHit` | `EmpoweredHitConsumed`, once per accepted attack, including cleave fanout. |
| `B2_ThunderGod` — Бог грома | Native roll20%; up to3 unique Food/Ally targets, excludes boss, secondary guard; combat `:263` | Combat `Lightning` procedural camera-facing double strip | `ChainSegment` after accepted secondary hit; real endpoints + deterministic noise seed. |
| `B2_EchoSwing` — Эхо замаха | Snapshot actual tool/contact pose, damage at native +0.5s, secondary damage40%; combat `:207/401` | Combat `EchoTool` captured original static mesh parts, ghost material, swept pose | `EchoTelegraph` / `EchoContact`, actual captured ToolParts and ContactAt; never query current selected tool later. |
| `B2_Billiards` — Бильярд | Actual small-enemy launch episode and confirmed secondary contact30%; combat `:294/307` | Feedback `Billiards` at struck target | `SecondaryContact`, actual collision direction/target; initial-launch event is optional new art, not current cue. |
| `B2_Softener` — Размягчитель | Real sprayed treatment, brush multiplier40% for5s; defense `:191/203` | No separate visual; shared spray mist / ordinary stain | Optional treatment stamp/expiry hooks; adding VFX must not clean/damage again. |
| `B2_Antiseptic` — Антисептик | Real spray-clean dose + cooldown penalty30%; defense `:239`, inventory cycle | Shared spray mist; no dedicated passive NS | Shared spray start/loop/end; preserve exact cooldown/dose/occlusion. |
| `B2_LastingFormula` — Стойкая формула | Native residue3s + standing hazard10s /5DPS, own dose clocks; defense `:203/324` | `AMCSprayCoating` procedural surface film | `CoatingStart/Update/End`, native Point/Normal/Radius/Until; locked hazard footprint and host-driven lifetime. |

## Visual inventory: what actually needs replacement

All geometry routines below are **existing C++ artwork**, not source Niagara assets. Their properties/material identity are requirements for a parity pass, not a new art approval.

| Family / exact renderer | Current technique, identity and lifetime | Migration requirement |
|---|---|---|
| Heal — `MCPassiveFeedbackCue.cpp:149` | Bevelled mint 3D rounded plus, low opposite shoulder accent and three elastic rising volumes; camera-relative body-safe offset;1.05s | Real mesh/sprite NS layers, tooth-context scale/shoulder placement, preserved material family and positive-HP receipt. |
| Neighbor — `:204` | Three staggered closed mint droplets following an actual source→target arch; arrival star;0.75s | Real position-bound NS/trajectory; supply endpoints as LWC Positions. |
| Speed — `:243` | Three blunt joined-volume arrowlets below torso, staggered pop/travel;0.65s receipt | Mesh/sprite NS at true feet/direction, not permanent aura. |
| Shield — `:288` | Elastic teal side plate, rolled curved rim, dent and two late beads;0.65s | Mesh NS or baked plate/arc assets; actual damage-reduction context. |
| Enamel — `:315` | Compact thick cream four-point ping and rounded ricochet beads, not Shield bubble;0.65s | Distinct NS, retain cream material identity and genuine prevented-hit trigger. |
| Wind — `:335` | Three unequal cyan chest/cheek lobes stretching/slowing in sequence;0.65s | Real NS lobes/droplets, incoming recovery energy rather than foot dust. |
| Vanish / Return — `:358` | Native disappearance/reappearance receipts; separate world-owned cue remains visible when hero is hidden;0.65s | Separate real NS compositions; snapshot actor frame for Vanish, actual restored subject for Return. |
| PowerHit — `:392` | Gold curved mass + stars/soft accent at the accepted empowered hit;0.65s | Real NS accent, once per consumed dash bonus. |
| Billiards — `:409` | Two directed colored closed balls and contact stars at accepted collision;0.65s | Real NS with physical contact direction/subject, not an invented launch success. |
| Lightning — `MCPassiveCombatCue.cpp:94` | Two procedural ribbon strips,10 segments, cyan core/soft outer, deterministic `NoiseSeed+floor(Age*20)`,0.20s visible/.24 actor | Real beam/ribbon NS with actual endpoints and seed, without duplicating gameplay RNG. |
| EchoTool — combat cue `:54/83` | Up to3 actual captured tool mesh components, per-LOD translucent vertex alpha, ghost MIC, native overhead/horizontal contact pose,0.24s/.30 actor | Requires captured-mesh/transform binding, not a generic static “tool icon”. Add a dedicated mesh-input adapter or explicitly retain a named native-mesh layer until implemented. |
| SprayCoating — `MCDefenseSprayPassives.cpp:63/76` |48-sided6-ring irregular low-opacity film exactly matching native radius; target-relative point/normal updates; native lifetime | Surface-aligned NS/decal layer with locked Point/Normal/Radius and native start/stop. Decorative layer cannot redefine hazard geometry. |

Material roots currently retained on native CDOs:

- Feedback soft: `/Game/Gameplay/VFX/MI_PassiveFeedbackSoft`.
- Feedback glow: `/Game/Gameplay/VFX/MI_PassiveFeedbackGlow`.
- Solid volume: `/Game/Gameplay/VFX/M_CartoonReceiptSolid`.
- Lightning: `/Game/Gameplay/VFX/MI_PassiveLightning`.
- Captured tool ghost: `/Game/Gameplay/VFX/MI_PassiveGhost`.
- Coating: `/Game/Gameplay/VFX/M_ReactionSoft`.

No gameplay-time MID is required by these current classes. Echo allocates transient vertex-color buffers from retained original tool meshes. Material conversion must preserve depth behavior, lit/translucent identity and body occlusion; replacing everything with bright additive sprites would be a visual redesign, not parity.

## Minimal runtime bridge without ActiveSkillAction

`AMCPerkVFXCompositionPlayer` already does not require an active-action instance. Its prepared data, frame, optional subject and typed values can be supplied by a generic presenter. The active factory currently adds its own action prerequisite externally; passive one-shots can use the same player directly.

1. Add a native **presentation-only event service** (e.g. `UMCPerkPresentationSubsystem`) which resolves `PerkID → saved row VFXProfile → native UMCVFXComposition`, validates and retains immutable snapshots/resolved NS during the location readiness phase. It must not depend on `AMCActiveSkillAction`, inventory selection, RNG or damage methods.
2. Native gameplay owners emit an accepted receipt containing **source PerkID + semantic EventKey + actual subject/frame + server start time + presentation strength/direction + optional endpoints/seed/captured tool parts**. Existing generic `Heal` and `Speed` calls currently erase the originating PerkID; add it explicitly at the call site, do not guess from all owned perks.
3. Preserve existing replication ownership initially: existing replicated cosmetic cue actors can carry the event identity/profile information and dispatch a local composition once on each rendering peer. Alternatively a dedicated replicated event actor can replace them after parity tests. Do not run a second client RNG or replicate particle actors. Maintain the current server-time age behavior for late cue arrival, dedupe receipt IDs, and never restart old effects for late observers.
4. A runtime preset must be class `UMCVFXComposition` in **MessControl**, not the editor-only `UMCVFXRecipePreset`. Editor GUID/layout/source metadata remains outside the runtime class and cook references. Preserve old profile enum values. A saved native composition keeps independent NS rather than exporting a merged graph.
5. Generic semantic event names should be declared by a native provider/context contract. Current active profile validation is limited to Start/Loop/Impact/End/Warning; do not silently label every passive as those five. Either map per-provider semantics explicitly with meaningful labels or extend structural profile validation plus provider-specific allowed-event validation. Merely typing an extra event name cannot create a trigger.
6. One-shots use the existing player with no loop anchor. For coating/buff loops, generalize the follow anchor to `USceneComponent*` and keep optional native Niagara-input synchronization separate; do not create a fake action/Niagara component solely to satisfy the API. Native owner stop/expiry releases the player. `DurationSeconds` remains preview horizon; it must not replace the native10s coating timer or0.5s echo damage deadline.
7. Extend typed event inputs for **LWC From/To Positions**, deterministic seed and captured mesh parts. Current player supports scalar/color/bool/vector overrides and active-loop native Position synchronization; a passive lightning/neighbor provider has no such loop component. A dedicated Position input path is required, not Vector3 substitution or changed material coordinates.
8. Provide authoritative-input policies: coating/safe-zone/danger `Radius/Origin/Normal/Endpoints` are read-only gameplay inputs applied after artistic values. Particle size/tint/timing accents remain artist controls. Current general “artist overrides last” policy is suitable for cosmetic receipts but unsafe for exact hazard/safety footprints or their aggregate transform scale.

## Gameplay/network invariants to preserve

- Enamel native roll and Thunder roll remain exactly in their current server gameplay owners. Cosmetic random noise must use the supplied seed or a local deterministic stream, never consume global `FMath::FRand` and alter gameplay outcomes.
- Cleaning serial sealing occurs before callbacks; technical/no-op/partial cleaning cannot emit completion VFX. NeatFreak secondary cleaning cannot recursively produce another neighbor chain.
- Full-HP/no-stamina-gain cases stay silent. Speed refresh follows true delivery/clean completion, not a per-frame modifier query.
- Houdini restores only its own visibility baseline; no false Return after death/reset/teardown. A hidden subject must not hide its detached world cue.
- Echo uses the original accepted tool geometry and contact pose even after a tool swap, and hits at the existing ContactAt. Damage is not inferred from a renderer completion event or particle collision.
- Billiards uses actual accepted secondary contacts and the existing hit-target set; no additional damage/launch on VFX replay.
- LastingFormula keeps real native cleaning/damage clocks and target-relative anchoring; visual actor/component destruction does not award cleanup or change hazard lifetime.
- Existing feedback coalescing: same subject+kind within0.15s merges strength; caps32 total cues /3 per subject. Separating formerly shared Heal/Speed into PerkID-specific compositions creates a visible coalescing-policy choice. Preserve or explicitly replace that policy; do not silently double all overlapping feedback.
- Cleanup must be pawn/run/world-owned, cancel pending delayed visuals on invalidation, and bound active layers/cues/tails. Native NS lifetimes stay native; safety cleanup remains explicit and diagnostic.
- Network preservation requires source-level review and later authorized network validation. Solo preview/native tests are not multiplayer proof.

## Prewarm/data reachability

Current arena manifest explicitly retains `AMCPassiveCombatCue`, `AMCPassiveFeedbackCue`, and `AMCSprayCoating` CDO resources in `MCPreloadManifest.cpp:185`–`:188`. New default profiles/compositions must become reflected runtime roots on the appropriate shared context/defaults asset and/or per-perk row.

Existing load waves already traverse `DT_Perks → VFXProfile.Slots → reflected soft Composition → native DataAsset.Layers → NS → Niagara renderer materials/meshes`. Keep fields non-Transient/non-EditorOnly. Do not first-load a known passive's NS or create a known material variant at the first proc. Retain prepared resources across the readiness barrier; captured tool mesh families must be reachable before an Echo first appears. Migration must be checked on a fresh first use, not accepted solely because the local Editor cache is warm.

## Bounded migration sequence and review gates

1. **Event/data bridge, no visual change:** introduce the generic presentation context, explicit PerkID/native events, prepared cache and profile reachability. Keep old renderer as explicit legacy fallback while default composition assets are authored. Disabled must suppress only cosmetics; invalid explicit overrides should diagnose/fail closed, not secretly replay a different effect.
2. **Nine simple feedback families:** author real editable NS using the current material/mesh identity for Heal, Speed, Shield, Enamel, Wind, Vanish, Return, PowerHit and Billiards. Compare old/new motion in gameplay camera plus front/side views. Do not mark the old procedural geometry as a Niagara asset.
3. **Two endpoint families:** Neighbor and Lightning require actual Positions/seed bindings and per-segment receipt checks. Verify world-origin rebasing, walls/accepted-target invariants and no client RNG reroll.
4. **Captured Echo:** add a real mesh-binding adapter retaining original parts and contact pose. Static replacement meshes are not acceptable parity for the visible equipped tool. Keep native echo damage scheduling untouched.
5. **Persistent surface film:** coating gets host-driven lifecycle, target-relative follow and locked native footprint. Validate visual boundary against standing-actor predicate and native cleanup/death/perk-removal conditions.
6. **Thirteen stat/shared-action rows:** expose existing brush/spray/pickup/throw contexts and clearly show that there is no standalone passive effect. New acquisition/expiry/cleave accents are separate intentional additions with empty defaults; no permanent invented aura is required to call the migration complete.

Required meaningful checks: keep existing27 mechanic tests/scenarios; disabling/replacing cosmetics leaves HP/stamina/RNG/dirty masks/coins/timers unchanged; same native receipt emits once; old/new material/pose parity clips; immutable prepared snapshot after authoring edits; actual source meshes and all source packages unchanged; missing socket/DI refused visibly; manifest discovery reaches every default and changed composition; first visible use checked; delayed cancellation/owner reset cleans up; exact coating footprint; meaningful network checks deferred until separately authorized.

## Current deliverable status

This document is an audit and implementation plan only. No passive has been migrated in this pass, no source Niagara artwork has been replaced, and no new gameplay/network/art approval is claimed. Air Timeout fixture is outside this task and has its own validated native6/6 Warning/End result.
