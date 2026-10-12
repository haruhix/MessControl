"""Request only the focused fragment contact regression in the idle Editor."""
import unreal as u

levels = u.get_editor_subsystem(u.LevelEditorSubsystem)
assert not levels.is_in_play_in_editor(), 'Preserve active PIE'
assert not u.EditorLevelLibrary.get_pie_worlds(True), 'Preserve play worlds'
world = u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
u.log('MC_FRAGMENT_CONTACT_REGRESSION_REQUESTED')
u.SystemLibrary.execute_console_command(world, 'Automation RunTests MessControl.Food.Stack.FragmentsDoNotKnockPlayers')
