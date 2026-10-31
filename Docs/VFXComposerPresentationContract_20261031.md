# Общий контракт представления для Composer

Source-only audit, 2026-10-31. Ни runtime-код, ни сохранённые ассеты этим аудитом не изменялись. Полный saved export завершён: 3834/3835 текущих пакетов, build current; исключён legacy EUB_SnapToActor с ошибкой компиляции. Ссылочные ограничения и актуальные чтения DT находятся в SavedEvidence.json. Документ не делает выводов об отсутствии Blueprint/asset-потребителей.

## Вывод

Композиции уже могут сохранять независимые Niagara Systems с их собственными sprite/mesh/ribbon/GPU renderers. Ограничение находится в привязке: Composer читает и назначает профиль исключительно через DT_Perks, а готовит/запускает его через AMCActiveSkillAction. Добавление строк каталога или установка capability-флага не подключит эффекты боссов и карты к этому пути.

Минимальный путь: сохранить существующие сериализованные типы и проверенный проигрыватель, отделить канонический источник профиля от DT, вынести подготовку/доставку визуальных событий из active-action владельца и подключать реальные игровые hooks небольшими адаптерами. Не создавать фиктивные строки перков для боссов или событий карт.

**Ограничение этого минимального Niagara-пути:** весь существующий визуал игры не сводится к NS. Source inventory отдельного агента подтверждает procedural meshes/material uniforms в NutCombat/Ice/Throat/care, UMG у рыбы и instanced mesh/UMG/SFX у монет. Поэтому общий presentation contract должен быть нейтрален к типу представления. Один только Niagara slot не выполняет требование «всё редактируется в Composer».

## Две разные степени готовности

1. **Подключено событие.** Реальное игровое событие конкретной механики получает SourceID, умеет проигрывать выбранную сохранённую композицию и проверяется в своём настоящем контексте. Это позволяет заменить прежний визуал авторским, но ещё не делает прежний procedural эффект редактируемым.
2. **Перенесён исходный эффект.** У исходного native mesh/material/decal/UI эффекта доступны настоящие authorable resources/settings/curves, либо он корректно переавторен в NS с сохранением характера картинки. Только эта стадия означает, что существующий эффект полноценно редактируется в Composer.

Непрозрачный слой `NativeDefault`, который только вызывает прежний C++ визуал, не засчитывается как редактируемый исходный эффект. UseDefault остаётся честным fallback. Для общих cue actors должен сохраняться реальный PerkID/SourceID, чтобы изменение одного перка не перекрашивало все остальные источники того же Kind.

Ни одна из предложенных ниже generic API/схем этим аудитом не реализована. Текущий результат — source-backed список seams и план; широкая реализация начинается после полного saved inventory и согласованных конкретных батчей.

## Проверенные seams

| Область | Текущий source | Что ограничено |
|---|---|---|
| Каталог | `MCVFXActionCatalog.cpp:20,35,78,86` | Метаданные независимы, но editable-флаг выводится из preview-provider bool. Реального writer/load-binding в записи нет. |
| Session загрузка | `MCVFXActionSession.cpp:253–271` | `CanonicalRowID`, DT lookup, `Row->VFXProfile`; editable принудительно требует Row. |
| Stale guards | `MCVFXActionSession.cpp:143–156` | Сравнение профиля/композиций полезно, однако канонический owner всегда строка DT. |
| Session Apply | `MCVFXActionSession.cpp:445–512` | Новые master/runtime/profile сначала сохраняются; последним меняется DT. Rollback знает только Row/Table. |
| Shared preview | `MCPerkActionPreviewPanel.cpp:664–683` | Context без SourceRowID допустим, но получает пустой профиль вместо реального non-perk owner. |
| Preview injection | `MCPerkPreviewRunner.cpp:108,381–406` | Профиль попадает в `Definition.VFXProfile`. У Context без Definition override не доходит до реального boss/map actor. |
| Ресурсы preview | `MCPerkPreviewRunner.cpp:260` | Подготовленные визуалы берутся у Action; Context/passive owner пока не предоставляет общий resource view. |
| Сохранение/назначение | `MCPerkPreviewEditorLibrary.cpp:61,176,195` | Проверки persisted runtime DA/NS и типов уже годятся; writer назначения всё ещё находит DT_Perks row. |
| Контракт событий | `MCPerkVFXProfile.cpp:18–21,59–65`; `MCActiveSkillAction.cpp:119–154` | Пять имён Start/Loop/Impact/End/Warning и лимит 5 прошиты в legacy active path. |
| Независимый player | `MCPerkVFXCompositionPlayer.h:32–44`; `.cpp:33–70` | Уже принимает immutable snapshot, world frame, Subject/socket и optional native anchor. Ни поиска DT, ни урона здесь нет. |
| Native inputs | `MCPerkVFXCompositionPlayer.cpp:73–103` | Базовые gameplay inputs перечислены; LWC Position копируется с native anchor. Простая универсализация без точных типов потеряет beam/tether/mesh-sampling данные. |
| Prewarm | `MCPreloadManifest.cpp:47–124,149–196` | Уже обходит reflected DataAssets/DT/maps/arrays и Niagara render resources; нужно дать ему достижимый reflected non-perk root. |

Важная потенциальная несогласованность для новых Context providers: каталог способен показать `bSupportsProfileOverride=true` без Row, а Session затем отключает редактирование через `&& Row`. Сейчас это не доказывает дефект существующего подключённого контекста; при расширении capability должна вычисляться по наличию настоящего runtime binding и writer, а не по одному bool.

## Один профильный контейнер без массовой миграции ассетов

`UMCPerkVFXProfile` содержит только `Slots`. Его историческое имя не требует наличия перка или DT. На первом этапе оставить class path, `Slots`, enum values и soft references неизменными.

Добавить отдельную проверку `ValidateForEvents(AllowedEventIDs, Error)`: уникальные известные данному source события, ограниченное количество, существующий `ValidateSlot`, точные параметры вложенных NS. Существующий `Validate(Error)` продолжает вызывать legacy пять событий; active callers и старые ассеты сохраняют прежнюю семантику.

Новые boss/map контракты передают собственный список событий в общий dispatcher. Это не повод глобально разрешить произвольные неизвестные события в старом active path. Не переименовывать UCLASS и не переносить сериализованное свойство Slots в другой base class в первой итерации. Возможное косметическое переименование типа — отдельная миграция с Core Redirects и полным saved-asset reference export.

## Разделить три существующие роли

### 1. Source/event contract

Одна зарегистрированная запись описывает реальную механику, не создаёт её:

```cpp
struct FMCVFXPresentationContract
{
    FName SourceID;                          // стабильная идентичность механики
    TArray<FMCVFXEventDescriptor> Events;     // имя, label, one-shot/loop, реальные anchors/data
    FName ProfileBindingID;                  // канонический persisted owner
    FName PreviewProviderID;                 // отдельная возможность полного preview
};
```

События проверяются по конкретному native owner. Для нового заклинания не добавляется глобальный enum/switch в Composer. Набор renderers остаётся внутри выбранного оригинального NS; Composer не должен разбирать его на примитивы или пересобирать эффект из guessed параметров.

### Полный охват существующих native представлений

В общей модели событие содержит presentation layers с **зарегистрированным переиспользуемым pattern provider**, а не только ссылку на NS. Минимальные семейства:

- Niagara: нынешняя independent composition; все исходные renderers и typed User inputs сохраняются.
- Native mesh/material: реальная процедурная/инстансная геометрия владельца, её material/instance/curve controls и cosmetic transform; игровой shape остаётся native read-only данными.
- Decal/material overlay: материал, surface/mesh anchor, cosmetic controls и штатная lifecycle-последовательность.
- Widget: реальный world/screen UI anchor и widget/brush/curve resources; fishing flight не подменяется world particles.

Контракт pattern provider: reflected settings schema + CollectResources + Prepare + Begin/Update/End/Reset. Niagara — первый проверенный provider, а не ограничение всего каталога. Source binding передаёт native mesh/material/widget настройки через этот же интерфейс; не нужно превращать каждую способность в новый hardcoded renderer type.

Существующий `FMCVFXCompositionLayer` хранит только NS и потому не является финальным общим форматом для всех этих семейств. Не добавлять в него guessed универсальные поля и не утверждать full support до настоящей интеграции. Общий editor binding должен возвращать neutral presentation view/schema и выбирать правильный сериализованный профиль через provider. Legacy UMPerkProfile остаётся одним supported provider для Niagara-пути; native pattern settings могут храниться в их реальной runtime settings DA. Если позже нужен единый новый generic profile asset, его вводят рядом с legacy profile, а не ломают типы старых DT references массовым переименованием.

### 2. Каноническая editor binding

Session перестаёт знать `UDataTable` и `FMCPerkDefinition`. Интерфейс выбранного binding предоставляет:

```cpp
ReadCanonical() -> {Profile, OwnerIdentity, SnapshotGuard};
GetEventContract() -> Events;
ValidateUnchanged(SnapshotGuard) -> bool;
AssignSavedProfile(NewSavedProfile, SnapshotGuard) -> CommitResult;
PreparePreviewOverride(ActualPreviewOwner, TransientProfile) -> bool;
```

Первый provider — существующая DT_Perks row/VFXProfile. Второй — отражённое свойство/запись обычного runtime DataAsset, доступного босcу/карте. Простая общая non-perk root DA с `Profiles[SourceID]` позволит писать один проверяемый reflected root вместо случайного CDO или текущего actor instance. Этот root обязателен в manifest соответствующих локаций. Если у конкретной механики уже есть подходящая profile/settings DA, provider использует её; второй конфликтующий источник истины не создаётся.

Prewarm не должен превращаться в загрузку всех эффектов проекта на каждой карте: нынешний collector рекурсивно обходит все отражённые entries DataAsset. Поэтому при общей глобальной библиотеке manifest получает проверенный subset реально используемых данной локацией профилей; более простой первый вариант — существующие per-location/settings roots. Не добавлять одну огромную библиотеку без фильтра в общие roots хаба.

SnapshotGuard включает identity owner/property/row/key, сохранённый owner digest, текущее assigned profile value, содержимое профиля и referenced runtime compositions. Перед единственной канонической записью повторить dirty/stale checks. Новые master/runtime/profile ассеты сохраняются до owner write. CommitResult различает успешное сохранение и только in-memory изменение; на ошибке вернуть прежнее назначение и сохранить черновики. Не переиспользовать устаревший raw Row pointer после load/save.

`AssignSavedProfileToPerk` и Rocket wrappers остаются compatibility фасадами DT provider. Новая общая API принимает SourceID и делегирует provider, не выбирает DT по умолчанию. `EnsureContext` читает профиль через тот же binding. В preview Context добавить независимый `PresentationOverride`/prepared-presentation handle: nullable Definition остаётся только для механики перка.

### 3. Runtime presentation dispatcher

Из `AMCActiveSkillAction::PrepareVFXBindings`, `PlayPreparedComposition`, single-system spawn и loop cleanup вынести общий native presentation owner/component. Он:

- подготавливает immutable snapshots и все nested NS до resource barrier;
- получает real event ID, world frame, Subject/socket, native data и lifetime scope;
- запускает single NS или существующий independent composition player;
- обновляет/заканчивает loop по настоящему событию владельца;
- предоставляет prepared resource list для loader и private preview;
- очищает только собственные визуалы на interrupt/death/reset/EndPlay.

Action path становится thin legacy adapter над этим dispatcher; его существующие replicated cues, damage, collision, input и resource commit сохраняются. Passive proc, boss attack и world-event hook вызывают тот же presentation dispatcher **после настоящего игрового события**. Random proc/AI/таймеры не симулируются ради картинки.

UseDefault вызывает реальное прежнее представление данного события. Disabled гасит только его визуальную часть. Replace/Composition заменяет её выбранным набором NS; не рисовать старый procedural эффект одновременно, если это не явная авторская композиция. Collision, safe-zone protection, урон и причинность события остаются у gameplay owner.

Для существующих procedural cue actors важна точная identity: `AMCPassiveFeedbackCue::Kind=Heal` разделяется несколькими источниками. Для редактирования конкретного перка SourceID нужно передавать из реального proc hook, а не угадывать из цвета/Kind/класса cue. Аналогично shared reaction actors не означают один профиль на все смерти/попадания.

## Native данные и безопасность читаемости

Начать с проверенных typed inputs текущего player и native anchor. Расширение endpoint/beam/mesh data выполняется через event contract/typed payload и per-owner binding; LWC Position не превращается в Vector3. Data Interfaces, mesh sampling и неописанные live inputs должны явно отклоняться до подключения корректного provider.

Для опасных событий native радиус/геометрия safe zone/длительность/endpoint — обязательные данные контракта. Текущий player применяет artist overrides после native inputs; без ограничений художник способен нарисовать радиус, отличный от реальной защиты. Для таких событий объявить обязательные native bindings read-only и оставить отдельно художественные ширину/мягкость/свет/декоративные слои. Проверять совпадение с реальной collision/protection формой в preview, не только красивый эффект.

События распространяются через существующую репликацию механики, после чего каждый клиент воспроизводит локальное представление. Не добавлять параллельный generic multicast, который продублирует уже реплицируемые cue actors. Частицы не становятся gameplay authority.

## Capabilities без ложных готовых строк

Каталог показывает отдельно: source найден; runtime binding работает; полное native scenario preview подключено; saved assignment доступно. Editable-флаг появляется только при настоящем потребителе и writer. Полный preview не подменяется изолированным NS или ракетницей. Неподключённые map/boss источники продолжают быть явно отмечены до их реальной интеграции.

Сохранение использует безопасный asset slug, отдельный от SourceID. Текущая конкатенация SourceID в package path не подходит для существующих metadata IDs вида `EditorOnly.CharacterDeath` с точкой.

## Порядок интеграции

1. После завершённого полного saved-asset export сохранить legacy активки как контроль: profile/default/Disabled/Composition, независимые несовместимые NS, reload, Undo, first-use resources.
2. Ввести generic editor binding и dynamic-event validation; старый DT provider работает без изменения существующих DT assets.
3. Вынести общий runtime dispatcher, сохранив active facade и network transport. Подключить один уже существующий non-perk hook с reflected persisted owner как доказательство отсутствия DT зависимости.
4. Проверить более пяти событий на одном настоящем контексте, все привязки World/Composition/Subject, delayed layers, loop interruption и сохранённую runtime DA после restart.
5. Подключать полный inventory по реальным owner hooks: пассивки, care/clean/death, boss attacks, world events, UI-награды. UMG-награда не становится Niagara только ради общей витрины; её generic presentation owner должен сохранять соответствующий UI канал/anchor.

Это миграция presentation boundary, не переписывание механик или перевод проекта на GAS. Editor UI остаётся один.

## Минимальные проверки

- Legacy compatibility: прежние UMProfile/DT references, five-stage validation, existing wrappers и старые saved assets работают без массового resave/rename.
- Non-perk proof: source с пустым SourceRowID читает реальный профиль из root DA, preview получает override без Definition, Apply меняет только этот root, restart сохраняет назначение.
- Stale safety: same-path external profile/root edits и dirty packages отклоняют Apply; частичный save не меняет runtime binding; Undo/Redo не поглощается generated resources.
- Реальная причинность: эффект вызывается фактическим proc/AI/world event; отсутствие random proc не объявляется багом; дефолтный и custom визуалы не дублируются.
- Lifecycle: delayed start/subject destruction/interrupt/death/reset/loop-stop/tail; длинные world loops не обрываются неявным preview TTL.
- Data: правильные LWC endpoints, authored mesh DIs, material/texture references, несколько разных renderers и несовместимых Niagara lifecycles в одной композиции.
- Resource readiness: reflected root → profile → runtime compositions → NS/render resources достижимы на каждой локации; подготовка до первого gameplay события, не доказательство одним тёплым Editor cache.
- Gameplay readability: VFX safe-zone/опасной области соответствует настоящей geometry/protection; artist overrides не меняют игровые правила.
- Один короткий локальный native/solo checkpoint для каждого интегрированного контекста. Сеть и packaged/cook проверки отдельно по явному разрешению; этим аудитом они не запускались.
