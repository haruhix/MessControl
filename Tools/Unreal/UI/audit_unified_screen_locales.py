"""Read current saved/live localization owners; never mutate assets or gameplay."""
import json
from pathlib import Path
import unreal as u
O=Path('E:/CodexWork/MessControl/Validation/UIHubReward_20261104')
table=u.load_asset('/Game/Gameplay/UI/Data/DT_UI_Localization')
rows=json.loads(u.DataTableFunctionLibrary.export_data_table_to_json_string(table))
(O/'LocaleSavedBefore.json').write_text(json.dumps(rows,ensure_ascii=False,indent=2),encoding='utf-8')
owners=[]
worlds=u.EditorLevelLibrary.get_pie_worlds(True)
for cls in (u.MCHubWidget,u.MCGameplayMenuWidget,u.MCMainMenuWidget,u.MCPerkChoiceWidget,u.MCUnifiedHubWidget,u.MCUnifiedGameplayMenuWidget,u.MCUnifiedRewardWidget,u.MCUnifiedFrontendWidget):
 for item in u.ObjectIterator(cls):
  if not worlds or item.get_world()!=worlds[0] or not item.get_owning_player():continue
  actual=item.get_resolved_translation_table()
  owners.append({'class':str(cls),'owner':item.get_path_name(),'table':actual.get_path_name() if actual else None})
(O/'LocaleOwnerTables.json').write_text(json.dumps(owners,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps({'rows':len(rows),'owners':owners},ensure_ascii=False))
