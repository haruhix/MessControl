# Composer: проверка спавна Rocket Impact

31.10.2026, UE 5.8.1, Editor Build11. Статус: **спавн подтверждён; визуальная эквивалентность и художественная приёмка не подтверждены**.

Проверены исходная `NS_Rocket_Impact` и экспортированная `NS_Rocket_ComposerContext08`, содержащая два совпадающих слоя исходного эффекта. Родительский агент выполнил один и тот же native-сценарий ракетницы `Target` и собрал снимки настоящих Niagara-компонентов; это не отдельный ручной спавн VFX в обход действия.

| Наблюдение | Исходник | Экспорт двух слоёв |
|---|---|---|
| Пиковое число CPU-частиц | 18: `[9, 1, 8]` | 36: `[9, 1, 8, 9, 1, 8]` |
| Компонент | Active, Visible, Registered | Active, Visible, Registered |
| Позиция, см | примерно `(880, 0.125, 123.370)` | та же в пределах точности float |
| Scale | `(1, 1, 1)` | `(1, 1, 1)` |
| Runtime User inputs | Radius=400, Duration≈0.9, Intensity=1 | те же значения |

Количество `impact_components_seen` в отчёте — число наблюдений в снимках, **не** число уникальных компонентов. Разница 18 → 36 ожидаема для двух одновременно совпадающих слоёв.

Ранее read-only Niagara MCP сравнил пять User defaults, SystemState lifecycle и resolved module inputs первой тройки экспортированных эмиттеров: различий не найдено. Эмиттеры включены, CPU, world-space; экспортированные bounds не меньше исходных. Native-код `SyncAliases` переименовывает Rapid Iteration aliases при изменении имени эмиттера. Build11 дополнительно опровергает гипотезу полной потери burst-спавна из-за переименования: все шесть эмиттеров действительно создают ожидаемые частицы.

Это не доказательство полной compiled/runtime parity. Счётчики не проверяют положения отдельных частиц, renderer bindings, материал, альфу на выходе шейдера, попадание в кадр, lifetime-профиль, стоимость прозрачности или читаемость. На диагностическом контактном листе Build09 Impact визуально не читается. Из этого изображения нельзя достоверно установить причину в renderer/material; их прямое source/generated-сравнение тогда прервалось из-за потери HTTP-соединения MCP. В Build11 не следует объявлять эту визуальную проблему исправленной только по счётчикам.

Следующая визуальная проверка — синхронные кадры исходника и экспорта из одинаковой камеры в момент Impact и его хвоста. Если различие сохранится, нужна проверка renderer/material/bindings и видимых данных частиц на этих же экземплярах. Художественная оценка, gameplay camera, loading/prewarm, cold packaged PSO и multiplayer этой проверкой не закрыты.

Свидетельства хранятся локально, вне Git:

- `E:/CodexWork/MessControl/Validation/PerkActionPreview_20261031/ParticleFidelity11.json`
- `E:/CodexWork/MessControl/Validation/PerkActionPreview_20261031/ParticleFidelity11.frames.json`
- `E:/CodexWork/MessControl/Validation/PerkActionPreview_20261031/compare_particles11.py`
- `E:/CodexWork/MessControl/Validation/PerkActionPreview_20261031/Fidelity09_ReadOnly.json`
- `E:/CodexWork/MessControl/Validation/PerkActionPreview_20261031/ComposerImpact09_contact.png`

При подготовке этого заключения повторные сборки, UE-команды и изменения исходного кода/ассетов не выполнялись.
