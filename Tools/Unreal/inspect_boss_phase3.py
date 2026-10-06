"""Read saved third-phase defaults and verify relocated reward meshes."""
import json
from pathlib import Path
import unreal as u

report = {}
for name, path in (('phase1', '/Game/Gameplay/Boss/BP_ZombieBoss'),
                   ('phase3', '/Game/Gameplay/Boss/Phase3/BP_BossPhase3')):
    bp = u.load_asset(path)
    assert bp
    cdo = u.get_default_object(bp.generated_class())
    component = cdo.get_editor_property('mesh')
    values = {'class': cdo.get_class().get_path_name(), 'profile': str(cdo.get_editor_property('profile')),
              'mesh': str(component.get_editor_property('skeletal_mesh_asset')),
              'transform': str(component.get_relative_transform())}
    for property_name in ('hidden',):
        values[property_name] = str(cdo.get_editor_property(property_name))
    for property_name in ('visible', 'hidden_in_game', 'pause_anims', 'enable_update_rate_optimizations',
                          'animation_mode', 'visibility_based_anim_tick_option'):
        values[property_name] = str(component.get_editor_property(property_name))
    report[name] = values
chest = u.get_default_object(u.MCRewardChest)
report['reward_meshes'] = {}
for name in ('body', 'lid'):
    component = chest.get_editor_property(name)
    mesh = component.get_editor_property('static_mesh')
    assert mesh, 'Core Redirect failed for reward ' + name
    report['reward_meshes'][name] = mesh.get_path_name()
output = Path(u.Paths.project_dir()) / 'Saved/BossPhase3Integration/SavedDefaults.json'
output.write_text(json.dumps(report, indent=2), encoding='utf8')
u.log('MC_BOSS_PHASE3_DEFAULTS_PASS ' + json.dumps(report))
u.SystemLibrary.quit_editor()
