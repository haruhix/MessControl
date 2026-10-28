"""Author the nine current passives; retain every retired row as disabled data.

Run in idle, rebuilt MessControl Editor. Backups/reports stay on E:. This is the
current catalog authoring entry point; legacy perk recipes are historical.
"""
import copy
import json
from pathlib import Path
import unreal as u

ROOT=Path(u.Paths.project_dir()).resolve()
assert ROOT.as_posix().lower()=='e:/devgame/messcontrol'
OUT=Path('E:/CodexWork/MessControl/PassivesBatch1_20261028/Authoring')
OUT.mkdir(parents=True,exist_ok=True)
assert not u.EditorLevelLibrary.get_pie_worlds(True),'Preserve an existing Play session'
assert not u.EditorLoadingAndSavingUtils.get_dirty_map_packages(),'Preserve unsaved maps'
assert not u.EditorLoadingAndSavingUtils.get_dirty_content_packages(),'Preserve unsaved packages'
PERKS='/Game/Gameplay/Roguelike/DT_Perks'
CATALOG='/Game/Gameplay/Hub/Economy/DT_HubCatalog'
TEXTS='/Game/Gameplay/UI/Data/DT_UI_Localization'
RULES='/Game/Gameplay/Hub/Economy/DT_HubEconomyRules'

# id, RU name, EN name, RU description, EN description, rarity, price, weight, icon donor, modifiers
SPECS=[
 ('B1_HeavyHit','Тяжёлый удар','Heavy hit','Урон оружия +40%. Частота атак −30%.','Weapon damage +40%. Attack frequency −30%.','Rare',45,.65,None,[('WeaponDamage',.4,False),('AttackRate',-.3,False)]),
 ('B1_LightFoot','Лёгкая походка','Light foot','Скорость ходьбы и бега +8%. Расход выносливости +8%.','Walking and sprinting speed +8%. Stamina costs +8%.','Standard',24,1,'P01',[('NormalSpeed',.08,False),('StaminaCost',.08,False)]),
 ('B1_DeepBreath','Глубокий вдох','Deep breath','Максимальная выносливость +20%. Текущий запас при получении не пополняется.','Maximum stamina +20%. Acquisition does not refill current stamina.','Standard',30,1,'P02',[('MaxStamina',.2,False)]),
 ('B1_BigPortion','Большая порция','Big portion','Два дополнительных места для переносимой еды.','Two extra places for carried food.','Rare',38,.7,None,[('CarryCapacity',2,True)]),
 ('B1_ConfidentThrow','Уверенный бросок','Confident throw','Дальность броска еды +20%.','Food throwing reach +20%.','Standard',25,1,None,[('FoodThrowRange',.2,False)]),
 ('B1_FreshDelivery','Свежая доставка','Fresh delivery','Пока вы несёте еду, она портится на 10% медленнее.','Carried food spoils 10% more slowly.','Standard',22,1,None,[('SpoilRate',-.1,False)]),
 ('B1_GreedyHands','Руки загребущие','Reaching hands','Дальность подбора пищевых фрагментов +30%.','Food-fragment pickup range +30%.','Standard',24,1,None,[('FoodPickupRange',.3,False)]),
 ('B1_WideBristles','Широкая щетина','Wide bristles','Рабочая ширина щётки +15%.','Brush cleaning width +15%.','Standard',30,1,'P13',[('BrushWidth',.15,False)]),
 ('B1_UnderPressure','Под давлением','Under pressure','Дальность спрея +25%. Ширина струи +20%.','Spray range +25%. Spray width +20%.','Rare',40,.65,None,[('SprayRange',.25,False),('SprayWidth',.2,False)])]

def read(path):
    table=u.load_asset(path)
    assert table,path
    return table,json.loads(u.DataTableFunctionLibrary.export_data_table_to_json_string(table))

loaded={path:read(path) for path in [PERKS,CATALOG,TEXTS,RULES]}
for path,(_,rows) in loaded.items():
    target=OUT/(path.rsplit('/',1)[-1]+'.before.json')
    if not target.exists():target.write_text(json.dumps(rows,ensure_ascii=False,indent=2),encoding='utf8')
perks={row['Name']:copy.deepcopy(row) for row in loaded[PERKS][1]}
catalog={row['Name']:copy.deepcopy(row) for row in loaded[CATALOG][1]}
texts={row['Name']:copy.deepcopy(row) for row in loaded[TEXTS][1]}
old_ids=set(perks)-{spec[0] for spec in SPECS}
for row in perks.values():
    row.update(bEnabled=False,Weight=0,bAvailableForLevelChoice=False,bEarlyChoice=False)
# Preserve cosmetics and original paid item definitions/IDs. Disabled legacy
# purchases remain recognizable and unapplied, never silently converted/lost.
for row in catalog.values():
    if row.get('Kind')!='Cosmetic':row.update(bEnabled=False,bInShop=False)
source_path=ROOT/'Tools/Unreal/UI/gameplay_ui_texts.json'
source_rows=json.loads(source_path.read_text(encoding='utf-8-sig'))
source_by_id={row['Name']:row for row in source_rows}

def text(key,ru,en):
    row=texts.setdefault(key,{'Name':key});row.update(RU=ru,EN=en)
    source=source_by_id.setdefault(key,dict(Name=key,Localizations={},AdditionalLanguages={}))
    source.update(SourceText=ru,RU=ru,EN=en,Context='Batch1 current passive catalog')
    return key

for index,(ident,ru,en,ru_desc,en_desc,rarity,price,weight,donor,mods) in enumerate(SPECS):
    name_key=text('UI.Skill.Catalog.'+ident+'.Name',ru,en)
    description_key=text('UI.Skill.Catalog.'+ident+'.Description',ru_desc,en_desc)
    icon=perks.get(donor,{}).get('Icon','None') if donor else 'None'
    row=dict(Name=ident,bEnabled=True,Kind='Passive',Category='Batch1',DisplayName=ru,Description=ru_desc,
      DisplayNameKey=name_key,DescriptionKey=description_key,InitialCharges=1,CooldownSeconds=0,ActivationAdapterID='None',
      Polarity='Positive',Weight=weight,MaxStacks=1,EffectClass="/Script/CoreUObject.Class'/Script/MessControl.MCRoguePerkEffect'",
      Icon=icon,ToolUpgrade='None',Rarity=rarity,bAvailableForLevelChoice=True,bRoguePerk=True,bEarlyChoice=True,
      bRequiresTeam=False,bConsumableBooster=False,ChoiceFamily=ident,RequiredTool='None',RequiredEvents=[],
      Modifiers=[dict(Stat=stat,Value=value,bFlat=flat,Condition='Always') for stat,value,flat in mods])
    perks[ident]=row
    catalog_id='Passive_'+ident
    catalog[catalog_id]=dict(Name=catalog_id,bEnabled=True,bInShop=True,Kind='NextRunBooster',DisplayName=ru,Description=ru_desc,
      DisplayNameKey=name_key,DescriptionKey=description_key,Icon=icon,Rarity='Rare' if rarity=='Rare' else 'Common',
      Price=price,PerkID=ident,CosmeticID='None',ConsumableID='None',Quantity=1,MaximumQueuedStacks=1,
      StockWeight=weight,DuplicateCoins=max(1,price//3),SortOrder=index,ReelSymbol='None')
rules=copy.deepcopy(loaded[RULES][1])
assert len(rules)==1
rules[0]['bCalciumEnabled']=False
updates={PERKS:list(perks.values()),CATALOG:list(catalog.values()),TEXTS:list(texts.values()),RULES:rules}
for path,rows in updates.items():
    table=loaded[path][0]
    assert u.DataTableFunctionLibrary.fill_data_table_from_json_string(table,json.dumps(rows,ensure_ascii=False)),path
    assert u.EditorAssetLibrary.save_loaded_asset(table,only_if_is_dirty=False),path
    saved=read(path)[1]
    assert {row['Name'] for row in rows}=={row['Name'] for row in saved},path
source_path.write_text(json.dumps(list(source_by_id.values()),ensure_ascii=False,indent=2)+'\n',encoding='utf8')
saved=read(PERKS)[1]
enabled={row['Name'] for row in saved if row['bEnabled']}
assert enabled=={spec[0] for spec in SPECS}
assert all(not row['bAvailableForLevelChoice'] and row['Weight']==0 for row in saved if row['Name'] in old_ids)
report=dict(enabled=sorted(enabled),retired=len(old_ids),total=len(saved),saved_tables=list(updates),
  old_paid_definitions_preserved=True,cosmetics_preserved=True,new_runtime_assets=False,
  icons='Existing metadata for3 passives; remaining cards use existing theme fallback. Final icon art deferred.')
(OUT/'Result.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
print('BATCH1_CATALOG_AUTHORED',json.dumps(report,ensure_ascii=False))
