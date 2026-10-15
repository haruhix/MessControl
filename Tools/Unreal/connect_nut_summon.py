"""Persist the faster Mage summon tuning without changing other boss bindings."""
import unreal as u,json,hashlib,re
from pathlib import Path
root=Path(u.Paths.project_dir());out=root/'Artifacts/MageSummons_20261016'
assert not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor()
assert not u.EditorLoadingAndSavingUtils.get_dirty_content_packages() and not u.EditorLoadingAndSavingUtils.get_dirty_map_packages()
baseline=json.loads((out/'Baseline.json').read_text());profile=u.load_asset('/Game/Gameplay/CoreLoop/DA_NutRain');settings=profile.get_editor_property('settings');boss=settings.get_editor_property('boss')
boss.set_editor_property('creep_move_speed',360.)
current=re.sub(r',?CreepMoveSpeed=[^,)]+','',boss.export_text());old=re.sub(r',?CreepMoveSpeed=[^,)]+','',baseline['boss_settings_text'])
assert current==old,'Unrelated boss settings changed'
settings.set_editor_property('boss',boss);profile.set_editor_property('settings',settings);assert u.EditorAssetLibrary.save_loaded_asset(profile)
for p,sha in baseline['legacy_assets'].items():assert hashlib.sha256((root/p).read_bytes()).hexdigest()==sha,p
(out/'ConnectionReport.json').write_text(json.dumps({'passed':True,'speed_before_cm_s':260.,'speed_after_cm_s':360.,'increase_percent':100*(360/260-1),'health':boss.get_editor_property('creep_health'),'legacy_boss_bindings_preserved':True},indent=2));print('MAGE_SUMMON_CONNECT_PASS')
