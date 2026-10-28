"""Save two passive cue MICs using existing shader parents; no graph edits/MIDs.

Run in an idle owned Editor. Reflected cue CDO references and the arena manifest
retain these variants before readiness. Generated readback stays on E:.
"""
import hashlib,json
from pathlib import Path
import unreal as u

assert not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor()
assert not u.EditorLoadingAndSavingUtils.get_dirty_map_packages()
assert not u.EditorLoadingAndSavingUtils.get_dirty_content_packages()
root=Path(u.Paths.project_dir()).resolve()
assert root.drive.upper()=='E:'
parents={name:root/'Content/Gameplay/VFX'/f'{name}.uasset' for name in ('M_Reaction','M_ReactionSoft')}
before={name:hashlib.sha256(path.read_bytes()).hexdigest() for name,path in parents.items()}
folder='/Game/Gameplay/VFX'
rows=[]
for name,parent,tint in [('MI_PassiveGhost','M_ReactionSoft',(.13,.27,.34)),
    ('MI_PassiveLightning','M_Reaction',(.12,.22,.28))]:
    asset=u.load_asset(folder+'/'+name)
    if asset is None:
        asset=u.AssetToolsHelpers.get_asset_tools().create_asset(name,folder,u.MaterialInstanceConstant,u.MaterialInstanceConstantFactoryNew())
    assert isinstance(asset,u.MaterialInstanceConstant)
    u.MaterialEditingLibrary.set_material_instance_parent(asset,u.load_asset(folder+'/'+parent))
    u.MaterialEditingLibrary.set_material_instance_vector_parameter_value(asset,'Color',u.LinearColor(*tint,1))
    u.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(asset,'Opacity',1.)
    u.MaterialEditingLibrary.update_material_instance(asset)
    assert u.EditorAssetLibrary.save_loaded_asset(asset,only_if_is_dirty=False)
    value=u.MaterialEditingLibrary.get_material_instance_vector_parameter_value(asset,'Color')
    opacity=u.MaterialEditingLibrary.get_material_instance_scalar_parameter_value(asset,'Opacity')
    assert all(abs(a-b)<1e-6 for a,b in zip((value.r,value.g,value.b),tint)) and abs(opacity-1)<1e-6
    rows.append(dict(asset=asset.get_path_name(),parent=asset.get_editor_property('parent').get_path_name(),color=[value.r,value.g,value.b],opacity=opacity))
assert before=={name:hashlib.sha256(path.read_bytes()).hexdigest() for name,path in parents.items()},'Shared shader parents changed'
out=Path('E:/CodexWork/MessControl/Validation/PassiveCueMaterials')
out.mkdir(parents=True,exist_ok=True)
(out/'Readback.json').write_text(json.dumps(dict(rows=rows,unchanged_parent_sha256=before),indent=2),encoding='utf8')
print(json.dumps(rows))
