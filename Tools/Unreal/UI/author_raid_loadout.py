"""Save the approved raid layout as editable Designer trees using the native seeds."""

import json
from pathlib import Path
import shutil
from string import Formatter

import unreal as u


def describe_tree(blueprint):
    tree = u.find_object(blueprint, 'WidgetTree')
    assert isinstance(tree, u.WidgetTree), blueprint.get_path_name()
    nodes = sorted(
        (widget for widget in u.ObjectIterator(u.Widget) if widget.get_outer() == tree),
        key=lambda widget: widget.get_name(),
    )
    assert nodes and any(not widget.get_parent() for widget in nodes), blueprint.get_path_name()
    return [
        {
            'name': widget.get_name(),
            'class': widget.get_class().get_name(),
            'parent': widget.get_parent().get_name() if widget.get_parent() else None,
        }
        for widget in nodes
    ]


def append_localization(project, output):
    asset = '/Game/Gameplay/UI/Data/DT_UI_Localization'
    table = u.load_asset(asset)
    assert table, asset
    assert all(p.get_path_name() != asset for p in u.EditorLoadingAndSavingUtils.get_dirty_content_packages()), asset
    data = json.loads((project / 'Tools/Unreal/UI/raid_loadout_texts.json').read_text(encoding='utf-8'))
    rows = json.loads(u.DataTableFunctionLibrary.export_data_table_to_json_string(table))
    existing = {row['Name'] for row in rows}
    assert len(existing) == len(rows), 'Preserve a localization table with duplicate keys.'
    added = []
    for name, texts in data['labels'].items():
        assert len(texts) == len(data['languages']) and all(texts), name
        patterns = [{field for _, field, _, _ in Formatter().parse(text) if field is not None} for text in texts]
        assert all(pattern == patterns[0] for pattern in patterns), name
        if name in existing:
            continue
        context = 'Raid preparation and owned perks in the Hub.'
        rows.append({'Name': name, 'SourceText': texts[0], 'Context': context, **dict(zip(data['languages'], texts))})
        added.append(name)
    if added:
        backup = output / 'DT_UI_Localization.Before.uasset'
        if not backup.exists():
            shutil.copy2(project / 'Content/Gameplay/UI/Data/DT_UI_Localization.uasset', backup)
        assert u.DataTableFunctionLibrary.fill_data_table_from_json_string(table, json.dumps(rows, ensure_ascii=False))
        assert u.EditorAssetLibrary.save_loaded_asset(table, only_if_is_dirty=False)
    return added


root = Path(u.Paths.project_dir()).resolve()
assert root == Path('E:/DEVGAME/MessControl').resolve(), root
assert not u.EditorLevelLibrary.get_pie_worlds(True), 'Save the Designer trees after the current Play session.'
out = Path(globals().get('MC_RAID_LOADOUT_OUTPUT', 'E:/CodexWork/MessControl/Validation/RaidRightKit')).resolve()
assert out.drive.upper() == 'E:', out
out.mkdir(parents=True, exist_ok=True)
base = '/Game/Gameplay/UI/V2/'
specs = [
    ('WBP_UIRaidPerkRow', 'MCRaidPerkRowWidget',
     {'RaidRowSurface', 'RaidRowIcon', 'RaidRowName', 'RaidRowDetail'}),
    ('WBP_UIRaidLoadout', 'MCRaidLoadoutWidget',
     {'RaidRoot', 'RaidLeftGutter', 'RaidRightGutter', 'RaidTeamRows', 'RaidStockRows', 'RaidActiveRows',
      'RaidPassiveRows', 'RaidReady', 'RaidBack', 'RaidStart', 'RaidStockClose'}),
]
classes = {native: u.load_class(None, '/Script/MessControl.' + native) for _, native, _ in specs}
assert all(classes.values()), 'Load the editor build containing the raid loadout presenters.'
assert not any(p.get_path_name().startswith('/Game/Gameplay/UI/')
               for p in u.EditorLoadingAndSavingUtils.get_dirty_content_packages()), 'Preserve existing unsaved UI work.'
localization = append_localization(root, out)
report = []
for name, native, required in specs:
    asset = base + name
    existed = u.EditorAssetLibrary.does_asset_exist(asset)
    blueprint = u.MCGameplayUIEditorLibrary.create_editable_gameplay_widget(asset, classes[native])
    assert blueprint, asset
    if not existed:
        tree = u.find_object(blueprint, 'WidgetTree')
        if name == 'WBP_UIRaidLoadout':
            for node in ('RaidLeftFit', 'RaidRightFit'):
                u.find_object(tree, node).set_editor_property('stretch_direction', u.StretchDirection.BOTH)
        else:
            u.find_object(tree, 'RaidRowName').set_editor_property('clipping', u.WidgetClipping.CLIP_TO_BOUNDS_ALWAYS)
        u.BlueprintEditorLibrary.compile_blueprint(blueprint)
        assert u.EditorAssetLibrary.save_loaded_asset(blueprint, only_if_is_dirty=False), asset
    generated = u.load_class(None, asset + '.' + name + '_C')
    assert generated and isinstance(u.get_default_object(generated), getattr(u, native)), asset
    nodes = describe_tree(blueprint)
    missing = required - {node['name'] for node in nodes}
    assert not missing, (asset, 'Expected Designer nodes missing', sorted(missing))
    report.append({'asset': asset, 'parent': native, 'existingTreePreserved': existed, 'designerNodes': nodes})

(out / 'Authoring.json').write_text(
    json.dumps({'assets': report, 'localizationKeysAdded': localization}, ensure_ascii=False, indent=2), encoding='utf-8')
print('RAID_LOADOUT_EDITABLE_ASSETS', ', '.join(item['asset'] for item in report))
