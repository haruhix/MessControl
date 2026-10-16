"""Resolve anatomy scalar ranges into actual portable PBR maps on CPU.

glTF does not preserve arbitrary Blender Map Range shader math. This candidate
binding uses direct images for coat and an exporter-supported .5*Alpha specular
link. No source texture/project is overwritten. Visual review remains required.
"""
import argparse
import copy
import hashlib
import json
from pathlib import Path

import numpy as np
from PIL import Image

p=argparse.ArgumentParser()
p.add_argument('--binding',required=True)
p.add_argument('--out',required=True)
args=p.parse_args()
source=Path(args.binding)
out=Path(args.out)
out.mkdir(parents=True,exist_ok=True)
binding=copy.deepcopy(json.loads(source.read_text(encoding='utf8')))
report={'schema':'portable_anatomy_scalar_maps.v1','status':'CANDIDATE_EXPORT_PARITY_REQUIRES_ACTUAL_GLB_REVIEW',
        'source_binding':str(source.resolve()),'source_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),
        'materials':{},'limitations':['Subsurface scattering is not represented by core glTF PBR.',
        'Specular IOR Level above0.5 is clamped to0.5 for normalized glTF specular intensity; actual candidate render review required.']}
for name,material in binding['materials'].items():
    if not material.get('wetness'):
        continue
    wet=np.asarray(Image.open(material['wetness']).convert('L'),dtype=np.float32)/255
    constants=material.get('shader_constants',{})
    coat_range=constants.get('coat_weight_range',[.03,.27])
    coat=coat_range[0]+(coat_range[1]-coat_range[0])*wet
    rough=np.full_like(wet,constants['coat_roughness']) if 'coat_roughness' in constants else .24-.14*wet
    spec_range=constants.get('specular_ior_level_range')
    spec=spec_range[0]+(spec_range[1]-spec_range[0])*wet if spec_range else np.full_like(wet,constants.get('specular_ior_level',.5))
    values={'coat_weight':coat,'coat_roughness':rough,'specular_ior_level':spec}
    entry={}
    for role,field in values.items():
        path=out/f'{name}_{role}.png'
        if role=='specular_ior_level':
            # Blender consumes .5*Alpha; glTF consumes Alpha with factor1.
            # The official exporter multiplies the recognized .5 factor by2.
            alpha=np.rint(np.clip(field*2,0,1)*255).astype('uint8')
            rgba=np.full((*alpha.shape,4),255,dtype='uint8');rgba[:,:,3]=alpha
            Image.fromarray(rgba).save(path)
            reconstruction=alpha/255*.5
        else:
            encoded=np.rint(np.clip(field,0,1)*255).astype('uint8')
            Image.fromarray(encoded).save(path)
            reconstruction=encoded/255
        material[role]=str(path.resolve())
        entry[role]={'path':str(path.resolve()),'sha256':hashlib.sha256(path.read_bytes()).hexdigest(),
                     'source_min':float(field.min()),'source_max':float(field.max()),
                     'maximum_quantization_or_clamp_error':float(np.max(np.abs(reconstruction-field)))}
    report['materials'][name]=entry
binding['status']='CANDIDATE_PORTABLE_SHADER_MAPS_NOT_VISUALLY_ACCEPTED'
binding['derived_export_maps']=str((out/'portable_maps_report.json').resolve())
(out/'material_binding_portable.json').write_text(json.dumps(binding,indent=2),encoding='utf8')
(out/'portable_maps_report.json').write_text(json.dumps(report,indent=2),encoding='utf8')
print(json.dumps({'binding':str((out/'material_binding_portable.json').resolve()),'materials':list(report['materials'])}))
