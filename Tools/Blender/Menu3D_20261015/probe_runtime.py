import bpy
import json
from pathlib import Path
report = {'version':bpy.app.version_string, 'binary':bpy.app.binary_path, 'cycles_devices':[]}
prefs = bpy.context.preferences.addons.get('cycles')
if prefs:
    p = prefs.preferences
    for backend in ['OPTIX','CUDA','HIP','ONEAPI']:
        try:
            p.compute_device_type = backend
            p.refresh_devices()
            report['cycles_devices'].append({'backend':backend,'devices':[{'name':d.name,'type':d.type,'id':d.id} for d in p.devices]})
        except Exception as e:
            report['cycles_devices'].append({'backend':backend,'error':str(e)})
report['principled_inputs'] = []
mat = bpy.data.materials.new('ProbeOnly')
mat.use_nodes = True
report['principled_inputs'] = [s.name for s in mat.node_tree.nodes.get('Principled BSDF').inputs]
path = Path(r'E:\DEVGAME\MessControl\ArtSource\UI\MainMenu3D\20261015_refB_v1\runtime.json')
path.write_text(json.dumps(report,indent=2),encoding='utf-8')
print(json.dumps(report))
