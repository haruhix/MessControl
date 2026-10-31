# Каталог действий VFX: реальные источники и границы preview

31.10.2026. Read-only аудит source/canonical specs для нового wireframe пользователя. Пользователь затем подтвердил многоэтапное действие с несколькими слоями, отдельный audition галереи с явным Apply и создание compound внутри этапа. Эти ответы задают следующий UI; сам аудит его не реализует. **Каталожная запись, наличие NS и полный native-preview — разные состояния.**

## Активки: существующие ID и вызываемые роли

ID взяты из `Tools/Unreal/UI/active_skill_specs.json`; роли сверены по `PlaySkillCue`/`StartLoop`/`StopLoop` в соответствующих action cpp. Общий `UMCPerkVFXProfile` реально поддерживает ровно `Start / Loop / Impact / End / Warning`; не следует показывать каждый слот как существующее событие каждого действия.

| Реальный ID / native action | Реально вызываемые события и смысл | Существующие источники | Готовность контекстного editor-preview |
|---|---|---|---|
| `Consumable_Rocket` / `AMCRocketSkillAction` | Start — дульный выстрел; Loop — полётный след; Impact — фактический контакт/детонация; End — нормальное завершение после Detonated. Warning не вызывается | `NS_Rocket_Start`, `NS_Rocket_Loop`, `NS_Rocket_Impact`; End допускает профиль, но default NS не назначена | **Есть отдельный native adapter:** `FMCPerkPreviewRunner::StartRocket` с Target / NearWall / Timeout, штатным зубиком/AnimInstance/руками. Не обещание каждого клипа/арта |
| `Active_Hook` / `AMCHookSkillAction` | Start — запуск крюка; Loop — трос/полёт; Impact — контакт; End — возврат/завершение | `NS_Hook_Start`, `_Loop`, `_Impact`, `_End` | Реальные metadata/action существуют; отдельного полного adapter в текущем `FMCPerkPreviewRunner` нет |
| `Active_Sponge` / `AMCSpongeSkillAction` | Start — начало поездки; Loop — контактная пена; End — окончание; отдельный ground SoapTrail по движению | `NS_Sponge_Start`, `_Loop`, `_End`, `_Trail` | Metadata/action есть; отдельного полного adapter нет. **SoapTrail не является шестым слотом профиля**: отдельный компонент/lifecycle в `MCSpongeSoapTrail.cpp` |
| `Consumable_Grenade` / `AMCGrenadeSkillAction` | Start — взведение в руке (Cooking), не выдуманное отдельное событие броска; Loop — активный запал; Warning — опасная область; Impact — взрыв | `NS_Grenade_Start`, `_Loop`, `_Warning`, `_Impact` | Metadata/action есть; отдельного полного adapter нет |
| `Consumable_Air` / `AMCAirSkillAction` | Loop — всасывание с Fill; Warning — переполнение; Impact — выдувание; End — разрыв при переполнении. Start cue не вызывается | `NS_Air_Loop`, `_Warning`, `_Impact`, `_Rupture` | Metadata/action есть; отдельного полного adapter нет |
| `Consumable_Driver` / `AMCDriverSkillAction` | Start — начало поездки; Loop — щётки/движение; End — остановка | `NS_Driver_Start`, `_Loop`, `_End` | **Parked:** canonical `Enabled=false`, сохранённое отключение документировано прежним readback. Показывать только как отключённый/архивный пункт, не включать gameplay. Полного editor-adapter нет |

NS в этой таблице — native default ссылки из `/Game/Gameplay/ActiveSkills/VFX/`. Это не новый exhaustive saved-package/Blueprint-override аудит. Профиль способен заменить/отключить default; исходный путь сам по себе не гарантирует художественный статус или совместимость с single-NS builder.

`LoopStart/LoopStop` в журнале — реальные границы одного binding `Loop`, не два дополнительных изменяемых profile slots. `Prepared/Windup/Flying/Detonated/ActionFinished` описывают action/scenario state; наличие записи в логе не создаёт самостоятельную точку назначения NS. В частности, подготовка/хват/recovery остаются native-анимацией, пока у действия нет отдельного VFX cue.

## Чистка, реакции, смерти и пассивки

У этих действий нет подтверждённого perk ID, если он не указан явно. Квалифицированный enum/метод ниже — реальный source hook, а не уже созданный ID новой UI-базы.

| Кандидат каталога | Уже существующий trigger/source | Что можно честно показать сейчас | Чего пока нет в новом tool |
|---|---|---|---|
| Щётка: контактная пена | `UMCBrushContactComponent`: Emit = активный доступный контакт, щётка в работе и IsTouchingSurface; это не само по себе подтверждение изменения mask | `FoamSystem` → `/Game/Gameplay/VFX/NS_BrushFoam`; реальный NS можно audition как ресурс | Полный brush→surface→accepted-cleaning adapter и profile slot этого инструмента |
| Спрей / водяной инструмент | `MCInventoryComponent.cpp`: `TreatmentSprayNiagara`, `WatergunSprayNiagara` | `NS_SprayMist`, `NS_WatergunMist` как готовые ресурсы | Контекстные aim/contact/use/stop adapters; наличие NS не делает их Rocket stages |
| Очистка / починка большого зуба | `UMCToothCareFeedbackComponent::ReportAccepted`, `EMCToothCareFeedbackKind::{Cleaning,Repair}`; вызывается после настоящего изменения mask/HP | Реальные процесс/завершение, точка/нормаль/serial. Представление сейчас procedural mesh/material | Универсальное NS-поле/профиль и native full-preview в Composer. Нельзя объявить procedural реакцию редактируемой NS |
| Обычный орех: смерть | `AMCNutEnemy::Defeat`: при fatal hit → `AMCReactionVFX::Spawn(...FoodBreak...)`, затем уменьшение меша/удаление | Событие существует; `FoodBreakSystem` использует `NS_NutDustImpact`, эффект дополнен процедурным представлением | Отдельный enemy-hit→fatal→death adapter и общий `EnemyDeath` profile slot |
| Босс-орех: смерть | `AMCNutBoss` → `AMCNutCombatEffect` с `EMCNutCombatCue::DeathBurst`, отдельная death-анимация/состояние | Реальная cue-метадата; связанные Niagara/процедурные части. Не сливать со смертью обычного ореха | Полный boss context adapter / назначение через нынешний perk profile |
| Разлом еды / запах порчи / попадание | `EMCReactionEffect::{FoodBreak,SpoiledSmell,CombatHit,PlayerHit,...}` в `AMCReactionVFX` | Существующие triggers/роли; часть формы procedural, FoodBreak имеет NS | Контекстные adapters и отдельные artist-profile bindings |
| Пассивные receipts | `AMCPassiveFeedbackCue`: Heal, Enamel, Shield, Wind, Speed, Vanish, Return, Neighbor, PowerHit, Billiards. Например, `MCResultPassives` вызывает Heal после реально восстановленного HP; `MCDefenseSprayPassives` — Shield/Enamel/Wind | Реальные condition-driven события на зубике; процедурные mesh/material cues | Автоматическая адаптация всех 27 пассивок к пяти active stages, NS-profile и полный preview. Числовой бонус сам по себе не требует постоянного VFX |

## Что можно экспонировать без новой игровой механики

- Реальные активные ID, русское имя, состояния enabled/parked, существующие event roles, ссылки на resources/default profile и исходный hook.
- Для `Consumable_Rocket` — рабочий contextual native-preview; для остальных отдельно помечать **«Ресурс/событие найдено; полный сценарий не подключён»**. Не запускать Rocket под видом выбранной губки/врага.
- Обычный asset audition в правой галерее возможен для существующей NS; это не проигрывание всей механики и не принятие такого назначения.
- Новые context adapters могут позже оборачивать существующий gameplay, без переписывания его механики, но всё равно требуют отдельной реализации/проверки поз, targets, contact, cleanup и readiness. Их наличие не выводится из metadata.

Предлагаемые каталожные поля: `SourceID/SourceKind`, понятное имя, реальные `EventRoles`, resource/default binding, `PreviewCapability` (`NativeActionReady / ResourceOnly / NeedsContextAdapter / Parked`). Текущий разрабатываемый editor-session уже объявляет `FMCVFXActionSession::GetCatalogue()` и `FMCVFXActionCatalogueEntry`; единый каталог следует расширять там, не создавать вторую независимую таблицу. Наличие декларации не подтверждает сборку или runtime следующего UI.

### Разрешение реальных default bindings

Не нужно дублировать пути шести активок в UI. Загрузить `DT_Perks` (`/Game/Gameplay/Roguelike/DT_Perks.DT_Perks`), проверить `FMCPerkDefinition::StaticStruct()` и найти строку по ID. `row.EffectClass` → CDO `UMCActiveSkillPerkEffect` → его публичный `ActionClass` → CDO `AMCActiveSkillAction`. Публичные soft-поля `StartVFX / LoopVFX / ImpactVFX / EndVFX / WarningVFX` дают реальные defaults, включая Blueprint CDO. Профиль `row.VFXProfile.Slots` применяет поверх них `UseDefault / Replace / Disabled`.

Список **вызываемых** ролей по-прежнему требует проверенного action contract из таблицы выше: непустое поле CDO или поддержка роли в `UMCPerkVFXProfile::GetSupportedStages()` не доказывают, что действие отправляет эту cue. Каталог не должен спавнить action или вызывать `PrepareVFXProfile` на CDO ради чтения metadata.

## Основания и свежесть

Проверены текущие `MCPerkTypes.h:64` (`VFXProfile`), `MCActiveSkillPerkEffect.cpp:25–28` (подготовка профиля до commit/fallback), `MCPerkVFXProfile.cpp:18–22` (пять допустимых ролей), шесть action cpp, `MCSpongeSoapTrail.cpp`, `MCBrushContactComponent.cpp`, `MCInventoryComponent.cpp`, `MCToothCareFeedbackComponent.h`, `MCNutEnemy.cpp:192–199`, `MCNutBoss.cpp:1783–1792`, `MCReactionVFX.h/.cpp`, `MCPassiveFeedbackCue.h` и `MCResultPassives.cpp`. Единственный complete editor adapter подтверждён декларацией `MCPerkPreviewRunner.h` и реализацией `StartRocket`/hardcoded RocketID; повторные runtime/network проверки этим аудитом не выполнялись.

`codebase_memory.list_projects` разрешил `E-DEVGAME-MessControl`; graph PlaySkillCue/MCReactionVFX сверен с source. Coverage у трёх ключевых путей — `metadata_changed`, поэтому выводы основаны на прочитанном source. Старый `Docs/PerkVFXDataWorkflow.md` описывает предыдущее предложение до реализации профиля; его прежнее «не реализовано» не использовано как текущее отсутствие API. Broad index refresh, build, Editor, source/package edits не запускались.
