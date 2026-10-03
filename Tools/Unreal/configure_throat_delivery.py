"""Save the shared 3s collection / 2s suction cycle and automatic delivery hints."""
import unreal as u

world = u.get_editor_subsystem(u.LevelEditorSubsystem)
assert world.load_level('/Game/Maps/L_Mouth')
throats = [a for a in u.get_editor_subsystem(u.EditorActorSubsystem).get_all_level_actors()
           if isinstance(a, u.MCThroat)]
assert throats, 'L_Mouth must contain the authored throat'
for throat in throats:
    throat.set_editor_property('anticipation_seconds', 3.0)
    throat.set_editor_property('swallow_seconds', 2.0)
assert world.save_current_level()
plan = u.EditorAssetLibrary.load_asset('/Game/Data/DA_Day01')
assert plan
steps = plan.get_editor_property('steps')
for index, step in enumerate(steps):
    if step.get_editor_property('step') == u.MCDayStep.BREAKFAST_CLEANUP:
        step.set_editor_property('instruction', 'RMB: cut large food. LMB: collect a horizontal stack. Carry it into the throat zone to deliver automatically. Other players have 3 seconds to add food before the 2-second suction.')
    elif step.get_editor_property('step') == u.MCDayStep.STUCK_FOOD:
        step.set_editor_property('instruction', 'RMB: break stuck food free. LMB: collect the pieces. Enter the THROAT zone with the stack; delivery is automatic.')
    steps[index] = step
plan.set_editor_property('steps', steps)
for step in plan.get_editor_property('steps'):
    if step.get_editor_property('step') in (u.MCDayStep.BREAKFAST_CLEANUP, u.MCDayStep.STUCK_FOOD):
        assert 'automatic' in str(step.get_editor_property('instruction'))
assert u.EditorAssetLibrary.save_loaded_asset(plan, only_if_is_dirty=False)
print('MC_THROAT_DELIVERY_ASSETS_READY throats=%d gather=3 swallow=2' % len(throats))
