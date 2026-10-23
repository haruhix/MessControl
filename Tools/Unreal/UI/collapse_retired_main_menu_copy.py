"""Opt-in cleanup of two retired text slots in the current MainMenu role.

Run inside idle Unreal after compiling the native menu. Keeps all widgets,
graphs, localization bindings, actions and artist content; no scene/theme edits.
"""
from pathlib import Path
import hashlib
import json
import re

ROOT = Path('E:/DEVGAME/MessControl')
THEME = '/Game/Gameplay/UI/Theme/DA_GameplayUITheme'
RETIRED = {'MenuMain_Text1': 'UI.MainMenu.Tagline',
           'MenuNavigationHint': 'UI.MainMenu.NavigationHint'}


def require(ok, message):
    if not ok:
        raise RuntimeError(message)


def enum(kind, name):
    normalize = lambda s: ''.join(c for c in s.lower() if c.isalnum())
    found = [getattr(kind, n) for n in dir(kind)
             if n.isupper() and normalize(n) == normalize(name)]
    require(len(found) == 1, 'Unverified enum: ' + str(kind) + '.' + name)
    return found[0]


def run(action='plan', *, target=None,
        output='E:/CodexWork/FragmentTimerMenuCleanup_20261023/MenuAuthoring01'):
    import unreal as u
    require(action in ('plan', 'apply'), 'Expected plan or apply')
    require(Path(u.Paths.project_dir()).resolve() == ROOT, 'Wrong project')
    require(not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor(),
            'Preserve active Play session')

    def dirty():
        return sorted(p.get_path_name() for p in
                      list(u.EditorLoadingAndSavingUtils.get_dirty_content_packages()) +
                      list(u.EditorLoadingAndSavingUtils.get_dirty_map_packages()))

    lib = u.EditorAssetLibrary
    theme = u.load_asset(THEME)
    require(theme, 'Missing gameplay UI theme')
    role = next((v for k, v in dict(theme.get_editor_property('widget_classes')).items()
                 if str(k).lower() == 'mainmenu'), None)
    role_path = role.get_path_name() if role and hasattr(role, 'get_path_name') else str(role)
    match = re.search(r'/Game/[^\s\'"<>\)]+', role_path)
    require(target or match, 'MainMenu role is not an authored /Game class')
    package = target or match.group(0).split('.')[0]
    require(package.startswith('/Game/'), 'Expected one existing /Game WBP')
    name = package.rsplit('/', 1)[-1]
    object_path = package + '.' + name
    asset = u.load_asset(package)
    cls = u.load_class(None, object_path + '_C')
    require(asset and cls, 'Missing saved MainMenu WBP: ' + package)
    defaults = u.get_default_object(cls)
    require(isinstance(defaults, u.MCMainMenuWidget), 'Preserve non-native menu class')
    prefix = object_path + ':WidgetTree.'

    def widgets():
        return {w.get_name(): w for w in u.ObjectIterator(u.Widget)
                if w.get_path_name().startswith(prefix)}

    controls = widgets()
    require('MenuMain_Page' in controls, 'Unexpected authored MainMenu layout')
    bindings = {str(k): str(v) for k, v in
                dict(defaults.get_editor_property('text_bindings')).items()}
    targets = [w for n, w in controls.items() if isinstance(w, u.TextBlock) and
               (bindings.get(n) in RETIRED.values() or
                (n in RETIRED and n not in bindings))]
    require(targets, 'No retired MainMenu copy found; preserve unexpected tree')
    target_names = [w.get_name() for w in targets]
    before_controls = {n: w.get_class().get_path_name() for n, w in controls.items()}
    actions = {n: str(w.get_editor_property('action')) for n, w in controls.items()
               if isinstance(w, u.MCMainMenuActionButton)}
    report = dict(action=action, target=package, role=role_path,
                  collapsed=target_names,
                  dirty_before=dirty(), bindings_preserved=True,
                  controls_preserved=True, actions_preserved=True,
                  changed_packages=[], validation='Plan only; no engine validation performed.')
    if action == 'plan':
        u.log(json.dumps(report, ensure_ascii=False, indent=2))
        return report
    require(not report['dirty_before'], 'Preserve unsaved packages: ' + ', '.join(report['dirty_before']))
    out = Path(output).resolve()
    require(out.drive.upper() == 'E:' and (not out.exists() or not any(out.iterdir())),
            'Choose a fresh E: output directory')
    source = ROOT / 'Content' / (package.removeprefix('/Game/') + '.uasset')
    require(source.is_file(), 'Expected saved source WBP')
    data = source.read_bytes()
    out.mkdir(parents=True, exist_ok=True)
    backup = out / (name + '_BeforeCleanup.uasset')
    backup.write_bytes(data)
    require(backup.read_bytes() == data, 'Backup verification failed')
    report.update(backup=str(backup), sha256_before=hashlib.sha256(data).hexdigest())
    asset.modify()
    for widget in targets:
        widget.modify()
        widget.set_visibility(enum(u.SlateVisibility, 'Collapsed'))
        if isinstance(widget.slot, u.VerticalBoxSlot):
            widget.slot.modify()
            widget.slot.set_padding(u.Margin(0., 0., 0., 0.))
    require(u.BlueprintEditorLibrary.compile_blueprint(asset) is not False, 'WBP compilation failed')
    require('ERROR' not in str(asset.get_editor_property('status')).upper(), 'WBP compiler errors')
    current = widgets()
    require(before_controls == {n: w.get_class().get_path_name() for n, w in current.items()},
            'Existing widget tree changed')
    require(actions == {n: str(current[n].get_editor_property('action')) for n in actions},
            'Native action changed')
    current_defaults = u.get_default_object(u.load_class(None, object_path + '_C'))
    require(bindings == {str(k): str(v) for k, v in
                        dict(current_defaults.get_editor_property('text_bindings')).items()},
            'Localization bindings changed')
    require(all(current[n].get_visibility() == enum(u.SlateVisibility, 'Collapsed')
                for n in target_names), 'Retired copy resurfaced during compile')
    require(lib.save_loaded_asset(asset, only_if_is_dirty=False), 'Could not save target WBP')
    report.update(changed_packages=[package], dirty_after=dirty(),
                  validation='Scoped Blueprint compile/save and control/action/binding preservation; '
                             'runtime cultures/input/visual layout pending root checks.')
    require(not report['dirty_after'], 'Unexpected unsaved package after scoped save')
    (out / 'MenuCleanup.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf8')
    u.log('MC_MAIN_MENU_COPY_CLEANUP_COMPLETE ' + str(out / 'MenuCleanup.json'))
    return report
