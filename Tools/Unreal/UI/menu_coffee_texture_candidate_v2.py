"""Patch only the owned menu coffee enamel with existing plaque-wash detail.

No live editor calls occur until root runs run('apply'). No shared texture or
material is edited. The patch preserves contact C/U/V/W parameters and enamel
normal connections, replacing the numerically fragile sine hash only inside
this one material. One existing texture sample supplies continuous density.
"""
import json
from pathlib import Path

TARGET = '/Game/UI/MenuShowcase/AnatomyV2/Materials/M_MenuCoffeeEnamel'
TEXTURE = '/Game/Gameplay/Care/Textures/T_PlaqueReference'
UV_LABEL = 'MENU coffee candidate | contact-oriented wash UV'
DENSITY_LABEL = 'MENU coffee candidate | existing plaque wash density'

# Every lattice corner goes through the identical hash arithmetic. The previous
# b=a+74.7 shortcut could disagree with dot(next_cell,...) after float32 rounding,
# which frac(sin()*43758) amplified into a visible discontinuity at cell faces.
STABLE_NOISE = '''
struct MenuStableValueNoise {
    float hash(float3 cell) {
        float3 h=frac(cell*float3(.1031,.1030,.0973));
        h+=dot(h,h.yxz+33.33);
        return frac((h.x+h.y)*h.z);
    }
    float value(float3 p) {
        float3 i=floor(p),f=frac(p);
        f=f*f*f*(f*(f*6.0-15.0)+10.0);
        float a=lerp(hash(i),hash(i+float3(1,0,0)),f.x);
        float b=lerp(hash(i+float3(0,1,0)),hash(i+float3(1,1,0)),f.x);
        float c=lerp(hash(i+float3(0,0,1)),hash(i+float3(1,0,1)),f.x);
        float d=lerp(hash(i+float3(0,1,1)),hash(i+float3(1,1,1)),f.x);
        return lerp(lerp(a,b,f.y),lerp(c,d,f.y),f.z);
    }
};
MenuStableValueNoise noise;
return noise.value(P*SCALE);
'''

UV_CODE = '''
float3 d=P-C;
return float2(dot(d,U)/116.0,dot(d,V)/132.0)*Scale+float2(.50,.50);
'''

DENSITY_CODE = '''
// The existing sRGB plaque reference is sampled into linear colour. Its blue
// channel separates thin warm wash from ivory; remap continuously, no threshold.
float wash=saturate(1.0-pow(max(Pigment.b,.0001),.455));
float grains=saturate(1.0-pow(max(dot(Pigment,float3(.2126,.7152,.0722)),.0001),.455));
return saturate(wash*.72+grains*.28);
'''

MASK_CODE = '''
float3 d=P-C;
float2 q=float2(dot(d,U)/58.0,dot(d,V)/66.0);
float a=atan2(q.y,q.x);
float edge=.87+.065*sin(a*3.0+.5)+.035*sin(a*7.0-.8)+(Density-.5)*.17;
float feather=max(.085,fwidth(length(q))*1.5);
float boundary=1.0-smoothstep(edge-feather,edge+.065,length(q));
float slab=1.0-smoothstep(28.0,58.0,abs(dot(d,W)));
float facing=smoothstep(.03,.35,dot(N,W));
return saturate(boundary*slab*facing);
'''

COLOR_CODE = '''
float concentration=saturate(Density*.90+N*.10);
float3 pigment=lerp(float3(.230,.105,.038),float3(.072,.021,.006),concentration);
float opticalDensity=Mask*(.16+.82*smoothstep(.07,.87,Density));
return lerp(Enamel,pigment,saturate(opticalDensity));
'''

ROUGH_CODE = '''
float opticalDensity=Mask*(.16+.82*smoothstep(.07,.87,Density));
return lerp(Rough,.205+.045*(1.0-Density),saturate(opticalDensity));
'''


def run(action='plan', *, pattern_scale=1.0):
    import unreal as u
    assert action in ('plan', 'apply') and .5 <= pattern_scale <= 2.0
    levels = u.get_editor_subsystem(u.LevelEditorSubsystem)
    assert not levels.is_in_play_in_editor(), 'Preserve active PIE.'
    world = u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
    assert world.get_path_name().startswith('/Game/UI/MenuShowcase/AnatomyV2/L_MenuAnatomyV2.')
    edit, lib = u.MaterialEditingLibrary, u.EditorAssetLibrary
    material, texture = lib.load_asset(TARGET), lib.load_asset(TEXTURE)
    assert isinstance(material, u.Material) and isinstance(texture, u.Texture2D)
    nodes = list(edit.get_material_expressions(material))
    customs = {str(n.get_editor_property('description')): n for n in nodes if isinstance(n, u.MaterialExpressionCustom)}
    required = ['Soft nonperiodic enamel variation', 'Band-limited microscopic enamel relief',
                'One connected feathered coffee film at bristle contact',
                'Espresso concentrate and translucent warm edges',
                'Thin wet coffee film retains enamel underneath']
    assert all(name in customs for name in required), 'Review the changed owned coffee graph.'
    params = {str(n.get_editor_property('parameter_name')): n for n in nodes
              if isinstance(n, u.MaterialExpressionVectorParameter)}
    assert all('Coffee ' + name in params for name in ('C', 'U', 'V', 'W'))
    before = {name: str(customs[name].get_editor_property('code')) for name in required}
    normal = edit.get_material_property_input_node(material, u.MaterialProperty.MP_NORMAL)
    wpo = edit.get_material_property_input_node(material, u.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    wpo_code = str(wpo.get_editor_property('code')) if isinstance(wpo, u.MaterialExpressionCustom) else None
    source_samples = sum(isinstance(n, u.MaterialExpressionTextureSample) for n in nodes)
    report = dict(material=TARGET, texture=TEXTURE, before_code=before,
                  texture_source='SourceArt/Care/PlaqueReference.png; already imported, no new download/image generation.',
                  source_srgb=bool(texture.get_editor_property('srgb')),
                  hypothesis='Float32 corner-hash disagreement is CPU-reproducible; exact native generated shader/GPU artifact cause is not yet confirmed.',
                  desktop_precision='UE5.8 Common.ush selects MaterialFloat=half for pixel shaders unless FORCE_MATERIAL_FLOAT_FULL_PRECISION. Platform.ush may map half to float/min16/native types according to compiler/platform defines. Exact generated SM5 shader must be inspected before claiming native half precision.',
                  expected_cost='One filtered 2D sample; stable ALU hash replaces sine hash. Exact GPU cost unprofiled.',
                  pattern_scale=pattern_scale, status='proposal')
    if action == 'plan':
        return report

    def scalar(name, value):
        result = next((n for n in edit.get_material_expressions(material)
                       if isinstance(n, u.MaterialExpressionScalarParameter)
                       and str(n.get_editor_property('parameter_name')) == name), None)
        if result is None:
            result = edit.create_material_expression(material, u.MaterialExpressionScalarParameter)
            result.set_editor_property('parameter_name', name)
        result.set_editor_property('default_value', value)
        return result

    def custom(label, code, inputs, output_type):
        result = customs.get(label)
        if result is None:
            result = edit.create_material_expression(material, u.MaterialExpressionCustom)
            result.set_editor_property('description', label)
        entries = []
        for name in inputs:
            entry = u.CustomInput()
            entry.set_editor_property('input_name', name)
            entries.append(entry)
        result.set_editor_property('inputs', entries)
        result.set_editor_property('output_type', output_type)
        result.set_editor_property('code', code)
        for name, (source, pin) in inputs.items():
            assert edit.connect_material_expressions(source, pin, result, name)
        return result

    position = next(n for n in nodes if isinstance(n, u.MaterialExpressionWorldPosition))
    uv = custom(UV_LABEL, UV_CODE,
                {'P': (position, ''), 'C': (params['Coffee C'], 'RGB'),
                 'U': (params['Coffee U'], 'RGB'), 'V': (params['Coffee V'], 'RGB'),
                 'Scale': (scalar('Menu Coffee Pattern Scale', pattern_scale), '')}, u.CustomMaterialOutputType.CMOT_FLOAT2)
    sample = next((n for n in edit.get_material_expressions(material)
                   if isinstance(n, u.MaterialExpressionTextureSampleParameter2D)
                   and str(n.get_editor_property('parameter_name')) == 'Menu Coffee Density Texture'), None)
    if sample is None:
        sample = edit.create_material_expression(material, u.MaterialExpressionTextureSampleParameter2D)
        sample.set_editor_property('parameter_name', 'Menu Coffee Density Texture')
    sample.set_editor_property('texture', texture)
    sampler = u.MaterialSamplerType.SAMPLERTYPE_COLOR if texture.get_editor_property('srgb') else u.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR
    sample.set_editor_property('sampler_type', sampler)
    assert edit.connect_material_expressions(uv, '', sample, 'UVs')
    density = custom(DENSITY_LABEL, DENSITY_CODE, {'Pigment': (sample, 'RGB')}, u.CustomMaterialOutputType.CMOT_FLOAT1)
    for label in required[2:]:
        expression = customs[label]
        inputs = list(expression.get_editor_property('inputs'))
        if 'Density' not in [str(i.get_editor_property('input_name')) for i in inputs]:
            entry = u.CustomInput()
            entry.set_editor_property('input_name', 'Density')
            inputs.append(entry)
            expression.set_editor_property('inputs', inputs)
        assert edit.connect_material_expressions(density, '', expression, 'Density')
    customs[required[0]].set_editor_property('code', STABLE_NOISE.replace('SCALE', '.075'))
    customs[required[1]].set_editor_property('code', STABLE_NOISE.replace('SCALE', '3.0'))
    customs[required[2]].set_editor_property('code', MASK_CODE)
    customs[required[3]].set_editor_property('code', COLOR_CODE)
    customs[required[4]].set_editor_property('code', ROUGH_CODE)
    assert edit.get_material_property_input_node(material, u.MaterialProperty.MP_NORMAL) == normal
    assert edit.get_material_property_input_node(material, u.MaterialProperty.MP_WORLD_POSITION_OFFSET) == wpo
    if wpo_code is not None:
        assert str(wpo.get_editor_property('code')) == wpo_code
    errors = list(edit.recompile_material(material))
    assert not errors, errors
    assert lib.save_loaded_asset(material, only_if_is_dirty=False)
    samples_after = sum(isinstance(n, u.MaterialExpressionTextureSample) for n in edit.get_material_expressions(material))
    report.update(status='compiled_pending_native_AB', errors=errors,
                  samples_before=source_samples, samples_after=samples_after,
                  normal_connection_preserved=True, wpo_preserved=True)
    out = Path(u.Paths.project_dir()).resolve() / 'Artifacts/MenuSceneV2_20261022/Environment/CoffeeTextureCandidateV2.json'
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(report, indent=2), encoding='utf8')
    u.log('MC_MENU_COFFEE_TEXTURE_CANDIDATE_PENDING_NATIVE_AB ' + str(out))
    return report
