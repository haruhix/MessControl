"""Author the fire particle shader and its smoke flipbook material only.

Run with UnrealEditor-Cmd -run=pythonscript -script=<absolute path>.
Wind, task feedback and source art are preserved. Texture duplication is native UE.
"""
import json
import os
from pathlib import Path
import unreal as u

lib = u.EditorAssetLibrary
edit = u.MaterialEditingLibrary
assets = u.AssetToolsHelpers.get_asset_tools()
folder = '/Game/Gameplay/VFX'
reports = []

def material(name, lit=False):
    path = folder + '/' + name
    mat = lib.load_asset(path) if lib.does_asset_exist(path) else assets.create_asset(
        name, folder, u.Material, u.MaterialFactoryNew())
    assert mat, path
    edit.delete_all_material_expressions(mat)
    mat.set_editor_property('blend_mode', u.BlendMode.BLEND_TRANSLUCENT)
    mat.set_editor_property('shading_model', u.MaterialShadingModel.MSM_DEFAULT_LIT if lit else u.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property('two_sided', True)
    if lit:
        mat.set_editor_property('translucency_lighting_mode', u.TranslucencyLightingMode.TLM_SURFACE)
    return mat

def node(mat, kind, x, y):
    return edit.create_material_expression(mat, kind, x, y)

def scalar(mat, name, value, y):
    n = node(mat, u.MaterialExpressionScalarParameter, -700, y)
    n.set_editor_property('parameter_name', name)
    n.set_editor_property('default_value', value)
    n.set_editor_property('group', 'Fire')
    return n

def custom(mat, name, code, inputs, x, y, output=u.CustomMaterialOutputType.CMOT_FLOAT1):
    n = node(mat, u.MaterialExpressionCustom, x, y)
    n.set_editor_property('description', name)
    n.set_editor_property('code', code)
    n.set_editor_property('output_type', output)
    entries = []
    for key in inputs:
        i = u.CustomInput()
        i.set_editor_property('input_name', key)
        entries.append(i)
    n.set_editor_property('inputs', entries)
    for key, (src, pin) in inputs.items():
        assert edit.connect_material_expressions(src, pin, n, key), key
    return n

def output(expression, pin, property):
    assert edit.connect_material_property(expression, pin, property), str(property)

def save(mat, **details):
    errors = edit.recompile_material(mat)
    assert not errors, errors
    assert lib.save_loaded_asset(mat, only_if_is_dirty=False), mat.get_path_name()
    reports.append(dict(material=mat.get_path_name(),
        blend=str(mat.get_editor_property('blend_mode')),
        shading=str(mat.get_editor_property('shading_model')),
        shader_compile_errors=errors or [], saved=True, **details))

def build_fire():
    mat = material('M_ReactionFire')
    uv = node(mat, u.MaterialExpressionTextureCoordinate, -1100, 0)
    vertex = node(mat, u.MaterialExpressionVertexColor, -1100, 220)
    age = scalar(mat, 'FireAge', 0, 450)
    glow = scalar(mat, 'SelfGlow', .95, 570)
    noise = r'''
    struct FlameNoise {
        float hash(float2 p) { return frac(sin(dot(p,float2(127.1,311.7)))*43758.5453); }
        float value(float2 p) {
            float2 i=floor(p),f=frac(p); f=f*f*(3-2*f);
            return lerp(lerp(hash(i),hash(i+float2(1,0)),f.x),
                        lerp(hash(i+float2(0,1)),hash(i+1),f.x),f.y);
        }
        float fbm(float2 p) {
            float n=value(p)*.53;
            p=float2(p.x*.80-p.y*.60,p.x*.60+p.y*.80)*2.03+17;
            n+=value(p)*.27;
            p=p*2.07+11;
            n+=value(p)*.13;
            return n+value(p*2.11+4)*.07;
        }
    }; FlameNoise N;
    float seed=Seed*19.73;
    float y=saturate(UV.y),x=UV.x-.5;
    float low=N.fbm(float2(x*3+seed,y*3-Age*1.35));
    float warp=(low-.5)*.28+sin(y*8-Age*5.2+seed)*.073*(.3+y);
    float detail=N.fbm(float2((x+warp)*8+seed,y*7-Age*2.6));
    float fine=N.fbm(float2((x+warp)*15-seed,y*13-Age*4.1));
    float width=.49*pow(1-y,.67)+(low-.5)*(.10+y*.17);
    float inside=width-abs(x+warp);
    float edge=smoothstep(-.065,.055,inside+(fine-.5)*.065*y);
    float holes=smoothstep(.18,.47,detail+(1-y)*.27);
    float tip=1-smoothstep(.61,.98,y+(low-.5)*.25);
    float foot=N.fbm(float2(x*12+seed,Age*.85));
    float base=smoothstep(-.015,.15,y-(.025+foot*.075));
    float alpha=edge*holes*tip*base;
    float core=pow(saturate(inside/max(.035,width)),2.5)*(1-smoothstep(.20,.66,y));
    core*=smoothstep(.25,.79,detail+low*.28)*Hotness;
    float streak=lerp(.74,1.08,smoothstep(.25,.72,fine));
    float3 colour=lerp(float3(.82,.066,.007),float3(.96,.31,.040),saturate(detail*.92));
    colour=lerp(colour,float3(.95,.71,.34),saturate(core));
    return float4(colour*streak*Glow,alpha);
    '''
    field = custom(mat, 'Advected turbulent fire, cream core and eroding orange tongues', noise,
        {'UV': (uv, ''), 'Seed': (vertex, 'R'), 'Hotness': (vertex, 'G'),
         'Age': (age, ''), 'Glow': (glow, '')}, -150, 0, u.CustomMaterialOutputType.CMOT_FLOAT4)
    emission = custom(mat, 'Hot fire stays emissive on every side', 'return F.rgb;',
        {'F': (field, '')}, 220, 0, u.CustomMaterialOutputType.CMOT_FLOAT3)
    output(emission, '', u.MaterialProperty.MP_EMISSIVE_COLOR)
    alpha = custom(mat, 'Soft animated density and particle fade', 'return F.a*A;',
        {'F': (field, ''), 'A': (vertex, 'A')}, 220, 220)
    fade = node(mat, u.MaterialExpressionDepthFade, 500, 220)
    fade.set_editor_property('fade_distance_default', 9)
    assert edit.connect_material_expressions(alpha, '', fade, 'Opacity')
    output(fade, '', u.MaterialProperty.MP_OPACITY)
    save(mat, self_glow=.95, animation='server-age advected domain-warped fBm, narrow detailed core and irregular soft foot', depth_fade=9)

if os.environ.get('MC_REACTION_SMOKE_ONLY') != '1':
    build_fire()

# Native 64-frame smoke footage provides billowing detail instead of circles.
texture_path = folder + '/T_FireSmoke'
source = '/Engine/Tutorial/SubEditors/TutorialAssets/T_SmokeSubUV_8X8'
created_texture = not lib.does_asset_exist(texture_path)
if created_texture:
    # Commandlets can omit tutorial content from their catalog scan. Load the
    # saved package directly, then duplicate its actual texture through UE.
    original = u.load_object(None, source + '.T_SmokeSubUV_8X8')
    assert original, source
    assert assets.duplicate_asset('T_FireSmoke', folder, original), source
texture = lib.load_asset(texture_path)
assert texture, texture_path
if created_texture:
    texture.set_editor_property('srgb', False)
    assert lib.save_loaded_asset(texture, only_if_is_dirty=False)

mat = material('M_ReactionSmoke')
uv = node(mat, u.MaterialExpressionTextureCoordinate, -1100, 0)
vertex = node(mat, u.MaterialExpressionVertexColor, -1100, 220)
tex = node(mat, u.MaterialExpressionTextureObjectParameter, -1100, 440)
tex.set_editor_property('parameter_name', 'SmokeFlipbook')
tex.set_editor_property('texture', texture)
tex.set_editor_property('sampler_type', u.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
sample = custom(mat, 'Native 8x8 smoke flipbook with continuous frame blending', r'''
float frame=saturate(Life)*63;
float a=floor(frame),b=min(a+1,63);
float2 local=saturate(UV)*.97+.015;
float2 uvA=(local+float2(fmod(a,8),floor(a/8)))/8;
float2 uvB=(local+float2(fmod(b,8),floor(b/8)))/8;
return lerp(Texture2DSample(Tex,TexSampler,uvA),Texture2DSample(Tex,TexSampler,uvB),frac(frame));
''', {'UV': (uv, ''), 'Life': (vertex, 'G'), 'Tex': (tex, '')}, -150, 0, u.CustomMaterialOutputType.CMOT_FLOAT4)
colour = custom(mat, 'Visible charcoal billows with bright thin edges and dark dense curls',
    'return lerp(float3(.105,.112,.125),float3(.023,.025,.029),saturate((S.r-.50)/.34));',
    {'S': (sample, '')}, 220, 0, u.CustomMaterialOutputType.CMOT_FLOAT3)
output(colour, '', u.MaterialProperty.MP_EMISSIVE_COLOR)
alpha = custom(mat, 'Smoke dissolves on its native transparent contour',
    r'''
float2 p=(UV-.5)*2;
float border=1-smoothstep(.70,.98,max(abs(p.x),abs(p.y)));
float rounded=1-smoothstep(.83,1.22,length(p*float2(.93,.88)));
float background=smoothstep(.025,.13,S.a);
float density=saturate(pow(max(S.a-.008,0),.65)*1.45);
return density*background*border*rounded*A;
''', {'S': (sample, ''), 'A': (vertex, 'A'), 'UV': (uv, '')}, 220, 220)
fade = node(mat, u.MaterialExpressionDepthFade, 500, 220)
fade.set_editor_property('fade_distance_default', 16)
assert edit.connect_material_expressions(alpha, '', fade, 'Opacity')
output(fade, '', u.MaterialProperty.MP_OPACITY)
save(mat, texture=texture.get_path_name(), atlas='8x8/64 frames, blended',
     density='threshold tiny alpha; native alpha ^ .65 * 1.45; rounded UV edge feather; maximum particle alpha .68', depth_fade=16)

report = Path(u.Paths.project_saved_dir()) / 'ReactionFireMaterial.json'
report.write_text(json.dumps(dict(materials=reports, texture=texture.get_path_name(),
    smoke_only=os.environ.get('MC_REACTION_SMOKE_ONLY') == '1'), indent=2), encoding='utf-8')
u.log('MC_REACTION_FIRE_MATERIAL_PASS ' + json.dumps(reports))
