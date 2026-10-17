"""Summarize completed local animation captures; never modify game or DCC assets."""
import hashlib
import json
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'Artifacts/NutDynamicsNightly'


def read(relative):
    path = ROOT / relative
    data = json.loads(path.read_text(encoding='utf8'))
    if data.get('status') not in ('complete', 'passed') and 'SeamAnalysis' not in relative:
        raise RuntimeError('Evidence is unfinished: ' + str(path))
    return data, dict(path=str(path), sha256=hashlib.sha256(path.read_bytes()).hexdigest())


def main():
    jiggle, jiggle_ref = read('ArtSource/NutAnimationStyle/Wizard/DynamicsLab/JiggleLabReport.json')
    seam, seam_ref = read('Artifacts/NutDynamicsNightly/BlenderSeamAnalysis.json')
    focused, focused_ref = read('Artifacts/NutDynamicsNightly/FocusedValidation.json')
    game, game_ref = read('Artifacts/NutDynamicsNightly/Gameplay/Capture.json')
    blender_video, blender_video_ref = read('Artifacts/WizardDynamicsComparison/VideoManifest.json')
    game_video, game_video_ref = read('Artifacts/NutDynamicsNightly/Gameplay/VideoManifest.json')
    checks = jiggle['physics_checks']
    runs = game['scenarios']
    modes = {}
    for mode in sorted({run['mode'] for run in runs}):
        rows = [run for run in runs if run['mode'] == mode]
        metrics = [metric for frame in game['frames'] if frame['case'] == mode for metric in frame['metrics']]
        modes[mode] = dict(
            runs=len(rows), passed=sum(run.get('passed', 0) > 0 for run in rows),
            failed=sum(run.get('failed', 0) > 0 for run in rows),
            captured_frames=sum(run['frames'] for run in rows),
            metric_frames=len(metrics),
            max_angle_deg=max((value['angle_degrees'] for value in metrics), default=0),
            max_translation_cm=max((value['translation_cm'] for value in metrics), default=0),
            angle_10deg_clamp_fraction=sum(value['angle_degrees'] >= 9.95 for value in metrics)/max(1,len(metrics)),
            translation_2cm_clamp_fraction=sum(value['translation_cm'] >= 1.995 for value in metrics)/max(1,len(metrics)),
        )
    functional_pass = focused['status'] == 'passed' and not game['violations'] and not game['restore_errors'] and all(
        run.get('passed', 0) > 0 and run.get('failed', 0) == 0 and run.get('observed_complete_cycle') for run in runs)
    findings = [
        'Jiggle Mild — кандидат для мягкой инерции рук. Сильный пресет уводит ладонь от точного жеста; динамика не исправляет исходную постановку поз.',
        'Короткая реакция на удар и смерть требуют отдельных настроек затухания: Medium/Strong сохраняют движение к концу клипа.',
        'Walk имеет унаследованный разрыв скорости на стыке при совпадающих конечных позах; ноги в этих Jiggle-пресетах не меняются.',
        'CONSTANT в исходном V6 найден только на восьми неизменных переключателях рига; это не подтверждает причину ступенчатого движения.',
        'Игровые StylizedDynamics/KawaiiPhysics сейчас воздействуют только на корпус и голову. Руки и squash/stretch в текущей интеграции не участвуют.',
        'В фазе крутилки отображается статический TankBall; костная динамика не управляет его вращением. Траектории расходятся из-за живой цели/столкновений: сравниваются фазы превращения/возврата, а путь шара не является сравнением солверов.',
        'В игровых Rain/Summon форма кистей и перекрытие пальцев всё ещё требуют авторской правки. Прохождение игровых callbacks не означает одобрение визуального качества.',
    ]
    report = dict(
        created_utc=datetime.now(timezone.utc).isoformat(),
        status='complete', local_functional_checks_passed=functional_pass,
        exact_cartoon_dynamic_product_identified=False,
        actual_providers=['Jiggle Physics 4.5.1', 'Native', 'StylizedDynamics', 'KawaiiPhysics'],
        evidence=[jiggle_ref, seam_ref, focused_ref, game_ref, blender_video_ref, game_video_ref],
        blender=dict(clips=len(jiggle['clips']), cases=len(jiggle['cases']), physics_bakes=len(checks),
            all_bakes_finite=all(value['finite'] for value in checks),
            pinned_leg_max_matrix_difference=max(value['pinned_leg_matrix_difference'] for value in checks),
            baseline_max_vertex_error_m=max(value['maximum_vertex_error'] for value in jiggle['surface_checks']),
            source_files_unchanged=jiggle['source_files_unchanged'],
            hand_drift=[{key:value[key] for key in ('clip','case','max_hand_delta_m','max_hand_angle_deg','end_hand_delta_m','end_hand_angle_deg')} for value in checks],
            seam_analysis=seam_ref),
        unreal=dict(world=game['preflight']['worlds'], gameplay_scenarios=len({run['name'] for run in runs}),
            mode_runs=len(runs), modes=modes, violations=game['violations'], restore_errors=game['restore_errors'],
            package_sources_unchanged=game['package_sources_unchanged'], recorded_hz=game['game_fps'],
            rendered_png_count=game['captured_png_count'], limitations=game['limitations'],
            diagnostics_note='Maximum of torso/head, one evaluation behind the rendered frame. Cannot independently measure the torso 4-degree clamp.'),
        videos=dict(blender=blender_video['videos'], unreal=game_video['videos']),
        findings=findings,
        next_steps=['Править постановку плеча/локтя/кисти и тайминг на базе исходных движений.',
                    'Исправить скорость на стыке Walk; отдельно проверить игровой переход в ходьбу.',
                    'Добавлять умеренную вторичную динамику с защитой контактов и затуханием на release/hit/death.'],
        scope_limits=['Точный инструмент с названием Cartoon Dynamic не идентифицирован. В этом проходе использованы реальные Jiggle Physics, StylizedDynamics и KawaiiPhysics.',
                      'Новые Mixamo-клипы не скачивались; в группе Original проверены три каста из сохранённого исходного .blend.',
                      'Тесты Blender: инерция/отдача, фиксированная длина костей, gravity 0; collider и squash/stretch не тестировались.',
                      'Пол проверялся в пяти точках каждого Blender-клипа; это не полная проверка всех контактов.',
                      '60 Hz — последовательные реальные игровые тики, не измерение производительности: захват PNG замедляет реальное время.',
                      'Только одиночная локальная PIE; сетевые тесты, build/cook/package и изменение производственных анимаций не выполнялись.'])
    (OUT / 'TestSummary.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf8')
    text = [f'Локальные проверки анимаций — {report["created_utc"]}',
            f'Blender: {len(jiggle["clips"])} клипов, {len(jiggle["cases"])} варианта, {len(checks)} запеканий с Jiggle Physics 4.5.1.',
            f'Unreal: {report["unreal"]["gameplay_scenarios"]} сцен, {len(runs)} запусков в Native / StylizedDynamics / KawaiiPhysics.',
            'Локальные функциональные проверки: ' + ('PASS' if functional_pass else 'есть проблемы, см. TestSummary.json'),
            '', *findings, '', 'Рекомендуемые следующие шаги:', *report['next_steps'], '', 'Границы проверки:', *report['scope_limits']]
    (OUT / 'RESULTS.txt').write_text('\n'.join(text)+'\n', encoding='utf8')
    print(json.dumps(dict(status=report['status'], functional_pass=functional_pass, game_runs=len(runs), modes=modes), ensure_ascii=False))


if __name__ == '__main__':
    main()
