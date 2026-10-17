"""Render actual fractional Blender poses at 60 fps without saving source files.

blender --background --factory-startup --disable-autoexec --python this.py
Each output frame is independently evaluated and rendered; no optical flow,
duplicate-frame conversion or reuse of the earlier 15 fps movie is performed.
"""
import argparse
import hashlib
import json
import math
import subprocess
import sys
from pathlib import Path

import bpy

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'ArtSource/NutAnimationStyle/TankCartoonV3'
CACHE = ROOT / 'Saved/NutAnimationStyle/TankCartoonV3_60fps'
BLEND = OUT / 'Nut_Tank_Cartoon_v3.blend'
FPS = 60
SIZE = 512
FFMPEG = 'C:/ffmpeg/ffmpeg.exe'
FFPROBE = 'C:/ffmpeg/ffprobe.exe'

sys.path.insert(0, str(Path(__file__).resolve().parent))
from author_nut_tank_cartoon_v3 import bind_action
from author_nut_tank_style import fcurves


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def curves(action):
    result = {'motion': {}, 'custom_properties': {}}
    for curve in fcurves(action):
        target = result['custom_properties' if curve.data_path.endswith(']') else 'motion']
        for key in curve.keyframe_points:
            target[key.interpolation] = target.get(key.interpolation, 0) + 1
    return result


def source_frame(label, output_frame):
    seconds = output_frame / FPS
    if label == 'Walk':
        return 1 + (seconds % 1.92) * 25
    if label == 'Hit':
        return 1 + (seconds % .4) * 25
    if seconds <= .65:
        return 1 + 50 * seconds / .65
    return 51 + 50 * (seconds - .65) / 2.5


def run(args):
    subprocess.run([FFMPEG, '-hide_banner', '-loglevel', 'error', '-y', *args], check=True)


def drawtext(text, y=14, size=21):
    return ("drawtext=fontfile='C\\:/Windows/Fonts/arial.ttf':text='" + text +
            "':expansion=none:x=14:y=" + str(y) + ':fontsize=' + str(size) +
            ':fontcolor=white:box=1:boxcolor=black@0.65')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--clips', nargs='+', choices=('Walk', 'Melee', 'Hit'),
                        default=['Walk', 'Melee', 'Hit'])
    args = parser.parse_args(sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else [])
    CACHE.mkdir(parents=True, exist_ok=True)
    old_report_path = OUT / 'Review60fpsReport.json'
    old_report = json.loads(old_report_path.read_text(encoding='utf8')) if old_report_path.exists() else {}
    monitored = [BLEND, *(OUT / ('A_NutTank_' + clip + '.fbx') for clip in ('Walk', 'Melee', 'Hit'))]
    hashes_before = {str(path): digest(path) for path in monitored}
    bpy.ops.wm.open_mainfile(filepath=str(BLEND))
    rig = bpy.data.objects['rig']
    scene = bpy.context.scene
    scene.render.engine = 'BLENDER_WORKBENCH'
    scene.render.resolution_x = scene.render.resolution_y = SIZE
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = 'PNG'
    scene.render.fps = FPS
    scene.render.fps_base = 1
    scene.display.render_aa = '8'
    rig.animation_data.use_nla = False
    for track in rig.animation_data.nla_tracks:
        track.mute = True
    report = {'fps': FPS, 'panel_size': SIZE, 'source_blend': str(BLEND),
              'evaluation': 'Blender frame_set(integer, subframe) for each rendered output frame',
              'preview_retimes_source_keyposes': False, 'optical_flow': False, 'frame_duplication': False,
              'render_scope': args.clips,
              'melee_authoring': {'last_held_coil_frame': 38, 'release_interim_frame': 41,
                                   'contact_frame': 51, 'impact_fraction': .5,
                                   'main_release_seconds': 10 * .65 / 50,
                                   'main_release_60fps_frames': 10 * .65 / 50 * FPS},
              'native_timing': {'walk_cycle_seconds': 1.92, 'walk_repeats': 2,
                                'melee_windup_seconds': .65, 'melee_recovery_seconds': 2.5,
                                'hit_seconds': .4, 'hit_repeats': 5},
              'camera': {'name': scene.camera.name, 'location': list(scene.camera.location),
                         'rotation': list(scene.camera.rotation_euler),
                         'ortho_scale': scene.camera.data.ortho_scale},
              'actions': {}, 'renders': {}, 'limitations': [
                  'Old reference actions retain their original stepped curves for an honest comparison.',
                  'Blender preview excludes native overlays, game colliders, lighting and impact VFX.',
                  'The 1.92-second walk period does not fall on the 60 fps sample grid; two cycles use 231 frames.']}
    counts = {'Walk': 231, 'Melee': 189, 'Hit': 120}
    paths = {}
    for label in ('Walk', 'Melee', 'Hit'):
        for version in (('Old', 'New') if label != 'Hit' else ('New',)):
            action_name = ('MC_Tank_V2_Reference_' if version == 'Old' else 'MC_Tank_') + label
            action = bpy.data.actions[action_name]
            audit = curves(action)
            report['actions'][action_name] = audit
            if version == 'New':
                assert audit['motion'].get('CONSTANT', 0) == 0, (action_name, audit)
                assert audit['motion'].get('BEZIER', 0) > 0, (action_name, audit)
            label_version = label + '_' + version
            movie = CACHE / (label_version + '.mp4')
            if label not in args.clips:
                assert movie.exists(), ('Missing previously rendered 60 fps clip', movie)
                paths[(label, version)] = movie
                report['renders'][label_version] = old_report['renders'][label_version]
                continue
            bind_action(rig, action)
            frames = CACHE / label_version
            frames.mkdir(exist_ok=True)
            feet_origin = None
            feet_max = 0.0
            previous = None
            angular_max = {}
            fraction_count = 0
            for output_frame in range(counts[label]):
                frame = source_frame(label, output_frame)
                integer = math.floor(frame)
                fraction = frame - integer
                fraction_count += int(fraction > 1e-6)
                scene.frame_set(integer, subframe=fraction)
                bpy.context.view_layer.update()
                evaluated = rig.evaluated_get(bpy.context.evaluated_depsgraph_get())
                feet = {bone: (evaluated.matrix_world @ evaluated.pose.bones[bone].matrix).translation.copy()
                        for bone in ('foot.l', 'foot.r')}
                if feet_origin is None:
                    feet_origin = feet
                feet_max = max(feet_max, *(abs((feet[bone] - feet_origin[bone]).length) for bone in feet))
                rotations = {bone: evaluated.pose.bones[bone].matrix.to_quaternion()
                             for bone in ('c_root.x', 'c_head.x', 'c_hand_fk.r')}
                if previous is not None:
                    for bone, quat in rotations.items():
                        angle = math.degrees(previous[bone].rotation_difference(quat).angle)
                        angular_max[bone] = max(angular_max.get(bone, 0), min(angle, 360 - angle))
                previous = rotations
                scene.render.filepath = str(frames / f'{output_frame:04d}.png')
                bpy.ops.render.render(write_still=True)
                if output_frame % 60 == 0:
                    print('TANK_REAL_60_FRAME', label_version, output_frame, flush=True)
            run(['-framerate', str(FPS), '-i', str(frames / '%04d.png'), '-frames:v', str(counts[label]),
                 '-vf', drawtext(label + ' | ' + ('Previous' if version == 'Old' else 'Smooth cartoon')),
                 '-c:v', 'libx264', '-crf', '18', '-pix_fmt', 'yuv420p', '-movflags', '+faststart', str(movie)])
            paths[(label, version)] = movie
            report['renders'][label_version] = {'frames': counts[label], 'fractional_frames': fraction_count,
                'feet_max_displacement_m': feet_max, 'max_rotation_delta_degrees_per_60fps_frame': angular_max}
            print('TANK_REAL_60_CLIP', label_version, flush=True)
    for label in ('Walk', 'Melee'):
        movie = CACHE / (label + '_Comparison.mp4')
        run(['-i', str(paths[(label, 'Old')]), '-i', str(paths[(label, 'New')]),
             '-filter_complex', 'hstack=inputs=2', '-c:v', 'libx264', '-crf', '18',
             '-pix_fmt', 'yuv420p', str(movie)])
        paths[label] = movie
    video = OUT / 'Tank_Cartoon_Review_60fps.mp4'
    def footer(text):
        return 'drawbox=x=0:y=466:w=iw:h=46:color=black@0.72:t=fill,' + drawtext(text, 482, 21)
    filters = ('[0:v]setpts=PTS-STARTPTS,' + footer('WALK | 1.92s cycle | 2 repeats | actual 60 FPS') + '[walk];' +
               '[1:v]setpts=PTS-STARTPTS,' + footer('MELEE | 0.65s windup + 2.50s recovery | contact 50%') + '[melee];' +
               '[2:v]setpts=PTS-STARTPTS,pad=1024:512:256:0:color=0x343c49,' +
               footer('HIT | 0.40s | 5 repeats | actual 60 FPS') + '[hit];' +
               '[walk][melee][hit]concat=n=3:v=1:a=0[out]')
    run(['-i', str(paths['Walk']), '-i', str(paths['Melee']), '-i', str(paths[('Hit', 'New')]),
         '-filter_complex', filters, '-map', '[out]', '-c:v', 'libx264', '-crf', '18',
         '-pix_fmt', 'yuv420p', '-movflags', '+faststart', str(video)])
    report['video'] = str(video)
    report['probe'] = json.loads(subprocess.check_output([FFPROBE, '-v', 'error', '-show_entries',
        'format=duration:stream=width,height,r_frame_rate,nb_frames', '-of', 'json', str(video)], text=True))
    hashes_after = {str(path): digest(path) for path in monitored}
    assert hashes_before == hashes_after, 'Preview must not modify source Blend or FBX files'
    report['sources_preserved'] = True
    report['source_sha256'] = hashes_after
    (OUT / 'Review60fpsReport.json').write_text(json.dumps(report, indent=2), encoding='utf8')
    print('TANK_REAL_60_COMPLETE', json.dumps({'video': str(video), 'probe': report['probe'],
                                           'sources_preserved': True}), flush=True)


if __name__ == '__main__':
    main()
