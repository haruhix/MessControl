"""Compose the transformation prototype with captions from the Blender timeline."""
import json
import math
from pathlib import Path

import BuildCarriesStage2ReviewVideo as video

PHASES = (
    (1, 35, 'Фаза 1 · больной зуб'),
    (36, 130, '1 → 2 · выход первой руки'),
    (131, 205, 'Фаза 2 · половина инфекции'),
    (206, 340, '2 → 3 · раскрытие второй половины'),
    (341, 420, 'Фаза 3 · полное раскрытие'),
)


def phase_caption(source_frame):
    for index, (_, _, label) in enumerate(PHASES):
        if index == len(PHASES)-1 or source_frame < PHASES[index+1][0]:
            return label


def main():
    video.OUTPUT = Path(__file__).resolve().parents[1] / 'Artifacts/CarriesTransformation/Review'
    video.TITLE = 'Кариес · раскрытие инфекции · Blender-проба'
    video.BASENAME = 'Carries_Transformation_Review'
    video.VIEW_LABELS = {'ThreeQuarter': 'Три четверти'}
    original_verify, original_composite = video.verify, video.composite
    context = {}

    def verify(directory, expected_clips):
        result = list(original_verify(directory, expected_clips))
        manifest = dict(result[0])
        context.update(directory=directory, source_fps=float(manifest.get('source_fps', 30)),
                       source_frames={})
        for record in manifest['frames']:
            if 'blender_frame' in record:
                frame = float(record['blender_frame'])
                if not math.isfinite(frame):
                    raise ValueError('Invalid source Blender frame')
                context['source_frames'][record['clip'], record['index']] = frame
        manifest['scope'] = 'Blender-only transformation prototype for artistic review.'
        result[0] = manifest
        return tuple(result)

    def composite(clip, index, ordinal, total, paths, fps):
        canvas = original_composite(clip, index, ordinal, total, paths, fps)
        source_frame = context['source_frames'].get(
            (clip['name'], index), 1+index*context['source_fps']/float(fps))
        draw = video.ImageDraw.Draw(canvas)
        draw.rectangle((0, 99, 799, 119), fill='#17212d')
        label = phase_caption(source_frame)
        draw.text((16, 100), label,
                  font=video.fitted_font(draw, label, 16, width=510, bold=True), fill='#f1cf91')
        timing = f'1× · {float(fps):.6g} кадр/с · исходный кадр {source_frame:g}'
        draw.text((544, 103), timing,
                  font=video.fitted_font(draw, timing, 12, width=240), fill='#becbda')
        return canvas

    video.verify, video.composite = verify, composite
    try:
        video.main()
    finally:
        video.verify, video.composite = original_verify, original_composite
    # Keep the declared source-frame intervals beside the video evidence.
    report_path = context['directory'] / 'Video.json'
    report = json.loads(report_path.read_text(encoding='utf8'))
    report.update(prototype=True, phase_caption_basis='evaluated Blender source frame',
                  phase_captions=[dict(source_frame_start=start, source_frame_end=end, label=label)
                                  for start, end, label in PHASES])
    temporary = report_path.with_suffix('.json.tmp')
    temporary.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf8')
    temporary.replace(report_path)


if __name__ == '__main__':
    main()
