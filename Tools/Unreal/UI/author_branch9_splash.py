"""Author the approved Branch9 art, UI reveal material and editable splash WBP.

Run explicitly inside the idle, saved MessControl Editor after its approved
native build. Does not start Play, open/close Editor, build, cook, package,
access a microphone or change shared localization. Existing artist trees,
timings, nonempty art overrides and a custom StudioSplash role are preserved.
All backup packages, preview PNGs and reports are written to a fresh E: folder.
"""
from pathlib import Path
import hashlib
import json
import re

import unreal as u

ROOT = Path(u.Paths.project_dir()).resolve()
SOURCE = ROOT / 'ArtSource/Brand/Branch9'
OUT = Path(globals().get('MC_BRANCH9_AUTHOR_OUTPUT',
    'E:/CodexWork/MessControlFrontEndBranch9_20261021/Authoring')).resolve()
BASE = '/Game/Gameplay/UI'
ART = BASE + '/Brand/Branch9'
MATERIAL = BASE + '/Materials/M_Branch9_Reveal'
WIDGET = BASE + '/WBP_StudioSplash'
THEME = BASE + '/Theme/DA_GameplayUITheme'
SNAPSHOTS = BASE + '/Materials/DesignerSnapshots/WBP_StudioSplash'
NAMES = ('T_Branch9_Wordmark', 'T_Branch9_Branch', 'T_Branch9_Leaves')
TIMES = (0.0, 0.15, 0.35, 0.6, 1.0, 2.2, 2.7, 3.0)
OWNER = 'MessControl.Branch9.Generated'
VERSION = '2'
lib = u.EditorAssetLibrary
edit = u.MaterialEditingLibrary
assets = u.AssetToolsHelpers.get_asset_tools()
tools = u.MCGameplayUIEditorLibrary


def require(ok, message):
    if not ok:
        raise RuntimeError(message)


def dirty():
    return list(u.EditorLoadingAndSavingUtils.get_dirty_content_packages()) + list(
        u.EditorLoadingAndSavingUtils.get_dirty_map_packages())


def write_report():
    (OUT / 'Authoring.json').write_text(json.dumps(report, ensure_ascii=False,
        indent=2), encoding='utf-8')


def disk_file(package):
    require(package.startswith('/Game/'), 'Expected project package: ' + package)
    return ROOT / 'Content' / (package.removeprefix('/Game/') + '.uasset')


def backup(package):
    if package in backed_up:
        return
    backed_up.add(package)
    path = disk_file(package)
    if not path.is_file():
        return
    data = path.read_bytes()
    output = OUT / 'BeforePackages' / path.relative_to(ROOT / 'Content')
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(data)
    require(hashlib.sha256(output.read_bytes()).hexdigest() ==
        hashlib.sha256(data).hexdigest(), 'Backup copy verification failed')
    report['backups'].append(dict(package=package, file=str(output),
        sha256=hashlib.sha256(data).hexdigest()))


def save(asset, label):
    require(lib.save_loaded_asset(asset, only_if_is_dirty=False),
        'Could not save owned package: ' + label)


def path_of(value):
    if not value:
        return ''
    if hasattr(value, 'get_path_name'):
        return value.get_path_name()
    match = re.search(r'/Game/[^\s\'"<>]+', str(value))
    return match.group(0).rstrip(')') if match else ''


require(ROOT.drive.upper() == 'E:' and OUT.drive.upper() == 'E:',
    'Project and task output must stay on drive E:')
require(ROOT == Path('E:/DEVGAME/MessControl').resolve(),
    'Run only in the intended MessControl checkout')
require(not OUT.exists() or not any(OUT.iterdir()),
    'Choose a fresh MC_BRANCH9_AUTHOR_OUTPUT; preserve earlier evidence')
require(not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor(),
    'Stop Play before authoring')
require(not dirty(), 'Preserve unsaved editor work before authoring')
require(hasattr(u, 'MCStudioSplashWidget') and hasattr(tools,
    'create_editable_gameplay_widget'), 'Build the new native Editor classes first')
require(all((SOURCE / (name + '.png')).is_file() for name in NAMES),
    'Approved core-three source PNGs are missing')
OUT.mkdir(parents=True, exist_ok=True)
report = dict(scope=__doc__, succeeded=False, source_revision=2,
    backups=[], created=[], preserved=[], textures=[], material={}, widget={},
    role={}, previews=[], snapshots=[], validation_scope=
    'Texture import/settings, real material compiler errors, Blueprint compile/save '
    'and eight deterministic actual-UMG preview frames. Runtime/first-launch checks '
    'are separate; this is not a gameplay or network check.')
backed_up = set()


def import_core_texture(name):
    package = ART + '/' + name
    source = SOURCE / (name + '.png')
    existed = lib.does_asset_exist(package)
    if existed:
        texture = u.load_asset(package)
        require(isinstance(texture, u.Texture2D),
            'Unrelated existing asset preserved: ' + package)
        # A rerun must not silently replace an artist's revised imported image.
        report['preserved'].append(package)
    else:
        task = u.AssetImportTask()
        task.set_editor_properties(dict(filename=str(source),
            destination_path=ART, destination_name=name, automated=True,
            replace_existing=False, save=False))
        assets.import_asset_tasks([task])
        texture = u.load_asset(package)
        require(isinstance(texture, u.Texture2D), 'Texture import failed: ' + package)
        lib.set_metadata_tag(texture, OWNER, VERSION)
        lib.set_metadata_tag(texture, 'MessControl.Branch9.SourceSHA256',
            hashlib.sha256(source.read_bytes()).hexdigest())
        report['created'].append(package)
    texture.modify()
    texture.set_editor_properties(dict(
        compression_settings=u.TextureCompressionSettings.TC_EDITOR_ICON,
        lod_group=u.TextureGroup.TEXTUREGROUP_UI,
        mip_gen_settings=u.TextureMipGenSettings.TMGS_NO_MIPMAPS,
        never_stream=True, srgb=True,
        address_x=u.TextureAddress.TA_CLAMP,
        address_y=u.TextureAddress.TA_CLAMP))
    # Saving finishes this imported texture's async platform build. Querying
    # GetSize immediately after PostEditChange can return the temporary default
    # stand-in dimensions, although the source is already 2048x768.
    save(texture, package)
    width, height = texture.blueprint_get_size_x(), texture.blueprint_get_size_y()
    require((width, height) == (2048, 768),
        'Preserved/imported logo layer must have the common 2048x768 canvas: ' + package)
    save(texture, package)
    report['textures'].append(dict(path=texture.get_path_name(), created=not existed,
        dimensions=[width, height], compression=str(texture.get_editor_property(
            'compression_settings')), group=str(texture.get_editor_property('lod_group')),
        mipmaps=str(texture.get_editor_property('mip_gen_settings')),
        never_stream=texture.get_editor_property('never_stream'),
        source_sha256=hashlib.sha256(source.read_bytes()).hexdigest()))
    require(not dirty(), 'Texture authoring left an unexpected dirty package')
    return texture


def make_reveal_material(default_texture):
    existed = lib.does_asset_exist(MATERIAL)
    if existed:
        material = u.load_asset(MATERIAL)
        require(isinstance(material, u.Material),
            'Unrelated existing material preserved: ' + MATERIAL)
        require(material.get_editor_property('material_domain') == u.MaterialDomain.MD_UI,
            'Existing non-UI material preserved: ' + MATERIAL)
        require(material.get_editor_property('blend_mode') == u.BlendMode.BLEND_TRANSLUCENT,
            'Existing material blend mode preserved; inspect before using: ' + MATERIAL)
        params = {str(n.get_editor_property('parameter_name')) for n in
            edit.get_material_expressions(material) if isinstance(n, (
                u.MaterialExpressionScalarParameter, u.MaterialExpressionVectorParameter,
                u.MaterialExpressionTextureObjectParameter))}
        require({'LogoTexture', 'Reveal', 'Softness', 'Opacity', 'LogoColor'} <= params,
            'Existing custom graph lacks the required native parameter contract; preserved')
        report['preserved'].append(MATERIAL)
    else:
        material = assets.create_asset('M_Branch9_Reveal', BASE + '/Materials',
            u.Material, u.MaterialFactoryNew())
        require(material, 'Could not create the reveal material')
        material.set_editor_property('material_domain', u.MaterialDomain.MD_UI)
        material.set_editor_property('blend_mode', u.BlendMode.BLEND_TRANSLUCENT)

        def node(cls, x, y, **properties):
            value = edit.create_material_expression(material, cls, x, y)
            require(value, 'Could not create ' + str(cls))
            if properties:
                value.set_editor_properties(properties)
            return value

        def wire(a, b, pin='', output=''):
            require(edit.connect_material_expressions(a, output, b, pin),
                'Material connection failed: ' + str(a) + ' -> ' + str(b) + '/' + pin)

        def scalar(name, value, x, y):
            return node(u.MaterialExpressionScalarParameter, x, y,
                parameter_name=name, default_value=value)

        uv = node(u.MaterialExpressionTextureCoordinate, -1250, -100)
        uv_x = node(u.MaterialExpressionComponentMask, -1000, -100,
            r=True, g=False, b=False, a=False)
        texture_object = node(u.MaterialExpressionTextureObjectParameter, -1250, 200,
            parameter_name='LogoTexture', texture=default_texture,
            sampler_type=u.MaterialSamplerType.SAMPLERTYPE_COLOR)
        sample = node(u.MaterialExpressionTextureSample, -950, 200,
            texture=default_texture, sampler_type=u.MaterialSamplerType.SAMPLERTYPE_COLOR)
        reveal = scalar('Reveal', 1.0, -1250, -600)
        softness = scalar('Softness', 0.0354166667, -1250, -400)
        opacity = scalar('Opacity', 1.0, -200, 400)
        color = node(u.MaterialExpressionVectorParameter, -200, -400,
            parameter_name='LogoColor', default_value=u.LinearColor(1, 1, 1, 1))
        # edge = -Softness + (1+2*Softness)*Reveal. Native values remain
        # bounded and positive; no UV distortion or additional texture lookup.
        twice_soft = node(u.MaterialExpressionMultiply, -1000, -500, const_b=2.0)
        extended = node(u.MaterialExpressionAdd, -800, -500, const_b=1.0)
        distance = node(u.MaterialExpressionMultiply, -600, -500)
        edge = node(u.MaterialExpressionSubtract, -400, -500)
        ahead = node(u.MaterialExpressionSubtract, -200, -100)
        soft_step = node(u.MaterialExpressionSmoothStep, 40, -100, const_min=0.0)
        shape_alpha = node(u.MaterialExpressionMultiply, 260, 100)
        final_alpha = node(u.MaterialExpressionMultiply, 480, 100)
        wire(uv, uv_x)
        wire(uv, sample, 'UVs')
        wire(texture_object, sample, 'Tex')
        wire(softness, twice_soft, 'A')
        wire(twice_soft, extended, 'A')
        wire(extended, distance, 'A')
        wire(reveal, distance, 'B')
        wire(distance, edge, 'A')
        wire(softness, edge, 'B')
        wire(edge, ahead, 'A')
        wire(uv_x, ahead, 'B')
        wire(softness, soft_step, 'Max')
        wire(ahead, soft_step, 'Value')
        wire(soft_step, shape_alpha, 'A')
        wire(sample, shape_alpha, 'B', 'A')
        wire(shape_alpha, final_alpha, 'A')
        wire(opacity, final_alpha, 'B')
        require(edit.connect_material_property(color, '',
            u.MaterialProperty.MP_EMISSIVE_COLOR), 'LogoColor output failed')
        require(edit.connect_material_property(final_alpha, '',
            u.MaterialProperty.MP_OPACITY), 'Opacity output failed')
        lib.set_metadata_tag(material, OWNER, VERSION)
        report['created'].append(MATERIAL)
    # UE 5.8 returns the actual material compiler error array, not a bool.
    errors = [str(error) for error in edit.recompile_material(material)]
    report['material'] = dict(path=material.get_path_name(), created=not existed,
        compile_errors=errors, expression_count=len(edit.get_material_expressions(material)),
        texture_samples=1, intended_logo_quads=3,
        formula='smoothstep(0,Softness,(-Softness+(1+2*Softness)*Reveal)-UV.x)*LogoTexture.a*Opacity')
    require(not errors, 'Material compiler reported: ' + '; '.join(errors))
    save(material, MATERIAL)
    require(not dirty(), 'Material authoring left an unexpected dirty package')
    return material


def make_widget(textures, material):
    existed = lib.does_asset_exist(WIDGET)
    native = u.load_class(None, '/Script/MessControl.MCStudioSplashWidget')
    require(native, 'Native MCStudioSplashWidget is not loaded')
    widget = tools.create_editable_gameplay_widget(WIDGET, native)
    require(widget, 'Could not create/preserve the editable splash')
    cls = u.load_class(None, WIDGET + '.WBP_StudioSplash_C')
    require(cls, 'Generated splash class is missing')
    defaults = u.get_default_object(cls)
    require(isinstance(defaults, u.MCStudioSplashWidget), 'Wrong widget parent preserved')
    widget.modify()
    defaults.modify()
    # Persist hard references for packaged/cooked dependency discovery.
    # Never overwrite a nonempty artist override or an existing timing curve.
    fills, preserved = {}, {}
    desired = dict(wordmark_texture=textures[0], branch_texture=textures[1],
        leaves_texture=textures[2], reveal_material=material)
    for name, value in desired.items():
        current = defaults.get_editor_property(name)
        if current:
            preserved[name] = path_of(current)
        else:
            defaults.set_editor_property(name, value)
            fills[name] = path_of(value)
    if not existed:
        defaults.set_editor_properties(dict(duration=3.0, use_native_timeline=True,
            word_reveal_start=.10, word_reveal_end=.52,
            branch_reveal_start=.29, branch_reveal_end=.65,
            leaves_reveal_start=.43, leaves_reveal_end=.73, fade_out_seconds=.37))
        lib.set_metadata_tag(widget, OWNER, VERSION)
    u.BlueprintEditorLibrary.compile_blueprint(widget)
    status = str(widget.get_editor_property('status'))
    require('ERROR' not in status.upper(), 'Splash Blueprint compile failed: ' + status)
    save(widget, WIDGET)
    # Compilation can reinstance the CDO; verify the saved generated class again.
    cls = u.load_class(None, WIDGET + '.WBP_StudioSplash_C')
    defaults = u.get_default_object(cls)
    hard_refs = {name: path_of(defaults.get_editor_property(name)) for name in desired}
    require(all(hard_refs.values()), 'Saved splash CDO lacks a hard art/material reference')
    report['widget'] = dict(path=widget.get_path_name(), created=not existed,
        compile_status=status, filled_art=fills, preserved_art=preserved,
        hard_references=hard_refs, existing_timings_preserved=existed,
        duration=float(defaults.get_editor_property('duration')))
    report['preserved' if existed else 'created'].append(WIDGET)
    require(not dirty(), 'Widget authoring left an unexpected dirty package')
    return cls


try:
    for package in [ART + '/' + name for name in NAMES] + [MATERIAL, WIDGET, THEME]:
        backup(package)
    old_snapshots = list(lib.list_assets(SNAPSHOTS, recursive=True, include_folder=False))
    for object_path in old_snapshots:
        backup(str(object_path).split('.')[0])
    require(lib.does_asset_exist(WIDGET) or not old_snapshots,
        'Orphaned prior snapshots preserved; inspect before creating a new WBP')
    textures = [import_core_texture(name) for name in NAMES]
    material = make_reveal_material(textures[0])
    cls = make_widget(textures, material)
    theme = u.load_asset(THEME)
    require(theme, 'Existing shared UI theme is missing')
    mapping = dict(theme.get_editor_property('widget_classes'))
    role_key = next((key for key in mapping if str(key).lower() == 'studiosplash'), None)
    selected = path_of(mapping.get(role_key)) if role_key is not None else ''
    if selected:
        custom = u.load_class(None, selected)
        require(custom and isinstance(u.get_default_object(custom), u.MCStudioSplashWidget),
            'Custom StudioSplash role is incompatible; preserved: ' + selected)
        report['role'] = dict(path=selected, preserved=True)
    else:
        theme.modify()
        mapping[role_key if role_key is not None else 'StudioSplash'] = cls
        theme.set_editor_property('widget_classes', mapping)
        theme.notify_theme_changed()
        save(theme, THEME)
        report['role'] = dict(path=cls.get_path_name(), preserved=False)
    require(not dirty(), 'Theme registration left an unexpected dirty package')
    report['snapshots'] = [str(item) for item in lib.list_assets(SNAPSHOTS,
        recursive=True, include_folder=False)]
    for index, time in enumerate(TIMES):
        output = OUT / 'Previews' / f'Branch9_{index:02d}_{time:04.2f}s.png'
        require(tools.render_gameplay_ui_preview(cls, str(output),
            u.IntPoint(1440, 900), 'Default', 'en', float(time)),
            'Actual UMG preview failed at ' + str(time))
        require(output.is_file() and output.stat().st_size > 100,
            'Preview did not produce a PNG: ' + str(output))
        report['previews'].append(dict(time=time, path=str(output),
            dimensions=[1440, 900], sha256=hashlib.sha256(output.read_bytes()).hexdigest(),
            kind='Actual native/Designer UMG frame, not a compositing mockup'))
    report['dirty_after'] = [str(package) for package in dirty()]
    require(not report['dirty_after'], 'Unexpected dirty packages preserved after previews')
    report['succeeded'] = True
    write_report()
    u.log('MC_BRANCH9_AUTHOR_COMPLETE ' + str(OUT / 'Authoring.json'))
except Exception as error:
    report['error'] = str(error)
    report['dirty_after'] = [str(package) for package in dirty()]
    write_report()
    # Do not silently reload packages or replace the user's live objects. The
    # verified before-packages remain on E: for deliberate recovery if needed.
    raise
