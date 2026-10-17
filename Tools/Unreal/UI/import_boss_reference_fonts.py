"""Import the two isolated boss-reference fonts in the idle UE 5.8 editor.

Run inside Unreal Python; this file does not start an editor, PIE or a build.
Sources and OFL notices live in ArtSource, not a temporary/Saved directory.
Creates only the four named FontFace/Runtime Font assets. Existing complete
pairs are validated and preserved. Does not assign widget/theme/CDO properties.
"""
import hashlib
import json
from pathlib import Path

import unreal as u


ROOT = Path(u.Paths.project_dir()).resolve()
assert ROOT.drive.upper() == 'E:', 'Task outputs must remain on drive E:'
SOURCE = ROOT / 'ArtSource/UI/BossReferenceMatch_20261017/Fonts'
DESTINATION = '/Game/Gameplay/UI/Fonts'
REPORT_PATH = ROOT / 'Saved/GameplayUI/boss_reference_font_import.json'
SPECS = (
    ('MCBossNames-Regular.ttf', 'FF_MCBossNames_Regular'),
    ('MCBossDigits-Regular.ttf', 'FF_MCBossDigits_Regular'),
)


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def validate_pair(face_path, font_path, source_file):
    face = u.load_asset(face_path)
    font = u.load_asset(font_path)
    require(isinstance(face, u.FontFace), 'Expected FontFace: ' + face_path)
    require(isinstance(font, u.Font), 'Expected companion UFont: ' + font_path)
    require(font.get_editor_property('font_cache_type') == u.FontCacheType.RUNTIME,
            'Companion must use runtime font caching: ' + font_path)
    require(Path(face.get_editor_property('source_filename')).resolve() == source_file,
            'Existing FontFace has a different source; preserving it: ' + face_path)
    # UE Python protects CompositeFont.DefaultTypeface. Do not access those
    # internal members: EditorFactories.cpp constructs the Default -> FontFace
    # reference, and actual glyph rendering verifies it in the subsequent QA.
    return face, font


def package_exists_on_disk(asset_path):
    require(asset_path.startswith('/Game/Gameplay/UI/Fonts/'), 'Unexpected font package: ' + asset_path)
    return (ROOT / 'Content' / (asset_path[len('/Game/'):] + '.uasset')).is_file()


def main():
    levels = u.get_editor_subsystem(u.LevelEditorSubsystem)
    require(not levels.is_in_play_in_editor(), 'Font import requires an idle editor')
    assets = u.AssetToolsHelpers.get_asset_tools()
    lib = u.EditorAssetLibrary
    manifest = json.loads((SOURCE / 'provenance.json').read_text(encoding='utf-8'))
    records = {entry['filename']: entry for entry in manifest}
    report = {'status': 'running', 'source_directory': str(SOURCE), 'assets': []}

    # Preflight both complete inputs and every destination before any import.
    for filename, face_name in SPECS:
        source_file = (SOURCE / filename).resolve()
        record = records[filename]
        require(source_file.is_file(), 'Missing font source: ' + str(source_file))
        require(hashlib.sha256(source_file.read_bytes()).hexdigest() == record['sha256'],
                'Font SHA256 does not match provenance: ' + filename)
        require(record['missing_ru_digits_percent'] == '', 'Incomplete RU/digit coverage: ' + filename)
        require((SOURCE / record['license_file']).is_file(), 'Missing OFL notice: ' + filename)
        face_path = DESTINATION + '/' + face_name
        font_path = face_path + '_Font'
        face_exists = lib.does_asset_exist(face_path)
        font_exists = lib.does_asset_exist(font_path)
        require(face_exists == font_exists,
                'Incomplete existing pair; preserving existing asset. Inspect ' + face_path + ' and ' + font_path)
        if face_exists:
            validate_pair(face_path, font_path, source_file)

    for filename, face_name in SPECS:
        source_file = (SOURCE / filename).resolve()
        face_path = DESTINATION + '/' + face_name
        font_path = face_path + '_Font'
        preserved = lib.does_asset_exist(face_path)
        if not preserved:
            factory = u.FontFileImportFactory()
            # UE 5.8 EditorFactories.cpp creates <FaceName>_Font as a runtime
            # UFont with one Default typeface referencing the new FontFace.
            # Explicit batch policy plus automated=True avoids the Yes/No dialog.
            factory.set_editor_property('batch_create_font_asset',
                                        u.BatchCreateFontAsset.CREATE_IF_NO_FONT_EXISTS)
            task = u.AssetImportTask()
            task.set_editor_property('filename', str(source_file))
            task.set_editor_property('destination_path', DESTINATION)
            task.set_editor_property('destination_name', face_name)
            task.set_editor_property('factory', factory)
            task.set_editor_property('automated', True)
            task.set_editor_property('replace_existing', False)
            task.set_editor_property('replace_existing_settings', False)
            task.set_editor_property('save', False)
            assets.import_asset_tasks([task])
        face, font = validate_pair(face_path, font_path, source_file)
        saved_packages = []
        # A previous failed validation may leave a complete factory-created
        # pair in memory/registry without either package saved. Resume that pair
        # after its source identity/class/runtime-cache validation above.
        for path, asset in ((face_path, face), (font_path, font)):
            if not package_exists_on_disk(path):
                require(lib.save_loaded_asset(asset, only_if_is_dirty=False), 'Could not save ' + path)
                require(package_exists_on_disk(path), 'Saved package is missing on disk: ' + path)
                saved_packages.append(path)
        report['assets'].append({'font_face': face_path, 'runtime_font': font_path,
                                 'typeface': 'Default', 'preserved': preserved,
                                 'saved_packages': saved_packages,
                                 'typeface_validation': 'unavailable: protected UE property; verified factory construction, glyph render QA pending',
                                 'source_sha256': records[filename]['sha256']})

    report['status'] = 'passed'
    REPORT_PATH.parent.mkdir(parents=True, exist_ok=True)
    REPORT_PATH.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    u.log('MC_BOSS_REFERENCE_FONTS PASS ' + json.dumps(report, ensure_ascii=False))
    return report


if __name__ == '__main__':
    main()
