# MESS / CONTROL

Кооперативный Unreal-проект о маленьких зубах, которые поддерживают порядок во рту: чистят поверхности, переносят еду, справляются с опасными событиями и боссами. Хаб объединяет подготовку команды, выбор локации, рыбалку, магазин и гардероб.

**Актуализация документации: 23.10.2026.** Описание сверено с текущим C++, конфигурацией, инструментами и файловым составом. Готовность отдельных сохранённых Blueprint/DataTable и полный игровой проход этим аудитом не подтверждены. Художественное направление и семидневная кампания остаются шире проверенной реализации.

## Начать знакомство

| Документ | Для чего читать |
| --- | --- |
| [Навигация по документации](Docs/README.md) | Найти актуальную область и отличить её от истории разработки |
| [Аудит проекта](Docs/ProjectAudit.md) | Подтверждённое состояние, расхождения, ограничения и дальнейшие проверки |
| [Архитектура](Docs/Architecture.md) | Владельцы состояния, маршрут игры, компоненты, сеть и данные |
| [Каталог исходников](Docs/SourceCatalog.md) | Полный список текущих runtime/editor исходников с декларациями |
| [Контент и инструменты](Docs/ContentAndTools.md) | Карты, art sources, импорт, плагины, тесты и авторинг |
| [Хаб и переход в игру](Docs/HubLobby.md) | Ready, выбор локации, загрузка и travel |
| [Последний checkpoint хаба](Docs/HubSystemsCheckpoint_20261023.md) | Ранее выполненные проверки и оставшиеся ограничения конкретного среза |

[GDD](Docs/GDD.md) и [SystemsDesign](Docs/SystemsDesign.md) описывают текущие игровые контуры и отдельно отмечают целевой дизайн, исторические решения и планы. Наличие идеи в плановом разделе не означает её включение в текущую сборку.

## Среда разработки

- Windows, Unreal Engine association **5.8**; локальный `E:/UE/UE_5.8/Engine/Build/Build.version` — **5.8.1, CL 56057345**.
- Visual Studio 2022 с Game development with C++, Windows SDK и Git LFS. Версию toolchain для конкретной сборки проверяет установленный движок.
- Targets: `MessControl` — Game, `MessControlEditor` — Editor; оба используют `BuildSettingsVersion.V7` и `EngineIncludeOrderVersion.Unreal5_8`.
- Runtime-модуль `MessControl`, редакторский `MessControlEditor`; собственный `MessControlProjectIndex` доступен только редактору. `KawaiiPhysics` включён в репозиторий. `StylizedDynamics` — optional локальный provider, отсутствующий в обычном версионируемом наборе.

Рабочие файлы, загрузки, кэш, временные данные, рендеры и exports размещаются на **E:**. Настройки и discovery-файлы приложений могут иметь фиксированное расположение; существующие глобальные каталоги не следует переносить целиком.

## Первый запуск

Из PowerShell подготовь checkout и бинарные ресурсы:

```powershell
git clone https://github.com/haruhix/MessControl.git E:/DEVGAME/MessControl
Set-Location E:/DEVGAME/MessControl
git lfs install
git lfs pull
```

Перед запуском инструментов задай временный каталог для текущего процесса:

```powershell
$taskTemp = 'E:/CodexTemp/MessControl'
New-Item -ItemType Directory -Path $taskTemp -Force | Out-Null
$env:TEMP = $taskTemp
$env:TMP = $taskTemp
$env:TMPDIR = $taskTemp
$env:UE_ROOT = 'E:/UE/UE_5.8'
.\Tools\Build.ps1 -EngineRoot $env:UE_ROOT
```

Это ручная команда сборки Editor; обновление документации её не запускало. Открой `MessControl.uproject` в UE 5.8. При обычном открытии редактора загружается `/Game/Maps/L_Mouth`; для проверки полного входного маршрута открой `/Game/Maps/L_MainMenu` и запусти solo PIE. В самостоятельной игре `GameDefaultMap` уже указывает на `L_MainMenu`.

Сохранённый контент входит в репозиторий: для обычного старта не нужно запускать `bootstrap_content.py` или все генераторы. Они относятся к разным ревизиям и могут менять пакеты. Чистая пересборка — отдельный сценарий `RebuildProject.bat` / `Tools/Rebuild.ps1`; упаковка Windows — отдельная задача по [WindowsBuild.md](Docs/WindowsBuild.md).

## Текущий маршрут игры

1. Основное меню вызывает `UMCSteamSessionSubsystem::EnterHub`. При доступном Steam создаётся комната; локальный fallback открывает `/Game/Maps/Hub` как listen server.
2. В хабе игроки занимаются подготовкой. Хост открывает выбор, когда вся команда загрузилась и находится в Ready-зоне.
3. Переход в фазу `Selecting` выполняется **внутри Hub**. Отдельная карта `L_HubSelection` сохранена для совместимости/отдельного входа; обычное открытие выбора не выполняет travel на неё.
4. Хост выбирает доступное направление из `DT_HubDestinations` и запускает его. Сервер проверяет путь и добавляет `MCLobby`, `MCHubRun`, `MCHubDay`; арена выполняет собственный loading/ready barrier.

Нативный fallback каталога включает день 1 → `L_Mouth`, а дни 2–7 отключает. Узкое live-чтение загруженной таблицы также подтвердило только Day01 с L_Mouth/DA_SingleDay; Day02–07 выключены. Это не свежий asset export всего проекта. Семь слотов в интерфейсе не означают семь готовых игровых дней.

В C++ `AMCGameMode` имеет `bUseSingleDayLoop=false`, `bUseDayOnePlan=true`, `bUseAdaptiveDirector=true`. Загруженный clean CDO `BP_MouthGameMode` при live-чтении имел **bUseSingleDayLoop=true** и DA_SingleDay. Маршрут из хаба также включает single-day loop по профилю направления; `-MCSingleDay` включает его явно, legacy-флаги могут выключать. Live CDO зависит от загруженных binaries/последней Blueprint compile и не подтверждает runtime нынешних несобранных source changes; подробности — в аудите.

Узкое read-only чтение clean DA также зафиксировало `DA_RunRules`: HP 100, Days 7, MaxPlayers 4, **InitialArenaTeeth 12** при native default 8. В `DA_SingleDay` загружены Nuts → Jellies → Ice → Fog, поддержка 120 с, `OpeningMealPortions=[3,5,8]`, `AfterNutMealPortions=[5,8,9]`, `bLegacyTimedFinale=false`. Это наблюдение editor-loaded объектов, не сборка текущего source и не подтверждение завершённой кампании.

## Основное управление

Ниже — текущие нативные привязки. Настройки, фокус UI и конкретное состояние героя могут ограничивать действие.

| Действие | Клавиатура / мышь | Геймпад |
| --- | --- | --- |
| Движение | WASD | Левый стик |
| Прыжок | Space | A / нижняя кнопка |
| Рывок при нажатии, бег при удержании | Shift | Нажатие левого стика |
| Камера вокруг героя | Мышь | Правый стик |
| Приближение камеры | Колесо мыши | — |
| Щётка / кирка / нож / спрей | 1 / 2 / 3 / 4 | D-pad: влево / вверх / вправо / вниз |
| Основное действие инструмента | ЛКМ | RB / RT |
| Взаимодействовать, удерживать/нести предмет | E | X / левая кнопка |
| Бросить удерживаемый предмет | Q | Y / верхняя кнопка |
| Удар / толчок | F | LT |
| Упор / защита | ПКМ | LB |
| Уход за собой | C | Нажатие правого стика |
| Рыболовный инструмент в контексте рыбалки | 5; вход на пирсе через E | — |
| Эмоции | T | — |
| Счёт и игроки | Удерживать Tab | View / Special Left |
| Голос, Push-to-Talk по умолчанию | Удерживать V | Настраиваемая клавиша |
| Пауза / настройки | Esc | Menu / Special Right |
| Запуск выбора/действие хоста в хабе | Enter | Через UI |
| Tooth Lab / подключение / dev-панель | F1 / F2 / F3 | — |

Подробности: [InventoryAndHUD.md](Docs/InventoryAndHUD.md), [Gamepad.md](Docs/Gamepad.md), [CareAndHandling.md](Docs/CareAndHandling.md), [DevPanel.md](Docs/DevPanel.md). Старые записи с ПКМ как ударом и неподвижной камерой относятся к прежней реализации.

## Что уже представлено в исходниках

- Контактная уборка и уход, загрязнение героя, зубной камень, повреждение больших зубов арены, смерть и очередь возрождения.
- Еда из таблиц, физический хват и переноска, бросок, фрагменты, порча, доставка и неправильная утилизация. Фрагменты выбираются из подготовленных `FragmentMeshes`; runtime-разрезания произвольной сетки и системы поглощения еды нет.
- Single-day sequence, адаптивный director, ореховые противники/боссы, лёд, Fog Brawl, кофе, катаклизм, язвы и другие события; конкретное включение зависит от профиля забега.
- Сундуки, mimic, карточные награды/перки и улучшения инструментов. `UMCProgressionComponent` оставлен как инертная совместимость; XP/level-up не является действующим источником наград. Buffer/перфоратор заблокирован для выдачи/активации, хотя legacy-код сохранён.
- Хаб: рыбалка, личная экономика, гардероб с отдельным fitting actor, магазин, гриль и баскетбол. Серверные профили сохраняются в `Saved/HubProfiles`; это не облачное account-save.
- Общая UI-тема, локализация таблицами, настройки, индикаторы голоса, scene music/ambience и подготовка ресурсов до показа локации.

По проверенным веткам удар инструмента по союзнику вызывает физическую реакцию `ApplyHit`, без прямого вычитания HP в этих ветках. Это не отменяет урон от опасностей, врагов и других механик.

## Где менять настройки и графику

| Изменение | Начальная точка |
| --- | --- |
| Правила забега, герой, инструменты | `Content/Data/DA_RunRules`, `DA_PlayerAppearance`, `DA_Equipment` |
| Уход, физика, язык | `DA_ToothCare`, `DA_FoodPhysics`, `DA_ToothPhysics`, `DA_Tongue`; [справочник языка](Docs/DA_Tongue.md) |
| Ход дня | `Content/Gameplay/CoreLoop`; [SingleDay.md](Docs/SingleDay.md) |
| Еда / косметика / направления | `DT_BreakfastMenu`, `DT_CharacterCosmetics`, `Gameplay/Hub/DT_HubDestinations` |
| HUD, меню, магазин | `Content/Gameplay/UI`, общая theme; [InventoryAndHUD.md](Docs/InventoryAndHUD.md) |
| Аудио | `UMCSoundPalette` / `DA_MouthSounds`: Events и два scene-слоя; сохранённые назначения нужно проверять в DA |
| Art и импорт | `ArtSource`, `ContentSource`, `SourceArt`, `Tools/Blender`, `Tools/Unreal`; [ContentAndTools.md](Docs/ContentAndTools.md) |

Новые runtime visual assets должны иметь preload и cook coverage для каждой использующей их локации. Динамические material variants подготавливаются до readiness barrier; [ResourceLoading.md](Docs/ResourceLoading.md) описывает Editor shader compilation, packaged PSO и streaming отдельно.

## Совместная игра и голос

Целевая модель — listen server до четырёх игроков. Steam использует тестовый AppID **480**, фильтр `MC_GAME=MessControl_Coop_v1`, SteamSockets и fallback IpNetDriver; инструкция — [SteamTesting.md](Docs/SteamTesting.md). В PIE/`-nosteam` доступен IP-маршрут; внешнее подключение требует доступной сети. Миграция хоста и полноценное reconnect/resume не реализованы.

Голос использует OSS: Push-to-Talk V по умолчанию, OpenMic, self/player mute, уровень микрофона и отдельный локальный mic test. Выбор входного устройства через игровой API выключен (`CanSelectInputDevice=false`); используются системные настройки. Voice имеет независимый SoundClass и не должен регулироваться громкостью SFX. Наличие этих систем не подтверждает свежий Steam/WAN-прогон.

## Проверки и известные ограничения

Текущий статический срез: **578 файлов Source / 123291 строка**, **3538 `.uasset` + 21 `.umap`**, **389 Automation-объявлений в 78 файлах**. Это инвентарь, а не число выполненных тестов. `Tools/Test.ps1` сохраняет старый минимальный порог 36 и legacy fixtures; [Validation.md](Docs/Validation.md) содержит исторические результаты.

В рамках обновления документации не запускались builds, тесты, PIE, cook/package или asset refresh. Работающий Editor проекта на E: осмотрен read-only на `L_MainMenu`, PIE выключен; узкие CDO/DA/table наблюдения перечислены выше и в аудите. Доступный asset snapshot от 21.10 устарел: `build_current=false`, все 1343 подробных снимка отмечены `schema_changed`. Графы и остальные CDO/table rows требуют отдельного подтверждения; live-чтение не обновило весь индекс.

Checkpoint [HubSystems_20261023](Docs/HubSystemsCheckpoint_20261023.md) сохраняет ранее выполненные Editor/узкие packaged-проверки. Его Stage04 предшествует исправлению `IFileManager::Move` в сохранении профиля; packaged first-use гардероба/магазина/баскетбола остаётся неполным. В наблюдавшемся забеге были 15 graphics PSO misses после Mouth Ready; отдельный Editor GPU PageFault остаётся нерешённым. Меню — художественный [WIP](Docs/MenuLookDevWIP_20261022.md), полный release/campaign pass не заявляется.

Для небольшого изменения проект предусматривает короткую solo-PIE проверку. Сетевые проверки и Windows build/cook/package выполняются отдельной явно запрошенной задачей. Необходимые Editor authoring/компиляция/экспорт определяются scope изменения; документационное обновление их не требовало. Индексы помогают искать: порядок работы и ограничения описаны в [ProjectIndex.md](Docs/ProjectIndex.md); источником истины остаются текущий код и сохранённые Unreal-пакеты.
