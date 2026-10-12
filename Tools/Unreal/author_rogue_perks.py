"""Merge the concrete roguelike catalog into the saved table; preserve authored tool rows."""
import json
from pathlib import Path
import unreal as u

root = Path(u.Paths.project_dir()).resolve()
table = u.load_asset('/Game/Gameplay/Roguelike/DT_Perks')
assert table
rows = json.loads(u.DataTableFunctionLibrary.export_data_table_to_json_string(table))
concrete = json.loads((root / 'Tools/Unreal/rogue_perk_rows.json').read_text(encoding='utf-8'))
by_id = {row['Name']: row for row in rows}
for row in rows:
    if row['Name'] in ['QuickFeet','LightDash','SecondWind','SharpEdge','HardEnamel','DeepClean','HeavyFeet','HeavyDash','ShortBreath','BluntEdge','SoftEnamel','StickyBrush']:
        row['Weight'] = 0
        row['bAvailableForLevelChoice'] = False
    if row.get('ToolUpgrade','None') != 'None':
        row['ChoiceFamily'] = 'tool_' + row['ToolUpgrade']
for row in concrete:
    by_id[row['Name']] = row
for name, title, description in [
    ('Fallback_Mouthwash','Ополаскиватель','Заряд: G выпускает очищающую волну. Повреждает врагов и смывает союзников.'),
    ('Fallback_BrushFury','Щёточный вихрь','Заряд: G запускает вихрь на 5 секунд. Тормозить нельзя; союзников разбрасывает.'),
    ('Fallback_Grenade','Граната падения','Заряд: G вооружает гранату. При сбивании взрывается и повреждает союзников.')]:
    by_id[name] = {'Name':name,'DisplayName':title,'Description':description,'Polarity':'Positive','Weight':1,'MaxStacks':9999,
        'EffectClass':"/Script/CoreUObject.Class'/Script/MessControl.MCRogueBoosterEffect'",'ToolUpgrade':'None','Rarity':'Standard',
        'bAvailableForLevelChoice':True,'bConsumableBooster':True,'ChoiceFamily':name}
assert u.DataTableFunctionLibrary.fill_data_table_from_json_string(table, json.dumps(list(by_id.values()), ensure_ascii=False))
assert u.EditorAssetLibrary.save_loaded_asset(table, only_if_is_dirty=False)

material_path = '/Game/Gameplay/Roguelike/M_RogueFoam'
material = u.load_asset(material_path)
if not material:
    material = u.AssetToolsHelpers.get_asset_tools().create_asset('M_RogueFoam','/Game/Gameplay/Roguelike',u.Material,u.MaterialFactoryNew())
    material.set_editor_property('shading_model',u.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property('blend_mode',u.BlendMode.BLEND_TRANSLUCENT)
    material.set_editor_property('two_sided',True)
    color = u.MaterialEditingLibrary.create_material_expression(material,u.MaterialExpressionConstant3Vector,-220,0)
    color.set_editor_property('constant',u.LinearColor(.65,.95,1,1))
    opacity = u.MaterialEditingLibrary.create_material_expression(material,u.MaterialExpressionConstant,-220,180)
    opacity.set_editor_property('r',.55)
    u.MaterialEditingLibrary.connect_material_property(color,'',u.MaterialProperty.MP_EMISSIVE_COLOR)
    u.MaterialEditingLibrary.connect_material_property(opacity,'',u.MaterialProperty.MP_OPACITY)
    u.MaterialEditingLibrary.recompile_material(material)
    assert u.EditorAssetLibrary.save_loaded_asset(material,only_if_is_dirty=False)

for path in ['/Game/Blueprints/BP_PlayerCharacter','/Game/Blueprints/BP_MouthGameMode','/Game/Gameplay/Roguelike/BP_RewardChest','/Game/Gameplay/UI/WBP_GameplayHUD']:
    bp = u.load_asset(path)
    if bp:
        u.BlueprintEditorLibrary.compile_blueprint(bp)
        assert u.EditorAssetLibrary.save_loaded_asset(bp,only_if_is_dirty=True)
saved = json.loads(u.DataTableFunctionLibrary.export_data_table_to_json_string(table))
assert all(any(row['Name'] == requested['Name'] for row in saved) for requested in concrete)
print('ROGUE_CATALOG_SAVED',len(concrete),'concrete perks;',len(saved),'total rows')
