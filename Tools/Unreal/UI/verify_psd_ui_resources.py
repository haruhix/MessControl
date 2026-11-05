"""Check saved heading references after restarting the owned Editor.

This checks Editor/Development prewarm roots and saved dependencies. It does
not establish a cold packaged launch or artistic acceptance.
"""
import json
from pathlib import Path
import unreal as u

OUT = Path(globals().get('MC_PSD_RESOURCE_REPORT', 'E:/CodexWork/MessControl/Validation/UIPSD_20261105/SavedResources.json'))
assert OUT.drive.upper() == 'E:' and not OUT.exists()
assert '-MCUIFoundationOwned' in u.SystemLibrary.get_command_line()
assert not u.EditorLevelLibrary.get_pie_worlds(True)
BASE = '/Game/Gameplay/UI/V2'
FONT = BASE + '/Fonts/F_UI_Display'
FACE = BASE + '/Fonts/FF_SpicySauce_Regular'
properties = {'Reward': ('reward_title_font', 64), 'Wardrobe': ('wardrobe_title_font', 48),
              'Shop': ('shop_title_font', 44), 'Team': ('team_title_font', 48)}
report = {'passed': False, 'owners': [], 'scope': 'Saved references and Editor/Development prewarm chain; cold packaged launch not tested'}
registry = u.AssetRegistryHelpers.get_asset_registry()
options = u.AssetRegistryDependencyOptions(include_hard_package_references=True, include_soft_package_references=True)
def deps(path):
    return sorted(str(x) for x in registry.get_dependencies(u.Name(path), options))
def check_info(owner, prop, size):
    info = owner.get_editor_property(prop)
    resource = info.get_editor_property('font_object')
    assert resource and resource.get_path_name() == FONT + '.F_UI_Display', owner.get_path_name()
    assert info.get_editor_property('size') == size
    return {'owner': owner.get_path_name(), 'property': prop, 'font': resource.get_path_name(), 'size': size}
try:
    root = u.load_asset(BASE + '/WBP_UIRoot')
    for name, (prop, size) in properties.items():
        asset = BASE + '/WBP_UI' + name
        cls = u.load_class(None, asset + '.WBP_UI' + name + '_C')
        report['owners'].append(check_info(u.get_default_object(cls), prop, size))
        report['owners'].append(check_info(u.find_object(None, root.get_path_name() + ':WidgetTree.Unified' + name), prop, size))
        assert FONT in deps(asset), asset
    report['fontDependencies'] = deps(FONT)
    assert FACE in report['fontDependencies']
    face = u.load_asset(FACE)
    assert face.get_editor_property('loading_policy') == u.FontLoadingPolicy.INLINE
    theme = BASE + '/Theme/DA_UITheme_V2'
    report['themeDependencies'] = deps(theme)
    for name in properties:
        assert BASE + '/WBP_UI' + name in report['themeDependencies']
    assert BASE + '/WBP_UIRoot' in report['themeDependencies']
    assert not u.EditorLoadingAndSavingUtils.get_dirty_content_packages()
    assert not u.EditorLoadingAndSavingUtils.get_dirty_map_packages()
    report['passed'] = True
finally:
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
print('MC_PSD_SAVED_RESOURCES', report['passed'])
