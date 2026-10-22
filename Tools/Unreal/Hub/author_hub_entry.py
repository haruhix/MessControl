"""Narrow Play -> Hub and editable guidance migration after an approved Editor build.

run('plan') only reads. run('apply', output=...) backs up exact packages on E:,
rewires only the real authored PlayButton/OnClicked event, appends missing Hub
guidance/Pause-invite controls, and adds the seven localization keys. It leaves
all shared day-selector functions, map placement, theme roles and artist
materials intact. No build, PIE, network, microphone or SaveAll is performed.
"""
from __future__ import annotations

import hashlib
import json
from pathlib import Path
import sys

import unreal as u

ROOT = Path('E:/DEVGAME/MessControl')
BASE = '/Game/Gameplay/UI'
PACKAGES = ['/Game/UI/MainMenu3D/WBP_MainMenu3D', BASE + '/WBP_Hub',
            BASE + '/WBP_MainMenu', BASE + '/WBP_PauseMenu_v10', BASE + '/Data/DT_UI_Localization', BASE + '/WBP_Scoreboard_v10']


def dirty():
    return sorted(p.get_path_name() for p in list(u.EditorLoadingAndSavingUtils.get_dirty_content_packages())
                  + list(u.EditorLoadingAndSavingUtils.get_dirty_map_packages()))


def run(action='plan', *, output='E:/CodexWork/MessControlHubEntry_20261021/Authoring'):
    assert action in ('plan', 'apply'), action
    assert Path(u.Paths.project_dir()).resolve() == ROOT, 'Wrong checkout'
    levels = u.get_editor_subsystem(u.LevelEditorSubsystem)
    assert not levels.is_in_play_in_editor(), 'Preserve the current Play session'
    assert not dirty(), 'Preserve unsaved packages before authoring'
    assert hasattr(u, 'MCHubFrontEndEditorLibrary'), 'Build the new Editor helpers first'
    report = dict(action=action, scope=__doc__, packages=PACKAGES, backups=[], saved=[])
    lib = u.EditorAssetLibrary
    assets = {package: u.load_asset(package) for package in PACKAGES}
    assert all(assets.values()), 'A required saved package is missing'
    if action == 'plan':
        u.log(json.dumps(report, ensure_ascii=False, indent=2))
        return report
    out = Path(output).resolve()
    assert out.drive.upper() == 'E:', 'All outputs must remain on E:'
    assert not out.exists() or not any(out.iterdir()), 'Choose a fresh backup/output folder'
    out.mkdir(parents=True, exist_ok=True)
    for package in PACKAGES:
        source = ROOT / 'Content' / (package.removeprefix('/Game/') + '.uasset')
        assert source.is_file(), source
        data = source.read_bytes()
        target = out / 'BeforePackages' / source.relative_to(ROOT / 'Content')
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
        assert hashlib.sha256(target.read_bytes()).digest() == hashlib.sha256(data).digest()
        report['backups'].append(dict(package=package, file=str(target), sha256=hashlib.sha256(data).hexdigest()))
    (out / 'Plan.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')

    tools = u.MCHubFrontEndEditorLibrary
    # The helper inspects current editable UbergraphPages, never compiled
    # intermediate snapshots or hard-coded historical GUIDs.
    menu3d = assets[PACKAGES[0]]
    assert tools.route_play_button_to_hub(menu3d), 'Could not route the real Play event'
    assert lib.save_loaded_asset(menu3d, only_if_is_dirty=False)
    assert tools.route_play_button_to_hub(menu3d), 'Play route is not idempotent'
    assert tools.add_play_status_presentation(menu3d), 'Could not append the visible room status'
    assert lib.save_loaded_asset(menu3d, only_if_is_dirty=False)
    assert tools.add_play_status_presentation(menu3d), 'Room status append is not idempotent'
    report['saved'].append(PACKAGES[0])
    for package in PACKAGES[1:4]:
        asset = assets[package]
        assert tools.add_hub_guidance_presentation(asset), 'Guidance append failed: ' + package
        assert lib.save_loaded_asset(asset, only_if_is_dirty=False)
        assert tools.add_hub_guidance_presentation(asset), 'Guidance append is not idempotent: ' + package
        report['saved'].append(package)
    scoreboard = assets[BASE + '/WBP_Scoreboard_v10']
    assert tools.add_scoreboard_voice_presentation(scoreboard), 'Scoreboard voice column append failed'
    assert lib.save_loaded_asset(scoreboard, only_if_is_dirty=False)
    assert tools.add_scoreboard_voice_presentation(scoreboard), 'Scoreboard voice append is not idempotent'
    report['saved'].append(BASE + '/WBP_Scoreboard_v10')
    sys.path.insert(0, str(ROOT / 'Tools/Unreal/UI'))
    import author_localization_catalog
    report['localization'] = author_localization_catalog.run('migrate', output_dir=str(out / 'Localization'),
        additional_rows_file=str(ROOT / 'Tools/Unreal/Hub/hub_entry_texts.json'))
    assert not dirty(), 'Unexpected unsaved authoring state was preserved'
    report['deferred'] = ['Actual Play -> Hub solo check, Ready waypoint/zone move and invitation UI check',
                          'Steam friend joins and voice/network behavior are outside this authoring step']
    (out / 'Authoring.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    u.log('MC_HUB_ENTRY_AUTHOR_COMPLETE ' + str(out / 'Authoring.json'))
    return report
