"""Bounded native-card readability repair; v06 geometry and Closed remain frozen.

All lower crowns and gum receive sourceZ +=.8*OpenAmount. This changes only the
lower-jaw motion path; it never stretches a crown or moves the upper jaw. The
five original morph samples and exact native FBX contract are retained.
"""
import bpy,numpy as np,json,hashlib
from pathlib import Path
from mathutils import Vector
assert bpy.app.background
BASE=Path('E:/DEVGAME/MessControl/ArtSource/UI/MainMenu3D/20261015_refB_v1/Assembly/UnrealHandoff')
SRC=BASE/'v06_SymmetricRemodel';OUT=BASE/'v07_LowerRowReadability';OUT.mkdir(parents=True,exist_ok=True)
source=SRC/'MC_DAY03_UnrealMorphSource.blend';source_hash=hashlib.sha256(source.read_bytes()).hexdigest()
assert source_hash=='d1d81a3d36070367a6539abc488212a9f67678c40e0878473e4d2d8bc0dad0f2'
meta=json.loads((SRC/'unreal_mouth_handoff.json').read_text())
bpy.ops.wm.open_mainfile(filepath=str(source));o=bpy.data.objects['SK_DAY03'];me=o.data;keys=me.shape_keys.key_blocks;n=len(me.vertices)
assert [k.name for k in keys]==['Basis','Pose_000','Pose_025','Pose_050','Pose_075']
amounts={'Basis':1.,'Pose_000':0.,'Pose_025':.25,'Pose_050':.5,'Pose_075':.75}
old={};new={};lower=np.zeros(n,bool)
for p in meta['parts']:
 if '_Lower_' in p['mesh']:lower[p['vertex_start']:p['vertex_start']+p['vertex_count']]=True
assert len([p for p in meta['parts'] if '_Lower_' in p['mesh']])==17
uv=np.empty(len(me.loops)*2,np.float32);me.uv_layers['UV0'].data.foreach_get('uv',uv);uv_hash=hashlib.sha256(uv.tobytes()).hexdigest()
checks={}
for k in keys:
 v=np.empty(n*3,np.float32);k.data.foreach_get('co',v);v=v.reshape(-1,3);old[k.name]=v.copy();v[lower,2]+=.8*amounts[k.name]
 assert np.array_equal(v[~lower],old[k.name][~lower]);assert np.array_equal(v[:,:2],old[k.name][:,:2])
 if k.name=='Pose_000':assert np.array_equal(v,old[k.name])
 expected=old[k.name].copy();expected[lower,2]+=.8*amounts[k.name];assert np.array_equal(v,expected)
 new[k.name]=v;k.data.foreach_set('co',v.ravel());k.value=0.
 checks[k.name]={'open_amount':amounts[k.name],'lower_z_lift':.8*amounts[k.name],'upper_exact':True,'all_xy_exact':True,'closed_all_vertices_exact':k.name=='Pose_000','only_declared_lower_z_changed':True}
me.vertices.foreach_set('co',new['Basis'].ravel());me.update()
uv_after=np.empty_like(uv);me.uv_layers['UV0'].data.foreach_get('uv',uv_after);assert np.array_equal(uv,uv_after)
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'MC_DAY03_UnrealMorphSource.blend'))
bpy.ops.object.select_all(action='DESELECT');o.select_set(True);rig=bpy.data.objects['MC_MenuMouth_Rig'];rig.select_set(True);bpy.context.view_layer.objects.active=o
bpy.ops.export_scene.fbx(filepath=str(OUT/'SK_DAY03.fbx'),use_selection=True,object_types={'MESH','ARMATURE'},use_mesh_modifiers=False,add_leaf_bones=False,use_armature_deform_only=True,bake_anim=False,path_mode='STRIP',axis_forward='-Y',axis_up='Z',apply_unit_scale=False,apply_scale_options='FBX_SCALE_NONE',mesh_smooth_type='FACE',use_tspace=False)
poses={amounts[k]:v for k,v in new.items()};np.savez_compressed(OUT/'source_world_poses_cm.npz',**{str(k):v*100 for k,v in poses.items()})
meta.update({'parent_source':str(source),'parent_source_sha256':source_hash,'source':str(OUT/'MC_DAY03_UnrealMorphSource.blend'),'source_sha256':hashlib.sha256((OUT/'MC_DAY03_UnrealMorphSource.blend').read_bytes()).hexdigest(),'status':'v07 lower-jaw motion repair, independent native validation pending','repair':'All17 lower parts Z +=.8*OpenAmount; Closed,upper,XY,UV,topology,rig unchanged.','lower_readability_checks':checks,'uv0_sha256':uv_hash,'bounds_open_cm':[(new['Basis'].min(0)*100).tolist(),(new['Basis'].max(0)*100).tolist()]})
# Fang landmarks are unchanged upper data; lower central seam X is unchanged.
meta['bounds_each_pose_cm']={str(k):{'min':(v.min(0)*100).tolist(),'max':(v.max(0)*100).tolist()} for k,v in poses.items()}
(OUT/'unreal_mouth_handoff.json').write_text(json.dumps(meta,indent=2))
receipt={'parent_source':str(source),'parent_source_sha256':source_hash,'checks':checks,'uv0_exact':True,'uv0_sha256':uv_hash,'lower_vertices':int(lower.sum()),'bounds_each_pose_cm':meta['bounds_each_pose_cm'],'files':{name:{'bytes':(OUT/name).stat().st_size,'sha256':hashlib.sha256((OUT/name).read_bytes()).hexdigest()} for name in ['MC_DAY03_UnrealMorphSource.blend','SK_DAY03.fbx','source_world_poses_cm.npz','unreal_mouth_handoff.json']},'validation':'Producer source invariants; independent freshFBX/native integration pending.'}
(OUT/'producer_handoff_receipt.json').write_text(json.dumps(receipt,indent=2))
assert hashlib.sha256(source.read_bytes()).hexdigest()==source_hash

# Same fixed presentation as v06; preview changes no saved source/material data.
scene=bpy.context.scene
for i,m in enumerate(me.materials):
 m.use_nodes=True;bs=m.node_tree.nodes.get('Principled BSDF');bs.inputs['Base Color'].default_value=(.68,.72,.73,1) if i==0 else (.28,.36,.38,1);bs.inputs['Roughness'].default_value=.38
w=bpy.data.worlds.new('GeometryReview');scene.world=w;w.use_nodes=True;w.node_tree.nodes['Background'].inputs[0].default_value=(.02,.025,.03,1);w.node_tree.nodes['Background'].inputs[1].default_value=.55
for name,loc,power,size in [('Front',(0,12,8),1600,10),('Left',(-9,8,3),1000,8),('Right',(9,8,3),1000,8),('Rim',(0,-7,8),1800,8)]:
 d=bpy.data.lights.new('GEO-Review'+name,'AREA');l=bpy.data.objects.new('GEO-Review'+name,d);scene.collection.objects.link(l);l.location=loc;l.rotation_euler=(-l.location).to_track_quat('-Z','Y').to_euler();d.energy=power;d.shape='DISK';d.size=size
cd=bpy.data.cameras.new('GEO-ReviewCamera');cam=bpy.data.objects.new('GEO-ReviewCamera',cd);scene.collection.objects.link(cam);scene.camera=cam
scene.render.engine='BLENDER_EEVEE';scene.render.resolution_x=1280;scene.render.resolution_y=720;scene.render.resolution_percentage=100;scene.render.image_settings.file_format='PNG';scene.view_settings.view_transform='AgX';scene.render.film_transparent=False
shots=[('front_closed',0.,(0,35,0),'ORTHO'),('front_open',1.,(0,35,0),'ORTHO')]+[('native_'+str(k).replace('.',''),k,(0,30,0),'PERSP') for k in (0.,.125,.25,.375,.5,.625,.75,.875,1.)]+[('oblique_closed',0.,(12,30,7),'PERSP'),('oblique_open',1.,(12,30,7),'PERSP')]
samples={'Pose_000':0.,'Pose_025':.25,'Pose_050':.5,'Pose_075':.75}
for name,amount,loc,typ in shots:
 for k in keys:k.value=max(0.,1.-abs(amount-samples[k.name])/.25) if k.name in samples else 0.
 cam.location=loc;cam.rotation_euler=(-cam.location).to_track_quat('-Z','Y').to_euler();cd.type=typ;cd.ortho_scale=19.5;cd.lens=49.5 if typ=='PERSP' else 50
 scene.render.filepath=str(OUT/(name+'.png'));bpy.ops.render.render(write_still=True)
print('LOWER_ROW_READABILITY_HANDOFF',json.dumps({'out':str(OUT),'source_sha256':receipt['files']['MC_DAY03_UnrealMorphSource.blend']['sha256'],'fbx_sha256':receipt['files']['SK_DAY03.fbx']['sha256'],'closed_and_upper_exact':True}))
