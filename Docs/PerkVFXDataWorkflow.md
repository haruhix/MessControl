# DT_Perks и VFX: аудит и минимальное расширение
Дата: 2026-10-30. Статус: предложение архитектуры; реализация VFXProfile **не выполнена**. Проверены текущие исходники, рецепты авторинга, граф связей и сохранённые отчёты. Editor, C++, ассеты и сборка этим аудитом не менялись.
Уточнение 31.10.2026: слоты должны соответствовать реальным событиям конкретного перка и быть доступны в предпросмотре полного действия с настоящим зубиком. Общие Start/Loop/Impact не заменяют понятные назначения вроде «след летящей ракеты». Дополненный контракт: [VFXActionPreviewContract.md](VFXActionPreviewContract.md). Он тоже пока не реализован.

## Что уже существует

У активок визуальная форма хранится в обычных Niagara System: эмиттеры, материал, atlas/mesh/ribbon renderer, spawn rate/count, lifetime, скорость и кривые редактируются в Niagara. C++ задаёт подтверждённое событие, мировую точку/направление, параметры и срок владения компонентом. Урон, очистка, заряды, попадания и cooldown принадлежат gameplay.

Текущая цепочка:
`DT_Perks.EffectClass → UMCActiveSkillPerkEffect.ActionClass → Action CDO → StartVFX/LoopVFX/ImpactVFX/EndVFX/WarningVFX → NS → renderer → material/mesh/texture`.

В [MCPerkTypes.h](E:/DEVGAME/MessControl/Source/MessControl/Public/MCPerkTypes.h:29) сейчас нет ни VFXProfile, ни слотов Niagara, ни общего флага VFXEnabled. Есть `Parameters: TMap<FName,float>` и `EffectClass`. `ActionClass` находится в [MCActiveSkillPerkEffect.h](E:/DEVGAME/MessControl/Source/MessControl/Public/MCActiveSkillPerkEffect.h:16), а ссылки на NS — отражённые `EditDefaultsOnly TSoftObjectPtr<UNiagaraSystem>` на action-классе в [MCActiveSkillAction.h](E:/DEVGAME/MessControl/Source/MessControl/Public/MCActiveSkillAction.h:102). Их текущие native назначения сделаны путями в конструкторах шести action-классов; редакторская замена через Blueprint-наследника возможна, но это не назначение непосредственно в DT.

У пассивок другая картина: [MCPassiveFeedbackCue.cpp](E:/DEVGAME/MessControl/Source/MessControl/Private/MCPassiveFeedbackCue.cpp:73) строит Puff/Stroke/Arc и mesh sections в C++; [MCPassiveCombatCue.cpp](E:/DEVGAME/MessControl/Source/MessControl/Private/MCPassiveCombatCue.cpp:21) строит молнию и ghost meshes. Материалы редактируются как ассеты, но форма и движение этих прототипов частично зашиты в код. Простое добавление ссылки NS в DT не заменит их и не исправит смысл события.

## Реальные роли, а не обязательные стадии

| Перк | Используемые роли | Смысл |
|---|---|---|
| Active_Sponge | Start, Loop, End; отдельный Trail в текущей ревизии | Старт/контактная пена/окончание; остающийся на земле мыльный след |
| Active_Hook | Start, Loop, Impact, End | Запуск; Loop — управляемый трос; контакт; возврат |
| Consumable_Grenade | Start, Loop, Warning, Impact | Взведение; фитиль; опасная область; взрыв |
| Consumable_Air | Loop, Warning, Impact, End | Всасывание/Fill; переполнение; выдувание; End здесь означает разрыв мешка |
| Consumable_Rocket | Start, Loop, Impact | Дульный акцент; Loop — полётный след; взрыв |
| Consumable_Driver | Start, Loop, End | Поездка/щётки/остановка; определение сохранено, но сейчас выключено |

`Warning` нельзя потерять при схеме Start/Loop/Impact/End/Trail. `Trail` нужен там, где существует отдельный жизненный цикл; не следует автоматически добавлять второй trail каждой активке. Sponge Trail — собственный world-space компонент из [MCSpongeSoapTrail.cpp](E:/DEVGAME/MessControl/Source/MessControl/Private/MCSpongeSoapTrail.cpp:11); при обычном завершении он прекращает спавн и даёт существующим частицам затухнуть. Обычный Loop базового action сейчас выключается немедленно.

## Предложение: одна ссылка в DT, именованные слоты в профиле

Добавить в `FMCPerkDefinition` необязательное отражённое `TSoftObjectPtr<UMCPerkVFXProfile> VFXProfile`. Профиль — обычный `UDataAsset`, например `DA_VFX_Sponge`, с фиксированными именованными полями `Start / Loop / Impact / End / Warning / Trail`. Каждое поле содержит `Mode = UseDefault | Replace | Disabled` и `TSoftObjectPtr<UNiagaraSystem> System`. Явный Mode устраняет неоднозначность пустого поля: пустой профиль/UseDefault сохраняет текущий CDO; Replace выбирает NS; Disabled явно выключает косметический слой. Отсутствующий/невалидный Replace должен выдавать проверяемую ошибку авторинга и сохранять старый default. Warning требует отдельной проверки читаемости опасности.

Это удобнее общего массива `EventName → System` на первом этапе: реальные роли уже известны, видны в Details, нет опечаток/дубликатов и обещания, что любое имя события заработает само. Список допустимых ролей задаёт конкретный action. Профиль не должен сам выбирать socket, collision, damage или момент взрыва. Произвольные event slots имеют смысл только после появления фактического дополнительного события; для пассивок впоследствии нужны осмысленные события вроде HealApplied/ShieldReducedHit/NeighborCleaned, а не искусственные Start/Loop для каждого числа.

Первый шаг — только активки с сохранением fallback. Пассивки остаются legacy до отдельного перевода каждого настоящего триггера и художественной приёмки. Числовые modifiers без визуального события не получают постоянную ауру.

Путь художника после реализации: открыть строку DT → назначить DA → выбрать NS в нужном слоте → настроить форму в Niagara → сохранить → коротко проверить в игре. C++ потребуется один раз для интеграции схемы, затем замена совместимой NS не требует правки C++.

## Контракт параметров и владение

Существующий базовый контракт проверяет имя **и тип** User-параметров перед записью: `Color: LinearColor`, `Radius/Duration/Intensity/Fill: float`, `Direction: Vector3f`. Опциональные числовые row overrides — `VFXColorR/G/B`, `VFXRadius`, `VFXDuration`, `VFXIntensity`; отсутствие значения сохраняет authored default. Duration — длительность картинки, не запал/cooldown. Игровой BlastRadius для Warning/Impact остаётся источником правды.

Специализации: Hook Loop требует `TetherStart/TetherEnd: NiagaraPosition` (мировые точки); Air Loop обновляет Fill; Sponge Trail получает `Direction/SurfaceNormal/Velocity: Vector3f` и `PreviewMotion: bool=false`, с необязательными row ключами `TrailWidth/Lifetime/Opacity/SpawnSpacing` → `User.Width/Lifetime/Opacity/SpawnSpacing: float`. Профиль должен проверять совместимость NS с ролью; переименование параметра без обновления контракта не считается готовым назначением. В минимальном расширении не нужны произвольные string→variant parameter maps; текущие Parameters остаются совместимыми.

Профиль разрешается один раз до commit действия. Сейчас action получает только копию float Parameters; ссылка нового профиля сама на клиента не попадёт. Реализация обязана явно передать/реплицировать выбранный стабильный профиль или зафиксированные slot references, подготовленные из авторитетной строки, и применять их до первой активации. Niagara остаётся косметикой клиента; нельзя переносить в него подтверждение попадания/очистку. Это требование проектирования, не заявление о проверенной сети.

## Интеграционные границы

- Авторинг: `author_current_passives.py` пересоздаёт полные словари активных/пассивных rows; новый VFXProfile нужно добавить в canonical specs и сохранять при повторном авторинге. Иначе художник потеряет назначение. Узкое native-редактирование одной строки предпочтительно полному JSON round-trip.
- Магазин/сундук/F3: сохраняют PerkID и существующие фильтры, получают профиль через canonical DT. Их таблицам не нужен второй независимый набор NS. Driver остаётся выключенным.
- Manifest: отражённая soft цепочка `DT → DA → NS → renderer resources` совместима с обходом в MCPreloadManifest и волнами UMCWorldPreloadSubsystem. Нужно доказать попадание профиля, NS, материалов/текстур/mesh и требуемых MID в удерживаемые ресурсы **до Ready**, для каждой использующей локации. Это проверка реализации, а не автоматическая гарантия только от наличия поля.
- Export/инструменты: новый reflected row/profile должен экспортироваться индексом и сохраняться в import/export рецептах. До изменения схемы индекс — производное доказательство.
- Runtime: короткая solo PIE — первое использование из loading, старт/попадание/окончание/затухание и отмена/replacement/death. Проверить реальную камеру, лицо/руки, опасную область, отсутствие телепортного моста/обрыва хвоста и поздней shader/streaming работы. Компиляция NS отдельно от художественной приёмки; bounds/Effect Type/culling/overdraw — по реальному числу одновременно активных эффектов.
- Сеть, Windows build/cook/package, холодный packaged PSO и широкие suites — отдельная явно запрошенная работа; этот аудит их не запускает.

## Свежесть и пределы

Граф выбран через list_projects: `E-DEVGAME-MessControl`; связи PlaySkillCue сверены по настоящим action cpp. Coverage: отражённые заголовки partial, cited paths metadata_changed; широкие Source/Tools scope имеют известные пропуски и пагинацию. Поэтому выводы основаны на прочитанном source, а не отсутствии символа в индексе.

Asset status в начале аудита: 3760 catalog, 3684 exported, все snapshots schema_changed, build_current=false. Текущий полный экспорт DT не запрашивался, refresh/build не запускался. Отдельный [Authoring01/Report.json](E:/CodexWork/MessControl/Validation/SpongeRevision_20261030/Authoring01/Report.json) подтверждает сохранённый native patch Active_Sponge: CleanOnContact=1, BounceSteeringLock=.2, 133 rows, прочие поля/строки сохранены. Текущий SHA256 DT_Perks проверен и совпал: `88456cb0fed2008448630c2824b17589c8868cdfb1fb1e15f4245c0eb8346e16`.

Пять enabled active/consumable +27 passives и parked Driver подтверждены прежним узким saved readback в [AbilityReviewCatalog_20261030.json](E:/DEVGAME/MessControl/Docs/Validation/AbilityReviewCatalog_20261030.json:1) и текущим canonical recipe. Это не новый глобальный asset refresh. Статус нового Sponge Trail gameplay/preview принадлежит отчётам основного агента; данный аудит их не перезапускает и не выдаёт художественного одобрения.
