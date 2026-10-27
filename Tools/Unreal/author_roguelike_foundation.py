"""Migrate the stable skill catalog and its presentation metadata in the open Editor.
Keeps row IDs and package paths, preserves profile files and unrelated assets.
Run after a normal Editor rebuild; every intended table is backed up on E:.
"""
import copy
import json
import re
from pathlib import Path
import unreal as u
ROOT=Path(u.Paths.project_dir()).resolve()
assert ROOT.as_posix().lower()=='e:/devgame/messcontrol'
OUT=Path('E:/CodexWork/MessControl/RoguelikeFoundation_20261027/Authoring')
OUT.mkdir(parents=True,exist_ok=True)
assert not u.EditorLevelLibrary.get_pie_worlds(True), 'Stop only an authorized PIE before authoring'
assert not u.EditorLoadingAndSavingUtils.get_dirty_map_packages(), 'Preserve unsaved maps'
assert not u.EditorLoadingAndSavingUtils.get_dirty_content_packages(), 'Preserve unsaved content'
PERKS='/Game/Gameplay/Roguelike/DT_Perks'
DATA='/Game/Gameplay/Hub/Economy/'
TEXTS='/Game/Gameplay/UI/Data/DT_UI_Localization'
def table_rows(path):
 asset=u.load_asset(path)
 assert asset,path
 return asset,json.loads(u.DataTableFunctionLibrary.export_data_table_to_json_string(asset))
def text_source(text):
 if not isinstance(text,str): return ''
 match=re.match(r'NSLOCTEXT\([^,]*,\s*[^,]*,\s*(".*")\)$',text,re.S)
 if match:
  try:return json.loads(match.group(1))
  except ValueError:pass
 return text
paths=[PERKS,DATA+'DT_HubCatalog',DATA+'DT_HubEconomyRules',DATA+'DT_GrillRewards',TEXTS,
 '/Game/Gameplay/UI/Data/DT_UI_Cards','/Game/Gameplay/UI/Data/DT_UI_Boosters']
loaded={p:table_rows(p) for p in paths}
for path,(_,rows) in loaded.items():
 backup=OUT/(path.rsplit('/',1)[-1]+'.before.json')
 if not backup.exists():backup.write_text(json.dumps(rows,ensure_ascii=False,indent=2),encoding='utf-8')
original_ids=[row['Name'] for row in loaded[PERKS][1]]
perks={row['Name']:row for row in copy.deepcopy(loaded[PERKS][1])}
texts={row['Name']:row for row in copy.deepcopy(loaded[TEXTS][1])}
def localize(key,ru,en):
 row=texts.setdefault(key,{'Name':key})
 row.update(RU=ru,EN=en)
 return key
visual={}
for path in ['/Game/Gameplay/UI/Data/DT_UI_Cards','/Game/Gameplay/UI/Data/DT_UI_Boosters']:
 for row in loaded[path][1]:
  ident=row.get('GameplayPerkID');
  if ident and ident!='None': visual[ident]=row
catalog_icons={}
for row in loaded[DATA+'DT_HubCatalog'][1]:
 ident=row.get('PerkID')
 if ident and ident!='None':
  visual.setdefault(ident,row)
  if row.get('Icon') not in [None,'','None']:catalog_icons[ident]=row['Icon']
for ident,row in perks.items():
 kind=row.get('Kind','Automatic')
 if kind=='Automatic':
  kind='ToolUpgrade' if row.get('ToolUpgrade','None')!='None' else 'Consumable' if row.get('bConsumableBooster') or ident.startswith('Recovery') else 'Passive'
 row.update(Kind=kind,MaxStacks=1)
 row.setdefault('InitialCharges',1);row.setdefault('CooldownSeconds',0)
 source=visual.get(ident,{})
 for field in ['DisplayNameKey','DescriptionKey']:
  if source.get(field) and source[field]!='None':row[field]=source[field]
 for field in ['Icon']:
  image=source.get('Image',source.get('Icon'))
  if image in [None,'','None']:image=catalog_icons.get(ident)
  if image and image!='None':row[field]=image
 # Strip the superseded same-ID stack rule from retained legacy copy.
 for field in ['Description']:
  row[field]=row.get(field,'').replace(' за стак','').replace(' per stack','')
 key=row.get('DescriptionKey')
 if key in texts:
  for culture in ['RU','EN']:
   value=texts[key].get(culture,'')
   texts[key][culture]=value.replace(' за стак','').replace(' per stack','')
for ident,ru,en in [
 ('Fallback_Mouthwash','Нажмите клавишу своего слота: очищающая волна вокруг вас. Повреждает врагов и смывает союзников.','Press its slot key: release a cleansing wave. Damages enemies and knocks allies away.'),
 ('Fallback_BrushFury','Нажмите клавишу своего слота: щёточный вихрь на 5 секунд. Двигайтесь осторожно рядом с союзниками.','Press its slot key: enter a five-second brush whirlwind. Take care around allies.'),
 ('Fallback_Grenade','Нажмите клавишу своего слота: вооружить гранату падения. Взрыв срабатывает при следующем нокдауне.','Press its slot key: arm a fall grenade. It explodes on your next knockdown.')]:
 row=perks[ident];row['DescriptionKey']=localize('UI.Skill.Catalog.'+ident+'.Description',ru,en);row['Description']=ru
# Technical reusable active example uses an existing effect; no new effect implementation.
active=copy.deepcopy(perks['Fallback_BrushFury'])
active.update(Name='Active_BrushFury',Kind='Active',Rarity='Rare',InitialCharges=1,CooldownSeconds=15,
 ActivationAdapterID='Fallback_BrushFury',bConsumableBooster=False,Weight=1,ChoiceFamily='Active_BrushFury',
 DisplayName='Щёточный вихрь',DisplayNameKey=localize('UI.Skill.Catalog.Active_BrushFury.Name','Щёточный вихрь','Brush whirlwind'),
 DescriptionKey=localize('UI.Skill.Catalog.Active_BrushFury.Description','Активный навык: щёточный вихрь на 5 секунд. Перезарядка 15 секунд.','Active skill: a five-second brush whirlwind. Cooldown: 15 seconds.'))
perks.setdefault('Active_BrushFury',active)
# Multi-charge example reuses the existing self-heal; it never heals on acquisition.
recovery=copy.deepcopy(perks['RecoverySelfHeal'])
recovery.update(Name='Consumable_RepairKit',Kind='Consumable',InitialCharges=3,CooldownSeconds=1,
 ActivationAdapterID='RecoverySelfHeal',Weight=1,ChoiceFamily='Consumable_RepairKit',DisplayName='Запасные пломбы',
 DisplayNameKey=localize('UI.Skill.Catalog.Consumable_RepairKit.Name','Запасные пломбы','Spare fillings'),
 DescriptionKey=localize('UI.Skill.Catalog.Consumable_RepairKit.Description','Три использования. Восстанавливает 25 HP только при нажатии клавиши слота; при полном HP заряд сохраняется.','Three uses. Restores 25 HP when its slot key is pressed; a full-health attempt keeps the charge.'))
perks.setdefault('Consumable_RepairKit',recovery)
catalog=copy.deepcopy(loaded[DATA+'DT_HubCatalog'][1])
for row in catalog:row.setdefault('StockWeight',1)
new_shop=[('Skill_Whirlwind','Active_BrushFury',55,'Rare'),('Consumable_RepairKit','Consumable_RepairKit',32,'Common'),('Consumable_Mouthwash','Fallback_Mouthwash',25,'Common')]
known={r['Name'] for r in catalog}
for name,perk,price,rarity in new_shop:
 if name not in known:
  catalog.append(dict(Name=name,bEnabled=True,bInShop=True,Kind='NextRunBooster',PerkID=perk,Price=price,
   Quantity=1,MaximumQueuedStacks=1,DuplicateCoins=7,Rarity=rarity,SortOrder=40+len(catalog),StockWeight=1))
rules=copy.deepcopy(loaded[DATA+'DT_HubEconomyRules'][1]);assert len(rules)==1
rules[0].update(MaximumShopItems=8,MaximumShopPassiveItems=4,CommonShopAvailability=1,
 RareShopAvailability=.35,EpicShopAvailability=.15,LegendaryShopAvailability=.05)
rewards=copy.deepcopy(loaded[DATA+'DT_GrillRewards'][1])
by_catalog={r['Name']:r for r in catalog}
# Retain authored symbol tuples and rarity while moving item payouts to coins.
for row in rewards:
 if row.get('Kind')=='CatalogItem':
  item=by_catalog.get(row.get('CatalogItem'))
  assert item,'Unknown authored grill catalog receipt: '+str(row)
  amount=max(1,item.get('DuplicateCoins',0))*max(1,row.get('Quantity',1))
  row.update(Kind='Coins',MinimumCoins=amount,MaximumCoins=amount,CatalogItem='None',Quantity=1)
copy_rows=[
 ('UI.Skill.Empty','Пусто','Empty'),('UI.Skill.Ready','Готово','Ready'),('UI.Skill.Cooldown','{0} с','{0} s'),
 ('UI.Skill.Charges','Заряды: {0}','Charges: {0}'),('UI.Skill.Skip','ПРОПУСТИТЬ [ESC]','SKIP [ESC]'),
 ('UI.Skill.Back','К НАГРАДАМ','BACK TO REWARDS'),('UI.Skill.ReplaceTitle','ЧЕТЫРЕ СЛОТА ЗАНЯТЫ','FOUR SLOTS OCCUPIED'),
 ('UI.Skill.ReplaceSubtitle','Выберите навык для замены или пропустите награду. Таймер сохранит старый набор.','Choose a skill to replace or skip. The timer keeps your current loadout.'),
 ('UI.Skill.ReplaceHint','Слот: 1 / 2 / 3 / 4 · отмена замены: Backspace · пропустить: Esc','Slot: 1 / 2 / 3 / 4 · back: Backspace · skip: Esc'),
 ('UI.Skill.ReplaceSlot','Заменить слот {Slot}','Replace slot {Slot}'),
 ('UI.Skill.Kind.Active','Активный навык · слот 1–4','Active skill · slot 1–4'),
 ('UI.Skill.Kind.Consumable','Расходник · {Charges} зар. · слот 1–4','Consumable · {Charges} uses · slot 1–4'),
 ('UI.Skill.Kind.Tool','Улучшение инструмента · без слота','Tool upgrade · no skill slot'),
 ('UI.Skill.Kind.Passive','Пассивный навык · действует постоянно','Passive skill · always active'),
 ('UI.Reward.OtherChoicesDisappear','Остальные варианты исчезнут после выбора','The other options disappear after you choose'),
 ('UI.Reward.TeamBoosterSubtitle','Выберите личную награду. Активный навык нужно использовать клавишей своего слота.','Choose your personal reward. Use active skills by pressing their slot keys.'),
 ('UI.Shop.ChooseReplacement','Выбрать заменяемый слот','Choose a slot to replace'),
 ('UI.Shop.ReplaceSlot','{0} · {1}','{0} · {1}'),
 ('UI.Shop.ReplacePrompt','Выберите слот подготовки на следующий ран.','Choose a prepared slot for the next run.')]
for key,ru,en in copy_rows:localize(key,ru,en)
updates={PERKS:list(perks.values()),DATA+'DT_HubCatalog':catalog,DATA+'DT_HubEconomyRules':rules,
 DATA+'DT_GrillRewards':rewards,TEXTS:list(texts.values())}
for path,rows in updates.items():
 asset=loaded[path][0]
 assert u.DataTableFunctionLibrary.fill_data_table_from_json_string(asset,json.dumps(rows,ensure_ascii=False)),path
 assert u.EditorAssetLibrary.save_loaded_asset(asset,only_if_is_dirty=False),path
 _,saved=table_rows(path)
 assert {r['Name'] for r in rows}=={r['Name'] for r in saved},path
assert set(original_ids).issubset({r['Name'] for r in table_rows(PERKS)[1]})
compiled=[]
for path in ['/Game/Gameplay/Roguelike/BP_RewardChest','/Game/Gameplay/UI/WBP_GameplayHUD',
 '/Game/Gameplay/UI/WBP_GameplayHUD_v10','/Game/Gameplay/UI/WBP_PerkChoice_Phase1',
 '/Game/Gameplay/UI/WBP_PerkChoice_v10','/Game/Gameplay/UI/WBP_HubShop']:
 bp=u.load_asset(path)
 if bp:
  u.BlueprintEditorLibrary.compile_blueprint(bp)
  assert u.EditorAssetLibrary.save_loaded_asset(bp,only_if_is_dirty=True),path
  compiled.append(path)
report={'saved_tables':list(updates),'compiled':compiled,'old_ids_preserved':len(original_ids),
 'canonical_rows':len(perks),'full_new_screenshot_perk_effects':'deferred; existing effects reused for foundation examples'}
(OUT/'Result.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
print('ROGUELIKE_FOUNDATION_AUTHORED',json.dumps(report,ensure_ascii=False))
