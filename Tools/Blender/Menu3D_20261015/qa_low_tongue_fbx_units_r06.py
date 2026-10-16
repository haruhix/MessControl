"""Read binary FBX unit/node data directly, without a DCC unit roundtrip."""
from pathlib import Path
import json
import numpy as np
from io_scene_fbx import parse_fbx

P = Path('E:/DEVGAME/MessControl/ArtSource/UI/MainMenu3D/20261015_refB_v1/Assembly/UnrealHandoff/v08_ReferenceFinish/Interior/UV_r06_Refine03_Candidate01')


def child(e, name):
    return next((c for c in e.elems if c.id == name), None)


def props(e):
    block = child(e, b'Properties70')
    return {p.props[0].decode():[v.decode(errors='replace') if isinstance(v,bytes) else v for v in p.props[4:]] for p in block.elems if p.id == b'P'} if block else {}


report = {'method':'Direct binary FBX parse, no DCC unit conversion','files':[]}
for name in ['SM_DAY03_Interior.fbx','SM_DAY03_Tongue.fbx']:
    root,version = parse_fbx.parse(str(P/name))
    settings = props(child(root,b'GlobalSettings'))
    objects = child(root,b'Objects')
    record = {'path':str(P/name),'version':version,'global_settings':settings,'models':[],'geometries':[]}
    for item in objects.elems:
        if item.id == b'Model':
            record['models'].append({'name':item.props[1].decode(),'properties':props(item)})
        if item.id == b'Geometry':
            vertices = np.asarray(child(item,b'Vertices').props[0]).reshape(-1,3)
            record['geometries'].append({'name':item.props[1].decode(),'vertices':len(vertices),'raw_min':vertices.min(0).tolist(),'raw_max':vertices.max(0).tolist()})
    assert settings['UnitScaleFactor'] == [1.]
    assert len(record['models']) == 1 and record['models'][0]['properties']['Lcl Scaling'] == [100.,100.,100.]
    report['files'].append(record)
report['pass'] = True
report['contract'] = 'Source identity objects in metres; FBX raw vertices retain metre-sized values, cm metadata UnitScaleFactor1 and ModelLclScaling100. Use existing native TransformVertexToAbsoluteTrue once; importuniformscale1, no extra100.'
(P/'QA_Independent/raw_fbx_units.json').write_text(json.dumps(report,indent=2))
print('R06_RAW_FBX_UNIT_PASS',json.dumps(report))
