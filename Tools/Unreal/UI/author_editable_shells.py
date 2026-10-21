"""Create missing Designer shells and connect them without replacing artist trees.

Run in an idle, freshly built MessControl Editor. Saves only the three new WBP,
the explicit UI class map, missing HUD children and the embedded menu class pin.
Existing WBP layouts, localization tables and unrelated packages are preserved.
"""
import hashlib
import json
import re
from pathlib import Path

import unreal as u

ROOT = Path('E:/DEVGAME/MessControl')
OUT = Path(globals().get('MC_EDITABLE_UI_OUTPUT',
    'E:/CodexWork/MessControlEditableUI_20261020/Authoring')).resolve()
assert OUT.drive.upper() == 'E:', 'Authoring evidence must stay on E:'
OUT.mkdir(parents=True, exist_ok=True)
lib = u.EditorAssetLibrary
tools = u.MCGameplayUIEditorLibrary
BASE = '/Game/Gameplay/UI'
THEME = BASE + '/Theme/DA_GameplayUITheme'
HOST = '/Game/UI/MainMenu3D/WBP_MainMenu3D'
report = {'created': [], 'preserved': [], 'backups': [], 'roles': {}}

assert not u.get_editor_subsystem(u.UnrealEditorSubsystem).get_game_world(), 'Stop Play before authoring'
dirty = list(u.EditorLoadingAndSavingUtils.get_dirty_content_packages())
dirty += list(u.EditorLoadingAndSavingUtils.get_dirty_map_packages())
assert not dirty, 'Preserve unsaved artist work before authoring: ' + str([p.get_name() for p in dirty])
assert hasattr(tools, 'add_missing_gameplay_hud_presentation'), 'Build the updated Editor module first'
assert hasattr(tools, 'use_authored_main_menu'), 'Build the embedded-menu integration first'
theme = u.load_asset(THEME)
assert theme, THEME
mapping = dict(theme.get_editor_property('widget_classes'))


def resolve_role(role):
    value = next((value for key, value in mapping.items() if str(key).lower() == role.lower()), None)
    if not value:
        return ''
    if hasattr(value, 'get_path_name'):
        return value.get_path_name()
    match = re.search(r'/Game/[^\s\'"<>]+', str(value))
    return match.group(0).rstrip(')') if match else ''


def backup(package):
    file = ROOT / 'Content' / (package.removeprefix('/Game/') + '.uasset')
    if file.exists():
        target = OUT / 'BeforePackages' / file.relative_to(ROOT / 'Content')
        target.parent.mkdir(parents=True, exist_ok=True)
        data = file.read_bytes()
        if target.exists():
            assert target.read_bytes() == data, 'A prior backup differs; use a fresh authoring output folder'
        else:
            target.write_bytes(data)
        report['backups'].append({'package': package, 'file': str(target),
                                  'sha256': hashlib.sha256(data).hexdigest()})


hud_class_path = resolve_role('GameplayHUD')
assert hud_class_path.startswith('/Game/Gameplay/UI/'), hud_class_path
hud_package = hud_class_path.split('.')[0]
for package in (THEME, HOST, hud_package):
    backup(package)

specs = [('Loading', 'WBP_Loading', 'MCLoadingWidget'),
         ('Hub', 'WBP_Hub', 'MCHubWidget'),
         ('MainMenu', 'WBP_MainMenu', 'MCMainMenuWidget')]
for role, name, native in specs:
    package = BASE + '/' + name
    existed = lib.does_asset_exist(package)
    parent = u.load_class(None, '/Script/MessControl.' + native)
    assert parent, native
    asset = tools.create_editable_gameplay_widget(package, parent)
    assert asset, package
    cls = u.load_class(None, package + '.' + name + '_C')
    assert cls, package
    report['preserved' if existed else 'created'].append(package)
    if not resolve_role(role):
        mapping[role] = cls
    report['roles'][role] = package + '.' + name + '_C'

hud = u.load_asset(hud_package)
assert hud and tools.add_missing_gameplay_hud_presentation(hud), hud_package
host = u.load_asset(HOST)
main_menu = u.load_class(None, BASE + '/WBP_MainMenu.WBP_MainMenu_C')
assert host and main_menu and tools.use_authored_main_menu(host, main_menu), HOST
theme.set_editor_property('widget_classes', mapping)
theme.notify_theme_changed()
assert lib.save_loaded_asset(theme, only_if_is_dirty=False), THEME
report['hud'] = hud_package
report['embedded_host'] = HOST
report['validation'] = 'Blueprint compilation during authoring; runtime checks reported separately'
(OUT / 'Authoring.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
u.log('MC_EDITABLE_UI_AUTHOR_COMPLETE ' + json.dumps(report, ensure_ascii=False))
