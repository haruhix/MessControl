# VFX Composer: единый бой «Орех-рыцарь → Орех-маг»

Состояние на 31.10.2026: **аудит и поэтапная интеграция**. `Boss.NutKnight.Shield` уже подключён к настоящим Hit/Break и канонической библиотеке; шесть проверок Default/Disabled/двух слоёв прошли на реальном разрешении урона. Визуальное превью ещё требует исправления взаимного положения моделей. `Boss.NutMage.Fireball` реализуется следующим. Остальные строки ниже остаются планом **Pending**. В таблицах 20 контекстов, а не 20 готовых эффектов или 20 независимых атак.

Обе фазы реализует `AMCNutBoss` с `BossRole=Tank/Mage`; отдельных классов MCNutKnight/MCNutMage в проверенном source нет. Настройки, модели и анимации берутся из `DA_NutRain.Settings.Boss`. Источник истины — текущие native callsites и сохранённые настройки; code graph использован для поиска и проверен чтением source. Его coverage помечен `metadata_changed` и не доказывает полноту Blueprint/level overrides.

Обычная последовательность encounter: вход рыцаря → бой/разрушение щита → смертельный урон рыцарю → **KnightHeadSplit и превращение того же boss actor** → раскрытие мага → бой с магом → финальный Defeat. `TankDeath` без второй фазы и отдельное падение `MageEntrance` существуют для standalone/lab, но не заменяют этот переход.

## Контракт представления

- Предложенные SourceID ниже относятся к общей библиотеке представления и общему каталогу Composer; они **не требуют строки DT_Perks**. За исключением уже подключённого щита, это ещё не зарегистрированные предложения. Названия events обозначают реальные границы соответствующей механики, а не универсальные стадии ракетницы.
- Только внешний вид редактируется. Источником позиции/нормали, damage/safe footprint, длительности windup, фактического контакта, seed и расписания остаётся native boss/event. Частицы не наносят урон и не определяют область атаки.
- `AMCNutCombatEffect` содержит **20 значений enum**, но enum не равен списку живых dispatches. Обычный Fireball испускает `CastCharge → CastRelease → projectile → FireImpact`, не `FireCast`: generic spawn исключает Fireball. При настоящем `EncounterOwner` общий косметический `NutRain` подавлен; предупреждения и контакты принадлежат реальным падающим продуктам.
- Текущий визуал смешанный: Niagara, procedural mesh, instanced debris, материалы/MID, анимация и состояние модели. Миграция отдельного NS **не означает миграцию всей атаки или procedural artwork**.

Строки следующих таблиц: **Pending — proposed binding / migration preview**, кроме описанного выше щита. Столбец lab показывает существующую native инфраструктуру, а не завершённый Composer adapter. Существующее стабильное имя щита — `Boss.NutKnight.Shield`; его не переименовываем под предложенный ниже префикс.

## Рыцарь и переход во вторую фазу

Префикс SourceID: `Boss.Nut.Knight.`. Пути callsite относятся к `Source/MessControl/Private/`.

| Контекст / предложенные events | Реальный trigger / callsite | Существующий визуал | Неизменяемый native контекст | Существующий lab |
|---|---|---|---|---|
| `Entrance`: Telegraph, Landing | `MCNutBoss.cpp:134,1636` | JumpTell → EntranceImpact; floor warning, impact ring/обломки, `NS_NutDustImpact` | EntranceLanding; radius BodyRadius+35; EntranceSeconds; фактическая траектория падения | TankEntrance |
| `Melee`: Telegraph, Strike, ComboStrike2, ComboGroundSlam | `MCNutBoss.cpp:1136,1216,1220,1572–1586` | MeleeTell; специализированный Tank MeleeSlash/wood debris/dust; третий контакт combo — JumpTell/SlamImpact | Реальный TankMeleeVariant 0/1/2 и TankMeleeContactOffset; обычный MeleeRange; ground center = LockedStart+Forward×range×.55, radius×.8; facing gate .35 | TankMelee |
| `Counter`: Telegraph, Impact | `MCNutBoss.cpp:792–820,1517–1524` | JumpTell → SlamImpact, floor cue/ring/debris/dust | Настоящий накопленный pressure threshold/HitGap; TankCounterRadius/Windup; DamageArea до cosmetic cue | — |
| `Charge`: Telegraph, Travel, End | `MCNutBoss.cpp:1142–1149,1216,1542–1549` | ChargeTell corridor; procedural detail; `NS_NutMotionDust` после windup, следующий за боссом | Достижимый LockedStart→LockedTarget, BodyRadius+35, ChargeSpeed; реальные sweep/контакт/остановка. Не добавлять выдуманный destination impact | TankCharge |
| `Roll`: Transform, Telegraph, Travel, End | `MCNutBoss.cpp:1150–1159,1215–1216,1332–1387` | Transform/ShieldHit accent; RollTell footprint/стрелка; один устойчивый `NS_NutMotionDust`; настоящая модель-шар | Locked initial corridor до steering; RollDamageRadius; PlannedRollSeconds; реальные повороты, отражения и per-target hit gap | TankRoll |
| `Jump`: Telegraph, Landing | `MCNutBoss.cpp:1160–1164,1216,1552–1561` | JumpTell → SlamImpact, ring/shell debris/`NS_NutDustImpact`; authored jump | Locked reachable target; JumpRadius/JumpWindup/JumpFlightSeconds; native arc 350 cm; реальный landing damage/contact | TankJump |
| `Shield`: Hit, Break | `MCNutBoss.cpp:822–889,891–930`; `MCNutCombatEffect.cpp:438–574` | Специализированные contact star/slashes/shell fragments; ground `NS_NutDustImpact` и contact `NS_NutShieldHit`; fracture на break | Только принятый shield damage; реальный shield contact frame; radius BodyRadius или×1.4; .32/1.2 s; .15 s coalescing; overflow не бьёт HP, break сохраняет native stun | TankShieldHit / TankShieldBreakOverflow |
| `ShieldDamage`: DamageState | `MCNutBoss.cpp:871,922,1616`; `MCNutCombatEffect.cpp:UpdateShieldDamageVisual` | Постоянные peer-local cracks/fracture на поверхности щита; это не отдельный Niagara burst | Реальные ShieldHealth/max и shield bone/contact frame; не заменять базовый материал художника | TankShieldHit |
| `Hurt`: AcceptedHit | `MCNutBoss.cpp:837–844,879–880` | Authored/procedural recoil и общий accepted-hit feedback; отдельного NutCombat hurt NS нет | Принятые health/shield deltas, направление, VisualHitAt; не прерывать атаки косметикой | — |
| `Death`: Death | `MCNutBoss.cpp:1781–1792`; `MCNutCombatEffect.cpp:390–394` | DeathBurst: dust + `NS_NutShieldHit`, native swirls/ring/debris и death animation | Только настоящий финальный/standalone Tank Defeat; BodyRadius, cue 1.1 s и native DeathSeconds | TankDeath |
| `ToMage`: HeadSplit, MageReveal | `MCNutBoss.cpp:1784–1786,1795–1825`; `MCNutCombatEffect.cpp:576–621` | KnightHeadSplit: две half-shell mesh, procedural cracks, `NS_NutArcaneBurst` + `NS_NutDustImpact`; раскрытие модели мага | bHasMagePhase + живые Tongue/EncounterOwner/Enemies; та же сущность; MagePhase 2.4 s, HeadSplitAt .42, MageRevealAt .9, native head frame | — |

## Маг и его реальные дочерние атаки

Префикс SourceID: `Boss.Nut.Mage.`. Ладонь и авторитетный release origin различаются намеренно: charge использует `GetSpellFocusOrigin`/authored hand pose; полёт начинается из `GetCastOrigin` с baked MageCastOffset, не требует вычисления cosmetic bones на сервере (`MCNutBoss.cpp:349–376`).

| Контекст / предложенные events | Реальный trigger / callsite | Существующий визуал | Неизменяемый native контекст | Существующий lab |
|---|---|---|---|---|
| `Entrance`: Telegraph, Landing | `MCNutBoss.cpp:134,1636` | JumpTell → EntranceImpact с настоящей моделью/ролью мага | Standalone/lab entrance; обычная вторая фаза использует MageReveal, не новое падение | MageEntrance |
| `Melee`: Telegraph, Impact | `MCNutBoss.cpp:1139–1141,1216,1596–1606`; `MCNutCombatEffect.cpp:637–720` | Procedural arc/slash с `M_NutArcane`, authored shove/cast fallback; отдельного NS в ветке нет | Windup, locked range/direction, rear-facing exclusion .35 и настоящий DamageArea; это не Tank shell-hit branch | MageMelee |
| `Fireball`: Charge, Release, Flight, Impact | `MCNutBoss.cpp:1212–1213,1499–1508,1590–1594`; `MCNutSpellProjectile.cpp:136–267` | `NS_NutCastCharge` + focus/flame; release `NS_NutArcaneBurst`; flying nut/core/flame/wake + `NS_NutEmberTrail`; `NS_NutFireImpact`/kernel debris | Реальная ладонь/focus и baked release frame; aim отслеживается до выпуска; swept flight start/target/speed/radius; impact point/normal/radius×2.6; damage/ulcer остаются native | MageFireball |
| `Summon`: Ritual, MarkNut, Release | `MCNutBoss.cpp:1168–1175,1212–1213,1389–1417,1595` | Caster ritual/`NS_NutCastCharge`; SummonTell над настоящими орехами; procedural spirals/`NS_NutArcaneBurst`; реальная конверсия в kamikaze | Список поддерживаемых ground nuts и их actual positions; windup из animation×release fraction; повторная проверка на release — разбитый орех не призывается | MageSummon |
| `NutRain`: Ritual, DropWarning, Falling, Landing | `MCNutBoss.cpp:1176–1188,1212–1216,1562–1570`; `MCNutRainEvent.cpp:299–317`; `MCNutLandingShadow.cpp:35–60,106–137` | Ritual charge; настоящие food mesh/landing shadow; реальный EntranceImpact/`NS_NutDustImpact`. Общий cosmetic storm не дублировать | Реальные pattern A+B/reachable footprints, flight/landing и per-food damage radius; cadence по фактическому числу drops, native catch-up budget | MageNutRain |
| `Teleport`: Telegraph, Departure, Arrival | `MCNutBoss.cpp:1075–1089,1527–1540`; `MCNutCombatEffect.cpp:637–720` | Парные procedural silhouettes/ribbons; `NS_NutArcaneBurst` в обоих endpoints при release | Bounded native поиск; объявленные locked source/target; MageTeleportWindup; повторная occupancy-проверка, без тихой подмены занятого destination | MageTeleport |
| `Hurt`: AcceptedHit | `MCNutBoss.cpp:837–844,879–886` | Authored mage hurt/recoil и общий accepted-hit feedback; отдельного native NutCombat hurt NS нет | Настоящий урон/contact; queued hit при cast; focus-hit счётчики могут запустить настоящий teleport | MageHurt |
| `Death`: Death | `MCNutBoss.cpp:1781–1792`; `MCNutCombatEffect.cpp:390–394` | DeathBurst: `NS_NutDustImpact` + `NS_NutArcaneBurst`, native swirls/ring/debris и death pose | Финальный Mage Defeat, CancelAttacks, BodyRadius и native lifespan; не обычный nut death | MageDeath |
| `Minion`: Fuse, Detonate | `MCNutBoss.cpp:1389–1417`; `MCNutKamikaze.cpp:Detonate,285` | Native fuse flame/telegraph; FireImpact composite/`NS_NutFireImpact` | Настоящий summoned creep, fuse/center/blast/damage; отдельный source owner от Fireball, несмотря на общий cue enum | — |

## Следующий вертикальный срез

**Минимальная настоящая boss-реакция: `Boss.Nut.Knight.Shield` — Hit и Break.** Есть публичный native `ReceiveToolDamage` и готовые реальные fixtures `TankShieldHit`/`TankShieldBreakOverflow` в `AMCVFXLabBossStation`; не требуется заменять босса обычным орехом или прямо проигрывать cue. Для первого полноценного spell/attack-среза далее — **`Boss.Nut.Mage.Fireball`** со всеми четырьмя контекстами.

Критерии первого среза (все Pending):

1. Настоящий `AMCNutBoss` роли Tank, сохранённые настройки/модель/анимации из DA_NutRain, реальная Tongue surface и живой зубик. Сценарий находится в общем Composer.
2. Принятый удар уменьшает ShieldHealth, HP остаётся прежним; breaking hit отбрасывает overflow и запускает native stun. Native receipt берётся после этого решения, не из постановочного таймера.
3. Контакт щита и floor dust используют свои реальные frames. Radii/windup/contact cadence остаются read-only gameplay inputs.
4. `UseDefault` сохраняет **весь существующий** specialized Tank presentation: procedural contact shape/shell fragments, floor dust/contact sparks, fracture state. Generic ring не добавляется поверх.
5. Если proof сначала подменяет лишь именованную Niagara-часть, результат маркируется частичной миграцией. Отдельно должны быть доступны исходные procedural/material элементы, прежде чем объявлять всё Shield artwork редактируемым.
6. Проверены сохранение/повторное открытие/Apply/Undo, отсутствие перекрёстных изменений чужих атак, interruption/reset, encounter teardown и per-location prewarm. Короткий реальный runtime clip оценивается отдельно от функционального результата.

## Что не засчитывается как миграция боссов

`EditorOnly.NutEnemyDeath` — отдельное техническое доказательство общего non-perk binding: обычный `AMCNutEnemy`, не рыцарь и не маг. Его успешные tests, default/Disabled/independent-layer сценарии и roundtrip **не закрывают ни одну строку этого документа**.

Демонстрация `AMCNutCombatEffect::PreviewAtAge` подтверждает только cosmetic cue. Она не заменяет реальный boss attack, animation/contact flow или полный бой с переходом фазы. Существующий boss lab — инфраструктура для общих preview providers, а не новый конкурирующий публичный VFX-инструмент.

Полная миграция procedural artwork, репликация новых bindings, покрытие сохранённых map/BP instances и художественная приёмка — самостоятельные пункты Pending. Source-маршруты выше не являются утверждением, что они уже выполнены.

Подробный read-only отчёт: `E:/CodexWork/MessControl/Validation/VFXComposerMigrationAudit_20261031/NutBossEventMatrix_20261031.json`. Текущие shared cue ресурсы: семь NS-полей и шесть material-полей в `MCNutCombatEffect.h:83–95`; projectile дополнительно держит `NS_NutEmberTrail` и core/wake/flame материалы. Общие donor assets не следует менять глобально ради одной атаки.
