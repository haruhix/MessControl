"""Fresh-process FBX dimensions/topology check; never touches the live Blender scene."""
import bpy,json,os
from pathlib import Path
from mathutils import Vector
ROOT=Path(__file__).resolve().parents[3];SOURCE=ROOT/'ArtSource/CharacterCurrent/Cosmetics'
report=json.loads((SOURCE/'CosmeticsReport.json').read_text(encoding='utf8'));records=[]
for expected in report['items']:
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.fbx(filepath=str(SOURCE/(expected['mesh']+'.fbx')))
    meshes=[o for o in bpy.data.objects if o.type=='MESH'];assert len(meshes)==1
    o=meshes[0];points=[o.matrix_world@v.co for v in o.data.vertices]
    size=[(max(p[i] for p in points)-min(p[i] for p in points))*100 for i in range(3)]
    wanted=[b-a for a,b in zip(*expected['bounds_cm'])]
    assert all(abs(a-b)<.03 for a,b in zip(size,wanted)),(size,wanted)
    assert len(o.data.materials)==2
    records.append({'name':expected['mesh'],'dimensions_cm':size,'materials':len(o.data.materials),'fresh_import':'pass'})
out=Path('E:/CodexWork/HubSystems_20261023/Cosmetics/FreshImportValidation.json');out.write_text(json.dumps(records,indent=2),encoding='utf8')
print('MC_COSMETICS_FRESH_IMPORT_PASS',json.dumps(records))
