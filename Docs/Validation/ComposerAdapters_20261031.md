# Composer: общий native checkpoint — 31.10.2026

Единый Composer, отдельный transient Game world, UE 5.8.1. Это техническая проверка контекстов и runtime-композиции; художественная приёмка, сетевой режим и packaged запуск в неё не входят. [ReviewBoard-версия action-workspace-20261031](http://127.0.0.1:8765/?task=vfx-composer&version=action-workspace-20261031) опубликована с лично просмотренными основным агентом GIF/MP4 **Build20**. Это демонстрация инструмента; рейтинг/арт-аппрув не выставлены. Финальный read-only UI/состояние окна Build23 ещё ожидаются.

## Сборки и runtime-композиция

| Сборка Editor | Результат | Время |
|---|---|---|
| Build18 | Succeeded | 195.77 с |
| Build19 | Succeeded | 13.58 с |
| Build20 | Succeeded | 19.30 с |
| Build21 | Succeeded | 67.18 с |
| Build22 | Succeeded | 8.91 с |
| Build23 | Succeeded | 33.67 с |

Первичные логи: `E:/CodexWork/MessControl/Validation/ComposerActionWorkspace_20261031/Build18.log` … `Build23.log`. NativeComposition18: **3 success / 0 failures / 0 warnings**, точные тесты:

- `MessControl.VFX.PerkComposition.ImmutableIndependentSources`;
- `MessControl.VFX.PerkComposition.DelayedCancellationAndSubjectValidation`;
- `MessControl.VFX.PerkComposition.ManifestLoadWaves`.

Результат: `NativeComposition18/index.json`. Новая композиция сохраняет самостоятельные исходные NS; строгий merge в единую NS больше не является обязательным барьером основного маршрута. `ActualMCPFreeMagicDrag17` подтвердил native Add двух элементов. Ошибка строгого merge17 остаётся диагностикой старого ограничения, а не успешным экспортом.

В Build20 реально проиграны исходный игровой эффект и независимые NS из Free Magic. Основной агент лично просмотрел `FreeMagicIndependent20_contact.png`; `FreeMagicIndependent20.mp4/.gif` — пригодное свидетельство этого tool-сценария, **не новый принятый игровой арт**. Файлы находятся в том же validation-каталоге; новые записи Build22 ещё не опубликованы.

## Сохранение пользовательского черновика

`AppliedIndependent20.json` подтверждает четыре Impact-слоя, сохранённые профиль и авторский master. Для пользователя сохранён полный редакторский пресет:

`/Game/Tests/VFXPractice/UserDrafts/DA_UserDraft20_Impact`

Профиль и master сохранены под `/Game/Gameplay/ActiveSkills/VFX/ActionCompositions/Consumable_Rocket_20261031_054936_369D6639/`. После тестового назначения **каноническая DT восстановлена побайтно**: `canonical_restored=true`, `table_hash_restored=true`.

Native Import в Build22 прочёл этот сохранённый master: `ImportSavedMaster22.json` содержит четыре Impact GUID и те же исходники — NS_Rocket_Impact, NS_Free_Magic_Hit2, NS_Explosion и NS_Free_Magic_Circle2. `UndoImport22.json` вернул baseline одного слоя, `RedoImport22.json` восстановил **ровно те же четыре GUID/источника**. После теста выполнен Undo обратно к baseline. Это проверка Import/Undo/Redo, не утверждение, что в финальном окне сейчас выбран этот черновик.

Пустой `0/0` у выбранной пассивки означает отсутствие редактируемой VFX-привязки, а не отсутствие её native-сценария. Просмотр механики и редактирование состава различаются. Build23 добавляет маркировку «Просмотр» и соответствующие read-only состояния; финальная UI-проверка этой сборки ещё ожидается.

## Активные контексты: 15 сценариев имеют pass evidence в Build21 + Build23

`E:/CodexWork/MessControl/Validation/ComposerAdapters_20261031/Run_20261031T061622Z_PID41864/Report.json`: завершены 15 сценариев пяти включённых активок; **14/15 native assertions passed**. Rocket, Hook, Sponge, Grenade прошли Target/NearWall/Timeout; Air прошёл Target/NearWall.

**Первый Air Timeout не прошёл:** fixture удержал максимум 1 из 6 продуктов; события Warning/End не наблюдались. Этот диагностический результат (`14_Consumable_Air_Timeout.json`) сохранён. После исправления fixture отдельный повтор **AirTimeout23/Run_20261031T065031Z_PID42876/Report.json** прошёл: максимум **6/6**, реальные Warning, End, LoopStop и ActionFinished, `native_finished`, wall time **6.156 с**, assertion/safety errors пусты. Защищённые SHA, save files и dirty sets не изменились. Это один повтор Timeout; остальные 14 passes относятся к Build21, а не к новому запуску всех 15 в Build23. Driver не включён.

Build21 устранил зависимость часов от перерисовки viewport: общий CoreTicker — единственный владелец хода мира, неактивный/paused мир не тикает. Обычный slow frame разбивается на native шаги до 0.05 с; catch-up ограничен 0.2 с с явным счётчиком отброшенного времени. Реальный local controller без LocalPlayer нужен native input-path. Run подтвердил движение, контакты и отражения; Python не тикает UWorld. Это не тест аппаратного пользовательского ввода и не performance benchmark.

В этом проходе четыре защищённых content-файла сохранили SHA; save files не изменились, dirty packages после reset отсутствовали.

## Пассивки: 27 из 27 setup/lifecycle, не 27 подтверждённых procs

`Passives27/Run_20261031T061845Z_PID41864/Report.json`: **27/27 setup/lifecycle assertions passed** с реальными компонентами, canonical grants и native-сценариями; RNG не принуждался. Источник и scope контекстов: [ComposerPassiveScenarios_20261031.md](ComposerPassiveScenarios_20261031.md).

У 22 записей observer подтвердил предусмотренный native trigger. У **WideBristles, CleanBreath, NeatFreak, LastStroke, RefreshingWork** `native_trigger_observed=false` и сохранён явный fixture follow-up: контакт/результат не подтверждён этим observer. При этом у CleanBreath и RefreshingWork действительно наблюдались Heal cues. Ни отсутствие отдельного события, ни видимый cue сами по себе не дают полного verdict по механике.

Подтверждались родные melee hits, подбор/бросок/доставка, движение, damage, dash и spray emission. Это не числовая проверка всех бонусов, всех случайных proc и всех условий очистки/остаточного спрея. Пассивки **не поддерживают применение заменяемого VFX-профиля**; их native/procedural представление не объявлено Niagara-миграцией. Защищённые content SHA, save files и dirty-package состояние также сохранены.

## Asset evidence и оставшиеся границы

Ограниченный refresh `9a059a7f8eeb49fe80d1c7cc7a6f85d6` завершился **5/5**, `build_current=true`, `current:5` на своём snapshot от `2026-10-31T05:39:24.155Z`: DT_Perks, DA_Rocket_ActionVFX, NS_Free_Magic_Hit2, BP_PlayerCharacter, NS_Sponge_Trail. Остальные 3825 snapshots тогда оставались `schema_changed`. Сохранённый старый EUB_SnapToActor failure не относится к этим пяти; это не новый полный экспорт и не доказательство отсутствия иных ссылок. Последующие source builds не сертифицируются старым snapshot.

Предыдущий полный partial job 3830/3831 и его ограничения сохранены в [ComposerAssetRefresh_20261031.json](ComposerAssetRefresh_20261031.json). Открыты: финальный read-only UI/восстановление окна, художественная оценка поз и эффектов, полная coverage боссов/карт, passive VFX bindings, вся Undo/error-матрица нового формата, длительный leak/overdraw, холодные packaged PSO и сеть. Бывшие публичные окна объединены в один Composer; исторические результаты не удаляются и не переименовываются в новые passes.
