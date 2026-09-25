"""Read-only asset diagnostics for the pour profile and its material defaults."""
from pathlib import Path
import json, sys, unreal as u
result={'python_optimize':sys.flags.optimize}
profile=u.EditorAssetLibrary.load_asset('/Game/Data/DA_CoffeeWater')
result['settings']=str(profile.get_editor_property('settings'))
for key in ('surface_mesh','surface_material','jet_mesh','crown_mesh','pour_material','drop_material'):
    result[key]=str(profile.get_editor_property(key))
for name in ('MI_CoffeeSurface','MI_CoffeePour','MI_CoffeeDrops'):
    mat=u.EditorAssetLibrary.load_asset('/Game/Art/Materials/'+name)
    result[name]={'parent':str(mat.get_editor_property('parent'))}
    for parameter in ('Strength','Opacity','ShallowOpacity','DeepOpacity','Roughness','FoamAmount','AmbientFill'):
        result[name][parameter]=u.MaterialEditingLibrary.get_material_instance_scalar_parameter_value(mat,parameter)
    for parameter in ('ShallowColor','BodyColor','DeepColor'):
        result[name][parameter]=str(u.MaterialEditingLibrary.get_material_instance_vector_parameter_value(mat,parameter))
    base=mat.get_editor_property('parent')
    result[name]['blend']=str(base.get_editor_property('blend_mode'))
    result[name]['outputs']={str(prop):str(u.MaterialEditingLibrary.get_material_property_input_node(base,prop)) for prop in (u.MaterialProperty.MP_BASE_COLOR,u.MaterialProperty.MP_OPACITY,u.MaterialProperty.MP_EMISSIVE_COLOR,u.MaterialProperty.MP_WORLD_POSITION_OFFSET)}
    result[name]['expressions']=u.MaterialEditingLibrary.get_num_material_expressions(base)
    result[name]['overrides']=str(mat.get_editor_property('scalar_parameter_values'))
for name in ('SM_CoffeeSurface','SM_CoffeeJet','SM_CoffeeCrown','SM_CoffeeDrain','SM_CoffeeDrop'):
    mesh=u.EditorAssetLibrary.load_asset('/Game/Art/Meshes/'+name)
    result[name]=str(mesh.get_bounding_box())
out=Path(u.Paths.project_saved_dir())/'CoffeePourReview.json'
out.write_text(json.dumps(result,indent=2),encoding='utf-8')
u.log('MC_POUR_REVIEW '+str(out))
u.SystemLibrary.quit_editor()
