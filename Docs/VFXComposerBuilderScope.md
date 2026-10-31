# Composer: граница компилятора в одну Niagara System

31.10.2026, UE5.8.1. Код добавлен в `MCVFXComposerBuilderLibrary.h/.cpp`; на момент передачи общей сборке он **ещё не скомпилирован и не проверен исполнением**. Этот документ не означает художественный аппрув или подтверждённую поддержку всей библиотеки.

## Что разделено

- Авторский master — `UMCVFXRecipePreset`: обычные настройки слоёв плюс GUID узлов, позиции и группы. Файл остаётся отдельным от результата.
- Standalone preview существующей композиции — независимые NS; это не доказательство экспорта.
- Builder создаёт отдельную NS с собственными копиями emitter handles. Тот же результат после успешной компиляции можно назначить одному VFX-слоту перка. Профили/действия этим builder не меняются.
- ExportNew сохраняет только новый `/Game` package, не перезаписывает существующие NS/DA. В метаданных результата записываются исходные GUID, имя master и ограничение времени.

## Подтверждённые локальные API

`UNiagaraSystemFactoryNew::InitializeSystem`, `FNiagaraEditorUtilities::AddEmitterToSystem`, `FNiagaraEmitterHandle::SetName`, `FVersionedNiagaraEmitterData::RemoveParent`, `UNiagaraSystem::RequestCompile/WaitForCompilationComplete` экспортированы из Niagara/NiagaraEditor. AddEmitterToSystem создаёт owned emitter, обновляет System graph и Overview graph. Уникальное имя emitter синхронизирует aliases скриптов/renderers. Parent удаляется у копии; отдельно проверяются оставшиеся ссылки на временный source-copy.

`FNiagaraEditorUtilities::UserParameters::RenameUserParameterForSystem` использует стандартный SystemViewModel и AllGraphs. Это надёжнее изменения одного поля parameter store. Для private User-параметров внутри custom HLSL текущий builder отказывает: автоматическое переименование такого текста не считается доказанным.

После ревью Build06 устранена потеря system-level lifecycle: output теперь дублирует первую source NS, удаляет только её emitter handles публичной utility и затем добавляет собственные копии слоёв. Он не пересоздаёт SystemState из factory defaults. Остальные источники принимаются лишь при одинаковой нормализованной сигнатуре SystemSpawn/Update: пустой spawn и один стандартный включённый SystemState, выбранная версия модуля, типизированные resolved inputs, системные RI-константы (без имён emitter instances/compile GUID), warmup/fixed tick/determinism/EffectType. Linked User.Duration допустим только по общему shared-контракту. Custom modules и недоказанные visible dynamic/DI lifecycle inputs отклоняются. Эта правка ожидает следующей общей сборки и регрессии, не считается пройденным тестом по одному source review.

Burst delay меняется через `UNiagaraExternalEditUtilities::Get/SetStackInputData` у существующего `SpawnBurst_Instantaneous → Spawn Time`. Это тот же native слой, который использует Niagara MCP. API помечен Epic как experimental; его зависимость ограничена editor builder и текущей версией UE. Прямое изменение строк RapidIteration не используется: штатный StackFunctionInput обновляет affected scripts и graph overrides.

## Поддерживаемая граница первого прохода

Исходные NS из явно перечисленных проектных папок дополнительно проходят проверку. Префикс папки означает допустимого кандидата, не автоматическую совместимость.

- Включённые standard emitters с внутренними графами, engine Niagara-модулями и локальными curve DI.
- User Float / LinearColor / Vector3f / Bool; override проверяется по точному имени и типу.
- Общие игровые `User.Direction/Radius/Duration/Intensity/Fill/Color` сохраняют имена. Типы и эффективные значения defaults/overrides должны совпадать между слоями. Конфликт сообщает имя параметра и оба слоя; скрытого усреднения или выбора последнего нет.
- Остальные поддержанные User-параметры получают namespace по GUID узла; их defaults/overrides переносятся в результат.
- Burst Spawn Time может получить стартовую задержку. Динамический Spawn Time и continuous SpawnRate delay требуют отдельного адаптера.
- Исходные fixed system bounds переносятся на собственные emitter copies. Так не появляется общая фиксированная коробка, обрезающая чужие dynamic CPU emitters. GPU без поддержанных fixed bounds отклоняется.

Отказ с диагностикой: неизвестные `System.*` / `NPC.*` зависимости, event handlers, simulation stages, stateless emitters, component/actor renderers, неподдержанные DI/внешние module assets, private User в custom HLSL, разные anchors/socket, per-layer transforms и принудительные emission cutoffs. Эти случаи нельзя изображать успешно экспортированными.

**Duration/Tail — длина standalone preview. Экспорт пока сохраняет native Niagara lifetime/cycles; это не запекание общей отсечки по Duration.** Solo служит audition и не исключает другие enabled узлы из экспорта. Тот же текст доступен через `GetExportNotes()` и должен отображаться перед экспортом.

Для возобновления авторинга export сохраняет editor package metadata `MCComposerBuilderVersion=1` и `MCComposerSourceRecipe=<постоянный master object path>`; нормальный путь — открыть этот master, а не утратить состав в плоском результате. Собственный generated NS вне папок библиотеки разрешается как кандидат только с version metadata и загружаемым source master, после чего проходит те же проверки графа. Префикс папки Generated сам по себе разрешения не даёт.

## Исследование источников

Read-only Niagara MCP подтвердил topology/input values текущего `NS_Rocket_Impact`: три CPU emitter, shared User-параметры и локальный Curve DI. Его UpwardMeshBurst содержит custom HLSL с User.*; поэтому общий параметр нельзя молча сделать частным без адаптации runtime-контракта.

На диске найдены 21 standalone NE в NiagaraExamples, но это не 21 готовый блок конструктора. В реально прочитанных NS instances Explosion использует `System.SpriteSizeMin/Max`, system wind и User.Smoke Color; ChimneySmoke — NPC wind и User.SpawnRate/Smoke Color/Sprite Size. Их перенос без исправления зависимостей не входит в текущую границу. Полноценная curated NE-библиотека потребует собственных адаптированных master emitters и явно назначенных ручек SpawnCount/Lifetime/ParticleScale/EmissiveScale, а не одинаковых фиктивных полей на любом NS.

Asset index при исследовании показал `schema_changed` и `build_current=false`; старые snapshots не использовались как актуальный полный экспорт. Editor не закрывался, пакеты не сохранялись и build/PIE этим агентом не запускались. Проверка реального clone → compile → save/reopen → Source unchanged → назначение перку остаётся общему чекпоинту.
