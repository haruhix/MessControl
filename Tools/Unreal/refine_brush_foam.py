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
    'design': 'Small shaded foam clumps and thin transparent bubble rims; synthetic unlit shading',
    'age': 'Niagara DynamicMaterialParameter0.x exports normalized age; ParticleColor alpha and material age supply soft fade',
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
// Three soft lobes occupy a compact part of the quad, not a smoke billboard.
float2 a = p - float2(-.18, .03);
float2 b = p - float2(.23, .13);
float2 c = p - float2(.01, -.29);
float da = length(a) - .55;
float db = length(b) - .43;
float dc = length(c) - .38;
float d = min(da, min(db, dc));
float aa = max(fwidth(d) * 1.15, .012);
float coverage = 1 - smoothstep(-aa, aa, d);
float2 v = da <= db && da <= dc ? a/.55 : db <= dc ? b/.43 : c/.38;
float z = sqrt(saturate(1 - dot(v,v)));
float3 n = normalize(float3(v * .82, max(z,.03)));
float light = saturate(dot(n, float3(-.36,-.43,.825)));
float shade = saturate(.38 + light*.54 + z*.06);
float3 color = lerp(float3(.39,.46,.49), float3(.92,.96,.97), shade);
float2 glintUV = v - float2(-.32,-.34);
float glint = pow(saturate(1 - dot(glintUV,glintUV)*11), 5);
color = lerp(color, float3(.99,1,1), glint*.44);
float life = smoothstep(0,.10,saturate(Age)) * (1 - smoothstep(.68,1,saturate(Age)));
float alpha = coverage * lerp(.25,.65,z) * saturate(Density) * saturate(Particle.a) * life;
return float4(color * saturate(Particle.rgb), alpha);
'''

BUBBLE_CODE = r'''
float2 p = (UV - .5) * 2;
float r = length(p);
float aa = max(fwidth(r) * 1.1, .006);
float disk = 1 - smoothstep(.80-aa,.80+aa,r);
float rim = 1 - smoothstep(.035-aa,.035+aa,abs(r-.765));
float2 direction = p/max(r,.001);
float upper = pow(saturate(dot(direction,float2(-.40,-.9165))),16) * rim;
float lower = pow(saturate(dot(direction,float2(.70,.714))),22) * rim;
// A restrained cool shift along the rim suggests a film, without scene color.
float tint = saturate(.5 + direction.x*.32 + direction.y*.12);
float3 film = lerp(float3(.59,.76,.84),float3(.76,.69,.84),tint*.32);
float3 color = film * lerp(.58,.87,rim);
color = lerp(color,float3(.97,.995,1),saturate(upper*.92+lower*.54));
float life = smoothstep(0,.10,saturate(Age)) * (1 - smoothstep(.70,1,saturate(Age)));
float alpha = disk * (.010 + rim*.42 + upper*.34 + lower*.16)
    * saturate(Density) * saturate(Particle.a) * life;
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


def author_material(path, code, density):
    mat = lib.load_asset(path) if lib.does_asset_exist(path) else None
    if mat is None:
        folder, name = path.rsplit('/', 1)
        mat = u.AssetToolsHelpers.get_asset_tools().create_asset(name, folder, u.Material, u.MaterialFactoryNew())
    assert isinstance(mat, u.Material), path
    previous_usage = usage(mat)
    edit.delete_all_material_expressions(mat)
    mat.set_editor_properties({
        'material_domain': u.MaterialDomain.MD_SURFACE,
        'blend_mode': u.BlendMode.BLEND_TRANSLUCENT,
        'shading_model': u.MaterialShadingModel.MSM_UNLIT,
        'two_sided': False,
        'disable_depth_test': False,
    })
    # Add sprite support without clearing any existing usage flags, including
    # the old foam's mesh-particle usage retained by archived Niagara assets.
    edit.set_base_material_usage(mat, u.MaterialUsage.MATUSAGE_NIAGARA_SPRITES, True)
    uv = expression(mat, u.MaterialExpressionTextureCoordinate, -950, -100)
    color = expression(mat, u.MaterialExpressionParticleColor, -950, 80)
    # The stateless template has no Particles.NormalizedAge output. Its native
    # builder exports 0..1 through DynamicMaterialParameter0.x instead.
    age = expression(mat, u.MaterialExpressionDynamicParameter, -950, 260,
                     param_names=['Age', 'Unused Y', 'Unused Z', 'Unused W'])
    opacity = scalar(mat, 'FoamOpacity' if path == FOAM else 'BubbleOpacity', density, 430)
    depth_distance = scalar(mat, 'SoftIntersectionCm', 4., 610)
    field = expression(mat, u.MaterialExpressionCustom, -250, 20,
                       description='Brush foam lobes' if path == FOAM else 'Transparent brush bubble film',
                       code=code, output_type=u.CustomMaterialOutputType.CMOT_FLOAT4)
    sources = {'UV': (uv, ''), 'Particle': (color, 'RGBA'), 'Age': (age, ''), 'Density': (opacity, '')}
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
    depth = expression(mat, u.MaterialExpressionDepthFade, 420, 140, fade_distance_default=4.)
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
        'soft_intersection_cm': 4., 'opacity_multiplier': density,
        'age_binding': 'DynamicMaterialParameter0.x',
    }
    report['materials'][path] = row
    u.log('MC_FOAM_MATERIAL ' + json.dumps({'path': path, 'errors': errors, 'stats': row['compiler_statistics']}))


with u.ScopedEditorTransaction('Brush foam and transparent bubble optics'):
    author_material(FOAM, FOAM_CODE, .86)
    author_material(BUBBLE, BUBBLE_CODE, .88)
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
