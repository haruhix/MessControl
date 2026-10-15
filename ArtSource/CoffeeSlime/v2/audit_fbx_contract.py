"""Read canonical FBX metadata; no import or asset mutation."""
import json
from pathlib import Path
from io_scene_fbx import parse_fbx

BASE=Path(__file__).resolve().parent
reports=[]
for path in sorted((BASE/'exports').glob('*.fbx')):
    tree,version=parse_fbx.parse(str(path))
    top={e.id:e for e in tree.elems}
    globals=next(e for e in top[b'GlobalSettings'].elems if e.id==b'Properties70')
    unit=next(p.props[-1] for p in globals.elems if p.props[0]==b'UnitScaleFactor')
    objects=top[b'Objects'].elems; models={e.props[0]:e for e in objects if e.id==b'Model'}
    roots={}
    for ident,model in models.items():
        name=model.props[1].split(b'\x00')[0].decode('utf-8')
        if name not in ('root','Armature'): continue
        props=next((e for e in model.elems if e.id==b'Properties70'),None)
        scaling=next((list(p.props[-3:]) for p in props.elems if p.props[0]==b'Lcl Scaling'),[1,1,1]) if props else [1,1,1]
        roots[name]=scaling
    assert abs(unit-1)<1e-6,(path.name,'not centimeter metadata',unit)
    assert 'root' in roots,(path.name,'no root')
    assert all(abs(v-1)<1e-8 for scale in roots.values() for v in scale),(path.name,roots)
    reports.append({'file':path.name,'fbx_version':version,'UnitScaleFactor_cm':unit,'root_and_wrapper_LclScaling':roots})
output=BASE/'audit'/'fbx_unit_contract.json'; output.write_text(json.dumps({'pass':True,'files':reports},indent=2))
print('FBX_UNIT_CONTRACT_PASS',len(reports),str(output))
