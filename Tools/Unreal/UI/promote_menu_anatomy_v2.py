"""Promote the reviewed AnatomyV2 WIP to the existing startup/return map.

Run explicitly inside an idle editor with AnatomyV2 open. Backs up every
overwritten package on E:, saves through Unreal, copies/rebinds Sequencer,
and changes only the MainMenu role of the shared UI theme. No build or play.
The preview map and its original sequence remain available for look work.
"""
from pathlib import Path
import hashlib
import json
import shutil


def run(output='E:/CodexWork/MessControlMenuPromotion_20261022/Promotion'):
    import unreal as u
    root = Path('E:/DEVGAME/MessControl')
    out = Path(output).resolve()
    source_map = '/Game/UI/MenuShowcase/AnatomyV2/L_MenuAnatomyV2'
    target_map = '/Game/Maps/L_MainMenu'
    source_sequence = '/Game/UI/MenuShowcase/AnatomyV2/LS_MenuCleaningV2'
    target_sequence = '/Game/UI/MenuShowcase/AnatomyV2/LS_MainMenuCleaningV2'
    widget_path = '/Game/UI/MenuShowcase/AnatomyV2/UI/WBP_WashNSmashMenu_AdaptiveV2'
    theme_path = '/Game/Gameplay/UI/Theme/DA_GameplayUITheme'
    lib = u.EditorAssetLibrary
    levels = u.get_editor_subsystem(u.LevelEditorSubsystem)
    assert Path(u.Paths.project_dir()).resolve() == root
    assert out.drive.upper() == 'E:' and not out.exists(), 'Choose a fresh E: output'
    assert not levels.is_in_play_in_editor(), 'Preserve active Play'
    assert not list(u.EditorLoadingAndSavingUtils.get_dirty_content_packages())
    assert not list(u.EditorLoadingAndSavingUtils.get_dirty_map_packages())
    world = u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
    assert world.get_path_name().split('.')[0] == source_map, 'Open saved AnatomyV2 first'
    assert not lib.does_asset_exist(target_sequence), 'Live sequence already exists; review it before another promotion'
    seq = lib.load_asset(source_sequence)
    theme = lib.load_asset(theme_path)
    widget = lib.load_asset(widget_path)
    assert seq and theme and widget
    old_roles = dict(theme.get_editor_property('widget_classes'))
    assert old_roles[u.Name('MainMenu')].get_path_name() == '/Game/Gameplay/UI/WBP_WashNSmashMenu.WBP_WashNSmashMenu_C'
    out.mkdir(parents=True)
    report = {'status': 'authoring', 'source_map': source_map, 'target_map': target_map,
              'sequence': target_sequence, 'widget': widget_path, 'backups': [], 'bindings': []}

    def backup(package, extension):
        src = root / 'Content' / (package.removeprefix('/Game/') + extension)
        dst = out / 'Before' / src.relative_to(root)
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, dst)
        sha = hashlib.sha256(src.read_bytes()).hexdigest()
        assert hashlib.sha256(dst.read_bytes()).hexdigest() == sha
        report['backups'].append({'source': str(src), 'backup': str(dst), 'sha256': sha})
        return sha

    backup(target_map, '.umap')
    backup(theme_path, '.uasset')
    source_map_hash = backup(source_map, '.umap')
    source_seq_hash = backup(source_sequence, '.uasset')
    backup(widget_path, '.uasset')
    binding_targets = {}
    for binding in seq.get_bindings():
        objects = list(seq.locate_bound_objects(binding, world))
        assert len(objects) == 1, (binding.get_name(), objects)
        obj_path = objects[0].get_path_name()
        assert obj_path.startswith(world.get_path_name() + ':PersistentLevel.'), obj_path
        assert binding.get_name() not in binding_targets
        binding_targets[binding.get_name()] = obj_path.split(':', 1)[1]

    # Save As through Unreal rewrites the map's package identity and local refs.
    # The external sequence is copied and rebound separately below.
    assert u.EditorLoadingAndSavingUtils.save_map(world, target_map)
    assert levels.load_level(target_map)
    world = u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
    assert world.get_path_name() == target_map + '.L_MainMenu'
    assert world.get_world_settings().get_editor_property('default_game_mode') == u.MCMainMenuGameMode.static_class()
    live = lib.duplicate_asset(source_sequence, target_sequence)
    assert isinstance(live, u.LevelSequence)
    players = [a for a in u.get_editor_subsystem(u.EditorActorSubsystem).get_all_level_actors()
               if isinstance(a, u.LevelSequenceActor)]
    assert len(players) == 1
    players[0].set_sequence(live)
    settings = players[0].get_editor_property('playback_settings')
    assert settings.auto_play and settings.loop_count.value == -1
    u.LevelSequenceEditorBlueprintLibrary.open_level_sequence(live)
    sequencer = u.get_editor_subsystem(u.LevelSequenceEditorSubsystem)
    before = {b.get_name(): (b.get_id().export_text(), len(b.get_tracks())) for b in live.get_bindings()}
    for binding in live.get_bindings():
        if binding.get_parent().is_valid():
            continue  # Component locators are relative to their actor parent.
        obj = u.find_object(None, world.get_path_name() + ':' + binding_targets[binding.get_name()])
        assert isinstance(obj, u.Actor), (binding.get_name(), obj)
        name = binding.get_name()
        sequencer.remove_all_bindings(binding)
        sequencer.add_actors_to_binding([obj], binding)
        binding.set_name(name)
    for binding in live.get_bindings():
        objects = list(live.locate_bound_objects(binding, world))
        expected = world.get_path_name() + ':' + binding_targets[binding.get_name()]
        assert len(objects) == 1 and objects[0].get_path_name() == expected, (binding.get_name(), objects, expected)
        assert before[binding.get_name()] == (binding.get_id().export_text(), len(binding.get_tracks()))
        report['bindings'].append({'name': binding.get_name(), 'object': expected,
                                   'track_count': len(binding.get_tracks()), 'guid_preserved': True})
    lib.set_metadata_tag(live, 'MC_MainMenuPromotion', 'AnatomyV2_WIP_20261022')
    assert lib.save_loaded_asset(live)
    u.LevelSequenceEditorBlueprintLibrary.close_level_sequence()
    assert levels.save_current_level()
    assert u.BlueprintEditorLibrary.compile_blueprint(widget) is not False
    assert 'ERROR' not in str(widget.get_editor_property('status')).upper()
    assert lib.save_loaded_asset(widget)
    cls = u.load_class(None, widget_path + '.WBP_WashNSmashMenu_AdaptiveV2_C')
    assert cls and isinstance(u.get_default_object(cls), u.MCMainMenuWidget)
    new_roles = dict(old_roles)
    new_roles[u.Name('MainMenu')] = cls
    theme.set_editor_property('widget_classes', new_roles)
    assert dict(theme.get_editor_property('widget_classes')) == new_roles
    assert lib.save_loaded_asset(theme)
    # Reload validates saved map/sequence identity and all external locators.
    assert levels.load_level(source_map)
    assert levels.load_level(target_map)
    world = u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
    for binding in live.get_bindings():
        objects = list(live.locate_bound_objects(binding, world))
        assert len(objects) == 1 and objects[0].get_path_name().startswith(world.get_path_name() + ':')
    assert hashlib.sha256((root/'Content/UI/MenuShowcase/AnatomyV2/L_MenuAnatomyV2.umap').read_bytes()).hexdigest() == source_map_hash
    assert hashlib.sha256((root/'Content/UI/MenuShowcase/AnatomyV2/LS_MenuCleaningV2.uasset').read_bytes()).hexdigest() == source_seq_hash
    dirty = [p.get_name() for p in list(u.EditorLoadingAndSavingUtils.get_dirty_content_packages()) +
             list(u.EditorLoadingAndSavingUtils.get_dirty_map_packages())]
    assert not dirty, dirty
    report.update(status='saved_and_reloaded_pending_solo', dirty_packages=dirty,
                  startup_and_return_paths_unchanged=True, source_preview_unchanged=True,
                  other_theme_roles_unchanged=True, build=False, package=False, network=False)
    (out/'Promotion.json').write_text(json.dumps(report, indent=2), encoding='utf8')
    u.log('MC_MAIN_MENU_PROMOTED '+str(out/'Promotion.json'))
    return report


if __name__ == '__main__':
    run()
