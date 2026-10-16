"""Actual isolated reflection contribution; lead-owned serialized DCC slot."""
import bpy
import json
from pathlib import Path

ROOT=Path(r'E:\DEVGAME\MessControl\ArtSource\UI\MainMenu3D\20261015_refB_v1')
bpy.ops.wm.open_mainfile(filepath=str(ROOT/'Assembly/MC_Menu3D_Candidate_Combined_cycles_lobes_v14.blend'))
scene=bpy.context.scene
out=ROOT/'Assembly/Review/SpecularDiagnostic'
out.mkdir(parents=True,exist_ok=True)
report=[]
for obj in scene.objects:
    if obj.type!='LIGHT':continue
    spec='CyclesGlossy' in obj.name
    obj.data.energy=obj.data.energy*8 if spec else 0
    report.append({'name':obj.name,'energy':obj.data.energy,'visible_diffuse':obj.visible_diffuse,
                   'visible_glossy':obj.visible_glossy,'position':list(obj.location),
                   'size':obj.data.size,'size_y':getattr(obj.data,'size_y',0),
                   'shadow':obj.data.use_shadow})
scene.world.node_tree.nodes['Background'].inputs[1].default_value=0
scene.cycles.samples=64
prefs=bpy.context.preferences.addons['cycles'].preferences
prefs.compute_device_type='OPTIX';prefs.get_devices()
for d in prefs.devices:d.use=d.type=='OPTIX'
scene.cycles.device='GPU'
scene.render.filepath=str(out/'glossy_only_gain8.png')
bpy.ops.render.render(write_still=True)
(out/'lights.json').write_text(json.dumps(report,indent=2))
print('GLOSSY_DIAGNOSTIC_READY',out)
