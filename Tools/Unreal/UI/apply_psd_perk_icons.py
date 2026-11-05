"""Import the measured PSD artwork and change only canonical DT_Perks.Icon fields."""
import hashlib,json,shutil
from datetime import datetime
from pathlib import Path
import unreal as u

ROOT=Path(__file__).resolve().parents[3]
MANIFEST=ROOT/'ArtSource/UI/V2/Icons/Perks/CanonicalIconMapping.json'
OUT=Path(globals().get('MC_PSD_PERK_ICON_OUTPUT',
    'E:/CodexWork/MessControl/Validation/UIPSD_HUD_20261105/PerkIconIntegration_'+datetime.now().strftime('%Y%m%d_%H%M%S_%f'))).resolve()
assert ROOT.drive.upper()==OUT.drive.upper()=='E:'
assert (ROOT/'MessControl.uproject').is_file()
assert '-MCUIFoundationOwned' in u.SystemLibrary.get_command_line()
assert not u.EditorLevelLibrary.get_pie_worlds(True)
assert not u.EditorLoadingAndSavingUtils.get_dirty_content_packages()
assert not u.EditorLoadingAndSavingUtils.get_dirty_map_packages()
assert not OUT.exists(),'Use a fresh output directory';OUT.mkdir(parents=True)
m=json.loads(MANIFEST.read_text(encoding='utf-8-sig'))
t=u.load_asset(m['tableAsset']);assert t
before=json.loads(u.DataTableFunctionLibrary.export_data_table_to_json_string(t))
old={r['Name']:r for r in before};assert len(old)==133
backup=OUT/'Before';backup.mkdir()
shutil.copy2(ROOT/'Content/Gameplay/Roguelike/DT_Perks.uasset',backup/'DT_Perks.uasset')
tasks=[]
for entry in m['imports']:
    relative=Path(entry['sourceFile']);assert not relative.is_absolute()
    source=(ROOT/relative).resolve();source.relative_to(ROOT)
    assert source.is_file() and hashlib.sha256(source.read_bytes()).hexdigest()==entry['sha256'],entry['rowID']
    existing=ROOT/'Content'/(entry['targetAssetPath'].removeprefix('/Game/')+'.uasset')
    if existing.exists():shutil.copy2(existing,backup/(entry['destinationName']+'.uasset'))
    task=u.AssetImportTask();task.filename=str(source);task.destination_path=entry['destinationPath'];task.destination_name=entry['destinationName']
    task.automated=True;task.replace_existing=True;task.save=False;tasks.append(task)
u.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)
for entry in m['imports']:
    texture=u.load_asset(entry['targetAssetPath']);assert isinstance(texture,u.Texture2D),entry
    texture.set_editor_property('compression_settings',u.TextureCompressionSettings.TC_EDITOR_ICON)
    texture.set_editor_property('lod_group',u.TextureGroup.TEXTUREGROUP_UI)
    texture.set_editor_property('mip_gen_settings',u.TextureMipGenSettings.TMGS_NO_MIPMAPS)
    texture.set_editor_property('srgb',True)
    assert u.EditorAssetLibrary.save_loaded_asset(texture,only_if_is_dirty=False)
patches={entry['rowID']:entry['objectPath'] for entry in m['imports']}
updated=[]
for row in before:
    row=dict(row)
    if row['Name'] in patches:row['Icon']=patches[row['Name']]
    # UE exports this float map with a redundant property-name wrapper;
    # its JSON importer expects the actual numeric map values.
    row['Parameters']={k:(v['Parameters'] if isinstance(v,dict) else v) for k,v in row.get('Parameters',{}).items()}
    updated.append(row)
assert u.DataTableFunctionLibrary.fill_data_table_from_json_string(t,json.dumps(updated,ensure_ascii=False))
after=json.loads(u.DataTableFunctionLibrary.export_data_table_to_json_string(t));now={r['Name']:r for r in after}
assert old.keys()==now.keys()
for key,original in old.items():
    assert {k:v for k,v in original.items() if k!='Icon'}=={k:v for k,v in now[key].items() if k!='Icon'},key
    if key in patches:assert patches[key] in now[key]['Icon'],(key,now[key]['Icon'])
    else:assert now[key]['Icon']==original['Icon'],key
assert u.EditorAssetLibrary.save_loaded_asset(t,only_if_is_dirty=False)
(OUT/'AfterRows.json').write_text(json.dumps(after,ensure_ascii=False,indent=2),encoding='utf-8')
report={'passed':True,'changedIconRows':len(patches),'allOtherFieldsAndRowsPreserved':True,'enabledRows':sum(r['bEnabled'] for r in after),'missingEnabledIcons':[r['Name'] for r in after if r['bEnabled'] and r['Icon']=='None'],'manifest':str(MANIFEST),'validation':'Canonical saved table roundtrip and texture import; visual and first-use checks recorded separately'}
(OUT/'Report.json').write_text(json.dumps(report,indent=2),encoding='utf-8');print(report)
