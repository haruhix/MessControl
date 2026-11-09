# Остаток яблока переставал резаться после 12 разрезов

## Причина и исправление

В пользовательской solo PIE найден один оставшийся `MCFoodActor_337`:
испорченное `SM_AppleRedCut`, масштаб 30, fragment/Free, масса 0,751208 кг,
автоматический режим, CPU-readable mesh и объём выше 110³. Нативный `getall`
показал начальный выбор островка и **12 включённых плоскостей**; ожидающей
транзакции нет. Передний выход существует, другие гнилые части успешно
утилизировались в той же сессии. В логе еды — 41/42 и единственный blocker.

`SpawnCutFragments` безусловно отказывал при 12 предыдущих разрезах.
После маленьких срезов мог остаться большой кусок, который уже нельзя было
закончить. Порча не вызывала этот отказ: он также затрагивал свежую еду.

В `MCFoodCutPrototype.cpp` ограничение 12 оставлено только сохранённому
ручному режиму. Новый автоматический режим продолжает резать крупный остаток
независимо от количества прошлых срезов. Собственный объём 110³, проверка
геометрии, готовность выхода, масса, распределение целого зачёта и направление
гнилых частиц не менялись. История не обрезается и не подменяется исходным мешем;
формат её репликации прежний. Новых ресурсов, эффектов и анимаций нет.

## Проверка

Пользователь разрешил завершить текущую PIE и собрать Editor. До остановки
сохранены его данные/поза/меш, нативная история и логи; несохранённых content/maps
не было. Read-only исследование не меняло игровой кусок или счётчики.

- Development Editor собран: 5 действий, 36,87 с, `Result: Succeeded`.
- Две направленные native-проверки прошли в rendered offscreen:
  `SpoiledCutUsesAtomicDiscardRoute` и `SpoiledKnifeDiscardsWithoutFeeding`.
- В первую добавлено восстановление реального AppleRedCut/scale30/позы и
  12 включённых плоскостей. Проверены свежий и испорченный варианты: следующий
  настоящий `HitFood` с плоскостью делит каждый на две готовые части объёмом
  822 344,75 и 572 624,69 см³. Масса 0,751208 и остаточный бюджет 1 сохранены.
  Гнилые части имеют discard receipts, без fresh credit, swallowed и meal sequence.
  Предыдущие проверки атомарного отказа без выхода сохранены.
- Короткий solo PIE с обычным primary input прошёл: гнилое AppleRed при
  сохранённом масштабе 30 разрезано, принята одна часть 100 911,42 см³, крупный
  сосед сохранился. Только Discard, прежние score/throat/meal counters,
  активный Niagara, без повторного приёма; 58 кадров, content/maps чистые.
  Root просмотрел кадры подготовки, удара и результата с игровой камеры.
- Точное восстановление 12-разрезного остатка в PIE остановилось до ввода:
  Editor блокирует `set CutPlanes`, а публичный Python/property API не даёт
  записать protected поле. Этот неуспешный setup сохранён отдельно. Успешный
  обычный PIE не объявляется проверкой 13-го разреза: сам исторический остаток
  и 13-й разрез покрыты native-тестом выше.
- Собственный тестовый Editor закрыт с проверкой PID/PIE/dirty state;
  обычный пользовательский Editor вновь открыт с обновлённой DLL, без
  transient validation profile и отдельного тестового UserDir.

## Доказательства и границы

Корень локальных данных:
`E:/CodexWork/MessControl/Validation/SpoiledCutBlocked_20261109/`.

- [Состояние текущей сессии](E:/CodexWork/MessControl/Validation/SpoiledCutBlocked_20261109/LiveFood_20261108T233724Z.json).
- [Данные проблемного куска](E:/CodexWork/MessControl/Validation/SpoiledCutBlocked_20261109/ExactUserPiece.json),
  [нативная история](E:/CodexWork/MessControl/Validation/SpoiledCutBlocked_20261109/ExactUserHistory.log).
- [Build log](E:/CodexWork/MessControl/Validation/SpoiledCutBlocked_20261109/EditorBuild_01.log),
  [native results](E:/CodexWork/MessControl/Validation/SpoiledCutBlocked_20261109/Native01/Results.json).
- [Solo input/discard](E:/CodexWork/MessControl/Validation/SpoiledCutBlocked_20261109/SoloFallback/20261108T235315Z/Report.json),
  [остановленный exact-history setup](E:/CodexWork/MessControl/Validation/SpoiledCutBlocked_20261109/Solo/20261108T234907Z/Report.json).

Изначальная Python-инспекция не имела доступа к protected `CutPlanes` и вернула
пустой placeholder. За истину принята полная нативная история, а не этот placeholder.
Сеть и новый Windows ZIP не запускались. Стоимость очень длинной истории при
холодном восстановлении отдельно не измерена; произвольный новый gameplay-предел
вместо 12 не добавлялся.
