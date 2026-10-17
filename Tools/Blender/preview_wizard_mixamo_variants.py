"""Render four saved Blender-only cast studies with an identical camera."""
import argparse
import importlib.util
import json
import math
import sys
import time
from pathlib import Path

import bpy
from mathutils import Vector

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'ArtSource/NutAnimationStyle/Wizard/MixamoVariants/NutWizard_Mixamo_CastVariations.blend'
ORIGINAL = ROOT / 'ArtSource/NutAnimationStyle/Wizard/Original/NutWizard_Original.blend'
OUTPUT = ROOT / 'Artifacts/WizardMixamoVariants'
CASES = ('Original', 'Quick', 'Heavy', 'Ritual')
LABELS = ('Исходный Mixamo', 'Быстрый выброс', 'Тяжёлый заряд', 'Ритуальный каст')
spec = importlib.util.spec_from_file_location('mixamo_review_util', Path(__file__).with_name('preview_wizard_human_v8.py'))
util = importlib.util.module_from_spec(spec)
spec.loader.exec_module(util)
spec = importlib.util.spec_from_file_location('mixamo_author_util', Path(__file__).with_name('author_wizard_mixamo_variants.py'))
author = importlib.util.module_from_spec(spec)
spec.loader.exec_module(author)


def defaults(rig):
    return {bone.name: dict(basis=bone.matrix_basis.copy(), properties={key: bone[key] for key in bone.keys()
            if isinstance(bone[key], (bool, int, float))}) for bone in rig.pose.bones}


def surface(mesh):
    deps = bpy.context.evaluated_depsgraph_get()
    evaluated = mesh.evaluated_get(deps)
    data = evaluated.to_mesh()
    result = [evaluated.matrix_world @ vertex.co for vertex in data.vertices]
    evaluated.to_mesh_clear()
    return result


def main():
    assert bpy.app.background, 'Use isolated background Blender'
    parser = argparse.ArgumentParser()
    parser.add_argument('--draft', action='store_true')
    args = parser.parse_args(sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else [])
    OUTPUT.mkdir(parents=True, exist_ok=True)
    before = util.sha256(SOURCE)
    original_hash = util.sha256(ORIGINAL)
    report = json.loads((SOURCE.parent/'CastVariationsReport.json').read_text(encoding='utf8'))
    assert report['passed'] and report['blend_sha256'] == before
    assert report['source_sha256'] == original_hash
    # Three visual-pose checks confirm that densifying controllers preserves
    # the actual original skin, including static unkeyed ARP control values.
    references = {}
    bpy.ops.wm.open_mainfile(filepath=str(ORIGINAL))
    rig, mesh = bpy.data.objects['rig'], bpy.data.objects['SM_NutWizard']
    source_defaults = defaults(rig)
    author.bind(rig, bpy.data.actions['NUT_W_Firenut'], source_defaults)
    for frame in (1, 27, 39, 69):
        bpy.context.scene.frame_set(frame)
        references[frame] = surface(mesh)
    bpy.ops.wm.open_mainfile(filepath=str(SOURCE))
    scene, rig, mesh = bpy.context.scene, bpy.data.objects['rig'], bpy.data.objects['SM_NutWizard']
    saved_defaults = defaults(rig)
    author.bind(rig, bpy.data.actions['MC_Mixamo_Original'], saved_defaults)
    errors = {}
    for frame, vertices in references.items():
        scene.frame_set(frame+12)
        current = surface(mesh)
        assert len(current) == len(vertices)
        errors[frame] = max((first-second).length for first, second in zip(vertices, current))
    assert max(errors.values()) < .00002, errors
    scene.render.resolution_x, scene.render.resolution_y = 640, 480
    scene.render.resolution_percentage = 100
    scene.render.engine = 'BLENDER_WORKBENCH'
    scene.render.image_settings.file_format = 'PNG'
    scene.render.image_settings.color_mode = 'RGB'
    scene.render.image_settings.compression = 10
    scene.render.fps, scene.render.fps_base = 30, 1.
    camera = bpy.data.objects['MC_ReviewCamera_ThreeQuarter']
    scene.camera = camera
    if args.draft:
        for case, frame in zip(CASES, (48, 40, 50, 69)):
            author.bind(rig, bpy.data.actions['MC_Mixamo_'+case], saved_defaults)
            scene.frame_set(frame)
            path = OUTPUT / ('Draft_' + case + '.png')
            scene.render.filepath = str(path)
            bpy.ops.render.render(write_still=True)
        (OUTPUT/'DraftCheck.json').write_text(json.dumps(dict(original_skin_max_error_m=errors, source_preserved=True), indent=2), encoding='utf8')
        print('MIXAMO_VARIANTS_DRAFT_READY', errors, flush=True)
        return
    assert not any(OUTPUT.rglob('frame_*.png')), 'Use a fresh render directory'
    fps, count = 60, 264
    manifest = dict(status='running', fps=fps, size=[640, 480], provider='Blender · original Mixamo motion studies',
        source_fps=30, evaluation_subframes=True, source_animation_resampled=True,
        sources=[dict(path=str(SOURCE), sha256=before), dict(path=str(ORIGINAL), sha256=original_hash)],
        cases=[dict(name=case, label=label, directory=case) for case, label in zip(CASES, LABELS)],
        clips=[dict(name='Cast', label='Варианты каста', frame_count=count, frame_start=0, frame_end=count-1,
                    timeline_seconds=(count-1)/fps, note='Общая стойка в начале и конце; время и жесты вариантов различаются.')],
        frames=[], views=['ThreeQuarter'], playback_speed=1, engine='BLENDER_WORKBENCH',
        camera=camera.name, ortho_scale=camera.data.ortho_scale,
        original_skin_max_error_m=errors, original_source_saved=False,
        notes=['Blender only; no Unreal import or gameplay changes.',
               'Original means unchanged Firenut motion between frames13-82, with a common intro/outro.',
               'New Actions retain source rig, source skin and original foot controls.',
               '60fps image samples evaluate Blender at authored half frames; no video-generated inbetween frames.',
               'source_frame is the evaluation index; blender_frame records the actual authored30fps sample.'])
    util.write_manifest(OUTPUT, manifest)
    started = time.monotonic()
    try:
        for case in CASES:
            author.bind(rig, bpy.data.actions['MC_Mixamo_'+case], saved_defaults)
            directory = OUTPUT / case / 'Cast'
            directory.mkdir(parents=True, exist_ok=True)
            for index in range(count):
                frame = 1 + index * .5
                scene.frame_set(math.floor(frame), subframe=frame-math.floor(frame))
                path = directory / ('frame_%04d.png' % index)
                scene.render.filepath = str(path)
                bpy.ops.render.render(write_still=True)
                assert path.is_file(), str(path)
                manifest['frames'].append(dict(case=case, clip='Cast', index=index, source_frame=index,
                    blender_frame=frame, path=str(path), sha256=util.sha256(path)))
                if index % 60 == 59:
                    util.write_manifest(OUTPUT, manifest)
            util.write_manifest(OUTPUT, manifest)
            print('MIXAMO_VARIANT_RENDER_READY', case, count, flush=True)
        assert util.sha256(SOURCE) == before and util.sha256(ORIGINAL) == original_hash
        manifest.update(status='complete', source_files_unchanged=True, rendered_png_count=len(manifest['frames']),
                        wall_seconds=time.monotonic()-started)
    except Exception as error:
        manifest.update(status='failed', error=str(error), wall_seconds=time.monotonic()-started)
        raise
    finally:
        util.write_manifest(OUTPUT, manifest)
    print('MIXAMO_VARIANTS_RENDER_COMPLETE', str(OUTPUT), flush=True)


if __name__ == '__main__':
    main()
