# Аудит проекта и обновление документации

Дата среза: **23 октября 2026 года**, checkout `E:/DEVGAME/MessControl`, ветка `main`. Анализ включает незакоммиченные файлы на диске. Это документационный срез текущего рабочего проекта, а не сертификат готовности определённого commit или пакета.

## Результат

Обновлены основные точки входа и технические справочники: [README проекта](../README.md), [архитектура](Architecture.md), [GDD](GDD.md), [системные контракты](SystemsDesign.md), [хаб](HubLobby.md), [инвентарь/HUD](InventoryAndHUD.md), [загрузка ресурсов](ResourceLoading.md) и [контекст для агентов](../.agents/ue-project-context.md). Добавлены [карта документации](README.md), [контент и инструменты](ContentAndTools.md) и [каталог исходников/карт](SourceCatalog.md).

Документы разделяют текущую реализацию, нативные defaults, сохранённые данные, исторические проверки и планы. Главные исправления — отказ от описания всего проекта как раннего семидневного стенда, актуальный маршрут через Hub, профиль/экономика, состояние прогрессии и upgrades, современный preload/readiness и фактические ограничения пакета.

## Охват и метод

Проведена полная файловая инвентаризация `Source` и сохранённых карт, просмотр дескрипторов/конфигурации/скриптов и системный статический разбор основных игровых и технических контуров. Структурный поиск выполнялся через codebase-memory, затем выводы сверялись с текущими исходниками. Дополнительно выполнено узкое read-only чтение загруженных объектов в работающем Unreal Editor, описанное ниже. Полный каталог файлов обеспечивает навигацию по областям; он не означает ручное прочтение каждой строки всех 123 тысяч строк или проверку каждого бинарного payload.

| Область | Что проверено | Граница вывода |
|---|---|---|
| Framework | GameMode/GameState, player/controller, subsystems, state ownership и маршрут запуска; live CDO BP_MouthGameMode | Fresh disk export/полные World Settings не выполнены; loaded CDO зависит от текущих binaries |
| Gameplay | Directors, события, food/cleaning/grip, урон/статусы, награды/перки, основные враги и боссы | Наличие ветки кода не подтверждает её прохождение в текущем runtime |
| Hub | Ready/selection/travel, рыбалка, магазин/гриль/гардероб, экономика/профиль; live DT_HubDestinations | Цены/баланс не сертифицированы; package save/reload не проверен |
| Presentation | Character/physics/animation/camera, HUD/theme/localization, world indicators, sound/voice | Художественный просмотр всех клипов/материалов/ассетов не проводился |
| Resource readiness | Manifest/native roots, UI preparation, streaming/render gates, first-use/late diagnostics, cook config | Полная достижимость каждого пакета и packaged PSO coverage не доказаны |
| Content/art | 21 карта, основные каталоги, import/export инструменты, поколения artwork и WIP | Все 3559 пакетов не прочитаны свежим Unreal exporter |
| Tooling/tests | Build/Test/ProjectIndex/Unreal/Blender/Discord, декларации тестов и существующие отчёты | Тесты/сборки не выполнялись; число деклараций не является PASS count |
| Documentation | Основные справочники, исторические checkpoints и ссылки изменённых документов | Старые узкие документы не превращены в новые runtime отчёты |

Не изменялись C++, конфигурация, игровые пакеты или приложения. Не выполнялись сборка, cook/package, PIE, multiplayer, DCC render или полное обновление индексов. Документационные проверки перечислены ниже отдельно.

## Инвентарь

| Объект | На диске / по тексту |
|---|---:|
| Файлы `Source` | 578 |
| `.cpp` / `.h` / `.cs` | 350 / 224 / 4 |
| Строки `Source`, включая тесты и Build/Target | 123291 |
| Runtime MessControl, включая тесты/Build.cs | 558 файлов |
| Editor MessControlEditor, включая Build.cs | 18 файлов |
| Корневые Target.cs | 2 |
| Content `.uasset` / `.umap` | 3538 / 21 |
| SIMPLE/COMPLEX Automation macro declarations | 389 в 78 файлах |
| `Private/Tests` | 123 `.cpp`, 6 `.h` |
| ProjectIndex Python unittest methods | 14 |
| DiscordCodex Node test cases | 18 |

Content packages на диске и tracked Git package paths совпали: 3559. Подсчёт использует Unicode paths без git quote escaping и включает external actor/object packages. Размеры локальных ArtSource/Artifacts в [ContentAndTools](ContentAndTools.md) включают незакоммиченные производные; это не размер чистого clone.

## Состояние доказательств

### Кодовый индекс

`list_projects` подтвердил auto-selected `E-DEVGAME-MessControl` с root этого checkout. `index_status` сообщил ready, 32434 nodes и 183348 edges; indexed_at — `2026-10-23T13:01:47Z`. Время индекса приводится буквально в UTC, не как местная дата аудита.

Для ключевых framework/hub/UI/preload/movement путей проверен `check_index_coverage`. C++ implementation paths часто имеют `no_recorded_issue`, reflection headers — `partial`; freshness в приведённых путях — `metadata_changed`. Поэтому цитируемые объявления и ветки прочитаны напрямую. Граф в этой версии иногда связывает распространённые `GetWorld`, `Add`, `Cast`, `HasAuthority`, `CreateWidget` с посторонними одноимёнными символами. Такие edges не использованы как точное доказательство runtime вызова. Отсутствие узла не трактуется как отсутствие использования или мёртвый код.

### Индекс ассетов

Полученный `asset_status`:

| Поле | Значение |
|---|---|
| Catalog assets | 3158 |
| Exported assets | 1343 |
| Snapshot states | 1343 `schema_changed` |
| build_current | **false** |
| captured_utc | `2026-10-21T04:43:11.334Z` |
| Engine в metadata | `5.8.1-56057345+++UE5+Release-5.8` |
| Последнее сохранённое refresh job | complete, но его старый result не делает нынешние snapshots текущими |

Эти числа нельзя выдавать за сегодняшнее полное содержимое Content: дисковая инвентаризация содержит 3559 пакетов, а охват/время и классификация каталога отличаются. Через индекс не подтверждены свежие Blueprint defaults, inherited properties, DataTable rows, graphs и исчерпывающие asset references. Узкие live-наблюдения ниже имеют другой источник и не обновляют индекс. `asset_read`/refresh не использовались для обхода этой границы; build/export не запускался. Для нового чтения сохранённых свойств через индекс потребуется актуализация сборки и экспорта с ожиданием завершения, когда это необходимо в scope соответствующей работы. В документационном аудите эти операции не требовались.

### Unreal и DCC

Проект настроен на unreal_epic, Blender, Substance Painter/Designer. На этапе первичного обнаружения unreal_epic tools были недоступны; позднее динамически появились и соединение удалось подтвердить read-only запросами. На **2026-10-23 13:27:20 UTC** работал UnrealEditor PID 19788 именно с `E:/DEVGAME/MessControl/MessControl.uproject`, открыта `/Game/Maps/L_MainMenu`, `IsPIERunning=false`. В запуске есть `-MCHubTransientProfile -NoCompile -NoHotReload -nosteam -nosound` и изолированный UserDir на E:. Это осмотр редактора, не gameplay/Steam/audio тест.

| Объект | Прочитанное live-состояние |
|---|---|
| L_MainMenu, DT_HubDestinations, BP_MouthGameMode | `is_dirty=false` на момент запросов |
| DT_HubDestinations Day01 | bEnabled=true, Map=L_Mouth, SingleDayProfile=DA_SingleDay |
| Day02–Day07 | bEnabled=false, Map/Profile=None |
| BP_MouthGameMode loaded CDO | bUseSingleDayLoop=true, bUseDayOnePlan=true, bUseAdaptiveDirector=true |
| CDO profiles | SingleDayProfile=DA_SingleDay, FirstDayPlan=DA_Day01, DirectorProfile=DA_SingleDayDirector |
| CDO framework | BP_PlayerCharacter_C, MCGameState, MCPlayerController, MCPlayerState; parent=MCGameMode, seamless=true |

Дополнительно прочитаны clean загруженные профили и таблицы через `ObjectTools.list_properties/get_properties`, `DataTableTools.list_rows/get_rows` и `AssetTools.is_dirty`, без Python:

| Объект | Live-значения |
|---|---|
| DA_SingleDay | NutEncounter → Jellies → IceEvent → FogBrawl, support=120 с у каждого слота; legacy finale=false; порции [3,5,8] и [5,8,9]; ориентир 25–35 минут |
| DA_SingleDay timing | AfterTraining=5 с, betweenMeal=5–10 с, beforeNuts=4 с; interval обоих Relax=1 с |
| DA_SingleDay legacy payload | completionExperience=25 в слотах, но XP по текущему C++ отключён; ZombieBoss/directorSeconds=30 относятся к выключенному legacy finale |
| DA_RunRules | maxMouthHealth=100, daysToSurvive=7, maxPlayers=4, **initialArenaTeeth=12**, в отличие от native default=8 |
| DT_HubEconomyRules.Default | Calcium=false; максимум монет 1000000, pending boosters 32, owned cosmetics 256; grill stages 0,4 / 2 / 1,2 / 0,4 с |
| DT_HubCatalog | 12 raw enabled/shop строк: 8 NextRunBooster, 3 Cosmetic, 1 Calcium consumable; цены boosters 18–60, cosmetics 35/45/40, Calcium 12 |
| DT_GrillRewards | 15 raw enabled строк: 3 Coins + 12 CatalogItem, сумма исходных весов 100; Calcium-строка имеет raw weight=5 |

Economy snapshot снят в `2026-10-23 13:29:49 UTC`. Все перечисленные DA/DT имели `is_dirty=false`. **Raw enabled не равно effective eligibility:** source `IsCatalogUsable` исключает Calcium при выключенном правиле, grill фильтрует тем же допуском; дальнейшие perk/cosmetic ограничения не исполнялись. Поэтому 12 строк каталога и 15 строк выдачи нельзя объявлять 12 доступными покупками и 15 достижимыми результатами. Аналогично InitialArenaTeeth=12 не доказывает, что конкретная карта создала 12 пригодных зубов: число ограничено sockets и текущим запуском.

Это чтение **загруженных editor objects/CDO**, не свежий disk exporter snapshot. `is_dirty=false` означает отсутствие несохранённых изменений у этих пакетов во время запроса; compiled CDO зависит от загруженных binaries/последней Blueprint compilation. Поэтому native source default=false и live CDO=true у SingleDay зафиксированы раздельно; текущий несобранный C++ не сертифицирован этим чтением. Графы, полное содержимое всех пакетов, unsaved edits других объектов и runtime результаты не проверялись.

Python, build, PIE, refresh, save, переключение карт, selection и focus для этих запросов не запускались/не менялись. DCC authoring для документации не требовался; приложения не редактировались.

## Существенные расхождения старой документации

| Прежнее описание | Подтверждённый текущий контракт | Источник |
|---|---|---|
| Проект — ранний семидневный случайный прототип | Существуют отдельные Hub/Tutorial/SingleDay/DayPlan/adaptive/legacy маршруты; семь законченных направлений не доказаны | [MCGameMode.cpp](../Source/MessControl/Private/MCGameMode.cpp), [MCHubGameState.cpp](../Source/MessControl/Private/MCHubGameState.cpp) |
| SingleDay безусловно включён по умолчанию | Native bUseSingleDayLoop=false; BP, URL/profile и `-MCSingleDay` меняют выбор | [MCGameMode.h](../Source/MessControl/Public/MCGameMode.h), [MCGameMode.cpp](../Source/MessControl/Private/MCGameMode.cpp) |
| Профиль одного дня обязательно заканчивается полной победой | Новый authored-фрагмент после последнего слота переходит в бессрочную поддержку; отдельный zombie finale legacy и в live DA выключен | [MCSingleDayDirector.cpp](../Source/MessControl/Private/MCSingleDayDirector.cpp), [SystemsDesign](SystemsDesign.md) |
| Ready всегда переносит команду в L_HubSelection | Обычный RequestOpenSelection меняет фазу внутри Hub; отдельная карта остаётся поддержанной | [MCHubGameMode.cpp](../Source/MessControl/Private/MCHubGameMode.cpp) |
| Обычный XP/уровни дают личные choices | Progression compatibility shell не начисляет XP; награды нужно описывать через действующие chest/perk/booster пути | [MCProgressionComponent.cpp](../Source/MessControl/Private/MCProgressionComponent.cpp), [SystemsDesign](SystemsDesign.md) |
| Оружие наносит союзникам фиксированный HP damage | Проверенные melee/chainsaw/watergun ветки применяют физический ApplyHit без прямого HP damage | [MCToothCharacter.cpp](../Source/MessControl/Private/MCToothCharacter.cpp), [MCToothPhysicsComponent.cpp](../Source/MessControl/Private/MCToothPhysicsComponent.cpp), [InventoryAndHUD](InventoryAndHUD.md) |
| Buffer/perforator — доступное улучшение | Названия/реализация сохранены, eligibility/HasToolUpgrade явно исключают Buffer | [MCPerkComponent.cpp](../Source/MessControl/Private/MCPerkComponent.cpp) |
| Сохранённый код плотов означает доступное событие | Cataclysm Rafts сохранены, но IsVariantEnabled исключает Rafts из live-ветки | [MCCataclysmEvent.h](../Source/MessControl/Public/MCCataclysmEvent.h) |
| Любая чистка — четыре контакта | Есть пространственные wipe masks и разные status/contact правила; одно число не описывает все поверхности | [MCBrushContactComponent.cpp](../Source/MessControl/Private/MCBrushContactComponent.cpp), [MCSurfaceWipe.h](../Source/MessControl/Public/MCSurfaceWipe.h) |
| Резка создаёт геометрию плоскостью / еда поглощается тканью | Разбиение использует авторенные FragmentMeshes; absorption compatibility отключена, порча имеет SpoilAt | [MCFoodActor.cpp](../Source/MessControl/Private/MCFoodActor.cpp), [MCFoodAbsorption.cpp](../Source/MessControl/Private/MCFoodAbsorption.cpp) |
| Warm Editor доказывает чистый first launch | Readiness/cook, Editor shader work и packaged PSO misses требуют разных доказательств | [ResourceLoading](ResourceLoading.md), [Hub checkpoint](HubSystemsCheckpoint_20261023.md) |
| Главный экран — только isolated preview | Есть активный native front end/Hub маршрут; часть artwork и look-dev по-прежнему WIP | [MenuLookDevWIP_20261022](MenuLookDevWIP_20261022.md), [ContentAndTools](ContentAndTools.md) |
| Unit wrapper подтверждает весь набор | В source 389 деклараций, а Unit acceptance всё ещё имеет порог 36; это не полный discovery/report | [Tools/Test.ps1](../Tools/Test.ps1), [SourceCatalog](SourceCatalog.md) |

## Сохранённые незавершённые вопросы

Это очередь следующих отдельных работ, а не новые выполненные проверки или автоматически подтверждённые дефекты нынешнего запуска.

1. **Профиль в packaged game.** [Checkpoint хаба](HubSystemsCheckpoint_20261023.md) прямо говорит: Stage04 предшествует исправлению boolean-result Move. Для утверждения исправленного пакета требуется пакет с этим исправлением и реальный save/reload того же профиля в новом GameInstance. Старый package PASS не покрывает новый source.
2. **Первое использование ресурсов.** В checkpoint оставлены packaged wardrobe/shop/basketball interactions. Там записано 15 graphics PSO misses после Mouth Ready за ограниченный период; это не Editor shader compile и не доказанный stall каждого miss. Новый user directory не гарантирует холодный driver cache.
3. **GPU PageFault.** Отдельный idle Editor D3D12/SM5 PageFault остался без установленной причины/исправления; позднейшие успешные проверки не закрывают его автоматически.
4. **Актуальность package evidence.** Перед рефакторингом или выводами о содержимом ассетов нужно привести build/export к текущему состоянию. Здесь это не делалось из-за документационного scope. AGENTS требует отдельную задачу для Windows build/cook/package; необходимые Editor authoring/компиляция/экспорт оцениваются по scope конкретного изменения, без общего дополнительного approval-правила.
5. **Границы test wrappers.** Нужен отдельный task на актуализацию discovery/acceptance и выборочных fixtures, если они станут критерием полного PASS. Результаты сентября и количество объявлений нельзя складывать в текущую сертификацию.
6. **Переносимость tooling.** Есть default paths на C:/старые D:/пользовательские installs и temp без явного E:. [ContentAndTools](ContentAndTools.md) перечисляет конкретные места. Они не запускались и не исправлялись в этой работе; перед использованием им задают task-specific E: пути. Глобальные системные данные не перемещались.
7. **Поколения арт-источников.** WIP/approval/integrated состояния отличаются. Особенно важны версии menu assembly и CoffeeSlime v1/v2: default старого importer не равен текущему v2 pipeline. Наличие preview/render не подтверждает Unreal integration.
8. **Полный gameplay/network/release.** Полная кампания, все death/travel/respawn cleanup, четыре игрока, Steam/WAN, голос между машинами и release certification не проверялись этим аудитом. Они требуют отдельного scope; сетевые тесты — отдельного явного запроса.

## Пользовательские правки на старте

На старте уже были изменены `MCMainMenuWidget.cpp/.h`, `MCWorldStatusComponent.cpp`, `MCWorldStatusWidget.cpp/.h` и два UI authoring scripts. Аудит читает их текущие значения, но не объявляет эти изменения собранными или проверенными. Документационный task не редактирует их, не откатывает и не коммитит; промежуточная проверка SHA256 этих семи файлов совпала со снятым baseline. В процессе работы также появился modified status у `WBP_WashNSmashMenu_AdaptiveV2.uasset`; документационный task его не записывал и сохранил чужое состояние. Изменения MainMenu скрывают устаревшие подписи/подсказки, WorldStatus — HP фрагментов, оставляя один таймер; подробнее в [InventoryAndHUD](InventoryAndHUD.md).

## Проверка документации

- Проверены существование локальных Markdown-ссылок и исходных файлов, указанных в обновлённых основных документах.
- Каталог Source/карт сверён с обходом файловой системы; package paths отдельно сверены с Git.
- Проверены форматирование diff/пробельные ошибки и отсутствие изменений runtime-кода со стороны документационного task.
- Старые PASS-числа сохранены только как датированные отчёты, с их исходным scope; отсутствующие прежние evidence paths отмечены в соответствующих справочниках.

Итог: в 14 обновлённых/новых документах проверены **1032 локальные Markdown-ссылки**, отсутствующих файлов/якорей — **0**; `git diff --check` для документационных изменений завершился без ошибок. Сборка/PIE не являются частью этой проверки. Следующее обновление начинается с текущих source/config/package evidence, а не с переноса старых утверждений PASS.
