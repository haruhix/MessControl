"""Append the editable Voice tab and register saved voice settings/indicator WBP.

Run only after an approved Editor build, in an idle saved project. Existing
settings tabs, layout, palette, artist material instances and custom role
assignments are retained. Does not start PIE, capture audio or test networking.
"""
import hashlib
import json
import re
import sys
from pathlib import Path

import unreal as u

ROOT = Path('E:/DEVGAME/MessControl')
OUT = Path(globals().get('MC_VOICE_AUTHOR_OUTPUT',
    'E:/CodexWork/MessControlVoice_20261021/Authoring')).resolve()
assert OUT.drive.upper() == 'E:', 'All authoring evidence must stay on E:'
assert not OUT.exists() or not any(OUT.iterdir()), 'Choose a fresh evidence folder'
OUT.mkdir(parents=True, exist_ok=True)
BASE = '/Game/Gameplay/UI'
THEME = BASE + '/Theme/DA_GameplayUITheme'
MENU = BASE + '/WBP_MainMenu'
tools = u.MCGameplayUIEditorLibrary
lib = u.EditorAssetLibrary
dirty = lambda: list(u.EditorLoadingAndSavingUtils.get_dirty_content_packages()) + list(
    u.EditorLoadingAndSavingUtils.get_dirty_map_packages())
assert not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor(), 'Stop Play first'
assert not dirty(), 'Preserve unsaved editor work before authoring'
assert hasattr(tools, 'add_main_menu_voice_presentation'), 'Build the new Editor module first'
report = dict(scope=__doc__, backups=[], created=[], preserved=[], roles={}, previews=[])


def backup(package):
    source = ROOT / 'Content' / (package.removeprefix('/Game/') + '.uasset')
    if not source.exists():
        return
    data = source.read_bytes()
    target = OUT / 'BeforePackages' / source.relative_to(ROOT / 'Content')
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(data)
    report['backups'].append(dict(package=package, file=str(target),
        sha256=hashlib.sha256(data).hexdigest()))


for package in (THEME, MENU, BASE + '/WBP_VoiceSettings', BASE + '/WBP_VoiceIndicator',
                BASE + '/Data/DT_UI_Localization'):
    backup(package)
theme = u.load_asset(THEME)
assert theme, THEME
mapping = dict(theme.get_editor_property('widget_classes'))


def role_path(role):
    value = next((value for key, value in mapping.items()
                  if str(key).lower() == role.lower()), None)
    if not value:
        return ''
    if hasattr(value, 'get_path_name'):
        return value.get_path_name()
    match = re.search(r'/Game/[^\s\'"<>]+', str(value))
    return match.group(0).rstrip(')') if match else ''


for role, name, native in (
        ('VoiceSettings', 'WBP_VoiceSettings', 'MCVoiceSettingsWidget'),
        ('VoiceIndicator', 'WBP_VoiceIndicator', 'MCVoiceIndicatorWidget')):
    package = BASE + '/' + name
    parent = u.load_class(None, '/Script/MessControl.' + native)
    assert parent, native
    existed = lib.does_asset_exist(package)
    asset = tools.create_editable_gameplay_widget(package, parent)
    assert asset, 'Could not create/preserve ' + package
    cls = u.load_class(None, package + '.' + name + '_C')
    assert cls, package
    report['preserved' if existed else 'created'].append(package)
    existing = role_path(role)
    if existing:
        selected = u.load_class(None, existing)
        assert selected and isinstance(u.get_default_object(selected), getattr(u, native)), (
            'Unrelated custom factory role preserved: ' + role + ' = ' + existing)
        report['roles'][role] = dict(path=existing, preserved=True)
    else:
        mapping[role] = cls
        report['roles'][role] = dict(path=package + '.' + name + '_C', preserved=False)

# The append helper constructs its seed through the actual VoiceSettings factory
# role, so save that class assignment before cloning the new nested child.
theme.set_editor_property('widget_classes', mapping)
theme.notify_theme_changed()
assert lib.save_loaded_asset(theme, only_if_is_dirty=False), THEME
menu = u.load_asset(MENU)
assert menu and tools.add_main_menu_voice_presentation(menu), 'Voice tab append failed'
assert tools.add_main_menu_voice_presentation(menu), 'Voice append is not idempotent'

sys.path.insert(0, str(ROOT / 'Tools/Unreal/UI'))
import author_localization_catalog
catalog = json.loads((ROOT / 'Tools/Unreal/UI/gameplay_ui_texts.json').read_text(encoding='utf-8'))
rows = [row for row in catalog if row['Name'].startswith('UI.Voice.')
        or row['Name'].startswith('UI.Reward.Team')
        or row['Name'] == 'UI.Settings.Tab.Voice']
assert rows, 'Voice localization keys must be added before authoring'
additions = OUT / 'VoiceTextAdditions.json'
additions.write_text(json.dumps(rows, ensure_ascii=False, indent=2), encoding='utf-8')
report['localization'] = author_localization_catalog.run('migrate',
    output_dir=str(OUT / 'Localization'), additional_rows_file=str(additions))

for widget, state, size in (
        ('WBP_MainMenu', 'SettingsVoice', u.IntPoint(1440, 900)),
        ('WBP_VoiceSettings', 'Default', u.IntPoint(720, 1000)),
        ('WBP_VoiceIndicator', 'Default', u.IntPoint(300, 140))):
    cls = u.load_class(None, BASE + '/' + widget + '.' + widget + '_C')
    output = OUT / 'Previews' / (widget + '_' + state + '.png')
    assert tools.render_gameplay_ui_preview(cls, str(output), size, state, 'ru', .6)
    report['previews'].append(str(output))
assert not dirty(), 'Unexpected unsaved packages were preserved'
report['validation'] = 'Blueprint compile/save and Designer previews only; microphone, PIE and network checks are separate.'
(OUT / 'Authoring.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
u.log('MC_VOICE_UI_AUTHOR_COMPLETE ' + str(OUT / 'Authoring.json'))
