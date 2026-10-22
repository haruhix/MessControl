"""Opt-in viewport/layout repair for the owned AnatomyV2 preview only.

Default is a read-only plan. Never opens a map, starts PIE, resizes a window,
changes renderer/gameplay defaults, activates a theme role, or runs a build.
Camera flags preserve its transform, horizontal reference FOV and post process.
Optional UMG authoring DUPLICATES the reviewed front-end into the preview
namespace; the active WBP_WashNSmashMenu and shared theme remain untouched.

Root executes inside the existing editor after reviewing this file:
  mod = runpy.run_path(PATH)
  mod['run']('plan')
  mod['run']('apply', author_widget=True, output=FRESH_E_DIRECTORY)

MaintainYFOV removes format bars and preserves vertical framing. It cannot by
itself keep an off-centre subject inside a narrow viewport; projection reports
explicitly identify that case. A common composition must pass the 4:3 frame.
"""
from pathlib import Path
import hashlib
import json
import math

ROOT = Path('E:/DEVGAME/MessControl')
BASE = '/Game/UI/MenuShowcase/AnatomyV2'
MAP = BASE + '/L_MenuAnatomyV2'
SOURCE_UI = '/Game/Gameplay/UI/WBP_WashNSmashMenu'
TARGET_UI = BASE + '/UI/WBP_WashNSmashMenu_AdaptiveV2'
OWNER_KEY = 'MC_MenuViewportFitV2'
OWNER_VALUE = 'AdaptivePreview_v2'
REFERENCE_ASPECT = 16.0 / 9.0
CASES = {'16x9': (1920, 1080), '16x10': (1920, 1200),
         '21x9': (2560, 1080), '4x3': (1440, 1080)}


def require(ok, message):
    if not ok:
        raise RuntimeError(message)


def enum_value(kind, canonical):
    normalized = ''.join(c for c in canonical.lower() if c.isalnum())
    matches = [getattr(kind, name) for name in dir(kind) if name.isupper()
               and ''.join(c for c in name.lower() if c.isalnum()) == normalized]
    require(len(matches) == 1, 'Unverified enum: ' + str(kind) + '.' + canonical)
    return matches[0]


def reference_vertical_fov(horizontal_degrees):
    return math.degrees(2 * math.atan(math.tan(math.radians(horizontal_degrees / 2)) / REFERENCE_ASPECT))


def horizontal_fov(horizontal_reference, aspect):
    vertical = math.radians(reference_vertical_fov(horizontal_reference) / 2)
    return math.degrees(2 * math.atan(math.tan(vertical) * aspect))


def projection_case(local_corners, reference_fov, aspect):
    """Camera-local coordinates: X forward, Y right, Z up; pinhole projection."""
    tan_v = math.tan(math.radians(reference_vertical_fov(reference_fov) / 2))
    behind = any(point[0] <= 0.001 for point in local_corners)
    visible = [point for point in local_corners if point[0] > 0.001]
    if behind or not visible:
        return {'pass': False, 'reason': 'Subject bounds cross or lie behind the camera.'}
    uv = [(0.5 + y / (2 * x * tan_v * aspect), 0.5 - z / (2 * x * tan_v))
          for x, y, z in visible]
    bounds = {'min_u': min(p[0] for p in uv), 'max_u': max(p[0] for p in uv),
              'min_v': min(p[1] for p in uv), 'max_v': max(p[1] for p in uv)}
    # Test the actor/brush union, not the intentionally cropped working tooth.
    passed = bounds['min_u'] >= .39 and bounds['max_u'] <= .96 and bounds['min_v'] >= .06 and bounds['max_v'] <= .94
    return {'pass': passed, 'normalized_subject_aabb': bounds,
            'reason': 'Geometric safety only; animation, UI overlap and visual framing still require captures.'}


def run(action='plan', *, output='E:/CodexWork/MessControlMenuAnimation_20261022/AdaptiveApply01',
        author_widget=False):
    import unreal as u
    require(action in ('plan', 'apply'), 'Expected plan or apply')
    require(Path(u.Paths.project_dir()).resolve() == ROOT, 'Wrong project')
    levels = u.get_editor_subsystem(u.LevelEditorSubsystem)
    actors = u.get_editor_subsystem(u.EditorActorSubsystem)
    require(not levels.is_in_play_in_editor(), 'Preserve active PIE')
    world = u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
    require(world.get_path_name() == MAP + '.L_MenuAnatomyV2', 'Open the owned AnatomyV2 preview; this script will not travel')
    dirty = sorted(p.get_path_name() for p in
                   list(u.EditorLoadingAndSavingUtils.get_dirty_content_packages()) +
                   list(u.EditorLoadingAndSavingUtils.get_dirty_map_packages()))
    if action == 'apply':
        require(not dirty, 'Preserve unsaved changes before applying: ' + ', '.join(dirty))
    all_actors = actors.get_all_level_actors()
    cameras = [a for a in all_actors if 'MC_MenuV2_Camera' in [str(t) for t in a.tags]]
    require(len(cameras) == 1 and isinstance(cameras[0], u.CameraActor), 'Expected the single owned menu camera')
    camera = cameras[0]
    component = camera.camera_component
    require(component.get_editor_property('projection_mode') == u.CameraProjectionMode.PERSPECTIVE,
            'Orthographic/custom projection needs its own reviewed framing')
    fov = float(component.get_editor_property('field_of_view'))
    require(math.isfinite(fov) and .01 < fov < 170., 'Invalid perspective reference FOV')
    legacy_y = u.SystemLibrary.get_console_variable_int_value('r.UseLegacyMaintainYFOVViewMatrix')
    require(legacy_y == 0, 'Legacy MaintainYFOV is enabled; preserve the process setting and review projection separately')
    before = {key: str(component.get_editor_property(key)) for key in
              ('constrain_aspect_ratio', 'aspect_ratio', 'override_aspect_ratio_axis_constraint', 'aspect_ratio_axis_constraint')}
    camera_transform = camera.get_actor_transform()
    def actor_pose(actor):
        loc, rot, scale = actor.get_actor_location(), actor.get_actor_rotation(), actor.get_actor_scale3d()
        return [loc.x, loc.y, loc.z, rot.pitch, rot.yaw, rot.roll, scale.x, scale.y, scale.z]
    pose_before = actor_pose(camera)
    local_corners = []
    subjects = []
    for actor in all_actors:
        if not {'MC_MenuV2_Hero', 'MC_MenuV2_Brush'}.intersection(str(t) for t in actor.tags):
            continue
        origin, extent = actor.get_actor_bounds(False)
        subjects.append(actor.get_path_name())
        for x in (-1, 1):
            for y in (-1, 1):
                for z in (-1, 1):
                    point = u.MathLibrary.inverse_transform_location(camera_transform, origin + u.Vector(extent.x*x, extent.y*y, extent.z*z))
                    local_corners.append((point.x, point.y, point.z))
    require(len(subjects) == 2, 'Expected one hero and one brush for a meaningful framing report')
    report = {'action': action, 'map': MAP, 'camera': camera.get_path_name(), 'camera_before': before,
              'reference_horizontal_fov': fov, 'vertical_fov_after': reference_vertical_fov(fov),
              'legacy_maintain_y_fov': legacy_y,
              'subject_bounds_source': subjects, 'dirty_before': dirty, 'cases': {}, 'backups': [],
              'changed_packages': [], 'ui_activated': False,
              'limits': ['No live execution was performed while preparing this script.',
                         'Current actor bounds do not prove all animated hand/brush poses fit.',
                         'Fixed vertical FOV is adaptive projection, not adaptive composition.',
                         'UMG clone is not activated in the shared theme or gameplay HUD.',
                         'Settings/Join subpages retain their existing controls and require separate width/overflow review.']}
    for name, (width, height) in CASES.items():
        case = projection_case(local_corners, fov, width / height)
        case.update({'resolution': [width, height], 'horizontal_fov': horizontal_fov(fov, width / height)})
        report['cases'][name] = case
    if action == 'plan':
        u.log(json.dumps(report, indent=2))
        return report
    out = Path(output).resolve()
    require(out.drive.upper() == 'E:' and not out.exists(), 'Choose a fresh output directory on E:')
    out.mkdir(parents=True)
    lib = u.EditorAssetLibrary

    def backup(package):
        extension = '.umap' if package == MAP else '.uasset'
        source = ROOT / 'Content' / (package.removeprefix('/Game/') + extension)
        if not source.exists():
            return
        target = out / 'Before' / source.relative_to(ROOT)
        target.parent.mkdir(parents=True, exist_ok=True)
        data = source.read_bytes()
        target.write_bytes(data)
        require(hashlib.sha256(target.read_bytes()).digest() == hashlib.sha256(data).digest(), 'Backup mismatch')
        report['backups'].append({'source': str(source), 'backup': str(target), 'sha256': hashlib.sha256(data).hexdigest()})

    backup(MAP)
    camera.modify()
    component.modify()
    component.set_editor_property('constrain_aspect_ratio', False)
    component.set_editor_property('aspect_ratio', REFERENCE_ASPECT)
    component.set_editor_property('override_aspect_ratio_axis_constraint', True)
    component.set_editor_property('aspect_ratio_axis_constraint',
                                  enum_value(u.AspectRatioAxisConstraint, 'AspectRatio_MaintainYFOV'))
    require(not component.get_editor_property('constrain_aspect_ratio'), 'Constrain aspect flag readback failed')
    require(component.get_editor_property('override_aspect_ratio_axis_constraint'), 'Constraint override readback failed')
    require(max(abs(a-b) for a,b in zip(actor_pose(camera),pose_before)) < .0001, 'Camera transform changed unexpectedly')
    require(abs(float(component.get_editor_property('field_of_view')) - fov) < .0001, 'Reference FOV changed unexpectedly')
    if author_widget:
        _author_widget(u, lib, out, report, backup)
    require(levels.save_current_level(), 'Could not save the owned preview map')
    report['changed_packages'].append(MAP)
    report['camera_after'] = {key: str(component.get_editor_property(key)) for key in before}
    report['validation'] = 'Property readback, projection math, scoped save; native captures at all four formats and runtime actions are pending.'
    (out / 'AdaptiveViewport.json').write_text(json.dumps(report, indent=2), encoding='utf8')
    u.log('MC_MENU_VIEWPORT_FIT_V2_PENDING_VISUAL_REVIEW ' + str(out))
    return report


def _author_widget(u, lib, out, report, backup):
    source = u.load_asset(SOURCE_UI)
    require(source and lib.get_metadata_tag(source, 'MC_WashNSmashMenu') == 'EditableNativeMenu_v1',
            'Source WBP ownership is not the reviewed Wash n Smash menu')
    existing = u.load_asset(TARGET_UI) if lib.does_asset_exist(TARGET_UI) else None
    require(not existing or lib.get_metadata_tag(existing, OWNER_KEY) == OWNER_VALUE, 'Preserve an unrelated target WBP')
    backup(TARGET_UI)
    if not existing:
        lib.make_directory(BASE + '/UI')
    asset = existing or lib.duplicate_asset(SOURCE_UI, TARGET_UI)
    require(asset, 'Preview menu duplicate failed')
    prefix = TARGET_UI + '.WBP_WashNSmashMenu_AdaptiveV2:WidgetTree.'
    widgets = {obj.get_name(): obj for obj in u.ObjectIterator(u.Widget) if obj.get_path_name().startswith(prefix)}
    required = ['MenuBackdrop', 'MenuCard', 'MenuTitle', 'MenuPages']
    require(all(name in widgets for name in required), 'Named native action/layout contract missing')
    backdrop, card = widgets['MenuBackdrop'], widgets['MenuCard']
    size = card.get_content()
    require(isinstance(backdrop, u.Border) and isinstance(card, u.Border) and isinstance(size, u.SizeBox), 'Unexpected custom source layout')
    scale = card.get_parent()
    require(isinstance(scale, u.ScaleBox) and isinstance(card.slot, u.ScaleBoxSlot), 'Expected existing ScaleBox -> MenuCard contract')
    asset.modify()
    # The parent must fill the viewport for percentage Canvas anchors to work.
    backdrop.set_padding(u.Margin(0.))
    backdrop.set_horizontal_alignment(enum_value(u.HorizontalAlignment, 'HAlign_Fill'))
    backdrop.set_vertical_alignment(enum_value(u.VerticalAlignment, 'VAlign_Fill'))
    canvas = widgets.get('MenuAdaptiveCanvasV2')
    if canvas is None:
        require(backdrop.get_content() == scale, 'Unexpected root content; preserve it')
        tree = scale.get_outer()
        canvas = u.new_object(u.CanvasPanel, outer=tree, name='MenuAdaptiveCanvasV2')
        scale.remove_from_parent()
        backdrop.set_content(canvas)
        slot = canvas.add_child_to_canvas(scale)
    else:
        require(isinstance(canvas, u.CanvasPanel) and backdrop.get_content() == canvas and scale.get_parent() == canvas, 'Owned adaptive wrapper drifted')
        slot = scale.slot
    require(isinstance(slot, u.CanvasPanelSlot), 'Expected adaptive canvas slot')
    anchors = u.Anchors()
    anchors.set_editor_property('minimum', u.Vector2D(0., 0.))
    anchors.set_editor_property('maximum', u.Vector2D(.36, 1.))
    slot.set_anchors(anchors)
    slot.set_offsets(u.Margin(40., 32., 20., 32.))
    slot.set_alignment(u.Vector2D(0., 0.))
    slot.set_auto_size(False)
    scale.set_stretch(enum_value(u.Stretch, 'ScaleToFit'))
    scale.set_stretch_direction(enum_value(u.StretchDirection, 'DownOnly'))
    card.slot.set_horizontal_alignment(enum_value(u.HorizontalAlignment, 'HAlign_Left'))
    card.slot.set_vertical_alignment(enum_value(u.VerticalAlignment, 'VAlign_Center'))
    size.set_width_override(440.)
    size.set_height_override(800.)
    title = widgets['MenuTitle']
    require(isinstance(title, u.TextBlock), 'Title is not the reviewed text control')
    title.set_auto_wrap_text(True)
    title.set_editor_property('wrap_text_at', 0.)
    cls = u.load_class(None, TARGET_UI + '.WBP_WashNSmashMenu_AdaptiveV2_C')
    defaults = u.get_default_object(cls)
    require(isinstance(defaults, u.MCMainMenuWidget), 'Native action adapter parent changed')
    defaults.modify()
    defaults.set_editor_property('use_native_layout', False)
    styles = list(defaults.get_editor_property('style_bindings'))
    font_sizes = {'MenuTitle': 60, 'MenuMain_Action1Label': 28, 'MenuStatus': 15,
                  'MenuNavigationHint': 14, 'MenuMain_Text1': 16}
    for index in (0, 2, 3, 4, 5):
        font_sizes['MenuMain_Action' + str(index) + 'Label'] = 22
    for binding in styles:
        name = str(binding.get_editor_property('widget_name'))
        if name in font_sizes:
            binding.set_editor_property('font_size', font_sizes[name])
    defaults.set_editor_property('style_bindings', styles)
    for index in range(6):
        caption = widgets.get('MenuMain_Action' + str(index) + 'Label')
        if isinstance(caption, u.TextBlock):
            caption.set_auto_wrap_text(True)
    lib.set_metadata_tag(asset, OWNER_KEY, OWNER_VALUE)
    require(u.BlueprintEditorLibrary.compile_blueprint(asset) is not False, 'Preview WBP compile failed')
    require('ERROR' not in str(asset.get_editor_property('status')).upper(), 'Preview WBP has compiler errors')
    require(lib.save_loaded_asset(asset, only_if_is_dirty=False), 'Could not save owned preview WBP')
    # Every original named control remains the same class after reparenting.
    current = {obj.get_name(): obj.get_class().get_path_name() for obj in u.ObjectIterator(u.Widget)
               if obj.get_path_name().startswith(prefix)}
    require(all(current.get(name) == obj.get_class().get_path_name() for name,obj in widgets.items()),
            'Existing named control contract changed')
    report['changed_packages'].append(TARGET_UI)
    report['preview_widget_class'] = TARGET_UI + '.WBP_WashNSmashMenu_AdaptiveV2_C'
    report['ui_layout'] = {'left_host_fraction': .36, 'host_insets_dp': [40, 32, 20, 32],
                           'content_size_dp': [440, 800], 'scale': 'ScaleToFit/DownOnly', 'fonts_dp': font_sizes,
                           'activation': 'None. Root must review native composite/actions before promotion.'}
