# Паттерны VFX и инструменты обучения

Проверено 30.10.2026 для MessControl / UE 5.8.1. Это исходное исследование и предложение небольшого инструмента. Позже в этой же задаче реализована первая проектная вкладка: текущее состояние и ограничения — [VFXComposerMVP.md](VFXComposerMVP.md). Сторонние плагины не устанавливались. **Сверка галереи 31.10.2026:** текущее окно существенно отличается от визуальной библиотеки и графа аддона; конкретные отличия и критерии переделки — [VFXComposerReferenceAudit_20261031.md](VFXComposerReferenceAudit_20261031.md).

## С чего начать

Встроенный [Emitter Summary](https://dev.epicgames.com/documentation/unreal-engine/summary-quick-start-guide) позволяет отобрать отдельные параметры, целые модули и renderer-свойства, сгруппировать их и дать понятные подписи. Именно эта возможность подходит для панели «цвет / размер / время / количество» поверх сложного Niagara. Упражнение из документации использует Niagara Fluids, но для Summary обычного sprite-эмиттера Fluids не требуется. Внешний плагин для такой панели не обязателен. Настройка Summary в наших NS в этом исследовании не выполнялась. MCP GetSystemSummary — обзор данных системы; это не авторинг GUI Emitter Summary.

Уже установлен `/Game/NiagaraExamples`, связанный с [официальным Niagara Examples Pack](https://www.fab.com/listings/0e188eca-4e54-4fb2-a9ed-d8b8a565e600). Там есть категории следов, воздействий, статусов и применения к мешам. Текущий диск содержит 67 файлов с префиксом NS_, включая утилиты/запекание; это не заявление о 67 уникальных игровых эффектах. Старый каталог в [FabVFXStudy.md](FabVFXStudy.md) имеет другую дату/область подсчёта. Оригиналы служат примерами; для упражнений создаём собственные копии.

## Повторяемые паттерны

| Задача | Основа | Для нашей игры |
|---|---|---|
| Эффект следует за персонажем/предметом целиком | Niagara Component или SpawnSystemAttached, component/socket | Пузыри рядом с зубиком, акцент у руки |
| Частицы рождаются на эмали | Skeletal Mesh Location / Surface Sampling | Отдельные блики на теле; чтобы следовать деформации, требуется update sampling/reproduction |
| Меняется сама эмаль | Материал/MID или Overlay Material | Волна восстановления/влажность; отдельный от Niagara слой |
| Летящий предмет | Gameplay actor/movement/collision + косметические NS | Ракетница: выстрел → хвост → подтверждённый контакт |
| Короткое попадание | Burst в точке и по нормали поверхности | Крюк/удар инструмента |
| След от движения | World-space particles/ribbon, поддержка завершения emission | Мыльный след губки; при телепорте история не должна соединяться длинной полосой |
| Две движущиеся точки | Beam/ribbon с началом и концом | Трос крюка |
| Область на земле | Ground-oriented mesh/decal/NS | Предупреждение гранаты; радиус получает от игровой механики |

Источники: [SpawnSystemAttached](https://dev.epicgames.com/documentation/unreal-engine/API/Plugins/Niagara/UNiagaraFunctionLibrary/SpawnSystemAttached?lang=en-US), [Particle Spawn](https://dev.epicgames.com/documentation/en-us/unreal-engine/particle-spawn-group-reference-for-niagara-effects-in-unreal-engine), [Particle Update](https://dev.epicgames.com/documentation/en-us/unreal-engine/particle-update-group-reference-for-niagara-effects-in-unreal-engine), [UMeshComponent / overlay](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/UMeshComponent), [Ribbon tutorial](https://dev.epicgames.com/documentation/unreal-engine/how-to-create-a-ribbon-effect-in-niagara-for-unreal-engine?lang=en-US).

## Сторонние инструменты

| Инструмент | Подтверждённое назначение | Применимость |
|---|---|---|
| [VFX Composer — VFX4GAME](https://www.fab.com/listings/3320d820-f836-4ab3-b67d-3aedcdfdceb5) | Визуальная библиотека, граф композиции элементов взрыва/огня/дыма/попадания, preview, presets; автор заявляет экспорт NS | При повторном просмотре Included formats 31.10.2026 карточка указала UE5.0, 5.5 и 5.8. Реальная работа в нашем проекте не проверена. Не заменяет игровые привязки. |
| [Niagara BP Generator — IPSE](https://www.fab.com/listings/756046fd-98f2-48aa-a73a-3d15308dbba3?lang=it) | Генерация BP-полей и Apply-функций из поддержанных User-параметров | По доступной первичной карточке заявлены UE5.2–5.7 Win64, 5.8 не подтверждена. Это удобство настройки, не художественный авторинг или полный адаптер mesh/DI. |
| [J Niagara And Parameters](https://www.fab.com/listings/8891d8aa-efe0-44a1-929a-4a1617ea9ec3?lang=ko) | Помощь с передачей параметров из BP/AnimNotify | Не библиотека обучающих паттернов. 5.8 не подтверждена. Обычные runtime setters уже существуют в Unreal. |

Ни один продукт не куплен, не установлен и не проверен в этом проекте. Недоступная в повторном просмотре страница/непоказанная версия означает неопределённость, а не доказанную несовместимость.

## Предлагаемая проектная вкладка

Минимальный путь: выбрать паттерн → нашего зубика/мишень → учебный NS/материал → небольшую Summary-панель → движение/запуск/контакт/промах/прерывание → повтор и сравнение. Шаблоны остаются обычными Niagara/материалами. Сохраняем отдельный preset, а подключение к настоящему навыку делаем через разрешённые события и будущий профиль. Для первой редакторской вкладки подходит [Editor Utility Widget](https://dev.epicgames.com/documentation/unreal-engine/editor-utility-widgets-in-unreal-engine?lang=en-US); отдельный plugin нужен при последующем упаковывании/развитии инструмента, а не для каждого урока.

Runtime-адаптеры остаются в проекте: игровой код определяет collision, урон, очистку, радиус и момент события; VFX его отображает. Обязательны корректные attach/surface targets, прекращение и затухание, отмена/смерть/смена предмета, типы User-параметров и включение ресурсов в prewarm. В текущей базе обычный action Loop останавливается через DeactivateImmediate; замена NS сама по себе не гарантирует мягкий остаток. У Sponge Trail уже отдельное владение хвостом. Исследование не изменяет эти механики.

Профиль `DT_Perks → VFXProfile` описан в [PerkVFXDataWorkflow.md](PerkVFXDataWorkflow.md), но не реализован. Учебные Basics/HealSeed не являются игровым лечением. Первые занятия: простой burst → эффект на движущемся зубике → снаряд с хвостом и попаданием; затем surface sampling и material overlay.
