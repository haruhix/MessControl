"""Export-only normal parity repair on frozen v08 source, no geometry/UV edits."""
import bpy,runpy,json,hashlib
from pathlib import Path
assert bpy.app.background
p=Path('E:/DEVGAME/MessControl/ArtSource/UI/MainMenu3D/20261015_refB_v1/Assembly/UnrealHandoff/v08_ReferenceFinish')
r=json.loads((p/'producer_handoff_receipt.json').read_text());immutable=['MC_DAY03_UnrealMorphSource.blend','SK_DAY03.fbx','source_world_poses_cm.npz','texture_mesh_packet.npz','unreal_mouth_handoff.json','painter_uv_handoff.json']
for n in immutable:assert hashlib.sha256((p/n).read_bytes()).hexdigest()==r['files'][n]['sha256'],n
bpy.ops.wm.open_mainfile(filepath=str(p/'MC_DAY03_UnrealMorphSource.blend'))
m=json.loads((p/'unreal_mouth_handoff.json').read_text());h=runpy.run_path(str(Path(__file__).with_name('day03_painter_uv_v08.py')));h['export_static_obj'](bpy.data.objects['SK_DAY03'],m['parts'],p)
n='MC_DAY03_Painter_Open.obj';r['files'][n]={'bytes':(p/n).stat().st_size,'sha256':hashlib.sha256((p/n).read_bytes()).hexdigest()};r['static_obj_normals']='Explicit Blender per-loop corner normals, f v/vt/vn, s1. Geometry/UV/source/FBX unchanged.'
for n in immutable:assert hashlib.sha256((p/n).read_bytes()).hexdigest()==r['files'][n]['sha256'],n
(p/'producer_handoff_receipt.json').write_text(json.dumps(r,indent=2));print('OBJ_NORMAL_PARITY_REPAIRED',json.dumps(r['files']['MC_DAY03_Painter_Open.obj']))
