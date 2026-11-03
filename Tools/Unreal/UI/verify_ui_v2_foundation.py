"""Focused idle-editor render checks after author_ui_v2_foundation.py.

Does not change global language, player settings, level or saved widget defaults.
Actual F3/focus/first-use behavior still requires a short solo PIE check.
"""
import json
from pathlib import Path
import unreal as u

assert not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor()
OUT = Path('E:/CodexWork/MessControl/Validation/UIFoundation_20261103')
OUT.mkdir(parents=True, exist_ok=True)
path = '/Game/Gameplay/UI/V2/WBP_UIUniversalPreview.WBP_UIUniversalPreview_C'
cls = u.load_class(None, path)
assert cls, 'Author the foundation assets first'
before = u.MCGameplayUIFactory.get_presentation_version()
report = {'renders': [], 'runtime_check': 'pending solo PIE', 'visual_acceptance': 'pending image inspection'}
try:
    for name, width, height, culture, scale, version in (
        ('New_RU_1080p', 1920, 1080, 'ru', 1.0, u.MCUIVersion.NEW),
        ('New_DE_720p', 1280, 720, 'de', 1.0, u.MCUIVersion.NEW),
        ('New_JA_720p', 1280, 720, 'ja', 1.0, u.MCUIVersion.NEW),
        ('New_RU_Ultrawide', 2560, 1080, 'ru', 1.0, u.MCUIVersion.NEW),
        ('New_ZH_DPI150', 1920, 1080, 'zh-Hans', 1.5, u.MCUIVersion.NEW),
        ('Current_EN_1080p', 1920, 1080, 'en', 1.0, u.MCUIVersion.CURRENT),
    ):
        file = OUT / (name + '.png')
        ok = u.MCGameplayUIEditorLibrary.render_gameplay_ui_preview(
            cls, str(file), u.IntPoint(width, height), 'Default', culture, 0.0, scale, True, version)
        assert ok and file.exists() and file.stat().st_size > 0, name
        report['renders'].append({'path': str(file), 'culture': culture, 'size': [width, height], 'scale': scale})
    assert u.MCGameplayUIFactory.get_presentation_version() == before, 'Preview changed runtime selection'
    report['result'] = 'renders generated; inspect before accepting'
finally:
    (OUT / 'preview_checks.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
u.log('MC_UI_V2_PREVIEWS ' + json.dumps(report, ensure_ascii=False))
