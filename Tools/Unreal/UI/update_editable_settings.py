"""Integrate the prepared settings controls and repair today's editable UI seeds.

Run only after building the new Editor, in an idle saved project. Source assets
are backed up on E:. Settings replacement is limited to its existing page;
other menu pages, maps, gameplay definitions and localization edits are retained.
"""
import hashlib
import json
import sys
from pathlib import Path

import unreal as u

ROOT = Path('E:/DEVGAME/MessControl')
OUT = Path(globals().get('MC_SETTINGS_AUTHOR_OUTPUT',
    'E:/CodexWork/MessControlEditableUI_20261020/SettingsAuthoring')).resolve()
assert OUT.drive.upper() == 'E:'
assert not OUT.exists() or not any(OUT.iterdir()), 'Choose a fresh authoring evidence folder'
OUT.mkdir(parents=True, exist_ok=True)
tools = u.MCGameplayUIEditorLibrary
assert hasattr(tools, 'refresh_main_menu_settings_presentation'), 'Build the new Editor first'
assert not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor()
dirty = lambda: list(u.EditorLoadingAndSavingUtils.get_dirty_content_packages()) + list(
    u.EditorLoadingAndSavingUtils.get_dirty_map_packages())
assert not dirty(), 'Preserve unsaved editor work'
report = dict(backups=[], fonts_repaired=[], previews=[], scope=__doc__)
packages = ('/Game/Gameplay/UI/WBP_Hub', '/Game/Gameplay/UI/WBP_MainMenu',
    '/Game/Gameplay/UI/WBP_GameplayHUD_Phase1', '/Game/Gameplay/UI/Data/DT_UI_Localization')
for package in packages:
    source = ROOT / 'Content' / (package.removeprefix('/Game/') + '.uasset')
    data = source.read_bytes()
    target = OUT / 'BeforePackages' / source.relative_to(ROOT / 'Content')
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(data)
    report['backups'].append(dict(package=package, file=str(target),
        sha256=hashlib.sha256(data).hexdigest()))

hub, menu, hud = (u.load_asset(package) for package in packages[:3])
assert tools.repair_editable_hub_material_snapshots(hub), 'Hub material snapshot repair failed'
assert tools.refresh_main_menu_settings_presentation(menu), 'Settings page integration failed'
# Upgrade only the two old generated action bindings; custom artist keys survive.
defaults = u.get_default_object(u.load_class(None, '/Game/Gameplay/UI/WBP_MainMenu.WBP_MainMenu_C'))
bindings = dict(defaults.get_editor_property('text_bindings'))
remapped = {}
for name, old, new in (
    ('MenuSettings_Action11Label', 'UI.MainMenu.Action.ApplySettings', 'UI.Settings.Apply'),
    ('MenuSettings_Action6Label', 'UI.MainMenu.Action.Back', 'UI.Settings.Back')):
    key = next((key for key in bindings if str(key) == name), None)
    if key is not None and str(bindings[key]) == old:
        bindings[key] = new
        remapped[name] = new
defaults.set_editor_property('text_bindings', bindings)
u.BlueprintEditorLibrary.compile_blueprint(menu)
defaults = u.get_default_object(u.load_class(None, '/Game/Gameplay/UI/WBP_MainMenu.WBP_MainMenu_C'))
actual_bindings = {str(key): str(value) for key, value in defaults.get_editor_property('text_bindings').items()}
assert all(actual_bindings.get(key) == value for key, value in remapped.items()), 'Action bindings lost during compile'
assert u.EditorAssetLibrary.save_loaded_asset(menu, only_if_is_dirty=False)

# These named labels were appended by this migration. A transient Slate font
# cannot survive save/load, so seed the shared packaged font when none is saved.
theme = u.load_asset('/Game/Gameplay/UI/Theme/DA_GameplayUITheme')
def belongs_to(item, owner):
    while item:
        if item == owner:
            return True
        item = item.get_outer()
    return False

sizes = [item for item in u.ObjectIterator(u.SizeBox.static_class())
    if item.get_name() == 'MenuSettings_SectionSize' and belongs_to(item, menu)]
assert len(sizes) == 1, 'Expected the generated settings section frame'
# The original menu shell has a fixed height. Keep its Back action visible while
# leaving four audio sliders inside the inner viewport. Respect custom sizing.
report['settings_frame_adjusted'] = False
if abs(float(sizes[0].get_editor_property('height_override')) - 320.) < .01:
    sizes[0].modify()
    sizes[0].set_height_override(280.)
    u.BlueprintEditorLibrary.compile_blueprint(menu)
    assert u.EditorAssetLibrary.save_loaded_asset(menu, only_if_is_dirty=False)
    report['settings_frame_adjusted'] = True

for name, size, bold in (('DirectorMonitorText', 14, False),
        ('DirectorCandidatesText', 11, False), ('DirectorLogButtonText', 11, True)):
    matches = [item for item in u.ObjectIterator(u.TextBlock.static_class())
        if item.get_name() == name and belongs_to(item, hud)]
    assert len(matches) == 1, 'Expected one saved HUD control: ' + name
    label = matches[0]
    if not label.get_editor_property('font').get_editor_property('font_object'):
        label.set_font(theme.make_gameplay_font(size, bold))
        report['fonts_repaired'].append(name)
if report['fonts_repaired']:
    u.BlueprintEditorLibrary.compile_blueprint(hud)
    assert u.EditorAssetLibrary.save_loaded_asset(hud, only_if_is_dirty=False)

sys.path.insert(0, str(ROOT / 'Tools/Unreal/UI'))
import author_localization_catalog
rows = json.loads((ROOT / 'Tools/Unreal/UI/gameplay_ui_texts.json').read_text(encoding='utf-8'))
additions = [row for row in rows if row['Name'].startswith('UI.Settings.')]
additions_file = OUT / 'SettingsTextAdditions.json'
additions_file.write_text(json.dumps(additions, ensure_ascii=False, indent=2), encoding='utf-8')
report['localization'] = author_localization_catalog.run('migrate',
    output_dir=str(OUT / 'Localization'), additional_rows_file=str(additions_file))

# Helpers must be safe to invoke again, without recreating material snapshots.
assert tools.repair_editable_hub_material_snapshots(hub), 'Hub repair is not idempotent'
assert tools.refresh_main_menu_settings_presentation(menu), 'Settings integration is not idempotent'
for widget, states in (
    ('WBP_MainMenu', ('Settings', 'SettingsAudio', 'SettingsLanguage', 'SettingsControls')),
    ('WBP_Hub', ('Selecting', 'Default')), ('WBP_Loading', ('Default',))):
    cls = u.load_class(None, '/Game/Gameplay/UI/' + widget + '.' + widget + '_C')
    for state in states:
        output = OUT / 'Previews' / (widget + '_' + state + '.png')
        assert tools.render_gameplay_ui_preview(cls, str(output), u.IntPoint(1440, 900), state, 'ru', 0.)
        report['previews'].append(str(output))
assert not dirty(), 'Unexpected dirty packages were preserved'
(OUT / 'Authoring.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
u.log('MC_SETTINGS_AUTHOR_COMPLETE ' + str(OUT / 'Authoring.json'))
