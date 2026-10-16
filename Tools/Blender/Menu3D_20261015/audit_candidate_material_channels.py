"""Inspect actual embedded GLB pixel channels against the authored map binding."""
import argparse
import json
from pathlib import Path
import struct

import cv2
import numpy as np

p = argparse.ArgumentParser()
p.add_argument('--glb', required=True)
p.add_argument('--binding', required=True)
p.add_argument('--out', required=True)
opt = p.parse_args()
raw = Path(opt.glb).read_bytes()
length, _ = struct.unpack_from('<II',raw,12)
document = json.loads(raw[20:20+length])
binary = memoryview(raw)[28+length:]
binding = json.loads(Path(opt.binding).read_text(encoding='utf8'))
binding = binding.get('materials',binding)

def decode(data):
    arr = cv2.imdecode(np.frombuffer(data,np.uint8),cv2.IMREAD_UNCHANGED)
    if arr is None:
        raise RuntimeError('Cannot decode texture')
    depth = str(arr.dtype)
    if arr.ndim == 2:
        arr = np.repeat(arr[:,:,None],4,axis=2)
    else:
        arr = arr[:,:,[2,1,0,3] if arr.shape[2]==4 else [2,1,0]]
    arr = arr.astype(np.float64)/(65535 if depth=='uint16' else 255)
    if arr.shape[2]==3:
        arr=np.dstack([arr,np.ones(arr.shape[:2])])
    return arr,depth

def embedded(info):
    texture=document['textures'][info['index']]
    image=document['images'][texture['source']]
    view=document['bufferViews'][image['bufferView']]
    offset=view.get('byteOffset',0)
    return decode(binary[offset:offset+view['byteLength']])

report=[]
for material in document.get('materials',[]):
    name=material.get('name','')
    if name not in binding:
        continue
    maps=binding[name]
    physical=material.get('pbrMetallicRoughness',{})
    extensions=material.get('extensions',{})
    coat=extensions.get('KHR_materials_clearcoat',{})
    spec=extensions.get('KHR_materials_specular',{})
    fields=[('base_color',physical.get('baseColorTexture'),slice(0,3),slice(0,3)),
            ('roughness',physical.get('metallicRoughnessTexture'),1,0),
            ('coat_weight',coat.get('clearcoatTexture'),0,0),
            ('coat_roughness',coat.get('clearcoatRoughnessTexture'),1,0),
            ('specular_ior_level',spec.get('specularTexture'),3,3)]
    for channel,info,actual_component,source_component in fields:
        filename=maps.get(channel)
        if not filename:
            continue
        if not info:
            report.append({'material':name,'channel':channel,'pass':False,'error':'Declared map missing from actual GLB'})
            continue
        actual,actual_depth=embedded(info)
        source,source_depth=decode(Path(filename).read_bytes())
        if actual.shape!=source.shape:
            report.append({'material':name,'channel':channel,'pass':False,'error':'Image dimensions differ','actual':list(actual.shape),'source':list(source.shape)})
            continue
        difference=np.abs(actual[:,:,actual_component]-source[:,:,source_component])
        report.append({'material':name,'channel':channel,'actual_depth':actual_depth,'source_depth':source_depth,
                       'texCoord':info.get('texCoord',0),'max_encoded_error':float(difference.max()),
                       'mean_encoded_error':float(difference.mean()),'pass':bool(difference.max()<=1/255+1e-7)})
result={'glb':opt.glb,'binding':opt.binding,'channels':report,'pass':all(row['pass'] for row in report),
        'limit':'Checks stored map channels/quantization. Browser sampling, SSS and appearance need separate review.'}
Path(opt.out).write_text(json.dumps(result,indent=2),encoding='utf8')
print('ACTUAL_GLB_MATERIAL_CHANNELS',len(report),'pass',result['pass'])
if not result['pass']:
    raise SystemExit(1)
