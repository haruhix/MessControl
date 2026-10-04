"""Migrate the dev Zombie's known positional-Rotator error; retain dimensions."""
import unreal as u
lib=u.EditorAssetLibrary
profile=lib.load_asset('/Game/Gameplay/Boss/DA_ZombieBoss')
assert profile
transform=profile.get_editor_property('mesh_transform')
transform.set_editor_property('rotation',u.Rotator(pitch=0,yaw=-90,roll=0).quaternion())
profile.set_editor_property('mesh_transform',transform)
assert lib.save_loaded_asset(profile,only_if_is_dirty=False)
bp=lib.load_asset('/Game/Gameplay/Boss/BP_ZombieBoss')
defaults=u.get_default_object(bp.generated_class())
defaults.get_editor_property('mesh').set_relative_transform(transform,False,True)
u.BlueprintEditorLibrary.compile_blueprint(bp)
assert lib.save_loaded_asset(bp,only_if_is_dirty=False)
u.log('MC_ZOMBIE_ORIENTATION_PASS '+str(transform))
