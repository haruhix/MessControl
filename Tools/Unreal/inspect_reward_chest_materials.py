"""Read-only saved/live chest material parameters and texture source metadata."""
import json
from pathlib import Path
import unreal as u

OUT=Path(globals().get('MC_CHEST_MATERIAL_OUTPUT',
    'E:/CodexWork/HubChestWardrobe_20261022/ChestMaterials')).resolve()
assert OUT.drive.upper()=='E:'
OUT.mkdir(parents=True,exist_ok=True)
E=u.MaterialEditingLibrary
report=[]
for asset in ['/Game/Art/Materials/Buster/MI_Chest',
        '/Game/Gameplay/VFX/RewardChest/MI_RewardChest_Glow']:
    m=u.load_asset(asset)
    if not m: continue
    row=dict(path=m.get_path_name(), parent=str(m.get_editor_property('parent')),scalars={},vectors={},textures={})
    for name in E.get_scalar_parameter_names(m):
        row['scalars'][str(name)]=E.get_material_instance_scalar_parameter_value(m,name)
    for name in E.get_vector_parameter_names(m):
        value=E.get_material_instance_vector_parameter_value(m,name)
        row['vectors'][str(name)]=[value.r,value.g,value.b,value.a]
    for name in E.get_texture_parameter_names(m):
        texture=E.get_material_instance_texture_parameter_value(m,name)
        row['textures'][str(name)]=texture.get_path_name() if texture else None
    report.append(row)
(OUT/'Inspection.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
u.log('MC_CHEST_MATERIALS '+str(OUT/'Inspection.json'))
