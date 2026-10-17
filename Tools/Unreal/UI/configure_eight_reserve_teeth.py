"""Apply the explicitly approved reserve count, preserving the other run settings."""
import hashlib,json,shutil
from pathlib import Path
import unreal as u
assert not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor()
root=Path(u.Paths.project_dir()).resolve()
source=root/'Content/Data/DA_RunRules.uasset'
backup=root/'Saved/GameplayUI/BeforeEightReserveTeeth/DA_RunRules.uasset'
backup.parent.mkdir(parents=True,exist_ok=True)
if not backup.exists():
    shutil.copy2(source,backup)
    assert hashlib.sha256(source.read_bytes()).digest()==hashlib.sha256(backup.read_bytes()).digest()
asset=u.load_asset('/Game/Data/DA_RunRules')
settings=asset.get_editor_property('settings')
unchanged={field:settings.get_editor_property(field) for field in ('max_mouth_health','days_to_survive','max_players')}
before=settings.get_editor_property('initial_arena_teeth')
settings.set_editor_property('initial_arena_teeth',8)
asset.set_editor_property('settings',settings)
assert all(settings.get_editor_property(field)==value for field,value in unchanged.items())
assert u.EditorAssetLibrary.save_loaded_asset(asset,only_if_is_dirty=False)
(root/'Saved/GameplayUI/eight_reserve_teeth.json').write_text(json.dumps({'asset':asset.get_path_name(),'before':before,'after':8,'preserved':unchanged,'backup':str(backup)},ensure_ascii=False,indent=2),encoding='utf-8')
u.log('MC_UI_RESERVE_PROFILE_COUNT '+str(before)+' -> 8')
