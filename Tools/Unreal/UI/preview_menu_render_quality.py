"""Opt-in render sampling for the AnatomyV2 menu preview, without saving assets.

Run in solo PIE through remote_python.py. Optional global MC_MENU_RENDER_PROFILE:
"balanced" (TAA150, default), "still" (TAA200), or "restore". This does not
change project renderer settings, geometry, materials, lights or startup routing.
The profile is temporary and process-wide while active; restore before testing
another map. Stills and GPU CSV evidence: Artifacts/RenderQuality_AB_20261022.
"""
import builtins
import unreal as u

KEY = 'mc_menu_render_sampling_preview'
mode = str(globals().get('MC_MENU_RENDER_PROFILE', 'balanced')).lower()
assert mode in ('balanced', 'still', 'restore'), 'Unknown menu render profile'
worlds = u.EditorLevelLibrary.get_pie_worlds(False)
previous = getattr(builtins, KEY, None)

if mode == 'restore':
    if previous:
        world = worlds[0] if len(worlds) == 1 else u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
        for name, value in previous['cvars'].items():
            u.SystemLibrary.execute_console_command(world, name + ' ' + format(value, 'g'))
        delattr(builtins, KEY)
    u.log('MENU_RENDER_SAMPLING_RESTORED')
else:
    assert len(worlds) == 1, 'Start one solo PIE menu preview first'
    world = worlds[0]
    assert 'L_MenuAnatomyV2.' in world.get_path_name(), 'Sampling preview is restricted to L_MenuAnatomyV2'
    values = {
        'r.AntiAliasingMethod': 2,
        'r.ScreenPercentage': 150 if mode == 'balanced' else 200,
        'r.SecondaryScreenPercentage.GameViewport': 100,
        'r.DynamicRes.OperationMode': 0,
        'r.DynamicRes.TestScreenPercentage': 0,
        'r.TemporalAA.Quality': 2,
    }
    if previous:
        assert previous['world'] == world.get_path_name(), 'Restore the previous preview before adopting another PIE world'
    else:
        previous = {'world': world.get_path_name(), 'cvars': {
            name: u.SystemLibrary.get_console_variable_float_value(name) for name in values}}
        setattr(builtins, KEY, previous)
    for name, value in values.items():
        u.SystemLibrary.execute_console_command(world, name + ' ' + format(value, 'g'))
    previous['profile'] = mode
    u.log('MENU_RENDER_SAMPLING_APPLIED ' + mode)
