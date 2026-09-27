"""Tune the current pressure field for readable walking trails; keep mass/depth tuning.

Explicit migration, run once after configure_pressure_designer.py. Backs up the
affected assets before changing them. No additional displacement or trail history.
"""
from pathlib import Path
import json, shutil
import unreal as u

lib=u.EditorAssetLibrary
edit=u.MaterialEditingLibrary
saved=Path(u.Paths.project_saved_dir())/'VisiblePressureTrails'
saved.mkdir(parents=True,exist_ok=True)
report={}

def backup(asset):
    relative=asset.get_path_name().split('.')[0].removeprefix('/Game/')+'.uasset'
    destination=saved/'Before'/relative
    destination.parent.mkdir(parents=True,exist_ok=True)
    if not destination.exists(): shutil.copy2(Path(u.Paths.project_content_dir())/relative,destination)

def save(asset):
    assert lib.save_loaded_asset(asset,only_if_is_dirty=False)

function=lib.load_asset('/Game/Gameplay/Arena/MF_TonguePressureSurface')
backup(function)
expressions=edit.get_material_function_expressions(function)
mask=next(n for n in expressions if isinstance(n,u.MaterialExpressionCustom) and n.get_editor_property('description')=='MC pressure mask')
feather=next((n for n in expressions if isinstance(n,u.MaterialExpressionScalarParameter) and str(n.get_editor_property('parameter_name'))=='Pressure Feather'),None)
if not feather:
    feather=edit.create_material_expression_in_function(function,u.MaterialExpressionScalarParameter,-800,880)
    feather.set_editor_property('parameter_name','Pressure Feather')
    feather.set_editor_property('default_value',.12)
    feather.set_editor_property('group','Pressure | Appearance')
    feather.set_editor_property('slider_min',.01)
    feather.set_editor_property('slider_max',.4)
    inputs=list(mask.get_editor_property('inputs'))
    entry=u.CustomInput(); entry.set_editor_property('input_name','Feather'); inputs.append(entry)
    mask.set_editor_property('inputs',inputs)
assert edit.connect_material_expressions(feather,'',mask,'Feather')
mask.set_editor_property('code','float d=saturate(Depth); return pow(d,clamp(Contrast,0.2,4.0))*smoothstep(0,max(Feather,0.001),d);')
edit.update_material_function(function)
save(function)
base_material=lib.load_asset('/Game/Gameplay/Arena/M_TonguePain')
assert not edit.recompile_material(base_material)
save(base_material)

root=lib.load_asset('/Game/Data/DA_Tongue')
backup(root)
report['before']=str(root.pressure)
settings=root.pressure
settings.set_editor_property('press_seconds',.08)
settings.set_editor_property('recover_seconds',2.2)
settings.set_editor_property('trail_hold_seconds',.35)
root.set_editor_property('pressure',settings)
save(root)
report['after']=str(root.pressure)

for name,values in (
    ('Soft',dict(player_depth_scale=2,depth_per_kg=.8,max_depth=18,press_seconds=.08,recover_seconds=2.2,trail_hold_seconds=.35)),
    ('Deep',dict(player_depth_scale=2,press_seconds=.1,recover_seconds=3,trail_hold_seconds=.5))):
    preset=lib.load_asset('/Game/Data/Pressure/DA_Pressure_'+name)
    backup(preset)
    settings=preset.settings
    for key,value in values.items(): settings.set_editor_property(key,value)
    preset.set_editor_property('settings',settings)
    save(preset)

# Use the calibrated walking/food preset at startup. Keep the user's inline
# sensitivity and depth untouched so clearing this reference restores them.
root.set_editor_property('default_pressure_preset',lib.load_asset('/Game/Data/Pressure/DA_Pressure_Soft'))
save(root)
report['default_preset']=root.default_pressure_preset.get_path_name()

appearance={'Pressure Contrast':.4,'Pressure Feather':.12,'Pressure Tint Amount':.9,'Pressure Darken':.12,
            'Pressure Rim Light':.10,'Pressure Wetness':.95,'Pressure Roughness':.10,
            'Pressure Normal Flatten':.4}
for path in ('/Game/Gameplay/Arena/MI_TonguePain',
             '/Game/Gameplay/Arena/Pressure/MI_Pressure_Soft',
             '/Game/Gameplay/Arena/Pressure/MI_Pressure_Deep'):
    material=lib.load_asset(path)
    backup(material)
    for parameter,value in appearance.items():
        edit.set_material_instance_scalar_parameter_value(material,parameter,value)
        assert abs(edit.get_material_instance_scalar_parameter_value(material,parameter)-value)<.0001
    edit.set_material_instance_vector_parameter_value(material,'Pressure Tint',u.LinearColor(2.2,2.5,2.7,1))
    edit.update_material_instance(material)
    save(material)

report['appearance']=appearance
# Inspect emissive so the readout also catches an artist graph that hides BaseColor.
material=lib.load_asset('/Game/Gameplay/Arena/M_TonguePain')
nodes=[]
seen=set()
def visit(node):
    if not node or node.get_name() in seen: return
    seen.add(node.get_name())
    item=dict(name=node.get_name(),kind=node.get_class().get_name())
    for key in ('parameter_name','default_value','constant','code'):
        try: item[key]=str(node.get_editor_property(key))
        except Exception: pass
    children=edit.get_inputs_for_material_expression(material,node)
    item['inputs']=[n.get_name() if n else None for n in children]
    nodes.append(item)
    for child in children: visit(child)
visit(edit.get_material_property_input_node(material,u.MaterialProperty.MP_EMISSIVE_COLOR))
report['emissive_graph']=nodes
(saved/'tuning.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
u.log('MC_VISIBLE_PRESSURE_TRAILS_CONFIGURED')
u.SystemLibrary.quit_editor()
