"""Read-only catalog, rig and deformation inspection in the running editor."""
import unreal as u,json
from pathlib import Path
ROOT=Path(u.Paths.project_dir()).resolve();OUT=ROOT/'Saved/HeadCosmetics20261024';OUT.mkdir(parents=True,exist_ok=True)
r={'pie':u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor(),'world':str(u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()),'tables':{}}
for p in ['/Game/Data/DT_CharacterCosmetics','/Game/Gameplay/Hub/Economy/DT_HubCatalog']:
    t=u.load_asset(p);r['tables'][p]=json.loads(u.DataTableFunctionLibrary.export_data_table_to_json_string(t))
cdo=u.get_default_object(u.MCToothCharacter);appearance=cdo.get_editor_property('Appearance');r['appearance']=u.ToolsetLibrary.get_object_properties(appearance,['SkeletalMesh','Material','MeshTransform','BoneMap'])
mesh=appearance.get_editor_property('SkeletalMesh');r['skeletal_mesh']=mesh.get_path_name();r['material_parameters']=[]
for slot in mesh.materials:
    m=slot.material_interface
    if m:
        root=m.get_base_material();r['material_parameters'].append({'path':m.get_path_name(),'root':root.get_path_name(),'scalars':[str(n) for n in u.MaterialEditingLibrary.get_scalar_parameter_names(m)],'wpo_node':str(u.MaterialEditingLibrary.get_material_property_input_node(root,u.MaterialProperty.MP_WORLD_POSITION_OFFSET))})
r['dirty']=[p.get_path_name() for p in list(u.EditorLoadingAndSavingUtils.get_dirty_map_packages())+list(u.EditorLoadingAndSavingUtils.get_dirty_content_packages())]
(OUT/'RuntimeInspection.json').write_text(json.dumps(r,ensure_ascii=False,indent=2),encoding='utf8')
print('HEAD_COSMETICS_RUNTIME',json.dumps(r,ensure_ascii=False))
