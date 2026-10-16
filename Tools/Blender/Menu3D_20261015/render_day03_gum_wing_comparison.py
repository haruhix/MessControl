import bpy,numpy as np,json
from pathlib import Path
from mathutils import Vector
assert bpy.app.background
BASE=Path('E:/DEVGAME/MessControl/ArtSource/UI/MainMenu3D/20261015_refB_v1')
SRC=BASE/'Assembly/UnrealHandoff/v04_SeatedSymmetricNative'
OUT=BASE/'Assembly/UnrealHandoff/v05_GumWidthRepair'
bpy.ops.wm.open_mainfile(filepath=str(SRC/'MC_DAY03_UnrealMorphSource.blend'))
o=bpy.data.objects['SK_DAY03'];me=o.data;keys=me.shape_keys.key_blocks;n=len(me.vertices)
old={}
for k in keys:
 a=np.empty(n*3,np.float32);k.data.foreach_get('co',a);old[k.name]=a.reshape(-1,3)
z=np.load(OUT/'source_world_poses_cm.npz');new={'Basis':z['1.0']/100.,'Pose_000':z['0.0']/100.,'Pose_025':z['0.25']/100.,'Pose_050':z['0.5']/100.,'Pose_075':z['0.75']/100.}
scene=bpy.context.scene
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
