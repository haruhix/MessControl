import bpy,json
from pathlib import Path
from mathutils import Matrix,Vector
ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'ArtSource/CharacterAnimation/ToothHits_20261102'
rig=bpy.data.objects['rig'];weapon=bpy.data.objects['Cube.001']; reference=bpy.data.objects['Cube']
with bpy.data.libraries.load(str(ROOT/'ArtSource/CharacterGameplay/TeethGameplay.blend'),link=False) as (src,dst):
    dst.objects=src.objects
target=next(o for o in dst.objects if o and o.type=='ARMATURE')
bone=target.data.bones['hand_r'];chain=[]
while bone:
    chain.append(bone.name);bone=bone.parent
unit=reference.scale.x
# The imported game mesh uses a shifted origin (the bounds have identical
# dimensions). Map its pivot back to the original Blender object's origin.
ue=json.loads((OUT/'WeaponGameMeshInspection.json').read_text())
mins=[min(v.co[i] for v in reference.data.vertices)*unit*100 for i in range(3)]
maxs=[max(v.co[i] for v in reference.data.vertices)*unit*100 for i in range(3)]
flipped_min=[mins[0],-maxs[1],mins[2]]
offset=[ue['knife']['bounds']['min'][i]-flipped_min[i] for i in range(3)]
asset_pivot_in_blender=Vector((offset[0],-offset[1],offset[2]))
def plain(m):return [list(r) for r in m]
out={'chain':chain,'knife_base_scale':unit,'asset_pivot_offset_cm':offset,'clip':{}}
flip=Matrix.Diagonal((1,-1,1,1))
rig.animation_data.use_nla=False
for name in ['hit_combo','hit3']:
    act=bpy.data.actions[name];rig.animation_data.action=act;rig.animation_data.action_slot=next(s for s in act.slots if s.identifier=='OBrig')
    start,end=map(int,act.frame_range);bpy.context.scene.frame_set(start);bpy.context.view_layer.update(); evaluated=rig.evaluated_get(bpy.context.evaluated_depsgraph_get())
    root_offset=(evaluated.pose.bones['root.x'].matrix.translation-rig.data.bones['root.x'].matrix_local.translation).copy()*100
    rows=[];previous=None
    for frame in range(start,end+1):
        bpy.context.scene.frame_set(frame);bpy.context.view_layer.update();dg=bpy.context.evaluated_depsgraph_get(); evaluated=rig.evaluated_get(dg)
        ps=evaluated.pose.bones['hand.r'].matrix.copy();rs=rig.data.bones['hand.r'].matrix_local.copy();ps.translation*=100;rs.translation*=100
        th=ps@rs.inverted()@target.data.bones['hand_r'].matrix_local;th.translation-=root_offset
        wm=weapon.evaluated_get(dg).matrix_world.copy();wm.translation*=100;wm.translation-=root_offset
        # Mesh in Unreal was exported from the unparented Cube at its base scale.
        wm=wm@Matrix.Diagonal((1/unit,1/unit,1/unit,1))@Matrix.Translation(-asset_pivot_in_blender)
        rel=flip@th.inverted()@wm@flip
        loc,rot,scale=rel.decompose()
        # Unreal's attachment transforms multiply scale componentwise and do
        # not preserve the shear produced by Blender's rotated nonuniform
        # hand scale. Compensate per axis so the displayed prop dimensions
        # match the evaluated source rather than swapping its Y/Z growth.
        hand_scale=th.to_scale();source_scale=wm.to_scale()
        scale=Vector(tuple(source_scale[i]/hand_scale[i] if abs(hand_scale[i])>1e-8 else 1.0 for i in range(3)))
        if previous is not None and rot.dot(previous)<0:rot.negate()
        previous=rot.copy()
        rows.append({'frame':frame,'target_hand':plain(th),'weapon':plain(wm),'weapon_component_scale':list(wm.to_scale()),
                     'relative':{'location':list(loc),'quaternion':[rot.x,rot.y,rot.z,rot.w],'scale':list(scale)}})
    out['clip'][name]=rows
(OUT/'WeaponReference.json').write_text(json.dumps(out,indent=2))
print('WEAPON_REFERENCE_SAMPLE',json.dumps({'chain':chain,'frames':{k:len(v) for k,v in out['clip'].items()}}))

