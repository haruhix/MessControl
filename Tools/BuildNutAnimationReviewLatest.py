"""Assemble the saved Tank gallery and Wizard V6 clips with the V7 test Cast."""
import hashlib
import json
from pathlib import Path
import subprocess

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'Artifacts/NutFullReview_20261011_V7Cast'
FFMPEG = Path('C:/ffmpeg/ffmpeg.exe')
FFPROBE = Path('C:/ffmpeg/ffprobe.exe')
FPS = 30
SIZE = (1280, 720)
NAMES = dict(Idle='Ожидание', Walk='Ходьба', UnshieldedIdle='Ожидание без щита',
             UnshieldedWalk='Ходьба без щита', WalkLeft='Ходьба влево', WalkRight='Ходьба вправо',
             Melee='Размашистый удар', Hit='Реакция на удар', ShieldBreak='Поломка щита',
             Jump='Прыжок', Transform='Превращение', ChargeTell='Подготовка рывка',
             ChargeLoop='Рывок', ChargeRecovery='Восстановление после рывка', Cast='Огненный каст',
             HeavyCast='Тяжёлый каст / телепорт', Summon='Призыв', Rain='Дождь орехов', Death='Смерть')


def font(size, bold=False):
    return ImageFont.truetype('C:/Windows/Fonts/' + ('seguisb.ttf' if bold else 'segoeui.ttf'), size)


def probe(path):
    return json.loads(subprocess.check_output([
        str(FFPROBE), '-v', 'error', '-count_frames', '-select_streams', 'v:0',
        '-show_entries', 'stream=width,height,r_frame_rate,nb_read_frames,duration',
        '-of', 'json', str(path)], text=True))['streams'][0]


def banner(case, position, total):
    image = Image.new('RGB', SIZE, '#111c28')
    d = ImageDraw.Draw(image)
    role = 'ТАНК' if case['role'] == 'tank' else 'МАГ'
    label = NAMES[case['name'].split('_')[-1]]
    d.text((24, 12), f'{role}  {position:02d}/{total}  ·  {label}', font=font(29, True), fill='#f0f4f8')
    d.text((24, 55), case['version_label'], font=font(19), fill='#8cd4bd' if case['v7'] else '#b5c4d4')
    views = [(213, 'СПЕРЕДИ'), (640, '¾'), (1067, 'СБОКУ')] if case['v7'] else [(320, 'СПЕРЕДИ / ¾'), (960, 'СБОКУ')]
    for center, title in views:
        box = d.textbbox((0, 0), title, font=font(16, True))
        d.text((center-(box[2]-box[0])/2, 88), title, font=font(16, True), fill='#cdd9e6')
    if case['v7']:
        lines = ['Cast V7: новый риг и постановка в отдельном тестовом профиле.',
                 'Два повтора в игровом темпе 1,8 с; затем повтор на скорости 0,5×.',
                 'В основном игровом наборе Cast пока остаётся V6.']
    elif case['role'] == 'mage':
        lines = ['V6: прежняя постановка. Этот клип ещё требует переделки.',
                 f"Полный исходный клип · 1× скорость · {case['cycles']} повтора · 30 fps",
                 'Просмотр анимации без игровых VFX и вторичной физики.']
    else:
        lines = ['Сохранённый клип текущего набора танка.',
                 f"Полный исходный клип · 1× скорость · {case['cycles']} повтора · 30 fps",
                 'Roll вращается по движению в игре и не является отдельным клипом.']
    for y, text in zip((610, 645, 680), lines):
        d.text((24, y), text, font=font(20 if y == 610 else 17), fill='#ecf2f8' if y == 610 else '#b5c4d4')
    path = OUT / 'Encoded' / (case['name'] + '_Banner.png')
    image.save(path)
    return path


def sources():
    cases, evidence = [], []
    for directory_name, role in [('NutFullReview_20261010_Review', 'tank'), ('NutFullReview_20261010_V6', 'mage')]:
        directory = ROOT / 'Artifacts' / directory_name
        report_path = directory / 'GalleryCapture.json'
        report = json.loads(report_path.read_text(encoding='utf-8'))
        assert report['status'] == 'complete' and report['fps'] == FPS and not report['restore_errors']
        evidence.append(dict(path=str(report_path), sha256=hashlib.sha256(report_path.read_bytes()).hexdigest()))
        selected = [dict(c) for c in report['scenarios'] if c['role'] == role]
        assert len(selected) == (14 if role == 'tank' else 9)
        for c in selected:
            c['source_video'] = str(directory / 'Encoded' / (c['name'] + '.mp4'))
            c['v7'] = c['name'] == 'A_NutWizard_Cast'
            if c['v7']:
                c['source_video'] = str(ROOT / 'Artifacts/WizardPerformanceV7/Wizard_Fireball_V7.mp4')
                review_path = ROOT / 'Artifacts/WizardPerformanceV7/VideoReviewManifest.json'
                review = json.loads(review_path.read_text(encoding='utf-8'))
                source = ROOT / 'ArtSource/NutAnimationStyle/Wizard/NutWizard_Performance_v7.blend'
                assert hashlib.sha256(source.read_bytes()).hexdigest() == review['source_sha256']
                c['frames'] = review['output_frames']
                c['asset'] = '/Game/Gameplay/CoreLoop/NutWizard/MCPerformanceV7/A_NutWizard_Cast.A_NutWizard_Cast'
                c['mesh'] = '/Game/Gameplay/CoreLoop/NutWizard/MCPerformanceV7/SK_NutWizard_PerformanceV7.SK_NutWizard_PerformanceV7'
                c['version_label'] = 'V7 · НОВЫЙ КАСТ · ТЕСТОВЫЙ ПРОФИЛЬ'
                evidence.append(dict(path=str(review_path), sha256=hashlib.sha256(review_path.read_bytes()).hexdigest()))
            else:
                c['version_label'] = c['asset'].split('/')[-2]
            p = probe(Path(c['source_video']))
            assert p['r_frame_rate'] == '30/1' and int(p['nb_read_frames']) == c['frames']
            c['source_sha256'] = hashlib.sha256(Path(c['source_video']).read_bytes()).hexdigest()
            cases.append(c)
    return cases, evidence


def encode_case(c, position, total):
    label = banner(c, position, total)
    output = OUT / 'Encoded' / (c['name'] + '.mp4')
    crop = 'crop=1440:540:0:104,scale=1280:480' if c['v7'] else 'crop=1280:480:0:72'
    subprocess.run([str(FFMPEG), '-y', '-hide_banner', '-loglevel', 'error',
        '-i', c['source_video'], '-loop', '1', '-framerate', str(FPS), '-i', str(label),
        '-filter_complex', f'[0:v]{crop},setsar=1[views];[1:v][views]overlay=0:114:shortest=1[out]',
        '-map', '[out]', '-frames:v', str(c['frames']), '-an', '-c:v', 'libx264', '-threads', '4',
        '-bf', '0', '-preset', 'veryfast', '-crf', '18', '-pix_fmt', 'yuv420p',
        '-fps_mode', 'passthrough', '-movflags', '+faststart', str(output)], check=True)
    return output


def assemble(basename, pairs):
    listing = OUT / 'Encoded' / (basename + '.txt')
    listing.write_text(''.join("file '" + path.as_posix().replace("'", "'\\''") + "'\n" for _, path in pairs), encoding='utf-8')
    chapters, cursor = [], 0
    metadata = [';FFMETADATA1']
    for case, _ in pairs:
        name = case['name'].split('_')[-1]
        end = cursor + case['frames']
        title = ('Tank' if case['role'] == 'tank' else 'Wizard') + ' / ' + name + ' / ' + ('V7 TEST' if case['v7'] else case['version_label'])
        metadata.extend(['[CHAPTER]', 'TIMEBASE=1/30', f'START={cursor}', f'END={end}', 'title=' + title])
        chapters.append(dict(name=case['name'], start_seconds=cursor/FPS, duration_seconds=case['frames']/FPS,
                             version=case['version_label'], asset=case['asset'], source_video=case['source_video'],
                             source_sha256=case['source_sha256'], test_profile=case['v7']))
        cursor = end
    meta_path = OUT / 'Encoded' / (basename + '_Chapters.txt')
    meta_path.write_text('\n'.join(metadata) + '\n', encoding='utf-8')
    output = OUT / (basename + '.mp4')
    subprocess.run([str(FFMPEG), '-y', '-hide_banner', '-loglevel', 'error', '-f', 'concat', '-safe', '0',
                    '-i', str(listing), '-f', 'ffmetadata', '-i', str(meta_path), '-map_metadata', '1',
                    '-map_chapters', '1', '-map', '0:v:0', '-c', 'copy', '-movflags', '+faststart', str(output)], check=True)
    p = probe(output)
    assert (int(p['width']), int(p['height'])) == SIZE and p['r_frame_rate'] == '30/1'
    assert int(p['nb_read_frames']) == cursor and abs(float(p['duration'])-cursor/FPS) < .02
    poster = output.with_suffix('.png')
    subprocess.run([str(FFMPEG), '-y', '-hide_banner', '-loglevel', 'error', '-ss', '0.75',
                    '-i', str(output), '-frames:v', '1', str(poster)], check=True)
    return dict(video=str(output), poster=str(poster), stream=p, chapters=chapters)


def main(reuse_encoded=False):
    (OUT / 'Encoded').mkdir(parents=True, exist_ok=True)
    cases, evidence = sources()
    pairs = []
    for c in cases:
        siblings = [x for x in cases if x['role'] == c['role']]
        path = OUT / 'Encoded' / (c['name'] + '.mp4')
        if reuse_encoded:
            p = probe(path)
            assert int(p['nb_read_frames']) == c['frames'] and p['r_frame_rate'] == '30/1'
            assert (int(p['width']), int(p['height'])) == SIZE
        else:
            path = encode_case(c, siblings.index(c)+1, len(siblings))
        pairs.append((c, path))
        print(c['name'], flush=True)
    videos = [assemble('All_Animations_Tank_Wizard_Latest', pairs),
              assemble('Wizard_AllAnimations_Latest', [(c, p) for c, p in pairs if c['role'] == 'mage'])]
    report = dict(videos=videos, source_manifests=evidence, fps=FPS, interpolation=False,
                  unique_clips=23, tank_clips=14, wizard_clips=9,
                  scope='Archived current Tank and eight Wizard V6 clips; the Cast slot previews V7 test profile. Main game still uses V6 Cast.',
                  authoring_or_runtime_changes=False)
    (OUT / 'Video.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    for v in videos:
        print(v['video'], flush=True)


if __name__ == '__main__':
    main()
