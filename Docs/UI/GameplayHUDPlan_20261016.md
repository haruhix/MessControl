# План чистового игрового интерфейса MessControl

Обновлено 17 октября 2026 года. Пользователь утвердил список из 15 задач и затем направление [FriendlyUISuite v10](E:/DEVGAME/MessControl/ArtSource/UI/FriendlyUISuite_20261017_v10/README.md) с последними правками карточек, восьми резервных зубов, общей темы и трёх DT. Реализация в исходном коде подготовлена, интеграция в Unreal продолжается. Успешная компиляция, создание/сохранение пакетов, Designer Preview и gameplay-проверка ещё не являются подтверждённым результатом.

Цель — получить редактируемые UMG/WBP вместо визуала функционального прототипа. TAB входит в работу. Preview / Play / Stop / Reset находятся в Details самого открытого игрового WBP; отдельная карта и PIE это требование не заменяют. Инструкция по Theme, DT и Preview — [GameplayUIStyleSystem](E:/DEVGAME/MessControl/Docs/UI/GameplayUIStyleSystem_20261016.md).

## Границы работ

- Текущий эталон: I01 `_smooth_v4`, I03 `_smooth_v3`, остальные I02/I04–I08 `_smooth_v2` набора v10. Морской синий, светлая мята и крем, плавные рисованные иконки; LCD/LED применяются к дисплеям. F03/v8 и прежние пиксельные/белые варианты остаются историей поиска.
- F1/F3, их команды, панели, служебные элементы и поведение не переоформляем. Встроенные debug-фрагменты директора сохраняются; его игровая инструкция относится к HUD.
- Главное меню, embedded front end и lobby ведутся отдельно. Gameplay ESC/настройки/титры/подтверждения и F2 ConnectionPanel входят в этот scope; их отдельный `UMCGameplayMenuWidget` не заменяет общий `MCMainMenuWidget`.
- Номер дня исключён. Восемь постоянных резервных слотов показывают состояние отдельных зубов арены; личные HP до четырёх игроков отображаются отдельно. Запас для возрождения и необратимая потеря зуба не изображаются обычным лечением.
- Сохраняем игровые ID, эффекты, серверные вызовы, условия выбора/переброса/доступности, текущие input и очередь алармов. Новый приоритет предупреждений или новая игровая функция требуют отдельного согласования.
- Несохранённые editor-изменения и чужие dirty-правки сохраняем. Новые project artifacts/temp/cache размещаем на E:. Сетевые проверки и Windows build/cook/package — отдельные запросы; итоговая локальная проверка — короткая solo PIE.

## Утверждённые задачи и статус

Статус «код подготовлен» означает наличие реализации в рабочем source, а не пройденную приёмку Unreal.

| № | Задача | Текущий результат и оставшаяся проверка |
|---|---|---|
| 1 | Состав и состояния | Инвентарь и матрица ниже; при интеграции сверить реальные сочетания и сохранённые деревья |
| 2 | Визуальная система | Утверждён v10; подготовлены общая Theme/Palette, шрифты, семантические цвета, панели/кнопки/фокус |
| 3 | Первый чистовой референс | I01 smooth v4 утверждён как эталон; восемь резервных слотов, без дня |
| 4 | Остальные референсы | Набор I02–I08 утверждён вместе с последними правками; это не подтверждение каждой runtime-комбинации |
| 5 | Превью в Details WBP | Подготовлен editor extender с Preview/Play/Stop/Reset и Theme panel; проверить в открытом WBP и после повторного открытия |
| 6 | Общие компоненты | Подготовлены базовый UMCGameplayUIWidget, Theme, factory, карточка, зуб, ресурс и display-компоненты; authored деревья и overrides интегрируются |
| 7 | Основная композиция | Код HUD: резерв8, LCD таймер/задача, чистота, XP/команда, события, ромб инструментов, ресурс, итоги «Забег завершён»; без дня |
| 8 | Алармы | UMG-представление, fixture и отступ под фактическое число boss rows; существующая очередь сохраняется; визуальная/runtime-проверка впереди |
| 9 | HP боссов и целей | Подготовлен value snapshot нескольких боссов с отдельным щитом; проверить 0–3 цели, обновления, потерю/смерть и вступление. Надписи прочих world targets — отдельный пункт матрицы |
| 10 | Состояния игрока | Ресурс, плавание и бустерный snapshot подключаются к HUD; смерть/наблюдение и оставшиеся native-индикаторы проверить в игре |
| 11 | Взаимодействия и мир | Подготовлены адаптации имён/сигналов, лечения, доставки и холода; остальные actor/native-подписи и feedback остаются в охвате, перенос всего в WBP не заявлен |
| 12 | Игровые окна | Код карточек, TAB, обучения, эмоций и отдельного ESC/F2 готов к интеграции; сохранить фактические действия и управление |
| 13 | Совместное отображение | Общая область 1920×1080 и отступы boss/alert заданы в source; тяжёлые сочетания, слои и видимость ещё проверить |
| 14 | Итоговая проверка | Не завершена: компиляция/authoring, Designer, ввод, длинные тексты, разрешения, короткая solo PIE и пользовательская приёмка |
| 15 | Три DT и локализация | RowStruct/lookup/валидация подготовлены для DT_UI_Localization, DT_UI_Boosters, DT_UI_Cards; пакеты, переводы и runtime-смена языка требуют проверки |

## Текущая архитектура и подключение

[UMCGameplayUIWidget](E:/DEVGAME/MessControl/Source/MessControl/Public/MCGameplayUIWidget.h) предоставляет `BuildPresentationTree`, `ApplyTheme`, `ApplyEditorPreview`, `ResetEditorPreview`, `PreviewScenario / PreviewState / PreviewCulture`. [UMCGameplayUITheme](E:/DEVGAME/MessControl/Source/MessControl/Public/MCGameplayUITheme.h) хранит master Palette, fonts, ссылки на DT и `WidgetClasses`. Пустой Theme дочернего компонента наследует тему родителя, затем master `/Game/Gameplay/UI/Theme/DA_GameplayUITheme`.

[Factory](E:/DEVGAME/MessControl/Source/MessControl/Private/MCGameplayUIFactory.cpp) проверяет совместимость authored класса с native-ролью и использует native fallback при отсутствии/ошибке. Выбор классов подключён в [MCPrototypeWidget](E:/DEVGAME/MessControl/Source/MessControl/Private/MCPrototypeWidget.cpp), [MCPlayerController](E:/DEVGAME/MessControl/Source/MessControl/Private/MCPlayerController.cpp) и дочерних компонентах HUD. Целевые ассеты — новые `/Game/Gameplay/UI/WBP_*_v10`; authoring-код находится в [author_gameplay_ui.py](E:/DEVGAME/MessControl/Tools/Unreal/UI/author_gameplay_ui.py). Наличие этого кода не подтверждает успешный запуск и сохранение пакетов.

Native `BuildPresentationTree` создаёт дерево только при отсутствии authored root. При последующей работе сохраняются имена элементов, необходимые `BindPresentationTree/RefreshState`; смена WidgetClasses не заменяет уже существующий child в authored дереве автоматически. Карточки/боссы/бустеры получают готовые presentation-значения. Designer fixture не ищет pawn/акторов и не вызывает gameplay/RPC. Прицелы еды/спрея/навигации остаются native; чужие исправления этих участков сохраняются.

## Инвентарь и обязательные состояния

Матрица — перечень для проверки. Наличие состояния в строке не означает, что оно уже доступно отдельной кнопкой Preview или проверено в runtime.

| Блок | Источник/компонент | Состояния для приёмки |
|---|---|---|
| Корень и цели | [MCGameplayHUD.cpp](E:/DEVGAME/MessControl/Source/MessControl/Private/MCGameplayHUD.cpp), ObjectivesPanel | Бой, передышка, обучение, обычная задача/катаклизм, 0/часть/всё; минимальный и полный HUD |
| Резерв8 | UMCReserveToothWidget; GameState.ArenaTeeth → ArenaTooth.State/Settings | Полный, повреждённый, пустой, bLost/bConsumed; 8/часть/1/0 доступных; лечение не возвращает утраченный зуб |
| Личные HP и команда | MCGameplayHUD, PlayersPanel | 1–4 игрока, цвета/хост, полное/низкое HP, смерть и наблюдение; длинные имена |
| Таймер и чистота | MCGameplayHUD, TimerPanel/CleanlinessProgress | Контекстный отсчёт, последние секунды, без лимита, 0/50/100% чистоты, итоги; без дня |
| Командный XP | MCGameplayHUD, ProgressionPanel | Частичный XP, граница уровня, ожидающий выбор, скрытие в обучении |
| События и инструкция | MCGameplayHUD, EventsPanel/CataclysmPanel | Сейчас/далее, завершено/частичный провал, нет следующего события, длинная инструкция, плоты/желе/уборка; debug-директор без нового скина |
| Инструменты | MCGameplayHUD, InventoryPanel | Четыре слота, выбор, апгрейд, недоступный инструмент, длинное название |
| Прицел и магазин | MCGameplayHUD::NativePaint | Спрей/напор, полный/пустой магазин/перезарядка, food/navigation; штатное скрытие при меню/смерти/плавании/наблюдении |
| Бустер | [MCBoosterWidget](E:/DEVGAME/MessControl/Source/MessControl/Public/MCBoosterWidget.h) | Очередь, доступный/blocked, BrushFury active, Grenade armed, отсутствие; count всей очереди, next ID с пропуском взведённой гранаты |
| Аларм | [MCGameplayAlertWidget](E:/DEVGAME/MessControl/Source/MessControl/Private/MCGameplayAlertWidget.cpp) | Один текущий аларм с владельцем, короткий/длинный текст, вход/удержание/выход, очередь/дубликат, отступ под 0–3 boss rows |
| Boss HP и вступление | [MCBossHealthWidget](E:/DEVGAME/MessControl/Source/MessControl/Public/MCBossHealthWidget.h), контроллер | 0/1/2/3 босса, полное/низкое/0 HP, shield full/broken/absent, длинное имя, смерть средней цели, новая цель; letterbox и возврат HUD |
| Выносливость/плавание | [MCStaminaWidget](E:/DEVGAME/MessControl/Source/MessControl/Public/MCStaminaWidget.h) | Полная/низкая/0, вода, усталость, утопление и отсчёт, восстановление, смерть |
| Холод | [MCFreezeBarWidget](E:/DEVGAME/MessControl/Source/MessControl/Private/MCFreezeBarWidget.cpp) | 0/часть/максимум, safe/danger, несколько игроков |
| Контекст/смерть/наблюдение | MCGameplayHUD, HintPanel/ContactProgress | Перенос/стопка/бросок, еда/глотка, лёд/цепь/плот/спасение; respawn countdown/нет резерва/смена цели |
| Имена/сигналы | [MCPlayerNameComponent](E:/DEVGAME/MessControl/Source/MessControl/Private/MCPlayerNameComponent.cpp) и MCPlayerNameWidget | Длинный ник, цвет/хост, внимание/помощь/нет сигнала |
| Лечение/зубочистки | [MCUlcerProgressWidget](E:/DEVGAME/MessControl/Source/MessControl/Private/MCUlcerProgressWidget.cpp), MCMouthSurface/MCToothpick | Фактическая клавиша, 0/часть/100%, повреждение/блок/исчезновение цели |
| Доставка и еда | [MCDeliveryZoneCaptionWidget](E:/DEVGAME/MessControl/Source/MessControl/Private/MCDeliveryZoneCaptionWidget.cpp), MCFoodActor | Вход/выход/ошибка, близко/далеко; застряла/тянуть, HP/масса/мусор/перец/запал/пауза |
| Прочие HP/урон | MCIceEventVFX, MCNutEnemy, MCReactionVFX | Ледяное ядро/кристаллы/орех, несколько целей/уничтожение; число/блок/затухание. Сохранить отдельные VFX попаданий |
| Награды/уровень/риск | [MCPerkChoiceWidget](E:/DEVGAME/MessControl/Source/MessControl/Public/MCPerkChoiceWidget.h) и [MCPerkCardWidget](E:/DEVGAME/MessControl/Source/MessControl/Public/MCPerkCardWidget.h) | Три карты, уровень, reroll available/spent, риск и цена, pending; вся карта clickable, без полос «Выбрать» |
| Договор | MCPerkChoiceWidget, ContractConfirm | Две read-only карты без полос «Выбрано», общие Accept/Decline снизу, фактические условия и отмена |
| Обучение | [MCTutorialWidget](E:/DEVGAME/MessControl/Source/MessControl/Private/MCTutorialWidget.cpp) | Портрет/fallback, длинная реплика, действие, личный/командный прогресс, готов/не готов, завершение |
| Эмоции | [MCEmoteWidget](E:/DEVGAME/MessControl/Source/MessControl/Private/MCEmoteWidget.cpp) | Категории, выбор, disabled, цвет игрока, открытие/закрытие |
| TAB | [MCScoreboardWidget](E:/DEVGAME/MessControl/Source/MessControl/Private/MCScoreboardWidget.cpp) | 1–4 игрока, длинные ники, очки/равенство, ping, host/spectator; hold/release TAB; без выдуманных MVP/kills |
| ESC/F2 | [MCGameplayMenuWidget](E:/DEVGAME/MessControl/Source/MessControl/Public/MCGameplayMenuWidget.h) | Пауза, экранные настройки/титры/выход, Keep/Revert/15с; Steam/IP, адрес/status/busy/недоступность и закрытие; main/embedded/lobby не меняются |
| Итоги | MCGameplayHUD, ResultsPanel | «Забег завершён», победа/поражение, незавершённые события, host-only новый забег по существующему правилу; без новых наград/автотаймера |

## Совместное отображение и ввод

- HUD и пассивные полосы не забирают игровой ввод. Boss list и alert используют одну design-область; позиция аларма зависит от числа отображаемых boss rows. Вложенный ZOrder и viewport ZOrder разных экранов проверяются раздельно.
- TAB сохраняет hold/release, закрытие при награде/вступлении и отсутствие застревания после смены input mode. Карточки поддерживают мышь, 1–3 и gamepad; выбор/focus/disabled/pending видны в неподвижном кадре.
- Награда, обучение, ESC/F2 и круг возвращают управление по существующим правилам. Boss intro скрывает обычный HUD/TAB/круг и восстанавливает видимость после выхода. Главное меню/lobby — только совместимость стыка.
- Контекстная строка и FIFO алармов сохраняют текущие правила. Дизайн не является разрешением менять порядок подсказок, подавлять предупреждения или придумывать отсчёт ожидания.

Обязательная тяжёлая композиция: четыре игрока + три босса/щит + один аларм с владельцем + повреждённый/потерянный резерв и одна доступная попытка + ресурс + длинная инструкция. Отдельно проверить обучение с инструментами, TAB над боем, награду, плавание/утопление, смерть/наблюдение, катаклизм и итоги. F1/F3 — только проверка сохранения прежнего поведения.

## Preview и общая тема в Details WBP

В Details игрового WBP предусмотрены категория **Gameplay UI Preview**, кнопки **Preview / Play / Stop / Reset** и **Gameplay UI Theme…**. Theme panel открывает master DA, RGBA Palette и образец. Общие правки сохраняются через **Save shared theme**; дочерние Theme overrides сбрасываются в None для наследования. `WidgetClasses` в DA выбирает WBP по роли, а локальные class overrides остаются в Defaults соответствующего компонента.

Наследуемые поля: `PreviewScenario`, `PreviewState`, `PreviewCulture` (например ru/en), `PreviewDuration`, текущее `PreviewTime`. У Booster есть `PreviewBoosterRow` (таблица + строка) и PreviewQueueCount; у standalone PerkCard сейчас SampleCard/bPreviewReadOnly, а не универсальный Row picker. PerkChoice поддерживает Reward/Level/Risky/ContractConfirm/Pending/RerollSpent. Применяются только поддержанные компонентом состояния, без вымышленных эффектов.

Editor-код продвигает время fixture через ticker и `ApplyEditorPreview(Time)`: Preview применяет кадр, Play циклически обновляет время, Stop останавливает, Reset вызывает сброс компонента. Сценарии не меняют глобальную culture и не обращаются к игровым акторам/RPC. Текущий механизм проигрывает native presentation; управление authored UMG animations через editor Sequencer ещё не подключено. Нельзя считать runtime NativeTick/PlayAnimation работающим механизмом Designer. Работа controls, повторное открытие/recompile и отсутствие сохранения fixture как runtime-состояния входят в предстоящую приёмку.

## Три DT и перевод

В `/Game/Gameplay/UI/Data/` предусмотрены:

- **DT_UI_Localization**: Row Name — `UI.*` ключ, SourceText, Context, Localizations[culture]. Общие имена действий, кнопки, menu, статусы и подсказки.
- **DT_UI_Boosters**: GameplayPerkID, Image, DisplayName, Description, Localizations[culture] с парой имя/описание.
- **DT_UI_Cards**: тот же presentation-контракт для карточек. Таблица переопределяет оформление, существующий DT_Perks остаётся источником эффектов и fallback.

Ссылки назначаются в `DA_GameplayUITheme → Theme → Data`; замена картинки/имени/описания делается строкой соответствующей DT. UI-строка не создаёт gameplay-эффект. Повторные GameplayPerkID, пустые данные, отсутствующие варианты языка и несовпадающие параметры диагностируются helper-валидацией. `FText` форматирует целые фразы с именованными параметрами; explicit-culture helpers не меняют язык редактора. Ники не переводятся, input передаётся по реальному управлению.

Набор целевых языков и полнота переводов ещё согласуются/проверяются. Обновление уже открытого runtime-окна после смены языка не объявляется завершённым. Подробности — [LocalizationContract](E:/DEVGAME/MessControl/Docs/UI/LocalizationContract_20261016.md).

## Контроль качества и точки приёмки

| Этап | Состояние |
|---|---|
| План15 и границы | Утверждены пользователем; TAB и DT входят, F1/F3 и главное меню исключены |
| Направление v10 и последние правки | Утверждены; I01 v4/I03 v3/остальные smooth v2 — текущие эталоны |
| Source implementation | Код подготовлен; интеграция и исправления по компиляции продолжаются |
| WBP/DT/иллюстрации и Details Preview | Проверка в Unreal впереди; успешное сохранение/просмотр не заявлены |
| Приёмка групп виджетов | Сначала review главного агента по реальному результату, затем пользователь |
| Итоговая проверка | Разрешения/переводы/ввод/совмещения и короткая solo PIE ещё не завершены |

Каждый агент получает конкретный компонент и границу файлов; главный агент проверяет diff и результат Unreal, соответствие утверждённым референсам, смысл данных и сохранение поведения. Синтетические fixtures не выдаются за снимок игры. Новые визуальные отклонения и изменения механик не принимаются скрыто внутри реализации.

Приёмка включает 1280×720, 1920×1080, 2560×1440 и широкую область, яркий/тёмный игровой фон, длинный русский/тестовый перевод, Theme inheritance, замену DT image/name/description, Preview/Play/Stop/Reset и возврат input. Статус пакетов, фактической компиляции и gameplay фиксируется только после соответствующих результатов; этот план их не подменяет.
