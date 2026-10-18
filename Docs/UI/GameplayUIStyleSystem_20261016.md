# Стилевая система игрового интерфейса

Последняя доводка Phase 1: [GameplayUIPolish_20261017.md](E:/DEVGAME/MessControl/Docs/UI/GameplayUIPolish_20261017.md), успешный pass 37 и новые превью. Исходная интеграция с проверкой Details: [Phase1GameplayUI_20261017.md](E:/DEVGAME/MessControl/Docs/UI/Phase1GameplayUI_20261017.md). Визуальное утверждение пользователем ожидается. Ниже — история предыдущих этапов, включая pass 15 и BossHealth Reference.

Обновлено 17 октября 2026 года, по результатам финальной сборки 15. Утверждённый набор — [FriendlyUISuite v10](E:/DEVGAME/MessControl/ArtSource/UI/FriendlyUISuite_20261017_v10/README.md): I01 `_smooth_v4`, I03 `_smooth_v3`, остальные I02/I04–I08 `_smooth_v2`. Сохранены и успешно компилируются 18 authored WBP, сохранены три DT со строками 522/3/72 и подключены 14 отдельных иллюстраций. Проходят 44/44 проверки локализации, 27 технических render fixtures и проверка изменения с восстановлением RGBA общей темы. Подтверждён короткий actual solo PIE с резервом 8/8, выбором награды, паузой, F2, TAB и активацией G; точный охват приведён ниже. Полная приёмка всех состояний, клавиш и физического gamepad этим статусом не заявляется; проверка продвижения, остановки и сброса времени Designer Preview завершена.

## Охват и оформление

В работу входят HUD, боссы и алармы, ресурсы, бустеры, награды и договоры, обучение, эмоции, TAB, игровые ESC/настройки/титры/подтверждения и F2 ConnectionPanel, результаты и надписи в мире. Главное меню, embedded front end, lobby и F1/F3 остаются вне переоформления. Игровые меню вынесены в отдельный `UMCGameplayMenuWidget`; общий `MCMainMenuWidget` не является носителем нового скина.

- Морские синие поверхности и контуры, светлая мята и крем, плавные рисованные зубные иконки. Пиксельный стиль больше не ограничение. LCD/LED остаются приёмом самих дисплеев.
- LCD — компактные таймеры, задачи и счётчики; красный LED — HP боссов и опасность; щит имеет отдельное значение. Body-текст и длинные переводы используют читаемый шрифт с кириллицей.
- Композиция HUD: восемь резервных зубов слева сверху, LCD таймера и задачи под ними, поднятая чистота, XP и команда справа сверху, ромб инструментов слева снизу, выносливость снизу по центру. Номер дня исключён.
- Резерв арены и личные HP четырёх игроков различаются. Каждый резервный зуб показывает текущий HP, пустую часть и необратимую потерю; одна агрегированная полоса `MouthHealth` это представление не заменяет.
- Исправление от 18 октября: изменение `DA_RunRules.InitialArenaTeeth` с 12 на 8 при настройке HUD было ошибкой — поле управляет физическим спавном, поэтому исчезли зубы ID 9–12. Для арены восстановлено 12; скрипт `configure_eight_reserve_teeth.py` теперь только проверяет настройку и не меняет игровой резерв. Исторический PIE 13 с восемью зубами не подтверждает корректность количества зубов. Текущий HUD имеет восемь отдельных иконок, но числовой счётчик учитывает весь резерв: при 12 зубах он показывает 12/12; индивидуальные иконки для ID 9–12 ещё не добавлены. Стартовые персонажи игроков исключены из `ArenaTeeth`. Выбитые и израсходованные записи остаются в своих местах, отображают 0 HP и уменьшают доступный счётчик.
- HUD, боссы и алармы используют полноэкранный Canvas и DPI проекта по короткой стороне экрана (`ShortestSide`). Фиксированной обёртки 1920×1080 нет: угловые элементы HUD привязаны к фактическим краям viewport, включая ultrawide. Модальные панели используют `ScaleToFit / DownOnly`. Техническое создание render fixtures проверено; читаемость, пересечения и композиция на разных разрешениях ещё проходят визуальную проверку.

F03/v8 и ранние белые/пиксельные варианты — история поиска. Унаследованная подпись ПКМ в растре не меняет управление: действие щётки — ЛКМ, подпись берётся из фактического input. Числа и имена референсов и Designer fixtures синтетические, это не runtime capture. Цельный PNG экрана не используется как фон с запечёнными текстами и кнопками.

## Общая тема и редактирование в WBP

База [UMCGameplayUIWidget](E:/DEVGAME/MessControl/Source/MessControl/Public/MCGameplayUIWidget.h) предоставляет `Theme`, `StyleBindings` и Preview. Для WBP в `/Game/Gameplay/UI/`, наследующего эту базу, editor-код добавляет в Details категорию **Gameplay UI Preview** и кнопку **Gameplay UI Theme…**. Кнопка открывает общую панель темы из самого WBP; открытие master Theme в работающем редакторе проверено.

1. Открыть игровой WBP, выбрать его корень/настройки экземпляра и нажать **Gameplay UI Theme…** в Details. Панель содержит выбор Theme asset, редактируемые свойства и образец интерфейса.
2. Выбрать `/Game/Gameplay/UI/Theme/DA_GameplayUITheme`. В `Palette` менять RGBA через поля или штатный color picker; Undo/Redo предусмотрены editor-кодом. Нажать **Save shared theme**, чтобы сохранить DA. WBP сохраняется отдельно при изменении его дерева или свойств.
3. Для общего оформления оставить `Theme` дочерних виджетов пустым. Разрешение идёт от собственного override к теме родителя, затем к master DA. Явно назначенный дочерний Theme — локальное исключение; сбросить его в `None`, чтобы вернуть наследование.
4. Геометрию, иерархию, padding и переносы редактировать в Designer. Для добавленного элемента использовать `StyleBindings`: имя UMG-элемента, `Target` (Background/Text/Image/Progress/Button), семантический `Color`, размер и жирность шрифта. Связанный цвет будет повторно применён из темы при обновлении.

Сохранённый `DA_GameplayUITheme` содержит `Palette`, `BodyFont / HeadingFont / DisplayFont`, `CornerRadius / OutlineWidth`, ссылки на три DT и `WidgetClasses`. При отсутствии доступного DA остаётся native fallback. Панель образца использует сохранённый `/Game/Gameplay/UI/WBP_UIThemePreview`. Открытие панели и техническая проверка изменения RGBA master theme с восстановлением исходных значений прошли. Полная ручная проверка Undo/Redo, сохранения и повторного открытия ещё не завершена.

| Поля Palette | Назначение |
|---|---|
| Surface, SurfaceAlt, Marine, Mint, Cream, Border, Backdrop | Поверхности, рамки и подложка модального окна |
| Ink, Muted, OnMarine | Основной, вторичный и контрастный текст |
| Button, ButtonHover, ButtonPressed, ButtonDisabled, ButtonText, Focus | Состояния кнопок и видимый фокус |
| Benefit, Cost | Польза и цена по смыслу модификатора; уменьшение cooldown — Benefit |
| BossHealth, Shield, Warning | HP противника, отдельный щит и опасность |
| LCDBackground, LCDInk, Stamina, Cleanliness, Accent | Счётчики, ресурсы и акцент |

Палитра задаёт общий стиль. Цвет игрока, фактические состояния опасности и значения ресурсов остаются данными. Прицелы еды/спрея/навигации и служебные фрагменты сохраняют существующую native-отрисовку; наличие редактируемого UMG-дерева не означает перенос всей отрисовки в Designer.

## Классы виджетов и наследование

В `Theme → Widgets → WidgetClasses` роль сопоставляется authored WBP-классу. [UMCGameplayUIFactory::ResolveClass](E:/DEVGAME/MessControl/Source/MessControl/Private/MCGameplayUIFactory.cpp) проверяет, что класс конкретный и наследуется от native-класса роли; отсутствующее/невалидное значение возвращает native fallback.

Основные роли: `GameplayHUD`, `ReserveTooth`, `Booster`, `GameplayAlert`, `BossHealth`, `Stamina`, `PerkChoice`, `PerkCard`, `Scoreboard`, `Emotes`, `Tutorial`, `PauseMenu`, `ConnectionMenu`, `FreezeBar`, `DeliveryCaption`, `UlcerProgress`, `PlayerName`. Для этих 17 ролей сохранены и успешно компилируются новые `WBP_*_v10` в `/Game/Gameplay/UI/`; вместе с `WBP_UIThemePreview` это 18 authored WBP. Master DA и три DT сохраняют стабильные имена без суффикса.

Наследуемые свойства вроде `CardWidgetClass`, `BoosterWidgetClass` и `AlertWidgetClass` доступны в Class Defaults/Details соответствующего WBP. Явный override выбирает локальный класс; пустое значение там, где подключён factory fallback, использует роль master theme. Карта ролей не перестраивает уже существующее authored дерево: замена класса применяется при создании/построении компонента. Существующие деревья сохраняются; повторный запуск authoring не должен заменять ручную работу.

В Class Defaults `WBP_BossHealth_v10`, категория `Appearance`, `PortraitBoxSize` задаёт общий конверт портрета 44×44 с сохранением пропорций (`ScaleToFit / DownOnly`). `PortraitScaleByBoss` позволяет отдельно компенсировать прозрачные поля картинки по `BossID`. При authoring начальные оптические коэффициенты вычисляются из размеров содержимого в `TechnicalQA.json`; это редактируемые значения WBP.

## Три таблицы данных

Ссылки назначаются в `DA_GameplayUITheme → Theme → Data`. RowStruct и lookup описаны в [MCUIDataTypes.h](E:/DEVGAME/MessControl/Source/MessControl/Public/MCUIDataTypes.h) и [MCUIDataLibrary.h](E:/DEVGAME/MessControl/Source/MessControl/Public/MCUIDataLibrary.h).

| Таблица в `/Game/Gameplay/UI/Data/` | Сохранённых строк | Что редактировать |
|---|---|---|
| DT_UI_Localization · FMCUILocalizationRow | 522 | Row Name = стабильный `UI.*` ключ; `SourceText`, `Context`, `Localizations[culture]` для названий действий, кнопок, меню, подсказок и статусов |
| DT_UI_Boosters · FMCUIBoosterRow | 3 | `GameplayPerkID`, `Image`, `DisplayName`, `Description`; `Localizations[culture]` с переведёнными DisplayName/Description |
| DT_UI_Cards · FMCUICardRow | 72 | Тот же presentation-контракт для наград/перков: gameplay ID, отдельная картинка, имя, описание и языковые варианты |

Чтобы заменить иллюстрацию или описание, изменить строку соответствующей DT и сохранить таблицу. `GameplayPerkID` связывает оформление с уже существующим эффектом из `DT_Perks`; имя строки presentation DT может отличаться от gameplay ID. Добавление UI-строки не создаёт новый эффект. Повторяющиеся `GameplayPerkID` неоднозначны и не выбираются произвольно. При отсутствии данных остаётся безопасный fallback; у карточки существующие поля `DT_Perks` используются как резерв.

Подключены 14 отдельных иллюстраций: три для бустеров, три для боссов и восемь для карточек. У остальных 64 карточек остаётся fallback-изображение; отдельные иллюстрации для них ещё не готовы. Названия, описания и другие локализуемые подписи выводятся виджетами и не запекаются в art.

Общие подписи получают `FText` через `ResolveText`; целые фразы форматируются именованными параметрами. Для явного языка есть `FormatTextForCulture`, `NumberForCulture`, `DecimalForCulture`; они не меняют culture редактора. Ники игроков не переводятся. Переводы и набор целевых языков не считаются завершёнными; обновление уже открытой runtime-модалки после смены языка ещё требует отдельной проверки/доводки. Подробный контракт — [LocalizationContract](E:/DEVGAME/MessControl/Docs/UI/LocalizationContract_20261016.md).

## Карточки и действия

В I02/I03 вся карточка является одной clickable/focus областью; нижней полосы «ВЫБРАТЬ» нет. Сохраняются клавиши 1–3, навигация gamepad, фактические правила переброса, ожидания и выбора. Hover, focus/selected и disabled различаются без изменения размера.

В подтверждении договора I08 две карты служат просмотром и не выбираются повторно. Полос «ВЫБРАНО» нет; общие Accept/Decline находятся снизу. Длинный текст переносится; полезные и вредные числовые фрагменты окрашиваются по смыслу stat, а не только по знаку числа. Правила эффектов и серверные вызовы не задаются внешним видом карточки.

## Preview из Details

Открыть игровой WBP и настроить наследуемые поля `PreviewScenario`, `PreviewState`, `PreviewCulture`, `PreviewDuration`. `PreviewCulture` принимает UE culture, например `ru` или `en`, и действует только на preview. `PreviewTime` показывает текущее время; набор применимых состояний определяется компонентом.

| Компонент | Данные и состояния |
|---|---|
| HUD / BossHealth / GameplayAlert | Empty/OneBoss/TwoBosses/ThreeBosses, низкие ресурсы и длинный текст; дополнительно именованные состояния компонента |
| PerkChoice | PreviewState: Reward, Level, Risky, ContractConfirm, Pending, RerollSpent |
| Booster | `PreviewBoosterRow` (Data Table + Row Name), `PreviewQueueCount`; сценарии доступного, активного и взведённого состояния; `bPreviewRowFound / bPreviewUsedFallback` показывают результат lookup |
| Отдельная PerkCard | `SampleCard` и `bPreviewReadOnly`; универсального Row picker у карточки сейчас нет. Рабочие карты панели получают image/name/description из DT_UI_Cards |

**Preview** применяет выбранные fixture и текущий кадр. **Play** запускает циклическое продвижение времени до PreviewDuration. **Stop** останавливает продвижение. **Reset** вызывает сброс компонента. В editor-коде [MCGameplayUIPreview.cpp](E:/DEVGAME/MessControl/Source/MessControlEditor/Private/MCGameplayUIPreview.cpp) время подаётся через editor ticker в `ApplyEditorPreview(Time)`, без игровых акторов и PIE. Это проигрывание фаз native presentation; управление authored UMG animations через Sequencer пока не подключено и не считается проверенным. Runtime NativeTick/PlayAnimation не служат механизмом Designer Preview.

В финальном редакторе кнопки нажаты через Slate: Play продвинул время, два чтения после Stop дали одинаковые 0.4564977 секунды, Reset вернул 0. Master Theme открыт из Details. Результат: Saved/GameplayUI/designer_validation.json.

## Проверка и передача

В проходе 13 подтверждены сохранение и успешная компиляция 18 authored WBP, три DT с 522/3/72 строками, подключение 14 отдельных иллюстраций, 44/44 проверки локализации и техническое выполнение 27 render fixtures с текущими native-шрифтами и цветами. Проверка изменения RGBA master theme с восстановлением исходных значений также прошла.

Короткий actual solo PIE подтвердил следующие действия; [runtime smoke report](E:/DEVGAME/MessControl/Saved/GameplayUI/runtime_smoke.json) содержит снимки состояния контроллера, принадлежащих ему WBP и восьми резервных зубов.

| Проверка | Подтверждённый результат |
|---|---|
| Runtime-классы и резерв | Реальный `MCPlayerController` владеет authored HUD, PerkChoice, Booster и BossHealth WBP; `ArenaTeeth` имеют ID 1–8, HUD показывает 8/8 |
| Награда | В проходе 12 реальный выбор карточки мышью закрыл Reward и вернул cursor/move/look в игровой режим; в проходе 13 тот же результат получен через `choose_reward_perk(0)` контроллера |
| Общая пауза | Ввод `GamepadStart` → Settings → Back → Back возвращает игровой режим ввода |
| F2 и TAB | Два открытия/закрытия F2 прошли без crash; нажатие/отпускание TAB создало Scoreboard и скрыло его после отпускания |
| Реальный бустер | G израсходовал очередь `Fallback_Grenade` с 1 до 0 и оставил `bGrenadeArmed=true` |

Это ограниченный solo smoke, без сетевых тестов, применения видеонастроек, Host/Join и разрушительных действий. Программная подача `GamepadStart` не подтверждает проверку физического устройства или всех его кнопок. Визуальная приёмка остальных состояний, одновременных панелей, длинных текстов и разрешений ещё не считается завершённой. Проверяемые размеры — 1280×720, 1920×1080, 2560×1440 и ultrawide, со светлым и тёмным игровым фоном.

Дополнительно в финальной сборке исправлено обрезание длинных имён в TAB и подтверждено сохранение изменённой палитры после GC с последующим восстановлением исходных RGBA (Saved/GameplayUI/final_regressions.json). Индекс успешно обновлён для 16 ключевых сохранённых пакетов, в том числе профиля резерва и boss WBP. Полный цикл Undo/Redo и все комбинации игровых окон не проверялись. Для 64 карточек остаётся fallback art; набор целевых языков и полнота переводов ещё требуют согласования. Игровые меню и F2 остаются в текущем охвате, главное меню и F1/F3 — вне него. Сетевые проверки и отдельный Windows build/cook/package требуют отдельного запроса.

## BossHealth_Reference: текущая реализация и границы проверки

Дополнение от 17 октября 2026 года уточняет новый компонент боссов после отдельного фидбека пользователя. Сведения о v10 и его прежней приёмке выше сохраняются как история; они сами по себе не подтверждают проверку новой реализации. Для боссов выбран исходный `I02_risky-reward.png` с красным сердитым зубом, серым рыцарем, серым зубом в фиолетовой шляпе и тёмным металлическим оформлением. Новые дружелюбные портреты v10 не являются текущим набором этого компонента.

В runtime назначен `/Game/Gameplay/UI/WBP_BossHealth_Reference`, наследующий `UMCBossHealthWidget`, через роль `BossHealth` общей темы. Из растра импортированы только три отдельные иконки `T_UI_BossIcon_Caries_Ref`, `T_UI_BossIcon_NutKnight_Ref` и `T_UI_BossIcon_NutMage_Ref` в `/Game/Gameplay/UI/Art/`. Их источник, pivot, оптические границы и проверка сохранения видимых RGB записаны в [IconsOnlyManifest.json](E:/DEVGAME/MessControl/ArtSource/UI/BossReferenceMatch_20261017/IconsOnlyManifest.json). У иконок нет металлической рамки или чёрной подложки. Нарезанные рамки, leftcaps и 9-slice из раннего эксперимента не импортированы и не используются; их файлы оставлены только как помеченная отклонённая история.

Рамка строится процедурно по alpha текущей иконки и объединяется с телом полосы. При замене изображения художник предоставляет только PNG: отдельную рамку рисовать или нарезать не требуется. [MCBossHealthWidget.cpp](E:/DEVGAME/MessControl/Source/MessControl/Private/MCBossHealthWidget.cpp) подготавливает поле расстояний размером 256×256 за 18 проходов при отсутствии подходящей записи в кеше. Ключ учитывает текстуру, её текущий ресурс, размер отрисовки и padding; обычное обновление HP использует готовое поле, а не повторяет 18 проходов каждый кадр. Цвета поверхности и металла задают `BossSurface`, `BossRim`, `BossRimHighlight` общей темы; HP и щит используют `BossHealth` и `Shield`.

| Сохранённый ресурс в `/Game/Gameplay/UI/` | Назначение |
|---|---|
| `Materials/M_UI_BossFrameProcedural` | Адаптивная рамка и углубление вокруг alpha иконки |
| `Materials/M_UI_BossReferenceGrid` | Динамическая LED-сетка HP и сегменты щита |
| `Materials/M_UI_BossShieldProcedural` | Процедурная иконка щита |
| `Fonts/FF_MCBossNames_Regular_Font` | Runtime-шрифт имён на основе производного Cuprum |
| `Fonts/FF_MCBossDigits_Regular_Font` | Runtime-шрифт процентов на основе производного Tektur |
| `Fonts/FF_MCBossCJK_Regular` | Явный `Droid Sans Fallback` FontFace для японских/CJK-глифов, лицензия Apache 2.0 |

Это три материала и две основные шрифтовые семьи; у каждого runtime-шрифта есть отдельный одноимённый `FontFace` без суффикса `_Font`. CJK использует дополнительный реальный `Droid Sans Fallback` FontFace в составе шрифта имён, без подмены Cuprum для латиницы и кириллицы. Источник файла, хеш и лицензия записаны в [font provenance](E:/DEVGAME/MessControl/ArtSource/UI/BossReferenceMatch_20261017/Fonts/provenance.json), рядом сохранены Apache 2.0 и NOTICE. Японские символы проверены по фактическим глифам в рендере. Имена боссов, проценты, значения щита и локализуемые подписи остаются живыми `FText`/данными. HP, запаздывающая потеря HP, LED-точки, щит и его сегменты не запечены в PNG. Компонент поддерживает отдельные строки для одного, двух или трёх боссов, а щит относится к соответствующей строке.

Редакторная сборка прохода 25 успешно завершена. После адаптации названий выполнены [14 reference fixtures](E:/DEVGAME/MessControl/Saved/GameplayUI/BossReference/render_report.json) и [12 проверок локализованных имён](E:/DEVGAME/MessControl/Saved/GameplayUI/BossReference/LocalizedNames/render_report.json): RU, DE и японский `ja` на 1280×720, 1920×1080 и 2560×1080, плюс отдельный Alert на 1920×1080 для каждого языка. Независимая визуальная проверка основным агентом и агентом темы не выявила блокирующих замечаний. [Текущий рендер трёх боссов](E:/DEVGAME/MessControl/Saved/GameplayUI/BossReference/WBP_BossHealth_Reference_ThreeBosses_ru_900.png) больше не обозначает состояние до адаптации названий. Проверка смены формы иконки представлена рендерами [Wide](E:/DEVGAME/MessControl/Saved/GameplayUI/BossReference/AdaptiveIcon_Wide_OneBoss.png), [Tall](E:/DEVGAME/MessControl/Saved/GameplayUI/BossReference/AdaptiveIcon_Tall_OneBoss.png) и [AlphaHole](E:/DEVGAME/MessControl/Saved/GameplayUI/BossReference/AdaptiveIcon_AlphaHole_OneBoss.png); [отчёт](E:/DEVGAME/MessControl/Saved/GameplayUI/BossReference/adaptive_icon_report.json) фиксирует успешное выполнение и восстановление исходного `BossPortraits`. Fixture используют реальный виджет Unreal с синтетическими данными. Файлы этих рендеров и отчётов обновляются при следующих проходах; ссылки не являются неизменяемыми снимками версии.

В solo PIE проверено изменение названий на уже существующем `BossHealthWidget` реального контроллера, без пересоздания виджета. Переход короткие → длинные → короткие имена изменил высоту стека `224.6 → 336.6 → 224.6`, что подтверждает как расширение, так и обратное сжатие; результат записан в [runtime_name_transition.json](E:/DEVGAME/MessControl/Saved/GameplayUI/BossReference/runtime_name_transition.json). [Runtime_Solo_Maximized.png](E:/DEVGAME/MessControl/Saved/GameplayUI/BossReference/Runtime_Solo_Maximized.png) — захват 2560×1392 с рамкой окна. В этом прогоне поданы тестовые UI-данные трёх боссов: это не бой с тремя AI. После проверки PIE остановлен, изменённые настройки восстановлены. В новом WBP через Slate также нажата кнопка Preview в Details.

Техническая и агентская визуальная проверка прохода 25 завершена в указанном охвате. Пользователь ещё не утвердил результат визуально. Полного опознания исходного шрифта для совпадения 1:1 нет; используемые производные шрифты и CJK fallback задокументированы как текущая реализация. Сетевые тесты и отдельный Windows build/cook/package этим дополнением не заявляются.
