"""Own menu foam readability pass, preserving shared gameplay VFX."""
from pathlib import Path
import runpy
import unreal as u

ROOT = Path('E:/DEVGAME/MessControl')
assert not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor()
module = runpy.run_path(str(ROOT / 'Tools/Unreal/author_menu_brush_foam_v2.py'))
module['PROFILES']['foam'].update(rate=32., lifetime=[.40, .60], size_cm=[5.5, 8.5],
                                plane_cm=[7., 4.], normal_offset_cm=2.2)
module['PROFILES']['bubbles'].update(rate=5., lifetime=[.8, 1.2], size_cm=[2.8, 4.5],
                                   plane_cm=[4., 3.], normal_offset_cm=3.)
module['main'].__globals__['MC_MENU_FOAM_RESUME_CREATED_TARGET'] = True
report = module['main']()
report['steady_state_estimate'] = {'contact_mean_live': 16., 'bubble_mean_live': 5.,
    'note': 'Rate*lifetime estimate, not a GPU profile. Native readability pending.'}
import json
(ROOT / 'Artifacts/MenuSceneV2_20261022/Environment/FoamReadabilityPass.json').write_text(json.dumps(report, indent=2), encoding='utf8')
u.log('MENU_FOAM_READABILITY_CANDIDATE_SAVED')
