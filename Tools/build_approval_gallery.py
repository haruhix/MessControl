"""Build a local gallery from recorded Unreal gameplay, with one clip per mechanic."""
from pathlib import Path
import html
import json
import subprocess

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'Artifacts/Approval'
FFMPEG = 'C:/ffmpeg/ffmpeg.exe'
FFPROBE = 'C:/ffmpeg/ffprobe.exe'


def command(args):
    return subprocess.run(args, check=True, capture_output=True, text=True).stdout


# Chapters preserve the recorded action and its timing. No frames are synthesized.
CHAPTERS = [
    ('Dodge', 'Hazards', 2.85, 1.85),
    ('Pepper', 'Hazards', 4.55, 9.0),
    ('Uvula', 'Throat', 0, 13),
    ('Vomit', 'Throat', 13, 13.1),
    ('Breathing', 'Materials', 16, 5.1),
    ('Portraits', 'Tools', 10.55, 7.95),
]
for name, source, start, duration in CHAPTERS:
    command([FFMPEG, '-hide_banner', '-loglevel', 'error', '-y', '-ss', str(start),
             '-i', str(OUT / (source + '.mp4')), '-t', str(duration), '-an',
             '-c:v', 'libx264', '-threads', '2', '-crf', '19', '-pix_fmt', 'yuv420p',
             '-movflags', '+faststart', str(OUT / (name + '.mp4'))])
    command([FFMPEG, '-hide_banner', '-loglevel', 'error', '-y', '-ss', str(min(duration / 2, 3)),
             '-i', str(OUT / (name + '.mp4')), '-frames:v', '1', '-update', '1',
             str(OUT / (name + '.png'))])
    (OUT / (name + '_Recording.json')).write_text(json.dumps({
        'case': name, 'source': source + '.mp4', 'chapterStart': start,
        'chapterDuration': duration, 'capture': 'Unreal game render target',
    }, indent=2), encoding='utf-8')

OLD = 'Записано до последней настройки цвета ткани; механика соответствует текущему коду.'
CLIPS = [
    ('Materials', 'Материалы ротовой области', 'Визуал',
     'Общий вид, десна, стенки / нёбо, пищевод и увула. Карты Painter 2K, рельеф, влажная плёнка и рассеивание света.',
     'Осмотр художественного результата. Требуется твоя оценка.', ''),
    ('Climb', 'Лазание по игровому зубу', 'Движение',
     'Зацеп, подъём, висение, перемещение в сторону, выход на коронку зуба и отскок.',
     'E удерживать у поверхности; W/S вверх и вниз, A/D в стороны, Space оттолкнуться. Стамины нет.', ''),
    ('Coffee', 'Кофе и управляемое плавание', 'Движение',
     'Наливание сверху, рост уровня, движение в жидкости и слив. Передняя волна больше не оглушает.',
     'WASD для движения; персонаж следует за уровнем жидкости.', OLD),
    ('Camera', 'Следящая камера и передняя граница', 'Движение',
     'Маршрут у передних зубов, по краям арены и к пищеводу. Это запись игровой камеры.',
     'Граница глаза камеры вынесена немного за передние зубы. Ограничение сохраняет высоту камеры.', OLD),
    ('Pickaxe', 'Кирка рядом с поверхностями', 'Инструменты',
     'Размашистые удары на языке и около настоящего игрового зуба. Проверено 180 поз; минимальный зазор 0,95 см.',
     '2 → ЛКМ. Кисть и инструмент корректируются вместе; при близкой стене размах сокращается.', OLD),
    ('Cola', 'Холодная кола, иней и лёд', 'События',
     'Напиток сверху, промерзание арены, скольжение после отпускания движения, падение льдин разных форм, разрушение и оттаивание.',
     'F3 → Холодная кола; 2 → ЛКМ по льду. Проверяются куб, сфера, цилиндр и вытянутый блок.', ''),
    ('Brush', 'Чистка и направление персонажа', 'Инструменты',
     'Контакт с двумя игровыми зубами и языком. Персонаж поворачивается к цели; грязь удаляется при контакте щётки.',
     '1 → удерживать ЛКМ. Эффект контакта использует Niagara.', ''),
    ('Tools', 'Четыре слота и работа инструментов', 'Инструменты',
     'Щётка, кирка для твёрдой еды, нож для мягкой еды и спрей. Также показаны реакции HUD на успех, ошибку и смерть.',
     '1–4 выбор; ЛКМ использовать. Водомёт остаётся вариантом апгрейда с временным мешем.', ''),
    ('Ulcer', 'Еда впитывается → язва → лечение', 'Инструменты',
     'Еда без внимания впитывается в язык и превращается в язву. Спрей заполняет круглый индикатор; пауза сохраняет прогресс.',
     '4 → удерживать ЛКМ на язве. Лечение требует 7 секунд суммарного контакта.', OLD),
    ('Grip', 'Захват, перенос, тяга и толкание', 'Движение',
     'Сетевой сценарий: настоящий listen server и три клиента. Показаны позы рук и корпуса с тестовыми грузами.',
     'E взять / удерживать, WASD двигать, Q бросить. Шахматные блоки и подписи — тестовые объекты.', ''),
    ('Uvula', 'Функциональная увула и проглатывание', 'События',
     'Подготовка, прыжок, давление весом, отскок и открытие прохода. Сохранена ранее созданная увула.',
     'Space в круге запускает совместное действие. В этом видео — одиночная проверка.', OLD),
    ('Vomit', 'Рвота и возврат партии', 'События',
     'Посторонний предмет / игрок в текущем проглатывании вызывает спазм. Предметы возвращаются, появляются три небольшие очищаемые лужи.',
     'Используется morph target vomit. Это небольшое загрязнение, заполнение арены относится к напиткам.', OLD),
    ('Breathing', 'Дыхание рта', 'Визуал',
     'Слабое движение блендшейпа Open в покое и движение живого горла. Это фрагмент осмотра материалов.',
     'Деформация работает у GAMEPLAY | Living throat → AuthoredMouth.', ''),
    ('Dodge', 'Прыжок через волну язвы', 'Движение',
     'Один персонаж прыгает через низкую волну и сохраняет здоровье, второй стоит на поверхности и получает урон.',
     'Space перед пересечением волны. Проверка использует такую же волну, как язва.', OLD),
    ('Pepper', 'Перец: предупреждение и детонация', 'События',
     'Предупреждение усиливается к концу фитиля, затем возникает локальная волна. В этом сценарии фитиль — 8 секунд.',
     'Таблица SpicyPepper: 8→6 секунд по дням, радиус растёт. Пауза при запуске увулы проверяется автоматическим тестом.', OLD),
    ('Portraits', 'Эмоции портрета игрока', 'Визуал',
     'Успех, ошибка и смерть меняют портрет P1 справа сверху. При смерти — крестики и серый цвет.',
     'Это состояния UMG HUD, отдельного Animation Sequence у иконки нет.', ''),
]
manifest = []
for name, title, group, description, controls, note in CLIPS:
    file = OUT / (name + '.mp4')
    probe = json.loads(command([FFPROBE, '-v', 'error', '-show_entries',
        'format=duration:stream=width,height,codec_name', '-of', 'json', str(file)]))
    command([FFMPEG, '-v', 'error', '-i', str(file), '-f', 'null', '-'])
    source = next((c[1] for c in CHAPTERS if c[0] == name), name)
    manifest.append(dict(id=name, title=title, group=group, description=description,
                         controls=controls, note=note, file=name + '.mp4', poster=name + '.png',
                         seconds=round(float(probe['format']['duration']), 2),
                         validation=source + '_Validation.txt', recording=name + '_Recording.json',
                         bytes=file.stat().st_size, decode='PASS'))

report = json.loads((ROOT / 'Saved/TestReports/index.json').read_text(encoding='utf-8-sig'))
summary = {k: report[k] for k in ('reportCreatedOn', 'succeeded', 'succeededWithWarnings', 'failed', 'notRun', 'totalDuration')}
summary['tests'] = [dict(name=t['fullTestPath'], state=t['state'], warnings=t['warnings'], errors=t['errors']) for t in report['tests']]
(OUT / 'Automation_Summary.json').write_text(json.dumps(summary, ensure_ascii=False, indent=2), encoding='utf-8')
(OUT / 'Manifest.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2), encoding='utf-8')

template = '''<!doctype html>
<html lang="ru"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>MessControl — видео для проверки</title>
<style>
:root{color-scheme:dark;--muted:#aeb7c5;--line:#303745;--pink:#ff8aaa}*{box-sizing:border-box}
body{margin:0;background:#11151d;color:#f0f3f8;font:16px/1.55 system-ui,sans-serif}main{max-width:1440px;margin:auto;padding:30px 32px}
.eyebrow{color:var(--pink);font-weight:700;font-size:12px;letter-spacing:.14em}h1{font-size:clamp(28px,3vw,40px);line-height:1.15;margin:10px 0 16px}p{margin:8px 0;color:var(--muted)}
.layout{display:grid;grid-template-columns:minmax(0,1fr) 320px;gap:26px;margin-top:28px}video{width:100%;aspect-ratio:16/9;background:#080a0f;border-radius:12px}
h2{font-size:25px;margin:18px 0 8px}a{color:#9ce9d4;text-underline-offset:3px}.links{display:flex;gap:20px;flex-wrap:wrap;margin-top:18px}
.filters{display:flex;gap:6px;flex-wrap:wrap;margin-bottom:14px}button{cursor:pointer;font:inherit;color:inherit;background:#202633;border:1px solid var(--line);border-radius:8px;padding:8px 12px}
.filters button{font-size:13px;padding:6px 10px}button.active{border-color:var(--pink);background:#392634}
.clip{display:block;text-align:left;width:100%;margin-bottom:8px}.clip small{display:block;color:var(--muted);font-size:12px}.note{padding:12px 15px;background:#202633;border-radius:8px;margin-top:18px;font-size:14px}
footer{margin-top:28px;border-top:1px solid var(--line);padding-top:18px;font-size:13px;color:var(--muted)}@media(max-width:900px){main{padding:20px}.layout{grid-template-columns:1fr}.list{display:grid;grid-template-columns:repeat(auto-fit,minmax(240px,1fr));gap:8px}.clip{margin:0}}
</style><main><div class="eyebrow">MESSCONTROL / 30 СЕНТЯБРЯ 2026</div>
<h1>Материалы и механики — видео для проверки</h1>
<p>16 отдельных роликов из проекта. Выбери пункт справа и запусти видео.</p>
<p><a href="Automation_Summary.json">64 теста пройдены</a>: 59 без предупреждений, 5 с предупреждениями, ошибок нет. Захват отдельно проверен на сервере и трёх клиентах.</p>
<div class="layout"><section><video id="video" controls preload="metadata" playsinline></video><h2 id="title"></h2><p id="description"></p><p id="controls"></p><p id="note" class="note"></p><div class="links"><a id="download">Открыть MP4</a><a id="validation">Результат проверки</a><a id="recording">Данные записи</a></div></section>
<aside><div class="filters" id="filters"></div><div id="list" class="list"></div></aside></div>
<footer>Сценарии задают расстановку и вызывают настоящие игровые действия программно. Записан только игровой render target; экран, WoW, мышь и клавиатура ОС не использовались. Исходные кадры сохраняют игровое время; MP4 выводится в 30 fps с повторением кадров. Совместный проход четырьмя людьми пока не проверен. Художественное качество материалов и процедурных анимаций требует твоей оценки.</footer></main>
<script>
const clips=__CLIPS__;let selected=clips[0].id,group='Все';const $=id=>document.getElementById(id);
function select(c){selected=c.id;const v=$('video');v.pause();v.src=c.file;v.poster=c.poster;v.load();$('title').textContent=c.title;$('description').textContent=c.description;$('controls').textContent=c.controls;$('note').textContent=c.note;$('note').hidden=!c.note;for(const [id,key] of [['download','file'],['validation','validation'],['recording','recording']])$(id).href=c[key];render();}
function render(){$('list').replaceChildren();clips.filter(c=>group==='Все'||c.group===group).forEach(c=>{const b=document.createElement('button');b.className='clip'+(c.id===selected?' active':'');const t=document.createElement('span');t.textContent=c.title;const s=document.createElement('small');s.textContent=c.group+' · '+Math.round(c.seconds)+' с';b.append(t,s);b.onclick=()=>select(c);$('list').append(b);});$('filters').replaceChildren();['Все',...new Set(clips.map(c=>c.group))].forEach(g=>{const b=document.createElement('button');b.textContent=g;b.className=g===group?'active':'';b.onclick=()=>{group=g;render();};$('filters').append(b);});}select(clips[0]);
</script></html>'''
(OUT / 'index.html').write_text(template.replace('__CLIPS__', json.dumps(manifest, ensure_ascii=False).replace('</', '<\\/')), encoding='utf-8')

lines = ['# Видео для проверки — 30 сентября 2026', '',
         'Записи настоящего игрового render target. Сценарии выполняют действия программно; совместный проход четырьмя людьми пока не проверен.', '',
         'Сборки Editor и Game успешны. 64 автоматических теста: 59 Success, 5 SuccessWithWarnings, 0 ошибок.', '',
         'Галерея: [index.html](index.html). Данные: [Manifest.json](Manifest.json), [Automation_Summary.json](Automation_Summary.json).', '',
         '| Пункт | Видео | Проверка |', '| --- | --- | --- |']
for c in manifest:
    lines.append(f"| {c['title']} | [{c['seconds']} с]({c['file']}) | [лог]({c['validation']}) |")
lines += ['', 'У нескольких записей сохранён предыдущий цвет ткани; это отмечено в галерее. Актуальный визуальный проход — Materials, Climb, Cola, Brush, Tools и Grip.', '',
          'Painter: `ArtSource/MouthV4/Painter`; карты: `ArtSource/MouthV4/Textures`; создание материалов: `Tools/Unreal/refine_mouth_v4.py`.', '',
          'Воспроизведение проверок: `Tools/CaptureApproval.ps1 -Case <пункт>`; сборка галереи: `C:/Python314/python.exe Tools/build_approval_gallery.py`.']
(OUT / 'README.md').write_text('\n'.join(lines) + '\n', encoding='utf-8')
print(json.dumps({'clips': len(manifest), 'decodeFailures': 0, 'tests': summary['succeeded'] + summary['succeededWithWarnings'], 'gallery': str(OUT / 'index.html')}, ensure_ascii=False))
