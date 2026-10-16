"""Use the chosen Relax layer for ordinary Gameplay, preserving every other entry.

The native audio core deliberately treats empty layers as silence. Matching the
two authored layers makes startup Relax -> Gameplay reuse its current voice.
"""
import unreal as u,json,shutil,hashlib
from pathlib import Path
r=Path(u.Paths.project_dir());out=r/'Artifacts/RelaxMusic_20261016';before=out/'Before';before.mkdir(exist_ok=True)
assert not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor()
p=u.load_asset('/Game/Data/DA_MouthSounds');scenes=p.get_editor_property('scenes');relax=scenes['Relax'];game=scenes['Gameplay']
layer=relax.get_editor_property('music').copy();sound=layer.get_editor_property('sound');assert sound and not game.get_editor_property('music').get_editor_property('sound'),'Preserve a newly authored Gameplay track'
palette=r/'Content/Data/DA_MouthSounds.uasset';track=r/(sound.get_path_name().split('.')[0].replace('/Game/','Content/')+'.uasset')
for source in [palette,track]:
 if source.exists():
  dest=before/source.relative_to(r);dest.parent.mkdir(parents=True,exist_ok=True);assert not dest.exists();shutil.copy2(source,dest)
baseline={str(k):v.export_text() for k,v in scenes.items()};ambience=game.get_editor_property('ambience').export_text();events={str(k):v.export_text() for k,v in p.get_editor_property('events').items()}
game.set_editor_property('music',layer);scenes['Gameplay']=game;p.set_editor_property('scenes',scenes)
after=p.get_editor_property('scenes');assert all(after[k].export_text()==v for k,v in baseline.items() if k!='Gameplay');assert after['Gameplay'].get_editor_property('ambience').export_text()==ambience
assert {str(k):v.export_text() for k,v in p.get_editor_property('events').items()}==events
assert u.EditorAssetLibrary.save_loaded_asset(sound);assert u.EditorAssetLibrary.save_loaded_asset(p)
(out/'ChangeReport.json').write_text(json.dumps({'changed':'Scenes.Gameplay.Music','relax_preserved':True,'other_scenes_and_events_preserved':True,'layer':layer.export_text(),'sound':sound.get_path_name(),'before_scenes':baseline,'after_scenes':{str(k):v.export_text() for k,v in after.items()},'track_saved_without_setting_changes':True,'sha256':{str(x.relative_to(r)):hashlib.sha256(x.read_bytes()).hexdigest() for x in [palette,track]}},indent=2))
print('RELAX_GAMEPLAY_MUSIC_CONNECTED',sound.get_path_name())
