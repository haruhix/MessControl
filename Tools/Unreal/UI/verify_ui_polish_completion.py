"""Targeted changed completion states, no PIE or asset mutation."""
import json
import hashlib
from datetime import datetime, timezone
from pathlib import Path
import unreal as u

root = Path(u.Paths.project_dir()).resolve()
assert root.drive.upper() == 'E:'
assert not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor()
out = root / 'Saved/GameplayUI/Polish/Completion'
out.mkdir(parents=True, exist_ok=True)
report = []
for role, state in (('Tutorial', 'FullComplete'), ('Tutorial', 'Ready'), ('Emotes', 'Signals'), ('Emotes', 'Default')):
    name = 'WBP_' + role + '_Phase1'
    cls = u.load_class(None, '/Game/Gameplay/UI/' + name + '.' + name + '_C')
    assert cls
    file = (root / 'Saved/GameplayUI/Polish/Previews/Emotes_Default_ru_1280x720.png'
            if role == 'Emotes' and state == 'Default' else out / (role + '_' + state + '.png'))
    assert u.MCGameplayUIEditorLibrary.render_gameplay_ui_preview(cls, str(file), u.IntPoint(1280,720), state, 'ru', 0.)
    report.append(dict(class_path=cls.get_path_name(), state=state, path=str(file)))
    if role == 'Emotes' and state == 'Default':
        matrix_path = root / 'Saved/GameplayUI/Polish/Previews/report.json'
        matrix = json.loads(matrix_path.read_text(encoding='utf-8'))
        entry = next(item for item in matrix['renders'] if item['id']=='Emotes_Default')
        entry.update(bytes=file.stat().st_size, sha256=hashlib.sha256(file.read_bytes()).hexdigest(), render_pass=37)
        matrix['updated_utc'] = datetime.now(timezone.utc).isoformat()
        matrix_path.write_text(json.dumps(matrix, ensure_ascii=False, indent=2), encoding='utf-8')
(out / 'report.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
u.log('MC_UI_POLISH_COMPLETION_EXPORT PASS')
