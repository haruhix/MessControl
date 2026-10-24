# Changelog

## 2026-10-24 — Защита от зависания после отбрасывания в глотку

- Вход в глотку проверяется по физическому телу ragdoll и пересечению отверстия между кадрами. Дальние телепорты и устаревшая история не считаются пролётом; перед входом и сбоку от ткани сохраняется безопасная область.
- После шести секунд неудачных попыток найти безопасную позу подъёма запускается обычная смерть и замена из резерва. Повторные удары не продлевают зависание; успешный подъём сбрасывает срок. Отбрасывание не перехватывает тело во время захвата глоткой.
- `MessControlEditor` собран. Девять целевых Automation-регрессий прошли без ошибок и предупреждений: быстрый физический пролёт, безопасные границы/телепорт, заблокированный и временно заблокированный подъём, повторные контакты, захват и штатная замена с одним расходом резерва. У старой фикстуры автоматической доставки явно выбран её legacy-режим; игровые правила ради теста не изменялись.
- В одном solo PIE на настоящем `L_Mouth` выполнены три контролируемых отбрасывания к входу/его краям через native `ApplyHit` из F3. Во всех трёх случаях подтверждены смерть, штатный сменщик и последующее движение на 106–115 см. Это направленные fixtures с разовой стартовой постановкой, а не естественные удары босса; точное положение из пользовательского скрина не воспроизведено. Отчёт: `E:/CodexWork/ThroatKnockback_20261024/Solo/Run_20261024T124724Z_f42ec5/Report.json`. Сетевая проверка не проводилась.
- Прыжок сменщика остаётся отдельным неинтегрированным превью, дальнейшая работа над ним отложена по просьбе пользователя. Обновлена инструкция Spacewar для текущего меню и Tab-панели; автоматический Quick Play ещё не подключён.

## 2026-10-24 — Превью замены из камеры игрока и интеграция косметики коллеги

- В отдельной карте `L_RespawnPlayerPOV` и sequence `LS_ReplacementPlayerPOV` подготовлена цепочка: смерть видимого со спины игрока, освобождение места в ряду, выход сменщика из измеренной поверхности десны, прыжок, squash/rebound при посадке и возврат кадра за нового героя. Два неверных ракурса отклонены после проверки actual PIE кадров. Исходные Stage/Final, анимации, меши и `L_Mouth` сохранены без изменений.
- Снят шестисекундный silent-дубль настоящего локального PlayerController в solo PIE: 180 кадров, 1280×720 после удаления только рамки окна. Видео и отчёты — `Artifacts/RespawnPlayerPOV_20261024`. Это постановка для согласования движения/камеры; native death, пятисекундное ожидание, расход резерва, possession и возврат управления в ролике не выполняются. Интеграция в боевой респавн и финальная художественная полировка сцены отложены до согласования.
- Локальная ветка fast-forward объединена с `dc178a83b`/`4564efa54` после сохранения tracked WIP в `codex/checkpoint-before-colleague-20261024`. Добавлены изменения хаба, семь аксессуаров, их материалы/иконки, исправления следования и кадрирования и новый wordmark коллеги. Локальные 26 строк локализации объединены с новыми строками remote без потерь.
- По принятому замечанию коллеги шесть новых аксессуаров стоят 40–90 монет, авторская кепка остаётся бесплатной. Старые цены и владение не менялись. Оба каталога уже входят в preload manifest; холодный packaged first-use отдельно не проверялся.
- После переноса шкафчиков расширена глубина E-зоны `180→340` в локальных единицах (масштаб station по Y — 0.505), а prompt перенесён с `(0,-285,160)` на `(0,0,240)` над серединой ряда. Это даёт место капсуле перед дверцами; геометрия шкафов, transform station и кадр примерки сохранены.
- Повторная сборка Editor после интеграции успешна. Короткий solo PIE с transient-профилем подтвердил семь цен, недоступность покупки платных вещей при 0 монет и примерку непроданной ковбойской шляпы с сохранением кошелька, владения и экипировки. Root проверил реальные кадры: E видна над шкафами, preview на полу и смотрит к камере. Отчёт: `Artifacts/ColleagueWardrobe_20261024/Run_20261024T111244Z_2a385b/Report.json`. Для изоляции UI pawn временно поставлен в проверенную свободную точку, а камера повёрнута обычными orbit/zoom-input; полноценный пеший подход и packaged/multiplayer этим не подтверждены.

## 2026-10-24 — Скрытая F3-панель и возврат потерянной еды/грязи

- В Editor/Development восстановлено открытие F3-панели без `-MCDevUI`: панель хоста остаётся открытой после Tick и закрывается повторным вызовом. Глобальный `MC.DevUI` остаётся 0; кнопка журнала директора в обычном HUD скрыта (`Collapsed`).
- Фрагменты сохраняют доступную точку родителя при разрезании вне арены. Если сохранённая точка и локальный поиск недоступны, нативное восстановление ищет проверенное место на языке; возврат свежей еды не считается утилизацией или доставкой.
- Добавлен серверный возврат обычного грязного пятна, потерявшего доступную поверхность: низкочастотная проверка, подтверждение недоступности и перенос с сохранением маски, частичной работы, care/status, Batch, Owner и seed. Доступные пятна не перемещаются и не завершаются от бездействия. При отсутствии любого допустимого footprint долг уборки сохраняется; paint tiles используют прежнее ограничение маски реальной тканью, без нового переноса всего canvas.
- `MessControlEditor` Win64 Development собран успешно. Узкие девять Automation-проверок прошли: 0 ошибок, 22 предупреждения фикстурного Engine/Plane об Allow CPU Access. В одном normal solo PIE без `-MCDevUI` подтверждены скрытый HUD/F3-handler и нативный возврат двух временных объектов с сохранением долга; исходные пакеты не сохранялись. Отчёты: `E:/CodexWork/RespawnVideoSoftlock_20261024/Automation/index.json` и `E:/CodexWork/RespawnVideoSoftlock_20261024/SoloRecovery/Run_20261024T100623Z_6eb50f/Report.json`. Охват и ограничения — [Validation.md](Validation.md).

Превью смерти/замены из игровой камеры описано выше отдельно от native-респавна. Эти проверки не подтверждают универсальное отсутствие softlock, сетевую работу, cook/package или итоговое художественное качество временных fixtures.

## 2026-10-24 — Чистая и здоровая замена игрока

- Отключено наследование статуса резервного зуба в `DA_ToothCare` и native defaults: свежий герой не получает кофе, повреждения и расшатывание выбранного зуба.
- В `ProcessRespawns` подготовлено заполнение HP после применения максимума от перков нового Pawn. Обычное получение max-HP перка по-прежнему не лечит.
- Обновлена регрессия `ConcreteReserveRespawn`: грязный резерв с 25 HP и расшатыванием, сменщик с P25 должен получить 115/115, кофе 0 и repair 0; повторный refresh не увеличивает максимум повторно. Сохранены проверки одного расходования, PlayerState и последней жизни.
- Собран `MessControlEditor` Win64 Development. `ConcreteReserveRespawn` прошёл без предупреждений и ошибок. В обычном solo PIE выполнены две последовательные замены из реальных грязных расшатанных резервов с 25 HP: 100/100 без перка и 115/115 с P25, кофе/repair 0, один расход резерва и прежний PlayerState; состояние осталось стабильным после последующих perk ticks. Сохранены снимки настоящего игрового окна и JSON в `Artifacts/CleanRespawn_20261024`. Проверка использовала временные ResetPerks, выдачу P25 и штатный dev KillSelf; последний приостанавливает посторонние события. Cook/package и сеть не запускались.

## 2026-10-23 — Обновление документации и статический аудит проекта

- Обновлены README, архитектура, GDD/системные контракты, Hub, HUD/инвентарь, загрузка ресурсов и контекст Unreal для агентов по текущему checkout.
- Добавлены единая карта документации, каталог всех файлов Source и карт, обзор контента/арт-пайплайнов/инструментов и отчёт с охватом/ограничениями аудита.
- Исправлены устаревшие описания семидневного маршрута, выбора дня в хабе, XP-прогрессии, урона союзникам, доступности upgrades и first-use readiness. Нативные defaults отделены от непроверенных сохранённых overrides; previews/WIP и исторические PASS не представлены как новая интеграция или проверка.
- Выполнены файловая инвентаризация, сверка source/config, узкое read-only чтение clean загруженных Unreal CDO/DA/Hub-таблиц, проверка ссылок и diff. Зафиксированы BP SingleDay=true при native false, DA_RunRules с 12 начальными зубами при native 8 и отключённые Day02–07. Документационный task не изменял C++, конфигурацию и ассеты; сборки, PIE, Automation, multiplayer, cook/package и refresh индекса не запускались.

Ограничения и отложенные проверки: устаревшие asset snapshots/build state; packaged save/reload после исправления профиля, отдельные first-use interactions, PSO/GPU вопросы и полная gameplay/network/release валидация. Подробности — [ProjectAudit.md](ProjectAudit.md) и сохранённый [checkpoint хаба](HubSystemsCheckpoint_20261023.md).

## 2026-10-12 — Caries boss, phase 2 animation preview

- Added eight separate Blender performances for the asymmetric phase-two boss: awakening/roar, forward hammer, wide backhand, palm shove, infection ground pulse, heavy stagger, exhausted recovery and a supported pivot.
- Added synchronized performances using the existing mouth expression shape keys. The original primary ARP body pose remains frozen; BoneBrush animates the body and huge arm, with the reviewed sole bones preserving foot orientation.
- Preserved the earlier animation actions and the hand-supported walk in the editable derivative. Added a continuous NLA review timeline and pose/event markers for the new clips.
- Added an isolated Blender renderer, compact animation helpers and one complete review video with Russian clip captions. The renderer evaluates real half-frame poses at 60 FPS from the 30 FPS source animation.
- Bundled the reviewed V4 parent rig used by the new authoring script. Historical references to the original user file and phase-three artist source do not need to be present to author or render the new pool.
- Updated AGENTS.md: use short solo PIE checks for gameplay work; run multiplayer tests only on a separate explicit request; include the actual additions/changes and validation in commit and pull request descriptions.

Status: Blender preview for approval. These are newly authored asymmetric performances, not newly downloaded Mixamo clips. Unreal animations, combat hitboxes and Niagara effects are not part of this change.

Validation: key-pose reach and floor/contact checks during authoring, visual review of rendered poses, and verification of the completed video. No Unreal build, cook, package, gameplay or multiplayer run is required for this Blender-only preview.

Review source: `ArtSource/CarriesStage2/CombatPool_v1/Carries_Stage2_CombatPool_v1.blend`.

Video: `Artifacts/CarriesStage2/CombatPool_v1/Carries_Stage2_NewAnimations.mp4`.

To render the saved pool in an isolated Blender process:

```powershell
blender --background --factory-startup --disable-autoexec --python-exit-code 1 --python Tools/Blender/render_carries_stage2_review.py -- --view ThreeQuarter --output-dir Saved/Codex/CarriesStage2NewReview
```

Use `--draft` for event-pose previews. Full rendering requires a fresh output folder. After a completed render, copy `RenderManifest_ThreeQuarter.json` to `RenderManifest.json` in that folder and run:

```powershell
python Tools/BuildCarriesStage2CombatPoolVideo.py --input-dir Saved/Codex/CarriesStage2NewReview
```

The encoder needs Pillow and `ffmpeg`/`ffprobe`; explicit executable paths can be passed with `--ffmpeg` and `--ffprobe`.
