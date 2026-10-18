# Phase 1: игровой интерфейс

Последующая визуальная доводка 17–18 октября, успешный pass 37 и новые превью: [GameplayUIPolish_20261017.md](E:/DEVGAME/MessControl/Docs/UI/GameplayUIPolish_20261017.md). Ниже — исходный отчёт интеграции pass 32.

Актуально на 17 октября 2026 года. Техническая база Phase 1 готова: editor build **pass 32 — SUCCESS**, сохранены и подключены новые виджеты, завершены статические превью, короткие проверки solo PIE и управление превью из Details. **Визуальное утверждение пользователем ожидается.** [Лог сборки pass 32](E:/DEVGAME/MessControl/Saved/GameplayUI/ColdEditorBuild-pass32.log).

## Что подключено

Компактный HUD и игровые меню используют общую морскую палитру с бирюзовыми контурами, светлыми карточками и кремовыми акцентами. Панели, заполнения, ромбы инструментов и малые HUD-иконки рисуются процедурно. Тексты, значения, полосы и кнопки остаются отдельными элементами UMG; экранный растр с запечённым интерфейсом не используется.

В [master Theme](E:/DEVGAME/MessControl/Content/Gameplay/UI/Theme/DA_GameplayUITheme.uasset) сопоставлены **16 новых WBP Phase1**: GameplayHUD, ReserveTooth, Stamina, Booster, PerkChoice, PerkCard, Scoreboard, Emotes, Tutorial, PauseMenu, ConnectionMenu, FreezeBar, DeliveryCaption, PlayerName, UlcerProgress и ThemePreview. Их деревья редактируются в Designer. BossHealth сохраняет `WBP_BossHealth_Reference`, GameplayAlert — прежний WBP; их отдельные роли не переподключались.

HUD показывает восемь резервных зубов с текущим HP и необратимой потерей, личные HP игроков отдельно, таймер/задачу, чистоту, командный XP и инструменты. Выносливость расположена снизу по центру, дополнительный ресурс плавания — над ней. Число видимых игроков меняет ширину командной панели. Длинные подписи увеличивают высоту соответствующих панелей. Механика дней в HUD отсутствует. Карточка выбирается нажатием на неё; отдельная большая кнопка выбора внутри каждой карточки не нужна.

Общие цвета редактируются через `Palette` master Theme, включая RGBA кнопок, ресурсов, панелей и текста. Из WBP доступна панель общей темы; собственный `Theme` дочернего виджета служит локальным override. Для наследования общей палитры оставить его пустым. Подробности базовой системы и история предыдущих этапов сохранены в [исходном документе](E:/DEVGAME/MessControl/Docs/UI/GameplayUIStyleSystem_20261016.md).

## Таблицы и локализация

| Таблица | Строк | Что редактировать |
|---|---:|---|
| [DT_UI_Localization](E:/DEVGAME/MessControl/Content/Gameplay/UI/Data/DT_UI_Localization.uasset) | 522 | Подписи интерфейса и переводы по ключам |
| [DT_UI_Boosters](E:/DEVGAME/MessControl/Content/Gameplay/UI/Data/DT_UI_Boosters.uasset) | 3 | Изображение, имя, описание и локализации бустера |
| [DT_UI_Cards](E:/DEVGAME/MessControl/Content/Gameplay/UI/Data/DT_UI_Cards.uasset) | 72 | Изображение, имя, описание и локализации карточки по Gameplay ID |

Содержимое трёх сохранённых DT, их тексты и назначения изображений в Phase 1 не изменены. Проверка временной строки с произвольным именем показала **описание X01 и отдельно назначенное изображение P03** в authored `WBP_PerkCard_Phase1`; корневой агент просмотрел результат. Временная таблица удалена, исходные ссылки и настройки восстановлены. У **64 строк карточек пока нет уникальной иллюстрации**: используется редактируемый fallback, собственное изображение можно назначить в DT.

Английские переводы ещё не заполнены: `en-GB` сейчас возвращает русский fallback. Такие PNG подтверждают разрешение данных и fallback, но не проверку вёрстки настоящего английского текста. Новые переводы, полная языковая приёмка и проверка физического gamepad этим этапом не заявляются.

## Что проверено

- **81/81 PNG** экспортированы из 12 ролей без игрового мира, с экранными вариантами 1280×720, 1920×1080 и 2560×1080. Малые компоненты представлены отдельными обрезанными превью; они не доказывают размещение в мировом viewport. Отчёт содержит размеры, классы и SHA256: [render_report.json](E:/DEVGAME/MessControl/Saved/GameplayUI/Phase1/Previews/render_report.json).
- Отдельно получены **6 PNG данных и темы**: RU/en-GB карточка произвольной строки и HUD/пауза до и после изменения RGBA. Прозрачность выносливости и кнопок проверена при alpha **0.35**; палитра, culture, ссылки CDO и сохранённые пакеты восстановлены. Три DT сохранили SHA256. См. [data_theme_report.json](E:/DEVGAME/MessControl/Saved/GameplayUI/Phase1/ThemePreviews/data_theme_report.json).
- В коротком **solo PIE pass 30** проверены выбор карточки мышью и возврат игрового ввода, меню паузы → настройки → назад, запрос Emotes через настоящий Menu API с последующим переходом Three → Signals и закрытием T. TAB press создаёт `WBP_Scoreboard_Phase1`, release скрывает его. Это точечные пути, а не полная приёмка всех способов навигации.
- В новом **solo PIE pass 32** F2 открыл реальный `WBP_ConnectionMenu_Phase1`: виджет видим, находится во viewport, курсор включён. Повторный F2 закрыл меню и вернул игровой ввод. [Connection.png](E:/DEVGAME/MessControl/Saved/GameplayUI/Phase1/PIE/Connection.png) содержит актуальный игровой экран. Все проверочные PIE завершены, настройки редактора восстановлены.
- В Details настоящего `WBP_PerkChoice_Phase1` нажаты **Preview / Play / Stop / Reset**. Play продвинул время до **2.240067**, два измерения после Stop подтвердили остановку, Reset вернул **0**. Результат `passed`: [control_report.json](E:/DEVGAME/MessControl/Saved/GameplayUI/Phase1/Designer/control_report.json), [DetailsPreview.png](E:/DEVGAME/MessControl/Saved/GameplayUI/Phase1/Designer/DetailsPreview.png).
- Все **6 SHA256 boss baseline** и **17 исторических WBP v10** совпали. F1/F3 сохранили прежние обработчики. Главное меню не переоформлялось в этой работе. Boss reference-иконки, материалы и сохранённый WBP не изменялись. См. [boss_baseline.json](E:/DEVGAME/MessControl/Saved/GameplayUI/Phase1/boss_baseline.json) и [authoring.json](E:/DEVGAME/MessControl/Saved/GameplayUI/Phase1/authoring.json).

Корневой агент просмотрел выборку из 15 статических PNG pass 30 и не обнаружил критических визуальных блокеров. Это проверка указанной выборки, а не визуальное утверждение всех 81 изображений пользователем. Сводка: [ReviewSheet.png](E:/DEVGAME/MessControl/Saved/GameplayUI/Phase1/ReviewSheet.png), [visual_review.json](E:/DEVGAME/MessControl/Saved/GameplayUI/Phase1/visual_review.json).

Основные изображения для просмотра: [HUD 1080p](E:/DEVGAME/MessControl/Saved/GameplayUI/Phase1/Previews/GameplayHUD_Default_ru_1920x1080.png), [HUD с длинным текстом](E:/DEVGAME/MessControl/Saved/GameplayUI/Phase1/Previews/GameplayHUD_LongText_ru_1920x1080.png), [карточки](E:/DEVGAME/MessControl/Saved/GameplayUI/Phase1/Previews/PerkChoice_Risky_ru_1920x1080.png), [меню паузы](E:/DEVGAME/MessControl/Saved/GameplayUI/Phase1/Previews/PauseMenu_Default_ru_1920x1080.png), [TAB](E:/DEVGAME/MessControl/Saved/GameplayUI/Phase1/Previews/Scoreboard_LongText_ru_1920x1080.png), [карточка X01 с изображением P03](E:/DEVGAME/MessControl/Saved/GameplayUI/Phase1/ThemePreviews/Card_ArbitraryRow_ru.png), [RGBA HUD](E:/DEVGAME/MessControl/Saved/GameplayUI/Phase1/ThemePreviews/Theme_RGBA035_HUD.png), [RGBA кнопок](E:/DEVGAME/MessControl/Saved/GameplayUI/Phase1/ThemePreviews/Theme_RGBA035_Pause.png). Пути отчётов и изображений обновляются повторными проверками; это не неизменяемые архивные версии.

## Остаток приёмки

Нужен визуальный аппрув пользователя на текущие экраны. Дальнейшие min/max-сценарии боссов относятся к Phase 2. Полная навигация мышью и физическим gamepad, прокрутка всех длинных списков до конца, приёмка настоящих переводов, сетевые испытания, cook/package и полный игровой прогон этим этапом не подтверждаются.
