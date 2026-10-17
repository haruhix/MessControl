"""Add single-purpose Wizard studies to the approved pool; preserve old motions."""
import json
import math
import re
import sys
from pathlib import Path

import bpy
from mathutils import Quaternion,Vector

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(Path(__file__).parent))
import author_wizard_mixamo_pool as pool
import wizard_mixamo_utility_recipes as recipes

INPUT=ROOT/'ArtSource/NutAnimationStyle/Wizard/MixamoPool/NutWizard_Mixamo_AnimationPool_v3.blend'
INPUT_REPORT=INPUT.parent/'AnimationPoolReport_v3.json'
DEST=ROOT/'ArtSource/NutAnimationStyle/Wizard/MixamoUtility'
OUTPUT=DEST/'NutWizard_Mixamo_SingleActions_v2.blend'
REPORT=DEST/'AnimationPoolReport_v2.json'
EXCLUDED={'Fire_DoublePulse'}
LOWER_WORDS=('foot','toes','leg','thigh','root','traj')


def excluded(name):
    return name.startswith('Combo_') or name in EXCLUDED


def compose(recipe,sample,neutral,neutral_props,upper,world_axes):
    records=[(pool.author.copy_pose(neutral),dict(neutral_props)) for _ in range(recipe['neutral_in_frames'])]
    previous=None
    for segment in recipe['segments']:
        entry,entry_props=records[-1]
        continuous=bool(previous and previous['source']==segment['source'] and previous['part']==segment['part']
                        and previous.get('lower_source_frame')==segment.get('lower_source_frame')
                        and abs(previous['source_end']-segment['source_start'])<1e-6)
        blend_frames=0 if continuous else segment['blend_in']
        for frame in range(segment['start'],segment['end']+1):
            pose,props=sample(segment['source'],pool.source_frame(segment,frame))
            pose,props=pool.author.copy_pose(pose),dict(props)
            if segment['part']=='upper':
                lower,lower_props=sample(segment['lower_source'],segment['lower_source_frame'])
                pose=pool.author.blend_pose(lower,pose,1.,upper)
                props.update({key:value for key,value in lower_props.items() if any(w in key[0] for w in LOWER_WORDS)})
            if blend_frames:
                weight=pool.author.smooth((frame-segment['start'])/max(1,blend_frames-1))
                pose=pool.author.blend_pose(entry,pose,weight)
                props={key:entry_props[key]+(value-entry_props[key])*weight for key,value in props.items()}
            records.append((pose,props))
        previous=segment
    last,last_props=records[-1]
    for index in range(1,recipe['neutral_out_frames']+1):
        weight=pool.author.smooth(index/recipe['neutral_out_frames'])
        records.append((pool.author.blend_pose(last,neutral,weight),
                        {key:value+(neutral_props[key]-value)*weight for key,value in last_props.items()}))
    assert len(records)==recipe['frames']
    for frame,(pose,props) in enumerate(records,1):
        for accent in recipe.get('accents',recipe.get('pose_accents',[])):
            bone,axis=accent['bone'],accent['axis'].upper()
            if 'forearm' in bone:
                assert axis=='Z','Preserve the native elbow hinge'
            degrees=pool.author.remap(frame,accent['keys'])
            assert abs(degrees)<=20.001,(recipe['name'],bone,degrees)
            loc,rot,scale=pose[bone]
            vector=world_axes[(bone,axis)] if axis.startswith('WORLD_') else {'X':(1,0,0),'Y':(0,1,0),'Z':(0,0,1)}[axis]
            pose[bone]=(loc,rot@Quaternion(vector,math.radians(degrees)),scale)
        assert all(math.isfinite(value) for transform in pose.values() for component in transform for value in component)
    for name in neutral:
        assert (records[0][0][name][0]-records[-1][0][name][0]).length<1e-6
        assert records[0][0][name][1].rotation_difference(records[-1][0][name][1]).angle<1e-5
    return records


def main():
    assert bpy.app.background and not OUTPUT.exists()
    if hasattr(recipes,'validate_recipes'):
        assert not recipes.validate_recipes(),recipes.validate_recipes()
    old_report=json.loads(INPUT_REPORT.read_text(encoding='utf8'))
    input_hash,source_hash=pool.author.sha(INPUT),pool.author.sha(pool.SOURCE)
    assert old_report['passed'] and old_report['blend_sha256']==input_hash
    bpy.ops.wm.open_mainfile(filepath=str(INPUT))
    scene,rig,mesh=bpy.context.scene,bpy.data.objects['rig'],bpy.data.objects['SM_NutWizard']
    defaults=pool.defaults(rig)
    retained=[dict(row) for row in old_report['clips'] if not excluded(row['name'])]
    removed=[row for row in old_report['clips'] if excluded(row['name'])]
    assert len(retained)==27
    preserved={row['action']:pool.signature(bpy.data.actions[row['action']]) for row in retained}
    originals={name:bpy.data.actions['NUT_W_'+name] for name in ('Firenut','CastSummon','NutRain')}
    original_signatures={name:pool.signature(action) for name,action in originals.items()}
    rest={b.name:tuple(v for row in b.matrix_local for v in row) for b in rig.data.bones}
    paths={fc.data_path for action in originals.values() for fc in pool.author.curves(action)}
    accent_names={accent['bone'] for recipe in recipes.RECIPES for accent in recipe.get('accents',recipe.get('pose_accents',[]))}
    names=sorted(set(old_report['names'])|accent_names)
    properties=[re.match(r'pose\.bones\["([^"]+)"\]\["([^"]+)"\]',p).groups() for p in sorted(paths) if p.endswith(']')]
    upper={name for name in names if not any(w in name for w in LOWER_WORDS)}
    cache={}

    def sample(source,frame):
        key=source,round(frame,7)
        if key not in cache:
            pool.author.bind(rig,originals[source],defaults)
            scene.frame_set(math.floor(frame),subframe=frame-math.floor(frame))
            cache[key]=({name:rig.pose.bones[name].matrix_basis.decompose() for name in names},
                        {(name,prop):float(rig.pose.bones[name][prop]) for name,prop in properties})
        return cache[key]

    neutral,neutral_props=sample('Firenut',1.)
    # Directional reactions hold Firenut1. Convert their requested world axes
    # through the actual evaluated native control orientation, rather than
    # guessing an FK axis from the rest rig. Feet keep their original IK pose.
    pool.author.bind(rig,originals['Firenut'],defaults)
    scene.frame_set(1)
    world_axes={}
    for recipe in recipes.RECIPES:
        for accent in recipe['accents']:
            bone,axis=accent['bone'],accent['axis']
            if axis.startswith('WORLD_'):
                world=Vector({'WORLD_X':(1,0,0),'WORLD_Y':(0,1,0),'WORLD_Z':(0,0,1)}[axis])
                world_axes[(bone,axis)]=(rig.matrix_world@rig.pose.bones[bone].matrix).to_quaternion().inverted()@world
    catalog=[]
    for number,recipe in enumerate(recipes.RECIPES,1):
        assert not excluded(recipe['name'])
        records=compose(recipe,sample,neutral,neutral_props,upper,world_axes)
        action=bpy.data.actions.new('MC_SINGLE_'+recipe['name'])
        action.use_fake_user=True
        for key in ('labelRU','groupRU','useRU'):
            action[key]=recipe[key]
        action['frame_start'],action['frame_end'],action['duration_seconds']=1,recipe['frames'],recipe['frames']/30
        action['source_recipe_json']=json.dumps(recipe,ensure_ascii=False)
        pool.bake(rig,action,defaults,records)
        catalog.append(dict(recipe,ordinal=number,action=action.name,curves=len(pool.author.curves(action)),new_motion=True))
        print('UTILITY_ACTION_READY',number,recipe['name'],recipe['frames'],flush=True)
    catalog += [dict(row,ordinal=len(catalog)+index,new_motion=False) for index,row in enumerate(retained,1)]
    # The camera stays identical to the previous review so old rendered clips
    # can be reused. Check every authored pose against its actual fixed view.
    camera=bpy.data.objects['MC_ReviewCamera_ThreeQuarter']
    scale=camera.data.ortho_scale
    inverse=camera.matrix_world.inverted()
    aspect=scene.render.resolution_x/scene.render.resolution_y
    required=0.
    for row in catalog[:len(recipes.RECIPES)]:
        pool.author.bind(rig,bpy.data.actions[row['action']],defaults)
        for frame in range(1,row['frames']+1):
            scene.frame_set(frame)
            evaluated=mesh.evaluated_get(bpy.context.evaluated_depsgraph_get())
            for corner in evaluated.bound_box:
                local=inverse@(evaluated.matrix_world@Vector(corner))
                required=max(required,2*abs(local.x)/.94,2*abs(local.y)*aspect/.94)
    assert required<=scale,(required,scale,'A new pose needs smaller accents or a separate review view')
    for name,signature in preserved.items():
        assert pool.signature(bpy.data.actions[name])==signature,name
    assert all(pool.signature(action)==original_signatures[name] for name,action in originals.items())
    assert rest=={b.name:tuple(v for row in b.matrix_local for v in row) for b in rig.data.bones}
    pool.author.bind(rig,None,defaults)
    for track in list(rig.animation_data.nla_tracks):
        if track.name.startswith('MC_POOL_'):
            rig.animation_data.nla_tracks.remove(track)
        else:
            track.mute=True
    for row in removed:
        bpy.data.actions.remove(bpy.data.actions[row['action']])
    track=rig.animation_data.nla_tracks.new()
    track.name='MC_SINGLE_ALL_PREVIEW'
    scene.timeline_markers.clear()
    cursor=1
    for row in catalog:
        action=bpy.data.actions[row['action']]
        strip=track.strips.new('%02d %s'%(row['ordinal'],row['name']),cursor,action)
        strip.action_frame_start,strip.action_frame_end=1,row['frames']+1
        strip.frame_end=cursor+row['frames']
        strip.blend_type,strip.extrapolation='REPLACE','NOTHING'
        strip.blend_in,strip.blend_out=0,0
        strip.use_auto_blend=False
        strip.action_slot=action.slots[0]
        scene.timeline_markers.new('%02d %s'%(row['ordinal'],row['labelRU'][:24]),frame=cursor)
        row['preview_start'],row['preview_end']=cursor,cursor+row['frames']-1
        cursor+=row['frames']
    rig.animation_data.use_nla=True
    track.mute=False
    scene.frame_start,scene.frame_end=1,cursor-1
    scene.frame_set(1)
    scene.camera=camera
    old_notes=bpy.data.texts.get('MIXAMO_POOL_README')
    if old_notes:
        bpy.data.texts.remove(old_notes)
    readme=bpy.data.texts.new('SINGLE_ACTIONS_README')
    readme.write('Single-purpose Wizard studies based on the approved artist/Mixamo rig and motions.\n'
                 'New motions first, then 27 preserved approved single-action studies.\n'
                 'Combo sequences and Fire_DoublePulse are removed from this file.\n'
                 'NLA MC_SINGLE_ALL_PREVIEW plays all clips; mute it to edit individual Actions.\n'
                 '30fps authoring; review uses actual Blender half-frame samples at60fps.\n'
                 'Teleport clips are body gestures for disappear/appear cues, without preview VFX.\n\n')
    for row in catalog:
        readme.write('%02d. %s / %s\n%s\n\n'%(row['ordinal'],row['action'],row['labelRU'],row['useRU']))
    DEST.mkdir(parents=True,exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=str(OUTPUT))
    assert pool.author.sha(INPUT)==input_hash and pool.author.sha(pool.SOURCE)==source_hash
    report=dict(passed=True,source=str(pool.SOURCE),source_sha256=source_hash,parent_blend=str(INPUT),
        parent_blend_sha256=input_hash,blend=str(OUTPUT),blend_sha256=pool.author.sha(OUTPUT),
        source_fps=30,clip_count=len(catalog),new_clip_count=len(recipes.RECIPES),retained_clip_count=len(retained),
        retained_names=[row['name']for row in retained],removed_names=[row['name']for row in removed],
        retained_actions_unchanged=True,original_actions_preserved=True,rig_and_rest_preserved=True,
        source_file_preserved=True,neutral_endpoints_match=True,ortho_scale=scale,
        required_ortho_scale=required,timeline_frames=cursor-1,duration_seconds=(cursor-1)/30,
        names=names,clips=catalog,scope='Blender-only single-purpose animation studies; no Unreal import.',
        limitations=['Derived gestures using the approved three artist motions and bounded local pose edits.',
                     'Teleport cues are gesture timing markers; visibility/VFX and gameplay slots are not integrated.'])
    REPORT.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
    print('WIZARD_SINGLE_ACTIONS_READY',str(OUTPUT),flush=True)


if __name__=='__main__':
    main()
