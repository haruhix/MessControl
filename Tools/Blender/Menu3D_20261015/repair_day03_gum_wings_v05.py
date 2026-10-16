"""Bounded DAY03 lateral gingiva repair on the already baked native five-pose mesh.
Keeps topology, tooth coordinates, UV0, rig and morph names. Does not touch GUI.
"""
import bpy, numpy as np, json, hashlib, math
from pathlib import Path
from mathutils import Vector
assert bpy.app.background
BASE=Path('E:/DEVGAME/MessControl/ArtSource/UI/MainMenu3D/20261015_refB_v1')
SRC=BASE/'Assembly/UnrealHandoff/v04_SeatedSymmetricNative'
OUT=BASE/'Assembly/UnrealHandoff/v05_GumWidthRepair'
OUT.mkdir(parents=True,exist_ok=True)
assert not (OUT/'SK_DAY03.fbx').exists(), 'Use a fresh output revision'
meta=json.loads((SRC/'unreal_mouth_handoff.json').read_text())
bpy.ops.wm.open_mainfile(filepath=str(SRC/'MC_DAY03_UnrealMorphSource.blend'))
o=bpy.data.objects['SK_DAY03']; me=o.data
n=len(me.vertices); parts=meta['parts']; gums=[p for p in parts if '_Gum' in p['mesh']]
assert len(gums)==2 and len(parts)==34
keys=o.data.shape_keys.key_blocks
assert [k.name for k in keys]==['Basis','Pose_000','Pose_025','Pose_050','Pose_075']
old={}
for key in keys:
 a=np.empty(n*3,np.float32); key.data.foreach_get('co',a);old[key.name]=a.reshape(-1,3)
uv=np.empty(len(me.uv_layers['UV0'].data)*2,np.float32);me.uv_layers['UV0'].data.foreach_get('uv',uv)
uv_hash=hashlib.sha256(uv.tobytes()).hexdigest()
tooth_mask=np.ones(n,bool)
for p in gums:tooth_mask[p['vertex_start']:p['vertex_start']+p['vertex_count']]=False
report={'version':bpy.app.version_string,'source':str(SRC/'MC_DAY03_UnrealMorphSource.blend'),'source_sha256':hashlib.sha256((SRC/'MC_DAY03_UnrealMorphSource.blend').read_bytes()).hexdigest(),'scope':'Two gum ranges only; five baked native poses; no tooth or UV edits','target_abs_x':8.7,'start_abs_x':8.3,'rounding':'Saturating smooth lateral remap; distal tissue thins toward its tooth-facing rim and recedes in depth','poses':{},'parts':gums}
new={}
for name,arr in old.items():
 a=arr.copy(); info=[]
 for p in gums:
  sl=slice(p['vertex_start'],p['vertex_start']+p['vertex_count']);g=arr[sl].astype(np.float64); b=g.copy();ax=np.abs(g[:,0]);mask=ax>8.3
  # Smooth, monotone lateral saturation. Points at/inside 8.3 are exact original.
  span=1.34 if name.startswith('Pose_0') and name!='Pose_000' else 1.300001
  norm=np.tanh(span/.4)
  b[mask,0]=np.sign(g[mask,0])*(8.3+.4*np.tanh((ax[mask]-8.3)/.4)/norm)
  b[mask,0]=np.clip(b[mask,0],-8.7,8.7)
  t=np.clip((ax-8.3)/1.3,0,1); t=t*t*(3-2*t)
  # Preserve the tooth-facing edge; remove the long square far-side bulk.
  lateral=ax>8.7
  anchor=float(g[lateral,2].min() if 'Upper' in p['mesh'] else g[lateral,2].max())
  b[:,2]=anchor+(g[:,2]-anchor)*(1.-.68*t*t)
  b[:,1]=g[:,1]-.16*t*t
  a[sl]=b.astype(np.float32)
  info.append({'part':p['mesh'],'before_min':g.min(0).tolist(),'before_max':g.max(0).tolist(),'after_min':b.min(0).tolist(),'after_max':b.max(0).tolist(),'anchor_z':anchor,'modified_vertices':int(mask.sum()),'max_shift':float(np.linalg.norm(b-g,axis=1).max())})
 assert np.array_equal(a[tooth_mask],arr[tooth_mask])
 new[name]=a; keys[name].data.foreach_set('co',a.ravel())
 report['poses'][name]={'gums':info,'tooth_coordinates_exact':True,'tooth_sha256':hashlib.sha256(a[tooth_mask].tobytes()).hexdigest(),'center_region_exact':bool(np.array_equal(a[np.abs(arr[:,0])<=8.3],arr[np.abs(arr[:,0])<=8.3]))}
me.vertices.foreach_set('co',new['Basis'].ravel());me.update()
uv2=np.empty_like(uv);me.uv_layers['UV0'].data.foreach_get('uv',uv2);assert np.array_equal(uv,uv2)
report.update({'vertex_count':n,'triangles':sum(len(p.vertices)-2 for p in me.polygons),'uv0_sha256':uv_hash,'uv0_exact':True,'hierarchy':[(x.name,x.type,x.parent.name if x.parent else None) for x in bpy.data.objects],'morphs':[k.name for k in keys],'native_y_flip':True,'source_units':'meters, FBX native conversion to centimeters; no geometry x100','validation':'Source geometry gates and Blender clay renders; Unreal import/runtime validation pending'})
# Save authoritative source and export before replacing materials for preview.
for key in keys:key.value=0.
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'MC_DAY03_UnrealMorphSource.blend'))
bpy.ops.object.select_all(action='DESELECT');o.select_set(True);rig=bpy.data.objects['MC_MenuMouth_Rig'];rig.select_set(True);bpy.context.view_layer.objects.active=o
bpy.ops.export_scene.fbx(filepath=str(OUT/'SK_DAY03.fbx'),use_selection=True,object_types={'MESH','ARMATURE'},use_mesh_modifiers=False,add_leaf_bones=False,use_armature_deform_only=True,bake_anim=False,path_mode='STRIP',axis_forward='-Y',axis_up='Z',apply_unit_scale=False,apply_scale_options='FBX_SCALE_NONE',mesh_smooth_type='FACE',use_tspace=False)
np.savez_compressed(OUT/'source_world_poses_cm.npz',**{('1.0' if k=='Basis' else str({'Pose_000':0.,'Pose_025':.25,'Pose_050':.5,'Pose_075':.75}[k])):v*100. for k,v in new.items()})
meta.update({'source':str(SRC/'MC_DAY03_UnrealMorphSource.blend'),'source_sha256':report['source_sha256'],'repair_source':str(OUT/'MC_DAY03_UnrealMorphSource.blend'),'repair':'v05 lateral gingiva only; teeth/UV/topology/five poses preserved','bounds_open_cm':[(new['Basis'].min(0)*100).tolist(),(new['Basis'].max(0)*100).tolist()],'interior':'Unchanged; reuse v04 static assets'})
(OUT/'unreal_mouth_handoff.json').write_text(json.dumps(meta,indent=2))
report['fbx_bytes']=(OUT/'SK_DAY03.fbx').stat().st_size
(OUT/'gum_width_repair_report.json').write_text(json.dumps(report,indent=2))
# Fast actual Blender clay evidence, independent of native framing/clip materials.
scene=bpy.context.scene
for i,m in enumerate(me.materials):
 m.use_nodes=True;bs=m.node_tree.nodes.get('Principled BSDF');bs.inputs['Base Color'].default_value=((.68,.73,.74,1) if i==0 else (.24,.33,.34,1));bs.inputs['Roughness'].default_value=.32
world=bpy.data.worlds.new('ReviewWorld') if not scene.world else scene.world;scene.world=world;world.use_nodes=True;world.node_tree.nodes['Background'].inputs[0].default_value=(.025,.03,.035,1);world.node_tree.nodes['Background'].inputs[1].default_value=.4
cd=bpy.data.cameras.new('GEO-ReviewCamera');cam=bpy.data.objects.new('GEO-ReviewCamera',cd);scene.collection.objects.link(cam);cam.location=(0,35,0);cam.rotation_euler=(Vector((0,0,0))-cam.location).to_track_quat('-Z','Y').to_euler();cd.type='ORTHO';cd.ortho_scale=21.6;scene.camera=cam
for name,loc,power,size in [('Key',(-7,12,9),1500,9),('Fill',(9,7,2),900,8),('Rim',(0,-5,8),1400,7)]:
 ld=bpy.data.lights.new('GEO-'+name,'AREA');lo=bpy.data.objects.new('GEO-'+name,ld);scene.collection.objects.link(lo);lo.location=loc;lo.rotation_euler=(-lo.location).to_track_quat('-Z','Y').to_euler();ld.energy=power;ld.shape='DISK';ld.size=size
scene.render.engine='BLENDER_EEVEE';scene.render.resolution_x=1280;scene.render.resolution_y=720;scene.render.resolution_percentage=100;scene.render.image_settings.file_format='PNG';scene.view_settings.view_transform='AgX'
for revision,arrays in [('before',old),('after',new)]:
 for name in arrays:keys[name].data.foreach_set('co',arrays[name].ravel())
 me.vertices.foreach_set('co',arrays['Basis'].ravel());me.update()
 for pose in ('open','closed'):
  for k in keys:k.value=1. if pose=='closed' and k.name=='Pose_000' else 0.
  scene.render.filepath=str(OUT/(revision+'_'+pose+'.png'));bpy.ops.render.render(write_still=True)
print('GUM_WIDTH_REPAIR_COMPLETE',json.dumps({'output':str(OUT),'fbx_bytes':report['fbx_bytes'],'all_tooth_coordinates_exact':True,'uv0_exact':True}))
