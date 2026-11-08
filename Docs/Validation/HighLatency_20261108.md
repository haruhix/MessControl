# Высокая задержка — 2026-11-08

Основа сетевого движения переработана и проверяется со стороны **не хоста**. Функциональный PASS и визуальная приёмка разделены; маршрут Бангкок–Якутск–Алматы не воспроизводился, использовалась задержка пакетов на loopback.

## Изменения поведения

- Деформация языка остаётся визуальной; игровая опора устойчива. Боль/волны сохраняют принятые сервером попадания и импульсы.
- Обычная ходьба игроков не блокируется другими игроками. Попадания, мир и крюки сохраняют отдельные игровые проверки.
- CMC, внешнее движение, полная физика и захват/посадка согласуют владельца движения, эпоху загрузки и явный сброс позиции.
- Общая компенсация положения и ориентации охватывает коррекцию CMC и полный replay; камера, руки, инструменты и еда используют согласованные кадры отображения.
- Канонические коллизии/скорость отделены от отображения. Еда влияет на скорость весом; её визуальный перенос и натяжение не должны дёргать персонажа. Принятые входящие удары сохраняются.
- Подтверждение сбора еды использует желаемое состояние, а не повторный toggle по запоздавшему флагу.
- Не-хост получает позы скольжения/ragdoll; обычная передача в BodyFrame сохраняет показанный кадр, явный reset/смена эпохи применяются отдельно.
- История штатной оценки времени сервера ограничена восемью образцами/EWMA после загрузки; Hook-лог дополнен показанной позицией/yaw/смещением.
- Телеметрия обнаружила оставшийся поворот отображения на 180° при ACK. Общий helper теперь сохраняет показанную quaternion-ориентацию через correction/replay и сводит её к канонической в существующем окне ≤0.25 s; игровые координаты/поворот применяются сразу.

Новых анимаций, ресурсов или VFX нет. Новый паттерн разрушения/автоподачи еды **не начат**. Сокращение трафика и устранение любых рывков при любом пинге не заявляются.

## Выполненная проверка

| Сборка | Результат и граница доказательства |
|---|---|
| Build14 | Foundation Baseline/Custom: оба peer PASS, все семь фаз; не-хост при готовом управлении: средний RTT 29.2/311.4 ms, Custom 298.6–412.0 ms. Пять ненулевых обычных коррекций Custom, максимум 7.288 cm; переполнений SavedMoves нет. Явные перестановки fixture исключены отдельно. |
| Build14 | ActiveSkills Baseline/Custom: оба peer PASS; настоящие Hook в обе стороны, Rocket, завершение действий и возврат управления. Плавность отображения требует отдельной приёмки. |
| Build14 → Build15 | Первоначально 58/62 native PASS. Build15: 12/12 выбранных проверок PASS в отрисовываемом Editor, включая ранее упавшие; первоначальные 58 — отдельное доказательство Build14, не единый повторный прогон 62 на Build15. |
| Build16 | Native clock + четыре связанные проверки: 5/5 PASS. FoodClock Baseline/Custom: все четыре участника PASS в каждом профиле, восемь итогов. |
| Build16 | Headless Foundation при добавленных 40 ms: оба профиля PASS; Custom: медиана готового RTT 78.9 ms, 77/88 отсчётов в 50–100 ms, пик 244.9 ms. Это функциональная проверка, без визуальной приёмки. |
| Build17 | Сборка успешна; native-набор 8/8 PASS, включая 90° adjustment + 90° replay, 30/60/120 Hz, absoluteRotation, неравномерный scale, quaternion zero-net deadline, BodyFrame и еду. Финальный Hook Custom: оба peer PASS; канонический replay 265.774 cm и смена yaw 180°→0° сохраняют показанные позицию и yaw 180° через response/replay. |

В логах первого зевка оценённый по UTC фазовый сдвиг Peer1 относительно хоста в Baseline снизился примерно 2.44 s → 0.038 s; это **не RTT** и не оценка всего времени кадра.

## Визуальная проверка и оставшиеся ограничения

- Root просмотрел 60 последовательных кадров Hook с частотой 20 fps: поворот теперь постепенный. В конце тесного подтягивания соседний зуб ещё кратко закрывает камеру; это оставшееся ограничение камеры/фрейминга, а не возврат position/yaw-коррекции по телеметрии.
- В просмотренных плотных кадрах FoodClock Baseline/Custom повторных жёстких скачков стопки/тела не видно. Крупная еда закрывает руки; полная художественная приёмка и совместное таскание в движении не подтверждены. Общий визуальный all-clear не заявляется.
- Scoped refresh Build17 завершён: 14/14 выбранных пакетов current, source/binary hashes до/после совпадают, изменений Content нет; ассеты не сохранялись. На момент scoped-проверки остальные 5154 снимка были schema_changed; затем существующий watcher автоматически начал их отдельное обновление. Старое исключение EUB_SnapToActor вне выбранной области сохраняется в общем каталоге.
- Исходный интерактивный Editor восстановлен: `/Game/Maps/L_Mouth`, PIE выключен, камера совпадает с исходной, dirty packages отсутствуют; пакеты не сохранялись.

## Доказательства и откат

Все пути ниже — внешние локальные артефакты на E:. Видео не добавляются в Git.

- [Foundation: числа, точные строки и Build14 SHA256](/E:/CodexWork/MessControl/Validation/HighLatency_20261108/FoundationClient300_v1/Analysis/Report.md); [ActiveSkills результаты](/E:/CodexWork/MessControl/Validation/HighLatency_20261108/SharedHookClient300_v1/Results.json).
- [Build15 native 12/12](/E:/CodexWork/MessControl/Validation/HighLatency_20261108/FoundationNative_RenderedFix_v1/Results.json); [Build16 native 5/5](/E:/CodexWork/MessControl/Validation/HighLatency_20261108/ClockAndBodyNative_v1/Results.json).
- [Build17 native 8/8](/E:/CodexWork/MessControl/Validation/HighLatency_20261108/SharedRotationNative_v1/Results.json); [финальный Hook Custom](/E:/CodexWork/MessControl/Validation/HighLatency_20261108/FinalRotationHookClient300_v3/Results.json), [телеметрия и визуальная проверка](/E:/CodexWork/MessControl/Validation/HighLatency_20261108/FinalRotationHookClient300_v3/FinalEvidence.json); [scoped refresh Validation](/E:/CodexWork/MessControl/Validation/HighLatency_20261108/PostBuild17AssetRefresh_v1/Validation.json); [восстановленный Editor](/E:/CodexWork/MessControl/Validation/HighLatency_20261108/EditorRestored.json).
- [FoodClock результаты](/E:/CodexWork/MessControl/Validation/HighLatency_20261108/SharedFoodClockClient300_v2/Results.json); [сравнение часов](/E:/CodexWork/MessControl/Validation/HighLatency_20261108/SharedFoodClockClient300_v2/ClockComparison.json).
- Видео не-хоста: [Foundation Custom](/E:/CodexWork/MessControl/Validation/HighLatency_20261108/FoundationClient300_v1/Custom/WindowVideo_FoundationClient300_v1_Custom/Capture.mp4), [финальный Hook Custom](/E:/CodexWork/MessControl/Validation/HighLatency_20261108/FinalRotationHookClient300_v3/Custom/WindowVideo_FinalRotationHookClient300_v3_Custom/Capture.mp4), [FoodClock Custom](/E:/CodexWork/MessControl/Validation/HighLatency_20261108/SharedFoodClockClient300_v2/Custom/WindowVideo_SharedFoodClockClient300_v2_Custom/Capture.mp4).
- Точка до общего рефактора: [Source/Config manifest](/E:/CodexWork/MessControl/Validation/HighLatency_20261108/Foundation_Before_v1/Manifest.json); после него: [manifest](/E:/CodexWork/MessControl/Validation/HighLatency_20261108/Foundation_After_v1/Manifest.json), [clock manifest](/E:/CodexWork/MessControl/Validation/HighLatency_20261108/ClockSync_After_v1/Manifest.json).
- Частный несобранный Hook-experiment сохранён и деактивирован в `HookPresentation_Deferred_v1`; он не является активной общей системой. [Рецепт scoped refresh](/E:/CodexWork/MessControl/Validation/HighLatency_20261108/PostBuild13AssetRefresh_Recipe_v1/README.md).

Примечание о хранении: автоматический follow-on индексатор использовал общий writable DDC на C: по своему штатному запуску; безопасно выделить и перенести только его записи общего кэша нельзя. Общий кэш сохранён. Все явно запущенные агентом builds/tests/exports и доказательства направлены на E:. См. AutomaticIndexerStorageLimitation.json во внешней папке проверки.
