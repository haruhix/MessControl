# GC-пауза после разлома еды — 2026-11-09

В `Config/DefaultEngine.ini [SystemSettingsEditor]` включён экспериментальный
incremental reachability с мягким бюджетом 0,002 с. Изменение применяется при
обычном запуске Editor и наследуется PIE. Частота GC, параллельная сборка,
уничтожение объектов и проверки корректности не изменены. Новых ассетов,
материалов и анимаций нет; пересборка бинарников для ini-изменения не нужна.

Это частичное уменьшение задержки редактора. Не исправление всего Draw time
и не гарантия кадра 16,7 мс. Для обычного Game/packaged startup движок выбирает
`SystemSettings`, поэтому здесь не включена экспериментальная runtime-политика.
Свежий packaged запуск в этой проверке не выполнялся.

## Причина и проверка

Предыдущая трасса зафиксировала 70,032 мс в `ConditionalCollectGarbage`:
37,806 мс обхода ссылок и 25,387 мс предварительных callbacks, включая
17,739 мс Python GC. Вложенные интервалы не складываются. Установленный
Python-плагин перед UE GC безусловно вызывает собственный полный collector;
настройки бюджета этого callback в его текущем API нет.

В исходниках игрового пути не найден ручной вызов GC при разрезе. Обычная
утилизированная еда получает lifespan 3 с; weak-кэши не удерживают акторов.
Один разлом винограда создаёт минимум 405 проектных UObjects, однако естественный
GC нового замера сканировал 223 977 объектов всего Editor-процесса. Эта проверка
не доказывает отсутствие всех утечек или отсутствие влияния выделений еды.

Перед пилотом проверены 580 заголовков Source/локальных Plugins:
5 545 деклараций UPROPERTY без обнаруженных сырых pointer-member полей.
Проверены проектные FGCObject и plain-data workers нарезки. Это ограниченный
аудит исходников, а не сертификация потоковой безопасности всех engine/plugins.
Epic помечает механизм экспериментальным и описывает ограничения потоков:
[Incremental GC](https://dev.epicgames.com/documentation/en-us/unreal-engine/incremental-garbage-collection-in-unreal-engine).

## Замер

Один собственный Editor, solo PIE, сохранённый Bacon/SM_GrapesPurple, масштаб 40,
настоящий нож и неизменённая игровая нарезка. Loading/prewarm дождались до
таймингов. В timed-окнах нет захватов, анализа Insights или обхода всех UObjects
в Python. Native class counts сняты только на границах фаз.

1. 10 с обычного reachability с диагностическим периодом GC 2 с.
2. 12 с incremental/0,002 с при том же периоде, с настоящим ударом и
   `gc.VerifyNoUnreachableObjects=1`.
3. 65 с incremental с восстановленными исходными периодом 61,1 с и verification=0.
   Зафиксирован один завершённый естественный GC; затем все process overrides
   восстановлены перед обычным завершением PIE.

Default-фаза содержит целый продукт, следующие — разрезанные части; это не
строго совпадающие по популяции A/B-сцены. Первые циклы ускоренных фаз считаются
прогревом. Верификационная фаза проверяет ссылки/жизненный цикл и не используется
как обычный performance-результат: одна её verification-проверка заняла 37,224 мс.

После исключения первого ускоренного default-цикла максимум GC составил
59,047 мс, а максимум содержащего GC engine tick — 87,613 мс (четыре цикла).
В естественной incremental-фазе это 34,003 и 63,635 мс соответственно.
Куски во второй сцене добавили 402 PIE-contained UObjects; общий процесс
содержал около 218 тысяч. Сравнение показывает уменьшение отдельной паузы,
а не точно совпадающую по нагрузке оценку общего FPS.

| Естественная incremental-фаза | Измерено |
| --- | ---: |
| Максимум `ConditionalCollectGarbage` за один проход | 34,003 мс |
| Предварительные callbacks, включая Python | 15,786 мс |
| Из них `PyUtil::CollectGarbage` | 15,666 мс |
| Максимум reachability, включая начальную маркировку | 18,147 мс |
| Число проходов reachability | 8 |
| Полный `FEngineLoop::Tick` с самым длинным GC | 63,635 мс |
| Native CSV interior: максимум GT / FrameTime | 60,493 / 63,601 мс |
| Native CSV interior: p95 GT / FrameTime | 31,621 / 31,388 мс |

Суммарные 56,752 мс GC в native analytics распределены по нескольким кадрам;
это не один кадр. Мягкий бюджет 2 мс не является потолком: первый проход
reachability занял 18,140 мс с маркировкой, последующие — примерно 1,2–3,8 мс.
Gather в установленном UE 5.8.1 остаётся синхронным; неработающий здесь
`gc.AllowIncrementalGather` не включался. Полные GC/смена карты также могут
выполняться синхронно.

Таблица CSV исключает первые/последние две строки каждой фазы. Native
`FrameTime` может относиться к предыдущему кадру относительно GC event, поэтому
привязка GC к кадру сделана по вложенности raw Insights scopes. Пересекающие
границы scopes могут включать snapshot вне региона: кажущийся пик 474 мс
включал предшествующий native class-count report. Максимум полностью лежащих
в natural-регионе engine ticks — 69,479 мс; гладкий FPS не заявлен.
Почти нулевой CSV RenderThreadTime не интерпретируется как настоящий Draw.

Разрез сохранил 47 результатов: 9 принятых и 38 физических частей, один
BEGIN/COMMIT и ни одной отмены. Контакт → завершение 247,155 мс измерен при
ускоренном GC с verification; это не замена прежнему нормальному cut-замеру
200,629 мс. Ошибок ссылок/GC и dirty content/maps не обнаружено.

После изменения ini Editor отдельно перезапущен. Readback подтвердил
reachability=1, budget=0,002, interval=61,1, parallel=1,
verification=0 и analytics=0. Оба собственных Editor закрыты с проверкой PID,
отсутствия PIE и несохранённых пакетов. Игровые настройки исходного профиля
не изменялись. Сеть, новые сборки и ZIP не запускались.

## Воспроизведение и возврат

Исходники установленного движка: `SystemSettings.cpp:34–108` выбирает и читает
`SystemSettingsEditor`; `LaunchEngineLoop.cpp:2814–2820` передаёт режим Editor
до применения настроек GC. Явный `-SystemSettings=` или console override может
переопределить маршрут. Возврат — установить здесь
`gc.AllowIncrementalReachability=0` и перезапустить Editor.

Локальные рецепты, исходные данные и записи сохранены на E:

- [ProfileGCFood.py](E:/CodexWork/MessControl/Validation/GCFood_20261109/ProfileGCFood.py),
  [SummarizeCSV.py](E:/CodexWork/MessControl/Validation/GCFood_20261109/SummarizeCSV.py).
- [Report.json](E:/CodexWork/MessControl/Validation/GCFood_20261109/Run_20261108T231614Z/Report.json),
  [CsvSummary.json](E:/CodexWork/MessControl/Validation/GCFood_20261109/Run_20261108T231614Z/CsvSummary.json).
- [Raw GameThread export summary](E:/CodexWork/MessControl/Validation/GCFood_20261109/Run_20261108T231614Z/GCExport_20261108T232059Z/ScopeSummary.json),
  [First-cycle exclusion](E:/CodexWork/MessControl/Validation/GCFood_20261109/Run_20261108T231614Z/GCExport_20261108T232059Z/FirstCycleExcluded.json),
  [Restart readback](E:/CodexWork/MessControl/Validation/GCFood_20261109/ConfigReadback/EffectiveGC.json).

Скрипты запускаются через существующий `Tools/Unreal/remote_python.py` только
после передачи точного PID собственного Editor. Для ручного повторения нужны
тот же grape fixture/настоящий нож и три фазы выше; `obj gc`, Python
`collect_garbage` и полный forced GC не заменяют естественный periodic GC.
