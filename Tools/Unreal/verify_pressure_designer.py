"""Inspect designer pressure assets and verify the physical/material contract."""
from pathlib import Path
import json
import unreal as u

lib=u.EditorAssetLibrary; edit=u.MaterialEditingLibrary
report={}
mat=lib.load_asset('/Game/Gameplay/Arena/M_TonguePain')
assert mat
report['scalars']=[str(n) for n in edit.get_scalar_parameter_names(mat)]
report['function']=str(edit.get_material_property_input_node(mat,u.MaterialProperty.MP_BASE_COLOR))
report['wpo']=str(edit.get_material_property_input_node(mat,u.MaterialProperty.MP_WORLD_POSITION_OFFSET))
root=lib.load_asset('/Game/Data/DA_Tongue')
report['inline']=str(root.pressure)
report['presets']=[p.get_path_name() for p in root.pressure_presets]
assert report['wpo']=='None', 'Pressure must not displace the mesh twice'
for name in ('Pressure Contrast','Pressure Feather','Pressure Tint Amount','Pressure Darken','Pressure Rim Light','Pressure Wetness','Pressure Roughness','Pressure Normal Flatten'):
    assert name in report['scalars'], name
function=lib.load_asset('/Game/Gameplay/Arena/MF_TonguePressureSurface')
for prop in (u.MaterialProperty.MP_BASE_COLOR,u.MaterialProperty.MP_ROUGHNESS,u.MaterialProperty.MP_NORMAL):
    call=edit.get_material_property_input_node(mat,prop)
    assert isinstance(call,u.MaterialExpressionMaterialFunctionCall) and call.get_editor_property('material_function')==function
assert len(root.pressure_presets)>=3
for preset in root.pressure_presets:
    assert preset.surface_material and preset.settings.enabled
    assert 1<=preset.settings.max_depth<=35 and 2<=preset.settings.falloff_power<=6
    parent=preset.surface_material
    while isinstance(parent,u.MaterialInstanceConstant): parent=parent.parent
    assert edit.get_material_property_input_node(parent,u.MaterialProperty.MP_WORLD_POSITION_OFFSET) is None
assert 1<=root.pressure.max_depth<=35, 'Inline depth is outside its supported range'
out=Path(u.Paths.project_saved_dir())/'PressureDesignerReview'/'verification.json'
out.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
u.log('MC_PRESSURE_DESIGNER_VERIFIED '+str(out))
u.SystemLibrary.quit_editor()
