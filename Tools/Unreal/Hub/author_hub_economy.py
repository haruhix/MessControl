"""Explicit, backed-up authoring of the Hub's TEST economy and grill station.

run('plan') is read-only with respect to Unreal packages. run('apply') creates
missing table rows, migrates the known empty-shop subtree, and optionally places
a grill driver from an inspected explicit assembly specification. Existing table
rows/prices, unrelated WBP layout, meshes and other maps are never overwritten.
No build, PIE, map switch, SaveAll, player-wallet edit or live roll is performed.
"""
from __future__ import annotations

import copy
from datetime import datetime, timezone
import json
import math
from pathlib import Path
import sys
import traceback
import uuid

ROOT = Path(__file__).resolve().parents[3]
HUB = '/Game/Maps/Hub'
PERKS = '/Game/Gameplay/Roguelike/DT_Perks'
COSMETICS = '/Game/Data/DT_CharacterCosmetics'
SHOP = '/Game/Gameplay/UI/WBP_HubShop'
TEXTS = '/Game/Gameplay/UI/Data/DT_UI_Localization'
DATA = '/Game/Gameplay/Hub/Economy'
OWNER = 'MC.HubEconomy.TestV1'
TABLES = (
    ('catalog', DATA+'/DT_HubCatalog', 'MCHubCatalogRow'),
    ('rewards', DATA+'/DT_GrillRewards', 'MCGrillRewardRow'),
    ('rules', DATA+'/DT_HubEconomyRules', 'MCHubEconomyRulesRow'),
)
SYMBOLS = ('Cherry', 'Lemon', 'Watermelon', 'Plum', 'Bell', 'Seven', 'Bar', 'X', 'Star')
STATS = (
    ('Speed', 'NormalSpeed', 18, 'Lemon', 'Быстрые ноги', 'Quick feet'),
    ('Stamina', 'MaxStamina', 18, 'Bell', 'Запас сил', 'Stamina reserve'),
    ('Recovery', 'StaminaRecovery', 20, 'Plum', 'Второе дыхание', 'Second wind'),
    ('Damage', 'PickaxeDamage', 24, 'Bar', 'Тяжёлый удар', 'Heavy hit'),
    ('BrushWidth', 'BrushWidth', 22, 'Watermelon', 'Широкая щётка', 'Wide brush'),
    ('BrushPower', 'BrushPower', 24, 'X', 'Сильная щётка', 'Power brush'),
    ('Health', 'MaxHealth', 22, 'Star', 'Толстая эмаль', 'Thick enamel'),
)
UI_COPY = (
    ('UI.Shop.NextRunHint', 'Бустеры — на следующий забег', 'Boosters apply to the next run'),
    ('UI.Shop.Balance', 'Монеты: {0}', 'Coins: {0}'),
    ('UI.Shop.BuyPrice', 'Купить · {0}', 'Buy · {0}'),
    ('UI.Shop.Price', '{0} монет', '{0} coins'),
    ('UI.Shop.Queued', 'На следующий забег: {0}', 'Next run: {0}'),
    ('UI.Shop.Owned', 'Получено', 'Owned'),
    ('UI.Shop.NoItems', 'Ассортимент пока недоступен', 'The catalog is currently unavailable'),
    ('UI.Shop.Buy', 'Купить', 'Buy'),
)


def require(value, message):
    if not value:
        raise RuntimeError(message)


def _enum(value):
    return str(value).rsplit('::', 1)[-1]


def _is_none(value):
    return value is None or str(value) in ('', 'None', 'NAME_None')


def _unique(rows, label):
    result = {}
    for row in rows:
        name = row.get('Name', '')
        require(name and name.casefold() != 'none' and name.casefold() not in result,
            'Ambiguous row name in '+label+': '+name)
        result[name.casefold()] = row
    return result


def select_perks(rows):
    """Resolve effects from the actual saved table, not copied/generated IDs."""
    result = {}
    for short, stat, *_ in STATS:
        candidates = []
        for row in rows:
            modifiers = row.get('Modifiers', [])
            if (_enum(row.get('Polarity')) == 'Positive' and _is_none(row.get('ToolUpgrade'))
                and not row.get('bRequiresTeam', False) and not row.get('bConsumableBooster', False)
                and _is_none(row.get('RequiredTool')) and not row.get('RequiredEvents', [])
                and row.get('MaxStacks', 0) > 0 and len(modifiers) == 1
                and _enum(modifiers[0].get('Stat')) == stat
                and _enum(modifiers[0].get('Condition')) == 'Always'
                and modifiers[0].get('Value', 0) > 0):
                candidates.append(row)
        require(candidates, 'No standalone positive '+stat+' perk in saved DT_Perks')
        result[short] = sorted(candidates, key=lambda row: (not row.get('bEarlyChoice', False), row['Name']))[0]
    candidates = [row for row in rows if _enum(row.get('Polarity')) == 'Positive'
        and _enum(row.get('ToolUpgrade')) == 'MeshaBrush' and row.get('MaxStacks', 0) > 0
        and not row.get('bRequiresTeam', False)]
    require(candidates, 'No existing positive MeshaBrush perk in saved DT_Perks')
    result['SuperBrush'] = sorted(candidates, key=lambda row: (_enum(row.get('Rarity')) != 'Rare', row['Name']))[0]
    return result


def plan_rows(perks, cosmetics):
    from localization_catalog import text_source
    selected = select_perks(perks)
    known_cosmetics = _unique(cosmetics, COSMETICS)
    catalog, rewards, copy_rows = [], [], list(UI_COPY)
    for order, (short, stat, price, symbol, ru, en) in enumerate(STATS):
        source = selected[short]
        item = 'Booster_'+short
        description = text_source(source.get('Description', ''))
        catalog.append(dict(Name=item, bEnabled=True, bInShop=True, Kind='NextRunBooster',
            DisplayName=ru, Description=description, DisplayNameKey='UI.HubCatalog.'+item+'.Name',
            DescriptionKey='UI.HubCatalog.'+item+'.Description', Icon=source.get('Icon', 'None'),
            Rarity='Common', Price=price, PerkID=source['Name'], Quantity=1,
            MaximumQueuedStacks=min(3, int(source['MaxStacks'])), DuplicateCoins=7,
            SortOrder=order, ReelSymbol=symbol))
        combo = [symbol]*3 if short != 'Health' else ['Star', 'Bell', 'Star']
        rewards.append(dict(Name='Loot_'+item, bEnabled=True, Weight=3.5, Kind='CatalogItem',
            Rarity='Common', CatalogItem=item, Quantity=1, ReelSymbols=combo))
        copy_rows.extend(((catalog[-1]['DisplayNameKey'], ru, en),
            (catalog[-1]['DescriptionKey'], description, 'Next run only. '+en+'.')))
    source = selected['SuperBrush']
    item = 'Booster_SuperBrush'
    description = text_source(source.get('Description', ''))
    catalog.append(dict(Name=item, bEnabled=True, bInShop=True, Kind='NextRunBooster',
        DisplayName='Суперщётка', Description=description, DisplayNameKey='UI.HubCatalog.'+item+'.Name',
        DescriptionKey='UI.HubCatalog.'+item+'.Description', Icon=source.get('Icon', 'None'),
        Rarity='Rare', Price=60, PerkID=source['Name'], Quantity=1, MaximumQueuedStacks=1,
        DuplicateCoins=20, SortOrder=7, ReelSymbol='Star'))
    rewards.append(dict(Name='Loot_'+item, bEnabled=True, Weight=1.5, Kind='CatalogItem',
        Rarity='Rare', CatalogItem=item, Quantity=1, ReelSymbols=['Star']*3))
    copy_rows.extend(((catalog[-1]['DisplayNameKey'], 'Суперщётка', 'Super brush'),
        (catalog[-1]['DescriptionKey'], description, 'Unlocks the existing super brush for your next run.')))
    for offset, (item, ru, en, price, combo, weight) in enumerate((
        ('Hat_WorkCap', 'Рабочая кепка', 'Work cap', 35, ['Cherry', 'Star', 'Cherry'], 3),
        ('Hat_WorkCapRed', 'Красная кепка', 'Red cap', 45, ['Bell', 'Star', 'Bell'], 2),
        ('Face_SportGlasses', 'Спортивные очки', 'Sport glasses', 40, ['Bar', 'Star', 'Bar'], 3))):
        cosmetic = known_cosmetics.get(item.casefold())
        require(cosmetic and cosmetic.get('bEnabled', True) and not _is_none(cosmetic.get('Mesh')),
            'Author/verify the actual cosmetic before economy row: '+item)
        name_key, desc_key = 'UI.HubCatalog.'+item+'.Name', 'UI.HubCatalog.'+item+'.Description'
        catalog.append(dict(Name=item, bEnabled=True, bInShop=True, Kind='Cosmetic', DisplayName=ru,
            Description='Аксессуар для гардероба. Только внешний вид.', DisplayNameKey=name_key,
            DescriptionKey=desc_key, Rarity='Rare', Price=price, CosmeticID=item, Quantity=1,
            MaximumQueuedStacks=1, DuplicateCoins=12, SortOrder=20+offset, ReelSymbol=combo[0]))
        rewards.append(dict(Name='Loot_'+item, bEnabled=True, Weight=weight, Kind='CatalogItem',
            Rarity='Rare', CatalogItem=item, Quantity=1, ReelSymbols=combo))
        copy_rows.extend(((name_key, ru, en), (desc_key, 'Аксессуар для гардероба. Только внешний вид.',
            'A wardrobe accessory. Appearance only.')))
    item = 'CalciumSelfGrenade'
    catalog.append(dict(Name=item, bEnabled=True, bInShop=True, Kind='Consumable',
        DisplayName='Кальциевая граната', Description='Следующий забег · H: под себя, +35 HP через 0,45 с. До 3 зарядов/забег; только себе.',
        DisplayNameKey='UI.HubCatalog.'+item+'.Name', DescriptionKey='UI.HubCatalog.'+item+'.Description',
        Rarity='Common', Price=12, ConsumableID=item, Quantity=1, MaximumQueuedStacks=3,
        DuplicateCoins=5, SortOrder=10, ReelSymbol='Seven'))
    rewards.append(dict(Name='Loot_'+item, bEnabled=True, Weight=5, Kind='CatalogItem',
        Rarity='Common', CatalogItem=item, Quantity=1, ReelSymbols=['Seven', 'X', 'Seven']))
    copy_rows.extend(((catalog[-1]['DisplayNameKey'], catalog[-1]['DisplayName'], 'Calcium grenade'),
        (catalog[-1]['DescriptionKey'], catalog[-1]['Description'],
         'Next run · H: toss at your feet, heal 35 HP after 0.45 s. Up to 3 charges/run; owner only.')))
    rewards[0:0] = [dict(Name=name, bEnabled=True, Weight=weight, Kind='Coins', Rarity=rarity,
        MinimumCoins=low, MaximumCoins=high, ReelSymbols=combo) for name, weight, low, high, rarity, combo in (
            ('NoPrize_Mixed', 35, 0, 0, 'Common', ['Cherry', 'Lemon', 'Watermelon']),
            ('Coins_Medium', 18, 12, 20, 'Common', ['Cherry']*3),
            ('Coins_Large', 8, 30, 45, 'Rare', ['Seven']*3))]
    require(len({tuple(row['ReelSymbols']) for row in rewards}) == len(rewards), 'Ambiguous reel combination')
    require(all(set(row['ReelSymbols']).issubset(SYMBOLS) and len(row['ReelSymbols']) == 3 for row in rewards), 'Unknown physical symbol')
    require(abs(sum(row['Weight'] for row in rewards)-100) < .001, 'Initial roll weights must total 100')
    rules = [dict(Name='Default', bCalciumEnabled=False, MaximumCoins=1000000, MaximumPendingBoosters=32,
        MaximumOwnedCosmetics=256, LidCloseSeconds=.4, RollSeconds=2., RevealSeconds=1.2, LidOpenSeconds=.4)]
    return dict(catalog=catalog, rewards=rewards, rules=rules), copy_rows, {key: row['Name'] for key, row in selected.items()}


def append_missing(before, proposed, label):
    known = _unique(before, label)
    added = [copy.deepcopy(row) for row in proposed if row['Name'].casefold() not in known]
    return copy.deepcopy(before)+added, [row['Name'] for row in added]


def _vector(values, label):
    require(isinstance(values, (list, tuple)) and len(values) == 3
        and all(isinstance(value, (int, float)) and math.isfinite(value) for value in values), label+' must be finite XYZ')


def validate_grill(spec):
    if spec is None:
        return
    for key in ('location', 'rotation', 'extent', 'hinge_local', 'hinge_axis'):
        _vector(spec.get(key), key)
    require(all(25 <= value <= 2000 for value in spec['extent']), 'Invalid grill OBB half extents')
    require(sum(value*value for value in spec['hinge_axis']) > .0001, 'Hinge axis cannot be zero')
    require(isinstance(spec.get('open_degrees'), (int, float)) and math.isfinite(spec['open_degrees'])
        and 1 <= abs(spec['open_degrees']) <= 120, 'Explicit inspected lid-open angle is required')
    require(spec.get('lid_actor') and len(spec.get('reel_actors', [])) == 3
        and len(set([spec['lid_actor']]+spec['reel_actors'])) == 4, 'Four separate inspected lid/reel actors are required')
    stops = spec.get('symbol_stop_angles', {})
    require(set(stops) == set(SYMBOLS) and all(isinstance(v, (int, float)) and math.isfinite(v) for v in stops.values()),
        'Provide calibrated stop angles for all nine actual atlas symbols')


def author_grill(u, spec, report):
    if spec is None:
        return
    world = u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
    require(world and world.get_path_name().split('.', 1)[0] == HUB, 'Hub must already be open')
    cls = u.load_class(None, '/Script/MessControl.MCHubGrillStation')
    require(cls, 'Rebuild Editor before authoring grill station')
    subsystem = u.get_editor_subsystem(u.EditorActorSubsystem)
    existing = list(u.GameplayStatics.get_all_actors_of_class(world, cls))
    require(len(existing) <= 1, 'Review duplicate grill stations before authoring')
    if existing:
        report['grill'] = dict(actor=existing[0].get_path_name(), existing_preserved=True)
        return
    by_path = {actor.get_path_name(): actor for actor in subsystem.get_all_level_actors()}
    actors = [by_path.get(path) for path in [spec['lid_actor']]+spec['reel_actors']]
    require(all(actors), 'An inspected assembly actor path no longer resolves')
    require(all(actor.get_attach_parent_actor() is None for actor in actors), 'Attached assembly requires explicit review')
    require(all(actor.get_root_component() for actor in actors), 'Assembly has a missing root component')
    baseline = actors[1].get_actor_rotation()
    before = [dict(path=actor.get_path_name(), transform=actor.get_actor_transform().export_text()) for actor in actors]
    for actor in actors:
        actor.modify()
        actor.get_root_component().set_mobility(u.ComponentMobility.MOVABLE)
    for reel in actors[1:]:
        reel.set_actor_rotation(baseline, False)
    pitch, yaw, roll = spec['rotation']
    station = subsystem.spawn_actor_from_class(cls, u.Vector(*spec['location']), u.Rotator(pitch=pitch, yaw=yaw, roll=roll))
    require(station, 'Could not create grill driver')
    station.set_actor_label('Hub_GrillSlotStation')
    station.tags = list(station.tags)+[u.Name(OWNER)]
    station.set_editor_property('lid_actor', actors[0])
    station.set_editor_property('reel_actors', actors[1:])
    station.set_editor_property('lid_hinge_local', u.Vector(*spec['hinge_local']))
    station.set_editor_property('lid_hinge_axis_local', u.Vector(*spec['hinge_axis']))
    station.set_editor_property('lid_open_degrees', spec['open_degrees'])
    station.set_editor_property('symbol_stop_angles', {u.Name(k): v for k, v in spec['symbol_stop_angles'].items()})
    station.get_editor_property('interaction_volume').set_box_extent(u.Vector(*spec['extent']), False)
    report['grill'] = dict(actor=station.get_path_name(), explicit_spec=spec, assembly_before=before,
        normalized_reel_rotation=baseline.export_text(), created=True)


def run(action='plan', *, output='E:/CodexWork/HubSystems_20261023/EconomyAuthoring',
        grill=None, refresh_shop=True, localize=True):
    import unreal as u
    from author_hub_wardrobe import backup, dirty, on_e, read_rows, write_json
    ui_tools = str(ROOT/'Tools/Unreal/UI')
    if ui_tools not in sys.path:
        sys.path.insert(0, ui_tools)
    from localization_catalog import index_rows, migrate_localization_row
    require(action in ('plan', 'apply'), 'Choose plan or apply explicitly')
    require(on_e(u.Paths.project_dir()) == ROOT, 'Wrong project')
    require(not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor(), 'Stop PIE before authoring')
    require(not dirty(u), 'Preserve unsaved editor packages before authoring: '+str(dirty(u)))
    validate_grill(grill)
    if grill is not None:
        world = u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
        require(world and world.get_path_name().split('.', 1)[0] == HUB, 'Open Hub explicitly; authoring does not switch maps')
    out = on_e(output)/(datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S%fZ')+'_'+uuid.uuid4().hex[:8])
    out.mkdir(parents=True, exist_ok=False)
    report = dict(action=action, status='running', output=str(out), backups=[], initially_absent=[],
        saved_assets=[], no_build=True, no_pie=True, existing_rows_preserved=True,
        test_economy=True, deferred_validation=['Native build', 'Solo interaction/visual checks', 'Network/reconnect not tested'])
    try:
        perks, cosmetics = u.load_asset(PERKS), u.load_asset(COSMETICS)
        require(isinstance(perks, u.DataTable) and isinstance(cosmetics, u.DataTable), 'Author current perks and cosmetic catalogs first')
        planned, copy_rows, selected = plan_rows(read_rows(u, perks), read_rows(u, cosmetics))
        report['resolved_perks'] = selected
        loaded = {}
        for key, package, struct_name in TABLES:
            structure = u.load_object(None, '/Script/MessControl.'+struct_name)
            require(structure, 'Build reflected table row first: '+struct_name)
            table = u.load_asset(package)
            if table:
                require(isinstance(table, u.DataTable) and u.DataTableFunctionLibrary.get_data_table_row_struct(table) == structure,
                    'Preserve incompatible existing table: '+package)
            before = read_rows(u, table) if table else []
            rows, added = append_missing(before, planned[key], package)
            loaded[key] = (table, structure, before, rows)
            report[key] = dict(package=package, added_rows=added, rows=rows)
        text_table = u.load_asset(TEXTS) if localize else None
        if localize:
            require(isinstance(text_table, u.DataTable), 'Shared localization DataTable is required')
            before_texts = read_rows(u, text_table)
            known = index_rows(before_texts, 'shared UI localization')
            additions = [migrate_localization_row(dict(Name=key, RU=ru, EN=en, SourceText=ru,
                Context='Hub test economy/catalog; other languages use existing fallback'))
                for key, ru, en in copy_rows if key.casefold() not in known]
            planned_texts = copy.deepcopy(before_texts)+additions
            report['localization_added'] = [row['Name'] for row in additions]
        write_json(out/'Plan.json', report)
        if action == 'plan':
            report['status'] = 'planned'
            return report
        # Back up every intended package before the first Unreal mutation.
        for _, package, _ in TABLES:
            backup(package, out, report)
        if localize:
            backup(TEXTS, out, report)
        if refresh_shop:
            backup(SHOP, out, report)
        if grill is not None:
            backup(HUB, out, report)
        if refresh_shop:
            blueprint = u.load_asset(SHOP)
            require(blueprint and u.MCGameplayUIEditorLibrary.refresh_hub_shop_catalog_presentation(blueprint),
                'Editable shop catalog migration failed')
            report['saved_assets'].append(SHOP)
        for key, package, _ in TABLES:
            table, structure, before, rows = loaded[key]
            if before == rows:
                continue
            if table is None:
                folder, name = package.rsplit('/', 1)
                factory = u.DataTableFactory()
                factory.set_editor_property('struct', structure)
                table = u.AssetToolsHelpers.get_asset_tools().create_asset(name, folder, u.DataTable, factory)
                require(table, 'Could not create '+package)
            table.modify()
            require(u.DataTableFunctionLibrary.fill_data_table_from_json_string(table, json.dumps(rows, ensure_ascii=False)),
                'Table import failed: '+package)
            actual = _unique(read_rows(u, table), package)
            for row in before:
                require(actual.get(row['Name'].casefold()) == row, 'Existing authored row changed: '+row['Name'])
            u.EditorAssetLibrary.set_metadata_tag(table, 'MC.Author', OWNER)
            require(u.EditorAssetLibrary.save_loaded_asset(table, only_if_is_dirty=False), 'Save failed: '+package)
            report['saved_assets'].append(package)
        if localize and additions:
            from author_localization_catalog import native_validate
            text_table.modify()
            require(u.DataTableFunctionLibrary.fill_data_table_from_json_string(text_table,
                json.dumps(planned_texts, ensure_ascii=False)), 'Could not append catalog localization')
            actual = index_rows(read_rows(u, text_table), 'updated UI localization')
            require(all(actual.get(row['Name'].casefold()) == row for row in before_texts), 'Existing UI text changed')
            valid, problems = native_validate(u, text_table, text_table)
            require(valid, 'Localization validation failed: '+str(problems))
            require(u.EditorAssetLibrary.save_loaded_asset(text_table, only_if_is_dirty=False), 'Could not save localization')
            report['saved_assets'].append(TEXTS)
        if grill is not None:
            author_grill(u, grill, report)
            if report.get('grill', {}).get('created'):
                world = u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
                require(u.EditorLoadingAndSavingUtils.save_map(world, HUB), 'Could not save only the Hub map')
                report['saved_assets'].append(HUB)
        report['dirty_after'] = dirty(u)
        require(not report['dirty_after'], 'Authoring left unsaved packages for review')
        report['status'] = 'authored'
        return report
    except Exception:
        report['status'] = 'failed'
        report['error'] = traceback.format_exc()
        report['dirty_after'] = dirty(u)
        report['rollback_policy'] = 'Verified before-package backups retained; do not overwrite packages loaded in Unreal externally.'
        raise
    finally:
        write_json(out/'Report.json', report)


if __name__ == '__main__':
    print('Import this module in the already-open, clean Editor; call run("plan") before run("apply").')
