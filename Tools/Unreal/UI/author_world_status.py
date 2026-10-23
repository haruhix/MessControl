"""Create the editable compact overhead indicator WBP and its explicit theme role.

Run only in an idle, saved freshly rebuilt Editor. Existing Designer trees and
other class-map entries are preserved. No level, food/enemy data or actor edits.
Native preview pictures are cosmetic fixtures, not gameplay validation.
"""
import hashlib
import json
from pathlib import Path
import unreal as u

ROOT=Path('E:/DEVGAME/MessControl')
OUT=Path(globals().get('MC_WORLD_STATUS_AUTHOR_OUTPUT','E:/CodexWork/HubSystems_20261023/WorldStatusAuthor01')).resolve()
assert OUT.drive.upper()=='E:' and (not OUT.exists() or not any(OUT.iterdir())), 'Use fresh E: output'
assert not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor(), 'Preserve active PIE'
dirty=lambda:list(u.EditorLoadingAndSavingUtils.get_dirty_content_packages())+list(u.EditorLoadingAndSavingUtils.get_dirty_map_packages())
assert not dirty(), 'Preserve unsaved packages before authoring'
OUT.mkdir(parents=True,exist_ok=True)
THEME='/Game/Gameplay/UI/Theme/DA_GameplayUITheme'
WIDGET='/Game/Gameplay/UI/WBP_WorldStatus'
report=dict(scope=__doc__,backups=[],previews=[],role='WorldStatus',widget=WIDGET,pie_fill_direction='0_to_1')
for package in (THEME,WIDGET):
    source=ROOT/'Content'/(package.removeprefix('/Game/')+'.uasset')
    if source.is_file():
        data=source.read_bytes();target=OUT/'BeforePackages'/source.relative_to(ROOT/'Content')
        target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(data)
        report['backups'].append(dict(package=package,file=str(target),sha256=hashlib.sha256(data).hexdigest()))
native=u.load_class(None,'/Script/MessControl.MCWorldStatusWidget');assert native, 'Build new native classes first'
existed=u.EditorAssetLibrary.does_asset_exist(WIDGET)
asset=u.MCGameplayUIEditorLibrary.create_editable_gameplay_widget(WIDGET,native);assert asset
cls=u.load_class(None,WIDGET+'.WBP_WorldStatus_C');assert cls
theme=u.load_asset(THEME);assert theme
mapping=dict(theme.get_editor_property('widget_classes'))
old=next((key for key in mapping if str(key)=='WorldStatus'),None)
if old is None:
    mapping['WorldStatus']=cls;theme.set_editor_property('widget_classes',mapping)
    theme.notify_theme_changed();assert u.EditorAssetLibrary.save_loaded_asset(theme,only_if_is_dirty=False)
    report['role_added']=True
else:
    report['role_added']=False;report['existing_role_preserved']=str(mapping[old])
report['existing_designer_tree_preserved']=existed
for state,times in (('FoodFill0',[1.]),('FoodFill25',[1.]),('FoodFill50',[1.]),('FoodFill75',[1.]),('FoodFill100',[1.]),
    ('FoodSpoiled',[1.]),('Enemy',[1.]),
    ('Loss',[0.,.12,.28]),('Appear',[0.,.08,.18]),('Fade',[0.,.08,.16])):
    for moment in times:
        target=OUT/'Previews'/(state+'_'+str(round(moment*1000)).zfill(4)+'.png');target.parent.mkdir(parents=True,exist_ok=True)
        assert u.MCGameplayUIEditorLibrary.render_gameplay_ui_preview(cls,str(target),u.IntPoint(168,46),state,'ru',moment)
        report['previews'].append(dict(file=str(target),state=state,visual_seconds=moment))
assert not dirty(), 'Unexpected dirty packages preserved'
(OUT/'Authoring.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
u.log('MC_WORLD_STATUS_AUTHOR_COMPLETE '+str(OUT/'Authoring.json'))
