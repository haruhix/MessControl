"""Temporary adaptive-icon visual QA; run in the idle project editor.

CPython --prepare-fixtures writes the three alpha PNGs without importing Unreal.
The UE run imports only missing exact QA texture paths, renders OneBoss with
each icon, restores the master BossPortraits map, then deletes only its own new
texture packages. It never saves the theme or edits a widget/CDO/material.
"""
import json
import math
from pathlib import Path
import struct
import sys
import zlib


ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / 'Saved/GameplayUI/BossReference'
FIXTURES = OUT / 'fixtures'
QA_BASE = '/Game/Gameplay/UI/_BossAdaptiveQA'
THEME_PATH = '/Game/Gameplay/UI/Theme/DA_GameplayUITheme'
WIDGET_PATH = '/Game/Gameplay/UI/WBP_BossHealth_Reference.WBP_BossHealth_Reference_C'
SPECS = (('Wide', 256, 96), ('Tall', 96, 256), ('AlphaHole', 160, 160))


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def png_chunk(kind, payload):
    return struct.pack('!I', len(payload)) + kind + payload + struct.pack('!I', zlib.crc32(kind + payload) & 0xffffffff)


def prepare_fixtures():
    """Diagnostic silhouettes, not UI art: asymmetric bars and a real alpha hole."""
    require(ROOT.drive.upper() == 'E:', 'All task output must remain on E:')
    FIXTURES.mkdir(parents=True, exist_ok=True)
    report = []
    for name, width, height in SPECS:
        raw = bytearray()
        opaque = transparent = 0
        for y in range(height):
            raw.append(0)  # PNG scanline filter: None.
            for x in range(width):
                px, py = x + .5, y + .5
                # Ellipse plus one off-centre bump exposes orientation/fit errors.
                nx, ny = (px-width*.5)/(width*.43), (py-height*.5)/(height*.38)
                ellipse = nx*nx + ny*ny <= 1.
                bump = ((px-width*.72)/(width*.15))**2 + ((py-height*.30)/(height*.19))**2 <= 1.
                inside = ellipse or bump
                if name == 'AlphaHole':
                    # Transparent centre and a notch must remain transparent.
                    inside = inside and math.hypot(px-width*.5, py-height*.5) > width*.17
                    inside = inside and not (px > width*.63 and py > height*.63)
                alpha = 255 if inside else 0
                opaque += bool(alpha)
                transparent += not bool(alpha)
                # Distinct halves expose mirroring; transparent RGB stays zero.
                color = (255, 80, 60) if px < width*.5 else (70, 220, 180)
                raw.extend((*color, alpha) if inside else (0, 0, 0, 0))
        filename = FIXTURES / ('T_QA_BossAdaptive_' + name + '.png')
        header = struct.pack('!2I5B', width, height, 8, 6, 0, 0, 0)
        payload = b'\x89PNG\r\n\x1a\n' + png_chunk(b'IHDR', header) + png_chunk(b'IDAT', zlib.compress(bytes(raw), 9)) + png_chunk(b'IEND', b'')
        filename.write_bytes(payload)
        require(opaque > 0 and transparent > 0, 'Invalid alpha fixture: ' + name)
        report.append({'scenario': name, 'source': str(filename), 'size': [width, height],
                       'opaque_pixels': opaque, 'transparent_pixels': transparent})
    return report


def main():
    import unreal as u

    require(Path(u.Paths.project_dir()).resolve() == ROOT, 'Wrong Unreal project')
    require(not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor(), 'QA requires an idle editor')
    fixtures = prepare_fixtures()
    lib = u.EditorAssetLibrary
    assets = u.AssetToolsHelpers.get_asset_tools()
    theme = u.load_asset(THEME_PATH)
    cls = u.load_class(None, WIDGET_PATH)
    require(theme is not None and cls is not None, 'Author the reference theme/WBP before this QA')
    cdo = u.get_default_object(cls)
    assigned_theme = cdo.get_editor_property('theme')
    require(assigned_theme is None or assigned_theme == theme, 'Reference WBP overrides the master theme')
    original = dict(theme.get_editor_property('boss_portraits'))
    created = []
    textures = []  # Hold Python references across the worldless renders.
    report = {'renders': [], 'created_packages': created, 'deleted_packages': [],
              'theme_saved': False, 'visual_acceptance': 'pending', 'cleanup_errors': [],
              'frame_material': str(cdo.get_editor_property('frame_material'))}
    failure = None
    try:
        for fixture in fixtures:
            name = 'T_QA_BossAdaptive_' + fixture['scenario']
            path = QA_BASE + '/' + name
            reused = lib.does_asset_exist(path)
            if not reused:
                # Reserve only an exact path verified absent before this import.
                # Cleanup also handles an import that creates an asset then fails.
                created.append(path)
                task = u.AssetImportTask()
                task.set_editor_property('filename', fixture['source'])
                task.set_editor_property('destination_path', QA_BASE)
                task.set_editor_property('destination_name', name)
                task.set_editor_property('automated', True)
                task.set_editor_property('replace_existing', False)
                task.set_editor_property('save', False)
                assets.import_asset_tasks([task])
            texture = u.load_asset(path)
            require(isinstance(texture, u.Texture2D), 'Expected QA Texture2D: ' + path)
            textures.append(texture)
            if not reused:
                texture.set_editor_property('compression_settings', u.TextureCompressionSettings.TC_EDITOR_ICON)
                texture.set_editor_property('mip_gen_settings', u.TextureMipGenSettings.TMGS_NO_MIPMAPS)
                texture.set_editor_property('lod_group', u.TextureGroup.TEXTUREGROUP_UI)
                texture.set_editor_property('srgb', True)
            portraits = dict(original)
            portraits[u.Name('RottenGuardian')] = texture
            theme.set_editor_property('boss_portraits', portraits)
            theme.notify_theme_changed()
            output = OUT / ('AdaptiveIcon_' + fixture['scenario'] + '_OneBoss.png')
            ok = u.MCGameplayUIEditorLibrary.render_gameplay_ui_preview(
                cls, str(output), u.IntPoint(900, 280), 'OneBoss', 'ru', 1.)
            report['renders'].append(dict(fixture, output=str(output), asset=path, reused=reused, ok=bool(ok)))
            require(ok, 'Adaptive icon render failed: ' + str(output))
    except Exception as exc:
        failure = exc
        report['error'] = str(exc)
    finally:
        # Restore before deleting textures: no temporary QA reference may remain
        # in the shared theme. Preserve the original map, including other IDs.
        try:
            theme.set_editor_property('boss_portraits', original)
            theme.notify_theme_changed()
            report['theme_restored'] = dict(theme.get_editor_property('boss_portraits')) == original
            require(report['theme_restored'], 'Master BossPortraits restoration failed')
        except Exception as exc:
            report['cleanup_errors'].append('Theme restore: ' + str(exc))
        if report.get('theme_restored'):
            textures.clear()
            for path in reversed(created):
                try:
                    if lib.does_asset_exist(path):
                        require(lib.delete_asset(path), 'Could not delete ' + path)
                        report['deleted_packages'].append(path)
                except Exception as exc:
                    report['cleanup_errors'].append(str(exc))
        report['status'] = 'passed' if failure is None and not report['cleanup_errors'] else 'failed'
        OUT.mkdir(parents=True, exist_ok=True)
        (OUT / 'adaptive_icon_report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    require(not report['cleanup_errors'], 'QA cleanup failed; inspect adaptive_icon_report.json')
    if failure is not None:
        raise failure
    u.log('MC_BOSS_ADAPTIVE_ICONS_PASS ' + json.dumps(report, ensure_ascii=False))
    return report


if __name__ == '__main__':
    if '--prepare-fixtures' in sys.argv:
        print(json.dumps(prepare_fixtures(), ensure_ascii=False, indent=2))
    else:
        main()
