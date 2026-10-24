# Инвентарь, HUD и пользовательский интерфейс

Актуализация: **2026-10-23**, по исходникам рабочего дерева. Native defaults ниже могут быть переопределены сохранёнными DA/DT/Blueprint. Asset index устарел (`build_current=false`); полного свежего экспорта package properties нет. Отдельное ограниченное read-only чтение clean loaded CDO/DA и таблиц Hub описано в [ProjectAudit](ProjectAudit.md); оно не подтверждает остальные package properties или актуальность binaries. В этом аудите код и ассеты не менялись, сборки/PIE/сетевые тесты не запускались. Изменения документации описывают код на диске, включая существующие пользовательские правки.

## Где находится логика

| Область | Источник и ответственность |
|---|---|
| Инвентарь | [MCInventoryComponent](../Source/MessControl/Private/MCInventoryComponent.cpp), [header/profile](../Source/MessControl/Public/MCInventoryComponent.h): выбор слота, tool meshes, spray/treatment, реплицируемое состояние |
| Улучшения | [MCInventoryUpgrades](../Source/MessControl/Private/MCInventoryUpgrades.cpp): MeshaBrush, Chainsaw, Watergun, retained Buffer code |
| Прицел | [MCInventoryAim](../Source/MessControl/Private/MCInventoryAim.cpp): camera ray, muzzle occlusion, сетевые view snapshots |
| Действия персонажа | [MCToothCharacter](../Source/MessControl/Private/MCToothCharacter.cpp): Enhanced Input, primary/brace/throw, server swing/contact, физические реакции |
| Комбо | [MCMeleeCombo](../Source/MessControl/Private/MCMeleeCombo.cpp): три combat-удара и отдельная food-chop поза |
| HUD | [MCGameplayHUD](../Source/MessControl/Private/MCGameplayHUD.cpp): чтение game/player/director state и локальная presentation |
| Индикаторы мира | [MCWorldStatusComponent](../Source/MessControl/Private/MCWorldStatusComponent.cpp), [MCWorldStatusWidget](../Source/MessControl/Private/MCWorldStatusWidget.cpp): HP и spoil/fuse без записи в gameplay state |
| Меню и фокус | [MCPlayerController](../Source/MessControl/Private/MCPlayerController.cpp), [MCMainMenuWidget](../Source/MessControl/Private/MCMainMenuWidget.cpp), [MCGameplayMenuWidget](../Source/MessControl/Private/MCGameplayMenuWidget.cpp) |
| Стиль и копирайт | [MCGameplayUIFactory](../Source/MessControl/Private/MCGameplayUIFactory.cpp), [MCGameplayUIWidget](../Source/MessControl/Private/MCGameplayUIWidget.cpp), [MCUIDataLibrary](../Source/MessControl/Private/MCUIDataLibrary.cpp) |

## Управление

Mapping context и Input Actions создаются программно в `AMCToothCharacter::BuildInput`. При `PawnClientRestart` context добавляется в Enhanced Input subsystem локального игрока; прежние mappings других pawn этого игрока удаляются. Это не набор обязательных IMC/IA ассетов в Content.

| Ввод | Действие |
|---|---|
| WASD / левый stick | Движение; конкретный режим учитывает плавание/лазание/плот |
| Space / gamepad нижняя кнопка | Прыжок и контекстное действие |
| Shift / левый stick click | Sprint |
| ЛКМ / RB / RT | Primary выбранного инструмента |
| F / LT | Удар/применение выбранного инструмента; у щётки физический комедийный удар |
| E / gamepad левая кнопка | Handle/взаимодействие: еда, pickup, спасение и другие контекстные действия |
| ПКМ / LB | Brace: удерживаться за еду, игрока или мировую опору; также контекстная защита/плот |
| Q / gamepad верхняя кнопка | Бросок; удержание поддерживает текущую механику выдачи предметов/стопки |
| C / правый stick click | Режим ухода за собой |
| 1 / D-pad left | Щётка |
| 2 / D-pad up | Кирка |
| 3 / D-pad right | Нож |
| 4 / D-pad down | Спрей; повторный выбор с Watergun меняет режим |
| 5 | Экипировать/убрать удочку |
| Мышь / правый stick, колесо | Orbit camera, zoom |
| F1, F2, F3 | Tuning/settings panel, подключение, developer panel; F3 исключён из Shipping |
| Escape / Start | Игровое pause menu |
| Tab / gamepad Special Left, удержание | Scoreboard |
| T | Emotes |
| G / gamepad правая кнопка | Rogue booster |
| H | Calcium grenade при наличии заряда/разрешении правил |
| Enter | Открытие Hub selector при готовой команде |
| R | Restart текущего забега через серверный путь |

Имена gamepad-кнопок описаны по Unreal keys; подсказки HUD адаптируются через `MCGamepadHints`. Реальное действие зависит от состояния: меню, loading, смерть, интро, reward interaction и удержание предметов могут блокировать инструмент. Отпускание/отмена primary и других held inputs обрабатывается явно. Подробнее: [Gamepad](Gamepad.md), [Grip](Grip.md), [FoodStackHolding](FoodStackHolding.md), [FoodThrow](FoodThrow_20261017.md), [HubFishing](HubFishing.md).

## Инструменты и улучшения

`UMCInventoryComponent` реплицируется; по умолчанию грузит `/Game/Data/DA_Equipment`. При отсутствии профиля создаёт native fallback. Физическую щётку в сценариях, где она требуется, нужно подобрать; доступность проверяется через `HasBrush` и правила текущего режима.

| Слот | Базовая механика | Улучшение |
|---|---|---|
| Brush | Контактная чистка зубов, языка и допустимых целей; self-care | `MeshaBrush`: native ×2 cleaning speed и изменённая contact/radius геометрия с rogue modifiers |
| Pickaxe | Hard food, calculus, лёд, допустимые противники | `Buffer` код/pose/profile сохранены, **но апгрейд отключён** в reward eligibility и `HasToolUpgrade` |
| Knife | Soft food и допустимые противники | `Chainsaw`: включаемое primary движение/контакт, сохраняет running после отпускания, останавливается по collision/state rules |
| Spray | Удерживаемое лечение язвы, тушение огня и ремонт доступного зуба | `Watergun`: care mode и automatic pressure fire; повторный выбор слота 4 переключает режим |

Источник доступных улучшений — perks на PlayerState и team upgrade mask в GameState, а не локальный HUD. Rare/legendary выдача проходит серверные проверки. `UMCPerkComponent::IsDefinitionUsable` и `HasToolUpgrade` явно исключают Buffer; наличие его модели/кода не означает доступность игроку. См. [MCPerkComponent](../Source/MessControl/Private/MCPerkComponent.cpp), [PerkEffects](PerkEffects.md), [Boosters](Boosters.md).

Отдельный `bWaterJetUnlocked` и authority-only `UnlockWaterJet` сохранены как compatibility hook первого слота. Они не равны Watergun четвёртого слота и не доказывают наличие отдельной законченной механики струи или магазина WaterJet.

Native defaults профиля: PickaxeDamage=40, KnifeDamage=25, SprayReach=235 см; WatergunCareReach=1000 см, fire interval=0,1 с, magazine=30, reload=2 с, shot damage=0,5. Фактические значения DA могут отличаться, rogue attack rate меняет частоту/тайминги. Pressure shot ограничен 1400 см и muzzle sweep; это не promise попадания сквозь препятствия.

Watergun стреляет сразу на press edge и продолжает при удержании. Сервер ограниченно догоняет cadence на медленном кадре; после последнего патрона начинает reload. Reload завершается после отпускания или смены слота, удерживаемая primary возобновляет огонь после него. Legacy charged-attack поля/API сохранены для загрузки старых данных, `WaterChargeFraction` возвращает 0.

Смена инструмента проверяет `CanWork`/`CanSwitchTool`, отменяет upgrade use, food collection/комбо/primary/contact/self-care. Выбранный слот, targets лечения/огня, pressure mode, ammo, reload deadlines и view state реплицируются; dedicated server не создаёт визуальные tool/Niagara компоненты.

## Удары и контакт

Базовые knife/pickaxe поддерживают combat-combo из трёх ударов; отдельный food chop не продолжает combat-chain. Native длительности combo: нож 0,58/0,62/0,78 с, кирка 0,60/0,64/0,80 с, с поправкой rogue attack rate. Поэтому прежнее описание одного фиксированного удара ножом 0,70 с или киркой 0,65 с не описывает все нынешние режимы. Подробности: [PlayerMeleeCombo](PlayerMeleeCombo.md), [EnemyWeaponContacts](EnemyWeaponContacts.md), [FoodKnifeHits](FoodKnifeHits.md).

`CanBreak` различает Hard/Soft food; Chainsaw имеет свой допуск. Food resistance хранится в данных, fragments наследуют правила своего происхождения. Текущие сохранённые настройки моркови и прочие DT overrides не подтверждались этим аудитом.

**Попадания по товарищам в текущем коде дают физическое отбрасывание.** В player-ветке `AMCToothCharacter::ResolveSwing` вызывается `ToothPhysics->ApplyHit`, счётчик подтверждённых попаданий и звук; снятия HP в этой ветке нет. Аналогично player contact у Chainsaw и Watergun pressure shot использует физическую реакцию. Старое утверждение этого документа о PvP уроне 25/40/25 HP больше не соответствует этим путям исходников.

Обычный melee player target проверяет базовую дальность 180 см с возможным rogue reach, перепад высоты 120 см, переднюю область, Visibility trace, живое состояние и отсутствие Recovering. Self исключён. После предмета/врага замах заканчивается; knife contact window повторяется только после промаха. Другие системы повреждения описаны в [PlayerDamage](PlayerDamage.md).

## Спрей, прицел и визуальное оснащение

Camera-centre ray определяет точку под прицелом. Для care targets без physical collision дополнительно проверяется видимая область язвы/огня на этом же луче; затем направление сходится от сопла к выбранной точке. Отдельный muzzle trace сохраняет стены, range и первый физический контакт ремонта зуба.

View snapshots передаются периодически (не чаще 0,05 с, unreliable) и надёжно перед primary press/release. Сервер ограничивает допустимые данные, получает направление сопла; локальный и удалённый показ используют общий aim contract. Это реализация сетевого обмена, не результат свежего multiplayer-теста.

Обычный спрей работает от held input без прежнего одноразового cooldown. `SprayReadyAt` и старые profile поля сохранены для совместимости. Authority tick выбирает fire/healing/tooth repair; Watergun care применяет повышенную эффективность. Детальные ulcer treatment rules: [CareAndHandling](CareAndHandling.md), [BacklogUlcerRework](BacklogUlcerRework.md) — последняя содержит планы и не является подтверждением доставки.

Tool meshes/attachment/idle/work/support grips задаёт `UMCEquipmentProfile`. При отсутствии мешей есть native fallback формы; сохранённый `SM_Pick` используется как fallback кирки. Художественные материалы имеют приоритет, neutral upgrade fallback применяется к WorldGridMaterial. Инструмент скрывается при ряде traversal/grip/held/reward состояний; это общий `ShouldPresentTool` для рук и визуала.

Niagara spray/water mist создаётся один раз на персонаже и включается по состоянию. Brush contact foam имеет отдельный retained Niagara component и следует фактическому контакту; details и исторический motion review см. [BrushCleaning](BrushCleaning.md), [VFXReferences](VFXReferences.md). Из наличия компонента нельзя вывести актуальный renderer/rate/material сохранённого Niagara asset.

Все runtime meshes/materials/Niagara/widget brushes должны входить в [ResourceLoading](ResourceLoading.md) manifest через equipment/theme/world references либо явные native roots; dynamic variants создаются до readiness. `LoadSynchronous` в presentation/refresh коде остаётся, поэтому отсутствие позднего hitch надо подтверждать по фактическому первому применению.

## Камера и visual first use

[MCToothCharacter camera defaults](../Source/MessControl/Public/MCToothCharacter.h) и [MCMouthCamera](../Source/MessControl/Private/MCMouthCamera.cpp) разделяют первоначальный обзор и свободный orbit. Native обзор: FollowDistance=900 см, FollowHeight=500 см, FOV=55°, CameraFocusOffset=(80,0,−140), dead zone=(110,80,90), FollowSpeed=7. Orbit ограничивает pitch диапазоном −85…85°, дистанцию 250–1600 см; высота pivot по умолчанию 100 см, wheel step=100 см. User settings отдельно масштабируют mouse/gamepad look и inversion.

В Hub камера начинает свободный orbit; в arena исходный follow использует помеченные `MCCameraBounds` объёмы. Spray/Watergun плавно сдвигает локальную камеру за плечо. Видимый avatar/ракурс остаются локальной presentation; aim передаётся по отдельному inventory protocol.

SpringArm использует Camera channel и сферический probe 24 см. Совместимые wall materials могут открыть круг вокруг focus через `CameraRevealEnabled` и Custom Primitive Data 20–27; native radius=500 см, feather=0,25. Требуется совместимость **всех material slots**: прочие стены сохраняют обычную camera collision. Отключение reveal/cleanup восстанавливает прежний Camera response; pawn/physics collision сохраняется. См. [MCCameraWallReveal](../Source/MessControl/Private/MCCameraWallReveal.cpp). Наличие шейдерных параметров/коллизии в сохранённой оболочке карты в этом аудите не проверялось.

Управление обзором блокируется общим input-mode resolver. Spectator view использует отдельное локальное состояние. Материалы radial reveal, tools и first-use VFX должны оставаться достижимыми из preload roots; обычный успешный frame после прогрева не доказывает отсутствие первого shader/streaming hitch.

## HUD: состояние и раскладка

`UMCGameplayHUD` читает GameState, PlayerState/perks, inventory и event/director state. Обычные данные обновляются примерно 10 раз в секунду; reticle, portraits и переходы могут обновляться каждый кадр. Таймеры берутся из server world time/deadlines. Чистота вычисляется по доступным care surfaces; при оставшейся грязи процент не округляется до 100%.

Тема выбирает Widget Blueprint для роли GameplayHUD; native fallback умеет построить дерево. Native layout сохраняет стабильные имена динамических элементов, чтобы Blueprint Designer мог менять представление. `UpdatePhase1Layout` учитывает ширину, scale, число игроков и измеренную высоту локализованного текста; старое описание исключительно фиксированного ScaleBox 1536×864 больше не универсально.

Основные области:

- слева сверху — здоровье/доступность восьми arena teeth, чистота, event timer и task progress;
- справа — до четырёх player portraits/HP и timeline; значения приходят из актуального режима;
- снизу слева — четыре tools, выбранное имя, pressure ammo/reload, context hint и contact progress;
- снизу по центру — stamina, отдельная swim energy;
- encounter/boss widget, очередь gameplay alerts, booster widget и cataclysm instruction — отдельные составные presentation;
- результаты и debug director monitor показываются по состоянию; legacy XP/progression widgets принудительно collapsed и очищаются.

Tutorial сохраняет инструменты и HUD controls, скрывая обычный day overview; objective подаёт tutorial UI. Lobby скрывает обычный gameplay UI. Spray/Watergun reticle рисуется в geometry HUD своего игрока, скрывается при меню, spectating и недоступном инструменте. В pressure mode кольцо показывает ammo/reload; отдельный food-throw reticle появляется по контексту.

Сохранённые WBP деревья и theme role mappings в этом аудите не экспортировались заново. Для Designer/contracts см. [Phase1GameplayUI](UI/Phase1GameplayUI_20261017.md), [GameplayUIStyleSystem](UI/GameplayUIStyleSystem_20261016.md), [MultibossHUDContract](UI/MultibossHUDContract_20261016.md), [TeethHealthHUDContract](UI/TeethHealthHUDContract_20261016.md). Документы с Plan в названии сохраняют проектные намерения; реализацию следует сверять с source/packages.

## Индикаторы мира и текущие пользовательские изменения

`UMCWorldStatusComponent` — **локальная нереплицируемая presentation**, использующая реплицированные health/deadlines еды/обычных nut enemies. На dedicated server отключается; boss исключён. Использует первого локального PlayerController, screen-space widget component, distance/frustum/Visibility culling. Native MaxDistance=2600 см, PollSeconds=0,12 с; видимый anchor следует физике каждый кадр, скрытый индикатор только периодически проверяет возможность появления.

У еды индикатор скрыт при disposed/dead, carried/equipped/stack-carried, absorbing/swallowing и brush-tool состояниях. Spoil timer показывает **израсходованную долю** срока, spicy — израсходованную долю fuse; deadline читается в server time, без изменения food state. Для физической еды верх anchor уточняется по сохранённым convex collision hulls, без чтения render CPU mesh. У armed kamikaze показывается fuse timer.

Нынешняя незакоммиченная правка добавляет `FMCWorldStatusSnapshot::bShowHealth`:

- fragments сохраняют gameplay health, но целиком скрывают `StatusHealthSize`, включая HP frame/bars/skin/занимаемую ширину;
- fragments Food/Spicy оставляют центрированный timer; fragments foreign objects без timer не создают пустой indicator;
- при возврате к whole-food/enemy preview восстанавливаются исходные Designer visibility/padding;
- добавлены preview states `FragmentFresh` и `FragmentSpoiled`.

Timer и pill health рисуются Slate vector skin с темой, loss trail, сглаженным timer follow и переходом в spoiled pictogram. Они не требуют новых texture/UI material shader variants. Важные имена: `StatusTimer`, `StatusHealthSize`, `StatusHealthFrame`, `StatusHealthLayers`, `StatusHealth`, `StatusLossTrail`, `StatusHealthPresentation`.

Эти изменения подтверждены только source diff; их компиляция, актуальность сохранённого WBP и игровой вид в этом аудите не проверялись. Использование PlayerController 0 также не является доказательством поддержки всех split-screen views.

## Главное меню, настройки и модальные окна

Native main menu имеет страницы Main/Host/Join/Settings/Credits/Exit/Lobby/Leave/VideoConfirm/Pause/Connection. Основное Play направляет через `UMCSteamSessionSubsystem::EnterHub`; offline fallback открывает Hub listen с MCHubGameMode. Tutorial — отдельный travel. Steam room/address join/host имеют существующие guards; embedded day launch допускается только для подключённого дня.

Настройки включают Graphics, Audio, Language, Controls, Voice. Resolution/window mode используют 15-секундное подтверждение с rollback; остальные preferences применяются через `UMCGameUserSettings`. Preview/Designer не запускает travel/session/settings changes.

В существующем пользовательском diff `CollapseRetiredMainMenuCopy` сворачивает retired tagline/navigation copy по binding keys `UI.MainMenu.Tagline`, `UI.MainMenu.NavigationHint` либо стабильным именам unbound template controls. Имена сохранены; явно переназначенный другой localization key не скрывается; vertical slot padding обнуляется. Это описание несобранного рабочего изменения, не результат визуального smoke.

Pause/Connection во время gameplay используют `UMCGameplayMenuWidget`, который контроллер создаёт через роли PauseMenu/ConnectionMenu. `UpdateInputMode` определяет приоритеты: loading → wardrobe/shop → lobby/pause/connection/tutorial → Hub selector → boss intro → scoreboard → reward/emote/dev/tuning → gameplay. Открытие модального UI отменяет удерживаемые действия pawn. Scoreboard release дополнительно отслеживается в tick на случай потери release event/фокуса. Игровое pause menu само по себе не означает остановку multiplayer world; perk choice имеет отдельный team pause protocol.

Сброс presentation при travel и readiness перед ACK описаны в [ResourceLoading](ResourceLoading.md); Hub scene camera/menu — в [HubLobby](HubLobby.md), [HubSystemsCheckpoint](HubSystemsCheckpoint_20261023.md). Startup splash может прикрывать preload, но завершение splash не означает готовность ресурсов.

## Общая тема, локализация и сохранение

`UMCGameplayUIFactory::GetMasterTheme` удерживает `/Game/Gameplay/UI/Theme/DA_GameplayUITheme` сильной ссылкой. `ResolveClass(Role, NativeFallback)` допускает только пригодного потомка native role и иначе возвращает fallback. [Theme](../Source/MessControl/Public/MCGameplayUITheme.h) хранит palette, fonts, soft widget class map, boss portraits и UI tables.

Widget theme берётся из собственного override, родительского UI widget или master. TranslationTable override наследуется отдельно; далее используются theme table и `/Game/Gameplay/UI/Data/DT_UI_Localization`. Виджеты подписываются на смену культуры/таблицы/темы и обновляют составные экраны.

`UMCGameUserSettings` предлагает `ru`, `en`, `de`, `fr`, `es`, `pt-BR`, `zh-Hans`, `zh-Hant`, `ja`, `ko`; наличие кодов не доказывает полноту перевода каждой строки. `MCUIDataLibrary` нормализует culture, использует fallback chain и explicit-culture numeric/plural formatting. PreviewCulture позволяет проверять Designer без смены глобального языка. См. [LocalizationEditing](UI/LocalizationEditing.md), [LocalizationContract](UI/LocalizationContract_20261016.md).

Настройки звука/управления/языка/voice и bag color относятся к local GameUserSettings. Покупки, косметика и pending next-run boosters принадлежат Hub economy/profile. Контроллер восстанавливает профиль на authority, переносит equipped cosmetics на pawn и применяет pending boosters по RunId: повтор того же RunId не выдаёт их снова; при неудачной batch grant покупки остаются. Эти источники не следует заменять сохранением UI полей. См. [MCPlayerHubEconomy](../Source/MessControl/Private/MCPlayerHubEconomy.cpp), [MCHubEconomyComponent](../Source/MessControl/Private/MCHubEconomyComponent.cpp), [HubLobby](HubLobby.md).

## Проверка и исторические свидетельства

Аудит 2026-10-23: graph-first exploration, затем actual source и существующий git diff; coverage проверен для ключевых путей. Индекс отмечает `metadata_changed`, reflection headers частично разобраны. Graph resolver может давать совпадения по общим именам; отсутствие/наличие edge не заменяет source.

Прежний документ фиксировал solo/Editor/network прогоны 2026-10-03 и foam review 2026-10-04. Из этих путей сейчас доступны `Saved/FoamReview/MaterialFinal.log` и `Saved/FoamReview/EditorSoloFinal.log`; `Saved/CarrotKnifeVerification.json` и `Saved/TestReports/WeaponPlayerHits/index.json` по прежним адресам не найдены. Старые pass-claims не подтверждают текущие source changes и особенно изменившийся teammate damage contract.

Существующие helpers/tests остаются средствами проверки, а не отчётом об их запуске: `Tools/TestInventory.ps1`, `Tools/TestCamera.ps1`, `Tools/TestOrbitCamera.ps1`, `Tools/TestBrush.ps1`, `MCInventoryTests`, localization/menu/no-experience HUD tests. Network helpers запускаются только по отдельному запросу пользователя; Windows build/cook/package — отдельная задача.

Текущие пробелы: свежая компиляция user UI changes, saved WBP/schema alignment, solo first-use visuals, fragment timer layout и меню в реальной камере, повторный travel/input restoration, полный cold packaged start и отдельно авторизованный multiplayer. Никакой новый PASS этим документом не заявляется.
