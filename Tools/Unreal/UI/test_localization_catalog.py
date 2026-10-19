"""Focused offline preservation/merge checks; no Unreal, builds or asset writes."""
import copy
import csv
import io
import json
from pathlib import Path
import unittest
from types import SimpleNamespace

from localization_catalog import (CatalogError, LANGUAGE_COLUMNS, export_spreadsheet, merge_spreadsheet,
                                  migrate_localization_row, plan_migration, text_source)
from author_localization_catalog import native_validate


class CatalogPreservationTests(unittest.TestCase):
    def fixture(self):
        source = 'NSLOCTEXT("Original.Namespace", "Original.Key", "Старое название")'
        texts = [dict(Name='UI.Existing', SourceText=source,
                      Localizations={'ru': 'Исправленное', 'en': 'Edited English', 'en_GB': 'British'},
                      Context='Do not replace', UnrecognizedAuthoredField=17)]
        cards = [dict(Name='ArtistRow', GameplayPerkID='P01', Image='/Game/Custom/Icon.Icon',
                      DisplayName='Имя', Description='Описание {Count}',
                      Localizations={'fr': {'DisplayName': 'Nom', 'Description': 'Description {Count}'},
                                     'pt-BR': {'DisplayName': 'Nome', 'Description': ''}})]
        return texts, cards, []

    def test_migration_preserves_native_literals_images_ids_and_exact_variants(self):
        original = self.fixture()
        before = copy.deepcopy(original)
        texts, cards, _, report = plan_migration(*original)
        self.assertEqual(original, before)
        self.assertEqual(texts[0]['SourceText'], original[0][0]['SourceText'])
        self.assertEqual(texts[0]['RU'], 'Исправленное')
        self.assertEqual(texts[0]['EN'], 'Edited English')
        self.assertEqual(texts[0]['AdditionalLanguages'], {'en_GB': 'British'})
        self.assertEqual(texts[0]['UnrecognizedAuthoredField'], 17)
        self.assertEqual(cards[0]['Image'], '/Game/Custom/Icon.Icon')
        self.assertEqual(cards[0]['GameplayPerkID'], 'P01')
        self.assertEqual(cards[0]['DisplayNameKey'], 'UI.Card.ArtistRow.Title')
        self.assertEqual(texts[1]['FR'], 'Nom')
        self.assertEqual(texts[1]['PT_BR'], 'Nome')
        self.assertEqual(texts[1]['AdditionalLanguages'], {})
        self.assertEqual(len(report['added_keys']), 2)

    def test_repeat_is_idempotent_and_central_edits_do_not_revert_to_presentation(self):
        first = plan_migration(*self.fixture())[:3]
        repeated = plan_migration(*first)
        self.assertEqual(repeated[:3], first)
        self.assertEqual(repeated[3]['added_keys'], [])
        first[0][1]['RU'] = 'Новая редакция после миграции'
        first[0][1]['FR'] = 'Nouvelle version'
        self.assertEqual(plan_migration(*first)[:3], first)

    def test_existing_unbound_key_conflict_and_duplicate_fname_are_rejected(self):
        texts, cards, boosters = self.fixture()
        texts.append(dict(Name='UI.Card.ArtistRow.Title', SourceText='Чужой смысл', Localizations={}))
        with self.assertRaisesRegex(CatalogError, 'conflicts'):
            plan_migration(texts, cards, boosters)
        with self.assertRaisesRegex(CatalogError, 'duplicate FName'):
            plan_migration([texts[0], dict(texts[0], Name='ui.existing')], [], [])

    def test_canonical_duplicates_rejected_and_new_variants_keep_priority(self):
        row = dict(Name='UI.Test', SourceText='База', Localizations={'en-GB': 'A', 'en_GB': 'B'})
        with self.assertRaisesRegex(CatalogError, 'duplicate canonical'):
            migrate_localization_row(row)
        row['Localizations'] = {'en-GB': 'Old British', 'en': 'Old English'}
        row['AdditionalLanguages'] = {'en-GB': 'New British', 'en': 'New English'}
        migrated = migrate_localization_row(row)
        self.assertEqual(migrated['EN'], 'New English')
        self.assertEqual(migrated['AdditionalLanguages']['en-GB'], 'New British')

    def test_asian_and_brazilian_columns_use_exact_cultures_without_flattening_regions(self):
        row = dict(Name='UI.Global', SourceText='Исходник', Localizations={
            'zh-Hans': '简体中文', 'zh_Hant': '繁體中文', 'ja': '日本語', 'ko': '한국어',
            'pt_BR': 'Português brasileiro', 'zh-CN': 'Regional Chinese',
            'ja-JP': 'Regional Japanese', 'es-419': 'Spanish Latin America'})
        source = copy.deepcopy(row)
        migrated = migrate_localization_row(row)
        self.assertEqual(row, source)
        self.assertEqual(migrated['ZH_HANS'], '简体中文')
        self.assertEqual(migrated['ZH_HANT'], '繁體中文')
        self.assertEqual(migrated['JA'], '日本語')
        self.assertEqual(migrated['KO'], '한국어')
        self.assertEqual(migrated['PT_BR'], 'Português brasileiro')
        self.assertEqual(migrated['AdditionalLanguages'], {'zh-CN': 'Regional Chinese',
                         'ja-JP': 'Regional Japanese', 'es-419': 'Spanish Latin America'})
        self.assertEqual(migrated['Localizations'], source['Localizations'])
        blank = migrate_localization_row(dict(Name='UI.Empty', SourceText='Исходник', Localizations={}))
        self.assertTrue(all(blank[column] == '' for column in LANGUAGE_COLUMNS if column != 'RU'))

    def test_asian_csv_roundtrip_and_older_additional_language_header_reaches_new_column(self):
        row = migrate_localization_row(dict(Name='UI.Global', SourceText='Исходник', Localizations={
            'zh-Hans': '简体中文', 'zh-Hant': '繁體中文', 'ja': '日本語', 'ko': '한국어', 'pt-BR': 'Nome'}))
        text, baseline = export_spreadsheet([row])
        self.assertEqual(merge_spreadsheet([row], text, baseline)[0], [row])
        changed = text.replace('日本語', '日本語の編集')
        self.assertEqual(merge_spreadsheet([row], changed, baseline)[0][0]['JA'], '日本語の編集')
        old = dict(Name='UI.Global', SourceText='Исходник', RU='Исходник', AdditionalLanguages={'pt-BR': 'Nome'})
        old_baseline = dict(format='MessControlLocalizationSpreadsheet', version=1,
                            fields=['Name', 'Language:pt-BR'], rows=[old])
        merged, report = merge_spreadsheet([row], 'Name,Language:pt-BR\nUI.Global,Nome novo\n', old_baseline)
        self.assertEqual(merged[0]['PT_BR'], 'Nome novo')
        self.assertEqual(report['changes'], [dict(row='UI.Global', field='PT_BR')])

    def test_explicit_new_rows_never_overwrite_existing_edited_rows(self):
        original = dict(Name='UI.MainMenu.Existing', SourceText='Old alias', RU='User edited',
                        EN='Approved translation', Context='Artist context', Localizations={})
        additions = [dict(Name='UI.MainMenu.Existing', SourceText='Starter', Localizations={}),
                     dict(Name='UI.MainMenu.New', SourceText='New visible label', Localizations={})]
        migrated = plan_migration([original], [], [], additional_localization_rows=additions)
        self.assertEqual(migrated[0][0]['RU'], 'User edited')
        self.assertEqual(migrated[0][0]['EN'], 'Approved translation')
        self.assertEqual(migrated[0][0]['Context'], 'Artist context')
        self.assertEqual(migrated[0][0]['SourceText'], 'Old alias')
        self.assertEqual(migrated[3]['added_keys'], ['UI.MainMenu.New'])
        self.assertEqual(migrated[3]['existing_additional_keys_preserved'], ['UI.MainMenu.Existing'])
        self.assertEqual(plan_migration(*migrated[:3], additional_localization_rows=additions)[:3], migrated[:3])

    def test_spreadsheet_roundtrip_retains_identity_and_partial_csv_retains_rows(self):
        texts = plan_migration(*self.fixture())[0]
        csv_text, baseline = export_spreadsheet(texts)
        merged, report = merge_spreadsheet(texts, csv_text, baseline)
        self.assertEqual(merged, texts)
        self.assertFalse(report['changes'])
        table = list(csv.DictReader(io.StringIO(csv_text)))
        table[0]['RU'] = 'Новый русский текст'
        table[0]['Language:en_GB'] = 'New British text'
        output = io.StringIO()
        writer = csv.DictWriter(output, fieldnames=list(table[0]), lineterminator='\n')
        writer.writeheader()
        writer.writerow(table[0])
        merged, report = merge_spreadsheet(texts, output.getvalue(), baseline)
        self.assertEqual(len(merged), len(texts))
        self.assertEqual(merged[0]['RU'], 'Новый русский текст')
        self.assertEqual(merged[0]['SourceText'], texts[0]['SourceText'])
        self.assertEqual(merged[0]['AdditionalLanguages']['en_GB'], 'New British text')
        self.assertEqual(len(report['changes']), 2)

    def test_concurrent_unreal_edit_conflicts_only_in_changed_cell(self):
        texts = plan_migration(*self.fixture())[0]
        csv_text, baseline = export_spreadsheet(texts)
        current = copy.deepcopy(texts)
        current[0]['EN'] = 'Concurrent Unreal edit'
        same_csv, _ = merge_spreadsheet(current, csv_text, baseline)
        self.assertEqual(same_csv, current)
        changed_csv = csv_text.replace('Edited English', 'Different spreadsheet edit')
        with self.assertRaisesRegex(CatalogError, 'concurrent Unreal/CSV'):
            merge_spreadsheet(current, changed_csv, baseline)

    def test_concurrently_added_row_does_not_replace_its_frozen_alias(self):
        current = [dict(Name='UI.New', RU='Текущая редакция', EN='', DE='', FR='', ES='',
                        Context='', AdditionalLanguages={}, Localizations={}, SourceText='Первоначальная редакция')]
        baseline = dict(format='MessControlLocalizationSpreadsheet', version=1, fields=['Name', 'RU'], rows=[])
        merged, _ = merge_spreadsheet(current, 'Name,RU\nUI.New,Текущая редакция\n', baseline)
        self.assertEqual(merged, current)

    def test_common_ue_ftext_literals_are_decoded_only_for_display(self):
        self.assertEqual(text_source('NSLOCTEXT("Ns", "Key", "Line\\n\\\"Quote\\\"")'), 'Line\n"Quote"')
        self.assertEqual(text_source('INVTEXT("Text")'), 'Text')
        with self.assertRaisesRegex(CatalogError, 'Unsupported structured'):
            text_source('LOCTABLE("Table", "Key")')

    def test_actual_project_seed_migration_has_expected_card_and_booster_coverage(self):
        here = Path(__file__).resolve().parent
        source = [json.loads((here / name).read_text(encoding='utf-8-sig')) for name in
                  ('gameplay_ui_texts.json', 'gameplay_ui_cards.json', 'gameplay_ui_boosters.json')]
        result = plan_migration(*source)
        self.assertEqual(result[3]['localization_rows_after'], len(source[0]) + len(result[3]['added_keys']))
        catalog = {row['Name'] for row in result[0]}
        for kind, rows in (('Card', result[1]), ('Booster', result[2])):
            for row in rows:
                self.assertIn(row['DisplayNameKey'], catalog)
                self.assertIn(row['DescriptionKey'], catalog)
        self.assertEqual(plan_migration(*result[:3])[:3], result[:3])
        for old, new in zip(source[1] + source[2], result[1] + result[2]):
            self.assertEqual({k: new[k] for k in old}, old)

    def test_unreal_bool_out_binding_shape_does_not_treat_empty_success_array_as_failure(self):
        selected = object()
        for value, expected in (([], True), (None, False), ((True, []), True),
                                ((False, ['Bad parameters']), False)):
            calls = []
            def validate(table, *, localization_table):
                calls.append(localization_table)
                return value
            u = SimpleNamespace(MCUIDataLibrary=SimpleNamespace(validate_content_table=validate))
            self.assertEqual(native_validate(u, object(), selected)[0], expected)
            self.assertEqual(calls, [selected])


if __name__ == '__main__':
    unittest.main()
