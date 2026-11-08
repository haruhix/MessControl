# Свежий аудит загрузки и целевой code review — 2026-11-03

Проверен `8f874644b8f611d15a871f23ad11b577c54fe151`. Пользователь уточнил: смущает вход в **PIE из редактора**. Вывод: большая разница первого и повторного захода указывает на разовую подготовку редактора; переносить эти секунды на Shipping нельзя. Однако runtime preload и резервирование геометрии остаются в игре. Обещание «в боевом билде проблем не будет» текущими доказательствами не подтверждается.

Это свежий read-only аудит текущего кода, конфигурации и существующих логов, с целевым review загрузки, графических ресурсов, SFX, UI и сохранения профиля. Это не построчное ревью всех 187985 строк C++/headers двух модулей. Новые Unreal/PIE, build, cook/package, automation и сетевые проверки не запускались. Игровой код и пакеты не менялись.

## Что показывает фактическое время

Все строки ниже извлечены заново из существующих логов. Это не новые замеры, сделанные данным аудитом, и не статистика нескольких холодных запусков.

| Существующее наблюдение | LoadingAssets | PreparingRendering | WaitingForStreaming | До native Ready |
| --- | ---: | ---: | ---: | ---: |
| Обычный проектный `Saved/Logs/MessControl.log`, 03.11 13:57:59–13:58:44 (Asia/Yakutsk) | 32.57 с | 9.97 с | 2.67 с | 45.21 с |
| Более поздний `Editor110.log`, первый наблюдавшийся PIE, 03.11 15:41:20–15:42:36 | 58.07 с | 14.01 с | 3.62 с | 75.70 с |
| Тот же процесс, следующий PIE, 03.11 15:42:42–15:42:44 | 0.36 с | 0.97 с | 1.14 с | 2.47 с |

Значения фаз — разности округлённых `elapsed` в нативном логе. Измерение начинается внутри world preload; время запуска Editor, копирования PIE-мира и работы до `BeginPreparation` в него не входит. Ready не равнозначен полному gameplay/visual acceptance.

В позднем процессе было 30 FPS, BelowNormal priority, `fx.Niagara.Compilation.MaxActiveTaskCount=2`, `-MCHubTransientProfile`, `-MCFoodCollisionNoAutoBake` и отдельный DDC на E:. Это условия авторского диагностического процесса. Доля задержки от каждого флага отдельно не измерена. Восстановление прежнего SkeletalMesh было сохранено до этих двух заходов; текущий SHA256 `DA_PlayerAppearance` совпал с квитанцией сохранения. Предыдущий observer не завершил полную проверку cleanup/визуального движения; его общий результат здесь не превращён в PASS.

Оба лога показывают **1323 запрошенных пути, 1669 удерживаемых ресурсов, 623 материала, 1102 warmup-компонента и 476 streaming targets**. Это разные счётчики, их нельзя складывать как уникальные ассеты. Первая загрузка имеет значительную цену даже после сегодняшней оптимизации; повторная подготовка существенно быстрее. Причина всех 58.07 с asset loading без loadtime trace не установлена: нельзя приписать их только диску или только шейдерам.

Существующая оптимизация `b1b016978` действительно присутствует: disabled perk rows и часть невыбранных профилей исключаются, numeric-only reflected контейнеры обходятся без прохода по вершинам, свои warmup proxies не перезапускают подготовку, очередь фабрик работает с бюджетом. Исторические 98.641 с resident rendering из старого before-run не являются текущим результатом. См. [её отдельную валидацию](E:/DEVGAME/MessControl/Docs/Validation/ScopedPreloadOptimization_20261103.md).

## Находки code review

### 1. P2 — планирование rendering всё ещё блокирует один игровой кадр

В [BuildRenderingWork](E:/DEVGAME/MessControl/Source/MessControl/Private/MCWorldPreloadSubsystem.cpp:475) сбор primitive/material inventory, вызовы precache и построение всех комбинаций выполняются за один вызов. Бюджет 2–20 мс применяется позже, только в [ProcessRenderingWork](E:/DEVGAME/MessControl/Source/MessControl/Private/MCWorldPreloadSubsystem.cpp:730). В первом позднем заходе `MC_PRELOAD_PLAN seconds=3.036`; `ResourceInventory` занял 2.353 с. В обычном проектном логе весь план занял 3.145 с, inventory — 2.289 с. Повторный поздний заход: план 0.035 с.

Следствие: даже исправленная очередь не гарантирует отзывчивость loading UI при первой инвентаризации. Этот путь не ограждён целиком `WITH_EDITOR`, хотя часть стоимости относится к Editor. Рекомендация: сделать продолжение inventory/context planning с бюджетом кадра, сохранить уже выполненную работу и все readiness gates. Нельзя обещать те же 3 с в packaged без измерения.

### 2. P2 — MeshBudgetMB не ограничивает создаваемый резерв procedural surfaces

[Prepare](E:/DEVGAME/MessControl/Source/MessControl/Private/MCNiagaraProceduralSurfaceResources.cpp:85) создаёт резерв для каждой найденной системы и каждого `SmallPreparedInstances`/`LargePreparedInstances`. [PrepareDraw](E:/DEVGAME/MessControl/Source/MessControl/Private/MCNiagaraProceduralSurfaceComponent.cpp:48) выделяет CPU-массивы и mesh sections, затем делает material variants. Есть лимиты одной системы/инстанса, но общий объём всех систем локации перед выделением не считается и не ограничивается.

`MeshBudgetMB=64` регулирует дополнительные streamed LOD bytes в [BuildStreamingTargets](E:/DEVGAME/MessControl/Source/MessControl/Private/MCWorldPreloadSubsystem.cpp:791); он не покрывает этот процедурный резерв, MID, Niagara pools или общий RAM/VRAM. Исторический отчёт сегодняшнего оптимизатора описывает банк примерно 5.26 млн вершин; это контекст риска, не новый замер текущей памяти. Утечки или OOM данным аудитом не доказаны.

Рекомендация: до создания банка считать суммарные triangles/sections/CPU bytes/ожидаемый GPU reserve, записывать их в диагностику и задавать общий бюджет по локации и целевому железу. Сохранить нужную gameplay concurrency; уменьшать её только после проверки реальных максимумов.

### 3. P2 — optical scale портрета обходит подготовленный UI mask cache

[NativeOnInitialized](E:/DEVGAME/MessControl/Source/MessControl/Private/MCBossHealthWidget.cpp:127) подготавливает первые три портрета с размером `Size*Fit`. [Runtime update](E:/DEVGAME/MessControl/Source/MessControl/Private/MCBossHealthWidget.cpp:481) использует `Size*Fit*PortraitScaleByBoss[BossID]`. Ключ кэша включает размер. При Scale != 1 первый показ портрета промахивается мимо прогрева и вызывает [PrepareMask](E:/DEVGAME/MessControl/Source/MessControl/Private/MCBossHealthWidget.cpp:89) с **18 FlushRenderingCommands** на игровом потоке. Четвёртый и последующие портреты также не попадают в текущий трёхэлементный прогрев.

Это условный дефект расширения/настройки UI, подтверждённый сравнением путей кода. Наличие ненулевых overrides в текущем saved widget CDO не установлено; актуальный snapshot недоступен. Для stock Scale=1 три прогретых портрета защищены. Рекомендация: готовить точные реальные ключи `BossID/texture/size/scale` до readiness, либо вынести вычисление поля из блокирующего runtime-пути. Переносить Flush без сохранения GPU-зависимостей проходов нельзя.

## Дополнительные риски и границы

- **Глобальная compile-очередь увеличивает PIE.** [GetPendingCompilationCount](E:/DEVGAME/MessControl/Source/MessControl/Private/MCWorldPreloadSubsystem.cpp:417) учитывает весь `FAssetCompilingManager` и `GShaderCompilingManager`, включая работу вне данного мира. Поэтому параллельно открытый/перекомпилируемый материал способен задержать вход в PIE до configured timeout 300 с. Это защитное поведение; удалять проверку без замены нельзя. Нужна диагностика world-specific versus unrelated Editor work. В cooked-клиенте ветка `WITH_EDITOR` отсутствует.
- **Disabled SFX остаются обязательными для playback readiness.** [TickEventAudioReadiness](E:/DEVGAME/MessControl/Source/MessControl/Private/MCWorldPreloadSubsystem.cpp:1333) собирает все непустые `Layer.Sound`, без фильтра Mode/bEnabled/group policy. Сохранение ссылок выключенных слоёв явно задумано в `MCEventSFX.cpp:41`. Но текущая политика также требует их playback-подготовку: неподдержанный MetaSound/procedural source в выключенном слое при доступном аудиовыходе может провалить загрузку всей локации; лимит 128 источников тоже включает их. Это вопрос политики, который нужно решить отдельно от удержания ассетов. Наличие такого проблемного выключенного слоя в текущих saved данных не проверено.
- **Shipping конфигурация не является текущим packaging default:** [DefaultGame.ini](E:/DEVGAME/MessControl/Config/DefaultGame.ini:12) содержит `PPBC_Development`. CLI/preset может переопределить это; файл не доказывает, какой режим использовался в чужом билде. Для release приёмки нужен явный Shipping target/config.
- **Cook roots широкие, но cook не равен runtime load.** Data/Art/Audio/Gameplay/Cataclysm и дополнительные UI/Hub каталоги принудительно готовятся для пакета. Это может увеличивать cook/install size; из этого не следует, что весь Content сразу загружается в память. Manifest обходит выбранные корни и зависимости, а не сканирует весь Content. Проблема отсутствия `/Game/Cataclysm` в старой конфигурации уже исправлена.
- **CPU-данные меша — отдельная packaged предпосылка.** [FreezePoseCapture](E:/DEVGAME/MessControl/Source/MessControl/Private/MCFreezePoseCapture.cpp:29) требует CPU position/tangent/UV/index/weight buffers. В установленном UE `FSkeletalMeshLODRenderData::ShouldForceKeepCPUResources` сохраняет их в Editor, а cooked retention зависит от runtime/cook-настроек и `NeedCPUData`. PIE Ready не доказывает доступность этих буферов после cook. Если их не будет, текущий freeze preparation провалит общий loading barrier. Это не подтверждённая ошибка текущего mesh asset; требуется проверить именно cooked текущий `SK_CharacterGameplay`, а не только ранее тестировавшийся subdiv V2.
- **Crash contexts нельзя считать packaged crash report.** В последних локальных записях есть и Ensure, и реальные access violations. Часть Ensure относится к Niagara editor authoring и directed tests. Для нескольких access violations стек не содержит project frame; причина и исправленность не установлены. Нельзя ни объявить это доказанным крашем игрока, ни закрыть как исправленное из-за последующего Ready.

## Что в проверенных путях сделано правильно

Async load handles сохраняются до конца world lifetime; missing assets приводят к явному Failed/Retry, а не фиктивному Ready. Барьер учитывает реальную presentation, компонентные PSO, общий pipeline work, streaming и RHI fences. Временные warmup components освобождаются после подготовки. Есть фильтрация выбранного профиля и enabled perk rows, исключены destination worlds. В просмотренном `MCHubProfileSubsystem` сохранение использует временный файл, проверяет boolean Move, защищает unreadable saves и делает migration backup. Это source-review, не новая проверка диска или player economy.

Проверка установленного UE закрыла потенциальный ложный finding: `FShaderPipelineCache::NumPrecompilesRemaining()` **включает** `PipelineStateCache::NumActivePrecacheRequests()`. Также Niagara `PrecachePSOs` вызывает asset-level precache даже без system instance; скрытый неактивный компонент сам по себе не доказывает отсутствие прогрева. Component PSO precaching в этом UE выключен при `GIsEditor`, поэтому `pipelines=0` в PIE не сертифицирует packaged PSO-покрытие. См. [Epic PSO precaching](https://dev.epicgames.com/documentation/en-us/unreal-engine/pso-precaching-for-unreal-engine) и [официальное описание async loading](https://dev.epicgames.com/documentation/en-us/unreal-engine/asynchronous-asset-loading-in-unreal-engine).

## Доказательства и реальная валидация

- Все **898 source-file hashes** из `Build86.Start.json` совпали с текущими файлами; новых C++/header файлов вне квитанции не найдено. Это сопоставление с прежней успешной Editor-сборкой, не новый build и не доказательство Shipping compilation. 11 прежних focused tests не перезапускались и не являются новым PASS аудита.
- Текущий `DA_PlayerAppearance.uasset` совпал с `AssetEdit.json` возврата `SK_CharacterGameplay`. Другие saved assets не сертифицированы.
- `codebase_memory` разрешён как `E-DEVGAME-MessControl`. Граф использован для структуры; coverage для цитируемых файлов возвращает `metadata_changed`, широкие scopes имеют parser gaps. Выводы основаны на заново прочитанных исходниках, а не на отсутствии ребра/символа.
- `messcontrol_assets`: catalog 4192, snapshots 4191/schema_changed, `build_current=false`. Было также прежнее export error у legacy `EUB_SnapToActor`. Свежие свойства через stale snapshot не читались, refresh/build не запускался. Saved coverage не объявлена полной.
- `unreal_epic` в доступном наборе tools не экспонируется; процессов UnrealEditor/UnrealEditor-Cmd при осмотре не было. Поэтому live состояние не подтверждено. Старые saved cooked files не использованы как актуальная сборка.
- Созданы только этот отчёт и локальная воспроизводимая read-only выгрузка. Пользовательские изменения ReviewBoard и незакоммиченные арт-материалы сохранены. Нет исправлений runtime, новых ресурсов, коммита или художественного acceptance.

Локально на E: сохранены [Evidence.json](E:/CodexWork/MessControl/Validation/ProjectLoadingAudit_20261103/Evidence.json), [рецепт сбора](E:/CodexWork/MessControl/Validation/ProjectLoadingAudit_20261103/collect_evidence.py), копии двух прочитанных логов и квитанция Editor110. Они не добавляются в Git.

## Что закрывает вопрос о боевом билде

Нужна отдельная явно запрошенная задача Windows cook/package, как требует AGENTS.md. Критерий — текущий Shipping-кандидат с проверенным cooked manifest и коротким solo маршрутом Menu → Hub → Mouth → Hub. Измерять время от запуска/перехода до первого управляемого кадра, отдельно первый и повторный запуск, с loadtime/CPU/PSO/streaming trace и пиками RAM/VRAM. Холодность DDC, OS и driver/PSO cache отмечать раздельно; новый UserDir не считается холодным driver cache.

На текущей внешности проверить CPU freeze resources из cooked payload, первое использование Ice/навыков/инструментов/UI и отсутствие поздней shader/PSO/streaming работы. Проверить save/reload профиля в новом процессе. Минимальное целевое железо и более слабый GPU требуют собственного результата. Сеть/Steam/multiplayer — отдельный scope по явному запросу.

До этого корректный статус: **PIE-задержка объяснена частично и выявлены оставшиеся узкие места; packaged first-launch и release stability не проверены. Подтверждённых P0/P1 дефектов в просмотренных путях не установлено; три P2 выше требуют доработки или проверки заданных условий.**
