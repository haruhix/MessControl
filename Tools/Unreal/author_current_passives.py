"""Extend the canonical catalog to 27 current passives; retain 100 retired IDs.

Run only in idle rebuilt Editor. Existing B1 data is preserved; B2 defaults and
RU/EN come from UI/passive_event_specs.json. This is the current authoring entry.
Outputs/backups use MC_PASSIVES_AUTHOR_OUTPUT or the explicit E: task folder.
"""
import copy,json,math
from pathlib import Path
import unreal as u

ROOT=Path(u.Paths.project_dir()).resolve()
OUT=Path(globals().get('MC_PASSIVES_AUTHOR_OUTPUT','E:/CodexWork/MessControl/PassivesRemaining_20261028/Authoring')).resolve()
assert ROOT.as_posix().lower()=='e:/devgame/messcontrol' and OUT.drive.upper()=='E:'
assert not u.EditorLevelLibrary.get_pie_worlds(True),'Preserve existing Play'
assert not u.EditorLoadingAndSavingUtils.get_dirty_map_packages(),'Preserve unsaved maps'
assert not u.EditorLoadingAndSavingUtils.get_dirty_content_packages(),'Preserve unsaved content'
OUT.mkdir(parents=True,exist_ok=True)
PERKS='/Game/Gameplay/Roguelike/DT_Perks';CATALOG='/Game/Gameplay/Hub/Economy/DT_HubCatalog'
TEXTS='/Game/Gameplay/UI/Data/DT_UI_Localization';RULES='/Game/Gameplay/Hub/Economy/DT_HubEconomyRules'
def read(path):
    table=u.load_asset(path);assert table,path
    return table,json.loads(u.DataTableFunctionLibrary.export_data_table_to_json_string(table))
loaded={path:read(path) for path in (PERKS,CATALOG,TEXTS,RULES)}
for path,(_,rows) in loaded.items():
    dest=OUT/(path.rsplit('/',1)[-1]+'.before.json')
    if not dest.exists():dest.write_text(json.dumps(rows,ensure_ascii=False,indent=2),encoding='utf8')
perks={row['Name']:copy.deepcopy(row) for row in loaded[PERKS][1]}
catalog={row['Name']:copy.deepcopy(row) for row in loaded[CATALOG][1]}
texts={row['Name']:copy.deepcopy(row) for row in loaded[TEXTS][1]}
base={name for name in perks if name.startswith('B1_')}
assert len(base)==9,'Author/recover the nine B1 definitions before extending'
specs=json.loads((ROOT/'Tools/Unreal/UI/passive_event_specs.json').read_text(encoding='utf8'))
assert len(specs)==18 and len({s['ID'] for s in specs})==18
current=base|{spec['ID'] for spec in specs}
for name,row in perks.items():
    if name not in current:row.update(bEnabled=False,Weight=0,bAvailableForLevelChoice=False,bEarlyChoice=False)
for row in catalog.values():
    if row.get('Kind')!='Cosmetic' and row.get('PerkID') not in current:row.update(bEnabled=False,bInShop=False)
source_path=ROOT/'Tools/Unreal/UI/gameplay_ui_texts.json'
source={row['Name']:row for row in json.loads(source_path.read_text(encoding='utf-8-sig'))}
def text(key,ru,en):
    texts.setdefault(key,{'Name':key}).update(RU=ru,EN=en)
    source.setdefault(key,dict(Name=key,Localizations={},AdditionalLanguages={})).update(
        SourceText=ru,RU=ru,EN=en,Context='Current event passives')
    return key
for index,spec in enumerate(specs):
    ident=spec['ID'];ru=spec['RU'];en=spec['EN'];polarity=spec.get('Polarity','Positive')
    assert all(math.isfinite(value) for value in spec['Parameters'].values())
    name_key=text('UI.Skill.Catalog.'+ident+'.Name',ru,en)
    desc_key=text('UI.Skill.Catalog.'+ident+'.Description',spec['DescriptionRU'],spec['DescriptionEN'])
    perks[ident]=dict(Name=ident,bEnabled=True,Kind='Passive',Category=spec['Category'],DisplayName=ru,Description=spec['DescriptionRU'],
        DisplayNameKey=name_key,DescriptionKey=desc_key,InitialCharges=1,CooldownSeconds=0,ActivationAdapterID='None',
        Polarity=polarity,Weight=spec['Weight'],MaxStacks=1,EffectClass="/Script/CoreUObject.Class'/Script/MessControl.MCRoguePerkEffect'",
        Icon='None',ToolUpgrade='None',Rarity=spec['Rarity'],bAvailableForLevelChoice=True,bRoguePerk=True,
        bEarlyChoice=polarity=='Positive',bRequiresTeam=False,bConsumableBooster=False,ChoiceFamily=ident,
        RequiredTool='None',RequiredEvents=[],Modifiers=[],Parameters=spec['Parameters'])
    catalog['Passive_'+ident]=dict(Name='Passive_'+ident,bEnabled=True,bInShop=True,Kind='NextRunBooster',DisplayName=ru,
        Description=spec['DescriptionRU'],DisplayNameKey=name_key,DescriptionKey=desc_key,Icon='None',
        Rarity='Legendary' if spec['Rarity']=='Legendary' else 'Rare' if spec['Rarity']=='Rare' else 'Common',
        Price=spec['Price'],PerkID=ident,CosmeticID='None',ConsumableID='None',Quantity=1,MaximumQueuedStacks=1,
        StockWeight=spec['Weight'],DuplicateCoins=max(1,spec['Price']//3),SortOrder=9+index,ReelSymbol='None')
rules=copy.deepcopy(loaded[RULES][1]);assert len(rules)==1;rules[0]['bCalciumEnabled']=False
updates={PERKS:list(perks.values()),CATALOG:list(catalog.values()),TEXTS:list(texts.values()),RULES:rules}
for path,rows in updates.items():
    table=loaded[path][0]
    assert u.DataTableFunctionLibrary.fill_data_table_from_json_string(table,json.dumps(rows,ensure_ascii=False)),path
    assert u.EditorAssetLibrary.save_loaded_asset(table,only_if_is_dirty=False),path
    assert {r['Name'] for r in read(path)[1]}=={r['Name'] for r in rows},path
source_path.write_text(json.dumps(list(source.values()),ensure_ascii=False,indent=2)+'\n',encoding='utf8')
saved=read(PERKS)[1];enabled={r['Name'] for r in saved if r['bEnabled']}
assert enabled==current and len(saved)==127
assert sum(r['bEnabled'] and r['Polarity']=='Negative' for r in saved)==1
report=dict(total=len(saved),enabled=sorted(enabled),retired=sum(not r['bEnabled'] for r in saved),
    parameters={s['ID']:s['Parameters'] for s in specs},tables=list(updates),
    final_individual_icon_art='Deferred; current-theme fallback, no new texture assets',
    negative='B2_LastingFormula is excluded from ordinary positive chest cards; shop/F3/negative pools remain available')
(OUT/'Result.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
print('CURRENT_PASSIVES_AUTHORED',json.dumps(report,ensure_ascii=False))
