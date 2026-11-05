"""Finish the five PSD comparison screens in an idle owned Editor.

Requires the matching native source revision to have been explicitly built.
Preserves existing trees/graphs through the bounded native migration, and keeps
Spicy Sauce on the four display headings only. No gameplay/profile changes.
"""
import hashlib
import json
import shutil
from pathlib import Path
import unreal as u

ROOT = Path(u.Paths.project_dir()).resolve()
OUT = Path(globals().get('MC_PSD_OUTPUT', 'E:/CodexWork/MessControl/Validation/UIPSD_20261105/Authoring'))
assert ROOT.drive.upper() == 'E:' and OUT.drive.upper() == 'E:'
assert '-MCUIFoundationOwned' in u.SystemLibrary.get_command_line()
assert not u.EditorLevelLibrary.get_pie_worlds(True)
assert not u.EditorLoadingAndSavingUtils.get_dirty_content_packages()
assert not u.EditorLoadingAndSavingUtils.get_dirty_map_packages()
assert hasattr(u.MCGameplayUIEditorLibrary, 'configure_psd_display_font'), 'Build the matching Editor source first'
assert not OUT.exists(), 'Use a fresh evidence directory'

BASE = '/Game/Gameplay/UI/V2'
LIB = u.EditorAssetLibrary
SOURCE = ROOT / 'ArtSource/UI/V2/Fonts/Spicy Sauce.ttf'
assert hashlib.sha256(SOURCE.read_bytes()).hexdigest() == '793652bff2fd7c75822a6bedccf854cf85ddec4472cc861a7135c00dc6608cdd'
assert (SOURCE.parent / 'OFL.txt').is_file()
OUT.mkdir(parents=True)
report = {'passed': False, 'assets': [], 'fontSource': str(SOURCE),
          'fontAuthor': 'https://resourceboy.com/fonts/spicy-sauce-font/',
          'fontDistribution': 'https://www.1001fonts.com/spicy-sauce-font.html',
          'scope': 'Five saved New UI components and their existing root; no perk icon/content or gameplay data changes'}

def backup(asset):
    path = ROOT / 'Content' / (asset.removeprefix('/Game/') + '.uasset')
    if path.is_file():
        target = OUT / 'BeforePackages' / path.relative_to(ROOT)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(path, target)
        assert target.read_bytes() == path.read_bytes()

try:
    for name in ['Reward', 'Wardrobe', 'Shop', 'Settings', 'Team', 'Root']:
        backup(BASE + '/WBP_UI' + name)
    face_path = BASE + '/Fonts/FF_SpicySauce_Regular'
    if not LIB.does_asset_exist(face_path):
        factory = u.FontFileImportFactory()
        factory.set_editor_property('batch_create_font_asset', u.BatchCreateFontAsset.NO)
        task = u.AssetImportTask()
        for key, value in {'filename': str(SOURCE), 'destination_path': BASE + '/Fonts',
                           'destination_name': 'FF_SpicySauce_Regular', 'factory': factory,
                           'automated': True, 'replace_existing': False, 'save': False}.items():
            task.set_editor_property(key, value)
        u.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    face = u.load_asset(face_path)
    assert isinstance(face, u.FontFace)
    assert Path(face.get_editor_property('source_filename')).resolve() == SOURCE
    face.set_editor_property('loading_policy', u.FontLoadingPolicy.INLINE)
    assert LIB.save_loaded_asset(face, only_if_is_dirty=False)

    font_path = BASE + '/Fonts/F_UI_Display'
    fallback = u.load_asset(BASE + '/Fonts/F_UI_Universal')
    font = u.load_asset(font_path) if LIB.does_asset_exist(font_path) else u.AssetToolsHelpers.get_asset_tools().duplicate_asset('F_UI_Display', BASE + '/Fonts', fallback)
    assert font and fallback
    backup(font_path)
    assert u.MCGameplayUIEditorLibrary.configure_psd_display_font(font, fallback, face)
    assert LIB.save_loaded_asset(font, only_if_is_dirty=False)

    properties = {'Reward': 'reward_title_font', 'Wardrobe': 'wardrobe_title_font',
                  'Shop': 'shop_title_font', 'Team': 'team_title_font'}
    heading_sizes = {'Reward': 64, 'Wardrobe': 48, 'Shop': 44, 'Team': 48}
    for name in ['Reward', 'Wardrobe', 'Shop', 'Settings', 'Team']:
        path = BASE + '/WBP_UI' + name
        bp = u.load_asset(path)
        if name in properties:
            defaults = u.get_default_object(u.load_class(None, path + '.WBP_UI' + name + '_C'))
            info = defaults.get_editor_property(properties[name]).copy()
            info.set_editor_property('font_object', font)
            info.set_editor_property('typeface_font_name', 'Default')
            info.set_editor_property('size', heading_sizes[name])
            defaults.set_editor_property(properties[name], info)
            u.BlueprintEditorLibrary.compile_blueprint(bp)
            assert LIB.save_loaded_asset(bp, only_if_is_dirty=False)
        assert u.MCGameplayUIEditorLibrary.migrate_unified_viewport_layout(bp), path
        report['assets'].append(path)

    root = u.load_asset(BASE + '/WBP_UIRoot')
    for name, prop in properties.items():
        instance = u.find_object(None, root.get_path_name() + ':WidgetTree.Unified' + name)
        assert instance, name
        info = instance.get_editor_property(prop).copy()
        info.set_editor_property('font_object', font)
        info.set_editor_property('typeface_font_name', 'Default')
        info.set_editor_property('size', heading_sizes[name])
        instance.set_editor_property(prop, info)
    u.BlueprintEditorLibrary.compile_blueprint(root)
    assert LIB.save_loaded_asset(root, only_if_is_dirty=False)
    report['assets'].append(BASE + '/WBP_UIRoot')
    # Reflected FontInfo references in the theme's WBP classes and root instances
    # are runtime resources, not editor-only catalog metadata.
    report['resourceReferences'] = {'font': font_path, 'face': face_path,
        'root': BASE + '/WBP_UIRoot', 'owners': properties,
        'preloadEntry': 'FMCPreloadManifest::Collect -> DA_UITheme_V2 -> WidgetClasses (Editor/Development, !UE_BUILD_SHIPPING)',
        'coldPackagedLaunch': 'not validated'}
    assert not u.EditorLoadingAndSavingUtils.get_dirty_content_packages()
    assert not u.EditorLoadingAndSavingUtils.get_dirty_map_packages()
    report['passed'] = True
finally:
    (OUT / 'Report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
print('MC_PSD_AUTHORING', report['passed'])
