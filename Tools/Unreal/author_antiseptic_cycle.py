"""Update only Antiseptic tuning and its RU/EN descriptions in saved tables.

Run in idle MessControl Editor. Other rows are compared before saving. Backups,
readback and process temporary files stay on E:. No widget or material changes.
"""
import copy
import json
from pathlib import Path
import unreal as u

ROOT=Path(u.Paths.project_dir()).resolve()
OUT=Path(globals().get('MC_ANTISEPTIC_AUTHOR_OUTPUT','E:/CodexWork/MessControl/PassiveVFX_20261029/AntisepticAuthoring')).resolve()
assert ROOT.as_posix().lower()=='e:/devgame/messcontrol' and OUT.drive.upper()=='E:'
assert not OUT.exists(), 'Keep prior authoring evidence'
assert not u.EditorLevelLibrary.get_pie_worlds(True), 'Preserve active Play'
assert not u.EditorLoadingAndSavingUtils.get_dirty_map_packages(), 'Preserve unsaved maps'
dirty={p.get_path_name() for p in u.EditorLoadingAndSavingUtils.get_dirty_content_packages()}
recover=bool(globals().get('MC_ANTISEPTIC_RECOVER_OWNED_IMPORT',False))
assert not dirty or (recover and dirty=={'/Game/Gameplay/Roguelike/DT_Perks'}), 'Preserve unsaved content'
OUT.mkdir(parents=True)
spec=next(s for s in json.loads((ROOT/'Tools/Unreal/UI/passive_event_specs.json').read_text(encoding='utf8')) if s['ID']=='B2_Antiseptic')
assert spec['Parameters']=={'CooldownPenalty':.3,'BaseCooldown':2,'SprayDuration':3,'CleanEfficiency':1}
paths={'perks':'/Game/Gameplay/Roguelike/DT_Perks',
       'catalog':'/Game/Gameplay/Hub/Economy/DT_HubCatalog',
       'texts':'/Game/Gameplay/UI/Data/DT_UI_Localization'}

def canonical(value):
    if isinstance(value,list):return [canonical(v) for v in value]
    if not isinstance(value,dict):return value
    result={}
    for key,item in value.items():
        if key in ('Parameters','Localizations','AdditionalLanguages') and isinstance(item,dict):
            result[key]={name:canonical(val[key] if isinstance(val,dict) and set(val)=={key} else val) for name,val in item.items()}
        else:result[key]=canonical(item)
    return result

def read(table):
    return canonical(json.loads(u.DataTableFunctionLibrary.export_data_table_to_json_string(table)))

report={'parameters':spec['Parameters'],'tables':{},'new_runtime_resources':[]}
for kind,path in paths.items():
    table=u.load_asset(path);assert table,path
    before=read(table)
    (OUT/(kind+'.before.json')).write_text(json.dumps(before,ensure_ascii=False,indent=2),encoding='utf8')
    changed=copy.deepcopy(before)
    target='B2_Antiseptic' if kind=='perks' else 'Passive_B2_Antiseptic' if kind=='catalog' else 'UI.Skill.Catalog.B2_Antiseptic.Description'
    row=next(r for r in changed if r['Name']==target)
    if kind=='perks':row['Parameters']=spec['Parameters'].copy()
    if kind in ('perks','catalog'):row['Description']=spec['DescriptionRU']
    else:row.update(RU=spec['DescriptionRU'],EN=spec['DescriptionEN'])
    assert u.DataTableFunctionLibrary.fill_data_table_from_json_string(table,json.dumps(changed,ensure_ascii=False)),path
    after=read(table)
    # Exact normalized roundtrip covers every unrelated row, including maps.
    assert len(after)==len(before)
    actual={r['Name']:r for r in after};expected={r['Name']:r for r in changed}
    assert set(actual)==set(expected)
    assert {k:v for k,v in actual.items() if k!=target}=={k:v for k,v in expected.items() if k!=target}, 'Unexpected unrelated row change: '+path
    # UE assigns the existing stable NSLOCTEXT identity to newly imported FText.
    # Preserve that identity rather than comparing it to a plain source string.
    text_fields={'Description'} if kind in ('perks','catalog') else {'RU','EN'}
    assert {k:v for k,v in actual[target].items() if k not in text_fields}=={k:v for k,v in expected[target].items() if k not in text_fields}, 'Unexpected target field change: '+path
    for key in text_fields:assert expected[target][key] in actual[target][key], 'Unexpected localized source: '+path+' '+key
    assert u.EditorAssetLibrary.save_loaded_asset(table,only_if_is_dirty=False),path
    (OUT/(kind+'.after.json')).write_text(json.dumps(after,ensure_ascii=False,indent=2),encoding='utf8')
    report['tables'][path]={'rows':len(after),'changed_row':target,'all_other_rows_preserved':True,'saved':True}
(OUT/'Result.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
print('ANTISEPTIC_CYCLE_AUTHORED '+json.dumps(report,ensure_ascii=False))
