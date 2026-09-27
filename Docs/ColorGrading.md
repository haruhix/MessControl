# Цветокор по референсу

В `L_Mouth` добавлен Post Process Volume **LOOK | Coral Reference**,
папка Outliner — `Lighting/PostProcess`. Пресет приближает цвет сцены к
[референсу](../ArtSource/References/Mouth_Coop_Reference.png): светлее коралловые
полутона, немного теплее светлые участки и аккуратнее свечение бликов.

## Настройки

| Параметр | Значение |
| --- | --- |
| Infinite Extent (Unbound) | Включён |
| Priority / Blend Weight | 50 / 1 |
| Exposure Min / Max | 1 / 1 — фиксированная экспозиция |
| Exposure Compensation | 0.85 |
| Temperature | 6750 K |
| Global Saturation / Contrast | 1.02 / 1.025 |
| Midtones Gamma / Saturation | 1.055 / 1.035 |
| Bloom Intensity / Threshold | 0.18 / 1.2 |
| Vignette | 0.08 |
| Chromatic Aberration, Lens Flare, Film Grain, Motion Blur | 0 |
| Depth of Field | Выключен |

Все выставленные значения и флаги override сохранены в
[CoralReference.json](../ArtSource/Looks/CoralReference.json). Это снимок свойств
для `ObjectTools.set_properties` официального Unreal MCP; игра читает настройки
из сохранённого актора карты, а не из JSON.

Чтобы уменьшить эффект, снизь **Blend Weight**. Значение 0 или отключение
**Enabled** возвращает обработку исходного Post Process Volume. Исходный volume
сохранён; его настройки отражений продолжают работать там, где он применяется.

Цветокор проверен в Play. Скриншот:
[результат](../Artifacts/ColorGrade_After.png).
Форма моделей, материалы и свет определяют оставшуюся разницу с референсом.

## Официальный MCP Epic

В проекте включены `ModelContextProtocol` и `AllToolsets` для редактора UE 5.8.1.
Сервер использует `http://127.0.0.1:8000/mcp`. Локальная настройка **Auto Start
Server** включена в Editor Preferences → Model Context Protocol.

В конфигурации Codex подключение называется `unreal_epic`. Запуск сервера
ChiR24 удалён из активной конфигурации; предыдущий файл сохранён в резервной копии.
Если текущая сессия Codex ещё показывает старые инструменты, переподключи MCP
или перезапусти Codex, чтобы он перечитал конфигурацию.

Проверены MCP initialize, список 52 наборов инструментов, чтение `L_Mouth`,
редактирование свойств, запуск/остановка Play, снимки и сохранение карты.
Инструкция Epic: [Unreal MCP](https://dev.epicgames.com/documentation/unreal-engine/unreal-mcp-in-unreal-editor).
