# Карта документации MessControl

Актуализировано **2026-10-23**. Документы проекта хранятся в репозитории; основной вход — [README проекта](../README.md). Содержание старых отчётов сохранено с их датой и областью проверки.

## С чего начать

1. [README](../README.md) — запуск, текущий маршрут и основные возможности.
2. [Architecture](Architecture.md) — владельцы состояния, модули, сеть и системные связи.
3. [GDD](GDD.md) и [SystemsDesign](SystemsDesign.md) — фактические игровые контуры и границы дизайна.
4. [HubLobby](HubLobby.md), [InventoryAndHUD](InventoryAndHUD.md), [ResourceLoading](ResourceLoading.md) — текущие прикладные контракты.
5. [ContentAndTools](ContentAndTools.md) и [SourceCatalog](SourceCatalog.md) — где находятся ассеты, исходники, карты и инструменты.
6. [ProjectAudit](ProjectAudit.md) — охват анализа, состояние индексов и оставшиеся проверки.

Для автоматических агентов: [AGENTS.md](../AGENTS.md) задаёт правила, [.agents/ue-project-context.md](../.agents/ue-project-context.md) содержит сверенный Unreal-контекст.

## Как читать статусы

- **Актуальный обзор** описывает source/config рабочего дерева на дату аудита. Saved overrides и runtime требуют отдельного доказательства.
- **Тематический справочник** сохраняет полезный контракт системы, но не весь его старый баланс/asset values перепроверен. При расхождении приоритет имеют исходники, актуальные пакеты и обновлённые обзоры.
- **Snapshot/checkpoint** фиксирует конкретный этап и его проверки. PASS относится к названной версии/сценарию, не ко всему сегодняшнему проекту.
- **План/preview/WIP** не означает готовую Unreal-интеграцию, художественное утверждение или release validation.

Asset index на дату аудита устарел. Узкие clean live-чтения Blueprint CDO, основных DA и Hub DataTables перечислены в ProjectAudit; они не заменяют свежий экспорт всех пакетов и runtime-проверку. Остальные сохранённые назначения, музыка, UI layouts и preload coverage требуют соответствующих доказательств.

## Актуальные обзоры

| Документ | Содержание / статус |
|---|---|
| [Архитектура MessControl](Architecture.md) | `Architecture.md` — Сверено с source/config 2026-10-23; runtime ограничения — в аудите |
| [Контент, исходники графики и инструменты](ContentAndTools.md) | `ContentAndTools.md` — Сверено с source/config 2026-10-23; runtime ограничения — в аудите |
| [Mess / Control — игровой дизайн](GDD.md) | `GDD.md` — Сверено с source/config 2026-10-23; runtime ограничения — в аудите |
| [Хаб, экономика и профили игроков](HubLobby.md) | `HubLobby.md` — Сверено с source/config 2026-10-23; runtime ограничения — в аудите |
| [Инвентарь, HUD и пользовательский интерфейс](InventoryAndHUD.md) | `InventoryAndHUD.md` — Сверено с source/config 2026-10-23; runtime ограничения — в аудите |
| [Аудит проекта и обновление документации](ProjectAudit.md) | `ProjectAudit.md` — Сверено с source/config 2026-10-23; runtime ограничения — в аудите |
| [Подготовка ресурсов и экран загрузки](ResourceLoading.md) | `ResourceLoading.md` — Сверено с source/config 2026-10-23; runtime ограничения — в аудите |
| [Каталог исходников и карт](SourceCatalog.md) | `SourceCatalog.md` — Сверено с source/config 2026-10-23; runtime ограничения — в аудите |
| [MessControl — игровые системы](SystemsDesign.md) | `SystemsDesign.md` — Сверено с source/config 2026-10-23; runtime ограничения — в аудите |
| [Проверки и исторические отчёты](Validation.md) | `Validation.md` — Сверено с source/config 2026-10-23; runtime ограничения — в аудите |

## Механики и сценарии

| Документ | Содержание / статус |
|---|---|
| [Рогалик: навыки, пассивные эффекты и награды](RoguelikeCore.md) | Целевой контракт новой итерации, 27.10.2026: четыре активных слота, отдельные пассивки и источники наград; открытые решения и статус реализации указаны внутри |
| [Как новый визуал подключён к механикам](ArtistArena.md) | `ArtistArena.md` — Тематический справочник; текущие изменения сверять с SystemsDesign |
| [Бустеры Mess / Control: преимущество с обязательной ценой](Boosters.md) | `Boosters.md` — Тематический справочник; текущие изменения сверять с SystemsDesign |
| [Boss foundation](BossAI.md) | `BossAI.md` — Тематический справочник; текущие изменения сверять с SystemsDesign |
| [Zombie boss presentation](BossIntro.md) | `BossIntro.md` — Тематический справочник; текущие изменения сверять с SystemsDesign |
| [Босс третьей стадии: скелет Auto-Rig Pro](BossPhase3ARP.md) | `BossPhase3ARP.md` — Тематический справочник; текущие изменения сверять с SystemsDesign |
| [Чистка видимых пятен](BrushCleaning.md) | `BrushCleaning.md` — Тематический справочник; текущие изменения сверять с SystemsDesign |
| [Чистка зубов, язвы и контакт с едой](CareAndHandling.md) | `CareAndHandling.md` — Тематический справочник; текущие изменения сверять с SystemsDesign |
| [Катаклизм: волна, плоты или живое желе](Cataclysm.md) | `Cataclysm.md` — Тематический справочник; текущие изменения сверять с SystemsDesign |
| [Changelog](Changelog.md) | `Changelog.md` — Тематический справочник; текущие изменения сверять с SystemsDesign |
| [Вскрывание сундука — черновик на аппрув](ChestLockpick.md) | `ChestLockpick.md` — Тематический справочник; текущие изменения сверять с SystemsDesign |
| [Кофейные лужицы](CoffeePuddles.md) | `CoffeePuddles.md` — Тематический справочник; текущие изменения сверять с SystemsDesign |
| [Кофейный слайм](CoffeeSlime.md) | `CoffeeSlime.md` — Тематический справочник; текущие изменения сверять с SystemsDesign |
| [Кофе / жидкость: цунами](CoffeeWater.md) | `CoffeeWater.md` — Тематический справочник; текущие изменения сверять с SystemsDesign |
| [DA_Tongue: где настраивать язык](DA_Tongue.md) | `DA_Tongue.md` — Тематический справочник; текущие изменения сверять с SystemsDesign |
| [День 0 и базовое главное меню](Day00.md) | `Day00.md` — Тематический справочник; текущие изменения сверять с SystemsDesign |
| [День 1: щётка → завтрак → кофе → застрявшие волокна](Day01.md) | `Day01.md` — Тематический справочник; текущие изменения сверять с SystemsDesign |
| [Enemy weapon contacts](EnemyWeaponContacts.md) | `EnemyWeaponContacts.md` — Тематический справочник; текущие изменения сверять с SystemsDesign |
| [Четыре игровые механики: где искать и как проверить](FirstFourSystems.md) | `FirstFourSystems.md` — Тематический справочник; текущие изменения сверять с SystemsDesign |
| [First Relax before the nut encounter](FirstRelax.md) | `FirstRelax.md` — Тематический справочник; текущие изменения сверять с SystemsDesign |
| [Потасовка в тумане](FogBrawl.md) | `FogBrawl.md` — Тематический справочник; текущие изменения сверять с SystemsDesign |
| [Коллизия еды из DT_BreakfastMenu](FoodCollisionAutomation.md) | `FoodCollisionAutomation.md` — Тематический справочник; текущие изменения сверять с SystemsDesign |
| [Food knife contact](FoodKnifeHits.md) | `FoodKnifeHits.md` — Тематический справочник; текущие изменения сверять с SystemsDesign |
| [Производительность падающей еды — 04.10.2026](FoodPerformance.md) | `FoodPerformance.md` — Тематический справочник; текущие изменения сверять с SystemsDesign |
| [Прыжок еды к рукам](FoodPickupAnimation.md) | `FoodPickupAnimation.md` — Тематический справочник; текущие изменения сверять с SystemsDesign |
| [Еда, стопки и реакции](FoodRework.md) | `FoodRework.md` — Тематический справочник; текущие изменения сверять с SystemsDesign |
| [Удержание стопки еды](FoodStackHolding.md) | `FoodStackHolding.md` — Тематический справочник; текущие изменения сверять с SystemsDesign |
| [Gameplay controls and configuration](GameplayUsability.md) | `GameplayUsability.md` — Тематический справочник; текущие изменения сверять с SystemsDesign |
| [Взгляд и моргание](Gaze.md) | `Gaze.md` — Тематический справочник; текущие изменения сверять с SystemsDesign |
| [Удержание и процедурный хват](Grip.md) | `Grip.md` — Тематический справочник; текущие изменения сверять с SystemsDesign |
| [Hub fishing rod](HubFishing.md) | `HubFishing.md` — Тематический справочник; текущие изменения сверять с SystemsDesign |
| [Hub ground painting](HubGroundPainting.md) | `HubGroundPainting.md` — Тематический справочник; текущие изменения сверять с SystemsDesign |
| [Hub station collision — final validation, 2026-10-23](HubStationCollision.md) | `HubStationCollision.md` — Тематический справочник; текущие изменения сверять с SystemsDesign |
| [Hub swimming](HubSwimming.md) | `HubSwimming.md` — Тематический справочник; текущие изменения сверять с SystemsDesign |
| [Ледяное событие: механика и точки изменения](IceEvent.md) | `IceEvent.md` — Тематический справочник; текущие изменения сверять с SystemsDesign |
| [Живая глотка и материалы рта](LivingThroat.md) | `LivingThroat.md` — Тематический справочник; текущие изменения сверять с SystemsDesign |
| [Движение, сцепление и вес](Locomotion.md) | `Locomotion.md` — Тематический справочник; текущие изменения сверять с SystemsDesign |
| [Nut encounter redesign](NutFightRedesign.md) | `NutFightRedesign.md` — Тематический справочник; текущие изменения сверять с SystemsDesign |
| [Перки забега](PerkEffects.md) | `PerkEffects.md` — Тематический справочник; текущие изменения сверять с SystemsDesign |
| [Повреждения эмали игрока](PlayerDamage.md) | `PlayerDamage.md` — Тематический справочник; текущие изменения сверять с SystemsDesign |
| [Стилизованная грязь игрока](PlayerDirt.md) | `PlayerDirt.md` — Тематический справочник; текущие изменения сверять с SystemsDesign |
| [Player melee combo](PlayerMeleeCombo.md) | `PlayerMeleeCombo.md` — Тематический справочник; текущие изменения сверять с SystemsDesign |
| [Second Relax between nuts and jellies](PostNutRelax.md) | `PostNutRelax.md` — Тематический справочник; текущие изменения сверять с SystemsDesign |
| [Task rewards and perks](Roguelike.md) | `Roguelike.md` — Тематический справочник; текущие изменения сверять с SystemsDesign |
| [Один день: первый авторенный фрагмент](SingleDay.md) | `SingleDay.md` — Тематический справочник; текущие изменения сверять с SystemsDesign |
| [Arena arrival, knight pressure, stage boosters and voice](StageRewardsAndVoice.md) | `StageRewardsAndVoice.md` — Тематический справочник; текущие изменения сверять с SystemsDesign |
| [Движение языка, сильный рывок и волна боли](Tongue.md) | `Tongue.md` — Тематический справочник; текущие изменения сверять с SystemsDesign |
| [Язык под весом игроков и предметов](TonguePressure.md) | `TonguePressure.md` — Тематический справочник; текущие изменения сверять с SystemsDesign |
| [Повреждение и ремонт зубов](ToothDamage.md) | `ToothDamage.md` — Тематический справочник; текущие изменения сверять с SystemsDesign |

## Интерфейс

| Документ | Содержание / статус |
|---|---|
| [Управление с геймпада](Gamepad.md) | `Gamepad.md` — Тематический контракт указанного этапа; читать с актуальным InventoryAndHUD |
| [План чистового игрового интерфейса MessControl](UI/GameplayHUDPlan_20261016.md) | `UI/GameplayHUDPlan_20261016.md` — Тематический контракт указанного этапа; читать с актуальным InventoryAndHUD |
| [Gameplay UI — доводка Phase 1, 17–18 октября 2026](UI/GameplayUIPolish_20261017.md) | `UI/GameplayUIPolish_20261017.md` — Тематический контракт указанного этапа; читать с актуальным InventoryAndHUD |
| [Стилевая система игрового интерфейса](UI/GameplayUIStyleSystem_20261016.md) | `UI/GameplayUIStyleSystem_20261016.md` — Тематический контракт указанного этапа; читать с актуальным InventoryAndHUD |
| [Контракт локализации игрового интерфейса](UI/LocalizationContract_20261016.md) | `UI/LocalizationContract_20261016.md` — Тематический контракт указанного этапа; читать с актуальным InventoryAndHUD |
| [Редактирование локализации MessControl](UI/LocalizationEditing.md) | `UI/LocalizationEditing.md` — Тематический контракт указанного этапа; читать с актуальным InventoryAndHUD |
| [HUD с несколькими боссами — проект визуального контракта](UI/MultibossHUDContract_20261016.md) | `UI/MultibossHUDContract_20261016.md` — Тематический контракт указанного этапа; читать с актуальным InventoryAndHUD |
| [Phase 1: игровой интерфейс](UI/Phase1GameplayUI_20261017.md) | `UI/Phase1GameplayUI_20261017.md` — Тематический контракт указанного этапа; читать с актуальным InventoryAndHUD |
| [Здоровье зубов и запас возрождений в HUD](UI/TeethHealthHUDContract_20261016.md) | `UI/TeethHealthHUDContract_20261016.md` — Тематический контракт указанного этапа; читать с актуальным InventoryAndHUD |

## Графика, анимация и физика

| Документ | Содержание / статус |
|---|---|
| [Мягкий Active Ragdoll](ActiveRagdoll.md) | `ActiveRagdoll.md` — Тематический/художественный справочник; preview и integration различаются |
| [Анимация персонажа](Animation.md) | `Animation.md` — Тематический/художественный справочник; preview и integration различаются |
| [Сравнение дополнительного движения](AnimationComparison.md) | `AnimationComparison.md` — Тематический/художественный справочник; preview и integration различаются |
| [Animation physics third-party sources](AnimationPhysicsThirdParty.md) | `AnimationPhysicsThirdParty.md` — Тематический/художественный справочник; preview и integration различаются |
| [Визуальный стиль: материалы и свет](ArtDirection.md) | `ArtDirection.md` — Тематический/художественный справочник; preview и integration различаются |
| [Цветокор по референсу](ColorGrading.md) | `ColorGrading.md` — Тематический/художественный справочник; preview и integration различаются |
| [Бесплатные Niagara-паки для MessControl](FabVFXStudy.md) | `FabVFXStudy.md` — Тематический/художественный справочник; preview и integration различаются |
| [Стилизованные жидкости](FluidShaders.md) | `FluidShaders.md` — Тематический/художественный справочник; preview и integration различаются |
| [Анимации под основной игровой референс](GameplayAnimationPlan.md) | `GameplayAnimationPlan.md` — Тематический/художественный справочник; preview и integration различаются |
| [Native main-menu blocking](MainMenu3DBlocking.md) | `MainMenu3DBlocking.md` — Тематический/художественный справочник; preview и integration различаются |
| [Editing and previewing the main menu](MainMenuPreview.md) | `MainMenuPreview.md` — Тематический/художественный справочник; preview и integration различаются |
| [Зуб, кости и физика](Physics.md) | `Physics.md` — Тематический/художественный справочник; preview и integration различаются |
| [Модель игрока и временный риг коллеги](PlayerAppearance.md) | `PlayerAppearance.md` — Тематический/художественный справочник; preview и integration различаются |
| [VFX lab](VFXLab.md) | `VFXLab.md` — Тематический/художественный справочник; preview и integration различаются |
| [Reproducible VFX reference library](VFXReferences.md) | `VFXReferences.md` — Тематический/художественный справочник; preview и integration различаются |

## Разработка и диагностика

| Документ | Содержание / статус |
|---|---|
| [Разработка по шагам](DevelopmentSteps.md) | `DevelopmentSteps.md` — Справочник/история; команды не выполнялись этим аудитом |
| [Dev-панель механик](DevPanel.md) | `DevPanel.md` — Справочник/история; команды не выполнялись этим аудитом |
| [Live Coding в UE 5.8.1](LiveCodingNotes.md) | `LiveCodingNotes.md` — Справочник/история; команды не выполнялись этим аудитом |
| [Диагностика движений](MotionDiagnostics.md) | `MotionDiagnostics.md` — Справочник/история; команды не выполнялись этим аудитом |
| [Союзные боты для плейтеста](PlaytestBots.md) | `PlaytestBots.md` — Справочник/история; команды не выполнялись этим аудитом |
| [Индексы кода и ассетов](ProjectIndex.md) | `ProjectIndex.md` — Справочник/история; команды не выполнялись этим аудитом |
| [Тест с другом через Steam / Spacewar](SteamTesting.md) | `SteamTesting.md` — Справочник/история; команды не выполнялись этим аудитом |
| [Windows-билд](WindowsBuild.md) | `WindowsBuild.md` — Справочник/история; команды не выполнялись этим аудитом |

## Датированные отчёты и checkpoints

| Документ | Содержание / статус |
|---|---|
| [Approved mechanics — 2026-10-12](ApprovedMechanics_20261012.md) | `ApprovedMechanics_20261012.md` — Snapshot указанной даты; проверки не переносить на текущий checkout |
| [External gum and wall borders — 2026-10-17](ArenaEdges_20261017.md) | `ArenaEdges_20261017.md` — Snapshot указанной даты; проверки не переносить на текущий checkout |
| [New arena integration — 2026-10-17](ArenaRefresh_20261017.md) | `ArenaRefresh_20261017.md` — Snapshot указанной даты; проверки не переносить на текущий checkout |
| [Requested tooth material — 2026-10-17](ArenaToothMaterial_20261017.md) | `ArenaToothMaterial_20261017.md` — Snapshot указанной даты; проверки не переносить на текущий checkout |
| [Подключение анимаций боссов — 12 октября 2026](BossAnimationIntegration_20261012.md) | `BossAnimationIntegration_20261012.md` — Snapshot указанной даты; проверки не переносить на текущий checkout |
| [Feedback 8: cleanup completion and visible dirt](CleanupFeedback8_20261018.md) | `CleanupFeedback8_20261018.md` — Snapshot указанной даты; проверки не переносить на текущий checkout |
| [F3 one-hit kill — 2026-10-17](DevOneHitKill_20261017.md) | `DevOneHitKill_20261017.md` — Snapshot указанной даты; проверки не переносить на текущий checkout |
| [Изменения по фидбеку 5–11](Feedback5_11_20261018.md) | `Feedback5_11_20261018.md` — Snapshot указанной даты; проверки не переносить на текущий checkout |
| [Фаербол: прицеливание в игрока без наземной метки](FireballTracking_20261017.md) | `FireballTracking_20261017.md` — Snapshot указанной даты; проверки не переносить на текущий checkout |
| [Огненный орех: скорость и точка вылета](FireNutHandFix_20261017.md) | `FireNutHandFix_20261017.md` — Snapshot указанной даты; проверки не переносить на текущий checkout |
| [Огненный орех: ускорение и язва боли](FireNutUlcer_20261017.md) | `FireNutUlcer_20261017.md` — Snapshot указанной даты; проверки не переносить на текущий checkout |
| [Food throw and physical throat entry — 2026-10-17](FoodThrow_20261017.md) | `FoodThrow_20261017.md` — Snapshot указанной даты; проверки не переносить на текущий checkout |
| [Food fragment placement — 2026-10-18](FragmentOrigin_20261018.md) | `FragmentOrigin_20261018.md` — Snapshot указанной даты; проверки не переносить на текущий checkout |
| [Hub systems checkpoint — 23 October 2026](HubSystemsCheckpoint_20261023.md) | `HubSystemsCheckpoint_20261023.md` — Snapshot указанной даты; проверки не переносить на текущий checkout |
| [Ледяной босс: две фазы — 13 октября 2026](IceBossStages_20261013.md) | `IceBossStages_20261013.md` — Snapshot указанной даты; проверки не переносить на текущий checkout |
| [Mage summon replacement — 2026-10-16](MageSummons_20261016.md) | `MageSummons_20261016.md` — Snapshot указанной даты; проверки не переносить на текущий checkout |
| [Menu look development checkpoint — WIP](MenuLookDevWIP_20261022.md) | `MenuLookDevWIP_20261022.md` — Snapshot указанной даты; проверки не переносить на текущий checkout |
| [Ореховый катаклизм: обязательный переход к боссу](NutBossGateFix_20261017.md) | `NutBossGateFix_20261017.md` — Snapshot указанной даты; проверки не переносить на текущий checkout |
| [Nut Knight animation update — 2026-10-16](NutKnightAnimations_20261016.md) | `NutKnightAnimations_20261016.md` — Snapshot указанной даты; проверки не переносить на текущий checkout |
| [Nut Knight: second supplied animation set](NutKnightBall_20261016.md) | `NutKnightBall_20261016.md` — Snapshot указанной даты; проверки не переносить на текущий checkout |
| [Nut Knight body volume — 2026-10-17](NutKnightBodyVolume_20261017.md) | `NutKnightBodyVolume_20261017.md` — Snapshot указанной даты; проверки не переносить на текущий checkout |
| [Corrected Nut Knight animations — 2026-10-17](NutKnightCorrected_20261017.md) | `NutKnightCorrected_20261017.md` — Snapshot указанной даты; проверки не переносить на текущий checkout |
| [Knight playback, size and turn-axis correction](NutKnightPlayback_20261016.md) | `NutKnightPlayback_20261016.md` — Snapshot указанной даты; проверки не переносить на текущий checkout |
| [NutRollToothDamage_20261017](NutRollToothDamage_20261017.md) | `NutRollToothDamage_20261017.md` — Snapshot указанной даты; проверки не переносить на текущий checkout |
| [Sequential walnut boss phases — 2026-10-17](NutSequentialPhases_20261017.md) | `NutSequentialPhases_20261017.md` — Snapshot указанной даты; проверки не переносить на текущий checkout |
| [Nut Wizard artist animation update — 2026-10-17](NutWizardUpdate_20261017.md) | `NutWizardUpdate_20261017.md` — Snapshot указанной даты; проверки не переносить на текущий checkout |
| [PIE / packaged parity: targeted audit, 2026-10-18](PackagedParityAudit_20261018.md) | `PackagedParityAudit_20261018.md` — Snapshot указанной даты; проверки не переносить на текущий checkout |
| [Restore the rear arena teeth — 2026-10-18](Validation/MissingRearTeeth_20261018.md) | `Validation/MissingRearTeeth_20261018.md` — Snapshot указанной даты; проверки не переносить на текущий checkout |
| [Nut storm, seated rafts and project integration — 2026-10-17](Validation/NutRaft_20261017.md) | `Validation/NutRaft_20261017.md` — Snapshot указанной даты; проверки не переносить на текущий checkout |
| [Wizard rain and fireball — 2026-10-18](Validation/WizardRainFireball_20261018.md) | `Validation/WizardRainFireball_20261018.md` — Snapshot указанной даты; проверки не переносить на текущий checkout |
| [Wizard walking, rain and teleport correction — 2026-10-17](WizardBehaviorFix_20261017.md) | `WizardBehaviorFix_20261017.md` — Snapshot указанной даты; проверки не переносить на текущий checkout |

## Планы и история

| Документ | Содержание / статус |
|---|---|
| [Беклог переработки язв](BacklogUlcerRework.md) | `BacklogUlcerRework.md` — План/согласованный этап; не доказательство реализации всех пунктов |
| [Живой рот и опасности](GameplayV3.md) | `GameplayV3.md` — План/согласованный этап; не доказательство реализации всех пунктов |
| [Идеи разнообразия орехового эвента](NutEventIdeas.md) | `NutEventIdeas.md` — План/согласованный этап; не доказательство реализации всех пунктов |

## Правила дальнейшего обновления

- После изменения механики обновлять профильный справочник и архитектурный контракт, если меняются владельцы состояния/загрузка/переходы.
- Указывать дату и источник: source, сохранённый package, runtime observation или художественный preview. Не заменять неизвестный saved default нативным значением без оговорки.
- Результаты проверок записывать с точным сценарием и версией; незавершённые проверки сохранять явно.
- Новые материалы/меши/VFX/brushes документировать вместе с preload/cook reachability. Отдельно различать Editor shader preparation и packaged PSO.
- Новые документы добавлять в эту карту. Историю и референсы сохранять; старое обещание не должно выглядеть текущим поведением.
- Рабочие файлы и результаты инструментов хранить на E:. Runtime, сеть и Windows package запускать в рамках соответствующего разрешённого scope.
