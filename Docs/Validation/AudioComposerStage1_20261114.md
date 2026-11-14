# Первый этап аудиорежима Composer

Дата: 14 ноября 2026 года. Область: общий workspace VFX/SFX, редактирование существующих сценовых Music/Ambience и их совместный предпросмотр с реальным выбранным действием. Полный охват хаба и ореховых действий ведётся по [карте аудио](E:/DEVGAME/MessControl/Docs/SFXComposerScopeMap_20261114.md).

Статус: код первого этапа подготовлен к Editor-сборке. Компиляция, запуск новых native-проверок и слышимость в Unreal ещё не подтверждены.

## Изменения

- В существующий workspace добавлен режим «Фон сцены». VFX/SFX переключаются без создания второго action draft; source/event GUID, группы и незавершённый черновик сохраняются.
- Музыка и атмосфера читаются из реального `UMCSoundPalette.Scenes`. Header и inspector используют один shared scene session с transactional transient duplicate, Undo/Redo и отдельным явным сохранением Scenes. Поля Events и runtime-профили эффектов этим сохранением не изменяются.
- Сохранение палитры проверяет исходные значения, package-файл, dirty/read-only состояние и сохранённые runtime источники. Устаревший черновик не может молча перезаписать внешние изменения. При ошибке save сохранённый успех не объявляется; локальный черновик удерживается.
- Scene preview использует настоящий `MCSceneAudioComponent` в том же transient Game world и private audio device, что игровой action preview. Выбранная transient палитра переживает Reset/StartAction; owned voices и pending loads завершаются при Stop/Cancel/выключении аудио/PIE/shutdown.
- Пауза охватывает сценовые голоса, включая источники, асинхронно подготовленные во время Pause. Music/Ambience имеют отдельные локальные переключатели прослушивания; они не меняют сохранённую громкость или настройки пользователя игры.
- По умолчанию preview выбирает Hub, Gameplay или NutEncounter по конкретному действию. Можно выбрать существующую сцену вручную. Реальный игровой controller продолжает владеть выбором фона в игре.

## Основные файлы

| Область | Файл |
| --- | --- |
| Общая scene session | [MCComposerSceneAudioSession.h](E:/DEVGAME/MessControl/Source/MessControlEditor/Private/MCComposerSceneAudioSession.h), [реализация](E:/DEVGAME/MessControl/Source/MessControlEditor/Private/MCComposerSceneAudioSession.cpp) |
| Embedded scene UI | [SMCComposerSceneAudioPanel.h](E:/DEVGAME/MessControl/Source/MessControlEditor/Private/SMCComposerSceneAudioPanel.h), [реализация](E:/DEVGAME/MessControl/Source/MessControlEditor/Private/SMCComposerSceneAudioPanel.cpp) |
| Интеграция workspace | [MCVFXActionWorkspace.cpp](E:/DEVGAME/MessControl/Source/MessControlEditor/Private/MCVFXActionWorkspace.cpp) |
| Private preview provider | [MCPerkPreviewRunner.h](E:/DEVGAME/MessControl/Source/MessControlEditor/Private/MCPerkPreviewRunner.h), [реализация](E:/DEVGAME/MessControl/Source/MessControlEditor/Private/MCPerkPreviewRunner.cpp) |
| Общий service clock | [MCPerkActionPreviewPanel.cpp](E:/DEVGAME/MessControl/Source/MessControlEditor/Private/MCPerkActionPreviewPanel.cpp) |
| Сценовый runtime | [MCSceneAudioComponent.h](E:/DEVGAME/MessControl/Source/MessControl/Public/MCSceneAudioComponent.h), [реализация](E:/DEVGAME/MessControl/Source/MessControl/Private/MCSceneAudioComponent.cpp) |

## Проверка исходников

`git diff --check` для изменённых существующих файлов прошёл. Для новых файлов проверены whitespace и соответствие интерфейсов. Отдельный review устранил:

- потерю scene draft, если пользователь сначала соглашался отбросить фон, затем отменял закрытие в диалоге action pack;
- продолжающийся фон после неудачной звуковой примерки;
- лишний краткий restart сцены при явном Stop;
- старт асинхронного scene voice без Pause в момент создания.

Это результаты проверки исходников, не результаты сборки или gameplay acceptance.

## Направленная проверка после сборки

Новые native проверки scene session: общий черновик, Undo, внешнее изменение, независимость Events и сохранение/откат. Проверки runner:

- `MessControl.Audio.ScenePreview.DisabledRetainsDraftWithoutOutput`;
- `MessControl.Audio.ScenePreview.PrivateWorldResetRetainsDraftWithoutOutput`.

Нужен также реальный короткий preview существующего действия с SFX на фоне выбранной Music/Ambience: начать, приостановить, продолжить, повторить, остановить и закрыть workspace. Отдельно проверить save/reopen на изолированной палитре и сохранность VFX/SFX черновика при смене режима. Реальная слышимость и отсутствие оставшихся voices должны быть записаны отдельно от no-output native fixtures.

## Следующие этапы

Первый этап не закрывает contextual preview всего хаба, новый per-phase soundtrack Рыцаря/Мага, отдельные варианты первого/второго melee удара, глобальное SFX Solo/Mute, многослойные stems или музыкальный outro. Сейчас Рыцарь, превращение и Маг разделяют существующий `NutEncounter`; музыка фаз требует отдельного игрового контракта. Полный список действий и приоритетов находится в карте.

Пак эффектов продолжает сохранять event VFX/SFX; scene Music/Ambience сохраняются явно в выбранную палитру. Выбор другой палитры сам по себе не назначает её контроллерам или картам. Новый scene source также требует проверки существующей загрузочной области перед первым использованием в игре.

Игровые ассеты и звуковые назначения этим этапом пока не записывались. Сеть, cook/package и чистый packaged-запуск не выполнялись.
