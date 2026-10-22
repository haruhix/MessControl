"""Grow only the legacy Scoreboard's outer SizeBox from 490 to 552 units.

The added voice controls can wrap the local microphone action onto two lines.
The saved v10 frame predates adaptive ScoreDesignSize and clips the fourth row.
This authoring repair retains its width, hierarchy, children and materials.
No native build, PIE, network operation, microphone capture or SaveAll occurs.
"""
from __future__ import annotations

import hashlib
import json
from pathlib import Path

import unreal as u

ROOT = Path('E:/DEVGAME/MessControl')
PACKAGE = '/Game/Gameplay/UI/WBP_Scoreboard_v10'
OBJECT = PACKAGE + '.WBP_Scoreboard_v10'
TARGET_HEIGHT = 552.0


def dirty():
    return sorted(p.get_path_name() for p in
        list(u.EditorLoadingAndSavingUtils.get_dirty_content_packages()) +
        list(u.EditorLoadingAndSavingUtils.get_dirty_map_packages()))


def find_frame():
    frame = u.find_object(None, OBJECT + ':WidgetTree.ScoreFrame')
    assert isinstance(frame, u.Border), 'The saved artist frame was not found'
    current = frame.get_parent()
    while current and not isinstance(current, u.SizeBox):
        current = current.get_parent()
    assert current, 'No outer SizeBox above the saved artist frame'
    assert current.get_outer() == frame.get_outer(), 'The target belongs to another tree'
    return frame, current


def inventory():
    prefix = OBJECT + ':WidgetTree.'
    result = []
    for widget in u.ObjectIterator(u.Widget):
        if not widget.get_path_name().startswith(prefix):
            continue
        parent = widget.get_parent()
        result.append(dict(name=widget.get_name(), native_class=widget.get_class().get_path_name(),
            parent=parent.get_name() if parent else None))
    return sorted(result, key=lambda value: value['name'])


def flag(widget, python_name, native_name):
    for name in (python_name, native_name):
        try:
            return bool(widget.get_editor_property(name))
        except Exception:
            continue
    raise AssertionError('Cannot read the authored layout policy: ' + native_name)


def run(action='plan', *, output='E:/CodexWork/MessControlFrontEndBranch9_20261021/ScoreboardHeightRepair01'):
    assert action in ('plan', 'apply'), action
    assert Path(u.Paths.project_dir()).resolve() == ROOT, 'Wrong project checkout'
    assert not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor(), 'Preserve the active Play session'
    assert not dirty(), 'Preserve unsaved packages before authoring'
    asset = u.load_asset(PACKAGE)
    assert asset, PACKAGE
    assert not dirty(), 'Loading exposed unsaved changes; preserve them before authoring'
    frame, size = find_frame()
    before_height = float(size.get_editor_property('height_override'))
    before_width = float(size.get_editor_property('width_override'))
    assert before_height in (490.0, TARGET_HEIGHT), 'Preserve other artist-authored heights'
    assert flag(size, 'override_height_override', 'bOverride_HeightOverride'), 'Preserve an unbounded artist layout'
    assert flag(size, 'override_width_override', 'bOverride_WidthOverride'), 'Preserve the current width policy'
    before = inventory()
    names = {row['name'] for row in before}
    assert {'ScoreVoiceHeader', 'ScorePlayer4Mute', 'ScorePlayer4Row'} <= names, 'Persist voice controls before repairing their frame'
    report = dict(action=action, package=PACKAGE, target=size.get_path_name(),
        before_height=before_height, after_height=TARGET_HEIGHT, preserved_width=before_width,
        preserved_tree=before, applied=False, previews=[])
    if action == 'plan':
        u.log(json.dumps(report, ensure_ascii=False, indent=2))
        return report
    out = Path(output).resolve()
    assert out.drive.upper() == 'E:', 'All output must remain on E:'
    assert not out.exists() or not any(out.iterdir()), 'Choose a fresh backup/output directory'
    out.mkdir(parents=True, exist_ok=True)
    source = ROOT / 'Content/Gameplay/UI/WBP_Scoreboard_v10.uasset'
    data = source.read_bytes()
    backup = out / 'BeforePackages/WBP_Scoreboard_v10.uasset'
    backup.parent.mkdir(parents=True, exist_ok=True)
    backup.write_bytes(data)
    assert hashlib.sha256(backup.read_bytes()).digest() == hashlib.sha256(data).digest()
    report['backup'] = dict(file=str(backup), sha256=hashlib.sha256(data).hexdigest())
    (out / 'Plan.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    if before_height != TARGET_HEIGHT:
        asset.modify()
        size.modify()
        size.set_height_override(TARGET_HEIGHT)
        u.BlueprintEditorLibrary.compile_blueprint(asset)
        assert 'ERROR' not in str(asset.get_editor_property('status')).upper(), 'Scoreboard Blueprint compile failed'
        assert u.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False), PACKAGE
        report['applied'] = True
    frame, actual = find_frame()
    assert abs(float(actual.get_editor_property('height_override')) - TARGET_HEIGHT) < .001
    assert abs(float(actual.get_editor_property('width_override')) - before_width) < .001, 'Width changed unexpectedly'
    assert inventory() == before, 'Saved artist hierarchy changed unexpectedly'
    cls = u.load_class(None, OBJECT + '_C')
    assert cls, 'Compiled Scoreboard class unavailable'
    for culture in ('ru', 'en'):
        for width, height in ((1280, 720), (1440, 900)):
            preview = out / f'ScoreboardVoice_{culture}_{width}x{height}.png'
            assert u.MCGameplayUIEditorLibrary.render_gameplay_ui_preview(
                cls, str(preview), u.IntPoint(width, height), 'Default', culture, .5), str(preview)
            report['previews'].append(str(preview))
    assert not dirty(), 'Unexpected unsaved packages were preserved'
    report['validation'] = 'One saved height property, preserved width/tree, Blueprint compile and four settled Designer renders; visual review and PIE are separate.'
    (out / 'Authoring.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    u.log('MC_SCOREBOARD_VOICE_HEIGHT_REPAIR_COMPLETE ' + str(out / 'Authoring.json'))
    return report
