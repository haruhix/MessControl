"""Repair the already-authored AdaptiveV2 clone; no import or live execution.

Root runs repair() inside idle UE. Existing report directories may be reused;
each pre-change WBP backup gets a unique filename. No camera/map/theme changes.
"""
from pathlib import Path
import hashlib
import json
import uuid

ROOT = Path('E:/DEVGAME/MessControl')
TARGET = '/Game/UI/MenuShowcase/AnatomyV2/UI/WBP_WashNSmashMenu_AdaptiveV2'


def require(ok, message):
    if not ok:
        raise RuntimeError(message)


def enum(kind, name):
    norm = lambda s: ''.join(c for c in s.lower() if c.isalnum())
    found = [getattr(kind, n) for n in dir(kind) if n.isupper() and norm(n) == norm(name)]
    require(len(found) == 1, 'Unverified enum ' + str(kind) + '.' + name)
    return found[0]


def repair(*, output='E:/CodexWork/MessControlMenuAnimation_20261022/AdaptiveOverlayRepair.json'):
    import unreal as u
    require(Path(u.Paths.project_dir()).resolve() == ROOT, 'Wrong project')
    require(not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor(), 'Preserve active PIE')
    def dirty():
        return sorted(p.get_path_name() for p in list(u.EditorLoadingAndSavingUtils.get_dirty_content_packages()) + list(u.EditorLoadingAndSavingUtils.get_dirty_map_packages()))
    require(not dirty(), 'Preserve unsaved packages: ' + ', '.join(dirty()))
    lib = u.EditorAssetLibrary
    asset = u.load_asset(TARGET)
    require(asset and lib.get_metadata_tag(asset, 'MC_AdaptiveMenuOverlay') == 'TexturePills_v2', 'Expected completed owned overlay clone')
    prefix = TARGET + '.WBP_WashNSmashMenu_AdaptiveV2:WidgetTree.'
    widgets = {w.get_name(): w for w in u.ObjectIterator(u.Widget) if w.get_path_name().startswith(prefix)}
    card, pages, main = (widgets[n] for n in ('MenuCard', 'MenuPages', 'MenuMain_Page'))
    size, scroll = card.get_content(), pages.get_parent()
    require(isinstance(size, u.SizeBox) and isinstance(scroll, u.ScrollBox), 'Preserve unexpected layout')
    column = size.get_content()
    require(isinstance(column, u.VerticalBox) and scroll.get_parent() == column and main.get_parent() == pages, 'Root/page chain changed')
    controls = {n: w.get_class().get_path_name() for n, w in widgets.items()}
    buttons = [widgets['MenuMain_Action' + str(i)] for i in (1, 2, 0, 3, 4, 5)]
    require(all(isinstance(b, u.MCMainMenuActionButton) for b in buttons), 'Native action adapter changed')
    actions = {b.get_name(): str(b.get_editor_property('action')) for b in buttons}
    defaults = u.get_default_object(u.load_class(None, TARGET + '.WBP_WashNSmashMenu_AdaptiveV2_C'))
    theme = defaults.get_editor_property('theme')
    texts = dict(defaults.get_editor_property('text_bindings'))
    require(theme and not defaults.get_editor_property('use_native_layout'), 'Preserve authored layout and existing palette')
    out = Path(output).resolve()
    require(out.drive.upper() == 'E:' and out.suffix == '.json', 'Choose an E: JSON report')
    out.parent.mkdir(parents=True, exist_ok=True)
    source = ROOT / 'Content' / (TARGET.removeprefix('/Game/') + '.uasset')
    require(source.is_file(), 'Expected saved target WBP')
    backup = out.parent / ('AdaptiveOverlay_BeforeRepair_' + uuid.uuid4().hex + '.uasset')
    data = source.read_bytes()
    backup.write_bytes(data)
    require(backup.read_bytes() == data, 'Backup verification failed')
    asset.modify()
    defaults.modify()
    hfill, vfill = enum(u.HorizontalAlignment, 'HAlign_Fill'), enum(u.VerticalAlignment, 'VAlign_Fill')
    def fill(slot, padding=None):
        slot.set_horizontal_alignment(hfill)
        slot.set_vertical_alignment(vfill)
        if padding is not None:
            slot.set_padding(padding)
    def child_size(rule):
        result = u.SlateChildSize()
        result.set_editor_property('size_rule', enum(u.SlateSizeRule, rule))
        result.set_editor_property('value', 1.)
        return result
    # Border properties alone did not update its saved UBorderSlot. Write the
    # slot as well: it is the authoritative source when Slate rebuilds.
    card.set_padding(u.Margin(12., 8., 12., 8.))
    fill(size.slot, u.Margin(12., 8., 12., 8.))
    size.set_width_override(440.)
    size.set_height_override(840.)
    fill(column.slot, u.Margin(0., 0., 0., 0.))
    fill(scroll.slot, u.Margin(0., 0., 0., 0.))
    scroll.slot.set_size(child_size('Fill'))
    fill(pages.slot, u.Margin(0., 0., 0., 0.))
    # UScrollBoxSlot sizes its content automatically; it has no SetSize API.
    fill(main.slot, u.Margin(0., 0., 0., 0.))
    repaired = []
    for button in buttons:
        name = button.get_name()
        primary = name == 'MenuMain_Action1'
        inset = button.get_content()
        pill_size = widgets[name + 'PillSizeV2']
        overlay, row = widgets[name + 'PillOverlayV2'], widgets[name + 'IconRowV2']
        image, label = widgets[name + 'PillV2'], widgets[name + 'Label']
        require(isinstance(inset, u.Border) and inset.get_content() == pill_size and pill_size.get_content() == overlay, 'Preserve custom button tree ' + name)
        # UButtonSlot defaults to CENTER/CENTER and 4/2 padding in UE5.8.
        # The outer border was full width but its inner pill followed text size.
        fill(inset.slot, u.Margin(0., 0., 0., 0.))
        inset.set_horizontal_alignment(hfill)
        inset.set_vertical_alignment(vfill)
        inset.set_padding(u.Margin(1.75, 1.75, 1.75, 1.75))
        fill(pill_size.slot, u.Margin(1.75, 1.75, 1.75, 1.75))
        fill(overlay.slot, u.Margin(0., 0., 0., 0.))
        fill(image.slot, u.Margin(0., 0., 0., 0.))
        row.slot.set_horizontal_alignment(hfill)
        row.slot.set_vertical_alignment(enum(u.VerticalAlignment, 'VAlign_Center'))
        row.slot.set_padding(u.Margin(22., 9., 22., 9.))
        label.slot.set_horizontal_alignment(hfill)
        label.slot.set_size(child_size('Fill'))
        label.set_auto_wrap_text(True)
        label.set_editor_property('wrap_text_at', 0.)
        pill_size.set_min_desired_height(72. if primary else 58.)
        image.set_render_opacity(1. if primary else .42)
        image.set_color_and_opacity(theme.get_color(enum(u.MCUIColorToken, 'Mint' if primary else 'Ink')))
        fill(button.slot, u.Margin(0., 0., 0., 12.))
        button.slot.set_size(child_size('Automatic'))
        repaired.append(name)
    styles = list(defaults.get_editor_property('style_bindings'))
    image_names = {b.get_name() + 'PillV2': ('Mint' if b.get_name() == 'MenuMain_Action1' else 'Ink') for b in buttons}
    changed = set()
    for style in styles:
        name = str(style.get_editor_property('widget_name'))
        if name in image_names:
            require(style.get_editor_property('target') == enum(u.MCUIStyleTarget, 'Image'), 'Unexpected fill binding')
            style.set_editor_property('color', enum(u.MCUIColorToken, image_names[name]))
            changed.add(name)
    require(changed == set(image_names), 'Some native palette Image bindings are missing')
    defaults.set_editor_property('style_bindings', styles)
    require(defaults.get_editor_property('theme') == theme and dict(defaults.get_editor_property('text_bindings')) == texts, 'Theme or localization changed')
    require(actions == {b.get_name(): str(b.get_editor_property('action')) for b in buttons}, 'Action changed')
    require(u.BlueprintEditorLibrary.compile_blueprint(asset) is not False, 'WBP compile failed')
    require('ERROR' not in str(asset.get_editor_property('status')).upper(), 'WBP has compiler errors')
    current = {w.get_name(): w for w in u.ObjectIterator(u.Widget) if w.get_path_name().startswith(prefix)}
    require(all(n in current and current[n].get_class().get_path_name() == klass for n, klass in controls.items()), 'Existing control changed')
    require(actions == {n: str(current[n].get_editor_property('action')) for n in actions}, 'Compiled action changed')
    require(lib.save_loaded_asset(asset, only_if_is_dirty=False), 'Could not save owned clone')
    require(not dirty(), 'Unexpected dirty package after scoped save: ' + ', '.join(dirty()))
    report = {'target': TARGET, 'backup': str(backup), 'backup_sha256': hashlib.sha256(data).hexdigest(),
              'changed_packages': [TARGET], 'fixed_controls': repaired,
              'fill_fix': 'ButtonSlot CENTER/CENTER -> FILL/FILL; zero button content padding; all nested slots Fill',
              'page_fix': 'Saved card BorderSlot padding24 ->12/8; design height800 ->840; preserve external footer and ScrollBox overflow fallback',
              'contrast_fix': 'Secondary fill: parent Theme Ink @.42 opacity; Cream labels/icons retained. Primary Mint/Ink unchanged.',
              'theme': theme.get_path_name(), 'imports': 0, 'activated': False,
              'validation': 'Scoped property/contract checks and Blueprint compile/save. Native captures at4:3/16:9/21:9 and long-label cultures pending root review.'}
    out.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf8')
    u.log('MC_ADAPTIVE_OVERLAY_REPAIR_PENDING_VISUAL_REVIEW ' + str(out))
    return report
