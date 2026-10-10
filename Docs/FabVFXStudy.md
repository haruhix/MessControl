# Бесплатные Niagara-паки для MessControl

В проект уже добавлены пять паков: 104 Niagara System по каталогу сохранённых пакетов. В `L_VFXLab` выставлены восемь образцов; остальные варианты доступны в Content Browser и собственных overview-картах паков.

| Пак | Папка / систем | Что разбирать для наших эффектов |
| --- | --- | --- |
| [Niagara Examples Pack — Epic Games](https://www.fab.com/listings/0e188eca-4e54-4fb2-a9ed-d8b8a565e600) | `/Game/NiagaraExamples` / 59 | Попадания, следы, пузырьки жидкости, статусы, параметры качества и Niagara Data Channels. |
| [Free Magic Niagara — Lord Enot Store](https://www.fab.com/listings/d0fe50c4-6ebe-40d5-b78a-56960832f49e) | `/Game/Free_Magic` / 17 | Круги каста, ауры, ударные кольца, многослойные эффекты босса. |
| [Free Niagara Particles — SoftTofuVFX](https://www.fab.com/listings/183732bc-c2fb-465c-9453-f70a1ce7ba2c) | `/Game/FreeParticle_SoftTofu` / 14 | Движение листьев/перьев, свечение и частицы для снегопада, вихря, очистки и заморозки. |
| [Realistic Fire & Explosion VFX Vol.1 — FREE Starter Pack](https://www.fab.com/listings/2adf4e4e-5f1d-4543-9e8c-192fe3c9a8ed) | `/Game/Fire_EXP_Vol01_Free` / 10 | Постоянный огонь, короткие вспышки, взрывы и настройка размера/цвета через User Parameters. |
| [Stylish Fire VFX — VfxSTOCK](https://www.fab.com/listings/01e8534c-5877-4ce2-8948-9a696100de11) | `/Game/Stylish_Fire_VFX` / 4 | Стилизованное пламя и управление свечением слоёв. |

Страницы Fab проверены 10 октября 2026. Количество систем взято из фактически импортированного содержимого, а назначение в последнем столбце — план разбора для MessControl. Исходные системы и материалы паков сохранены без изменений. Атрибуция SoftTofu CC BY 4.0 находится в `Docs/ThirdPartyVFXCredits.txt`.

Рабочий порядок: открыть образец в Niagara Editor, проверить emitters/materials/User Parameters, сделать отдельную копию под `/Game/Gameplay/VFX`, привязать её к реальному игровому событию, затем сравнить в полигоне при освещении арены. Это разбор и авторинг внутри проекта; веса модели не переобучаются.

Первые применения: круг и восстановление после каста ореха, разрушение щита, спираль падающего льда, кольцо заморозки ног, контакт щётки с маской грязи, искры перфоратора и всплеск волны кофе.
