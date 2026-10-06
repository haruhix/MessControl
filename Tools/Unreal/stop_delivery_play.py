"""End only this connected MessControl PIE session before inspecting/rebuilding native visuals."""
import unreal

unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_end_play()
unreal.log('MC_DELIVERY_END_PLAY_REQUESTED')
