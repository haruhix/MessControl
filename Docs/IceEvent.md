# Ледяное событие: механика и точки изменения

Рабочий контракт на 10 октября 2026. Механика живёт в `AMCIceEvent`;
состояние и результаты ударов определяет сервер. F3 и последовательность дня
сейчас создают **нативный класс**, поэтому настройки холодного события меняются
в C++-defaults, а не в произвольном Blueprint или `DA_SingleDayDirector`.
После изменения defaults нужен Editor build. `Start()` также ограничивает
значения допустимыми диапазонами: при расширении настройки проверить оба места.

Все ключи `ICE-*` ниже — постоянные якоря документации. В коде ориентироваться
на имена функций и свойств: номера строк меняются при следующих правках.

| Что менять | Якорь | Основное место |
|---|---|---|
| Цель, здоровье ядра, начало и конец | [ICE-CORE](#ice-core) | `MCIceEvent.h`; `Start`, `HitWithPickaxe`, `Finish` |
| Тайминги, перекрытие и сужение зон | [ICE-ZONES](#ice-zones) | `CircleDuration`, `SafeRadius`, `NextSafeRadius`, `UpdateCircles` |
| Размещение зон на арене | [ICE-PLACEMENT](#ice-placement) | `ChooseCircle`; геометрия `MCTongue` |
| Мешающий согреванию кристалл | [ICE-BLOCKERS](#ice-blockers) | `QueueZoneCrystal`, `IsCircleBlocked`, `HitObstructionWithPickaxe` |
| Серии сосулек и попадание | [ICE-ICICLES](#ice-icicles) | `UpdateIcicleSeries`, `QueueIcicle`, `ResolveIcicle` |
| Конус бури | [ICE-NOVA](#ice-nova) | `BeginNova`, `IsInsideNova`, `ResolveNova` |
| Ноги во льду и действия киркой | [ICE-FEET](#ice-feet) | `MCToothCharacter`; `MCToothMovementComponent` |
| Общая шкала холода и смерть | [ICE-METER](#ice-meter) | `UpdateFreeze`, `IsSafePoint` |
| F3 и порядок событий | [ICE-INTEGRATION](#ice-integration) | `MCDevCommands`, `MCDevPanelWidget`, `MCSingleDayDirector` |
| Сеть и очистка | [ICE-LIFECYCLE](#ice-lifecycle) | `GetLifetimeReplicatedProps`, `Stop`, `EndPlay` |
| Будущий визуальный polish | [ICE-PRESENTATION](#ice-presentation) | `MCIceEventVFX`; `/Game/Gameplay/Cold/VFX` |
| Проверки и видео | [ICE-VALIDATION](#ice-validation) | `MCIceEventTests`; `TestIceColdRework.ps1` |

## ICE-CORE

**Цель:** киркой разбить центральный кристалл. Его разрушение завершает холод;
после этого обычный директор продолжает последовательность дня.

Файлы: [MCIceEvent.h](../Source/MessControl/Public/MCIceEvent.h),
[MCIceEvent.cpp](../Source/MessControl/Private/MCIceEvent.cpp).

| Свойство | Сейчас | Где применяется |
|---|---:|---|
| `ArrivalSeconds` | 3 с | `Start`, переход `Arrival → Active` в `Tick` |
| `CandyHealthPerPlayer` | 960 | `Start`: масштаб здоровья по числу участников |
| `CandyHealth`, `MaxCandyHealth` | runtime | `HitWithPickaxe`; состояние реплицируется |

Имена `Candy*` сохранены из первого прототипа; теперь обозначают центральный
кристалл. `CandyContactPoint` отвечает за контакт инструмента. Изменение
видимого меша само по себе не меняет здоровье или условие завершения.

## ICE-ZONES

**Контракт:** следующая зона уже согревает в последние 10 секунд предыдущей.
Это перекрытие двух работающих зон; время новой зоны идёт с её появления.
При смене основной зоны таймер не начинается заново. Кристалл может временно
выключить согревание одной из зон. Радиус каждой зоны постепенно уменьшается
за её собственное время жизни, включая перекрытие.

Настройки в `MCIceEvent.h`; алгоритм в `MCIceEvent.cpp`:

| Свойство | Сейчас | Точка изменения |
|---|---:|---|
| `CircleSeconds` | 45 с | начальное время жизни |
| `CircleStepSeconds` | 5 с | ускорение каждой следующей зоны |
| `MinCircleSeconds` | 20 с | нижний предел времени жизни |
| `CircleOverlapSeconds` | 10 с | одновременная работа старой и новой зоны |
| `CircleRadius` | 330 см | начальный радиус |
| `MinCircleRadius` | 200 см | радиус перед исчезновением |

`CircleDuration` вычисляет длительность; `SafeRadius` и `NextSafeRadius` —
сужение; `UpdateCircles` — появление и передачу роли основной зоны.
`CircleStartedAt` и `NextCircleStartedAt` сохраняют **момент появления**.

Пример отсчёта от начала активной фазы:

| Зона | Появляется | Исчезает | Собственная длительность |
|---|---:|---:|---:|
| 0 | 0 с | 45 с | 45 с |
| 1 | 35 с | 75 с | 40 с |
| 2 | 65 с | 100 с | 35 с |
| 3 | 90 с | 120 с | 30 с |
| 4 | 110 с | 135 с | 25 с |
| 5 | 125 с | 145 с | 20 с |
| 6 | 135 с | 155 с | 20 с |

На минимуме 20 секунд новая зона появляется каждые 10 секунд, потому что
половина её жизни проходит при перекрытии. `IsSafePoint` проверяет объединение
живых, не заблокированных зон; внешний вид зоны не определяет согревание.

## ICE-PLACEMENT

`ChooseCircle` выбирает поддерживаемую точку у дальнего края доступной арены,
а следующую — далеко от предыдущей. Если поверхность позволяет, центры
разнесены минимум на два начальных радиуса. Это проверяется **до** фильтра
самых дальних от центра кандидатов, чтобы не выбирать соседние зоны на
несимметричном языке.

При изменении формы арены проверить `GameplaySpawnFootprint` и
`InteriorSurfacePoint` в [MCTongue](../Source/MessControl/Private/MCTongue.cpp),
доступную поверхность и полосы доставки. Точки хранятся в координатах языка;
`AnchorFloor` привязывает их к его текущей поверхности. Для слишком малой
поверхности есть запасной вариант размещения; требование полного разнесения
может быть геометрически недостижимо.

## ICE-BLOCKERS

| Свойство | Сейчас | Назначение |
|---|---:|---|
| `ZoneCrystalIntervalSeconds` | 16 с | период попытки создать препятствие |
| `ZoneCrystalHealth` | 80 | прочность кристалла зоны |

`QueueZoneCrystal` выбирает одну из подходящих действующих зон, в которой ещё
нет кристалла; попытка может быть пропущена. `FMCIceZoneCrystal.ZoneIndex`
привязывает препятствие к владельцу. Пока оно цело, `IsCircleBlocked` выключает
согревание **только этой зоны**. Общий таймер зоны продолжает идти.

`HitObstructionWithPickaxe` проверяет обычный контакт кирки, дистанцию,
направление и отсутствие препятствия. При разрушении зона снова согревает;
при окончании её жизни соответствующий кристалл удаляется. Не сбрасывать
`CircleStartedAt` при разрушении кристалла.

## ICE-ICICLES

**Контракт:** три удара в серии, новый старт серии раз в 12 секунд. Один
случайный живой игрок выбирается на серию. Каждая очередная отметка фиксирует
его позицию в момент своего появления и затем не преследует его.

| Свойство | Сейчас | Назначение |
|---|---:|---|
| `IcicleIntervalSeconds` | 12 с | от начала одной серии до начала следующей |
| `IcicleSeriesSpacingSeconds` | 1,2 с | промежуток между отметками серии |
| `IcicleWarningSeconds` | 2,2 с | время от отметки до удара |
| `IcicleRadius` | 155 см | область попадания |
| `IcicleDamage` | 35 | урон одного удара |

`UpdateIcicleSeries` управляет серией; `ChooseTarget` выбирает игрока;
`QueueIcicle` сохраняет `Anchor`, `ImpactAt` и уникальный `Id`;
`ResolveIcicle` применяет урон единожды и проверяет положение/преграды.
Если цель перестала быть доступна, последующие отметки серии могут быть
пропущены. Изменение падения видимого меша в `RefreshPresentation` не должно
менять `ImpactAt`, область попадания или количество ударов.

## ICE-NOVA

**Контракт:** центральный кристалл готовит конус холодного ветра. Направление
фиксируется при начале предупреждения. Попавшим в конус замораживает ноги;
вся шкала холода от этого не заполняется мгновенно.

| Свойство | Сейчас | Назначение |
|---|---:|---|
| `NovaIntervalSeconds` | 18 с | первое ожидание и пауза после предыдущего удара |
| `NovaWarningSeconds` | 2,5 с | подготовка заклинания |
| `NovaRange` | 2600 см | дальность |
| `NovaHalfAngleDegrees` | 35° | половина угла; полный конус 70° |
| `NovaFootHealth` | 80 | прочность льда на ногах |

`BeginNova` сохраняет `NovaDirection` в координатах языка и `NovaImpactAt`;
`IsInsideNova` проверяет геометрию; `ResolveNova` проверяет доступного игрока,
контакт с ареной и преграды, затем вызывает `FreezeLegs`. Не направлять конус
вслед за игроком после начала предупреждения.

## ICE-FEET

Файлы: [MCToothCharacter.h](../Source/MessControl/Public/MCToothCharacter.h),
[MCToothCharacter.cpp](../Source/MessControl/Private/MCToothCharacter.cpp),
[MCToothMovementComponent.cpp](../Source/MessControl/Private/MCToothMovementComponent.cpp).

`IceLegHealth` — отдельное реплицируемое состояние. `FreezeLegs`,
`ClearFrozenLegs` и `OnRep_IceLegHealth` связывают его с блокировкой движения.
`CanWork` сохраняет возможность пользоваться руками и инструментом.
`SetFrozenLegs` останавливает движение; `SetMovementMode`, `SimulateMovement`
и `PerformMovement` не дают обойти блокировку обычным движением/root motion.

Кирка работает через `ResolveSwing`: сначала свой лёд на ногах, затем лёд
союзника рядом, затем кристалл зоны и обычные цели. Метод
`HitFrozenLegsWithPickaxe` разрешает самоспасение без наведения камеры вниз;
для союзника проверяет дистанцию, направление и преграды. Кирка — слот 2,
обычный удерживаемый ЛКМ. Смерть, захват, проглатывание и остановка события
освобождают ноги. Отогрев в зоне сам по себе лёд на ногах не разбивает.

Локальная подсказка — в [MCGameplayHUD.cpp](../Source/MessControl/Private/MCGameplayHUD.cpp).
`UpdateIceLegVisuals` отвечает только за оболочки на ногах. Вертикальное
следование за деформацией языка при `MOVE_None` — в `MCTongue.cpp`.

## ICE-METER

`UpdateFreeze` проверяет точку у стоп игрока через `IsSafePoint`.

| Свойство | Сейчас | Назначение |
|---|---:|---|
| `FreezeSeconds` | 12 с | заполнение с нуля вне работающей зоны |
| `ThawSeconds` | 4 с | снижение полной шкалы до нуля при согревании |

`FMCPlayerFreeze.Amount` ограничен диапазоном 0…1. При 1 применяется
смертельный урон через обычный компонент статуса. Это отдельная система от
`IceLegHealth`: можно согреваться и всё ещё стоять с замороженными ногами.

## ICE-INTEGRATION

- [MCDevCommands.h](../Source/MessControl/Public/MCDevCommands.h):
  действия `IceEvent` и `StopIceEvent`.
- [MCDevCommands.cpp](../Source/MessControl/Private/MCDevCommands.cpp):
  запуск нативного `AMCIceEvent`, тег ручного события `MC_DevKeyEvent`, замена
  предыдущего ручного события и остановка.
- [MCDevPanelWidget.cpp](../Source/MessControl/Private/MCDevPanelWidget.cpp):
  кнопки F3 «Зима близко — начать» / «остановить тест», текст инструкции.
- [MCSingleDayDirector.cpp](../Source/MessControl/Private/MCSingleDayDirector.cpp):
  `BeginKeyEvent` выбирает слот; `BeginIce` запускает холод;
  `Tick` ждёт завершения; `BeginDirector` включает следующий интервал.
  Успешно завершённое событие удаляется без вызова отмены.
- Порядок слотов и интервалов — [SingleDay.md](SingleDay.md) и
  `MCSingleDaySettings`. `DA_SingleDayDirector` настраивает директора поддержки,
  а не перечисленные здесь тайминги холода.

При переносе настроек холода в отдельный DataAsset/Blueprint обновить **оба**
пути создания: ручной F3 и `BeginIce`; затем этот документ и проверки defaults.

## ICE-LIFECYCLE

Сервер управляет зонами, попаданиями, шкалой, льдом на ногах и завершением.
`GetLifetimeReplicatedProps` передаёт клиентам настройки и состояние.
`ClearGameplay` удаляет скользкую поверхность, освобождает ноги и очищает
ожидающие угрозы. `Stop` отменяет событие, `Finish` завершает его;
`EndPlay` очищает локальное представление при удалении/смене мира.

Краткий эффект успешного разрушения передаётся через надёжный
`Multicast_FinishVFX`; отмена — через `Multicast_CancelVFX`. После победы
локальный эффект может жить ещё 1,65 секунды, но игровой холод уже снят.
После отмены он сразу удаляется. Не использовать получение последнего
`Stage=Complete` как единственный сетевой сигнал: директор может удалить
актор в том же кадре.

## ICE-PRESENTATION

**Polish — отдельная следующая задача.** Текущий визуал привязан к механике,
но его изменения не должны менять попадания, зоны или таймеры.

| Что менять | Якорь кода/ассета |
|---|---|
| Границы зон, отметки, конус на поверхности | `RefreshFloorGuides` в `MCIceEvent.cpp` |
| Покрытие языка и персонажа | `RefreshFrostSurface`, `RefreshIceCoatings`; `/Game/Gameplay/Cold/Frost/M_TongueFrost`, `/Game/Gameplay/Cold/M_PlayerIceCoating` |
| Материал крупных кристаллов/ног/сосулек | `/Game/Art/Materials/ice/MI_Ice` |
| Видимое падение сосульки | `RefreshPresentation` в `MCIceEvent.cpp` |
| Буря, подготовка, пыль, осколки и реакция ног | [MCIceEventVFX.cpp](../Source/MessControl/Private/MCIceEventVFX.cpp), `UpdateFromEvent`, `UpdateWindSheet`, `PlayBurst` |
| Материалы и частицы нового прохода | `/Game/Gameplay/Cold/VFX` |
| Авторинг материалов/меша | [author_ice_vfx.py](../Tools/Unreal/author_ice_vfx.py); `ArtSource/VFX/Ice/SM_IceShard.obj` |
| Авторинг Niagara | [MCIceVFXAssetLibrary.cpp](../Source/MessControl/Private/MCIceVFXAssetLibrary.cpp) |

`LoadedAssets` держит сильные ссылки на загруженные мягкие ссылки всё время
события; это нужно и клиенту без рендера. Повторяющиеся компоненты принадлежат
локальному контроллеру, короткие выбросы удерживаются с ручным возвратом в пул.
Бюджет: 10 повторяющихся компонентов и максимум 8 удерживаемых выбросов.
Выделенный сервер VFX не создаёт. Niagara не определяет игровой урон.

Последующий polish может отдельно улучшить нормали гранёного меша, звук,
пыль/снег и оформление UI. Эти пункты не являются незавершённой механикой.

## ICE-VALIDATION

Автотесты — [MCIceEventTests.cpp](../Source/MessControl/Private/Tests/MCIceEventTests.cpp):
13 проверок холода, включая таймеры до минимума, перекрытие, сужение,
размещение на асимметричной арене, урон, блокирующий кристалл, конус,
самоспасение, серверную авторитетность и очистку. Интеграционные проверки
директора и инструмента лежат в группах `MessControl.SingleDay` и
`MessControl.Inventory`.

```powershell
./Tools/TestFogBrawlAutomation.ps1 -Suites 'MessControl.IceEvent','MessControl.SingleDay','MessControl.Inventory'
./Tools/TestIceColdRework.ps1 -Mode Network -RequireVFX
./Tools/TestIceColdRework.ps1 -Mode Visual -Players 2 -RequireVFX
```

`TestIceColdRework.ps1` использует сохранённую `L_Mouth`, реальные игровые
таймеры, двух локальных peers и обычные Enhanced Input контакты кирки. Проба
расставляет игроков для проверок; это автоматизированный прогон. Проверяет
нативное разрушение кристаллов, освобождение ног у хоста и клиента, смену зон,
уклонение/урон, немедленное удаление завершённого события и отмену с угрозами.
`-RequireVFX` также проверяет ресурсы и очистку локального представления.

Логи и `Validation.json` — в выбранном `OutputDirectory`; `FrameTimes.csv`
содержит игровые времена снятых PNG. [BuildEventCaptureVideo.py](../Tools/BuildEventCaptureVideo.py)
собирает MP4 без ускорения времени; неснятые промежутки вырезает и перечисляет
в соседнем JSON. Windows cook/package этим прогоном не проверяется.
