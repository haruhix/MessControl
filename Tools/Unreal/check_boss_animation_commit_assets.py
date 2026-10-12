"""Read saved encounter animation references and their runtime package closure.

Uses the MessControl Editor without interrupting an existing PIE session.
No assets/maps/settings are modified.
Records actual references, compatible skeletons, gesture curves and dependency
packages so the animation commit can be checked without adding local prototypes.
"""
import hashlib
import json
from pathlib import Path
import unreal as u


def main():
    root = Path(u.Paths.project_dir()).resolve()
    assert root == Path('E:/DEVGAME/MessControl').resolve(), 'Wrong project'
    pie_before = u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor()
    dirty_before = {package.get_path_name() for package in
                    u.EditorLoadingAndSavingUtils.get_dirty_content_packages()}
    profile_path = '/Game/Gameplay/CoreLoop/DA_NutRain'
    assert profile_path not in dirty_before, 'Profile has unsaved edits'
    profile = u.load_asset(profile_path)
    assert profile is not None
    data = json.loads(u.ToolsetLibrary.get_object_properties(profile, ['Settings']))
    settings = data.get('Settings', data.get('settings', {}))
    boss = settings.get('Boss', settings.get('boss', {}))
    references = {key: value for key, value in boss.items()
                  if 'animation' in key.lower() or 'skeletalmesh' in key.lower()}
    packages = {profile_path}
    for value in references.values():
        if isinstance(value, str) and value.startswith('/Game/'):
            packages.add(value.split('.')[0])
    assert len(packages) == 27, 'Unexpected active encounter animation scope'
    assert not packages.intersection(dirty_before), 'An animation target has unsaved edits'
    lib = u.EditorAssetLibrary
    meshes = {role: u.load_asset(boss[role+'SkeletalMesh']) for role in ('tank', 'mage')}
    sequences = []
    for key, value in sorted(references.items()):
        if not isinstance(value, str) or not value.startswith('/Game/'):
            continue
        asset = u.load_asset(value)
        assert asset is not None, 'Missing referenced asset: '+value
        if isinstance(asset, u.AnimSequence):
            role = 'mage' if key.lower().startswith('mage') else 'tank'
            assert asset.get_editor_property('skeleton') == meshes[role].get_editor_property('skeleton'), value
            assert asset.get_play_length() > 0 and not asset.get_editor_property('enable_root_motion'), value
            entry = dict(slot=key, asset=value, seconds=asset.get_play_length(),
                         skeleton=asset.get_editor_property('skeleton').get_path_name())
            if '/MCMixamo/' in value:
                times, values = u.AnimationLibrary.get_float_keys(asset, 'MCSpellGesture')
                assert len(times) == 2 and all(abs(float(v)-1.) < .0001 for v in values), value
                entry.update(gesture_keys=[float(v) for v in values],
                    source_original=lib.get_metadata_tag(asset, 'MCWizardOriginalAsset'),
                    integration_owner=lib.get_metadata_tag(asset, 'MCWizardMixamoIntegration'))
            sequences.append(entry)
    registry = u.AssetRegistryHelpers.get_asset_registry()
    registry.scan_paths_synchronous(['/Game/Gameplay/CoreLoop'], True)
    options = u.AssetRegistryDependencyOptions()
    options.set_editor_property('include_hard_package_references', True)
    options.set_editor_property('include_soft_package_references', True)
    queue = sorted(packages)
    dependencies = {}
    while queue:
        package = queue.pop()
        if package in dependencies:
            continue
        paths = sorted(str(path) for path in registry.get_dependencies(package, options)
                       if str(path).startswith('/Game/'))
        dependencies[package] = paths
        queue.extend(path for path in paths if path not in dependencies)
    files = []
    for package in sorted(dependencies):
        relative = 'Content/'+package.removeprefix('/Game/')+'.uasset'
        filename = root/relative
        assert filename.is_file(), 'Missing dependency package on disk: '+relative
        files.append(dict(package=package, file=relative, bytes=filename.stat().st_size,
                          sha256=hashlib.sha256(filename.read_bytes()).hexdigest()))
    dirty_after = {package.get_path_name() for package in
                   u.EditorLoadingAndSavingUtils.get_dirty_content_packages()}
    assert dirty_after == dirty_before, 'Read-only inspection changed dirty asset state'
    report = dict(passed=True, profile=profile_path, references=references,
                  sequences=sequences, dependencies=dependencies, files=files,
                  packages_saved=False, dirty_state_preserved=True,
                  existing_pie_preserved=(u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor() == pie_before),
                  validation='Live reference/skeleton/curve and saved dependency inspection; gameplay is separate')
    output = root/'Saved/Checks/MixamoCommitScope/AssetClosure.json'
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding='utf8')
    u.log('MC_BOSS_ANIMATION_ASSET_CLOSURE_PASS '+str(len(files))+' packages')


if __name__ == '__main__':
    main()
