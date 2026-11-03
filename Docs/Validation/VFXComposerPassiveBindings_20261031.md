# Пассивки: native-привязки Composer, 31.10.2026

Для всех 27 текущих пассивок подключены source/event-контракты, общий presentation-компонент и canonical DT binding. Проверены 27 отдельных Acquire и 27 Target-сценариев через публичные native методы, которыми пользуется UI Composer. Это техническое состояние; новые эффекты и визуальное качество не приняты.

**Фактический набор:** Acquire 27 — Build05b; Target 27 с полными MP4 — 16 из Build05b и 11 из Build06. Это объединение уникальных sourceID, а не один полный повтор на 06. Глобальная направленная automation-подборка06:45/45, 0 ошибок; два предупреждения CPU-access Plane в borrowed-Lab fixture. Ранее 05b: 38/39 с ошибкой read-only fixture; исправленный тест вошёл в 06.

Источники и SHA, точные события, границы проверок: [машинный отчёт](VFXComposerPassiveBindings_20261031.json). Полные оригинальные отчёты, MP4 и Master27MediaIndex.json остаются в E:/CodexWork/MessControl/Validation/PassiveMigration_20261031; в Git медиа не добавляются.

| SourceID | События в выбранном Target | Build | Видео |
|---|---|---|---|
| `B1_HeavyHit` · Тяжёлый удар | Hit | 05b | [MP4](E:/CodexWork/MessControl/Validation/PassiveMigration_20261031/WorkspaceRun_20261031T104000Z_PID34724/00_B1_HeavyHit_Target.mp4) |
| `B1_LightFoot` · Лёгкая походка | числовое движение | 05b | [MP4](E:/CodexWork/MessControl/Validation/PassiveMigration_20261031/WorkspaceRun_20261031T104000Z_PID34724/01_B1_LightFoot_Target.mp4) |
| `B1_DeepBreath` · Глубокий вдох | числовое движение | 05b | [MP4](E:/CodexWork/MessControl/Validation/PassiveMigration_20261031/WorkspaceRun_20261031T104000Z_PID34724/02_B1_DeepBreath_Target.mp4) |
| `B1_BigPortion` · Большая порция | Pickup | 05b | [MP4](E:/CodexWork/MessControl/Validation/PassiveMigration_20261031/WorkspaceRun_20261031T104000Z_PID34724/03_B1_BigPortion_Target.mp4) |
| `B1_ConfidentThrow` · Уверенный бросок | Throw | 05b | [MP4](E:/CodexWork/MessControl/Validation/PassiveMigration_20261031/WorkspaceRun_20261031T104000Z_PID34724/04_B1_ConfidentThrow_Target.mp4) |
| `B1_FreshDelivery` · Свежая доставка | CarryEnd, CarryStart | 05b | [MP4](E:/CodexWork/MessControl/Validation/PassiveMigration_20261031/WorkspaceRun_20261031T104000Z_PID34724/05_B1_FreshDelivery_Target.mp4) |
| `B1_GreedyHands` · Руки загребущие | Pickup | 05b | [MP4](E:/CodexWork/MessControl/Validation/PassiveMigration_20261031/WorkspaceRun_20261031T104000Z_PID34724/06_B1_GreedyHands_Target.mp4) |
| `B1_WideBristles` · Широкая щетина | Contact | 05b | [MP4](E:/CodexWork/MessControl/Validation/PassiveMigration_20261031/WorkspaceRun_20261031T104000Z_PID34724/07_B1_WideBristles_Target.mp4) |
| `B1_UnderPressure` · Под давлением | SprayEnd, SprayStart | 05b | [MP4](E:/CodexWork/MessControl/Validation/PassiveMigration_20261031/WorkspaceRun_20261031T104000Z_PID34724/08_B1_UnderPressure_Target.mp4) |
| `B2_ReturnTrip` · Обратный рейс | End, Proc | 05b | [MP4](E:/CodexWork/MessControl/Validation/PassiveMigration_20261031/WorkspaceRun_20261031T104000Z_PID34724/09_B2_ReturnTrip_Target.mp4) |
| `B2_CleanBreath` · Чистое дыхание | Proc | 05b | [MP4](E:/CodexWork/MessControl/Validation/PassiveMigration_20261031/WorkspaceRun_20261031T104000Z_PID34724/10_B2_CleanBreath_Target.mp4) |
| `B2_NeatFreak` · Чистюля | native activity; Proc/Link не наблюдался | 06 | [MP4](E:/CodexWork/MessControl/Validation/PassiveMigration_20261031/WorkspaceRun_20261031T110837Z_PID58136/00_B2_NeatFreak_Target.mp4) |
| `B2_LastStroke` · Последний штрих | FinishContact | 05b | [MP4](E:/CodexWork/MessControl/Validation/PassiveMigration_20261031/WorkspaceRun_20261031T104000Z_PID34724/12_B2_LastStroke_Target.mp4) |
| `B2_CleanAcceleration` · Чистый разгон | End, Proc | 05b | [MP4](E:/CodexWork/MessControl/Validation/PassiveMigration_20261031/WorkspaceRun_20261031T104000Z_PID34724/13_B2_CleanAcceleration_Target.mp4) |
| `B2_RefreshingWork` · Освежающий труд | Proc | 05b | [MP4](E:/CodexWork/MessControl/Validation/PassiveMigration_20261031/WorkspaceRun_20261031T104000Z_PID34724/14_B2_RefreshingWork_Target.mp4) |
| `B2_ThickEnamel` · Толстая эмаль | native activity; Proc/Link не наблюдался | 05b | [MP4](E:/CodexWork/MessControl/Validation/PassiveMigration_20261031/WorkspaceRun_20261031T104000Z_PID34724/15_B2_ThickEnamel_Target.mp4) |
| `B2_Shield` · Щит | Proc | 05b | [MP4](E:/CodexWork/MessControl/Validation/PassiveMigration_20261031/WorkspaceRun_20261031T104000Z_PID34724/16_B2_Shield_Target.mp4) |
| `B2_SecondWind` · Второе дыхание | Proc | 06 | [MP4](E:/CodexWork/MessControl/Validation/PassiveMigration_20261031/WorkspaceRun_20261031T110837Z_PID58136/01_B2_SecondWind_Target.mp4) |
| `B2_HoudiniDash` · Рывок Гудини | Return, Vanish | 06 | [MP4](E:/CodexWork/MessControl/Validation/PassiveMigration_20261031/WorkspaceRun_20261031T110837Z_PID58136/02_B2_HoudiniDash_Target.mp4) |
| `B2_MightyTooth` · Могучий зуб | Cleave | 06 | [MP4](E:/CodexWork/MessControl/Validation/PassiveMigration_20261031/WorkspaceRun_20261031T110837Z_PID58136/03_B2_MightyTooth_Target.mp4) |
| `B2_StartFromDash` · С места в карьер | Proc | 06 | [MP4](E:/CodexWork/MessControl/Validation/PassiveMigration_20261031/WorkspaceRun_20261031T110837Z_PID58136/04_B2_StartFromDash_Target.mp4) |
| `B2_ThunderGod` · Бог грома | native activity; Proc/Link не наблюдался | 06 | [MP4](E:/CodexWork/MessControl/Validation/PassiveMigration_20261031/WorkspaceRun_20261031T110837Z_PID58136/05_B2_ThunderGod_Target.mp4) |
| `B2_EchoSwing` · Эхо замаха | Echo, Windup | 06 | [MP4](E:/CodexWork/MessControl/Validation/PassiveMigration_20261031/WorkspaceRun_20261031T110837Z_PID58136/06_B2_EchoSwing_Target.mp4) |
| `B2_Billiards` · Бильярд | Impact, Launch | 06 | [MP4](E:/CodexWork/MessControl/Validation/PassiveMigration_20261031/WorkspaceRun_20261031T110837Z_PID58136/07_B2_Billiards_Target.mp4) |
| `B2_Softener` · Размягчитель | Coat, End | 06 | [MP4](E:/CodexWork/MessControl/Validation/PassiveMigration_20261031/WorkspaceRun_20261031T110837Z_PID58136/08_B2_Softener_Target.mp4) |
| `B2_Antiseptic` · Антисептик | Clean, SprayEnd, SprayStart | 06 | [MP4](E:/CodexWork/MessControl/Validation/PassiveMigration_20261031/WorkspaceRun_20261031T110837Z_PID58136/09_B2_Antiseptic_Target.mp4) |
| `B2_LastingFormula` · Стойкая формула | Coat, Refresh | 06 | [MP4](E:/CodexWork/MessControl/Validation/PassiveMigration_20261031/WorkspaceRun_20261031T110837Z_PID58136/10_B2_LastingFormula_Target.mp4) |

## Что подтверждено

- Настоящая выдача после resource barrier, отдельные SourceID и причинные source/event receipts; native gameplay-сценарии без прямого искусственного PlayVFX.
- Все выбранные клипы: native lifetime завершён, cleanup принят, первый кадр до Play, корректный видео-контейнер без аудио. Для всех трёх исходных запусков защищённые packages, save-файлы и dirty sets остались неизменны.
- Source поддерживает editable passive profiles; native automation проверяет контракты и направленные invariants. Подборка 27 не является проверкой сохранения/применения нового профиля отдельно для каждой пассивки.

## Что остаётся открытым

- Enamel/Thunder/Neat: в выбранных клипах случайные Proc/Link не выпали; вероятность и визуал этих событий не приняты. У Neat доказано уменьшение маски, но не записан CoffeeLeft/completion probe; не утверждаем достигнутый neighbor-roll.
- LastingFormula: наблюдались Coat/Refresh, но не End/Damage. Опасная видимая плёнка и gameplay actor сохранены; редактируемые акценты не означают перенос всего procedural renderer.
- Художественная проверка каждого клипа, профили всех 27 в Custom/Disabled и их Save→Apply→reopen, первое холодное появление/PSO, network/package/performance, полный бой и остальные boss/map contexts.
- Видеозаписи получены через Windows Graphics Capture реального Composer; контейнер и покрытие времени проверены. Публичные native вызовы используют UI route, но54 сценария не являются54 ручными кликами по UI.

Все старые диагностические отчёты, включая recorder failure и остановку при смене пользователем контекста, сохранены. ReviewBoard publication этой подборки ожидает отдельного разрешения root; человеческие отзывы не изменялись.

Основные source-контракты: `Source/MessControl/Private/MCPerkPresentationEvents.cpp`; `Source/MessControlEditor/Private/MCPerkPreviewPassiveAdapters.cpp`; `Source/MessControlEditor/Private/MCVFXProfileBinding.cpp`; реальные владельцы результата — `MCResultPassives`, `MCDefenseSprayPassives`, `MCCombatPassiveSubsystem`, inventory/food/movement.
