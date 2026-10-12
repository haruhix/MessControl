"""Request one asynchronous Editor Live Coding integration; no build/cook/package."""
import unreal as u

levels = u.get_editor_subsystem(u.LevelEditorSubsystem)
assert not levels.is_in_play_in_editor(), 'Stop PIE before compiling'
world = u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
u.log('MC_APPROVED_LIVE_CODING_REQUEST')
u.SystemLibrary.execute_console_command(world, 'LiveCoding')
u.SystemLibrary.execute_console_command(world, 'LiveCoding.Compile')
