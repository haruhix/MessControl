"""Apply the approved PSD HUD fixes to the existing saved New UI packages.

Run only after building the matching Editor source. The root uses the bounded
in-place HUD helper; component migrations retain existing graph definitions.
"""
import hashlib
import json
import shutil
from pathlib import Path
import unreal as u

ROOT = Path(u.Paths.project_dir()).resolve()
OUT = Path(globals().get('MC_PSD_HUD_OUTPUT', 'E:/CodexWork/MessControl/Validation/UIPSD_HUD_20261105/Authoring01')).resolve()
BASE = '/Game/Gameplay/UI/V2/'
assert ROOT.drive.upper() == OUT.drive.upper() == 'E:'
assert '-MCUIFoundationOwned' in u.SystemLibrary.get_command_line()
assert not u.EditorLevelLibrary.get_pie_worlds(True)
assert not u.EditorLoadingAndSavingUtils.get_dirty_content_packages()
assert not u.EditorLoadingAndSavingUtils.get_dirty_map_packages()
assert hasattr(u.MCGameplayUIEditorLibrary, 'apply_psd_gameplay_hud_layout')
assert not OUT.exists(), 'Use a fresh report directory'
OUT.mkdir(parents=True)
report = {'passed': False, 'assets': [], 'scope': 'Existing task-owned UI packages; no profile or gameplay data mutation'}
names = ['Reward', 'Wardrobe', 'Shop', 'Loading', 'HUDExtras', 'Root']

def digest(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()

try:
    for name in names:
        path = ROOT / 'Content/Gameplay/UI/V2' / ('WBP_UI' + name + '.uasset')
        target = OUT / 'BeforePackages' / path.relative_to(ROOT)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(path, target)
        assert digest(path) == digest(target)
        report['assets'].append({'path': BASE + 'WBP_UI' + name, 'beforeSHA256': digest(path)})

    for name in names[:-1]:
        bp = u.load_asset(BASE + 'WBP_UI' + name)
        assert bp
        assert u.MCGameplayUIEditorLibrary.migrate_unified_viewport_layout(bp), name

    root = u.load_asset(BASE + 'WBP_UIRoot')
    assert root
    embedded = {}
    for name in ['HUDExtras', 'Loading']:
        instance = u.find_object(None, root.get_path_name() + ':WidgetTree.Unified' + name)
        assert instance, name
        embedded[name] = instance.get_class().get_path_name()
        assert embedded[name] == BASE + 'WBP_UI' + name + '.WBP_UI' + name + '_C', embedded
    report['embeddedClasses'] = embedded
    # This saved tree predates the named native row. Resolve its exact four
    # existing card holders and use the UMG rename operation (GUID bookkeeping).
    if not u.find_object(None, root.get_path_name() + ':WidgetTree.SkillCards'):
        prefix = root.get_path_name() + ':WidgetTree.'
        key = u.find_object(None, prefix + 'V2SkillKey1')
        row = key.get_parent().get_parent() if key else None
        assert isinstance(row, u.HorizontalBox) and row.get_parent().get_name() == 'Screen_Gameplay'
        assert row.get_children_count() == 4
        for index in range(4):
            card = u.find_object(None, prefix + 'V2Skill' + str(index + 1))
            assert card and card.get_parent() == row.get_child_at(index)
        report['namedExistingSkillRow'] = row.get_path_name()
        args = {'widgetBlueprint': {'refPath': root.get_path_name()},
                'widget': {'refPath': row.get_path_name()}, 'newDisplayName': 'SkillCards'}
        u.ToolsetRegistry.execute_tool('UMGToolSet.UMGToolSet', 'RenameWidget', json.dumps(args))
        assert row.get_name() == 'SkillCards'
        u.BlueprintEditorLibrary.compile_blueprint(root)
        assert u.EditorAssetLibrary.save_loaded_asset(root, only_if_is_dirty=False)
    assert u.MCGameplayUIEditorLibrary.apply_psd_gameplay_hud_layout(root)

    cls = u.load_class(None, BASE + 'WBP_UIRoot.WBP_UIRoot_C')
    defaults = u.get_default_object(cls)
    metrics = defaults.get_editor_property('hud_layout_metrics')
    assert len(metrics) >= 35
    report['metricCount'] = len(metrics)
    report['referenceSize'] = str(defaults.get_editor_property('hud_reference_size'))
    for entry in report['assets']:
        path = ROOT / 'Content' / (entry['path'].removeprefix('/Game/') + '.uasset')
        entry['savedSHA256'] = digest(path)
    assert not u.EditorLoadingAndSavingUtils.get_dirty_content_packages()
    assert not u.EditorLoadingAndSavingUtils.get_dirty_map_packages()
    report['passed'] = True
finally:
    (OUT / 'Report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
print('MC_PSD_HUD_AUTHORING', report['passed'])
