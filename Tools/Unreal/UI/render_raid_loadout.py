"""Render the saved raid loadout and row Designer fixtures without touching player state."""

import json
from pathlib import Path

import unreal as u


assert not u.EditorLevelLibrary.get_pie_worlds(True), 'Render the Designer fixtures after the current Play session.'
out = Path(globals().get('MC_RAID_LOADOUT_OUTPUT', 'E:/CodexWork/MessControl/Validation/RaidRightKit')).resolve()
assert out.drive.upper() == 'E:', out
out.mkdir(parents=True, exist_ok=True)
base = '/Game/Gameplay/UI/V2/'
specs = [
    ('WBP_UIRaidLoadout', ('RaidStock', 'RaidEmpty', 'RaidFull', 'RaidReady', 'RaidGuest', 'RaidAllReady'), (1920, 864)),
    ('WBP_UIRaidPerkRow', ('Default', 'Stock', 'Add', 'Disabled'), (500, 96)),
]
report = []
for name, states, size in specs:
    path = base + name + '.' + name + '_C'
    widget_class = u.load_class(None, path)
    assert widget_class, path
    cases = [(state, 'en', size) for state in states]
    if name == 'WBP_UIRaidLoadout':
        cases += [('RaidStock', 'ru', (1440, 810)), ('RaidFull', 'de', (1280, 720))]
    for state, culture, draw_size in cases:
        file = out / f'{name}_{state}_{culture}.png'
        assert u.MCGameplayUIEditorLibrary.render_gameplay_ui_preview(
            widget_class, str(file), u.IntPoint(*draw_size), state, culture, 0., 1., True, u.MCUIVersion.NEW,
        ), (path, state, culture)
        report.append({'class': path, 'state': state, 'culture': culture, 'size': draw_size, 'file': str(file)})

(out / 'DesignerPreview.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
print('RAID_LOADOUT_DESIGNER_PREVIEWS', len(report))
