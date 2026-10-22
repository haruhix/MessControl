"""One optional rounded-papilla candidate for the owned menu tongue only.

run('plan') is read-only. run('apply') keeps the real source normal underneath,
adds shallow world-space relief and correlated saliva roughness. run('restore')
returns the previous texture-normal/roughness outputs without removing nodes.
Root must compare the native fixed-camera A/B; compilation is not visual PASS.
"""
import json
from pathlib import Path

MATERIAL = '/Game/UI/MenuShowcase/AnatomyV2/Materials/M_Soft_MI_Menu_MI_Pressure_Soft'
HEIGHT_LABEL = 'MENU candidate | rounded papilla height'
NORMAL_LABEL = 'MENU candidate | papilla surface gradient'
ROUGH_LABEL = 'MENU candidate | papilla saliva correlation'
TRANSFORM_LABEL = 'MENU candidate | source tangent normal to world'
VERTEX_LABEL = 'MENU candidate | geometric world normal'

HEIGHT_CODE = '''
float spacing=max(Spacing,1.0);
float2 q=P.xy/spacing;
float2 origin=floor(q-.5);
float height=0.0;
[unroll] for(int y=0;y<2;y++) {
    [unroll] for(int x=0;x<2;x++) {
        float2 cell=origin+float2(x,y);
        // Smooth fixed cell jitter: no time input, texture fetch or grid lines.
        float3 h=frac(float3(cell.x,cell.y,cell.x)*float3(.1031,.1030,.0973));
        h+=dot(h,h.yxz+33.33);
        h=frac((h.xxy+h.yxx)*h.zyx);
        float2 center=cell+.5+(h.xy-.5)*.58;
        float radius=lerp(.30,.43,h.z);
        float2 d=(q-center)/radius;
        float dome=saturate(1.0-dot(d,d));
        height+=dome*dome*Height*lerp(.78,1.20,h.z);
    }
}
return height;
'''

NORMAL_CODE = '''
float3 geometric=normalize(GeomNormal);
float3 base=normalize(SourceNormal);
float3 px=ddx(P),py=ddy(P);
float3 r1=cross(py,geometric),r2=cross(geometric,px);
float det=dot(px,r1);
float footprint=max(length(px.xy),length(py.xy))/max(Spacing,1.0);
float attenuation=1.0-smoothstep(.30,.85,footprint);
float3 gradient=sign(det)*(ddx(H)*r1+ddy(H)*r2)/max(abs(det),.000001);
return normalize(base-gradient*attenuation);
'''

ROUGH_CODE = '''
float footprint=max(length(ddx(P.xy)),length(ddy(P.xy)))/max(Spacing,1.0);
float attenuation=1.0-smoothstep(.30,.85,footprint);
float papilla=lerp(.40,saturate(H/max(Height,.001)),attenuation);
// Convex tips have a thin rougher layer, saliva rests in the shallow valleys.
return clamp(SourceRoughness+lerp(-.030,.065,papilla),.245,.485);
'''


def run(action='plan', *, spacing_cm=2.35, height_cm=.14):
    import unreal as u
    assert action in ('plan', 'apply', 'restore')
    assert 1.5 <= spacing_cm <= 3.0 and .08 <= height_cm <= .20
    levels = u.get_editor_subsystem(u.LevelEditorSubsystem)
    assert not levels.is_in_play_in_editor(), 'Preserve active PIE.'
    world = u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
    assert world.get_path_name().startswith('/Game/UI/MenuShowcase/AnatomyV2/L_MenuAnatomyV2.')
    edit, lib = u.MaterialEditingLibrary, u.EditorAssetLibrary
    material = lib.load_asset(MATERIAL)
    assert isinstance(material, u.Material)
    nodes = list(edit.get_material_expressions(material))
    customs = {str(n.get_editor_property('description')): n for n in nodes if isinstance(n, u.MaterialExpressionCustom)}
    source_normal = customs['Preserve soft papillae and sculpted mucosa']
    source_rough = customs['Saliva breaks across the existing tissue relief']
    wpo = edit.get_material_property_input_node(material, u.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    wpo_code = str(wpo.get_editor_property('code')) if isinstance(wpo, u.MaterialExpressionCustom) else None
    base_color = edit.get_material_property_input_node(material, u.MaterialProperty.MP_BASE_COLOR)
    before_samples = sum(isinstance(n, u.MaterialExpressionTextureSample) for n in nodes)
    report = dict(material=MATERIAL, action=action, spacing_cm=spacing_cm, height_cm=height_cm,
                  source_normal=source_normal.get_path_name(), source_roughness=source_rough.get_path_name(),
                  texture_samples_before=before_samples,
                  visual_status='not_run',
                  estimated_cost='Rough order 150–250 extra scalar ALU plus pixel derivatives; 0 new texture samples. Exact native shader statistics and GPU time not measured.')
    if action == 'plan':
        return report
    if action == 'restore':
        assert edit.connect_material_property(source_normal, '', u.MaterialProperty.MP_NORMAL)
        assert edit.connect_material_property(source_rough, '', u.MaterialProperty.MP_ROUGHNESS)
        material.set_editor_property('tangent_space_normal', True)
    else:
        def find_desc(cls, text):
            return next((n for n in edit.get_material_expressions(material)
                         if isinstance(n, cls) and str(n.get_editor_property('desc')) == text), None)

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
            for name, node in inputs.items():
                assert edit.connect_material_expressions(node, '', result, name)
            return result

        spacing = scalar('Menu Papilla Spacing CM', spacing_cm)
        height = scalar('Menu Papilla Height CM', height_cm)
        position = next((n for n in nodes if isinstance(n, u.MaterialExpressionWorldPosition)), None)
        if position is None:
            position = edit.create_material_expression(material, u.MaterialExpressionWorldPosition)
        world_normal = find_desc(u.MaterialExpressionTransform, TRANSFORM_LABEL)
        if world_normal is None:
            world_normal = edit.create_material_expression(material, u.MaterialExpressionTransform)
            world_normal.set_editor_property('desc', TRANSFORM_LABEL)
        world_normal.set_editor_property('transform_source_type', u.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_TANGENT)
        world_normal.set_editor_property('transform_type', u.MaterialVectorCoordTransform.TRANSFORM_WORLD)
        assert edit.connect_material_expressions(source_normal, '', world_normal, '')
        geometric_normal = find_desc(u.MaterialExpressionVertexNormalWS, VERTEX_LABEL)
        if geometric_normal is None:
            geometric_normal = edit.create_material_expression(material, u.MaterialExpressionVertexNormalWS)
            geometric_normal.set_editor_property('desc', VERTEX_LABEL)
        field = custom(HEIGHT_LABEL, HEIGHT_CODE,
                       {'P': position, 'Spacing': spacing, 'Height': height}, u.CustomMaterialOutputType.CMOT_FLOAT1)
        normal = custom(NORMAL_LABEL, NORMAL_CODE,
                        {'P': position, 'H': field, 'Spacing': spacing,
                         'GeomNormal': geometric_normal, 'SourceNormal': world_normal}, u.CustomMaterialOutputType.CMOT_FLOAT3)
        rough = custom(ROUGH_LABEL, ROUGH_CODE,
                       {'P': position, 'H': field, 'Spacing': spacing, 'Height': height,
                        'SourceRoughness': source_rough}, u.CustomMaterialOutputType.CMOT_FLOAT1)
        assert edit.connect_material_property(normal, '', u.MaterialProperty.MP_NORMAL)
        assert edit.connect_material_property(rough, '', u.MaterialProperty.MP_ROUGHNESS)
        material.set_editor_property('tangent_space_normal', False)
    assert edit.get_material_property_input_node(material, u.MaterialProperty.MP_BASE_COLOR) == base_color
    assert edit.get_material_property_input_node(material, u.MaterialProperty.MP_WORLD_POSITION_OFFSET) == wpo
    if wpo_code is not None:
        assert str(wpo.get_editor_property('code')) == wpo_code
    after_samples = sum(isinstance(n, u.MaterialExpressionTextureSample) for n in edit.get_material_expressions(material))
    assert before_samples == after_samples
    errors = list(edit.recompile_material(material))
    assert not errors, errors
    assert lib.save_loaded_asset(material, only_if_is_dirty=False)
    report.update(status='compiled_pending_native_AB', errors=errors,
                  texture_samples_after=after_samples, preserved_base_color=True, preserved_wpo=True)
    out = Path(u.Paths.project_dir()).resolve() / 'Artifacts/MenuSceneV2_20261022/Environment/TonguePapillaCandidate.json'
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(report, indent=2), encoding='utf8')
    u.log('MC_MENU_PAPILLA_CANDIDATE_PENDING_NATIVE_AB ' + str(out))
    return report
