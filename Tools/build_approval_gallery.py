"""Build a local gallery from recorded Unreal gameplay, with one clip per mechanic."""
from pathlib import Path
import html
import json
import re
import subprocess
import hashlib
from datetime import datetime, timezone, timedelta

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'Artifacts/Approval'
FFMPEG = 'C:/ffmpeg/ffmpeg.exe'
FFPROBE = 'C:/ffmpeg/ffprobe.exe'


def command(args):
    return subprocess.run(args, check=True, capture_output=True, text=True).stdout


def file_sha256(path):
    with path.open('rb') as handle:
        return hashlib.file_digest(handle, 'sha256').hexdigest()


def tree_digest(root, directory, saved_packages=False):
    """Same portable path+file-hash protocol as CaptureApproval.ps1."""
    files = sorted((p.relative_to(root).as_posix(), p) for p in (root / directory).rglob('*') if p.is_file())
    digest = hashlib.sha256()
    count = size = 0
    for relative, path in files:
        if saved_packages and (path.suffix.lower() not in ('.uasset', '.umap') or relative.startswith('Content/_CodexMapMerge/')):
            continue
        digest.update((relative + '\0' + file_sha256(path) + '\n').encode('utf-8'))
        count += 1
        size += path.stat().st_size
    return dict(algorithm='sha256-tree-v1', root=directory, fileCount=count, bytes=size, sha256=digest.hexdigest())


def validation_result(source):
    """Report only the recorded run's markers, including failed assertions."""
    path = OUT / (source + '_Validation.txt')
    if not path.exists():
        return 'UNKNOWN', ''
    log = path.read_text(encoding='utf-8-sig')
    state = 'UNKNOWN'
    if re.search(r'MC_\w+_FAIL\b|MC_\w+_CHECK FAIL\b', log):
        state = 'FAIL'
    elif re.search(r'MC_\w+_PASS\b', log):
        state = 'PASS'
    timestamps = re.findall(r'\[(\d{4}\.\d{2}\.\d{2}-\d{2}\.\d{2}\.\d{2})', log)
    return state, timestamps[-1] if timestamps else ''


def local_timestamp(raw):
    if not raw:
        return ''
    try:
        stamp = datetime.fromisoformat(raw.replace('Z', '+00:00'))
    except ValueError:
        stamp = datetime.strptime(raw, '%Y.%m.%d-%H.%M.%S').replace(tzinfo=timezone.utc)
    return stamp.astimezone(timezone(timedelta(hours=9))).strftime('%Y-%m-%d %H:%M:%S UTC+9')


def recording_metadata(name):
    path = OUT / (name + '_Recording.json')
    return json.loads(path.read_text(encoding='utf-8-sig')) if path.exists() else {}


def fingerprint_state(recording, workspace, source, name):
    fingerprint = recording.get('captureFingerprint')
    if not fingerprint:
        return 'UNRECORDED', [], None
    if (fingerprint.get('schemaVersion') != 1 or fingerprint.get('case') != source or
            recording.get('case') != name or not fingerprint.get('startedUtc') or
            not fingerprint.get('completedUtc') or fingerprint.get('validation', {}).get('result') != 'PASS'):
        return 'INCOMPLETE', [], False
    validation_file = OUT / (source + '_Validation.txt')
    video_file = OUT / (name + '.mp4')
    binding = bool(validation_file.exists() and fingerprint.get('validation', {}).get('sha256') == file_sha256(validation_file)
                   and video_file.exists() and recording.get('videoSha256') == file_sha256(video_file))
    if name != source:
        # A chapter inherits its source run only while the parent movie and its
        # recording metadata still agree. Replaced source footage is unbound.
        parent_video = OUT / (source + '.mp4')
        parent_recording = recording_metadata(source)
        binding = bool(binding and parent_video.exists() and
                       recording.get('sourceVideoSha256') == file_sha256(parent_video) and
                       parent_recording.get('videoSha256') == recording.get('sourceVideoSha256') and
                       parent_recording.get('captureFingerprint') == fingerprint)
    if not binding:
        return 'INCOMPLETE', [], False
    changed = []
    for key in ('editorDll', 'source', 'savedContent'):
        if fingerprint.get(key, {}).get('sha256') != workspace[key].get('sha256'):
            changed.append(key)
    return ('DIFFERENT' if changed else 'MATCH'), changed, binding


def legacy_time_compatible(name, source):
    """Weak fallback only: validation precedes the encoded movie by <=5 minutes."""
    video = OUT / (name + '.mp4')
    validation = OUT / (source + '_Validation.txt')
    if not video.exists() or not validation.exists() or validation.stat().st_size == 0:
        return False
    gap = video.stat().st_mtime - validation.stat().st_mtime
    return 0 <= gap <= 300


def network_summary(path):
    if not path.exists():
        return ''
    report = json.loads(path.read_text(encoding='utf-8-sig'))
    runs = report.get('runs', [])
    processes = [process for run in runs for process in run.get('processes', [])]
    passed = sum(process.get('result') == 'PASS' for process in processes)
    modes = {'ClimbNetwork': 'лазание', 'SwimNetwork': 'плавание', 'CoffeeNetwork': 'кофе', 'SprayNetwork': 'спрей'}
    conditions = []
    for run in runs:
        condition = (f"{modes.get(run.get('mode'), run.get('mode', 'сценарий'))}: "
                     f"{run.get('packet_lag_ms', '?')} мс, {run.get('packet_loss_percent', '?')}% потерь")
        peer_results = run.get('processes', [])
        rendered_spray_peer = next((peer for peer in peer_results if peer.get('index') == 1 and
                                   re.search(r'\brendered_activation=1\b', peer.get('validation', ''))), None)
        if rendered_spray_peer:
            activation = 'PASS' if rendered_spray_peer.get('result') == 'PASS' else 'не подтверждена'
            condition += '; рендер client 1, активация Niagara ' + activation
        elif run.get('rendered', report.get('rendered')) is False or (peer_results and all(
                re.search(r'\brendered_activation=0\b', peer.get('validation', '')) for peer in peer_results)):
            condition += '; NullRHI, проверка сетевых состояний'
        elif run.get('mode') == 'SprayNetwork':
            condition += '; активация Niagara на client 1 не установлена'
        conditions.append(condition)
    conditions = '; '.join(conditions)
    text = (f"Сеть: {report.get('result', 'UNKNOWN')}, успешны {passed} из {len(processes)} запусков процессов "
            f"в {len(runs)} сценариях; по {report.get('players_per_run', '?')} игрока. {conditions}.")
    if report.get('generated_utc'):
        text += ' Отчёт: ' + local_timestamp(report['generated_utc']) + '.'
    return text


# Chapters preserve the recorded action and its timing. No frames are synthesized.
CHAPTERS = [
    ('Dodge', 'Hazards', 2.85, 1.50),
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
    parent_recording = recording_metadata(source)
    chapter_recording = {
        'case': name, 'source': source + '.mp4', 'chapterStart': start,
        'chapterDuration': duration, 'capture': 'Unreal game render target',
        'sourceRecording': source + '_Recording.json',
        'sourceVideoSha256': file_sha256(OUT / (source + '.mp4')),
        'encodedUtc': datetime.now(timezone.utc).isoformat(),
        'videoSha256': file_sha256(OUT / (name + '.mp4')),
    }
    for key in ('captureFingerprint', 'fingerprintValidationMatches', 'sample_fps'):
        if key in parent_recording:
            chapter_recording[key] = parent_recording[key]
    (OUT / (name + '_Recording.json')).write_text(json.dumps(chapter_recording, indent=2), encoding='utf-8')

VISUAL_REVIEW = ('Качество материалов и движения нужно оценить по видео; '
                 'автоматическая проверка подтверждает только условия сценария.')
CLIPS = [
    ('Materials', 'Материалы ротовой области', 'Визуал',
     'Общий вид, десна, стенки / нёбо, пищевод и увула. Карты Painter 2K, профиль рассеивания SP_Mucosa и менее контрастная влажная поверхность.',
     'Материалы пересобраны скриптом refine_mouth_v5.py. Соответствие референсу требует визуальной оценки.',
     VISUAL_REVIEW + ' В финальном проходе Materials на десне остаётся тёмная рваная полоса; этот дефект изображения ещё требует исправления.'),
    ('Climb', 'Лазание по игровому зубу', 'Движение',
     'Зацеп, подъём, висение, перемещение в сторону, выход на коронку зуба и отскок. Процедурная поза использует контакты рук и ног с поверхностью.',
     'E удерживать у поверхности; W/S вверх и вниз, A/D в стороны, Space оттолкнуться. Стамины нет.', VISUAL_REVIEW),
    ('Coffee', 'Кофе и управляемое плавание', 'Движение',
     'Наливание сверху, рост уровня, движение в жидкости и слив. Процедурные гребки рук проходят по эллипсу; во время плавания инструмент скрыт.',
     'WASD для движения; персонаж следует за уровнем жидкости. Сценарий отдельно проверяет управление и отсутствие оглушения передней волной.', VISUAL_REVIEW),
    ('Camera', 'Следящая камера и передняя граница', 'Движение',
     'Маршрут у передних зубов, по краям арены и к пищеводу. Это запись игровой камеры.',
     'Граница глаза камеры вынесена немного за передние зубы. Ограничение сохраняет высоту камеры.', VISUAL_REVIEW),
    ('Pickaxe', 'Кирка рядом с поверхностями', 'Инструменты',
     'Удары на языке в четырёх направлениях и около настоящего игрового зуба. Лог отдельно показывает проверку видимости инструмента и зазора над поверхностью.',
     '2 → ЛКМ. Кисть и инструмент корректируются вместе; при близкой стене размах сокращается.', VISUAL_REVIEW),
    ('Cola', 'Холодная кола, иней и лёд', 'События',
     'Напиток сверху, промерзание арены, скольжение после отпускания движения, падение льдин разных форм, разрушение и оттаивание.',
     'F3 → Холодная кола; 2 → ЛКМ по льду. Проверяются куб, сфера, цилиндр и вытянутый блок.', VISUAL_REVIEW),
    ('ShiftReset', 'Два перезапуска смены и новый таймер', 'События',
     'RestartShift вызывается во время живого холодного напитка с плаванием и льдом, затем во время лечения язвы с паузой фитиля перца и предупреждением волны. После каждого перезапуска проверяются новый игрок, инструменты, климат и удаление прежних опасностей.',
     'Автосценарий ждёт штатную паузу 8 секунд и запуск настоящего DayDirector первого дня. Затем через его API запускает событие завтрака и проверяет таймер, появление еды и счётчики задач.',
     VISUAL_REVIEW + ' Этот ролик проверяет два RestartShift; автоматический переход на Day 2 им не проверяется.'),
    ('Brush', 'Чистка и направление персонажа', 'Инструменты',
     'Контакт с двумя игровыми зубами и языком. Персонаж поворачивается к цели; грязь удаляется при контакте щётки.',
     '1 → удерживать ЛКМ. Эффект контакта использует Niagara.', VISUAL_REVIEW),
    ('Tools', 'Четыре слота и работа инструментов', 'Инструменты',
     'Щётка, кирка для твёрдой еды, нож для мягкой еды и спрей. Также показаны реакции HUD на успех, ошибку и смерть.',
     '1–4 выбор; ЛКМ использовать. Водомёт остаётся вариантом апгрейда с временным мешем.', VISUAL_REVIEW),
    ('Ulcer', 'Еда впитывается → язва → лечение', 'Инструменты',
     'Еда без внимания впитывается в язык и превращается в язву. Лечение сохраняет исходный вид ткани язвы; спрей использует Niagara NS_SprayMist.',
     '4 → удерживать ЛКМ на язве. Круглый индикатор заполняется за 7 секунд суммарного контакта; отпускание сохраняет прогресс.', VISUAL_REVIEW),
    ('SprayNetwork', 'Спрей без цели: нажатие и отпускание по сети', 'Инструменты',
     'Запись настоящего удалённого owning client 1 на listen server с тремя клиентами. Четыре игрока дважды удерживают и отпускают ЛКМ без язвы или цели лечения; проверяются намерение, репликация и активация Niagara на видимых персонажах.',
     'Слот 4 → удерживать ЛКМ: туман из сопла; отпустить: эффект прекращается. Сетевая запись идёт с обычным игровым временем.', VISUAL_REVIEW),
    ('Grip', 'Захват, перенос, тяга и толкание', 'Движение',
     'Сетевой сценарий: настоящий listen server и три клиента. Показаны позы рук и корпуса с тестовыми грузами.',
     'E взять / удерживать, WASD двигать, Q бросить. Шахматные блоки и подписи — тестовые объекты.', VISUAL_REVIEW),
    ('Uvula', 'Функциональная увула и проглатывание', 'События',
     'Подготовка, прыжок, давление весом, отскок и открытие прохода. Сохранена ранее созданная увула.',
     'Space в круге запускает совместное действие. В этом видео — одиночная проверка.', VISUAL_REVIEW),
    ('Vomit', 'Рвота и возврат партии', 'События',
     'Посторонний предмет / игрок в текущем проглатывании вызывает спазм. Предметы возвращаются, появляются три небольшие очищаемые лужи.',
     'Используется morph target vomit. Это небольшое загрязнение, заполнение арены относится к напиткам.', VISUAL_REVIEW),
    ('Breathing', 'Дыхание рта', 'Визуал',
     'Слабое движение блендшейпа Open в покое и движение живого горла. Это фрагмент осмотра материалов.',
     'Деформация работает у GAMEPLAY | Living throat → AuthoredMouth.', VISUAL_REVIEW),
    ('Dodge', 'Прыжок через волну язвы', 'Движение',
     'Один персонаж прыгает через низкую волну и сохраняет здоровье, второй стоит на поверхности и получает урон.',
     'Space перед пересечением волны. Проверка использует такую же волну, как язва.', VISUAL_REVIEW),
    ('Pepper', 'Перец: предупреждение и детонация', 'События',
     'Перец увеличивается и краснеет в такт одному пульсу; частота растёт перед локальной детонацией. Масштаб меняется вокруг центра меша, игровая коллизия остаётся постоянной.',
     'Таблица SpicyPepper: 8→6 секунд по дням, радиус растёт. Таймер при запуске увулы ставится на паузу.', VISUAL_REVIEW),
    ('Portraits', 'Эмоции портрета игрока', 'Визуал',
     'Успех, ошибка и смерть меняют портрет P1 справа сверху. При смерти — крестики и серый цвет.',
     'Это состояния UMG HUD, отдельного Animation Sequence у иконки нет.', VISUAL_REVIEW),
]

workspace = dict(editorDll=dict(sha256=file_sha256(ROOT / 'Binaries/Win64/UnrealEditor-MessControl.dll')),
                 source=tree_digest(ROOT, 'Source/MessControl'), savedContent=tree_digest(ROOT, 'Content', True))
version_labels = dict(MATCH='SHA256 совпадают', DIFFERENT='другая версия проекта',
                      UNRECORDED='версия не зафиксирована', INCOMPLETE='неполные данные запуска')

manifest = []
for name, title, group, description, controls, note in CLIPS:
    file = OUT / (name + '.mp4')
    probe = json.loads(command([FFPROBE, '-v', 'error', '-show_entries',
        'format=duration:stream=width,height,codec_name', '-of', 'json', str(file)]))
    command([FFMPEG, '-v', 'error', '-i', str(file), '-f', 'null', '-'])
    source = next((c[1] for c in CHAPTERS if c[0] == name), name)
    state, recorded_on = validation_result(source)
    recording = recording_metadata(name)
    version, changed, binding = fingerprint_state(recording, workspace, source, name)
    fingerprint = recording.get('captureFingerprint', {})
    captured_utc = fingerprint.get('startedUtc') or recorded_on
    log_state = state
    time_compatible = version == 'UNRECORDED' and legacy_time_compatible(source, source)
    validation_association = 'SHA256' if binding else 'LEGACY_TIME_HEURISTIC' if time_compatible else 'UNBOUND'
    if validation_association == 'UNBOUND':
        state = 'UNKNOWN'
    if version == 'MATCH':
        note += ' SHA256 бинарника, исходников и сохранённых пакетов совпадают с состоянием проекта при сборке галереи.'
    elif version == 'DIFFERENT':
        note += ' Ролик относится к другой версии проекта: SHA256 отличаются от текущего сохранённого состояния.'
    elif version == 'INCOMPLETE':
        note += ' Данные запуска неполные или SHA256 лога отличается от записи; результат нельзя связать с роликом.'
    else:
        note += ' Версия сборки при записи не зафиксирована; соответствие текущему коду не установлено.'
        if time_compatible:
            note += ' Связь ролика с логом предполагается только по времени файлов; эта эвристика не подтверждает версию сборки.'
        else:
            note += ' Связь текущего лога с этим роликом не подтверждена.'
    if state == 'FAIL':
        note += ' В логе этого запуска есть ошибка проверки; результат требует исправления.'
    elif log_state == 'UNKNOWN':
        note += ' Итоговый результат сценария в логе не найден.'
    if isinstance(recording.get('sample_fps'), (int, float)):
        note += f" Частота исходных игровых кадров: {recording['sample_fps']} fps; MP4 воспроизводится в 30 fps с повторением кадров."
    manifest.append(dict(id=name, title=title, group=group, description=description,
                         controls=controls, note=note, file=name + '.mp4', poster=name + '.png',
                         seconds=round(float(probe['format']['duration']), 2),
                         validation=source + '_Validation.txt', recording=name + '_Recording.json',
                         bytes=file.stat().st_size, decode='PASS', scenarioResult=state,
                         recordedOn=local_timestamp(captured_utc), recordedOnUtc=captured_utc,
                         captureVersion=version, versionLabel=version_labels[version],
                         changedFingerprintParts=changed, validationMatchesRecording=binding,
                         validationAssociation=validation_association, logResult=log_state,
                         sourceFramesPerSecond=recording.get('sample_fps'),
                         sourceRecording=source + '_Recording.json', historical=version == 'DIFFERENT'))

report_path = ROOT / 'Saved/TestReports/index.json'
report = json.loads(report_path.read_text(encoding='utf-8-sig'))
summary = {k: report[k] for k in ('reportCreatedOn', 'succeeded', 'succeededWithWarnings', 'failed', 'notRun', 'totalDuration')}
summary['inProcess'] = report.get('inProcess', 0)
summary['totalTests'] = sum(summary[k] for k in ('succeeded', 'succeededWithWarnings', 'failed', 'notRun', 'inProcess'))
summary['tests'] = [dict(name=t['fullTestPath'], state=t['state'], warnings=t['warnings'], errors=t['errors']) for t in report['tests']]
summary['reportCreatedOnUtc'] = datetime.strptime(summary['reportCreatedOn'], '%Y.%m.%d-%H.%M.%S').replace(tzinfo=timezone.utc).isoformat()
unit_validation = dict(summary, schemaVersion=1, suite='MessControl',
                       result='FAIL' if summary['failed'] else 'INCOMPLETE' if summary['notRun'] or summary['inProcess'] else 'PASS',
                       generatedUtc=datetime.now(timezone.utc).isoformat(),
                       sourceReport='Saved/TestReports/index.json', sourceReportSha256=file_sha256(report_path))
automation_text = (f"Автотесты: {summary['succeeded'] + summary['succeededWithWarnings']} из {summary['totalTests']} пройдены; "
                   f"{summary['succeeded']} без предупреждений, {summary['succeededWithWarnings']} с предупреждениями, "
                   f"ошибок {summary['failed']}, не запущено {summary['notRun']}, выполняется {summary['inProcess']}.")
(OUT / 'Automation_Summary.json').write_text(json.dumps(summary, ensure_ascii=False, indent=2), encoding='utf-8')
(OUT / 'Unit_Validation.json').write_text(json.dumps(unit_validation, ensure_ascii=False, indent=2), encoding='utf-8')
(OUT / 'Manifest.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2), encoding='utf-8')
generated_local = datetime.now(timezone(timedelta(hours=9))).strftime('%Y-%m-%d %H:%M UTC+9')
network_reports = [(name, network_summary(OUT / name)) for name in
                   ('TraversalNetwork_Validation.json', 'SprayNetwork_Validation.json')]
network_html = ''.join('<p><a href="' + name + '">' + html.escape(text) + '</a> '
                       'Сведения о рендеринге указаны у сценариев; визуальное качество оценивается по роликам.</p>'
                       for name, text in network_reports if text)

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
</style><main><div class="eyebrow">MESSCONTROL / __GENERATED_LOCAL__</div>
<h1>Материалы и механики — видео для проверки</h1>
<p>__CLIP_COUNT__ отдельных роликов из проекта. Выбери пункт справа и запусти видео. У каждого указаны дата запуска, результат сценария и сведения о версии проекта.</p>
<p><a href="Unit_Validation.json">__AUTOMATION_TEXT__</a> Отчёт: __REPORT_DATE__. Он описывает свой запуск; результаты сценариев указаны у каждого видео.</p>
__NETWORK_SUMMARY__
<div class="layout"><section><video id="video" controls preload="metadata" playsinline></video><h2 id="title"></h2><p id="description"></p><p id="controls"></p><p id="result"></p><p id="note" class="note"></p><div class="links"><a id="download">Открыть MP4</a><a id="validation">Результат проверки</a><a id="recording">Данные записи</a></div></section>
<aside><div class="filters" id="filters"></div><div id="list" class="list"></div></aside></div>
<footer>Сценарии задают расстановку и вызывают настоящие игровые действия программно. Записан только игровой render target; экран, WoW, мышь и клавиатура ОС не использовались. Исходные кадры сохраняют игровое время; MP4 выводится в 30 fps с повторением кадров. Совместный проход четырьмя людьми пока не проверен. Художественное качество материалов и процедурных анимаций требует твоей оценки.</footer></main>
<script>
const clips=__CLIPS__;let selected=clips[0].id,group='Все';const $=id=>document.getElementById(id);
function select(c){selected=c.id;const v=$('video');v.pause();v.src=c.file;v.poster=c.poster;v.load();$('title').textContent=c.title;$('description').textContent=c.description;$('controls').textContent=c.controls;$('result').textContent='Результат записи: '+c.scenarioResult+(c.recordedOn?' · '+c.recordedOn:'')+' · '+c.versionLabel+(c.validationAssociation==='LEGACY_TIME_HEURISTIC'?' · связь с логом по времени файлов':c.validationAssociation==='UNBOUND'?' · текущий лог: '+c.logResult:'');$('note').textContent=c.note;$('note').hidden=!c.note;for(const [id,key] of [['download','file'],['validation','validation'],['recording','recording']])$(id).href=c[key];render();}
function render(){$('list').replaceChildren();clips.filter(c=>group==='Все'||c.group===group).forEach(c=>{const b=document.createElement('button');b.className='clip'+(c.id===selected?' active':'');const t=document.createElement('span');t.textContent=c.title;const s=document.createElement('small');s.textContent=c.group+' · '+Math.round(c.seconds)+' с · '+c.scenarioResult+' · '+c.versionLabel;b.append(t,s);b.onclick=()=>select(c);$('list').append(b);});$('filters').replaceChildren();['Все',...new Set(clips.map(c=>c.group))].forEach(g=>{const b=document.createElement('button');b.textContent=g;b.className=g===group?'active':'';b.onclick=()=>{group=g;render();};$('filters').append(b);});}select(clips[0]);
</script></html>'''
page = template.replace('__CLIPS__', json.dumps(manifest, ensure_ascii=False).replace('</', '<\\/'))
page = page.replace('__CLIP_COUNT__', str(len(manifest))).replace('__AUTOMATION_TEXT__', html.escape(automation_text))
page = page.replace('__REPORT_DATE__', html.escape(local_timestamp(summary['reportCreatedOnUtc'])))
page = page.replace('__GENERATED_LOCAL__', html.escape(generated_local))
page = page.replace('__NETWORK_SUMMARY__', network_html)
(OUT / 'index.html').write_text(page, encoding='utf-8')

lines = ['# Видео для проверки — ' + generated_local, '',
         'Записи настоящего игрового render target. Сценарии выполняют действия программно; совместный проход четырьмя людьми пока не проверен.', '',
         automation_text + ' Отчёт: ' + local_timestamp(summary['reportCreatedOnUtc']) + '.', '',
         'Галерея: [index.html](index.html). Данные: [Manifest.json](Manifest.json), [Unit_Validation.json](Unit_Validation.json), [Automation_Summary.json](Automation_Summary.json).', '',
         '| Пункт | Видео | Проверка запуска | Запись |', '| --- | --- | --- | --- |']
network_lines = []
for name, text in network_reports:
    if text:
        network_lines += [text, '', '[Сетевой отчёт](' + name + '). Режим рендеринга указан у каждого сценария; художественное качество оценивается по видео.', '']
lines[6:6] = network_lines
for c in manifest:
    age = c['versionLabel'] + ('; ' + c['recordedOn'] if c['recordedOn'] else '')
    lines.append(f"| {c['title']} | [{c['seconds']} с]({c['file']}) | [{c['scenarioResult']}]({c['validation']}) | {age} |")
lines += ['', 'Версия каждого нового запуска фиксируется до старта Unreal: SHA256 Editor DLL, исходников Source/MessControl и сохранённых пакетов Content. При сборке галереи эти значения сравниваются с файлами проекта; главы наследуют сведения исходного ролика. У старых записей без fingerprint версия не установлена. Художественное качество и соответствие референсу требуют визуальной оценки.', '',
          'Painter: `ArtSource/MouthV4/Painter`; карты: `ArtSource/MouthV4/Textures`; создание материалов и профиля SP_Mucosa: `Tools/Unreal/refine_mouth_v5.py`.', '',
          'Воспроизведение проверок: `Tools/CaptureApproval.ps1 -Case <пункт>`; сборка галереи: `C:/Python314/python.exe Tools/build_approval_gallery.py`.']
(OUT / 'README.md').write_text('\n'.join(lines) + '\n', encoding='utf-8')
print(json.dumps({'clips': len(manifest), 'decodeFailures': 0, 'tests': summary['succeeded'] + summary['succeededWithWarnings'], 'gallery': str(OUT / 'index.html')}, ensure_ascii=False))
