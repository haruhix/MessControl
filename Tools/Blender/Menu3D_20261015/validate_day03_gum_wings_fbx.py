import bpy,numpy as np,json,hashlib
from pathlib import Path
assert bpy.app.background
p=Path('E:/DEVGAME/MessControl/ArtSource/UI/MainMenu3D/20261015_refB_v1/Assembly/UnrealHandoff/v05_GumWidthRepair')
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.fbx(filepath=str(p/'SK_DAY03.fbx'),use_anim=False)
meshes=[o for o in bpy.data.objects if o.type=='MESH'];arms=[o for o in bpy.data.objects if o.type=='ARMATURE'];assert len(meshes)==1 and len(arms)==1
ob=meshes[0];keys=ob.data.shape_keys.key_blocks
assert [k.name for k in keys]==['Basis','Pose_000','Pose_025','Pose_050','Pose_075']
assert len(ob.data.vertices)==274798
assert [b.name for b in arms[0].data.bones]==['root']
r={'version':bpy.app.version_string,'fbx':str(p/'SK_DAY03.fbx'),'fbx_sha256':hashlib.sha256((p/'SK_DAY03.fbx').read_bytes()).hexdigest(),'vertices':len(ob.data.vertices),'triangles':sum(len(f.vertices)-2 for f in ob.data.polygons),'bones':[b.name for b in arms[0].data.bones],'morphs':[k.name for k in keys],'uv_layers':[l.name for l in ob.data.uv_layers],'all_vertex_weights_root':all(len(v.groups)==1 and abs(v.groups[0].weight-1)<1e-6 for v in ob.data.vertices),'preview_images':['before_open.png','before_closed.png','after_open.png','after_closed.png'],'unreal_validation':'pending parent import and native PIE inspection'}
assert r['all_vertex_weights_root']
(p/'fresh_fbx_validation.json').write_text(json.dumps(r,indent=2));print(json.dumps(r))
