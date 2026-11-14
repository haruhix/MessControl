# Одобрение Composer и локальная проверка PIE — 2026-11-14

## Реализовано

- Отметки одобрения независимы для каждой пары ActionID/Event и отдельно для VFX/SFX. Изначально они не выставлены; подтверждение делает автор.
- В SFX есть checkbox у события в дереве и строка «SFX одобрено» в инспекторе. Легенда: «SFX одобрено · «−» нужна проверка». Промежуточное состояние означает изменённую либо ещё не сверенную текущую версию. В VFX доступны отдельные отметки одобрения визуала.
- Изменение параметров своего lane требует повторной проверки. Layout и preview Solo не меняют одобряемую версию; SFX-правки не инвалидируют VFX и наоборот. Отметка не управляет Enabled, мьютом или блокировкой редактирования.
- Метаданные сохраняются отдельно от игровых профилей в `Config/ComposerReview.json`. Повреждённый файл не перезаписывается; на Windows используется замена через `MoveFileExW` с сохранением прежнего файла при ошибке rename.

## Проверено

- Editor Win64 Development, UE 5.8.1: **PASS**, полная длительность сборки **87,28 с** (`EditorBuild.log`).
- `MessControl.Composer.Review.*`: **4/4 Success**, без errors/warnings, 0,144 с (`ApprovalTests.json`). Проверены сохранение/повторное чтение, stale/unknown state, разделение lanes/actions/events, unmark, сохранность повреждённых JSON и семантические fingerprints на независимом Rocket draft. Тесты не применяют игровые ассеты и не сбрасывают Undo.

## Живой UI

- VFX: отметки события в дереве и инспекторе согласованно выставляются и снимаются настоящими кликами в Slate.
- SFX, `Boss.NutKnight.Roll / Transform`: обе отметки выставлены. Перенос `cut` на строку события добавил слой, после чего обе отметки стали промежуточными. Настоящий Ctrl+Z выполнил «Undo: Добавить звуковой слой события»: обе отметки снова checked, «Сохранить звук» выключено.
- В конце отметки сняты (`reviews: []`). Созданный QA пустой `Config/ComposerReview.json` скопирован в evidence и удалён, вернув исходное отсутствие файла. При первом авторском одобрении он создаётся автоматически. Снимки после очистки: `SFXApproval.png`, `VFXApproval.png` в каталоге evidence ниже.

## Локальный PIE: аудио персонажа

- Финальная последовательность **PASS в описанных границах** (`AudioSummary.json`): настоящий живой pawn начинал со 100 HP; обычный прыжок дал подъём 102,01 см и завершился в `MOVE_WALKING`. Несмертельный урон принят: 100 → 95; смертельный принят: 95 → 0.
- Probe собрал 125 сэмплов, `device_muted=false`. В engine history с флагом `playing_audio` наблюдались `S_Step` — 2 экземпляра, `S_Jump` (приземление) — 2, `S_Fall` — 1, фоновая `Relax` — 1.
- Сохранён `CharacterAudioPIE.wav`: 14,5067 с, stereo, 48 кГц, RMS 0,0140. Ненулевой общий микс включает музыку; он не доказывает отдельный звук получения урона или DeathSpirit.
- В history не обнаружены отдельные custom Hurt/DeathSpirit sounds. Read-only `CharacterAudioSources.json` подтверждает отсутствие добавленных SFX slots у `Character.Hurt / Hit` и `Character.DeathSpirit / Rise, Merge, Release` (`UseDefault`). Это незаполненные назначения, а не подтверждённая регрессия Composer UI; отсутствие наблюдения probe само по себе не доказывает полную тишину.
- `StandUp` и `Pull` не воспроизводились в этом сценарии. Контекстные preview adapters для Movement/DeathSpirit пока не подключены.

Палитра `DA_MouthSounds` содержит `Step → S_Step`, приземление `Land → Jump → S_Jump`, `Fall → S_Fall`, `StandUp → S_StandUp`. Назначение `StandUp` подтверждено данными, его воспроизведение не проверено.

Первоначальные ожидания прогрева/render preparation завершались тайм-аутом; финальный прогон выполнен повторной нативной проверкой после компиляции/подготовки. `PIEAudio_ColdPreparationTimeout.json`, `PIEAudio_RenderPreparationTimeout.json`, а также невалидные попытки `PIEAudio_OutsideArenaFixture.json` и `PIEAudio_HeightBaselineAssertion.json` сохранены отдельно как диагностика. Итог относится к валидному финальному сценарию, а не к этим попыткам.

## VFX: Rocket Play/Pause/Stop

**PASS для `Consumable_Rocket / Target`** (`VFXTransport.json`): настоящая кнопка Play запустила Rocket в изолированном мире. Получены реальные события `Prepared`, `Windup`, `Start`, `LoopStart`, `Flying`, `LoopStop`, `Impact`, `Detonated`, `End`, `ActionFinished`. Наблюдались три активных Niagara-компонента с CPU particle counts 9/3/31.

Настоящая Pause установила `paused=true`. Stop вернул `running=false`, `paused=false`, время 0, `events=[]` и 0 активных Niagara-компонентов. Документ сохранил `dirty=false`, scene audio — `scene_audio_dirty=false`; исходные количества слоёв Start/Loop/Impact — по 2, End — 0, без изменений. Это проверка Rocket, а не подтверждение всех действий каталога.

Финальный `FinalState.json`: PIE завершён, оба окна остановлены, оба общих документа чистые, dirty packages отсутствуют, исходная карта `/Engine/Maps/Entry` восстановлена. Игровые пакеты не сохранялись и Composer Apply не вызывался; запуски использовали `-MCHubTransientProfile` без записи прогресса.

## Ограничения

- Fingerprint описывает настройки события и ссылки на ресурсы; байты внешних Sound Wave/Niagara и других исходных ассетов не хэшируются. Изменение содержимого по прежнему пути пока не гарантирует автоматический stale.
- Editor-only audio probe снимает engine Active Sounds примерно каждые **100 мс**, с ограниченной историей. Короткий звук может попасть между сэмплами; отсутствие наблюдения не доказывает тишину. Это не запись выходного аудио и не измерение физической слышимости или качества микса.
- Этот этап не подтверждает сетевое поведение и packaged readiness.

Скрипты, JSON, логи и снимки: `E:/CodexWork/MessControl/Validation/ComposerApprovalPIE_20261114/`. Генерируемые медиа в Git не добавляются.
