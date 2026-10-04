"""Author brush foam sprites, then refine the existing lightweight Niagara asset.

Run in a stopped editor through remote_python.py, or the full editor's Python
commandlet with -AllowCommandletRendering. This script does not start Unreal.
Backups stay outside Content; unsaved target packages are never overwritten.
"""
import hashlib
import json
import shutil
from datetime import datetime, timezone
from pathlib import Path

import unreal as u


FOAM = '/Game/Gameplay/Care/M_BrushFoam'
BUBBLE = '/Game/Gameplay/Care/M_BrushBubble'
SYSTEM = '/Game/Gameplay/VFX/NS_BrushFoam'
TARGETS = (FOAM, BUBBLE, SYSTEM)
edit, lib = u.MaterialEditingLibrary, u.EditorAssetLibrary
headless = '-run=pythonscript' in u.SystemLibrary.get_command_line().lower()
if not headless:
    level_editor = u.get_editor_subsystem(u.LevelEditorSubsystem)
    assert not (level_editor and level_editor.is_in_play_in_editor()), 'Stop PIE before foam authoring'
dirty = {p.get_path_name() for p in u.EditorLoadingAndSavingUtils.get_dirty_content_packages()}
assert not dirty.intersection(TARGETS), 'Preserve unsaved target edits: ' + str(dirty.intersection(TARGETS))
assert lib.does_asset_exist(FOAM), 'The existing brush foam material is required'
assert lib.does_asset_exist(SYSTEM), 'The existing brush foam Niagara asset is required'
assert callable(getattr(u.MCVFXAssetBuilder, 'refine_brush_foam', None)), 'Build the editor with RefineBrushFoam first'

root, saved = Path(u.Paths.project_dir()).resolve(), Path(u.Paths.project_saved_dir()).resolve()
backup = saved / 'FoamReview' / 'MaterialBackups' / datetime.now(timezone.utc).strftime('%Y%m%d_%H%M%S_%f')
backup.mkdir(parents=True)
report = {
    'backup': str(backup), 'original_packages': {}, 'materials': {},
    'design': 'Gameplay-camera pearly white foam clumps, pale cyan contour and broad iridescent bubble arcs',
    'age': 'ParticleColor alpha supplies the only lifetime fade; no material life multiplier',
    'performance': 'Two sprite emitters, no texture assets, scene-color lookup, refraction, collision, or extra material layer',
}
for path in TARGETS:
    if not lib.does_asset_exist(path):
        report['original_packages'][path] = {'existed': False}
        continue
    source = root / 'Content' / Path(path.removeprefix('/Game/') + '.uasset')
    assert source.is_file(), 'Save the target package before authoring: ' + str(source)
    destination = backup / source.name
    shutil.copy2(source, destination)
    report['original_packages'][path] = {
        'existed': True, 'backup_file': str(destination),
        'sha256': hashlib.sha256(source.read_bytes()).hexdigest(),
    }


FOAM_CODE = r'''
float2 p = (UV - .5) * 2;
// Broad pearly lobes retain their silhouettes at the ordinary gameplay view.
float2 a = p - float2(-.26, .12);
float2 b = p - float2(.29, .09);
float2 c = p - float2(.01, -.34);
float da = length(a) - .63;
float db = length(b) - .57;
float dc = length(c) - .54;
float d = min(da, min(db, dc));
float aa = max(fwidth(d) * 1.15, .012);
float coverage = 1 - smoothstep(-aa, aa, d);
float2 v = da <= db && da <= dc ? a/.63 : db <= dc ? b/.57 : c/.54;
float z = sqrt(saturate(1 - dot(v,v)));
float3 n = normalize(float3(v * .82, max(z,.03)));
float light = saturate(dot(n, float3(-.36,-.43,.825)));
float shade = saturate(.38 + light*.50 + z*.12);
float3 color = lerp(float3(.62,.83,.90), float3(.97,.995,1), shade);
float contour = 1 - smoothstep(.025,.12,-d);
color = lerp(color,float3(.44,.88,.97),contour*.65);
float2 glintUV = v - float2(-.32,-.34);
float glint = pow(saturate(1 - dot(glintUV,glintUV)*11), 5);
color = lerp(color, float3(1.08,1.10,1.12), glint*.80);
float alpha = coverage * lerp(.86,.95,z) * saturate(Density) * saturate(Particle.a);
return float4(color * saturate(Particle.rgb), alpha);
'''

BUBBLE_CODE = r'''
float2 p = (UV - .5) * 2;
float r = length(p);
float aa = max(fwidth(r) * 1.1, .006);
float disk = 1 - smoothstep(.89-aa,.89+aa,r);
float rim = 1 - smoothstep(.105-aa,.105+aa,abs(r-.775));
float2 direction = p/max(r,.001);
float upper = pow(saturate(dot(direction,float2(-.40,-.9165))),5) * rim;
float lower = pow(saturate(dot(direction,float2(.70,.714))),7) * rim;
// Broad pink/cyan/gold film arcs stay distinct without scene-color refraction.
float tint = saturate(.5 + direction.x*.36 + direction.y*.18);
float3 film = lerp(float3(.48,.91,1),float3(.96,.69,.93),tint);
film = lerp(film,float3(1,.92,.63),lower*.35);
float shade = saturate(.68 + dot(direction,float2(-.40,-.9165))*.22);
float3 color = film * shade;
float2 g0 = p - float2(-.37,-.62), g1 = p - float2(.49,.54);
float glint0 = pow(saturate(1-dot(g0,g0)*75),3);
float glint1 = pow(saturate(1-dot(g1,g1)*110),3);
color = lerp(color,float3(1.10,1.13,1.16),saturate(upper*.75+lower*.4+glint0+glint1));
float alpha = disk * saturate(.035+rim*.62+upper*.24+lower*.17+glint0*.8+glint1*.7)
    * saturate(Density) * saturate(Particle.a);
return float4(color * saturate(Particle.rgb), alpha);
'''


def expression(mat, cls, x, y, **properties):
    node = edit.create_material_expression(mat, cls, x, y)
    assert node, cls
    node.set_editor_properties(properties)
    return node


def connect(source, output, target, input_name):
    assert edit.connect_material_expressions(source, output, target, input_name), input_name


def scalar(mat, name, value, y):
    return expression(mat, u.MaterialExpressionScalarParameter, -650, y,
                      parameter_name=name, default_value=value, group='Brush foam optics')


def usage(mat):
    return {
        'niagara_sprites': bool(edit.has_material_usage(mat, u.MaterialUsage.MATUSAGE_NIAGARA_SPRITES)),
        'niagara_mesh_particles': bool(edit.has_material_usage(mat, u.MaterialUsage.MATUSAGE_NIAGARA_MESH_PARTICLES)),
    }


def statistics(mat):
    try:
        stats = edit.get_statistics(mat)
        names = ('num_pixel_shader_instructions', 'num_vertex_shader_instructions',
                 'num_samplers', 'num_pixel_texture_samples', 'num_vertex_texture_samples')
        return {name: int(stats.get_editor_property(name)) for name in names}
    except Exception as error:
        # Compiler counts can be unavailable for a headless RHI. Compilation
        # errors are checked independently and always fail the authoring run.
        return {'unavailable': str(error)}


def update_existing_graph(mat, code, density):
    nodes = list(edit.get_material_expressions(mat))
    if not nodes:
        return False
    is_foam = mat.get_path_name().startswith(FOAM + '.')
    description = 'Brush foam lobes' if is_foam else 'Transparent brush bubble film'
    fields = [n for n in nodes if isinstance(n, u.MaterialExpressionCustom)
              and str(n.get_editor_property('description')) == description]
    opacity_name = 'FoamOpacity' if is_foam else 'BubbleOpacity'
    opacity = [n for n in nodes if isinstance(n, u.MaterialExpressionScalarParameter)
               and str(n.get_editor_property('parameter_name')).lower() == opacity_name.lower()]
    distances = [n for n in nodes if isinstance(n, u.MaterialExpressionScalarParameter)
                 and str(n.get_editor_property('parameter_name')).lower() == 'softintersectioncm']
    fades = [n for n in nodes if isinstance(n, u.MaterialExpressionDepthFade)]
    counts = {'custom': len(fields), 'opacity': len(opacity), 'distance': len(distances), 'depth_fade': len(fades)}
    assert all(counts.values()), 'Unexpected saved foam graph: ' + mat.get_path_name() + ' ' + str(counts)
    # Keep every input, connection and material usage. Updating existing nodes
    # avoids recompiling intermediate graphs. Earlier authoring runs left
    # duplicate named nodes: update all matching nodes, including the live ones.
    for field in fields:
        if field.get_editor_property('code') != code:
            field.set_editor_property('code', code)
    for node in opacity:
        if float(node.get_editor_property('default_value')) != density:
            node.set_editor_property('default_value', density)
    for node in distances:
        if float(node.get_editor_property('default_value')) != 1.5:
            node.set_editor_property('default_value', 1.5)
    for node in fades:
        if float(node.get_editor_property('fade_distance_default')) != 1.5:
            node.set_editor_property('fade_distance_default', 1.5)
    u.log('MC_FOAM_GRAPH_REUSED ' + json.dumps({'path': mat.get_path_name(), 'matched_nodes': counts}))
    return True


def author_material(path, code, density):
    mat = lib.load_asset(path) if lib.does_asset_exist(path) else None
    if mat is None:
        folder, name = path.rsplit('/', 1)
        mat = u.AssetToolsHelpers.get_asset_tools().create_asset(name, folder, u.Material, u.MaterialFactoryNew())
    assert isinstance(mat, u.Material), path
    previous_usage = usage(mat)
    desired_properties = {
        'material_domain': u.MaterialDomain.MD_SURFACE,
        'blend_mode': u.BlendMode.BLEND_TRANSLUCENT,
        'shading_model': u.MaterialShadingModel.MSM_UNLIT,
        'two_sided': False,
        'disable_depth_test': False,
        'enable_responsive_aa': True,
    }
    changed = {name: value for name, value in desired_properties.items()
               if mat.get_editor_property(name) != value}
    if changed:
        mat.set_editor_properties(changed)
    # Add sprite support without clearing any existing usage flags, including
    # the old foam's mesh-particle usage retained by archived Niagara assets.
    if not edit.has_material_usage(mat, u.MaterialUsage.MATUSAGE_NIAGARA_SPRITES):
        edit.set_base_material_usage(mat, u.MaterialUsage.MATUSAGE_NIAGARA_SPRITES, True)
    reused = update_existing_graph(mat, code, density)
    if not reused:
        # Bootstrap only a new, empty material. Saved graphs are updated above.
        uv = expression(mat, u.MaterialExpressionTextureCoordinate, -950, -100)
        color = expression(mat, u.MaterialExpressionParticleColor, -950, 80)
        opacity = scalar(mat, 'FoamOpacity' if path == FOAM else 'BubbleOpacity', density, 430)
        depth_distance = scalar(mat, 'SoftIntersectionCm', 1.5, 610)
        field = expression(mat, u.MaterialExpressionCustom, -250, 20,
                           description='Brush foam lobes' if path == FOAM else 'Transparent brush bubble film',
                           code=code, output_type=u.CustomMaterialOutputType.CMOT_FLOAT4)
        sources = {'UV': (uv, ''), 'Particle': (color, 'RGBA'), 'Density': (opacity, '')}
        inputs = []
        for name in sources:
            entry = u.CustomInput()
            entry.set_editor_property('input_name', name)
            inputs.append(entry)
        field.set_editor_property('inputs', inputs)
        for name, (source, output) in sources.items():
            connect(source, output, field, name)
        rgb = expression(mat, u.MaterialExpressionComponentMask, 150, -50, r=True, g=True, b=True, a=False)
        alpha = expression(mat, u.MaterialExpressionComponentMask, 150, 140, r=False, g=False, b=False, a=True)
        connect(field, '', rgb, '')
        connect(field, '', alpha, '')
        depth = expression(mat, u.MaterialExpressionDepthFade, 420, 140, fade_distance_default=1.5)
        connect(alpha, '', depth, 'Opacity')
        connect(depth_distance, '', depth, 'FadeDistance')
        assert edit.connect_material_property(rgb, '', u.MaterialProperty.MP_EMISSIVE_COLOR)
        assert edit.connect_material_property(depth, '', u.MaterialProperty.MP_OPACITY)
    errors = [str(error) for error in edit.recompile_material(mat)]
    assert not errors, {path: errors}
    assert lib.save_loaded_asset(mat, False), path
    row = {
        'compile_errors': errors, 'compiler_statistics': statistics(mat),
        'usage_before': previous_usage, 'usage_after': usage(mat),
        'blend': 'Translucent', 'shading': 'Unlit synthetic hemisphere/film',
        'soft_intersection_cm': 1.5, 'opacity_multiplier': density,
        'lifetime_fade': 'ParticleColor alpha only', 'responsive_aa': True,
        'updated_existing_graph': reused,
    }
    report['materials'][path] = row
    u.log('MC_FOAM_MATERIAL ' + json.dumps({'path': path, 'errors': errors, 'stats': row['compiler_statistics']}))


with u.ScopedEditorTransaction('Brush foam and transparent bubble optics'):
    author_material(FOAM, FOAM_CODE, 1.)
    author_material(BUBBLE, BUBBLE_CODE, 1.)
    # The editor-only native builder configures the two lightweight sprite
    # emitters after their materials compile. CreateBrushFoam remains unchanged.
    system = u.MCVFXAssetBuilder.refine_brush_foam()
    assert isinstance(system, u.NiagaraSystem), 'RefineBrushFoam returned no Niagara system'
    assert system.get_path_name() == SYSTEM + '.NS_BrushFoam', system.get_path_name()
    assert lib.save_loaded_asset(system, False), SYSTEM
    report['system'] = system.get_path_name()

report['compiler_statistics_note'] = 'Instruction/sample counts are compiler evidence, not measured GPU milliseconds.'
(saved / 'FoamReview' / 'MaterialBuild.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
u.log('MC_BRUSH_FOAM_SAVED ' + json.dumps({'backup': str(backup), 'system': report['system'], 'materials': list(report['materials'])}))
