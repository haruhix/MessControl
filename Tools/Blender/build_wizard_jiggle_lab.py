"""Isolated, real Jiggle Physics 4.5.1 tests on saved Wizard animations.

Run in factory-startup Blender background. Never modifies source .blend files.
The provider is the locally installed add-on, not a substitute spring solver.
"""
import argparse
import hashlib
import importlib.util
import json
import math
import runpy
import sys
from pathlib import Path

import bpy
from mathutils import Matrix

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'ArtSource/NutAnimationStyle/Wizard/DynamicsLab'
ADDON = Path('C:/Users/user/AppData/Roaming/Blender Foundation/Blender/5.1/extensions/blender_org/jiggle_physics/__init__.py')
PROVIDER = 'Jiggle Physics 4.5.1'
CASES = [
    dict(name='Baseline', label='Исходная анимация', blend=0., elasticity=1., body_blend=0.),
    dict(name='Mild', label='Jiggle: мягко', blend=.22, elasticity=.86, body_blend=.06),
    dict(name='Medium', label='Jiggle: средне', blend=.42, elasticity=.74, body_blend=.10),
    dict(name='Strong', label='Jiggle: сильно', blend=.65, elasticity=.62, body_blend=.16),
]
PHYSICS_BONES = ('WalnutBody', 'Forearm.R', 'Forearm.L', 'Hand.R', 'Hand.L')


def sha(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as f:
        for chunk in iter(lambda: f.read(1024*1024), b''):
            h.update(chunk)
    return h.hexdigest()


SOURCE_DEFAULTS = {}


def activate(rig, action):
    rig.animation_data_create()
    rig.animation_data.action = None
    for pb in rig.pose.bones:
        defaults = SOURCE_DEFAULTS.get(rig.name, {}).get(pb.name)
        if defaults:
            pb.matrix_basis = defaults['basis'].copy()
            for name,value in defaults['properties'].items():
                pb[name] = value
        else:
            pb.matrix_basis.identity()
    rig.animation_data.action = action
    rig.animation_data.use_nla = False
    for t in rig.animation_data.nla_tracks:
        t.mute = True
    if action and action.slots:
        rig.animation_data.action_slot = action.slots[0]
    bpy.context.view_layer.update()


def curves(action):
    return [fc for layer in action.layers for strip in layer.strips
            for bag in strip.channelbags for fc in bag.fcurves]


def register_provider():
    if hasattr(bpy.types.Scene, 'jiggle'):
        return
    spec = importlib.util.spec_from_file_location('wizard_lab_real_jiggle_provider', ADDON)
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    # Skip only the optional user-preset installation; keep the actual solver
    # and bake operator intact. Factory-startup test must not write preferences.
    module.install_presets = lambda: None
    module.register()
    return module


def configure(rig, case, loop):
    scene = bpy.context.scene
    scene.jiggle.enable = False
    rig.jiggle.enable = True
    rig.jiggle.mute = False
    rig.jiggle.freeze = False
    for pb in rig.pose.bones:
        pb.jiggle.enable = False
    for name in PHYSICS_BONES:
        pb = rig.pose.bones[name]
        pb.jiggle.enable = True
        body = name == 'WalnutBody'
        pb.jiggle_angle_elasticity = .94 if body else case['elasticity']
        pb.jiggle_root_elasticity = .98 if body else 1.
        pb.jiggle_length_elasticity = 1.
        pb.jiggle_elasticity_soften = 0.
        pb.jiggle_gravity = 0.
        pb.jiggle_blend = case['body_blend'] if body else case['blend']
        pb.jiggle_air_drag = .12
        pb.jiggle_friction = .20
        pb.jiggle_collision_radius = .025
    scene.jiggle.loop = loop
    scene.jiggle.preroll = 90 if loop else 30
    scene.jiggle.bake_overwrite = True  # Only overwrites a fresh action copy.
    scene.jiggle.bake_nla = False
    scene.jiggle.enable = True


def pose_sample(rig):
    return {pb.name: (rig.matrix_world @ pb.matrix).copy() for pb in rig.pose.bones}


def source_clips(label):
    if label == 'Original':
        return [dict(name='Original_'+n, label=n, action='NUT_W_'+n,
                     start=int(bpy.data.actions['NUT_W_'+n].frame_range[0]),
                     end=int(bpy.data.actions['NUT_W_'+n].frame_range[1]), loop=False)
                for n in ('Firenut','NutRain','CastSummon')]
    report = json.loads((ROOT/'ArtSource/NutAnimationStyle/Wizard/WizardFreshV6Report.json').read_text(encoding='utf8'))
    return [dict(name='Current_'+r['label'], label=r['label'], action=r['action'],
                 start=r['start'], end=r['end'], loop=r['loop']) for r in report['records']]


def build_source(label, source, helpers, report):
    bpy.ops.wm.open_mainfile(filepath=str(source))
    scene = bpy.context.scene
    scene.render.fps = 30
    scene.render.fps_base = 1.
    scene.use_preview_range = False
    scene.jiggle.enable = False
    old, mesh = bpy.data.objects['rig'], bpy.data.objects['SM_NutWizard']
    SOURCE_DEFAULTS.clear()
    defaults = {pb.name:dict(basis=pb.matrix_basis.copy(),properties={k:pb[k] for k in pb.keys() if isinstance(pb[k],(bool,int,float))}) for pb in old.pose.bones}
    SOURCE_DEFAULTS[old.name] = defaults
    names, parents, order, deform = helpers['_names_and_parents'](old)
    clips = source_clips(label)
    reference = {}
    for clip in clips:
        activate(old, bpy.data.actions[clip['action']])
        for fraction in (0., .45, .75, 1.):
            frame = round(clip['start']+(clip['end']-clip['start'])*fraction)
            scene.frame_set(frame)
            reference[clip['name'], frame] = helpers['_surface'](mesh)
    old.name = 'ARP_Source_Archive'
    SOURCE_DEFAULTS[old.name] = defaults
    new = helpers['_create_rig'](old, names, parents, order)
    new.name = 'JiggleLabRig'
    for mod in mesh.modifiers:
        if mod.type == 'ARMATURE' and mod.object == old:
            mod.object = new
    if mesh.parent == old:
        world = mesh.matrix_world.copy()
        mesh.parent = new
        mesh.matrix_world = world
    for source_name in deform:
        group = mesh.vertex_groups.get(source_name)
        if group:
            group.name = names[source_name]
    actions = {}
    for clip in clips:
        original = bpy.data.actions[clip['action']]
        activate(old, original)
        baked = bpy.data.actions.new('JLab_Baseline_'+clip['name'])
        baked.use_fake_user = True
        activate(new, baked)
        previous = {}
        error = 0.
        for frame in range(clip['start'], clip['end']+1):
            scene.frame_set(frame)
            bpy.context.view_layer.update()
            evaluated = old.evaluated_get(bpy.context.evaluated_depsgraph_get())
            transform = new.matrix_world.inverted() @ evaluated.matrix_world
            targets = {n: transform @ evaluated.pose.bones[n].matrix for n in order}
            actual = {}
            for n in order:
                b, pb = new.data.bones[names[n]], new.pose.bones[names[n]]
                kwargs = {}
                if parents[n]:
                    p = parents[n]
                    kwargs = dict(parent_matrix=actual[p], parent_matrix_local=new.data.bones[names[p]].matrix_local)
                basis = b.convert_local_to_pose(targets[n], b.matrix_local, invert=True, **kwargs)
                loc, rot, scale = basis.decompose()
                if n in previous and rot.dot(previous[n]) < 0:
                    rot.negate()
                previous[n] = rot.copy()
                pb.location, pb.rotation_quaternion, pb.scale = loc, rot, scale
                actual[n] = b.convert_local_to_pose(Matrix.LocRotScale(loc,rot,scale),b.matrix_local, **kwargs)
                error = max(error, max(abs(actual[n][r][c]-targets[n][r][c]) for r in range(4) for c in range(4)))
                for channel in ('location','rotation_quaternion','scale'):
                    pb.keyframe_insert(channel, frame=frame, group=pb.name)
        for fc in curves(baked):
            for kp in fc.keyframe_points:
                kp.interpolation = 'LINEAR'
        clip.update(frame_start=clip['start'],frame_end=clip['end'],frame_count=clip['end']-clip['start']+1,
                    timeline_seconds=(clip['end']-clip['start'])/30.,source_label=label,maximum_bake_matrix_error=error)
        assert error < .0005, ('Baseline matrix bake mismatch', clip['name'],error)
        actions['Baseline',clip['name']] = baked
        print('BASELINE_BAKED', clip['name'],flush=True)
    old.hide_render = True
    old.hide_set(True)
    for obj in scene.objects:
        if obj.type == 'MESH' and obj != mesh:
            obj.hide_render = True
    surface_checks = []
    for (clip_name, frame), positions in reference.items():
        activate(new, actions['Baseline',clip_name])
        scene.frame_set(frame)
        actual = helpers['_surface'](mesh)
        assert len(actual)==len(positions)
        error = max((a-b).length for a,b in zip(positions,actual))
        surface_checks.append(dict(clip=clip_name,frame=frame,maximum_vertex_error=error))
        assert error < .0005, ('Baseline surface mismatch',clip_name,frame,error)
    physics_checks = []
    bpy.ops.object.select_all(action='DESELECT')
    new.select_set(True)
    bpy.context.view_layer.objects.active = new
    bpy.ops.object.mode_set(mode='POSE')
    for clip in clips:
        baseline = actions['Baseline',clip['name']]
        scene.frame_start, scene.frame_end = clip['start'],clip['end']
        for case in CASES[1:]:
            scene.jiggle.enable = False
            baked = baseline.copy()
            baked.name = 'JLab_'+case['name']+'_'+clip['name']
            baked.use_fake_user = True
            activate(new,baked)
            scene.frame_set(clip['start'])
            configure(new,case,clip['loop'])
            result = bpy.ops.armature.jiggle_bake()
            assert 'FINISHED' in result, ('Jiggle bake failed',result)
            assert new.animation_data.action == baked, 'Provider unexpectedly replaced the action'
            scene.jiggle.enable = False
            new.jiggle.mute = True
            new.jiggle.freeze = True
            maximum_delta = 0.
            pinned_delta = 0.
            max_ground_penetration = 0.
            finite = True
            for frame in range(clip['start'],clip['end']+1):
                activate(new,baseline)
                scene.frame_set(frame)
                before = pose_sample(new)
                activate(new,baked)
                scene.frame_set(frame)
                after = pose_sample(new)
                for n in PHYSICS_BONES:
                    delta = max(abs(before[n][r][c]-after[n][r][c]) for r in range(4) for c in range(4))
                    maximum_delta = max(maximum_delta,delta)
                for n in ('Pelvis','Foot.R','Foot.L','Thigh.R','Thigh.L'):
                    pinned_delta=max(pinned_delta,max(abs(before[n][r][c]-after[n][r][c]) for r in range(4) for c in range(4)))
                finite &= all(math.isfinite(v) for m in after.values() for row in m for v in row)
                for n in ('Foot.R','Foot.L'):
                    max_ground_penetration=max(max_ground_penetration,max(0.,-after[n].translation.z))
            assert finite and pinned_delta < .0005, ('Pinned bones changed/nonfinite',clip['name'],case['name'],pinned_delta)
            physics_checks.append(dict(clip=clip['name'],case=case['name'],operator='armature.jiggle_bake',
                operator_result=list(result),finite=finite,maximum_matrix_difference=maximum_delta,
                pinned_leg_matrix_difference=pinned_delta,foot_joint_below_zero=max_ground_penetration,
                actual_effect=maximum_delta>1e-5))
            actions[case['name'],clip['name']] = baked
            print('JIGGLE_BAKED',clip['name'],case['name'],maximum_delta,flush=True)
    bpy.ops.object.mode_set(mode='OBJECT')
    scene.jiggle.enable=False
    new.jiggle.mute=True
    new.jiggle.freeze=True
    activate(new,actions['Baseline',clips[0]['name']])
    scene.frame_start,scene.frame_end=clips[0]['start'],clips[0]['end']
    scene.frame_set(clips[0]['start'])
    mesh.hide_render=False
    try:
        bpy.ops.file.pack_all()
    except Exception:
        pass
    destination=OUT/('Wizard_'+label+'_JiggleLab.blend')
    bpy.ops.wm.save_as_mainfile(filepath=str(destination))
    report['sources'].append(dict(path=str(source),sha256=sha(source),label=label,lab_path=str(destination),lab_sha256=sha(destination)))
    report['clips'].extend(clips)
    report['surface_checks'].extend(surface_checks)
    report['physics_checks'].extend(physics_checks)


def analyze_existing_labs(helpers):
    """Measure the saved provider bakes, without simulating or authoring again."""
    report=json.loads((OUT/'JiggleLabReport.json').read_text(encoding='utf8'))
    assert report['status']=='complete'
    SOURCE_DEFAULTS.clear()
    for source in report['sources']:
        assert sha(source['path'])==source['sha256']
        assert sha(source['lab_path'])==source['lab_sha256']
        bpy.ops.wm.open_mainfile(filepath=source['lab_path'])
        scene=bpy.context.scene
        if hasattr(scene,'jiggle'):
            scene.jiggle.enable=False
        rig,mesh=bpy.data.objects['JiggleLabRig'],bpy.data.objects['SM_NutWizard']
        source_clips=[c for c in report['clips'] if c['source_label']==source['label']]
        for clip in source_clips:
            frames=list(range(clip['frame_start'],clip['frame_end']+1))
            baseline={}
            ground={}
            ground_frames={round(frames[0]+(frames[-1]-frames[0])*f) for f in (0.,.25,.5,.75,1.)}
            activate(rig,bpy.data.actions['JLab_Baseline_'+clip['name']])
            for frame in frames:
                scene.frame_set(frame)
                baseline[frame]={n:(rig.matrix_world@rig.pose.bones[n].matrix).copy() for n in ('Hand.R','Hand.L')}
                if frame in ground_frames:
                    ground[frame]=min(p.z for p in helpers['_surface'](mesh))
            for case in CASES[1:]:
                row=next(r for r in report['physics_checks'] if r['clip']==clip['name'] and r['case']==case['name'])
                activate(rig,bpy.data.actions['JLab_'+case['name']+'_'+clip['name']])
                maximum_m,maximum_angle,end_m,end_angle=0.,0.,0.,0.
                samples=[]
                for frame in frames:
                    scene.frame_set(frame)
                    for n in ('Hand.R','Hand.L'):
                        before=baseline[frame][n]
                        after=rig.matrix_world@rig.pose.bones[n].matrix
                        delta=(before.translation-after.translation).length
                        angle=before.to_quaternion().rotation_difference(after.to_quaternion()).angle
                        angle=math.degrees(min(angle,2*math.pi-angle))
                        maximum_m=max(maximum_m,delta)
                        maximum_angle=max(maximum_angle,angle)
                        if frame==frames[-1]:
                            end_m=max(end_m,delta)
                            end_angle=max(end_angle,angle)
                    if frame in ground_frames:
                        minimum=min(p.z for p in helpers['_surface'](mesh))
                        samples.append(dict(frame=frame,mesh_minimum_world_z=minimum,
                                            baseline_mesh_minimum_world_z=ground[frame],
                                            extra_mesh_ground_penetration=max(0.,ground[frame]-minimum)))
                row.update(max_hand_delta_m=maximum_m,max_hand_angle_deg=maximum_angle,
                           end_hand_delta_m=end_m,end_hand_angle_deg=end_angle,
                           ground_surface_samples=samples,
                           max_extra_mesh_ground_penetration=max(s['extra_mesh_ground_penetration'] for s in samples))
                print('JIGGLE_METRIC',clip['name'],case['name'],maximum_m,maximum_angle,flush=True)
    report['saved_bake_analysis_complete']=True
    report['shared_solver_settings']=dict(body_angle_elasticity=.94,body_root_elasticity=.98,
        limb_root_elasticity=1.,length_elasticity=1.,elasticity_soften=0.,gravity=0.,
        air_drag=.12,friction=.20,collision_radius=.025,collision_enabled=False,
        loop_preroll=90,one_shot_preroll=30,bake_overwrite_copied_action=True)
    report['analysis_notes']=['Hand deltas measured from saved baked matrices at every source frame; angular difference takes the shortest quaternion arc.',
        'Ground samples cover start,25%,50%,75%,end. They report inherited baseline penetration and any additional jiggle penetration separately.',
        'No resimulation, rebake, source or .blend save during analysis.',
        'This pass tests rotational follow-through, keeping limb length rigid. It does not test squash/stretch or collision response.']
    (OUT/'JiggleLabReport.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')


def main():
    assert bpy.app.background, 'This authoring tool is background-only'
    parser=argparse.ArgumentParser()
    parser.add_argument('--source-label',choices=('Original','Current','Both'),default='Both')
    parser.add_argument('--analyze-only',action='store_true')
    args=parser.parse_args(sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else [])
    OUT.mkdir(parents=True,exist_ok=True)
    helpers=runpy.run_path(str(ROOT/'Tools/Blender/build_wizard_readable_rig_v9.py'))
    if args.analyze_only:
        analyze_existing_labs(helpers)
        return
    register_provider()
    report=dict(status='building',provider=PROVIDER,addon_path=str(ADDON),addon_sha256=sha(ADDON),
                cases=CASES,physics_bones=list(PHYSICS_BONES),sources=[],clips=[],surface_checks=[],physics_checks=[])
    definitions=[('Original',ROOT/'ArtSource/NutAnimationStyle/Wizard/Original/NutWizard_Original.blend'),
                 ('Current',ROOT/'ArtSource/NutAnimationStyle/Wizard/NutWizard_Fresh_v6.blend')]
    before={str(p):sha(p) for label,p in definitions}
    try:
        for label,source in definitions:
            if args.source_label in ('Both',label):
                build_source(label,source,helpers,report)
                (OUT/'JiggleLabReport.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
        report['source_files_unchanged']=all(sha(Path(p))==digest for p,digest in before.items())
        assert report['source_files_unchanged']
        report['status']='complete'
    except Exception as e:
        report['status']='failed'
        report['error']=repr(e)
        raise
    finally:
        (OUT/'JiggleLabReport.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
    print('JIGGLE_LAB_COMPLETE',len(report['clips']),len(report['physics_checks']),flush=True)


if __name__=='__main__':
    main()
