"""Author neutral shared passive feedback MICs without changing shader parents."""
import hashlib,json
from pathlib import Path
import unreal as u
root=Path(u.Paths.project_dir()).resolve()
assert root.drive.upper()=='E:'
folder='/Game/Gameplay/VFX'
parents={name:root/'Content/Gameplay/VFX'/f'{name}.uasset' for name in ('M_Reaction','M_ReactionSoft')}
before={name:hashlib.sha256(path.read_bytes()).hexdigest() for name,path in parents.items()}
rows=[]
for name,parent in [('MI_PassiveFeedbackSoft','M_ReactionSoft'),('MI_PassiveFeedbackGlow','M_Reaction')]:
    path=folder+'/'+name
    asset=u.load_asset(path) if u.EditorAssetLibrary.does_asset_exist(path) else u.AssetToolsHelpers.get_asset_tools().create_asset(name,folder,u.MaterialInstanceConstant,u.MaterialInstanceConstantFactoryNew())
    assert isinstance(asset,u.MaterialInstanceConstant)
    u.MaterialEditingLibrary.set_material_instance_parent(asset,u.load_asset(folder+'/'+parent))
    u.MaterialEditingLibrary.set_material_instance_vector_parameter_value(asset,'Color',u.LinearColor(1,1,1,1))
    u.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(asset,'Opacity',1.)
    u.MaterialEditingLibrary.update_material_instance(asset)
    assert u.EditorAssetLibrary.save_loaded_asset(asset,only_if_is_dirty=False)
    color=u.MaterialEditingLibrary.get_material_instance_vector_parameter_value(asset,'Color')
    assert color.r==color.g==color.b==1
    rows.append(dict(path=path,parent=parent,color=[color.r,color.g,color.b],opacity=u.MaterialEditingLibrary.get_material_instance_scalar_parameter_value(asset,'Opacity')))
assert before=={name:hashlib.sha256(path.read_bytes()).hexdigest() for name,path in parents.items()}
output=Path('E:/CodexWork/MessControl/PassiveVFX_20261029');output.mkdir(parents=True,exist_ok=True)
(output/'MaterialReadback.json').write_text(json.dumps(dict(rows=rows,unchanged_parents=before),indent=2),encoding='utf8')
print(json.dumps(rows))
