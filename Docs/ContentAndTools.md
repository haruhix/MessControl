# Контент, исходники графики и инструменты

Срез рабочего каталога на **23.10.2026**. Документ описывает расположение ресурсов, цепочки подготовки и доступные проверки по текущим файлам. Уже работающий Unreal Editor осмотрен только read-only запросами; DCC не запускались, пакеты не пересохранялись, Python, сборка, cook, PIE и тесты не выполнялись.

Архитектура игры описана в [Architecture.md](Architecture.md), загрузка ресурсов — в [ResourceLoading.md](ResourceLoading.md), индексы — в [ProjectIndex.md](ProjectIndex.md). Статусы конкретных художественных работ и проверок нужно читать вместе с датой соответствующего отчёта.

## Состав и границы инвентаризации

| Область | Файлы под контролем Git | Назначение |
| --- | ---: | --- |
| `Content` | 3562, в том числе 3559 `.uasset`/`.umap` | Сохранённые Unreal-ресурсы и карты |
| `ArtSource` | 1594 | Сцены Blender, FBX/GLB, исходные текстуры, аудио, manifests и отчёты передачи |
| `ContentSource` | 71 | Ранние исходники мешей/аудио: 60 FBX, 5 WAV, 4 JSON, 2 Blend |
| `SourceArt` | 14 | Материальные исходники: 4 Substance Designer `.sbs`, 9 PNG, JSON |
| `Artifacts` | 12006 | Отобранные превью, видео, технические отчёты и свидетельства проверок |
| `Tools` | 1283 | Генерация, импорт, редактирование, сборка, проверки, запись и индексация |
| `Plugins` | 198 | Версионируемые локальные плагины и исходные данные экспериментов |

Это количество путей из `git ls-files`, а не размер загрузки репозитория. Фактический рабочий каталог содержит также локальные и игнорируемые данные. На диске в `Content` найдены **3538 `.uasset` и 21 `.umap`**, всего 3559 пакетов; их набор совпал с Git-инвентарём, включая Unicode-пути. В подсчёт входят external actors/objects и демонстрационные пакеты. Идентификатор пакета также не равен одному объекту Asset Registry: разные способы подсчёта не взаимозаменяемы.

Локальные `ArtSource` и `Artifacts` значительно шире версионируемого набора: соответственно 14532 файла / около 18,63 GiB и 67729 файлов / около 48,06 GiB. Эти числа получены по метаданным файлов без чтения тяжёлых медиа; они не означают, что весь материал входит в чистый checkout. В частности, кадры рендера и отвергнутые варианты могут оставаться только локально.

В доступном saved-индексе было 3158 catalog assets, 1343 подробных снимка, все 1343 с `schema_changed`, `build_current=false`; снимок датирован 21.10.2026. Он не подтверждает текущие значения Blueprint/CDO и полное покрытие нынешнего `Content`. Для этой инвентаризации использованы файловая система, конфигурация, код инструментов и существующие отчёты; refresh/build не запускались. Отдельное узкое live-чтение clean CDO/DA и Hub table перечислено в [ProjectAudit.md](ProjectAudit.md); оно не актуализирует общий saved-индекс и не сертифицирует нынешние несобранные исходники.

В live clean `DA_RunRules` прочитаны HP 100, Days 7, MaxPlayers 4, InitialArenaTeeth 12 (native default 8); в `DA_SingleDay` — Nuts → Jellies → Ice → Fog, поддержка 120 с, `OpeningMealPortions=[3,5,8]`, `AfterNutMealPortions=[5,8,9]`, `bLegacyTimedFinale=false`. Эти значения относятся к загруженным объектам, а не к полному новому экспорту пакетов.

## Карты и назначение папок Content

В конфигурации сохранены разные входы: `EditorStartupMap=/Game/Maps/L_Mouth`, `GameDefaultMap=/Game/Maps/L_MainMenu`. Старт игры и обычное открытие редактора поэтому ведут на разные карты.

| Карта | Роль и текущая трактовка |
| --- | --- |
| `/Game/Maps/L_MainMenu` | Основное меню игры; актуальный WIP look-development описан в [MenuLookDevWIP_20261022.md](MenuLookDevWIP_20261022.md) |
| `/Game/Maps/Hub` | Хаб с входом в забег, рыбалкой, гардеробом, магазином и грилем; [HubLobby.md](HubLobby.md), [HubSystemsCheckpoint_20261023.md](HubSystemsCheckpoint_20261023.md) |
| `/Game/Maps/L_HubSelection` | Выбор локации из хаба; явно перечислен в `MapsToCook` |
| `/Game/Maps/L_HubSelectionStage` | Сохранённая сцена представления выбора; её наличие само по себе не доказывает весь runtime dependency closure |
| `/Game/Maps/L_Mouth` | Основная игровая арена |
| `/Game/Maps/L_Tutorial` | Отдельное обучение, день 0 |
| `/Game/UI/MainMenu3D/L_MainMenu3D` | Ранний блокинг трёхмерного выбора рта |
| `/Game/UI/MainMenu3D/L_MainMenu3D_RefFinish_v08` | Контрольная карта reference-finish; DAY01–03 имеют подготовленную графику, DAY04–07 — placeholders, финальный art approval не заявлен |
| `/Game/UI/MenuShowcase/AnatomyV2/L_MenuAnatomyV2` | Частная сцена предпросмотра нового меню, из которой выполнено продвижение в `L_MainMenu` |
| `/Game/Tests/L_VFXLab` | Изолированная лаборатория эффектов и референсных образцов |
| `/Game/Tests/L_CombatImpactCompare` | Сравнение отклика попаданий |
| `/Game/Maps/Props`, `/Game/Level/Props` | Сохранённые карты props; активная игровая роль по имени файла не определяется |

Остальные восемь карт принадлежат импортированным наборам: `Fire_EXP_Vol01_Free/Map/Map_Overview`, `FreeParticle_SoftTofu/Map/Overview`, `Free_Magic/Maps/Free_Magic_Map`, `NiagaraExamples/GalleryLevel`, `NiagaraExamples/Utilities/SpriteGeneration/TestSpriteMap`, `Stylish_Fire_VFX/Demo/Map/Demo_Map`, `Stylized_Fruits/Maps/Stylized_Fruits_Showcase`, `Stylized_Vegetables/Maps/Stylized_Vegetables_Showcase`. Их присутствие не означает использование в основном забеге.

| Папка | Что искать |
| --- | --- |
| `Content/Gameplay` — 764 пакета | Собственные игровые ресурсы: `Arena`, `Boss`, `Care`, `Character`, `CharacterCurrent`, `Cold`, `CoreLoop`, `Delivery`, `FogBrawl`, `Hazards`, `Hub`, `Liquid`, `MouthV3`, `Roguelike`, `Throat`, `UI`, `VFX` |
| `Content/Art` — 314 | Общие меши, риг, материалы, текстуры, UI-арт |
| `Content/Data` — 31 | Общие профили персонажа, оборудования, ухода, физики, языка и дня; таблицы еды/косметики; число включает вложенные файлы |
| `Content/UI` — 162 | Трёхмерное меню и сцены look-development |
| `Content/Generated` — 223 | Подготовленные производные ресурсы; название не является разрешением на удаление |
| `Content/Cataclysm` — 34 | Плот, желейные существа и сопутствующее визуальное представление катаклизма |
| `Content/Audio` — 34 | Импортированные звуки; параметры палитры находятся в DA и native-коде |
| `Content/Hub` — 14 | Дополнительные ресурсы хаба, включая confetti |
| `Content/Tests` — 11 | Карты и ресурсы изолированных визуальных проверок |
| `Content/Blueprints` — 4 | Исходные точки расширения GameMode/персонажа/задач |
| `Content/FromBlender*`, `ProjectHandiment` | Импортированные пакеты коллег и ранние передачи; актуальность проверять по реальным ссылкам |
| `Content/__ExternalActors__`, `__ExternalObjects__` | Внешние пакеты карты; не отдельный художественный набор |

Крупные сторонние/примерные наборы по текущему диску: `NiagaraExamples` — 669 пакетов, `Stylized_Vegetables` — 378, `Stylized_Fruits` — 316, `Free_Magic` — 192, `SoStylized` — 76, `FreeParticle_SoftTofu` — 54, `Fire_EXP_Vol01_Free` — 50, `Stylish_Fire_VFX` — 13. `Fab` содержит ещё 22 пакета. Эти подсчёты характеризуют хранение; они не являются аудитом runtime-использования, стоимости cook или лицензий всех ресурсов.

## Где редактируются данные

Профили расположены не только в `Content/Data`. Например, `DA_SingleDay`, `DA_SingleDayDirector`, `DA_NutRain`, `DT_NutRainMenu` находятся в `Gameplay/CoreLoop`; материалы и Blueprint-ресурсы отдельных механик лежат рядом с ними.

| Задача | Исходная точка |
| --- | --- |
| Общие правила, модель/инструменты игрока | `Content/Data/DA_RunRules`, `DA_PlayerAppearance`, `DA_Equipment` |
| Уход, физика еды и зубов | `DA_ToothCare`, `DA_FoodPhysics`, `DA_ArenaTooth`, `DA_ToothPhysics` |
| Язык, давление и движения | `DA_Tongue`, `DA_Tongue_Jolt`, `DA_Tongue_LocalLift`, `DA_Tongue_Pain`, `DA_Tongue_Travel`; [DA_Tongue.md](DA_Tongue.md), [TonguePressure.md](TonguePressure.md) |
| Еда, косметика | `Content/Data/DT_BreakfastMenu`, `DT_CharacterCosmetics` |
| Обычный день и ореховые волны | `Content/Gameplay/CoreLoop`; [SingleDay.md](SingleDay.md) |
| Игровой HUD, диалоги, загрузка | `Content/Gameplay/UI`; [InventoryAndHUD.md](InventoryAndHUD.md), [ResourceLoading.md](ResourceLoading.md) |
| Гардероб/магазин/гриль | `WBP_HubWardrobe`, `WBP_HubShop`, `WBP_WardrobeCosmeticTile`, `WBP_GrillHologram`; инструменты `Tools/Unreal/Hub` |
| Модели героя и инструменты | `Content/Gameplay/CharacterCurrent`; [ArtSource/CharacterCurrent/README.md](../ArtSource/CharacterCurrent/README.md) |

В `Gameplay/UI` одновременно сохранены `WBP_*_Phase1`, `WBP_*_v10` и некоторые исходные виджеты. Для выбора действующего варианта требуется ссылка в теме/профиле или коде загрузки; наибольший номер версии в имени сам по себе этого не определяет. То же относится к старым `Character`, `CharacterCurrent`, `Art/Rig`, импортам `FromBlender*` и нескольким поколениям материалов.

## Исходники графики и статус передачи

`ArtSource` содержит самостоятельные цепочки, а не один генератор, восстанавливающий весь проект с нуля. Основные группы:

- Арена и ткани: `ArenaGameplay`, `MouthV2`, `MouthV4`, `LivingThroat`, `CareMaterials`, `Fluids`, `CoffeeWater`.
- Герой: `CharacterCurrent`, `CharacterGameplay`, `CharacterAnimation`, `CharacterFace`, `SKWalkIntegration`; дополнительные аксессуары — `CharacterCurrent/Cosmetics`.
- Еда, боссы и события: `Breakfast`, `Cold`, `WinterCandy`, `BossPhase3`, `BossPhase3ARP`, `CarriesStage2`, `CarriesTransformation`, `NutKnight*`, `NutTank`, `NutWizard*`, `NutSummon`, `ZombieBoss*`, `CataclysmRaft`, `CoffeeSlime`.
- UI, меню и бренд: `UI`, `Brand/Branch9`, `Concepts`, `Looks`, `References`.
- Эффекты, награды и звук: `VFX`, `ChestLockpick`, `ChestGameplay_20261022`, `Audio`.

| Работа | Подтверждённый документами статус | Где продолжать |
| --- | --- | --- |
| Текущий персонаж | Исходник коллеги адаптирован к существующему игровому ригу; основной mesh и tools имеют отдельные экспорт/импорт рецепты | [CharacterCurrent/README.md](../ArtSource/CharacterCurrent/README.md), `Tools/Blender/build_character_gameplay.py`, `export_character_tools.py`, `Tools/Unreal/import_character_gameplay.py`, `import_character_tools.py` |
| Косметика | Примеры cap/red cap/glasses интегрированы в таблицу и гардероб по checkpoint 23.10; предыдущие видео с растянутой кепкой заменены новым исправлением | [Cosmetics/README.md](../ArtSource/CharacterCurrent/Cosmetics/README.md), [HubSystemsCheckpoint_20261023.md](HubSystemsCheckpoint_20261023.md) |
| CoffeeSlime v2 | DCC-пакет и пять клипов подготовлены; README фиксирует последующий импорт и 15 узких solo-проверок, без заявления о network/cook/GPU profile | [CoffeeSlime/v2/README.md](../ArtSource/CoffeeSlime/v2/README.md), `Tools/Unreal/import_coffee_slime_v2.py` |
| Трёхмерный выбор рта | Functional/reference-finish checkpoint; sampled continuity PASS, финальные 1:1 art/motion-look HOLD, DAY04–07 placeholders | [MainMenu3DBlocking.md](MainMenu3DBlocking.md), [work_state.md](../ArtSource/UI/MainMenu3D/20261015_refB_v1/Assembly/UnrealHandoff/v08_ReferenceFinish/work_state.md) |
| Новое основное меню | Продвинуто в активную `L_MainMenu`, отдельный sequence rebinding; художественно WIP, требуется дальнейшая работа над руками, формой и светом | [MenuLookDevWIP_20261022.md](MenuLookDevWIP_20261022.md), `Tools/Unreal/UI/MenuLookDev20261022` |
| Свечение сундука | Собственные маски/меши/HLSL и editable construction settings; есть узкие Editor/PIE проверки; shader instruction count не является FPS-замером | [Tools/Art/RewardChest/README.md](../Tools/Art/RewardChest/README.md), [ChestLockpick.md](ChestLockpick.md) |
| Лёд/снежная пудра | Сохранены исходные Kenney-puff PNG, отдельные Niagara/material recipes; эти PNG — вариации, а не кадры flipbook | [KenneySmoke/README.md](../ArtSource/VFX/KenneySmoke/README.md), [VFXReferences.md](VFXReferences.md) |

Статус «есть экспорт», «техническая проверка пройдена», «есть импорт в Unreal», «интегрировано в gameplay» и «художественно одобрено» нужно учитывать раздельно. Папка с суффиксом `Final`, `Approved`, `vN` или сохранённый отчёт не заменяют актуальную проверку загрузки пакета и его использования. Уже существующие manifests сохраняют source hash, параметры единиц, порядок слотов, rig/clip contract и контекст конкретной проверки.

Для передачи нужны компактный исходный пакет, целевой Unreal path, параметры импорта, список изменяемых пакетов, исходные hashes и отчёт технической проверки. Текущие рецепты применяют разные контракты единиц: например, CoffeeSlime v2 держит Blend/GLB в метрах, а FBX — в сантиметровом контракте. Универсальное домножение component scale на 100 нарушает этот контракт.

## Навигация по Tools

Количество версионируемых путей: `Tools/Unreal` — 883, `Tools/Blender` — 254, `Tools/Art` — 33, `Tools/ProjectIndex` — 5, `Tools/DiscordCodex` — 19. Остальные 89 находятся на верхнем уровне или в `Benchmarks`. Локальные `__pycache__` и `node_modules` не входят в эти числа.

| Группа | Входы | Что делает и что меняет |
| --- | --- | --- |
| Сборка | `Build.ps1`, `Rebuild.ps1`, корневой `RebuildProject.bat` | Сборка Editor/Game; Rebuild очищает генерируемые директории. Это отдельная операция, не prerequisite чтения документации |
| Упаковка | `PackageWindows.ps1` | Editor build → collision audit/bake по флагу → Windows Development cook/stage; вывод и временные каталоги принудительно на E: |
| Проверки готового EXE | `TestWindowsBuild.ps1`, `TestSteamBuild.ps1` | Имеющийся build root, сценарии запуска/камеры/механик; не заменяют актуальный полный release audit |
| Локальная игра и боты | `PlayCoop.ps1`, `TestPlaytestBots.ps1` | Отдельные процессы или сценарный бот-прогон; `coop` резервирует место человеку, `observe` — автономное наблюдение |
| Collision | `OptimizeFoodCollision.ps1`, `Unreal/bake_menu_food_collision.py` | Аудит либо подготовка сохранённой коллизии, отдельный idempotence-check; по умолчанию Optimize — авторинг, для чтения требуется `-AuditOnly` |
| Основной ранний bootstrap | `Unreal/bootstrap_content.py` | Подготавливает исходные prototype assets; `MC_REBUILD_MAP=1` явно разрешает замену карты, `MC_REIMPORT=1` — повторный импорт. Не восстанавливает все поздние правки проекта |
| Gameplay authoring | `Unreal/author_*`, `import_*`, `bind_*`, `enhance_*` | Импорт мешей/клипов/текстур, материалы, Data Assets, Blueprint/level bindings; операции могут сохранять пакеты |
| Gameplay inspections | `Unreal/audit_*`, `inspect_*`, `verify_*`, `report_*` | Узкие снимки геометрии/материалов/настроек; имя скрипта не гарантирует отсутствие записи отчёта или editor-side изменений |
| Solo probes | `Unreal/check_*_solo.py`, `probe_*_solo.py` | Сценарные проверки и кадры конкретной механики; нужно читать fixture и механизм восстановления состояния |
| Хаб | `Unreal/Hub` | Рыбалка, экономика, fitting, продуктовые карточки, выбор локации, грунт, коллизии станций, баскетбол; JSON с destinations/localization |
| Меню | `Unreal/UI`, `Unreal/Menu3D`, `Blender/Menu3D_20261015` | Пакеты меню, тема/виджеты, геометрия/текстуры, native graph DSL и evidence; много исторических контрольных вариантов |
| Blender | `Blender/create_*`, `build_*`, `author_*`, `export_*`, `validate_*` | Воспроизводимые производные модели/анимации, fresh-import проверки и рендеры. В отдельных рецептах нужны конкретные исходники коллеги и версия Blender |
| Собственный VFX-арт | `Art/RewardChest` | Генерация масок/OBJ, HLSL и DSL construction graph; numpy/Pillow для генераторов |
| Видео и аудио | `Capture*.ps1`, `Build*Video.py`, `encode*.py`, `generate_audio.py`, `GenerateFogBrawlAudio.py` | Сценарная запись, компоновка/кодирование кадров и исходные синтезированные звуки; тайминг ролика не обязательно равен realtime-производительности |
| Микробенчмарки | `Benchmarks/coffee_mask_resolution_benchmark.cpp`, `coffee_wipe_bounded_ab.cpp`, `run_coffee_wipe_bounded_ab.py` | Изолированный CPU A/B кофе-маски; runner разделяет correctness/prepare и явное timing/measure, не импортирует Unreal и не меняет production source |

Перед повторным запуском конкретного рецепта нужно проверить его input revision, текущие owned packages, параметры apply/preview, output path и восстановление CVars/visibility/callbacks. Например, старый `import_coffee_slime.py` по умолчанию указывает на v1; для текущего v2 существует отдельный `import_coffee_slime_v2.py`, который по умолчанию составляет план, а применение требует явной revision. Массовый запуск всех `author_*` не является поддерживаемой сборкой контента.

## Плагины и внешние приложения

| Компонент | Источник истины | Роль и ограничения |
| --- | --- | --- |
| `KawaiiPhysics` 1.21.0 | `Plugins/KawaiiPhysics/KawaiiPhysics.uplugin`, [UPSTREAM.md](../Plugins/KawaiiPhysics/UPSTREAM.md) | Runtime/Sequencer bone physics и UncookedOnly editor tooling. MIT, upstream snapshot закреплён commit hash. Optional upstream tests module не включён в descriptor |
| `StylizedDynamics` 1.0.0 | Локальный `.uplugin`, [AnimationPhysicsThirdParty.md](AnimationPhysicsThirdParty.md) | Optional beta secondary-motion provider; локальный эксперимент, исключён из Git этого checkout. Документ не подтверждает право перераспространения исходников; чистый checkout должен работать без него |
| `MessControlProjectIndex` 1.0 | `Plugins/MessControlProjectIndex/MessControlProjectIndex.uplugin` | Editor-only C++ commandlet для чтения Asset Registry/reflection/graphs; `CanContainContent=false`; не входит в game target |
| `MPFSpineVFXTest/SourceAssets` | Файлы `Plugins/MPFSpineVFXTest` | Сохранены исходные данные эксперимента; `.uplugin` в этой папке не найден, это не подтверждённый активный UE-плагин |
| Niagara, PhysicsControl, ProceduralMeshComponent, EnhancedInput, DeformerGraph, Metasound | `MessControl.uproject` | Включённые engine plugins, используемые runtime/authoring областями проекта |
| OnlineSubsystemSteam, SteamSockets | `MessControl.uproject`, `DefaultEngine.ini` | Steam/P2P и тестовый AppID 480; наличие настроек не подтверждает работоспособность WAN/Steam в текущей сборке |
| PythonScriptPlugin, EditorScriptingUtilities, SequencerScripting | Editor allowlist в `.uproject` | Unreal Python/редакторский авторинг; не переносится как Python runtime в packaged игру |
| MCPClientToolset, ModelContextProtocol, AllToolsets | Editor allowlist в `.uproject` | Инструменты редакторской интеграции; наличие включённого плагина не подтверждает live connection |

По проектным правилам live-работа выполняется через `unreal_epic`, Blender, Substance Painter и Substance Designer MCP, когда приложение подключено; saved-индекс дополняет, а не заменяет состояние редактора. Read-only запросы подтвердили работающий Editor именно `E:/DEVGAME/MessControl`, карту `L_MainMenu` и `IsPIERunning=false`; изменения, build/save/Python и переключение сцены не выполнялись. Узкие clean CDO/DA/Hub table наблюдения описаны в [ProjectAudit.md](ProjectAudit.md). DCC не проверялись; unsaved edits остальных пакетов, визуальный результат и viewport этим документом не сертифицируются.

В [ThirdPartyVFXCredits.txt](ThirdPartyVFXCredits.txt) сохранена атрибуция `FreeParticle_SoftTofu` (CC BY 4.0). Для Kenney-puff сохранён CC0 source/license packet. Общий реестр техник, первоисточников и реально опробованных в проекте эффектов — [VFXReferences.md](VFXReferences.md); [VFXLab.md](VFXLab.md) описывает лабораторию. Наличие стороннего demo-набора в `Content` не следует трактовать как готовое собственное игровое VFX.

## Индекс проекта и Discord-помощник

Цепочка saved-индекса: `MCProjectIndexCommandlet.cpp` → JSON catalog/snapshots → `Tools/ProjectIndex/index.py` / SQLite → stdio `server.py`. `codebase_server.py` запускает codebase-memory для текущего корня. В этой сессии проект графа разрешён как `E-DEVGAME-MessControl`.

Экспортёр читает сохранённые Unreal-пакеты, отражаемые поля, CDO, DataTable, графы/пины/соединения и зависимости; не декодирует native binary payload мешей/текстур и несохранённые editor edits. Native exporter ограничивает collections 256 элементами и текст 32768 символами. Search/read MCP пагинируется; каталог метаданных шире набора подробных snapshots. Кэш, SQLite, jobs и build stamp находятся в `Saved/ProjectIndex` и не коммитятся.

Graph-first анализ инструментов подтвердил `AssetIndex`, status/read/search/references/refresh и серверные jobs; затем проверены сами исходники. `check_index_coverage` указывает `metadata_changed` для прочитанных Python-файлов и `parse_partial` у `.cpp/.h` commandlet. Это ограничивает полноту графа; отсутствие записи не доказывает отсутствие вызова. Пересоздание второго индекса с иным именем для того же checkout не требуется.

[Tools/DiscordCodex/README.md](../Tools/DiscordCodex/README.md) описывает отдельный Node.js 22+ бот (`discord.js` 14.27.0): mention/slash ask/find, черновики задач, чтение Git и ограниченные операторские fetch/pull. Модель получает read-only сессию проекта и allowlist двух индексов; Git-команды выполняет программа с фиксированными аргументами. Это отдельная сессия, она не продолжает текущий desktop chat. Конфиг/контекст/черновики находятся в `Saved/DiscordCodex`, токен — DPAPI-файл вне репозитория. Само наличие инструмента не означает, что бот сейчас запущен; при документировании он не запускался и сообщений не отправлял.

## Инвентарь проверок

В `Source/MessControl/Private/Tests` находятся **123 `.cpp` и 6 `.h`**. Литеральный поиск по `Source` дал **389 объявлений Automation-макросов в 78 файлах**; это статический подсчёт объявлений, а не число зарегистрированных, собранных или прошедших тестов. Имена макросов в header/условных блоках и smoke-код требуют проверки движком для точного runtime inventory.

| Область | Семейства исходников/сценариев |
| --- | --- |
| Базовая механика/физика | `MCGameplayTests`, `MCTongueDeformationTests`, `MCToothpickTests`, `MCBraceTests`, `MCDashTests`, `MCWeaponPlayerHitTests`, player healing/spawn/camera |
| Еда/уборка/жидкости | FoodCollision/Entry/MouthEntry/FrontDiscard/Strike/Stack, CleanupConsistency, CoffeePuddle/Debris, CataclysmJelly, ThroatBatch/Suction/TissueContact |
| Ход дня и награды | GameDirector, SingleDay/SingleDaySequence, PostNutRelax, ManualIntakeFlow, RoguePerk, PerkChoicePause, StageBoosterReward, ToolBooster, RewardMimic, Progression |
| События и боссы | IceEvent/IceBossPhase, FogBrawlEvent, NutRain/Kamikaze/LandingShadow/BossBlocking/BossCombat/BossAnimation/CombatVFX, EnemyWeaponContact/UpgradeContact, boss melee/mouth attack fixtures |
| Хаб/меню/UI/звук | HubEconomy/Flow/Guidance, MainMenu/Tutorial/StudioSplash, CosmeticCatalog, GameUserSettings, UI localization catalog/widgets, delivery/scoreboard/voice, sound palette, ContinuousDance/Yawn |
| Ресурсы и графика | WorldPreload, DesignerPresentation, VFXAsset, AmbientParticles, SpawnZone; визуальные probes отдельно от native assertions |
| Боты | PlaytestBots/Adaptation, PhysicalApproach, BotCoffee/Discard/TowerDelivery, NutCombat |
| Интеграционные smoke | Остальные `*Smoke.cpp`/сценарные файлы в `Private/Tests`, `Tools/Test*.ps1`, `Tools/Unreal/check_*`, `probe_*` |

`Tools/Test.ps1` объединяет старые Unit/visual/network входы. `Unit` вызывает `RunTests MessControl`, однако скрипт всё ещё проверяет только минимальный порог **36** успешных записей. Поэтому «скрипт вернул PASS» сам по себе не доказывает, что выполнился весь сегодняшний набор. Многие ранние сценарии явно запускают `-MCLegacyDays`: их результат нельзя переносить на полный текущий single-day flow.

Верхнеуровневые специализированные скрипты включают care/brush coverage/facing, inventory, gamepad, camera, active ragdoll, coffee puddles, food zones, throat, stuck food, ice, fog brawl, director, front end, playtest bots и Windows/Steam build. Network-режимы существуют, но запускаются только по отдельному явному запросу. Для обычной небольшой реализации проект требует короткую solo-PIE проверку; документационный аудит не запускал ни один из этих сценариев.

Отдельные автономные наборы:

- `Tools/ProjectIndex/test_index.py`: **14 unittest methods** — stale asset/schema/dependency invalidation, сохранение старого evidence после ошибки, pruning, неполное coverage, MCP transport, watcher settling, jobs и concurrent refresh ownership.
- `Tools/DiscordCodex/test/bridge.test.mjs`: **18 node:test cases** — access control, команды/Unicode, environment/MCP restrictions, RPC/timeout/queue, контекст, Git restrictions и дата Asia/Yakutsk. Это количество верхнеуровневых объявлений, не результат запуска.
- `Tools/Benchmarks`: отдельные correctness/timing эксперименты CPU-алгоритмов, не игровой performance benchmark.

[Validation.md](Validation.md) — исторический журнал ранних прогонов сентября с 17/35/36 тестами и прежними network fixtures. Актуальные узкие результаты октября распределены по feature-документам и `Artifacts/*/Report*.json`; их нельзя складывать в заявление о свежем полном проходе всех 389 объявлений. Последний checkpoint хаба [23.10.2026](HubSystemsCheckpoint_20261023.md) отдельно отмечает, что Stage04 предшествует исправлению profile-save, packaged first-use wardrobe/shop/basketball остаётся неполным, а PSO misses и отдельный Editor GPU PageFault не закрыты.

## Загрузка, cook и хранение

`DefaultGame.ini` явно cook-ит `L_Mouth`, `L_Tutorial`, `L_MainMenu`, `Hub`, `L_HubSelection` и AlwaysCook-корни `/Game/Data`, `/Game/Art`, `/Game/Audio`, `/Game/Gameplay`, `/Game/Hub/VFX`, `/Game/UI/MainMenu3D`, `/Game/Cataclysm`. Cook inclusion и готовность первого видимого использования — разные проверки. Карта или string-path resource вне этих корней требует фактической зависимости/отдельного правила; по одному имени файла это не подтверждается.

Новый runtime material/instance/texture/mesh/Niagara/widget brush должен быть достижим через preload roots каждой использующей его локации. Предпочтительны отражаемые ссылки карты, DA/DT и общей UI-темы; native-only lazy roots регистрируются в `MCPreloadManifest` или per-map настройках. Динамические material variants должны быть созданы и удерживаться до readiness barrier. [ResourceLoading.md](ResourceLoading.md) различает Editor compilation, packaged PSO precaching, streaming и NullRHI assets-only проверки.

Все новые рабочие файлы, outputs/caches/temp должны сохраняться на E:. Git LFS настроен для `.uasset`, `.umap`, `.blend`, `.spp`, `.fbx`, `.png`, `.wav`, `.mp4`; `.obj/.npz` имеют отдельные правила для frozen menu packets. Некоторые manifests/evidence требуют byte-identical SHA256 и специальных `-text` правил в `.gitattributes`; их нельзя механически форматировать без обновления evidence contract. `Saved`, `Intermediate`, `Binaries`, `DerivedDataCache`, `.codebase-memory`, `__pycache__` игнорируются.

## Выявленные ограничения и технический долг

1. **Актуальный saved asset closure не подтверждён.** Индекс старый и сборка неактуальна; 3538 `.uasset` не были последовательно загружены и экспортированы. До анализа конкретного Blueprint/refactor нужен текущий refresh с завершённым job и актуальной сборкой, как описано в [ProjectIndex.md](ProjectIndex.md).
2. **Нет единой clean-room команды генерации всего контента.** Bootstrap покрывает ранний прототип; поздние сцены, материалы и bindings оформлены отдельными recipes. Повторное применение старой ревизии может заменить новые данные. Требуется выбирать конкретный manifest/handoff, а не запускать каталог целиком.
3. **Переносимость путей неполная.** Ряд menu-рецептов содержит `E:/DEVGAME/MessControl` напрямую; `build_wizard_jiggle_lab.py` указывает на пользовательский Blender addon в `C:/Users/user/AppData/Roaming/...`; старый CharacterCurrent README ссылается на Downloads исходника и `D:/BLENDER_TEST`. Это реальные внешние входы/локальные зависимости, не гарантированно доступные после clone. Их переносимость здесь не исправлялась.
4. **Redirect TEMP не унифицирован.** `PackageWindows.ps1` явно задаёт E: для TEMP/TMP/TMPDIR/UAT/DDC и восстанавливает environment. `Build.ps1` и ранние test/capture wrappers не обеспечивают того же самостоятельно; для будущего запуска нужно задавать process-scoped E: temp. `test_index.py` использует `TemporaryDirectory()` без явного `dir`, следовательно зависит от environment. Setup индекса по умолчанию устанавливает binary в LOCALAPPDATA, а Discord хранит DPAPI token там же; новые установки/переносы должны учитывать E:-правило и сохранять fixed discovery configuration отдельно.
5. **Статус сценариев рассредоточен.** `Test.ps1` имеет старый минимальный порог и legacy-day fixtures; `Validation.md` не является текущим сводным test report. Нужна отдельная согласованная registry-сводка зарегистрированных тестов и фактически запущенного набора при следующей разрешённой проверке.
6. **Несколько поколений UI/арта сосуществуют.** Новый active `L_MainMenu` не делает устаревшие reference-finish receipts текущим описанием startup. Действующий runtime binding нужно проверять по теме, C++ и saved packages; отвергнутые/локальные варианты не удалялись.
7. **Художественная готовность и performance не завершены.** MainMenu look-development WIP, будущие рты DAY04–07 placeholders, final 1:1 art approval не заявлен. Рендеры/ролики и shader instruction counts не доказывают realtime FPS, packaged first-launch PSO completeness или полное прохождение кампании.

Источники этого документа: текущие `MessControl.uproject`, `Config/DefaultEngine.ini`, `Config/DefaultGame.ini`, `.gitattributes`, `.gitignore`, manifests локальных плагинов, исходники `Tools/ProjectIndex`, `Tools/DiscordCodex`, build/package/test wrappers, выборочные author/import recipes, файловый и Git-инвентарь, перечисленные feature README/checkpoints и graph coverage. Исторические PASS приведены только как зафиксированные ранее результаты; новой runtime-валидацией они не являются.
