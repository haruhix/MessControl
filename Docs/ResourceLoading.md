# Подготовка ресурсов и экран загрузки

Актуализация: **2026-10-23**, по исходникам рабочего дерева, включая незакоммиченные изменения. Это описание кода, а не подтверждение нынешней сборки или первого packaged-запуска. В этом аудите сборка, PIE, сетевые проверки и cook/package не запускались. Asset index имеет `build_current=false` и устаревшие snapshots после изменения схемы; полного свежего экспорта package properties нет. Исключение — ограниченное read-only чтение clean loaded CDO/DA и таблиц Hub в работающем Editor, отдельно зафиксированное в [ProjectAudit](ProjectAudit.md). Оно не подтверждает остальные свойства пакетов, актуальность binaries или visual readiness.

## Ответственность систем

| Система | Назначение |
|---|---|
| `UMCWorldPreloadSubsystem` | Один Game/PIE world; собирает зависимости, удерживает ресурсы, готовит rendering/streaming и выдаёт локальный Ready. На dedicated server не создаётся. |
| `FMCPreloadManifest` | Корни и reflected зависимости мира, DA/DT, native/Blueprint CDO и Designer-деревьев виджетов. |
| `UMCLoadingWidget` | Локальная подготовка, ошибка с Retry и отдельное ожидание команды. |
| `AMCPlayerController` | Готовность, блокировка ввода, Hub scene generation и серверные gameplay/tutorial ACK. |
| `AMCMainMenuPlayerController` | Подготовка меню; startup splash может идти поверх неё, но не обходит readiness. |
| `UMCTravelLoadingSubsystem` | GameInstance subsystem: Slate/MoviePlayer экран только на время блокирующей загрузки карты вне Editor. |

Источники: [world subsystem](../Source/MessControl/Private/MCWorldPreloadSubsystem.cpp), [manifest](../Source/MessControl/Private/MCPreloadManifest.cpp), [controller loading](../Source/MessControl/Private/MCPlayerControllerLoading.cpp), [main menu controller](../Source/MessControl/Private/MCMainMenuPlayerController.cpp), [travel screen](../Source/MessControl/Private/MCTravelLoadingSubsystem.cpp).

Подготовка не ставит движок на паузу, не запускает директора и не создаёт gameplay actors. Временные render-компоненты не имеют коллизии, навигации и animation/gameplay tick. Серверный запуск событий остаётся отдельным контрактом.

## Последовательность подготовки

Фазы: `Idle → LoadingAssets → PreparingRendering → WaitingForStreaming → Ready`; ошибка переводит в `Failed`.

1. `BeginPreparation` принимает локальный контроллер своего мира. Manifest грузится через высокоприоритетный `RequestAsyncLoad`; каждая завершённая волна раскрывает дополнительные reflected зависимости. Лимиты: 32 волны, 4096 уникальных путей, глубина reflected обхода 24.
2. Должны завершиться требуемые загрузка и видимость streaming levels. Число actors, зарегистрированных primitives и levels стабилизируется на три кадра. Manifest повторно снимается с фактического мира; новые зависимости возвращают систему к загрузке.
3. Готовятся реальные primitive bindings и временные static/skeletal/instanced mesh proxies. Вызывается `PrecachePSOs`; новые proxies создаются по четыре за tick. Для явно добавленных surface-материалов используются representative meshes и соответствующие vertex factories.
4. В Editor обрабатываются ожидающие asset/shader compile jobs. Для наблюдавшегося первого использования `/Game/Art/Materials/MM_Standart_SSS` запрашивается недостающая shader map и проверяется её полнота. Это путь `WITH_EDITOR`, включая Editor executable с `-game`; cooked-игра использует cooked shaders и runtime PSO precaching.
5. Рассчитываются ограниченные бюджетом streaming targets. Ожидаются component PSO work, `FShaderPipelineCache::NumPrecompilesRemaining()`, streaming и texture resources. В Editor нужна ещё одна секунда без новой compile-работы; затем проходят три устойчивых render/RHI fence цикла.
6. После Ready временные proxies удаляются. Загруженные UObject и completed streamable handles удерживаются до сброса/Retry или уничтожения мира.

`SetPresentationReady` — дополнительный входной барьер. В Hub контроллер ждёт созданную 3D сцену селектора и подготовленную wardrobe presentation, а в Gathering — также pawn текущего мира. Dynamic material variants должны существовать до завершения readiness.

При недоступном rendering NullRHI/commandlet может завершить только asset-подготовку с явным `MC_PRELOAD_ASSETS_ONLY visualValidation=0`. Это не проверка графической готовности.

## Manifest

Для всех обслуживаемых карт добавляются:

- `CommonAssets` и соответствующая запись `Maps` из `UMCPreloadSettings`;
- `/Engine/BasicShapes/Plane`, `DA_GameplayUITheme`, UI-материалы `M_UI_Phase1Panel`, `M_UI_Phase1Fill`, `M_UI_Phase1Tool`;
- Hub catalog, grill rewards, economy rules и character cosmetics, включая пока не надетые предметы;
- фактические actors/components мира с их сохранёнными или уже созданными visual overrides.

Для `L_Mouth` и `L_Tutorial` добавляются профиль дня и food/DA/DT зависимости, выбранный через `MCHubDay=` single-day профиль, core-loop/director/nut profiles, appearance/equipment/animation/arena/coffee/cola профили, native-only material/Niagara roots и CDO классов событий. Точный перечень находится в `FMCPreloadManifest::Collect` и должен меняться вместе с native `LoadObject` путями.

Обход раскрывает nested structs, arrays, maps, sets, hard/soft object/class references, строки DataTable, CDO и Widget Blueprint Designer archetype tree. Пропускаются `Transient`, `EditorOnly`, `Deprecated` поля; destination `UWorld`/`ULevel` и `USoundBase` не раскрываются как визуальные зависимости. Допускаются `/Game`, `/Engine`, `/Script/MessControl`.

Manifest не сканирует весь Content и не заменяет cooking. Config soft path должен отдельно попадать в cook. Наличие пути в коде не доказывает существование и актуальную настройку сохранённого пакета.

## Переходы, ACK и ввод

`AMCPlayerController` требует подготовку локально в `L_Mouth`, `L_Tutorial` и Hub. Loading widget расположен на Z=10000: UI-only ввод, блокировка движения/обзора, `CancelGameplayInput` у текущего pawn. При смене pawn отмена удерживаемых действий повторяется; после освобождения вызывается общий `UpdateInputMode`.

Gameplay/tutorial ACK отправляются после локального Ready, наличия pawn текущего мира и PlayerState. Сервер отдельно проверяет командную готовность:

- Hub ACK содержит `SceneGeneration` и может повторяться раз в секунду; новые scene-generation ACK не возвращают начальную loading cover поверх открытого Hub;
- Hub-run arena lobby автоматически стартует после ACK участников; manual lobby сохраняет Start-кнопку;
- tutorial ждёт выхода director из Loading, single-day — из Training;
- `bDevManualEvents` имеет отдельный путь без ожидания обычного запуска.

После раскрытия UI `bLoadingPresentationReleased` не возвращает начальную завесу при следующих обновлениях сцены. Travel сбрасывает presentation для нового world. Локальный Ready может показывать «у тебя всё готово», пока сервер ждёт команду.

Главное меню создаётся до освобождения preload. Startup splash расположен выше loading screen (Z=20000) и может идти параллельно подготовке; его завершение отдаёт фокус меню только после `bPreparationReleased`. MoviePlayer автоматически заканчивается с блокирующей частью map load; в PIE отключён. Оставшуюся подготовку, включая seamless travel, обслуживает world subsystem.

Источники: [front end/ACK](../Source/MessControl/Private/MCPlayerController.cpp), [Hub presentation](../Source/MessControl/Private/MCPlayerControllerHub.cpp), [серверный startup](../Source/MessControl/Private/MCGameMode.cpp). См. [HubLobby](HubLobby.md), [SingleDay](SingleDay.md), [InventoryAndHUD](InventoryAndHUD.md).

## Ошибки и настройки

Отсутствующий ресурс, отменённый/неполученный async handle, превышение лимита manifest или timeout приводят к Failed. `BeginPreparation` не перезапускает ошибку автоматически. Retry освобождает прежние handles/proxies/retained objects и собирает manifest заново. Ошибка и timeout не разрешают ready ACK.

[Loading widget](../Source/MessControl/Private/MCLoadingWidget.cpp) поддерживает кнопку Retry и R при ошибке; Escape/gamepad Back возвращает в меню через session/travel логику. Asset progress отражает уже обнаруженные запросы, а не всю GPU-подготовку. Следующая волна может увеличить знаменатель; 100% asset bar ещё не означает Ready.

Project Settings → Game → Resource preparation, [UMCPreloadSettings](../Source/MessControl/Public/MCPreloadSettings.h), Game config:

| Поле | Native default | Значение |
|---|---:|---|
| `CommonAssets` | пусто | Дополнительные roots всех карт |
| `Maps` | пусто | Roots по короткому имени карты без PIE prefix |
| `TimeoutSeconds` | 120 с | Дедлайн подготовки |
| `TargetTextureSize` | 1024 px | Начальный ordinary texture target |
| `TextureBudgetMB` | 256 MB | Дополнительный mip residency budget |
| `MeshBudgetMB` | 64 MB | Дополнительный mesh LOD residency budget |

Это defaults C++, не подтверждённые значения нынешнего config/package. Streaming учитывает quality mip bias, разрешённые/необязательные LOD и уже resident объём. Бюджеты не ограничивают общие CPU memory, VRAM и ранее загруженные данные; удержание ресурсов увеличивает residency. User quality settings и общий texture pool сохраняются. Virtual texture pages имеют отдельную систему residency.

## Диагностика и пределы

Маркеры `LogMCWorldPreload`: `MC_PRELOAD_PHASE`, `MC_PRELOAD_RENDER_WORK`, `MC_PRELOAD_STREAM_TARGETS`, `MC_PRELOAD_WAIT`, `MC_PRELOAD_READY`, `MC_PRELOAD_MISSING`, `MC_PRELOAD_FAILED`.

Editor first-use путь пишет `MC_PRELOAD_EDITOR_FIRST_USE` и `MC_PRELOAD_EDITOR_FIRST_USE_READY`. Ещё 60 секунд после Ready наблюдается поздняя работа: `MC_PRELOAD_LATE_WORK`, `MC_PRELOAD_LATE_CATEGORY`, `MC_PRELOAD_LATE_MATERIAL`. Это глобальные Editor managers: работа другого tab тоже может попасть в счётчики. Диагностика не перекрывает начавшийся gameplay повторно.

Representative proxies не доказывают готовность каждого будущего procedural/Niagara варианта. Candy/Frost Nova использует целевую подготовку точного Niagara controller и instance/material layout перед первым импульсом; исторические материалы по прежнему тёмному onset см. [VFXReferences](VFXReferences.md). Startup barrier не вводит неуязвимость присоединившемуся к уже опасному событию игроку. Hazard safeguard `UsePSOPrecacheRenderProxyDelay=false` не является полной PSO-гарантией.

Новый runtime visual root должен быть достижим из reflected ссылок карты/таблиц/темы либо зарегистрирован в manifest/map settings. Dynamic variants создаются и удерживаются до readiness. Проверяется первое видимое применение в игровом ракурсе; тёплый Editor cache не доказывает чистый packaged launch.

## Историческая проверка

Evidence от **2026-10-20** хранится в `E:/CodexWork/MessControlResourceLoading_20261020/`. В аудите проверено наличие этих файлов; прежние результаты не перенесены на текущее рабочее дерево.

| Evidence | Зафиксированный охват |
|---|---|
| `FocusedTests_LastRun.json` | Success: четыре `MessControl.Loading.Manifest.*` и 16 selected SingleDay/PostNut тестов |
| `SoloMouth01/Report.json`, `SoloHub01/Report.json`, `SoloMainMenu01/Report.json` | Rendered solo: non-ready overlay → Ready → UI; arena включала первый perk и естественную выдачу еды |
| `SoloFailureRetry03/Report.json` | 9 checks: постоянный Failed, Retry UI, восстановление settings до subsystem Retry, Ready и первый perk; без физического нажатия кнопки |
| `Previews/LoadingInitial/LoadingInitial.mp4` | Прежний немой preview с исходным wall timing |

Прежний документ также записывал Editor build success 2026-10-20 и Steam 480 log 2026-10-18: 50 PSO hitches, из них 7 precached, texture pool 1000 MB. Это не доказательство состояния нынешних binaries, причины pop-in или устранения всех задержек.

Не подтверждены этим аудитом: полный main menu → Hub → selector → arena travel chain, tutorial entry, Return travel, repeat-travel восстановление ввода, холодный packaged launch и multiplayer. Новый build/refresh нужен отдельно для актуальной saved-asset evidence после native изменений; он здесь не запускался.

Метод: codebase-memory graph для `E-DEVGAME-MessControl`, затем actual source. Coverage показывает `metadata_changed` и partial reflection headers; источником утверждений служит код на диске, не полнота графа. См. [ProjectIndex](ProjectIndex.md), [Validation](Validation.md), [WindowsBuild](WindowsBuild.md).
