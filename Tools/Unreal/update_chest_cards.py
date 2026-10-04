"""Compile the reward Blueprint and save the F3-only boss map migration.

Run with the current Editor module. Keep authored chest dimensions and perk rows.
"""
import unreal as u

lib = u.EditorAssetLibrary
level = u.get_editor_subsystem(u.LevelEditorSubsystem)
actors = u.get_editor_subsystem(u.EditorActorSubsystem)
assert not u.EditorLoadingAndSavingUtils.get_dirty_map_packages(), 'Preserve unsaved map edits'
assert not u.EditorLoadingAndSavingUtils.get_dirty_content_packages(), 'Preserve unsaved asset edits'
assert level.load_level('/Game/Maps/L_Mouth')
reward = lib.load_asset('/Game/Gameplay/Roguelike/BP_RewardChest')
assert reward
u.BlueprintEditorLibrary.compile_blueprint(reward)
defaults = u.get_default_object(reward.generated_class())
defaults.set_editor_property('lockpicking_seconds', 5.)
assert lib.save_loaded_asset(reward, only_if_is_dirty=False)
chests = 0
for actor in actors.get_all_level_actors():
    if actor.actor_has_tag('MC_ZombieBoss'):
        assert actors.destroy_actor(actor)
    elif isinstance(actor, u.MCRewardChest):
        # Blueprint compilation reconstructs instances while retaining artist scale.
        chests += 1
        hinge = actor.get_editor_property('lid_pivot').get_editor_property('relative_location')
        assert hinge.x < 0 and abs(hinge.y) < .01, str(hinge)
assert level.save_current_level()
u.log('MC_CHEST_CARDS_AUTHOR_PASS placed_chests=' + str(chests) + '; normal boss absent')
