"""Read-only OBJ-export/canonical UV comparison of current and source chest pairs.

Does not spawn, save packages or modify mesh descriptions. Quantized geometry
and position->UV corner hashes are independent of OBJ vertex/index ordering.
"""
import hashlib
import json
from pathlib import Path
import unreal as u

OUT=Path(globals().get('MC_CHEST_UV_OUTPUT',
    'E:/CodexWork/HubChestWardrobe_20261022/ChestUVComparison')).resolve()
assert OUT.drive.upper()=='E:'
assert not u.EditorLevelLibrary.get_pie_worlds(False)
OUT.mkdir(parents=True,exist_ok=True)

def digest(rows):
    return hashlib.sha256(json.dumps(sorted(rows),separators=(',',':')).encode()).hexdigest()

def obj_metrics(file):
    vertices,uvs,faces=[],[],[]
    for line in file.read_text(encoding='utf-8',errors='replace').splitlines():
        words=line.split()
        if not words:continue
        if words[0]=='v':vertices.append(tuple(round(float(x),3) for x in words[1:4]))
        elif words[0]=='vt':uvs.append(tuple(round(float(x),6) for x in words[1:3]))
        elif words[0]=='f':
            face=[]
            for token in words[1:]:
                indices=token.split('/')
                vertex=vertices[int(indices[0])-1]
                uv=uvs[int(indices[1])-1] if len(indices)>1 and indices[1] else None
                face.append((vertex,uv))
            for i in range(1,len(face)-1):faces.append([face[0],face[i],face[i+1]])
    geometry=[tuple(sorted(corner[0] for corner in face)) for face in faces]
    mapping=[tuple(sorted((corner[0],corner[1] or (0.,0.)) for corner in face)) for face in faces]
    return dict(vertices=len(vertices),uvs=len(uvs),triangles=len(faces),
        geometry_hash=digest(geometry),geometry_uv0_hash=digest(mapping),
        uv_range=[[min(t[a] for t in uvs),max(t[a] for t in uvs)] for a in [0,1]] if uvs else None)

report=dict(scope=__doc__,meshes={},comparisons={})
for part in ['Bot','Top']:
    for label,folder in [('Current','/Game/FromBlender'),('Source','/Game/Gameplay/Roguelike/ChestAuthored')]:
        asset=folder+'/SM_Chest_'+part
        mesh=u.load_asset(asset)
        assert isinstance(mesh,u.StaticMesh),asset
        output=OUT/(label+'_'+part+'.obj')
        task=u.AssetExportTask()
        task.set_editor_properties(dict(object=mesh,filename=str(output),automated=True,
            prompt=False,replace_identical=True,exporter=u.StaticMeshExporterOBJ()))
        assert u.Exporter.run_asset_export_task(task), 'OBJ export failed: '+asset
        assert output.exists()
        values=obj_metrics(output)
        values.update(asset=mesh.get_path_name(),file=str(output))
        report['meshes'][label+'_'+part]=values
    a,b=(report['meshes'][label+'_'+part] for label in ['Current','Source'])
    report['comparisons'][part]=dict(same_geometry=a['geometry_hash']==b['geometry_hash'],
        same_geometry_and_uv0=a['geometry_uv0_hash']==b['geometry_uv0_hash'])
(OUT/'Comparison.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
u.log('MC_CHEST_UV_COMPARISON '+str(OUT/'Comparison.json'))
