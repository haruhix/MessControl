"""Deferred long-name visual QA. --manifest runs without Unreal.

Synthetic stress copy is not approved translation content. The UE run creates
one unsaved QA localization DT, swaps only the master's table reference, renders
fresh worldless HUDs, then restores the reference and deletes its own exact DT.
No source DT, theme, widget, font or language setting is saved/changed globally.
"""
import copy
import json
from pathlib import Path
import sys


ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / 'Saved/GameplayUI/BossReference/LocalizedNames'
QA_TABLE = '/Game/Gameplay/UI/_BossLocalizationQA/DT_QA_BossNames'
KEYS = ('UI.Boss.Zombie.Name', 'UI.Boss.NutTank.Name', 'UI.Boss.NutMage.Name')
PROFILES = (
    ('RU_Mixed', 'ru', (
        'Страж',
        'Сверхъестественный хранитель разрушенной эмалевой крепости',
        'Ореховый колдун — хранитель восьми ледяных врат и повелитель бесконечной зубной бури')),
    ('DE_Compound', 'de', (
        'Übermächtiger Haselnusswächter',
        'HaselnussfestungsbelagerungskommandantderunbezwingbarenNordgrenze',
        'Äußerst gefährlicher Großmeister — Wächter der zwölf verschlossenen Eistore')),
    ('CJK_Fallback', 'ja', (
        '古代胡桃城塞の守護者',
        '氷雪に閉ざされた永遠の城塞を守る大魔導師',
        '八つの氷雪の門と終わりなき歯の嵐を支配する古代の守護者')),
)
SIZES = ((1280, 720), (1920, 1080), (2560, 1080))
ALERT_MESSAGES = {
    'RU_Mixed': 'Берегись следующего удара! Держись рядом с опорой и помоги товарищу выбраться из опасной зоны.',
    'DE_Compound': 'Hüte dich vor dem nächsten schweren Schlag! Bleibe bei einer sicheren Stütze und hilf deinem Team, die gefährliche Zone rechtzeitig zu verlassen.',
    'CJK_Fallback': '次の強力な攻撃に注意し、安全な支柱の近くに留まり、仲間が危険な場所から離れるのを助けてください。',
}


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def prepare_manifest():
    require(ROOT.drive.upper() == 'E:', 'Task output must remain on E:')
    OUT.mkdir(parents=True, exist_ok=True)
    report = {'synthetic_copy_not_approved_translations': True, 'profiles': [],
              'sizes': [list(size) for size in SIZES], 'visual_acceptance': 'pending',
              'coverage_note': 'Cuprum covers RU/Latin; CJK depends on Slate fallback. Offline cmap coverage is not runtime proof.',
              'live_resize_and_cache_shrink': 'not exercised: each helper render creates a fresh widget'}
    try:
        from fontTools.ttLib import TTFont
        face = TTFont(ROOT / 'ArtSource/UI/BossReferenceMatch_20261017/Fonts/MCBossNames-Regular.ttf')
        primary = set(face.getBestCmap())
        fallback = set(TTFont('E:/UE/UE_5.8/Engine/Content/Slate/Fonts/DroidSansFallback.ttf').getBestCmap())
    except ImportError:
        primary = fallback = None  # UE Python does not require FontTools.
    for identity, culture, names in PROFILES:
        entry = {'profile': identity, 'preview_culture': culture, 'names': dict(zip(KEYS, names)),
                 'character_lengths': [len(name) for name in names], 'alert_owner': names[0],
                 'alert_message': ALERT_MESSAGES[identity]}
        if primary is not None:
            chars = {ord(char) for text in names for char in text.upper() if not char.isspace()}
            missing = chars-primary
            entry['missing_primary_font_chars'] = ''.join(chr(cp) for cp in sorted(missing))
            entry['missing_primary_and_droid_fallback_chars'] = ''.join(chr(cp) for cp in sorted(missing-fallback))
        report['profiles'].append(entry)
    (OUT / 'fixture_manifest.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    return report


def main():
    import unreal as u

    require(Path(u.Paths.project_dir()).resolve() == ROOT, 'Wrong Unreal project')
    require(not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor(), 'QA requires idle editor')
    manifest = prepare_manifest()
    lib = u.EditorAssetLibrary
    require(not lib.does_asset_exist(QA_TABLE), 'Preserving existing QA table: ' + QA_TABLE)
    theme = u.load_asset('/Game/Gameplay/UI/Theme/DA_GameplayUITheme')
    require(theme is not None, 'Master theme is missing')
    original = theme.get_editor_property('localization_table')
    require(isinstance(original, u.DataTable), 'Load the master localization table before this QA')
    baseline = json.loads(u.DataTableFunctionLibrary.export_data_table_to_json_string(original))
    baseline_keys = {row['Name'] for row in baseline}
    require(all(key in baseline_keys for key in KEYS), 'Expected boss name rows are missing')
    cls = u.load_class(None, '/Game/Gameplay/UI/WBP_GameplayHUD_v10.WBP_GameplayHUD_v10_C')
    require(cls is not None, 'Authored gameplay HUD is missing')
    require(u.get_default_object(cls).get_editor_property('preview_scenario') == u.MCGameplayUIPreviewScenario.THREE_BOSSES,
            'Alert fixture requires the authored HUD ThreeBosses preview default')
    factory = u.DataTableFactory()
    factory.set_editor_property('struct', u.load_object(None, '/Script/MessControl.MCUILocalizationRow'))
    table = None
    report = {'renders': [], 'table': QA_TABLE, 'saved_any_asset': False,
              'cjk_fallback_coverage': 'pending actual glyph/tofu inspection',
              'visual_acceptance': 'pending', 'live_resize_and_cache_shrink': manifest['live_resize_and_cache_shrink'],
              'cleanup_errors': []}
    failure = None
    try:
        table = u.AssetToolsHelpers.get_asset_tools().create_asset(
            QA_TABLE.rsplit('/', 1)[1], QA_TABLE.rsplit('/', 1)[0], u.DataTable, factory)
        require(isinstance(table, u.DataTable), 'Could not create the isolated unsaved QA table')
        theme.set_editor_property('localization_table', table)
        for identity, culture, names in PROFILES:
            rows = copy.deepcopy(baseline)
            replacements = dict(zip(KEYS, names))
            replacements['UI.Alert.BossWarningPreview'] = ALERT_MESSAGES[identity]
            for row in rows:
                if row['Name'] in replacements:
                    row['SourceText'] = replacements[row['Name']]
                    row['Localizations'] = {}
                    row['Context'] = 'QA synthetic stress copy; not approved translation'
            require(u.DataTableFunctionLibrary.fill_data_table_from_json_string(table, json.dumps(rows, ensure_ascii=False)),
                    'Could not populate QA profile: ' + identity)
            theme.notify_theme_changed()
            for width, height in SIZES:
                output = OUT / (identity + '_ThreeBosses_' + str(width) + 'x' + str(height) + '.png')
                ok = u.MCGameplayUIEditorLibrary.render_gameplay_ui_preview(
                    cls, str(output), u.IntPoint(width, height), 'ThreeBosses', culture, 1.)
                report['renders'].append({'profile': identity, 'culture': culture, 'names': list(names),
                                         'size': [width, height], 'output': str(output), 'ok': bool(ok)})
                require(ok, 'Localized boss-name render failed: ' + str(output))
            # Alert state preserves the authored ThreeBosses scenario while
            # making the HUD alert visible below the actual variable-height stack.
            output = OUT / (identity + '_Alert_1920x1080.png')
            ok = u.MCGameplayUIEditorLibrary.render_gameplay_ui_preview(
                cls, str(output), u.IntPoint(1920, 1080), 'Alert', culture, 1.)
            report['renders'].append({'profile': identity, 'culture': culture, 'names': list(names),
                                     'alert_owner': names[0], 'alert_message': ALERT_MESSAGES[identity],
                                     'state': 'Alert', 'size': [1920, 1080], 'output': str(output), 'ok': bool(ok)})
            require(ok, 'Localized alert-offset render failed: ' + str(output))
    except Exception as exc:
        failure = exc
        report['error'] = str(exc)
    finally:
        try:
            theme.set_editor_property('localization_table', original)
            theme.notify_theme_changed()
            report['theme_table_restored'] = theme.get_editor_property('localization_table') == original
            require(report['theme_table_restored'], 'Master localization-table reference was not restored')
        except Exception as exc:
            report['cleanup_errors'].append('Theme restore: ' + str(exc))
        if report.get('theme_table_restored') and lib.does_asset_exist(QA_TABLE):
            try:
                require(lib.delete_asset(QA_TABLE), 'Could not delete the exact newly-created QA table')
                report['qa_table_deleted'] = True
            except Exception as exc:
                report['cleanup_errors'].append(str(exc))
        report['status'] = 'passed' if failure is None and not report['cleanup_errors'] else 'failed'
        (OUT / 'render_report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    require(not report['cleanup_errors'], 'Cleanup failed; inspect the localized-name render report')
    if failure is not None:
        raise failure
    u.log('MC_BOSS_LOCALIZED_NAMES_PASS ' + json.dumps(report, ensure_ascii=False))
    return report


if __name__ == '__main__':
    if '--manifest' in sys.argv:
        print(json.dumps(prepare_manifest(), ensure_ascii=False, indent=2))
    else:
        main()
