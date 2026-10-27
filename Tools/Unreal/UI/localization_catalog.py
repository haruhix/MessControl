"""Lossless planning and spreadsheet merge for the shared UI localization table.

This module has no Unreal dependency. Unreal authoring lives in
``author_localization_catalog.py``. DataTable JSON FText export literals are
copied verbatim; unchanged spreadsheet cells keep their original text identity.
"""
from __future__ import annotations

import copy
import csv
import io
import json
import re


LANGUAGE_CULTURES = {
    'RU': 'ru', 'EN': 'en', 'DE': 'de', 'FR': 'fr', 'ES': 'es',
    'ZH_HANS': 'zh-Hans', 'ZH_HANT': 'zh-Hant', 'JA': 'ja', 'KO': 'ko', 'PT_BR': 'pt-BR',
}
LANGUAGE_COLUMNS = tuple(LANGUAGE_CULTURES)
LANGUAGE_FIELDS = {culture.casefold(): column for column, culture in LANGUAGE_CULTURES.items()}
EXTRA_PREFIX = 'Language:'
_QUOTED = re.compile(r'"(?:[^"\\]|\\.)*"')
_TEXT_LITERAL = re.compile(r'^(NSLOCTEXT|LOCTEXT|INVTEXT)\s*\((.*)\)$', re.DOTALL)
_ESCAPED_CHARACTER = re.compile(r'\\(.)', re.DOTALL)


class CatalogError(ValueError):
    """The requested operation would lose or ambiguously select existing copy."""


def _decode_ue_quoted_string(value):
    # FString::ReplaceCharWithEscapedChar adds \' as well as JSON-compatible
    # escapes. Consume escape pairs once so a literal \\ before an apostrophe
    # is preserved; a global replace would silently remove that backslash.
    normalized = _ESCAPED_CHARACTER.sub(
        lambda match: "'" if match.group(1) == "'" else match.group(0), value)
    return json.loads(normalized)


def text_source(value):
    """Read common UE FText literals for comparison/export, never for mutation.

    The original literal is retained in the rows and spreadsheet baseline.
    Unknown structured histories fail rather than being flattened incorrectly.
    """
    if value is None:
        return ''
    if not isinstance(value, str):
        raise CatalogError('FText must be a JSON string, got ' + type(value).__name__)
    match = _TEXT_LITERAL.fullmatch(value.strip())
    if match:
        arguments = _QUOTED.findall(match.group(2))
        expected = {'NSLOCTEXT': 3, 'LOCTEXT': 2, 'INVTEXT': 1}[match.group(1)]
        residue = _QUOTED.sub('', match.group(2)).replace(',', '').strip()
        if len(arguments) != expected or residue:
            raise CatalogError('Unsupported FText export literal: ' + value[:100])
        try:
            return _decode_ue_quoted_string(arguments[-1])
        except json.JSONDecodeError as exc:
            raise CatalogError('Unsupported FText literal escape: ' + value[:100]) from exc
    if re.match(r'^(?:NSLOCTEXT|LOCTEXT|INVTEXT|LOCTABLE|LOCGEN_[A-Z_]+)\s*\(', value.strip()):
        raise CatalogError('Unsupported structured FText: edit this cell in Unreal: ' + value[:100])
    return value


def has_copy(value):
    return bool(text_source(value).strip())


def canonical_culture(value):
    culture = str(value).strip().replace('_', '-')
    if not re.fullmatch(r'[A-Za-z]{2,8}(?:-[A-Za-z0-9]{1,8})*', culture):
        raise CatalogError('Invalid culture key: ' + repr(value))
    return culture.casefold()


def index_rows(rows, label):
    """FName row names are case insensitive. Do not silently collapse duplicates."""
    indexed = {}
    for row in rows:
        if not isinstance(row, dict):
            raise CatalogError(label + ': row is not an object')
        name = row.get('Name')
        if not isinstance(name, str) or not name.strip() or name.casefold() == 'none':
            raise CatalogError(label + ': invalid Name: ' + repr(name))
        if name.casefold() in indexed:
            raise CatalogError(label + ': duplicate FName row: ' + name)
        indexed[name.casefold()] = row
    return indexed


def indexed_languages(mapping, label):
    if not isinstance(mapping, dict):
        raise CatalogError(label + ': language variants must be an object')
    indexed = {}
    for culture, value in mapping.items():
        canonical = canonical_culture(culture)
        if canonical in indexed:
            raise CatalogError(label + ': duplicate canonical culture: ' + culture)
        indexed[canonical] = (culture, value)
    return indexed


def _put_if_empty(row, field, value):
    if not has_copy(row.get(field, '')) and has_copy(value):
        row[field] = value


def migrate_localization_row(row):
    """Fill new columns without replacing any authored column or legacy field."""
    result = copy.deepcopy(row)
    legacy = indexed_languages(result.get('Localizations', {}), row['Name'] + '.Localizations')
    additional = result.setdefault('AdditionalLanguages', {})
    extra_index = indexed_languages(additional, row['Name'] + '.AdditionalLanguages')
    for column in LANGUAGE_COLUMNS:
        result.setdefault(column, '')
        canonical = LANGUAGE_CULTURES[column].casefold()
        # New explicit variants already outrank the legacy map at runtime.
        candidate = extra_index.get(canonical, ('', ''))[1]
        if not has_copy(candidate):
            candidate = legacy.get(canonical, ('', ''))[1]
        _put_if_empty(result, column, candidate)
    _put_if_empty(result, 'RU', result.get('SourceText', ''))
    for canonical, (culture, value) in legacy.items():
        if canonical in LANGUAGE_FIELDS:
            continue
        existing = extra_index.get(canonical)
        if not existing:
            additional[culture] = value
        elif not has_copy(existing[1]) and has_copy(value):
            additional[existing[0]] = value
    result.setdefault('Context', '')
    return result


def _presentation_text(row, field, key, context):
    legacy = indexed_languages(row.get('Localizations', {}), row['Name'] + '.Localizations')
    translations = {}
    for culture, value in legacy.values():
        if not isinstance(value, dict):
            raise CatalogError(row['Name'] + ': presentation language must contain DisplayName/Description')
        if has_copy(value.get(field, '')):
            translations[culture] = value[field]
    return migrate_localization_row(dict(Name=key, SourceText=row.get(field, ''),
                                        Localizations=translations, Context=context))


def _merge_generated(target, incoming):
    """Existing keyed copy wins only when compatible; conflicts require review."""
    for field in LANGUAGE_COLUMNS:
        left, right = target.get(field, ''), incoming.get(field, '')
        if has_copy(left) and has_copy(right) and text_source(left) != text_source(right):
            raise CatalogError(incoming['Name'] + ': existing catalog conflicts with presentation ' + field)
        _put_if_empty(target, field, right)
    extra = target.setdefault('AdditionalLanguages', {})
    indexed = indexed_languages(extra, target['Name'] + '.AdditionalLanguages')
    for canonical, (culture, value) in indexed_languages(incoming.get('AdditionalLanguages', {}), incoming['Name']).items():
        old = indexed.get(canonical)
        if old and has_copy(old[1]) and has_copy(value) and text_source(old[1]) != text_source(value):
            raise CatalogError(incoming['Name'] + ': existing catalog conflicts with presentation ' + culture)
        if not old:
            extra[culture] = value
        elif not has_copy(old[1]):
            extra[old[0]] = value
    if not target.get('Context', '').strip():
        target['Context'] = incoming['Context']


def plan_migration(localization_rows, card_rows, booster_rows, *,
                   card_path='/Game/Gameplay/UI/Data/DT_UI_Cards',
                   booster_path='/Game/Gameplay/UI/Data/DT_UI_Boosters',
                   additional_localization_rows=()):
    """Return new rows and a report; inputs, images and gameplay IDs stay untouched."""
    index_rows(localization_rows, 'localization')
    texts = [migrate_localization_row(row) for row in localization_rows]
    indexed = index_rows(texts, 'localization')
    presentation = []
    added = []
    preserved_additional = []
    index_rows(additional_localization_rows, 'additional localization rows')
    for row in additional_localization_rows:
        if row['Name'].casefold() in indexed:
            preserved_additional.append(row['Name'])
            continue
        migrated = migrate_localization_row(row)
        texts.append(migrated)
        indexed[row['Name'].casefold()] = migrated
        added.append(row['Name'])
    for kind, rows, path in (('Card', card_rows, card_path), ('Booster', booster_rows, booster_path)):
        index_rows(rows, kind)
        result = copy.deepcopy(rows)
        for row in result:
            for field, suffix in (('DisplayName', 'Title'), ('Description', 'Description')):
                key_field = field + 'Key'
                key = row.get(key_field)
                explicit_key = key not in (None, '', 'None')
                if key in (None, '', 'None'):
                    key = 'UI.' + kind + '.' + row['Name'] + '.' + suffix
                    row[key_field] = key
                if not isinstance(key, str) or not re.fullmatch(r'[A-Za-z0-9_.-]+', key):
                    raise CatalogError(row['Name'] + ': unsupported stable key: ' + repr(key))
                incoming = _presentation_text(row, field, key, path + ':' + row['Name'] + '.' + field)
                target = indexed.get(key.casefold())
                if target is None:
                    texts.append(incoming)
                    indexed[key.casefold()] = incoming
                    added.append(key)
                elif not explicit_key:
                    _merge_generated(target, incoming)
        presentation.append(result)
    return texts, presentation[0], presentation[1], dict(
        localization_rows_before=len(localization_rows), localization_rows_after=len(texts),
        added_keys=added, card_rows=len(card_rows), booster_rows=len(booster_rows),
        existing_additional_keys_preserved=preserved_additional,
        source_aliases_preserved=True, presentation_fallbacks_preserved=True,
        gameplay_ids_and_images_preserved=True)


def _csv_languages(rows):
    languages = {}
    for row in rows:
        for canonical, (culture, _) in indexed_languages(row.get('AdditionalLanguages', {}), row['Name']).items():
            if canonical in LANGUAGE_FIELDS:
                continue
            languages.setdefault(canonical, culture)
    return [languages[key] for key in sorted(languages)]


def export_spreadsheet(rows):
    """Readable CSV plus lossless baseline for stale-edit detection on merge."""
    index_rows(rows, 'localization')
    cultures = _csv_languages(rows)
    fields = ['Name', *LANGUAGE_COLUMNS, 'Context', *(EXTRA_PREFIX + value for value in cultures)]
    output = io.StringIO(newline='')
    writer = csv.DictWriter(output, fieldnames=fields, lineterminator='\n')
    writer.writeheader()
    for row in rows:
        result = {field: text_source(row.get(field, '')) for field in LANGUAGE_COLUMNS}
        result.update(Name=row['Name'], Context=row.get('Context', ''))
        extra = indexed_languages(row.get('AdditionalLanguages', {}), row['Name'])
        for culture in cultures:
            result[EXTRA_PREFIX + culture] = text_source(extra.get(canonical_culture(culture), ('', ''))[1])
        writer.writerow(result)
    return output.getvalue(), dict(format='MessControlLocalizationSpreadsheet', version=1,
                                   fields=fields, rows=copy.deepcopy(rows))


def _cell(row, field):
    if field.startswith(EXTRA_PREFIX):
        culture = canonical_culture(field[len(EXTRA_PREFIX):])
        return indexed_languages(row.get('AdditionalLanguages', {}), row.get('Name', '?')).get(culture, ('', ''))[1]
    return row.get(field, '')


def _csv_target_field(field):
    # Older CSVs may have exported pt-BR/zh-Hans as an additional language
    # before it gained a dedicated column. Their edits must reach the active
    # new column rather than a now-shadowed map entry.
    if field.startswith(EXTRA_PREFIX):
        return LANGUAGE_FIELDS.get(canonical_culture(field[len(EXTRA_PREFIX):]), field)
    return field


def merge_spreadsheet(current_rows, csv_text, baseline):
    """Three-way cell merge; missing rows/cells are preserved, not deleted.

    A changed CSV cell may overwrite its unchanged baseline value. Concurrent
    Unreal edits are preserved or reported as conflicts, never silently lost.
    """
    if baseline.get('format') != 'MessControlLocalizationSpreadsheet' or baseline.get('version') != 1:
        raise CatalogError('Missing/unsupported spreadsheet baseline')
    result = copy.deepcopy(current_rows)
    indexed = index_rows(result, 'current localization')
    old = index_rows(baseline['rows'], 'spreadsheet baseline')
    reader = csv.DictReader(io.StringIO(csv_text.lstrip('\ufeff'), newline=''))
    fields = reader.fieldnames or []
    if 'Name' not in fields or len(fields) != len(set(fields)):
        raise CatalogError('CSV must have one Name column and no duplicate headers')
    for field in fields:
        if field not in ('Name', 'Context', *LANGUAGE_COLUMNS) and not field.startswith(EXTRA_PREFIX):
            raise CatalogError('Unsupported CSV column: ' + str(field))
        if field.startswith(EXTRA_PREFIX):
            canonical_culture(field[len(EXTRA_PREFIX):])
    if len([_csv_target_field(field) for field in fields]) != len(set(_csv_target_field(field) for field in fields)):
        raise CatalogError('Duplicate logical language columns')
    if len([canonical_culture(x[len(EXTRA_PREFIX):]) for x in fields if x.startswith(EXTRA_PREFIX)]) != len(set(
            canonical_culture(x[len(EXTRA_PREFIX):]) for x in fields if x.startswith(EXTRA_PREFIX))):
        raise CatalogError('Duplicate canonical language columns')
    changes = []
    seen = set()
    for incoming in reader:
        if None in incoming:
            raise CatalogError('CSV row has too many cells')
        name = incoming.get('Name', '')
        if not name.strip() or name.casefold() == 'none' or name.casefold() in seen:
            raise CatalogError('Empty/duplicate CSV row: ' + repr(name))
        seen.add(name.casefold())
        previous = old.get(name.casefold())
        target = indexed.get(name.casefold())
        created = target is None
        if target is None:
            if previous:
                raise CatalogError(name + ': row was removed in Unreal after CSV export')
            if not re.fullmatch(r'[A-Za-z0-9_.-]+', name):
                raise CatalogError('New localization keys must be stable ASCII identifiers: ' + name)
            target = dict(Name=name, **{column: '' for column in LANGUAGE_COLUMNS}, AdditionalLanguages={},
                          SourceText='', Localizations={}, Context='')
            result.append(target)
            indexed[name.casefold()] = target
        for field in fields:
            if field == 'Name' or incoming[field] is None:
                continue
            value = incoming[field]
            before = _cell(previous or {}, field)
            target_field = _csv_target_field(field)
            now = _cell(target, target_field)
            if field != target_field and not has_copy(now):
                now = _cell(target, field)
            if value == text_source(before):
                continue
            if text_source(now) not in (text_source(before), value):
                raise CatalogError(name + ': concurrent Unreal/CSV edit conflicts in ' + field)
            if value == text_source(now):
                continue
            if target_field.startswith(EXTRA_PREFIX):
                culture = target_field[len(EXTRA_PREFIX):]
                mapping = target.setdefault('AdditionalLanguages', {})
                key = indexed_languages(mapping, name).get(canonical_culture(culture), (culture, ''))[0]
                mapping[key] = value
            else:
                target[target_field] = value
            changes.append(dict(row=name, field=target_field))
        if created:
            # Freeze the first native wording as a compatibility alias. Later RU
            # edits never change SourceText and cannot break old literal bindings.
            target['SourceText'] = target.get('RU', '')
    return result, dict(changes=changes, rows_before=len(current_rows), rows_after=len(result),
                        missing_csv_rows_preserved=True, source_aliases_preserved=True)
