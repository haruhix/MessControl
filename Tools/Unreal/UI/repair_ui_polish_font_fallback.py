"""Add engine Droid symbol fallback only to our isolated compact Font asset."""
import json
from pathlib import Path
import unreal as u

root = Path(u.Paths.project_dir()).resolve()
assert root.drive.upper() == 'E:'
assert not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor()
font = u.load_asset('/Game/Gameplay/UI/Fonts/FF_MCGameplay_Condensed_Font')
face = u.load_asset('/Engine/EngineFonts/Faces/DroidSansFallback')
assert isinstance(font, u.Font) and isinstance(face, u.FontFace)
original = font.get_editor_property('composite_font').copy()
candidate = original.copy()
fallback = '(Typeface=(Fonts=((Name="Default",Font=(FontFaceAsset=FontFace\'' + face.get_path_name() + '\')))),ScalingFactor=1.0)'
patch = '(FallbackTypeface=' + fallback + ')'
assert candidate.import_text(patch)
before, after = original.export_text(), candidate.export_text()
assert face.get_path_name() in after, after


def field(text, name):
    # UE exported struct fields are balanced groups. The two protected fields
    # must be identical; only FallbackTypeface is authorized for this repair.
    start = text.index(name + '=') + len(name) + 1
    level = 0
    for index in range(start, len(text)):
        if text[index] == '(':
            level += 1
        elif text[index] == ')':
            level -= 1
        if level == 0:
            return text[start:index + 1]
    raise AssertionError('Unbalanced exported field: ' + name)


for name in ('DefaultTypeface', 'SubTypefaces'):
    assert field(before, name) == field(after, name), name + ' changed unexpectedly'
try:
    font.set_editor_property('composite_font', candidate)
    assert font.get_editor_property('composite_font').export_text() == after
    assert u.EditorAssetLibrary.save_loaded_asset(font, only_if_is_dirty=False)
except Exception:
    font.set_editor_property('composite_font', original)
    raise
u.SystemLibrary.execute_console_command(None, 'Slate.FlushFontCache 1')
report = dict(status='passed', font=font.get_path_name(), fallback_face=face.get_path_name(),
              preserved_fields=['DefaultTypeface', 'SubTypefaces'], before=before, after=after,
              validation='Actual arrows and check glyph render pending; engine face was read only')
out = root / 'Saved/GameplayUI/Polish/font_fallback.json'
out.write_text(json.dumps(report, indent=2), encoding='utf-8')
u.log('MC_UI_POLISH_FONT_FALLBACK PASS')
