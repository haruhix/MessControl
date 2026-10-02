"""Add view-dependent radial wall coverage to the existing rainy roof material.

Run inside Unreal with PIE stopped. Custom primitive data 20-27 is reserved for
the local camera. Existing surface, rain and artist opacity outputs are retained.
"""
import json
from pathlib import Path
import unreal as u

assert not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor()
edit=u.MaterialEditingLibrary
lib=u.EditorAssetLibrary
path='/Game/Art/Materials/Arena/RainDrips/M_Roof_RainDrips'
material=u.load_asset(path)
assert isinstance(material,u.Material)
marker='MC Camera: radial wall coverage'
nodes=edit.get_material_expressions(material)
existing=next((n for n in nodes if isinstance(n,u.MaterialExpressionCustom) and n.get_editor_property('description')==marker),None)
if not existing:
    backup='/Game/Art/Materials/Arena/RainDrips/M_Roof_BeforeCameraReveal'
    if not lib.does_asset_exist(backup):
        assert lib.duplicate_asset(path,backup)
    original=edit.get_material_property_input_node(material,u.MaterialProperty.MP_OPACITY_MASK)
    original_pin=edit.get_material_property_input_node_output_name(material,u.MaterialProperty.MP_OPACITY_MASK)
    assert original, 'Preserve the authored opacity mask'

    def node(cls,**props):
        result=edit.create_material_expression(material,cls,1400,700)
        for k,v in props.items():
            result.set_editor_property(k,v)
        return result

    focus=node(u.MaterialExpressionVectorParameter,parameter_name='CameraRevealFocus',default_value=u.LinearColor(0,0,0,0),group='Camera Reveal',use_custom_primitive_data=True,primitive_data_index=20)
    settings=node(u.MaterialExpressionVectorParameter,parameter_name='CameraRevealSettings',default_value=u.LinearColor(0,140,.25,0),group='Camera Reveal',use_custom_primitive_data=True,primitive_data_index=24)
    enabled=node(u.MaterialExpressionScalarParameter,parameter_name='CameraRevealEnabled',default_value=1.,group='Camera Reveal')
    position=node(u.MaterialExpressionWorldPosition)
    camera=node(u.MaterialExpressionCameraPositionWS)
    coverage=node(u.MaterialExpressionCustom,description=marker,output_type=u.CustomMaterialOutputType.CMOT_FLOAT1)
    inputs={'P':(position,''),'Camera':(camera,''),'Focus':(focus,'RGB'),'Settings':(settings,'RGBA'),'Enabled':(enabled,'')}
    custom_inputs=[]
    for name in inputs:
        entry=u.CustomInput(); entry.set_editor_property('input_name',name); custom_inputs.append(entry)
    coverage.set_editor_property('inputs',custom_inputs)
    for name,(source,pin) in inputs.items():
        assert edit.connect_material_expressions(source,pin,coverage,name)
    function=u.load_asset('/Engine/Functions/Engine_MaterialFunctions02/Utility/DitherTemporalAA')
    assert function
    dither=node(u.MaterialExpressionMaterialFunctionCall,desc='MC Camera: soft radial edge')
    dither.set_material_function(function)
    assert edit.connect_material_expressions(coverage,'',dither,'')
    combined=node(u.MaterialExpressionMultiply,desc='MC Camera: preserve authored coverage')
    assert edit.connect_material_expressions(original,original_pin or '',combined,'A')
    assert edit.connect_material_expressions(dither,'',combined,'B')
    assert edit.connect_material_property(combined,'',u.MaterialProperty.MP_OPACITY_MASK)
    existing=coverage

existing.set_editor_property('code',r'''
float3 toFocus=Focus-Camera;
float viewDistance=max(length(toFocus),1.0);
float3 axis=toFocus/viewDistance;
float3 surface=P-Camera;
float depth=dot(surface,axis);
// A circular aperture on the camera-to-avatar ray; far walls stay opaque.
float radial=length(surface-axis*depth);
float radius=max(Settings.y*max(depth,0.0)/viewDistance,1.0);
float feather=clamp(Settings.z,.02,.8);
float circle=1.0-smoothstep(radius*(1.0-feather),radius,radial);
float inFront=step(-2.0,depth)*(1.0-smoothstep(viewDistance-15.0,viewDistance+15.0,depth));
return 1.0-saturate(Settings.x)*saturate(Enabled)*circle*inFront;
''')
assert material.get_editor_property('blend_mode')==u.BlendMode.BLEND_MASKED
# Keep the rest of the shell visible when the camera passes to its reverse side.
material.set_editor_property('two_sided',True)
errors=edit.recompile_material(material)
assert not errors,str(errors)
assert lib.save_loaded_asset(material,only_if_is_dirty=False)
report=dict(material=material.get_path_name(),focus_index=20,settings_index=24,compiled=True)
Path(u.Paths.project_saved_dir()+'CameraRevealMaterial.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
u.log('MC_CAMERA_REVEAL_MATERIAL_READY '+json.dumps(report))
