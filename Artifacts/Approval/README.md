# Видео для проверки — 30 сентября 2026

Записи настоящего игрового render target. Сценарии выполняют действия программно; совместный проход четырьмя людьми пока не проверен.

Сборки Editor и Game успешны. 64 автоматических теста: 59 Success, 5 SuccessWithWarnings, 0 ошибок.

Галерея: [index.html](index.html). Данные: [Manifest.json](Manifest.json), [Automation_Summary.json](Automation_Summary.json).

| Пункт | Видео | Проверка |
| --- | --- | --- |
| Материалы ротовой области | [21.27 с](Materials.mp4) | [лог](Materials_Validation.txt) |
| Лазание по игровому зубу | [10.53 с](Climb.mp4) | [лог](Climb_Validation.txt) |
| Кофе и управляемое плавание | [14.2 с](Coffee.mp4) | [лог](Coffee_Validation.txt) |
| Следящая камера и передняя граница | [15.23 с](Camera.mp4) | [лог](Camera_Validation.txt) |
| Кирка рядом с поверхностями | [12.13 с](Pickaxe.mp4) | [лог](Pickaxe_Validation.txt) |
| Холодная кола, иней и лёд | [32.6 с](Cola.mp4) | [лог](Cola_Validation.txt) |
| Чистка и направление персонажа | [21.23 с](Brush.mp4) | [лог](Brush_Validation.txt) |
| Четыре слота и работа инструментов | [18.63 с](Tools.mp4) | [лог](Tools_Validation.txt) |
| Еда впитывается → язва → лечение | [16.0 с](Ulcer.mp4) | [лог](Ulcer_Validation.txt) |
| Захват, перенос, тяга и толкание | [27.13 с](Grip.mp4) | [лог](Grip_Validation.txt) |
| Функциональная увула и проглатывание | [13.0 с](Uvula.mp4) | [лог](Throat_Validation.txt) |
| Рвота и возврат партии | [13.1 с](Vomit.mp4) | [лог](Throat_Validation.txt) |
| Дыхание рта | [5.1 с](Breathing.mp4) | [лог](Materials_Validation.txt) |
| Прыжок через волну язвы | [1.83 с](Dodge.mp4) | [лог](Hazards_Validation.txt) |
| Перец: предупреждение и детонация | [8.97 с](Pepper.mp4) | [лог](Hazards_Validation.txt) |
| Эмоции портрета игрока | [7.93 с](Portraits.mp4) | [лог](Tools_Validation.txt) |

У нескольких записей сохранён предыдущий цвет ткани; это отмечено в галерее. Актуальный визуальный проход — Materials, Climb, Cola, Brush, Tools и Grip.

Painter: `ArtSource/MouthV4/Painter`; карты: `ArtSource/MouthV4/Textures`; создание материалов: `Tools/Unreal/refine_mouth_v4.py`.

Воспроизведение проверок: `Tools/CaptureApproval.ps1 -Case <пункт>`; сборка галереи: `C:/Python314/python.exe Tools/build_approval_gallery.py`.
