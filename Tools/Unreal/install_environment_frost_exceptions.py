"""Give map meshes with engine/test materials frost variants, preserving sources."""
import hashlib
import json
import shutil
from pathlib import Path
import unreal as u

e, lib = u.MaterialEditingLibrary, u.EditorAssetLibrary
levels=u.get_editor_subsystem(u.LevelEditorSubsystem)
assert not levels.is_in_play_in_editor()
root=Path(u.Paths.project_dir())
out=root/'Artifacts/EnvironmentFrost'
function=u.load_asset('/Game/Gameplay/Cold/Frost/MF_EnvironmentFrost')
assert isinstance(function,u.MaterialFunction)
folder='/Game/Gameplay/Cold/Frost'
world=u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
assert world.get_path_name()=='/Game/Maps/L_Mouth.L_Mouth'
assert not u.EditorLoadingAndSavingUtils.get_dirty_map_packages()
report={'originals_unchanged':{},'assignments':[]}
for name in ['M_Ice','MI_Ice']:
    source=root/'Content/Art/Materials/ice'/(name+'.uasset')
    report['originals_unchanged'][name]=hashlib.sha256(source.read_bytes()).hexdigest()
map_backup=out/'L_Mouth.BeforeEnvironmentFrost.umap'
if not map_backup.exists(): shutil.copy2(root/'Content/Maps/L_Mouth.umap',map_backup)

def save(asset):
    assert lib.save_loaded_asset(asset,only_if_is_dirty=False),asset.get_path_name()
def link(source,output,dest,pin=''):
    assert e.connect_material_expressions(source,output,dest,pin),pin
def load_or_duplicate(source,dest):
    asset=u.load_asset(dest) if lib.does_asset_exist(dest) else lib.duplicate_asset(source,dest)
    assert asset,dest
    return asset

grid=load_or_duplicate('/Engine/EngineMaterials/WorldGridMaterial',folder+'/M_DefaultPropFrost')
calls=[n for n in e.get_material_expressions(grid) if isinstance(n,u.MaterialExpressionMaterialFunctionCall)
    and n.get_editor_property('material_function')==function]
if not calls:
    normal=e.get_material_property_input_node(grid,u.MaterialProperty.MP_NORMAL)
    call=e.create_material_expression(grid,u.MaterialExpressionMaterialFunctionCall,2800,1200)
    assert call.set_material_function(function)
    for name,prop in [('BaseColor',u.MaterialProperty.MP_BASE_COLOR),('Roughness',u.MaterialProperty.MP_ROUGHNESS)]:
        source=e.get_material_property_input_node(grid,prop)
        pin=str(e.get_material_property_input_node_output_name(grid,prop))
        assert source
        link(source,pin,call,name)
        assert e.connect_material_property(call,name,prop)
    assert e.get_material_property_input_node(grid,u.MaterialProperty.MP_NORMAL)==normal
    assert not list(e.recompile_material(grid))
    save(grid)

ice=load_or_duplicate('/Game/Art/Materials/ice/M_Ice',folder+'/M_IcePropFrost')
calls=[n for n in e.get_material_expressions(ice) if isinstance(n,u.MaterialExpressionMaterialFunctionCall)
    and n.get_editor_property('material_function')==function]
if not calls:
    front=e.get_material_property_input_node(ice,u.MaterialProperty.MP_FRONT_MATERIAL)
    assert isinstance(front,u.MaterialExpressionMaterialFunctionCall)
    original_inputs=dict(zip([str(n) for n in e.get_material_expression_input_names(front)],
        e.get_inputs_for_material_expression(ice,front)))
    assert isinstance(original_inputs['Base Color'],u.MaterialExpressionLinearInterpolate)
    assert isinstance(original_inputs['Roughness'],u.MaterialExpressionConstant)
    call=e.create_material_expression(ice,u.MaterialExpressionMaterialFunctionCall,1200,1500)
    assert call.set_material_function(function)
    link(original_inputs['Base Color'],'',call,'BaseColor')
    link(original_inputs['Roughness'],'',call,'Roughness')
    for output,pin in [('BaseColor','Base Color'),('Roughness','Roughness'),('Metallic','Metallic'),('Specular','Specular')]:
        link(call,output,front,pin)
    inputs_after=dict(zip([str(n) for n in e.get_material_expression_input_names(front)],
        e.get_inputs_for_material_expression(ice,front)))
    assert inputs_after['Normal']==original_inputs['Normal']
    assert e.get_material_property_input_node(ice,u.MaterialProperty.MP_FRONT_MATERIAL)==front
    assert not list(e.recompile_material(ice))
    save(ice)
sphere=load_or_duplicate('/Game/Art/Materials/ice/MI_Ice',folder+'/MI_IcePropFrost')
assert isinstance(sphere,u.MaterialInstanceConstant)
e.set_material_instance_parent(sphere,ice)
save(sphere)

with u.ScopedEditorTransaction('Add frost to remaining map teeth and test prop'):
    for actor in u.get_editor_subsystem(u.EditorActorSubsystem).get_all_level_actors():
        if actor.get_actor_label().startswith(('COLLISION','LOOK')) or actor.get_editor_property('hidden'):
            continue
        for comp in actor.get_components_by_class(u.MeshComponent):
            if not comp.is_visible() or comp.get_editor_property('hidden_in_game'):
                continue
            for index in range(comp.get_num_materials()):
                original=comp.get_material(index)
                if not original: continue
                path=original.get_path_name()
                replacement=None
                if path=='/Engine/EngineMaterials/WorldGridMaterial.WorldGridMaterial': replacement=grid
                if path=='/Game/Art/Materials/ice/MI_Ice.MI_Ice': replacement=sphere
                if replacement:
                    comp.set_material(index,replacement)
                    report['assignments'].append({'actor':actor.get_path_name(),'label':actor.get_actor_label(),
                        'component':comp.get_path_name(),'slot':index,'before':path,'after':replacement.get_path_name()})
    if report['assignments']: assert levels.save_current_level()

for name,digest in report['originals_unchanged'].items():
    source=root/'Content/Art/Materials/ice'/(name+'.uasset')
    assert hashlib.sha256(source.read_bytes()).hexdigest()==digest,name
report['map_backup']=map_backup.as_posix()
report['dirty_content']=[p.get_path_name() for p in u.EditorLoadingAndSavingUtils.get_dirty_content_packages()]
report['dirty_maps']=[p.get_path_name() for p in u.EditorLoadingAndSavingUtils.get_dirty_map_packages()]
(out/'MapAssignments.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
levels.editor_invalidate_viewports()
print('ENVIRONMENT_FROST_REMAINING_SAVED',json.dumps(report))
