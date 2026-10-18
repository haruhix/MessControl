"""Three isolated renders for the changed single-pass progress surface.

Temporary in-memory palette only; no asset save or PIE. Pixel analysis is done
outside UE on the exported transparent PNGs. Existing theme restores in finally.
"""
import json
from pathlib import Path
import unreal as u

root = Path(u.Paths.project_dir()).resolve()
assert root.drive.upper() == 'E:'
assert not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor()
theme = u.MCGameplayUIFactory.get_master_theme()
original = theme.get_editor_property('palette').copy()
out = root / 'Saved/GameplayUI/Polish/AlphaFill'
out.mkdir(parents=True, exist_ok=True)
report = {'renders': [], 'restored': False}
try:
    palette = original.copy()
    # Isolate the progress surface: no text, surrounding panel, rim or gloss.
    for field in ('marine', 'on_marine', 'accent', 'cream'):
        color = palette.get_editor_property(field).copy()
        color.a = 0.0
        palette.set_editor_property(field, color)
    cls = u.load_class(None, '/Game/Gameplay/UI/WBP_Stamina_Phase1.WBP_Stamina_Phase1_C')
    assert cls
    for name, state, alpha, percent in (('fill14_alpha35', 'LowResources', .35, .14),
                                       ('fill72_alpha35', 'Default', .35, .72),
                                       ('fill72_alpha0', 'Default', 0., .72)):
        palette.set_editor_property('stamina', u.LinearColor(.08, .72, .48, alpha))
        # Low stamina uses Warning, so isolate that semantic token too.
        palette.set_editor_property('warning', u.LinearColor(.08, .72, .48, alpha))
        theme.set_editor_property('palette', palette)
        theme.notify_theme_changed()
        file = out / (name + '.png')
        assert u.MCGameplayUIEditorLibrary.render_gameplay_ui_preview(
            cls, str(file), u.IntPoint(336, 80), state, 'ru', 0.)
        report['renders'].append(dict(path=str(file), alpha=alpha, percent=percent))
finally:
    theme.set_editor_property('palette', original)
    theme.notify_theme_changed()
    report['restored'] = theme.get_editor_property('palette').export_text() == original.export_text()
    (out / 'render_report.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
assert report['restored']
u.log('MC_UI_POLISH_ALPHA_EXPORT PASS')
