"""Render four actual Jiggle Physics cases with common camera and source timing."""
import argparse
import hashlib
import importlib.util
import json
import sys
import time
from pathlib import Path
from types import SimpleNamespace

import bpy
from mathutils import Vector

ROOT=Path(__file__).resolve().parents[2]
REPORT=ROOT/'ArtSource/NutAnimationStyle/Wizard/DynamicsLab/JiggleLabReport.json'
OUTPUT=ROOT/'Artifacts/WizardDynamicsComparison'


def sha(path):
    h=hashlib.sha256()
    with Path(path).open('rb') as f:
        for chunk in iter(lambda:f.read(1024*1024),b''):
            h.update(chunk)
    return h.hexdigest()


def save_manifest(output,manifest):
    temp=output/'RenderManifest.json.tmp'
    temp.write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf8')
    temp.replace(output/'RenderManifest.json')


def bind(rig,action):
    rig.animation_data_create()
    rig.animation_data.action=None
    for pb in rig.pose.bones:
        pb.matrix_basis.identity()
    rig.animation_data.action=action
    rig.animation_data.use_nla=False
    if action.slots:
        rig.animation_data.action_slot=action.slots[0]
    rig.update_tag()
    bpy.context.view_layer.update()


def get_util():
    spec=importlib.util.spec_from_file_location('wizard_preview_utils',Path(__file__).with_name('preview_wizard_human_v8.py'))
    util=importlib.util.module_from_spec(spec)
    spec.loader.exec_module(util)
    return util


def open_lab(source,util,scale):
    bpy.ops.wm.open_mainfile(filepath=source['lab_path'])
    scene=bpy.context.scene
    rig=bpy.data.objects['JiggleLabRig']
    mesh=bpy.data.objects['SM_NutWizard']
    settings=SimpleNamespace(width=640,height=480,render_aa='5',focus=(-.01,-.13,.85),ortho_scale=scale)
    cameras=util.setup_scene(scene,rig,mesh,settings)
    camera=cameras['ThreeQuarter']
    scene.camera=camera
    return scene,rig,mesh,camera


def main():
    assert bpy.app.background,'Run in an isolated background Blender process'
    parser=argparse.ArgumentParser()
    parser.add_argument('--output',type=Path,default=OUTPUT)
    parser.add_argument('--clips',nargs='+')
    args=parser.parse_args(sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else [])
    report=json.loads(REPORT.read_text(encoding='utf8'))
    assert report['status']=='complete',report.get('error')
    clips=[c for c in report['clips'] if not args.clips or c['name'] in args.clips]
    if args.clips:
        assert set(args.clips)=={c['name'] for c in clips},'Unknown clip requested'
    assert clips
    output=args.output.resolve()
    assert not any(output.rglob('frame_*.png')),'Use a fresh output directory'
    output.mkdir(parents=True,exist_ok=True)
    sources=report['sources']
    for source in sources:
        assert sha(source['path'])==source['sha256'],'Original source changed after bake'
        assert sha(source['lab_path'])==source['lab_sha256'],'Lab changed after bake'
    manifest=dict(status='running',fps=30,size=[640,480],provider=report['provider'],
        sources=sources,cases=[dict(name=c['name'],label=c['label'],directory=c['name']) for c in report['cases']],
        clips=[dict(name=c['name'],label=c['source_label']+' / '+c['label'],source_label=c['source_label'],frame_count=c['frame_count'],
                    frame_start=c['frame_start'],frame_end=c['frame_end'],timeline_seconds=c['timeline_seconds']) for c in clips],
        frames=[],views=['ThreeQuarter'],playback_speed=1,engine='BLENDER_WORKBENCH',
        source_animation_resampled=False,source_saved=False,notes=[
            'Actual installed Jiggle Physics4.5.1 bake operator, never a substituted solver.',
            'Original animation timing. Four cases rendered with the same camera and framing.',
            'All finite/pinned-leg and baseline skin checks are in JiggleLabReport.json.',
            'Blender authoring comparison. No Unreal packages or gameplay defaults were changed.'])
    save_manifest(output,manifest)
    util=get_util()
    started=time.monotonic()
    scale=3.1
    # Fit one scale against every case so apparent size cannot bias the A/B.
    for source in sources:
        source_clips=[c for c in clips if c['source_label']==source['label']]
        if not source_clips:
            continue
        scene,rig,mesh,camera=open_lab(source,util,scale)
        inverse=camera.matrix_world.inverted()
        deps=bpy.context.evaluated_depsgraph_get()
        for case in report['cases']:
            for clip in source_clips:
                bind(rig,bpy.data.actions['JLab_'+case['name']+'_'+clip['name']])
                for frame in range(clip['frame_start'],clip['frame_end']+1):
                    scene.frame_set(frame)
                    evaluated=mesh.evaluated_get(deps)
                    for corner in evaluated.bound_box:
                        local=inverse@(evaluated.matrix_world@Vector(corner))
                        scale=max(scale,2*abs(local.x)/.88,2*abs(local.y)*(640/480)/.88)
    manifest['ortho_scale']=scale
    try:
        for source in sources:
            source_clips=[c for c in clips if c['source_label']==source['label']]
            if not source_clips:
                continue
            scene,rig,mesh,camera=open_lab(source,util,scale)
            for clip in source_clips:
                for case in report['cases']:
                    action=bpy.data.actions['JLab_'+case['name']+'_'+clip['name']]
                    bind(rig,action)
                    directory=output/case['name']/clip['name']
                    directory.mkdir(parents=True,exist_ok=True)
                    for index,frame in enumerate(range(clip['frame_start'],clip['frame_end']+1)):
                        scene.frame_set(frame)
                        path=directory/('frame_%04d.png'%index)
                        scene.render.filepath=str(path)
                        bpy.ops.render.render(write_still=True)
                        assert path.is_file()
                        manifest['frames'].append(dict(case=case['name'],clip=clip['name'],index=index,
                            path=str(path),source_frame=frame,sha256=sha(path)))
                    save_manifest(output,manifest)
                    print('JIGGLE_RENDER_CASE_READY',clip['name'],case['name'],clip['frame_count'],flush=True)
        for source in sources:
            assert sha(source['path'])==source['sha256']
            assert sha(source['lab_path'])==source['lab_sha256']
        manifest.update(status='complete',source_files_unchanged=True,rendered_png_count=len(manifest['frames']),wall_seconds=time.monotonic()-started)
    except Exception as e:
        manifest.update(status='failed',error=repr(e),wall_seconds=time.monotonic()-started)
        raise
    finally:
        save_manifest(output,manifest)
    print('JIGGLE_RENDER_COMPLETE',len(clips),len(manifest['frames']),flush=True)


if __name__=='__main__':
    main()
