import json
from pathlib import Path
import unreal as u
out=Path('E:/CodexWork/MessControl/Validation/UISettingsTeam_20261103')
worlds=u.EditorLevelLibrary.get_pie_worlds(True);assert len(worlds)==1
world=worlds[0]
root=next((x for x in u.ObjectIterator(u.MCUnifiedUIWidget) if x.get_world()==world and x.is_in_viewport()),None)
menu=next((x for x in u.ObjectIterator(u.MCMainMenuWidget) if x.get_world()==world and x.is_in_viewport()),None)
settings=next((x for x in u.ObjectIterator(u.MCUnifiedSettingsWidget) if x.get_world()==world and x.get_owning_player() and not str(x.get_path_name()).startswith('/Game/')),None)
score=next((x for x in u.ObjectIterator(u.MCScoreboardWidget) if x.get_world()==world and x.is_in_viewport()),None)
team=next((x for x in u.ObjectIterator(u.MCUnifiedTeamWidget) if x.get_world()==world and x.get_owning_player() and not str(x.get_path_name()).startswith('/Game/')),None)
owners={x.get_path_name():{} for x in (root,menu,settings,score,team) if x}
trees={tree.get_path_name():tree.get_outer().get_path_name() for tree in u.ObjectIterator(u.WidgetTree) if tree.get_outer().get_path_name() in owners}
for w in u.ObjectIterator(u.Widget):
    parent=trees.get(w.get_outer().get_path_name())
    if parent:owners[parent][w.get_name()]=w
def controls(owner):
    return owners.get(owner.get_path_name(),{}) if owner else {}
def description(w):
    d={'visibility':str(w.get_visibility())}
    if isinstance(w,u.WidgetSwitcher):d['page']=w.get_active_widget_index()
    if isinstance(w,u.Slider):d['value']=w.get_value()
    if isinstance(w,u.ComboBoxString):d.update(selected=w.get_selected_option(),count=w.get_option_count())
    if isinstance(w,u.CheckBox):d['checked']=w.is_checked()
    if isinstance(w,u.TextBlock):d['text']=str(w.get_text())
    if isinstance(w,u.MCUnifiedMicrophone):d['muted']=w.get_editor_property('bMuted')
    return d
wanted=['InterfaceScreens','MenuPages','MenuSettings_Sections','SettingsV2Sections','SettingsV2_RenderScaleSlider','SettingsV2_MusicSlider','MenuSettings_RenderScaleSlider','MenuSettings_MusicSlider','SettingsV2VideoConfirm','UnifiedTeam','TeamPlayer1Name','TeamPlayer1Points','TeamPlayer1Ping','TeamPlayer1Microphone']
report={'world':world.get_path_name(),'version':str(u.MCGameplayUIFactory.get_presentation_version()),'rootVisible':root.is_visible() if root else False,'objects':{str(type(x)):x.get_path_name() for x in (root,menu,settings,score,team) if x},'controls':{}}
for owner in (root,menu,settings,score,team):
    if not owner:continue
    for n,w in controls(owner).items():
        if n in wanted:report['controls'][n]=description(w)
report['sourceRootVisibility']=str(next(iter(controls(menu).values())).get_visibility()) if menu else ''
report['scoreEntries']=len(score.get_editor_property('player_entries')) if score else 0
report['entries']=[{'name':str(e.get_editor_property('Nickname')),'muted':bool(e.get_editor_property('bVoiceMuted'))} for e in score.get_editor_property('player_entries')] if score else []
(out/'LiveLatest.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
assert u.MCGameplayUIEditorLibrary.capture_pie_game_window(world,str(out/'LiveLatest.png'))
print(json.dumps(report,ensure_ascii=False))
