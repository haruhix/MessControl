"""Warm, wet menu anatomy using the real arena maps; no gameplay mutation.

Run in the saved AnatomyV2 editor world with PIE stopped. Only existing owned
presentation materials are edited. Camera, lights, actor transforms and WPO are
deliberately outside this script's ownership. Re-running is deterministic.
"""
import json
from pathlib import Path
import unreal as u

BASE = '/Game/UI/MenuShowcase/AnatomyV2'
FOLDER = BASE + '/Materials'
ROOT = Path(u.Paths.project_dir()).resolve()
LIB = u.EditorAssetLibrary
EDIT = u.MaterialEditingLibrary
LEVELS = u.get_editor_subsystem(u.LevelEditorSubsystem)
assert not LEVELS.is_in_play_in_editor(), 'Stop the task-owned preview PIE first.'
WORLD = u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
assert WORLD.get_path_name().startswith(BASE + '/L_MenuAnatomyV2.')


def node(m, cls, **properties):
    result = EDIT.create_material_expression(m, cls)
    for name, value in properties.items():
        result.set_editor_property(name, value)
    return result


def scalar(m, name, value):
    return node(m, u.MaterialExpressionScalarParameter,
                parameter_name=name, default_value=value)


def vector(m, name, values):
    return node(m, u.MaterialExpressionVectorParameter,
                parameter_name=name, default_value=u.LinearColor(*values, 1))


def custom(m, label, code, inputs, dim=3):
    result = node(m, u.MaterialExpressionCustom, description=label, code=code,
                  output_type={1: u.CustomMaterialOutputType.CMOT_FLOAT1,
                               3: u.CustomMaterialOutputType.CMOT_FLOAT3}[dim])
    entries = []
    for name in inputs:
        entry = u.CustomInput()
        entry.set_editor_property('input_name', name)
        entries.append(entry)
    result.set_editor_property('inputs', entries)
    for name, (source, pin) in inputs.items():
        assert EDIT.connect_material_expressions(source, pin, result, name)
    return result


def output(expression, prop, pin=''):
    assert EDIT.connect_material_property(expression, pin, prop)


def prepare(path):
    assert path.startswith(FOLDER + '/')
    material = LIB.load_asset(path)
    assert isinstance(material, u.Material), path
    # Root's silhouette pass is applied after this material reconstruction.
    EDIT.delete_all_material_expressions(material)
    material.set_editor_property('blend_mode', u.BlendMode.BLEND_OPAQUE)
    material.set_editor_property('tangent_space_normal', True)
    return material


def finish(material, details):
    EDIT.layout_material_expressions(material)
    errors = list(EDIT.recompile_material(material))
    assert not errors, (material.get_path_name(), errors)
    assert LIB.save_loaded_asset(material, only_if_is_dirty=False)
    return dict(material=material.get_path_name(), errors=errors, **details)


report = []
# Linear colours: coral tongue, warmer gum, wine-red roof receding in depth.
# These are base reflectance values, not baked illumination or a postprocess.
tissues = [
    ('MI_Pressure_Soft', (.430, .140, .155), .80, .245, .105, .46),
    ('MI_TissueV10_EdgeFade', (.375, .105, .120), .54, .285, .095, .44),
    ('MI_Roof_0', (.310, .085, .115), .42, .325, .090, .40),
    ('M_ExitClimate', (.310, .085, .115), .42, .325, .090, .40),
]
for name, tint, strength, rough_min, rough_range, specular in tissues:
    source = LIB.load_asset(FOLDER + '/MI_Menu_' + name)
    assert source, name
    textures = {key: EDIT.get_material_instance_texture_parameter_value(source, key)
                for key in ('Albedo Texture', 'Normal Texture', 'ARM')}
    assert textures['Albedo Texture'] and textures['Normal Texture'], name
    assert 'WhiteSquare' not in textures['Albedo Texture'].get_path_name(), name
    material = prepare(FOLDER + '/M_Soft_MI_Menu_' + name)
    material.set_editor_property('shading_model', u.MaterialShadingModel.MSM_SUBSURFACE)
    base = node(material, u.MaterialExpressionTextureSample,
                texture=textures['Albedo Texture'], sampler_type=u.MaterialSamplerType.SAMPLERTYPE_COLOR)
    normal = node(material, u.MaterialExpressionTextureSample,
                  texture=textures['Normal Texture'], sampler_type=u.MaterialSamplerType.SAMPLERTYPE_NORMAL)
    tint_node = vector(material, 'Tissue Coral', tint)
    colour = custom(material, 'Real tissue pigment with coral art direction', '''
float luminance=dot(Base,float3(.2126,.7152,.0722));
float variation=clamp(luminance*1.65,.64,1.30);
float3 chroma=Base/max(luminance,.035);
return saturate(Tint*variation*lerp(float3(1,1,1),clamp(chroma,.5,1.6),.16));
''', {'Base': (base, 'RGB'), 'Tint': (tint_node, 'RGB')})
    output(colour, u.MaterialProperty.MP_BASE_COLOR)
    strength_node = scalar(material, 'Source Normal Strength', strength)
    flat_normal = custom(material, 'Preserve soft papillae and sculpted mucosa',
                         'return normalize(float3(N.xy*Strength,lerp(1.0,N.z,Strength)));',
                         {'N': (normal, 'RGB'), 'Strength': (strength_node, '')})
    output(flat_normal, u.MaterialProperty.MP_NORMAL)
    if textures['ARM']:
        arm_texture=textures['ARM']
        arm_sampler=(u.MaterialSamplerType.SAMPLERTYPE_COLOR if arm_texture.get_editor_property('srgb') else
                     u.MaterialSamplerType.SAMPLERTYPE_MASKS if arm_texture.get_editor_property('compression_settings')==u.TextureCompressionSettings.TC_MASKS else u.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
        arm = node(material, u.MaterialExpressionTextureSample, texture=arm_texture,
                   sampler_type=arm_sampler)
        rough = custom(material, 'Saliva breaks across the existing tissue relief',
                       'return clamp(Minimum+Source*Variation,.20,.48);',
                       {'Source': (arm, 'G'), 'Minimum': (scalar(material, 'Wet Roughness', rough_min), ''),
                        'Variation': (scalar(material, 'Wet Roughness Variation', rough_range), '')}, 1)
        ao = custom(material, 'Restrained cavity occlusion, no baked black crevices',
                    'return lerp(.78,1.0,Source);', {'Source': (arm, 'R')}, 1)
        output(ao, u.MaterialProperty.MP_AMBIENT_OCCLUSION)
    else:
        rough = scalar(material, 'Wet Roughness', rough_min + rough_range * .5)
    output(rough, u.MaterialProperty.MP_ROUGHNESS)
    output(scalar(material, 'Wet Dielectric Specular', specular), u.MaterialProperty.MP_SPECULAR)
    scatter = custom(material, 'Warm shallow tissue scattering',
                     'return Tint*float3(.75,.23,.18);', {'Tint': (tint_node, 'RGB')})
    output(scatter, u.MaterialProperty.MP_SUBSURFACE_COLOR)
    output(scalar(material, 'Tissue Density', .86), u.MaterialProperty.MP_OPACITY)
    if name == 'M_ExitClimate':
        EDIT.set_base_material_usage(material, u.MaterialUsage.MATUSAGE_SKELETAL_MESH)
        EDIT.set_base_material_usage(material, u.MaterialUsage.MATUSAGE_MORPH_TARGETS)
    report.append(finish(material, dict(kind='tissue', textures={k: v.get_path_name() if v else None for k, v in textures.items()})))

# A low-frequency, nonperiodic field for subtle enamel variation. No time input,
# extra textures, translucency or geometry displacement. The field is smooth.
NOISE = '''
float3 p=P*.075;
float3 i=floor(p),f=frac(p); f=f*f*(3.0-2.0*f);
float4 a=float4(dot(i,float3(127.1,311.7,74.7)),dot(i+float3(1,0,0),float3(127.1,311.7,74.7)),dot(i+float3(0,1,0),float3(127.1,311.7,74.7)),dot(i+float3(1,1,0),float3(127.1,311.7,74.7)));
float4 b=a+74.7;
a=frac(sin(a)*43758.5453); b=frac(sin(b)*43758.5453);
float n0=lerp(lerp(a.x,a.y,f.x),lerp(a.z,a.w,f.x),f.y);
float n1=lerp(lerp(b.x,b.y,f.x),lerp(b.z,b.w,f.x),f.y);
return lerp(n0,n1,f.z);
'''


def enamel_graph(material):
    material.set_editor_property('shading_model', u.MaterialShadingModel.MSM_DEFAULT_LIT)
    material.set_editor_property('tangent_space_normal', False)
    position = node(material, u.MaterialExpressionWorldPosition)
    noise = custom(material, 'Soft nonperiodic enamel variation', NOISE, {'P': (position, '')}, 1)
    enamel = custom(material, 'Warm natural enamel, clean ivory highlights',
                    'return lerp(float3(.61,.555,.455),float3(.73,.69,.585),N*.55+.25);',
                    {'N': (noise, '')})
    rough = custom(material, 'Subtle enamel polish variation',
                   'return .295+N*.055;', {'N': (noise, '')}, 1)
    # Height is millimetric in this display scene. Derivative attenuation keeps
    # the smooth detail from sparkling when a crown occupies only a few pixels.
    micro = custom(material, 'Band-limited microscopic enamel relief',
                   NOISE.replace('P*.075', 'P*3.0'), {'P': (position, '')}, 1)
    normal = custom(material, 'Enamel surface gradient, very shallow relief', '''
float3 n=normalize(N);
float3 px=ddx(P),py=ddy(P);
float footprint=max(length(px),length(py));
float attenuation=1.0-smoothstep(.15,.55,footprint);
float h=Height*.006*attenuation;
float3 r1=cross(py,n),r2=cross(n,px);
float det=dot(px,r1);
float3 gradient=sign(det)*(ddx(h)*r1+ddy(h)*r2);
return normalize(max(abs(det),.000001)*n-gradient);
''', {'N': (node(material, u.MaterialExpressionVertexNormalWS), ''),
      'P': (position, ''), 'Height': (micro, '')})
    output(normal, u.MaterialProperty.MP_NORMAL)
    output(scalar(material, 'Enamel Specular', .40), u.MaterialProperty.MP_SPECULAR)
    return position, noise, enamel, rough


clean = prepare(FOLDER + '/M_MenuCleanEnamel')
position, noise, enamel, rough = enamel_graph(clean)
output(enamel, u.MaterialProperty.MP_BASE_COLOR)
output(rough, u.MaterialProperty.MP_ROUGHNESS)
report.append(finish(clean, dict(kind='enamel', texture_samples=0)))

# Preserve the authored brush-contact projection rather than repaint all crowns.
actors = u.get_editor_subsystem(u.EditorActorSubsystem).get_all_level_actors()
hero = next(a for a in actors if 'MC_MenuV2_Hero' in [str(t) for t in a.tags])
sample_path = ROOT / 'ArtSource/UI/MenuSceneV2_20261022/Blender/Exports/A_Menu_CleanTooth_Loop_V2_BrushWorld.json'
sample = json.loads(sample_path.read_text(encoding='utf8'))['samples'][20]
transform = hero.get_actor_transform()
loc, q = sample['location_m'], sample['quaternion_wxyz']
pos = transform.transform_location(u.Vector(loc[0]*100, -loc[1]*100, loc[2]*100))
rotation = (transform.rotation * u.Quat(-q[1], q[2], -q[3], q[0])).rotator()
pose = u.Transform()
pose.set_editor_property('translation', pos)
pose.set_editor_property('rotation', rotation.quaternion())
pose.set_editor_property('scale3d', transform.scale3d * (sample['scale'][0] / .01))
center = pose.transform_location(u.Vector(80, 0, -14))
axes = [u.MathLibrary.get_right_vector(rotation), u.MathLibrary.get_forward_vector(rotation), u.MathLibrary.get_up_vector(rotation)]
coffee = prepare(FOLDER + '/M_MenuCoffeeEnamel')
position, noise, enamel, rough = enamel_graph(coffee)
mask_inputs = {'P': (position, ''), 'N': (node(coffee, u.MaterialExpressionVertexNormalWS), '')}
for name, value in zip(['C', 'U', 'V', 'W'], [center] + axes):
    mask_inputs[name] = (vector(coffee, 'Coffee ' + name, [value.x, value.y, value.z]), 'RGB')
mask = custom(coffee, 'One connected feathered coffee film at bristle contact', '''
float3 d=P-C;
float2 q=float2(dot(d,U)/28.0,dot(d,V)/32.0);
float a=atan2(q.y,q.x);
float edge=.86+.07*sin(a*3.0+.5)+.035*sin(a*7.0-.8)+.025*sin(a*11.0);
float boundary=1.0-smoothstep(edge-.13,edge+.055,length(q));
float slab=1.0-smoothstep(6.0,18.0,abs(dot(d,W)));
float facing=smoothstep(.10,.45,dot(N,W));
return saturate(boundary*slab*facing);
''', mask_inputs, 1)
pigment = custom(coffee, 'Espresso concentrate and translucent warm edges', '''
float3 pigment=lerp(float3(.205,.080,.025),float3(.105,.030,.009),N*.65);
float density=saturate(Mask*.90);
return lerp(Enamel,pigment,density);
''', {'N': (noise, ''), 'Mask': (mask, ''), 'Enamel': (enamel, '')})
film_rough = custom(coffee, 'Thin wet coffee film retains enamel underneath',
                    'return lerp(Rough,.235+N*.045,Mask*.80);',
                    {'Rough': (rough, ''), 'N': (noise, ''), 'Mask': (mask, '')}, 1)
output(pigment, u.MaterialProperty.MP_BASE_COLOR)
output(film_rough, u.MaterialProperty.MP_ROUGHNESS)
report.append(finish(coffee, dict(kind='coffee_film', texture_samples=0,
                                contact_center=[center.x, center.y, center.z])))

destination = ROOT / 'Artifacts/MenuSceneV2_20261022/Environment/MaterialPass.json'
destination.parent.mkdir(parents=True, exist_ok=True)
destination.write_text(json.dumps(dict(status='authored_and_compiled_pending_visual_QA',
                                     scope=FOLDER, materials=report), indent=2), encoding='utf8')
u.log('MENU_ENVIRONMENT_MATERIAL_PASS_READY ' + str(destination))
