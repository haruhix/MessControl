# Учебная VFX-площадка

Карта: `/Game/Tests/VFXPractice/L_VFXPractice`. Это отдельный World для редактирования учебных Niagara-эффектов рядом с нашими зубиками: шесть зон, восемь выбираемых станций. Режим — `MCVFXLabGameMode`, свободный spectator. Размещённые `MCToothCharacter` служат reference масштаба/силуэта; игрок ими не управляет.

**Состояние 30.10.2026:** короткая solo-проверка прошла, 12/12 проверок. После фидбека «вместо полей виден код» добавлен отдельный `NS_Practice_FoamBasics` и назначен на Free Play / Practice_01; новый solo-прогон также прошёл 12/12. Основной агент лично просмотрел runtime-движение и кадры; художественная приёмка открыта. Проверены учебные визуальные станции, без gameplay лечения, очистки пола и настоящего попадания крюка.

## Открыть и сравнить

1. В Content Browser перейти в `/Game/Tests/VFXPractice`, найти `L_VFXPractice` и открыть двойным нажатием.
2. До Play выбрать в World Outliner actor `Practice_01`, `Practice_02` и т. д. В его Details выбрать Niagara-компонент **PracticeFX**. Поле **Asset** содержит назначенный NS; открыть его кнопкой рядом с полем либо найти в Content Browser.
3. Записать исходное число. Изменить один параметр: цвет, размер, lifetime либо частоту. В Niagara редактируются эмиттеры/модули, а renderer ведёт к используемым mesh/material. Учебные renderer-материалы и текстуры скопированы под `/Game/Tests/VFXPractice`; их можно менять для упражнения.
4. Сохранить изменённый NS/материал. Если менялись `PracticeFX → Asset` или параметры размещённой станции — сохранить карту. Нажать Play, дождаться сообщения `VFX practice ready`, выбрать площадку и сравнить одинаковый ракурс.
5. Для следующего варианта остановить PIE, изменить одно число и повторить. Instance-правки во время PIE обычно исчезают после Stop; перенести полезное значение в edit-world actor или учебный NS явно.

Общие runtime-ассеты персонажа используются как настоящий reference. Их изменение относится к отдельной задаче персонажа. Для упражнений скопированы NS, renderer-материалы, родители material instances и текстуры. Material Functions, Material Parameter Collections и color curves остаются общими read-only зависимостями.

## Камера и управление

| Ввод | Действие |
|---|---|
| F3 | Открыть/закрыть каталог станций |
| PageUp / PageDown | Выбрать соседнюю станцию и переместить камеру |
| R | Сбросить симуляцию выбранной станции |
| P | Пауза/продолжение lab |
| WASD + мышь | Свободное перемещение и взгляд |

Перед вводом щёлкнуть по игровому viewport. Начальный ракурс — общий обзор: приблизиться WASD/мышью. F3 здесь открывает каталог площадок; выдача активок арены находится в другом игровом контексте. Gallery и checked=0 — унаследованные служебные надписи общего lab, автоматической оценки упражнения нет. После выбора подогнать камеру мышью и сохранить одинаковый ракурс A/B. Проверка штатной камеры способности выполняется при отдельной gameplay-интеграции.

В solo-прогоне команды контроллера вызывались через native reflection. Наблюдались открытие/закрытие навигации, переходы, пауза и reset; физическое нажатие горячих клавиш в этом прогоне не проверялось.

`R` очищает текущую симуляцию, возвращает источник в исходную точку и применяет параметры станции. Если станция была на паузе, сброс сохраняет паузу. Сохранённые NS-defaults и параметры карты не откатываются. `P` останавливает/возобновляет lab; полный повтор подготовки ресурсов выполняется новым запуском Play.

## Зоны и упражнения

| Зона / actor | Начальный NS | Одна полезная проба |
|---|---|---|
| 01 Free Play / `Practice_01` | `NS_Practice_FoamBasics` | Initialize Particle: Lifetime1,6/0,8, затем отдельно Size35–65/55–85, Color picker; без формул |
| 02 Ground Trail / `Practice_02` | `NS_Practice_Trail` | `TrailWidth` 60/110 при одинаковых Lifetime/Spacing; старая пена остаётся в мире |
| 03 Heal Studio / `Practice_03` | `NS_Practice_HealSeed` | Сделать свой Healing NS, назначить в PracticeFX и сохранить лицо читаемым |
| 04 Impact / `Practice_04` | `NS_Practice_Impact` | Сократить lifetime или размер акцента возле зубика |
| 05 Slope + Wall / `Practice_05` | `NS_Practice_Trail` | Прилегание/ориентация на склоне 20°, боковой вид у стены |
| 06 Contrast / `Practice_06_Dark`, `_Pink`, `_Light` | `NS_Practice_Burst` | Одинаковый burst на трёх фонах; менять только один параметр |

Все пять учебных NS лежат в `/Game/Tests/VFXPractice/Effects`. Контраст имеет три экземпляра, поэтому в каталоге восемь станций. Free Play теперь показывает непрерывный Basics с `bAutoRepeat=false`, P/R останавливают/сбрасывают его. Impact и Contrast используют короткие повторяющиеся визуальные пробы. Ground Trail и Slope двигают компонент эффекта; это упражнение формы/пространства, без уборки поверхности, настоящего действия губки, контакта крюка или ходьбы по стенам.

## Первый урок без выражений

Открыть `/Game/Tests/VFXPractice/Effects/NS_Practice_FoamBasics`, выбрать эмиттер в System Overview. В **Particle Spawn → Initialize Particle** вводятся обычные Lifetime и Uniform Sprite Size Min/Max; Color открывает picker. Затем **Emitter Update → Spawn Rate** задаёт частиц/с, **Shape Location → Sphere Radius** — область рождения. В **Particle Update → Scale Color → Scale Alpha** — встроенный Float from Curve с редактируемыми точками, без inline HLSL.

Первый опыт: только Lifetime1,6 → 0,8, остальные поля сохранить. В Basics нет User Parameters: это намеренное упрощение первого урока. CPU/world-space sprites направлены FaceCamera; образец неподвижный и не должен приклеиваться к наклонному полу. Это не замена механизма Trail, очистки или лечения. Подробнее — [первые клики в гайде](../Tools/VFXGuide/index.html#expression-help).

## NS-default и override станции

**NS** хранит форму и поведение: эмиттеры, Spawn/Initialize/Update, кривые, renderer, material и User-defaults. Сохранённое изменение действует на все экземпляры этого NS. **Actor/component override** относится к конкретной размещённой станции. Если NS-default меняется, а площадка выглядит прежней, проверить overrides.

`BP_PracticeStation` содержит:

| Поле | Что задаёт |
|---|---|
| PracticeFX → Asset | Какой Niagara System показывает станция |
| bTrail | Передачу параметров следа и движение источника |
| bAutoRepeat / RepeatSeconds | Автоповтор короткого визуального цикла |
| TrailSpeed / TrailAmplitude | Скорость фазы и размах учебного движения источника |
| TrailDirection | Локальную ось движения источника |
| SurfaceNormal | Мировую нормаль для ориентации частиц |
| TrailWidth / TrailLifetime | Overrides `User.Width / User.Lifetime` |
| TrailOpacity / TrailSpawnSpacing | Overrides `User.Opacity / User.SpawnSpacing` |

Для `bTrail=true` станция устанавливает `User.PreviewMotion=false`, обновляет мировые Velocity/Direction и передаёт перечисленные overrides при запуске/сбросе. Поэтому для следующего, продвинутого Width A/B менять **TrailWidth на actor**, а для формы/кривых/renderer — учебный NS. В отдельном Niagara asset-preview `PreviewMotion=true` демонстрирует движение без станции. Более широкий диапазон числа не отменяет clamp внутри графа.

## Сделать собственное лечение

`NS_Practice_HealSeed` — копия пенного `NS_Sponge_End`. Это явно подписанная визуальная заготовка. Она не является игровым лечением и не меняет HP. Текущий игровой passive heal остаётся native procedural geometry.

1. В `/Game/Tests/VFXPractice/Effects` создать свой Niagara System, например `NS_MyHealing`, либо продублировать учебную заготовку.
2. Сначала выбрать одну главную форму восстановления: место рядом с зубиком, размер, появление/уход. Оставить лицо видимым. Затем добавить одно вторичное движение.
3. Выбрать `Practice_03 → PracticeFX → Asset` и назначить собственный NS. Для неподвижного короткого образца оставить `bTrail=false`; проверить `bAutoRepeat / RepeatSeconds`, чтобы соседние циклы не спорили друг с другом.
4. Сохранить NS и карту, повторить Play и сравнить полный цикл. Принести репетитору короткий клип, параметры, прогноз и объяснение выбора.

Подключение образца к настоящему HealApplied/HP, timing и lifecycle — отдельная интеграция. Standalone Impact таким же образом проверяет внешний вид burst, а совпадение с настоящим hit проверяется вместе с игровым действием.

## Изоляция, подготовка и свидетельства

Учебный мир использует собственные `BP_PracticeStation` и `BP_PracticeBootstrap`, четыре копии NS и отдельный новый Basics, renderer-материалы, родителей material instances и текстур. Общие Material Functions/MPC/color curves остаются read-only. Bootstrap готовит ресурсы до старта lab. После назначения нового NS повторить Play, чтобы он попал в подготовку первого показа. Сообщение об ошибке подготовки означает, что эффекты остаются остановлены; посмотреть `MCWorldPreload` в логе.

Главные карты, игровая экономика и таблица `DT_Perks` не входят в этот учебный цикл. Новый Basics: [author_beginner_foam.py](../Tools/Unreal/VFXPractice/author_beginner_foam.py). Исходная карта/остальные станции: [author_practice_map.py](../Tools/Unreal/VFXPractice/author_practice_map.py), [Blueprint authoring](../Tools/Unreal/VFXPractice/author_practice_blueprints.py), [renderer isolation](../Tools/Unreal/VFXPractice/isolate_practice_renderers.py). Обычный 51-сценарный [VFXLab](VFXLab.md) остаётся отдельной картой.

Обзор исходного полигона **до замены Free Play на Basics**: [SoloReport.json](E:/CodexWork/MessControl/Validation/VFXPracticeSandbox_20261030/SoloReport.json), [MP4](E:/CodexWork/MessControl/Validation/VFXPracticeSandbox_20261030/SandboxTour.mp4), [GIF](E:/CodexWork/MessControl/Validation/VFXPracticeSandbox_20261030/SandboxTour.gif). GIF около 14,4 с, MP4 — полный обзор: реальное floating-окно 1286×760 с viewport 1280×720. Основной агент лично просмотрел runtime-движение и кадры.

Новая версия Basics: [Saved.json](E:/CodexWork/MessControl/Validation/VFXBeginnerControls_20261030/Saved.json), [SoloReport.json](E:/CodexWork/MessControl/Validation/VFXBeginnerControls_20261030/Runtime/SoloReport.json), [новый MP4](E:/CodexWork/MessControl/Validation/VFXBeginnerControls_20261030/Runtime/SandboxTour.mp4). 12/12 true; основной агент посмотрел новый runtime-образец. Производственный NS_Sponge_Trail не изменён на диске; его текущие несохранённые правки и несохранённое состояние BP_PlayerCharacter сохранены без вмешательства.

Все 12 проверок отчёта true: gate наблюдался до release; восемь станций были активны хотя бы один раз; след двигался; пауза выключила все станции; reset наблюдался с `Cycles=1`; навигация открывалась/закрывалась; day scheduler отсутствовал; диск исходного BP персонажа сохранился неизменным. Часть этих проверок — отдельные подтверждения одного сценария, а не 12 различных gameplay-механик. Навигационные команды — native controller/reflection, без физического ввода. Сеть, Windows build/cook/package, голос, холодный packaged PSO и измерения GPU/overdraw не выполнялись.

Курс: [Tools/VFXGuide/index.html](../Tools/VFXGuide/index.html). Числа в HTML-схеме не передаются в Unreal; учебные отчёты не отправляются в чат автоматически.
