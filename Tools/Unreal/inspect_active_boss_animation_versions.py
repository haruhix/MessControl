"""Read current Nut encounter animation references without changing packages."""
import json
from pathlib import Path
import unreal as u

profile = u.load_asset('/Game/Gameplay/CoreLoop/DA_NutRain')
assert profile is not None
data = json.loads(u.ToolsetLibrary.get_object_properties(profile, ['Settings']))
settings = data.get('Settings', data.get('settings', {}))
boss = settings.get('Boss', settings.get('boss', {}))
refs = {name: value for name, value in boss.items()
        if 'Animation' in name or 'SkeletalMesh' in name}
report = dict(profile=profile.get_path_name(), references=refs,
              profile_dirty=any(p.get_name() == '/Game/Gameplay/CoreLoop/DA_NutRain'
                                for p in u.EditorLoadingAndSavingUtils.get_dirty_content_packages()),
              packages_saved=False)
output = Path(u.Paths.project_saved_dir())/'Checks/ActiveBossAnimationVersions.json'
output.write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding='utf8')
u.log('MC_ACTIVE_BOSS_ANIMATIONS '+json.dumps(report, ensure_ascii=False))
