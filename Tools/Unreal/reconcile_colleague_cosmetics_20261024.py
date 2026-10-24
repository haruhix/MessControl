"""Reconcile the accepted colleague feedback after the two Git commits are merged.

Run in the idle MessControl Editor, never while PIE is running. This saves only
DT_HubCatalog and DT_UI_Localization. It does not grant/revoke ownership, touch
profiles, edit DT_CharacterCosmetics, change old product prices, or save a map.

Default input: E:/CodexWork/ColleagueCosmeticsReconcile_20261024/Input.json
{
  "localization_backup_json": "E:/.../LocalLocalization.json",
  "localization_conflict_choices": {"Existing.Row": "local"},
  "dry_run": false
}
The backup must contain exported rows as a list or {"rows": [...]}. Unequal
same-name rows need an explicit local/remote choice; they are reported before
any mutation. An optional localization_default_conflict_choice applies an
explicit common choice. Remote-only rows and local-only rows are both kept.
"""
from __future__ import annotations

import copy
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import shutil


DEFAULT_INPUT = 'E:/CodexWork/ColleagueCosmeticsReconcile_20261024/Input.json'
CATALOG = '/Game/Gameplay/Hub/Economy/DT_HubCatalog'
LOCALIZATION = '/Game/Gameplay/UI/Data/DT_UI_Localization'
COSMETICS = '/Game/Data/DT_CharacterCosmetics'
PAID_DESCRIPTION = 'UI.HeadCosmetics.PaidDescription'
# New test prices accepted in this task, not a claim about saved remote prices.
NEW_PRICES = {
    'Hat_Head_CowboyHat': 80,
    'Face_Head_SquareGlasses': 60,
    'Hat_Head_Feather': 40,
    'Face_Head_Brows': 45,
    'Hat_Head_ConeHat': 55,
    'Hat_Head_ArtistCap': 0,
    'Hat_Head_FootballHelmet': 90,
}


def require(value, message):
    if not value:
        raise RuntimeError(message)


def e_path(value):
    path = Path(value).resolve()
    require(path.drive.upper() == 'E:', 'Task inputs/outputs must stay on E: '+str(path))
    return path


def write_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')


def row_index(rows, context):
    require(isinstance(rows, list), context+': expected a row list')
    result = {}
    for row in rows:
        require(isinstance(row, dict) and isinstance(row.get('Name'), str), context+': missing row Name')
        key = row['Name'].casefold()
        require(key and key not in result, context+': duplicate row '+row['Name'])
        result[key] = row
    return result


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def nsloctext_parts(value):
    """Decode only the exact NSLOCTEXT history needed for owned copy changes."""
    require(isinstance(value, str), 'Expected an exported FText string')
    literal = re.fullmatch(r'NSLOCTEXT\s*\((.*)\)', value.strip(), re.DOTALL)
    require(literal is not None, 'Expected an existing NSLOCTEXT identity: '+value[:120])
    quoted = re.compile(r'"(?:[^"\\]|\\.)*"')
    arguments = quoted.findall(literal.group(1))
    residue = quoted.sub('', literal.group(1)).replace(',', '').strip()
    require(len(arguments) == 3 and not residue, 'Unsupported NSLOCTEXT history: '+value[:120])
    return tuple(json.loads(argument) for argument in arguments)


def nsloctext(namespace, key, source):
    return 'NSLOCTEXT('+', '.join(json.dumps(value, ensure_ascii=False) for value in (namespace,key,source))+')'


def changed_ftext_source(original, source, expected_key):
    namespace,key,_ = nsloctext_parts(original)
    require(key == expected_key, 'Unexpected FText key; refusing to replace its identity: '+key)
    return nsloctext(namespace,key,source)


def paid_description_row(template):
    """New row with explicit FText histories; unrelated rows stay byte-for-byte JSON equal."""
    languages = ('RU','EN','DE','FR','ES','PT_BR','ZH_HANS','ZH_HANT','JA','KO')
    expected_fields = {'Name',*languages,'AdditionalLanguages','Context','SourceText','Localizations'}
    require(set(template) == expected_fields, 'Unexpected localization schema; inspect before creating a row')
    namespaces = [nsloctext_parts(template[field])[0] for field in ('RU','EN','SourceText')]
    require(len(set(namespaces)) == 1, 'Existing shared-description FText namespaces disagree')
    result = copy.deepcopy(template)
    result.update(Name=PAID_DESCRIPTION,AdditionalLanguages={},Localizations={},
                  Context='Paid head cosmetics; accepted colleague feedback 2026-10-24')
    for field in languages:
        result[field] = ''
    for field,source in (('RU','Косметический аксессуар для шкафчика.'),
                         ('EN','A cosmetic wardrobe accessory.'),
                         ('SourceText','Косметический аксессуар для шкафчика.')):
        result[field] = nsloctext(namespaces[0],PAID_DESCRIPTION+'_'+field,source)
    return result


def reconcile(config_path=DEFAULT_INPUT):
    import unreal as u
    config_file = e_path(config_path)
    config = json.loads(config_file.read_text(encoding='utf-8-sig'))
    root = e_path(u.Paths.project_dir())
    require((root/'MessControl.uproject').is_file(), 'Wrong Editor project')
    require(not u.EditorLevelLibrary.get_pie_worlds(True), 'PIE must be stopped before table reconciliation')
    stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S%fZ')
    out = e_path(config.get('output_directory', 'E:/CodexWork/ColleagueCosmeticsReconcile_20261024'))/stamp
    out.mkdir(parents=True, exist_ok=False)
    report_file = out/'Report.json'
    dirty_before = sorted(p.get_path_name() for p in u.EditorLoadingAndSavingUtils.get_dirty_content_packages())
    require(not set(dirty_before).intersection((CATALOG, LOCALIZATION)),
            'Reconciliation targets are dirty; preserve those edits first')
    map_dirty_before = sorted(p.get_path_name() for p in u.EditorLoadingAndSavingUtils.get_dirty_map_packages())
    tables = {path:u.EditorAssetLibrary.load_asset(path) for path in (CATALOG, LOCALIZATION)}
    require(all(isinstance(table, u.DataTable) for table in tables.values()), 'Required DataTables are missing')
    before = {path:json.loads(u.DataTableFunctionLibrary.export_data_table_to_json_string(table))
              for path,table in tables.items()}
    for path,rows in before.items():
        row_index(rows, path)
        write_json(out/('Before_'+path.rsplit('/', 1)[1]+'.json'), rows)
    protected_cosmetics = root/'Content/Data/DT_CharacterCosmetics.uasset'
    protected_hash = digest(protected_cosmetics)
    for path in tables:
        source = root/'Content'/(path.removeprefix('/Game/')+'.uasset')
        destination = out/'BeforePackages'/source.relative_to(root)
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, destination)
    export = json.loads((root/'ArtSource/CharacterCurrent/HeadCosmetics20261024/ExportReport.json').read_text(encoding='utf-8'))
    require({item['id'] for item in export['items']} == set(NEW_PRICES), 'Unexpected imported item set')
    proposed_catalog = copy.deepcopy(before[CATALOG])
    catalog = row_index(proposed_catalog, CATALOG)
    price_changes = []
    for item_id,price in NEW_PRICES.items():
        row = catalog.get(item_id.casefold())
        require(row is not None and row.get('Kind') == 'Cosmetic' and row.get('CosmeticID') == item_id,
                'Missing/unexpected cosmetic product '+item_id)
        price_changes.append({'id':item_id, 'before':row.get('Price'), 'after':price})
        row['Price'] = price
        if price:
            row['Description'] = changed_ftext_source(row['Description'],
                'Косметический аксессуар для шкафчика.',item_id+'_Description')
            row['DescriptionKey'] = PAID_DESCRIPTION
    backup_file = e_path(config['localization_backup_json'])
    local = json.loads(backup_file.read_text(encoding='utf-8-sig'))
    if isinstance(local, dict):
        local = local.get('rows')
    local_index = row_index(local, 'Local localization backup')
    proposed_localization = copy.deepcopy(before[LOCALIZATION])
    remote_index = row_index(proposed_localization, LOCALIZATION)
    choices = {key.casefold():value for key,value in config.get('localization_conflict_choices', {}).items()}
    default_choice = config.get('localization_default_conflict_choice')
    require(default_choice in (None, 'local', 'remote'), 'Invalid default localization conflict choice')
    conflicts, additions = [], []
    for key,row in local_index.items():
        current = remote_index.get(key)
        if current is None:
            value = copy.deepcopy(row);proposed_localization.append(value);remote_index[key] = value
            additions.append(row['Name'])
        elif current != row:
            choice = choices.get(key, default_choice)
            require(choice in (None, 'local', 'remote'), 'Invalid conflict choice for '+row['Name'])
            conflicts.append({'name':row['Name'], 'choice':choice,
                              'different_fields':sorted(field for field in set(current)|set(row) if current.get(field) != row.get(field)),
                              'local':row, 'remote':copy.deepcopy(current)})
            if choice == 'local':
                current.clear();current.update(copy.deepcopy(row))
    # Paid products must not retain the remote shared description saying "free".
    template = remote_index.get('ui.headcosmetics.description')
    require(template is not None, 'Missing existing shared head-cosmetic description')
    paid_copy = paid_description_row(template)
    previous_paid = remote_index.get(PAID_DESCRIPTION.casefold())
    if previous_paid is None:
        proposed_localization.append(paid_copy)
    else:
        require(previous_paid.get('RU') == paid_copy['RU'] and previous_paid.get('EN') == paid_copy['EN'],
                'The task-owned paid-description key has conflicting authored copy')
    row_index(proposed_localization, 'Proposed localization')
    report = {'status':'planned', 'captured_utc':datetime.now(timezone.utc).isoformat(),
              'feedback':'User accepted colleague feedback: six new accessories paid; artist cap remains free.',
              'price_changes':price_changes, 'old_product_prices':'preserved',
              'ownership':'unchanged; no profile reads/writes and no default-unlock changes',
              'localization_backup':str(backup_file), 'localization_conflicts':conflicts,
              'local_only_rows_added':additions, 'paid_description_key':PAID_DESCRIPTION,
              'ftext_validation':'Exact round-trip equality; changed descriptions retain their original namespace/key, and the one new localization row has explicit histories in the existing table namespace.',
              'saved_packages':[], 'unrelated_dirty_content':dirty_before,
              'unrelated_dirty_maps':map_dirty_before,
              'protected_cosmetics_sha256':protected_hash,
              'validation':'Offline authoring plan only until saved and checked by root in solo PIE.'}
    write_json(out/'Proposed_DT_HubCatalog.json', proposed_catalog)
    write_json(out/'Proposed_DT_UI_Localization.json', proposed_localization)
    unresolved = [item['name'] for item in conflicts if item['choice'] is None]
    if unresolved or config.get('dry_run', False):
        report['status'] = 'localization_choices_required' if unresolved else 'dry_run'
        report['unresolved_rows'] = unresolved
        write_json(report_file, report)
        print('MC_COSMETICS_RECONCILE_PLAN '+str(report_file))
        return report
    proposed = {CATALOG:proposed_catalog, LOCALIZATION:proposed_localization}
    try:
        # Validate both in memory before saving either package.
        for path,rows in proposed.items():
            require(u.DataTableFunctionLibrary.fill_data_table_from_json_string(tables[path], json.dumps(rows, ensure_ascii=False)),
                    'Cannot fill table '+path)
            actual = json.loads(u.DataTableFunctionLibrary.export_data_table_to_json_string(tables[path]))
            require(row_index(actual, path) == row_index(rows, path), 'Round-trip mismatch in '+path)
        original_catalog = row_index(before[CATALOG], CATALOG)
        require(all(row == catalog[key] for key,row in original_catalog.items() if row['Name'] not in NEW_PRICES),
                'An unrelated catalog row changed')
        require(digest(protected_cosmetics) == protected_hash, 'Protected cosmetic fitting/defaults package changed')
        for path in tables:
            if before[path] != proposed[path]:
                require(u.EditorAssetLibrary.save_loaded_asset(tables[path], only_if_is_dirty=False), 'Cannot save '+path)
                report['saved_packages'].append(path)
        report['status'] = 'saved_pending_solo'
    except Exception as error:
        report['status'] = 'failed_check_before_using_result'
        report['error'] = str(error)
        report['recovery'] = 'BeforePackages and Before JSON retained. Do not save all or overwrite unrelated edits.'
        write_json(report_file, report)
        raise
    report['protected_cosmetics_after_sha256'] = digest(protected_cosmetics)
    report['dirty_content_after'] = sorted(p.get_path_name() for p in u.EditorLoadingAndSavingUtils.get_dirty_content_packages())
    report['dirty_maps_after'] = sorted(p.get_path_name() for p in u.EditorLoadingAndSavingUtils.get_dirty_map_packages())
    write_json(report_file, report)
    require(report['dirty_content_after'] == dirty_before and report['dirty_maps_after'] == map_dirty_before,
            'Dirty package set changed unexpectedly; inspect report before continuing')
    print('MC_COSMETICS_RECONCILED '+str(report_file))
    return report


if __name__ == '__main__':
    reconcile()
