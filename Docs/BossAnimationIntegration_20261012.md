# Подключение анимаций боссов — 12 октября 2026

Пользователь разрешил зафиксировать и отправить локальные анимации боссов вместе
с кодом их подключения. Объём изменений: полный текущий сохранённый профиль
`/Game/Gameplay/CoreLoop/DA_NutRain`, 24 новых runtime-пакета и семь выбранных
нативных файлов, а также проверенный исходный Blender-файл и три инструмента
интеграции/инспекции. Документ описывает этот объём и выполненную проверку; видео
и сгенерированные отчёты остаются локальными.

## Changelog

- Маг использует проектные Mixamo-клипы Cast, Summon, Rain и HeavyCast из
  `NutWizard/Animations/MCMixamo`. Кривая `MCSpellGesture=1` сохраняет исходные
  жесты: процедурные накладки для кастования не перезаписывают позу рук.
- Обычный одноразовый Rain доходит до позы выпуска, удерживает её на время
  серверной фазы дождя, затем продолжает авторское завершение клипа. Зацикливание
  участка допускается при явных корректных кривых `MCSpellChannelEnd` и
  `MCSpellChannelPeriod`; переход из такого цикла смешивается с завершением.
- Косметические эффекты каста могут исходить от ладоней в авторском клипе.
  Положение ладоней задаёт видимую точку эффекта; расчёт попаданий остаётся
  на сервере.
- Танку подключены клипы реакции на удар и слом щита, состояния Idle/Walk без
  щита и соответствующий меш `SK_NutTank_NoShield`. Вес процедурной реакции
  учитывает авторское движение, чтобы не дублировать его поверх клипа.
- Профиль связывает текущие наборы Wizard MCStyleV6 и Tank MCSmoothV2/MCStyleV3–V5
  с выбором клипа и серверной фазой действия в нативной анимационной системе.

## Пакеты и происхождение

| Набор | Новых пакетов | Содержимое |
| --- | ---: | --- |
| Wizard MCMixamo | 4 | Cast, HeavyCast, Summon, Rain |
| Wizard MCStyleV6 | 5 | Idle, Walk, Melee, Hit, Death |
| Tank MCSmoothV2 | 6 | Idle, WalkLeft, WalkRight, Jump, Transform, ChargeLoop |
| Tank MCStyleV3 | 2 | Walk, Hit |
| Tank MCStyleV4 | 5 | UnshieldedIdle, UnshieldedWalk, ShieldBreak, ChargeTell, ChargeRecovery |
| Tank MCStyleV5 | 1 | Melee |
| Tank NoShield | 1 | `SK_NutTank_NoShield` |

MCMixamo содержит проектные копии ранее импортированных оригиналов Cast,
Summon и Rain. HeavyCast — отдельный пакет на основе Rain, используемый в том
числе для телепорта. Для этих копий добавлена кривая `MCSpellGesture=1`.
Новый Blender-пул из 32/39 анимаций в этот объём не импортирован.

Включён точный проверенный источник
[NutWizard_Original.blend](E:/DEVGAME/MessControl/ArtSource/NutAnimationStyle/Wizard/Original/NutWizard_Original.blend):
7 113 175 байт, SHA-256
`a26301457a083051e42515fd56aada5116cbce646d8cd430a9d97ad74b455a37`.
Хеш совпадает с исходником в существующем `ArtSource/NutWizard/ExportReport.json`.
Сохранение этого файла в проекте обеспечивает интегратору доступ к проверенному
источнику после клонирования репозитория.

Инструменты:
[integrate_nut_wizard_mixamo.py](E:/DEVGAME/MessControl/Tools/Unreal/integrate_nut_wizard_mixamo.py)
проверяет источник и создаёт/подключает проектные копии;
[check_boss_animation_commit_assets.py](E:/DEVGAME/MessControl/Tools/Unreal/check_boss_animation_commit_assets.py)
проверяет ссылки, скелеты, кривые и зависимости;
[inspect_active_boss_animation_versions.py](E:/DEVGAME/MessControl/Tools/Unreal/inspect_active_boss_animation_versions.py)
читает текущие подключения. Инспекция не сохраняет пользовательские пакеты.

Нативный объём: `MCNutBoss.cpp/.h`, `MCNutBossTypes.h`,
`MCNutBossAnimInstance.cpp/.h`, `MCNutCombatEffect.cpp` и
`Tests/MCNutBossAnimationTests.cpp`. Сравнение анимационных систем, физические
эксперименты, изменения лаборатории, дополнительное клиентское сглаживание,
накопление вращения шара и переписанные VFX удара танка остаются за пределами
этого объёма. Старые локальные документы BossAI/AnimationList сохранены.

## Выполненная проверка

- **Связи сохранённых ассетов прошли проверку:** live read-only inspection
  32 пакетов в цепочке зависимостей DA_NutRain, включая профиль, скелеты
  и нужные кривые. Все файлы из
  [AssetClosure.json](E:/DEVGAME/MessControl/Saved/Checks/MixamoCommitScope/AssetClosure.json)
  совпали с записанными SHA-256. Проверка не сохраняла пакеты, сохранила dirty
  state редактора и уже запущенный пользователем PIE.
- **Нативный объём выделен отдельно:**
  [NativeAnimationClosure.json](E:/DEVGAME/MessControl/Saved/Checks/MixamoCommitScope/NativeAnimationClosure.json)
  фиксирует семь файлов-кандидатов и их локальные зависимости. Для проверки
  подготовлен изолированный проект из HEAD и этих семи файлов без локальных
  экспериментов, изменений экспериментальных плагинов, Build.cs или `.uproject`.
- **Изолированная сборка Editor прошла:** 28 действий, 381,89 секунды, для HEAD
  и семи выбранных файлов. Лог:
  [IsolatedEditorBuild.log](E:/DEVGAME/MessControl/Saved/Checks/MixamoCommitScope/IsolatedEditorBuild.log).
- **Пять фокусных тестов прошли:** headless NullRHI на той же изолированной DLL,
  карта `/Engine/Maps/Entry`. Все получили `Success`; ошибок, проваленных и
  незапущенных тестов — 0. В отчёте `succeededWithWarnings=5`: всего 16
  предупреждений о том, что временные тестовые SkeletalMesh не имеют render data.
  Это проверки логики анимационных снимков, а не тест отрисовки этих мешей.
  Проверены `AuthoredSpellGesturesAndChannel`, `ChargePhasesAndServerClock`,
  `MageCastGesturesAndPalmContact`, `MageMeleePreservesShove` и
  `OptionalChargeFallback` в группе `MessControl.CoreLoop.NutBoss.Animation`.
  [Отчёт тестов](E:/DEVGAME/MessControl/Saved/Checks/MixamoCommitScope/AnimationTests/index.json)
  и [лог](E:/DEVGAME/MessControl/Saved/Checks/MixamoCommitScope/AnimationTests.log).
  Пользовательский PIE не останавливался и не был затронут.
- **Существующая solo-демонстрация завершена:**
  [Wizard_Mixamo_Gameplay.mp4](E:/DEVGAME/MessControl/Artifacts/WizardMixamoIntegration/Review/Wizard_Mixamo_Gameplay.mp4)
  показывает Fireball, Summon, Rain и Teleport в нативных игровых циклах
  лаборатории. Это 850 настоящих кадров Unreal при 60 FPS, 14,17 секунды,
  без интерполяции и изменения скорости. Отчёт зафиксировал ноль нарушений,
  ноль неудачных игровых прогонов и отсутствие ошибок восстановления.
  Все 29 исходных пакетов в render manifest совпадают с текущими сохранёнными
  файлами. Полное декодирование существующего видео также прошло проверку.
  Метаданные:
  [Video.json](E:/DEVGAME/MessControl/Artifacts/WizardMixamoIntegration/Review/Wizard_Mixamo_Gameplay.Video.json)
  и [Capture.json](E:/DEVGAME/MessControl/Artifacts/WizardMixamoIntegration/Review/Capture.json).

## Границы проверки

Демонстрация охватывает выбранные solo-циклы мага и существовала до изолированной
проверки объёма коммита. Полный бой под управлением Director, сетевое поведение
и Walk в лаборатории отдельно не проверялись. Наличие клипов танка и их
совместимость со скелетом подтверждены проверкой ассетов; это не заменяет
полного игрового прогона всех состояний танка.

Фокусное обновление asset index завершилось ошибкой LNK1104 из-за занятой DLL
редактора при пользовательском PIE. Изолированный проект использует отдельную
DLL и сохраняет этот сеанс.
Его незавершённые/устаревшие данные не используются как текущие доказательства;
для описанной проверки применены live inspection и сохранённые пакеты.
Cook и packaging не запускались. PR не создаётся.
