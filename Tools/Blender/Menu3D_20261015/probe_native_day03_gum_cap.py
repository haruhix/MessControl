import bpy,json,numpy as np
from pathlib import Path
p=Path('E:/DEVGAME/MessControl/ArtSource/UI/MainMenu3D/20261015_refB_v1/Assembly/UnrealHandoff/v04_SeatedSymmetricNative')
bpy.ops.wm.open_mainfile(filepath=str(p/'MC_DAY03_UnrealMorphSource.blend'))
o=bpy.data.objects['SK_DAY03']; print('VERSION',bpy.app.version_string, 'OBJECTS',[(x.name,x.type) for x in bpy.data.objects]); print('TRANSFORM',list(o.location),list(o.scale),list(o.rotation_euler))
h=json.loads((p/'unreal_mouth_handoff.json').read_text()); parts=[q for q in h['parts'] if 'Gum' in q['mesh']]
for k in o.data.shape_keys.key_blocks:
 a=np.empty(len(k.data)*3,dtype=np.float32); k.data.foreach_get('co',a);a=a.reshape(-1,3)
 for q in parts:
  b=a[q['vertex_start']:q['vertex_start']+q['vertex_count']]; b=b[b[:,0]>9.59];print('CAP',k.name,q['mesh'],b.shape,b.min(0).tolist(),b.max(0).tolist()); print('CAP_SAMPLE',b[::max(1,len(b)//8)].tolist())
