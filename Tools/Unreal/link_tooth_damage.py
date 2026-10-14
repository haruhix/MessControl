"""Run in an editor started with the compiled damage class.

Live Coding may not regenerate Python struct wrappers; use native ObjectTools
set_properties with the same rows in that session, then save the profile package.
"""
import json
from pathlib import Path
import unreal as u
root=Path(u.Paths.project_dir()).resolve();folder='/Game/Gameplay/Arena/Damage'
factory=u.DataAssetFactory();factory.set_editor_property('data_asset_class',u.load_class(None,'/Script/MessControl.MCToothDamageProfile'))
path=folder+'/DA_ToothDamage'
profile=u.load_asset(path) if u.EditorAssetLibrary.does_asset_exist(path) else u.AssetToolsHelpers.get_asset_tools().create_asset('DA_ToothDamage',folder,u.DataAsset,factory)
rows=[]
row_type=u.get_type_from_struct(u.load_object(None,'/Script/MessControl.MCToothDamageMeshes'))
for source in json.loads((root/'ContentSource/Meshes/ToothDamage/Sources.json').read_text()):
    row=row_type()
    row.set_editor_property('Intact',u.load_asset(source['mesh']))
    row.set_editor_property('Chipped',u.load_asset(folder+'/'+source['name']+'_Chipped'))
    row.set_editor_property('Broken',u.load_asset(folder+'/'+source['name']+'_Broken'))
    assert row.get_editor_property('Chipped') and row.get_editor_property('Broken')
    rows.append(row)
profile.set_editor_property('Teeth',rows);assert u.EditorAssetLibrary.save_loaded_asset(profile)
print('TOOTH_DAMAGE_PROFILE_SAVED',profile.get_path_name(),len(rows))
