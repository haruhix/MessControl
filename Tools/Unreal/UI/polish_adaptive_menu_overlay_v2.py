"""Root-run, opt-in presentation pass for the owned AdaptiveV2 WBP only.

No map/camera/shared theme/source WBP changes, no play, no build. Native action
buttons and localized labels survive. Texture-backed Images keep their shape
when native ApplyTheme refreshes their palette tint; native Button bindings are
removed only for the six main-page controls whose state brushes we author.
"""
from pathlib import Path
import hashlib
import json
import math
import struct
import zlib

ROOT = Path('E:/DEVGAME/MessControl')
TARGET = '/Game/UI/MenuShowcase/AnatomyV2/UI/WBP_WashNSmashMenu_AdaptiveV2'
TEXTURES = '/Game/UI/MenuShowcase/AnatomyV2/UI/OverlayTextures'
SOURCES = ROOT / 'ArtSource/UI/MenuShowcase_20261021/UI/AdaptiveOverlayV2'
OWNER_KEY, OWNER = 'MC_AdaptiveMenuOverlay', 'TexturePills_v2'
ORDER = (1, 2, 0, 3, 4, 5)
ICONS = {1: 'Play', 2: 'Friends', 0: 'Tutorial', 3: 'Settings', 4: 'Credits', 5: 'Exit'}


def require(ok, message):
    if not ok:
        raise RuntimeError(message)


def enum(kind, name):
    norm = lambda s: ''.join(c for c in s.lower() if c.isalnum())
    found = [getattr(kind, n) for n in dir(kind) if n.isupper() and norm(n) == norm(name)]
    require(len(found) == 1, 'Unverified enum ' + str(kind) + '.' + name)
    return found[0]


def png(path, width, height, coverage):
    def chunk(kind, data):
        return struct.pack('!I', len(data)) + kind + data + struct.pack('!I', zlib.crc32(kind + data) & 0xffffffff)
    raw = bytearray()
    for y in range(height):
        raw.append(0)
        for x in range(width):
            alpha = max(0., min(1., coverage(x + .5, y + .5)))
            raw.extend((255, 255, 255, round(alpha * 255)))
    data = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('!2I5B', width, height, 8, 6, 0, 0, 0))
    data += chunk(b'IDAT', zlib.compress(bytes(raw), 9)) + chunk(b'IEND', b'')
    path.write_bytes(data)


def generate_sources():
    """Small analytic UI masks, not a flattened menu or generated screenshot."""
    SOURCES.mkdir(parents=True, exist_ok=True)
    pill = SOURCES / 'T_MenuPill.png'
    png(pill, 128, 64, lambda x, y: .5 - (math.hypot(max(abs(x-64)-32, 0), abs(y-32)) - 32))

    def segment(x, y, a, b):
        dx, dy = b[0]-a[0], b[1]-a[1]
        t = max(0., min(1., ((x-a[0])*dx+(y-a[1])*dy) / max(dx*dx+dy*dy, .0001)))
        return math.hypot(x-a[0]-t*dx, y-a[1]-t*dy)

    def icon(name, x, y):
        x, y = x/2., y/2.
        paths, rings = [], []
        filled = False
        if name == 'Play':
            filled = x >= 10 and x <= 24 and abs(y-16) <= (24-x)*.72
        elif name == 'Friends':
            rings = [(12, 10, 3), (22, 11, 2.5)]
            paths = [[(5, 25), (5, 21), (8, 18), (16, 18), (19, 21), (19, 25)],
                     [(21, 18), (25, 18), (28, 21), (28, 25)]]
        elif name == 'Tutorial':
            paths = [[(16, 10), (11, 7), (5, 7), (5, 24), (11, 24), (16, 27), (21, 24), (27, 24), (27, 7), (21, 7), (16, 10), (16, 27)]]
        elif name == 'Settings':
            rings = [(16, 16, 4), (16, 16, 9)]
            for angle in range(0, 360, 45):
                c, s = math.cos(math.radians(angle)), math.sin(math.radians(angle))
                paths.append([(16+9*c, 16+9*s), (16+13*c, 16+13*s)])
        elif name == 'Credits':
            rings = [(16, 16, 11)]
            paths = [[(16, 14), (16, 23)], [(16, 9), (16, 10)]]
        elif name == 'Exit':
            paths = [[(12, 5), (5, 5), (5, 27), (12, 27)], [(12, 16), (28, 16)], [(22, 10), (28, 16), (22, 22)]]
        distances = [segment(x, y, a, b) for path in paths for a, b in zip(path, path[1:])]
        distances += [abs(math.hypot(x-cx, y-cy)-radius) for cx, cy, radius in rings]
        return 1. if filled else max(0., min(1., (1.25-min(distances, default=100.))*2))
    for name in ICONS.values():
        png(SOURCES / ('T_MenuIcon_' + name + '.png'), 64, 64, lambda x, y, n=name: icon(n, x, y))
    return {p.stem: {'path': str(p), 'sha256': hashlib.sha256(p.read_bytes()).hexdigest()}
            for p in sorted(SOURCES.glob('T_Menu*.png'))}


def run(action='plan', *, output='E:/CodexWork/MessControlMenuAnimation_20261022/AdaptiveOverlayApply01'):
    import unreal as u
    require(action in ('plan', 'apply'), 'Expected plan or apply')
    require(Path(u.Paths.project_dir()).resolve() == ROOT, 'Wrong project')
    require(not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor(), 'Preserve active PIE')
    lib = u.EditorAssetLibrary
    asset = u.load_asset(TARGET)
    require(asset and lib.get_metadata_tag(asset, 'MC_MenuViewportFitV2') == 'AdaptivePreview_v2', 'Expected owned adaptive clone')
    dirty = sorted(p.get_path_name() for p in list(u.EditorLoadingAndSavingUtils.get_dirty_content_packages()) + list(u.EditorLoadingAndSavingUtils.get_dirty_map_packages()))
    require(action == 'plan' or not dirty, 'Preserve unsaved packages: ' + ', '.join(dirty))
    prefix = TARGET + '.WBP_WashNSmashMenu_AdaptiveV2:WidgetTree.'
    widgets = {w.get_name(): w for w in u.ObjectIterator(u.Widget) if w.get_path_name().startswith(prefix)}
    required = ['MenuBackdrop', 'MenuCard', 'MenuTitle', 'MenuAdaptiveCanvasV2', 'MenuMain_Page']
    require(all(n in widgets for n in required), 'Adaptive layout contract missing')
    buttons = [widgets['MenuMain_Action' + str(i)] for i in ORDER]
    require(all(isinstance(b, u.MCMainMenuActionButton) for b in buttons), 'Preserve native action adapters')
    before_controls = {n: w.get_class().get_path_name() for n, w in widgets.items()}
    before_actions = {b.get_name(): str(b.get_editor_property('action')) for b in buttons}
    cls = u.load_class(None, TARGET + '.WBP_WashNSmashMenu_AdaptiveV2_C')
    defaults = u.get_default_object(cls)
    require(isinstance(defaults, u.MCMainMenuWidget), 'Native main menu parent changed')
    theme = defaults.get_editor_property('theme')
    require(theme, 'Keep existing parent palette; no private palette fork is created')
    old_text_bindings = dict(defaults.get_editor_property('text_bindings'))
    report = dict(action=action, target=TARGET, theme=theme.get_path_name(), dirty_before=dirty,
                  actions_before=before_actions, changed_packages=[], backups=[], activated=False,
                  design={'panel': 'Transparent; no opaque rectangle', 'primary': 'Mint texture pill / Ink caption',
                          'secondary': 'Button palette texture pill, .72 opacity / Cream caption',
                          'left_host': '.36 viewport width; 40/32/20/32 dp insets; ScaleToFit DownOnly',
                          'design_size_dp': [440, 800], 'button_min_heights_dp': [72, 58], 'gap_dp': 12,
                          'icons': 'Six editable monochrome texture slots; no Unicode/font dependency'},
                  limits=['Offline-prepared helper; native runtime/visual validation is pending.',
                          'Hover/pressed rim colors are authored cream constants; palette tint of fills, labels and icons remains dynamic.',
                          'Native title is one localized TextBlock: cream is preserved; a split-color wordmark needs a later explicit title presenter.',
                          'Existing Join/Settings/connection pages retain their controls; long text still needs actual aspect/culture captures.',
                          'No animation, map, camera, theme role or shared source widget is changed.'])
    if action == 'plan':
        u.log(json.dumps(report, ensure_ascii=False, indent=2))
        return report
    out = Path(output).resolve()
    require(out.drive.upper() == 'E:' and not out.exists(), 'Choose fresh E: report folder')
    out.mkdir(parents=True)

    def backup(package):
        source = ROOT / 'Content' / (package.removeprefix('/Game/') + '.uasset')
        if source.exists():
            dest = out / 'Before' / source.relative_to(ROOT)
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_bytes(source.read_bytes())
            require(dest.read_bytes() == source.read_bytes(), 'Backup mismatch')
            report['backups'].append({'package': package, 'path': str(dest), 'sha256': hashlib.sha256(dest.read_bytes()).hexdigest()})
    backup(TARGET)
    sources = generate_sources()
    textures = {}
    for name, info in sources.items():
        package = TEXTURES + '/' + name
        texture = u.load_asset(package) if lib.does_asset_exist(package) else None
        require(not texture or lib.get_metadata_tag(texture, OWNER_KEY) == OWNER, 'Preserve unrelated texture ' + package)
        backup(package)
        if texture is None:
            task = u.AssetImportTask()
            task.set_editor_property('filename', info['path'])
            task.set_editor_property('destination_path', TEXTURES)
            task.set_editor_property('destination_name', name)
            task.set_editor_property('automated', True)
            task.set_editor_property('save', False)
            u.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
            texture = u.load_asset(package)
        require(isinstance(texture, u.Texture2D), 'UI texture import failed ' + package)
        texture.modify()
        texture.set_editor_property('lod_group', enum(u.TextureGroup, 'TEXTUREGROUP_UI'))
        texture.set_editor_property('compression_settings', enum(u.TextureCompressionSettings, 'TC_EditorIcon'))
        texture.set_editor_property('mip_gen_settings', enum(u.TextureMipGenSettings, 'TMGS_NoMipmaps'))
        lib.set_metadata_tag(texture, OWNER_KEY, OWNER)
        require(lib.save_loaded_asset(texture, only_if_is_dirty=False), 'Could not save owned UI texture')
        report['changed_packages'].append(package)
        textures[name] = texture
    asset.modify()
    defaults.modify()
    tree = widgets['MenuTitle'].get_outer()
    white, transparent = u.LinearColor(1, 1, 1, 1), u.LinearColor(1, 1, 1, 0)
    def slate_color(color):
        value = u.SlateColor()
        value.set_editor_property('specified_color', color)
        return value
    def empty_brush():
        brush = u.SlateBrush()
        brush.set_editor_property('draw_as', enum(u.SlateBrushDrawType, 'NoDrawType'))
        return brush
    def rim(alpha):
        brush = u.SlateBrush()
        brush.set_editor_property('draw_as', enum(u.SlateBrushDrawType, 'RoundedBox'))
        brush.set_editor_property('tint_color', slate_color(transparent))
        outline = brush.get_editor_property('outline_settings')
        outline.set_editor_property('rounding_type', enum(u.SlateBrushRoundingType, 'HalfHeightRadius'))
        outline.set_editor_property('color', slate_color(u.LinearColor(.93, .88, .76, alpha)))
        outline.set_editor_property('width', 1.5)
        brush.set_editor_property('outline_settings', outline)
        return brush
    def new(kind, name):
        obj = widgets.get(name)
        require(obj is None or isinstance(obj, kind), 'Preserve custom widget ' + name)
        if obj is None:
            obj = u.new_object(kind, outer=tree, name=name)
            widgets[name] = obj
        return obj
    def fill(slot):
        slot.set_horizontal_alignment(enum(u.HorizontalAlignment, 'HAlign_Fill'))
        slot.set_vertical_alignment(enum(u.VerticalAlignment, 'VAlign_Fill'))
    def style(name, token, target, size=0, bold=False):
        value = u.MCUIStyleBinding()
        for prop, val in [('widget_name', name), ('color', enum(u.MCUIColorToken, token)), ('target', enum(u.MCUIStyleTarget, target)), ('font_size', size), ('bold', bold)]:
            value.set_editor_property(prop, val)
        return value
    for name in ('MenuBackdrop', 'MenuCard'):
        widgets[name].set_brush(empty_brush())
        widgets[name].set_brush_color(transparent)
    widgets['MenuCard'].set_padding(u.Margin(12, 8, 12, 8))
    title = widgets['MenuTitle']
    title.set_auto_wrap_text(True)
    title.set_editor_property('wrap_text_at', 0.)
    title.slot.set_padding(u.Margin(0, 0, 0, 22))
    styles = list(defaults.get_editor_property('style_bindings'))
    replaced = {'MenuBackdrop', 'MenuCard', 'MenuTitle'}
    authored = [style('MenuTitle', 'Cream', 'Text', 60, True)]
    for index, button in zip(ORDER, buttons):
        name = button.get_name()
        label = widgets[name + 'Label']
        inset = button.get_content()
        require(isinstance(inset, u.Border), 'Existing button inset contract changed')
        replaced.update((name, label.get_name(), inset.get_name()))
        primary = index == 1
        button.modify()
        button.set_background_color(white)
        button.set_color_and_opacity(white)
        state = button.get_editor_property('widget_style')
        for prop, alpha in [('normal', .12), ('hovered', .70), ('pressed', 1.), ('disabled', .06)]:
            state.set_editor_property(prop, rim(alpha))
        state.set_editor_property('normal_padding', u.Margin(0))
        state.set_editor_property('pressed_padding', u.Margin(0, 1, 0, 0))
        button.set_style(state)
        inset.set_brush(empty_brush())
        inset.set_padding(u.Margin(1.75))
        size = new(u.SizeBox, name + 'PillSizeV2')
        overlay = new(u.Overlay, name + 'PillOverlayV2')
        background = new(u.Image, name + 'PillV2')
        row = new(u.HorizontalBox, name + 'IconRowV2')
        icon_size = new(u.SizeBox, name + 'IconSlotV2')
        icon = new(u.Image, name + 'IconV2')
        if inset.get_content() != size:
            require(inset.get_content() == label, 'Preserve custom button body')
            label.remove_from_parent()
            inset.set_content(size)
            size.set_content(overlay)
            fill(overlay.add_child_to_overlay(background))
            row_slot = overlay.add_child_to_overlay(row)
            row_slot.set_padding(u.Margin(24, 9, 24, 9))
            row_slot.set_vertical_alignment(enum(u.VerticalAlignment, 'VAlign_Center'))
            row_slot.set_horizontal_alignment(enum(u.HorizontalAlignment, 'HAlign_Fill'))
            icon_slot = row.add_child_to_horizontal_box(icon_size)
            icon_slot.set_padding(u.Margin(0, 0, 16, 0))
            icon_slot.set_vertical_alignment(enum(u.VerticalAlignment, 'VAlign_Center'))
            icon_size.set_content(icon)
            label_slot = row.add_child_to_horizontal_box(label)
            child_size = u.SlateChildSize()
            child_size.set_editor_property('size_rule', enum(u.SlateSizeRule, 'Fill'))
            child_size.set_editor_property('value', 1.)
            label_slot.set_size(child_size)
            label_slot.set_vertical_alignment(enum(u.VerticalAlignment, 'VAlign_Center'))
        size.set_min_desired_height(72 if primary else 58)
        background.set_brush_from_texture(textures['T_MenuPill'], False)
        image_brush = background.get_editor_property('brush')
        image_brush.set_editor_property('draw_as', enum(u.SlateBrushDrawType, 'Box'))
        image_brush.set_editor_property('margin', u.Margin(.25, .49, .25, .49))
        background.set_brush(image_brush)
        background.set_render_opacity(1. if primary else .72)
        icon_size.set_width_override(26)
        icon_size.set_height_override(26)
        icon.set_brush_from_texture(textures['T_MenuIcon_' + ICONS[index]], False)
        for child in (overlay, row):
            child.set_visibility(enum(u.SlateVisibility, 'SelfHitTestInvisible'))
        for child in (background, icon, label):
            child.set_visibility(enum(u.SlateVisibility, 'HitTestInvisible'))
        label.set_auto_wrap_text(True)
        label.set_editor_property('justification', enum(u.TextJustify, 'Left'))
        button.slot.set_padding(u.Margin(0, 0, 0, 12))
        for obj, token, target, font in ((background, 'Mint' if primary else 'Button', 'Image', 0),
                                         (icon, 'Ink' if primary else 'Cream', 'Image', 0),
                                         (label, 'Ink' if primary else 'Cream', 'Text', 27 if primary else 22)):
            replaced.add(obj.get_name())
            authored.append(style(obj.get_name(), token, target, font, target == 'Text'))
            if isinstance(obj, u.Image):
                obj.set_color_and_opacity(theme.get_color(enum(u.MCUIColorToken, token)))
    for name in ('MenuMain_Text1', 'MenuStatus', 'MenuNavigationHint'):
        obj = widgets.get(name)
        if isinstance(obj, u.TextBlock):
            replaced.add(name)
            obj.set_auto_wrap_text(True)
            obj.set_render_opacity(.72 if name != 'MenuStatus' else 1.)
            authored.append(style(name, 'Cream' if name != 'MenuStatus' else 'Mint', 'Text', 15 if name != 'MenuNavigationHint' else 13))
    defaults.set_editor_property('style_bindings', [s for s in styles if str(s.get_editor_property('widget_name')) not in replaced] + authored)
    defaults.set_editor_property('use_native_layout', False)
    require(defaults.get_editor_property('theme') == theme and dict(defaults.get_editor_property('text_bindings')) == old_text_bindings, 'Theme/localization contract changed')
    require(before_actions == {b.get_name(): str(b.get_editor_property('action')) for b in buttons}, 'Action changed')
    require(all(widgets[n].get_class().get_path_name() == klass for n, klass in before_controls.items()), 'Existing named control replaced')
    require(u.BlueprintEditorLibrary.compile_blueprint(asset) is not False, 'WBP compilation failed')
    require('ERROR' not in str(asset.get_editor_property('status')).upper(), 'WBP has compiler errors')
    current = {w.get_name(): w for w in u.ObjectIterator(u.Widget) if w.get_path_name().startswith(prefix)}
    require(all(n in current and current[n].get_class().get_path_name() == klass for n, klass in before_controls.items()), 'Compiled tree lost an existing control')
    require(before_actions == {name: str(current[name].get_editor_property('action')) for name in before_actions}, 'Compiled action contract changed')
    lib.set_metadata_tag(asset, OWNER_KEY, OWNER)
    require(lib.save_loaded_asset(asset, only_if_is_dirty=False), 'Could not save owned WBP')
    report['changed_packages'].append(TARGET)
    report['sources'] = sources
    report['dirty_after'] = sorted(p.get_path_name() for p in list(u.EditorLoadingAndSavingUtils.get_dirty_content_packages()) + list(u.EditorLoadingAndSavingUtils.get_dirty_map_packages()))
    require(not report['dirty_after'], 'Unexpected unsaved package after scoped save: ' + ', '.join(report['dirty_after']))
    report['validation'] = 'Scoped import, original action/control/text-binding preservation, Blueprint compile/save; actual aspect/culture/hover/action captures pending root review.'
    (out / 'AdaptiveOverlay.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf8')
    u.log('MC_ADAPTIVE_OVERLAY_V2_PENDING_VISUAL_REVIEW ' + str(out))
    return report
