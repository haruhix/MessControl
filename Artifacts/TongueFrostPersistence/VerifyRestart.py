import hashlib
import json
import os
from pathlib import Path
import unreal as u

project = Path(u.Paths.project_dir()).resolve()
out = project/'Artifacts/TongueFrostPersistence'
state = json.loads((out/'EditorState.json').read_text(encoding='utf-8'))
assert project.as_posix() == state['project']
assert not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor()
mat = u.load_asset('/Game/Gameplay/Cold/Frost/M_TongueFrost')
params = {str(n.get_editor_property('parameter_name')):float(n.get_editor_property('default_value'))
    for n in u.MaterialEditingLibrary.get_material_expressions(mat) if isinstance(n,u.MaterialExpressionScalarParameter)}
assert params['Frost Fill']==1.0 and params['Warm Fade Width']==45.0 and params['Warm Edge Variation']==12.0
cdo = u.get_default_object(u.MCIceEvent)
native_frost = cdo.get_editor_property('frost_surface').get_material(0)
native_player = cdo.get_editor_property('player_ice_material')
assert native_frost == mat,(native_frost.get_path_name(),mat.get_path_name())
assert native_player.get_path_name() == '/Game/Gameplay/Cold/M_PlayerIceCoating.M_PlayerIceCoating'
digest = hashlib.sha256((project/'Content/Gameplay/Cold/Frost/M_TongueFrost.uasset').read_bytes()).hexdigest()
assert digest == state['material_hash_before']
subsystem = u.get_editor_subsystem(u.UnrealEditorSubsystem)
world = subsystem.get_editor_world()
assert world.get_path_name().split('.')[0] == state['map'],world.get_path_name()
probe = u.Rotator(pitch=1,yaw=2,roll=3).to_tuple()
keys = {1:'pitch',2:'yaw',3:'roll'}
rotation = u.Rotator(**dict(zip([keys[v] for v in probe],state['camera_rotation'])))
subsystem.set_level_viewport_camera_info(u.Vector(*state['camera_location']),rotation)
report = {'passed':True,'pid_after_restart':os.getpid(),'native_default_tongue_material':native_frost.get_path_name(),
    'native_player_material':native_player.get_path_name(),'frost_fill':params['Frost Fill'],
    'warm_fade_width_cm':params['Warm Fade Width'],'warm_edge_variation_cm':params['Warm Edge Variation'],
    'material_file_unchanged':True,'base_dll':(project/'Binaries/Win64/UnrealEditor-MessControl.dll').as_posix(),
    'base_dll_hash':hashlib.sha256((project/'Binaries/Win64/UnrealEditor-MessControl.dll').read_bytes()).hexdigest(),
    'camera_restored':True}
(out/'RestartVerification.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print('FROST_PERSISTENCE_RESTART_VERIFIED',json.dumps(report))
