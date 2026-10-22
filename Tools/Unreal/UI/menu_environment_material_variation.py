"""Patch existing menu tissue colour and sheen, preserving graph and WPO.

Root runs run('apply') through editor Python after stopping owned PIE. No map,
lights, actor transforms, shared texture settings or gameplay packages change.
run('plan') inspects only. This never calls the full material rebuild script.
"""
import json
from pathlib import Path

BASE = '/Game/UI/MenuShowcase/AnatomyV2/Materials/'
NAMES = ['MI_Pressure_Soft', 'MI_TissueV10_EdgeFade', 'MI_Roof_0', 'M_ExitClimate']
SETTINGS = {
    'MI_Pressure_Soft': {'roughness': .352, 'variation': .060, 'specular': .40},
    'MI_TissueV10_EdgeFade': {'roughness': .365, 'variation': .065, 'specular': .40},
    'MI_Roof_0': {'roughness': .390, 'variation': .055, 'specular': .37},
    'M_ExitClimate': {'roughness': .390, 'variation': .055, 'specular': .37},
}

# One inexpensive, stationary smooth value field. Its scales are 60–250 cm in
# this display scene, so this is broad perfusion/saliva, not extra noisy pores.
FIELD = '''
float2 q=P.xy*.0083+P.z*float2(.0021,.0037)+float2(7.31,12.47);
float2 cell=floor(q),f=frac(q); f=f*f*(3.0-2.0*f);
float4 hashes=float4(dot(cell,float2(127.1,311.7)),dot(cell+float2(1,0),float2(127.1,311.7)),dot(cell+float2(0,1),float2(127.1,311.7)),dot(cell+float2(1,1),float2(127.1,311.7)));
hashes=frac(sin(hashes)*43758.5453);
float field=lerp(lerp(hashes.x,hashes.y,f.x),lerp(hashes.z,hashes.w,f.x),f.y);
'''

COLOR_CODE = FIELD + '''
float luminance=max(dot(Base,float3(.2126,.7152,.0722)),.001);
// Recover source value variation rather than clipping its dark half to .64.
float sourceValue=pow(saturate(luminance),.48);
float variation=lerp(.76,1.20,sourceValue);
float3 chroma=clamp(Base/max(luminance,.035),.55,1.6);
float3 warmCool=lerp(float3(.965,.96,1.065),float3(1.045,1.11,.99),field);
float broadDensity=lerp(.92,1.08,field);
return saturate(Tint*variation*lerp(float3(1,1,1),chroma,.14)*warmCool*broadDensity);
'''

ROUGH_CODE = FIELD + '''
// ARM source may be authored with sRGB enabled; operate smoothly on the data
// as currently sampled. Its texture configuration remains untouched.
float sourceRough=pow(saturate(Source),.455);
float brokenSaliva=(field-.5)*.145;
return clamp(Minimum+(sourceRough-.5)*Variation+brokenSaliva,.265,.465);
'''


def run(action='plan'):
    import unreal as u
    assert action in ('plan', 'apply')
    edit, lib = u.MaterialEditingLibrary, u.EditorAssetLibrary
    levels = u.get_editor_subsystem(u.LevelEditorSubsystem)
    assert not levels.is_in_play_in_editor(), 'Preserve active PIE.'
    world = u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
    assert world.get_path_name().startswith('/Game/UI/MenuShowcase/AnatomyV2/L_MenuAnatomyV2.')
    plan = []
    targets = []
    for name in NAMES:
        mat = lib.load_asset(BASE + 'M_Soft_MI_Menu_' + name)
        assert isinstance(mat, u.Material), name
        expressions = list(edit.get_material_expressions(mat))
        custom = {str(n.get_editor_property('description')): n for n in expressions if isinstance(n, u.MaterialExpressionCustom)}
        color = custom['Real tissue pigment with coral art direction']
        rough = custom['Saliva breaks across the existing tissue relief']
        params = {str(n.get_editor_property('parameter_name')): n for n in expressions if isinstance(n, u.MaterialExpressionScalarParameter)}
        assert all(p in params for p in ('Wet Roughness', 'Wet Roughness Variation', 'Wet Dielectric Specular'))
        wpo = edit.get_material_property_input_node(mat, u.MaterialProperty.MP_WORLD_POSITION_OFFSET)
        wpo_code = str(wpo.get_editor_property('code')) if isinstance(wpo, u.MaterialExpressionCustom) else None
        position = next((n for n in expressions if isinstance(n, u.MaterialExpressionWorldPosition)), None)
        data = dict(material=mat.get_path_name(), before_nodes=len(expressions),
                    before_texture_samples=sum(isinstance(n, u.MaterialExpressionTextureSample) for n in expressions),
                    before_color_code=str(color.get_editor_property('code')),
                    before_rough_code=str(rough.get_editor_property('code')),
                    wpo=wpo.get_path_name() if wpo else None, wpo_code=wpo_code,
                    proposed=SETTINGS[name])
        plan.append(data)
        targets.append((name, mat, color, rough, params, wpo, wpo_code, position, data))
    if action == 'plan':
        return dict(status='proposal', materials=plan)
    for name, mat, color, rough, params, wpo, wpo_code, position, data in targets:
        # Reuse root's existing position node (including on the ridged tongue).
        if position is None:
            position = edit.create_material_expression(mat, u.MaterialExpressionWorldPosition)
        for expression, code in ((color, COLOR_CODE), (rough, ROUGH_CODE)):
            inputs = list(expression.get_editor_property('inputs'))
            if 'P' not in [str(i.get_editor_property('input_name')) for i in inputs]:
                entry = u.CustomInput()
                entry.set_editor_property('input_name', 'P')
                inputs.append(entry)
                expression.set_editor_property('inputs', inputs)
            assert edit.connect_material_expressions(position, '', expression, 'P')
            expression.set_editor_property('code', code)
        for key, parameter in [('roughness', 'Wet Roughness'), ('variation', 'Wet Roughness Variation'), ('specular', 'Wet Dielectric Specular')]:
            params[parameter].set_editor_property('default_value', SETTINGS[name][key])
        assert edit.get_material_property_input_node(mat, u.MaterialProperty.MP_WORLD_POSITION_OFFSET) == wpo
        if wpo_code is not None:
            assert str(wpo.get_editor_property('code')) == wpo_code
        errors = list(edit.recompile_material(mat))
        assert not errors, (name, errors)
        after = list(edit.get_material_expressions(mat))
        samples = sum(isinstance(n, u.MaterialExpressionTextureSample) for n in after)
        assert samples == data['before_texture_samples'], 'Unexpected texture-sampler change'
        assert len(after) <= data['before_nodes'] + 1, 'Unbounded graph change'
        assert lib.save_loaded_asset(mat, only_if_is_dirty=False)
        data.update(after_nodes=len(after), after_texture_samples=samples, errors=errors, preserved_wpo=True)
    root = Path(u.Paths.project_dir()).resolve()
    out = root / 'Artifacts/MenuSceneV2_20261022/Environment/MaterialVariationPass.json'
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(dict(status='compiled_pending_native_visual_review', materials=plan,
                                  performance='No new texture samples; added low-frequency ALU not yet GPU profiled.'), indent=2), encoding='utf8')
    u.log('MC_MENU_MATERIAL_VARIATION_PENDING_VISUAL_REVIEW ' + str(out))
    return dict(status='compiled_pending_native_visual_review', report=str(out))


def apply_final_balanced_parameters():
    """Two diagnosed parameter adjustments after the native close framing.

    Warmer roof stops the cool depth fill multiplying a blue-heavy tissue tint.
    Tongue recovers restrained wet breakup after the light's specular reduction.
    Enamel/coffee need the corrected native camera focus before any judgment.
    """
    import unreal as u
    edit, lib = u.MaterialEditingLibrary, u.EditorAssetLibrary
    levels = u.get_editor_subsystem(u.LevelEditorSubsystem)
    assert not levels.is_in_play_in_editor(), 'Preserve active PIE.'
    world = u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
    assert world.get_path_name().startswith('/Game/UI/MenuShowcase/AnatomyV2/L_MenuAnatomyV2.')
    changes = {
        'MI_Pressure_Soft': {'scalars': {'Wet Roughness': .315, 'Wet Dielectric Specular': .44}},
        'MI_Roof_0': {'vectors': {'Tissue Coral': (.295, .095, .087)}},
        'M_ExitClimate': {'vectors': {'Tissue Coral': (.295, .095, .087)}},
    }
    report = []
    for name, values in changes.items():
        material = lib.load_asset(BASE + 'M_Soft_MI_Menu_' + name)
        assert isinstance(material, u.Material)
        nodes = list(edit.get_material_expressions(material))
        scalar_params = {str(n.get_editor_property('parameter_name')): n for n in nodes if isinstance(n, u.MaterialExpressionScalarParameter)}
        vector_params = {str(n.get_editor_property('parameter_name')): n for n in nodes if isinstance(n, u.MaterialExpressionVectorParameter)}
        wpo = edit.get_material_property_input_node(material, u.MaterialProperty.MP_WORLD_POSITION_OFFSET)
        wpo_code = str(wpo.get_editor_property('code')) if isinstance(wpo, u.MaterialExpressionCustom) else None
        before = {}
        for key, value in values.get('scalars', {}).items():
            param = scalar_params[key]
            before[key] = param.get_editor_property('default_value')
            param.set_editor_property('default_value', value)
        for key, value in values.get('vectors', {}).items():
            param = vector_params[key]
            color = param.get_editor_property('default_value')
            before[key] = [color.r, color.g, color.b, color.a]
            param.set_editor_property('default_value', u.LinearColor(*value, 1))
        assert list(edit.get_material_expressions(material)) == nodes
        assert edit.get_material_property_input_node(material, u.MaterialProperty.MP_WORLD_POSITION_OFFSET) == wpo
        if wpo_code is not None:
            assert str(wpo.get_editor_property('code')) == wpo_code
        errors = list(edit.recompile_material(material))
        assert not errors, (name, errors)
        assert lib.save_loaded_asset(material, only_if_is_dirty=False)
        report.append(dict(material=material.get_path_name(), before=before, after=values,
                           nodes_preserved=True, wpo_preserved=True, errors=errors))
    out = Path(u.Paths.project_dir()).resolve() / 'Artifacts/MenuSceneV2_20261022/Environment/FinalMaterialParameters.json'
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(dict(status='parameters_compiled_pending_native_visual_review',
                                  changes=report, unchanged=['enamel', 'coffee', 'normal', 'WPO', 'graph topology']), indent=2), encoding='utf8')
    u.log('MC_MENU_FINAL_MATERIAL_PARAMETERS_PENDING_VISUAL_REVIEW ' + str(out))
    return str(out)
