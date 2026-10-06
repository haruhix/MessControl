"""Repair only the eight owned third-phase clips; retain mesh/skeleton packages."""
import json
from pathlib import Path
import unreal as u

root = Path(u.Paths.project_dir())
manifest = json.loads((root / 'ArtSource/BossPhase3/AnimationReport.json').read_text(encoding='utf8'))
mesh = u.load_asset('/Game/Gameplay/Boss/Phase3/SK_BossPhase3')
skeleton = mesh.get_editor_property('skeleton')
records = []
for record in manifest['clips']:
    sequence = u.load_asset('/Game/Gameplay/Boss/Phase3/Animations/' + record['name'])
    assert sequence and sequence.get_editor_property('skeleton') == skeleton
    before = u.AnimationLibrary.get_bone_pose_for_time(sequence, 'root', 0., False)
    u.AnimationLibrary.remove_bone_animation(sequence, 'root', include_children=False, finalize=True)
    after = u.AnimationLibrary.get_bone_pose_for_time(sequence, 'root', 0., False)
    assert abs(sequence.get_play_length() - record['duration_seconds']) < .04
    assert u.EditorAssetLibrary.save_loaded_asset(sequence, only_if_is_dirty=False)
    records.append({'clip': sequence.get_path_name(), 'root_before': str(before), 'root_after': str(after)})
output = root / 'Saved/BossPhase3Integration/RootTrackRepair.json'
output.write_text(json.dumps(records, indent=2), encoding='utf8')
u.log('MC_BOSS_PHASE3_ROOT_REPAIR_PASS ' + json.dumps(records))
u.SystemLibrary.quit_editor()
