"""Upgrade the owned fracture material through live Epic MCP toolsets.

Lit vertex colors retain the dark gap and raw shell lip. A cheap procedural
grain breaks the cutout edge; no artist mesh/material or imported pack is edited.
The same cook-reachable material shades the bounded cosmetic shell fragments.
"""
import json
from pathlib import Path
from native_mcp_client import NativeMCP

PATH = '/Game/Gameplay/VFX/NutCombat/M_NutShieldFracture'
M = 'editor_toolset.toolsets.material.MaterialTools'
O = 'editor_toolset.toolsets.object.ObjectTools'
A = 'editor_toolset.toolsets.asset.AssetTools'
CODE = '''
float stripe = sin(UV.y * 337.0 + sin(UV.y * 83.0) * 2.6);
float grain = 0.5 + 0.5 * stripe;
float lip = smoothstep(0.14, 0.28, abs(UV.x - 0.5));
float edge = smoothstep(0.015, 0.075 + grain * 0.045, min(UV.x, 1.0 - UV.x));
float3 shell = Color.rgb * lerp(0.86, 1.12, grain * lip);
return float4(shell, Alpha * edge * lerp(0.55, 1.0, grain));
'''


def main():
    client = NativeMCP()

    def call(group, name, arguments):
        result = client.tool('call_tool', dict(toolset_name=group, tool_name=name, arguments=arguments))
        if result.get('isError'):
            raise RuntimeError(result)
        return json.loads(result['content'][0]['text'])['returnValue']

    if call('EditorToolset.EditorAppToolset', 'IsPIERunning', {}):
        raise RuntimeError('Preserve an existing Play session')
    mat = call(A, 'load_asset', {'asset_path': PATH})
    nodes = call(M, 'get_expressions', {'material_or_function': mat})
    classes = [call(O, 'get_class', {'instance': node})['refPath'] for node in nodes]
    tags = call(A, 'get_metadata_tags', {'asset_path': PATH})
    if len(nodes) == 3 and set(classes) == {
            '/Script/Engine.MaterialExpressionVertexColor',
            '/Script/Engine.MaterialExpressionTextureCoordinate',
            '/Script/Engine.MaterialExpressionCustom'}:
        custom = nodes[classes.index('/Script/Engine.MaterialExpressionCustom')]
        owned = json.loads(call(O, 'get_properties', {'instance': custom, 'properties': ['description', 'code']}))
        if owned.get('description') == 'MC owned shell grain v2' and owned.get('code') in (
                CODE, CODE.replace('Alpha * edge', 'Color.a * edge')):
            # Resume our own interrupted schema assignment; original outputs
            # still point at the retained vertex node at this stage.
            for node, kind in zip(nodes, classes):
                if kind != '/Script/Engine.MaterialExpressionVertexColor':
                    call(M, 'delete_expression', {'material_or_function': mat, 'expression': node})
            nodes = call(M, 'get_expressions', {'material_or_function': mat})
            classes = [call(O, 'get_class', {'instance': node})['refPath'] for node in nodes]
    # This asset was created by the previous pass. Custom artist graphs are not
    # replaced; reruns reuse this pass only when its exact source marker exists.
    if len(nodes) != 1 or classes != ['/Script/Engine.MaterialExpressionVertexColor']:
        if tags.get('MCNutShellSurfaceVersion') == '2':
            owned_fields = []
            for node, kind in zip(nodes, classes):
                if kind == '/Script/Engine.MaterialExpressionCustom':
                    desc = json.loads(call(O, 'get_properties', {'instance': node, 'properties': ['description']}))
                    if desc.get('description') == 'MC owned shell grain v2':
                        owned_fields.append(node)
            if len(owned_fields) != 1:
                raise RuntimeError('Preserved changed ownership markers')
            original = json.loads(call(O, 'get_properties', {'instance': owned_fields[0], 'properties': ['code']}))['code']
            if original != CODE:
                call(O, 'set_properties', {'instance': owned_fields[0], 'values': json.dumps(dict(code=CODE))})
                call(M, 'recompile', {'material_or_function': mat})
                if not call(A, 'save_assets', {'asset_paths': [PATH]}):
                    raise RuntimeError('Could not save revised grain mask')
            print(json.dumps(dict(asset=PATH, already_authored=True, shader_updated=original != CODE,
                                  compiled=True, saved=True)))
            return
        raise RuntimeError('Preserved an unrecognized existing material graph')
    vertex = nodes[0]
    settings = dict(materialDomain='MD_Surface', blendMode='BLEND_Masked',
                    shadingModel='MSM_DefaultLit', opacityMaskClipValue=.25,
                    twoSided=True)

    def properties(obj, values):
        schema = json.loads(call(O, 'list_properties', {'instance': obj}))
        if not set(values).issubset(schema):
            raise RuntimeError('Engine schema changed: ' + str(set(values)-set(schema)))
        if 'inputs' in values:
            current = json.loads(call(O, 'get_properties', {'instance': obj, 'properties': ['inputs']}))['inputs']
            if len(current) != len(values['inputs']):
                # The MCP array differ requires resizing without renaming the
                # existing elements, then editing names at the stable size.
                resized = current[:len(values['inputs'])]
                resized += [dict(current[0]) for _ in range(len(values['inputs'])-len(resized))]
                call(O, 'set_properties', {'instance': obj, 'values': json.dumps(dict(inputs=resized))})
        if not call(O, 'set_properties', {'instance': obj, 'values': json.dumps(values)}):
            raise RuntimeError('Could not assign properties')

    def add(kind, x, y, values=None):
        choices = call(M, 'list_expression_classes', {'material_or_function': mat, 'search': kind})
        target = {'refPath': '/Script/Engine.MaterialExpression'+kind}
        if target not in choices:
            raise RuntimeError('Expression unavailable: '+kind)
        obj = call(M, 'add_expression', {'material_or_function': mat,
                   'expression_class': target, 'x': x, 'y': y})
        if values:
            properties(obj, values)
        return obj

    before = json.loads(call(O, 'get_properties', {'instance': mat, 'properties': list(settings)}))
    properties(mat, settings)
    uv = add('TextureCoordinate', -700, 200)
    field = add('Custom', -350, 100, dict(description='MC owned shell grain v2', code=CODE,
        outputType='CMOT_Float4', inputs=[dict(inputName='UV'), dict(inputName='Color'), dict(inputName='Alpha')]))
    inputs = call(M, 'get_expression_input_names', {'expression': field})
    if not all(name in inputs for name in ('UV', 'Color', 'Alpha')):
        raise RuntimeError('Custom material inputs changed')
    for source, output, pin in ((uv, '', 'UV'), (vertex, '', 'Color'), (vertex, 'A', 'Alpha')):
        call(M, 'connect_expressions', {'from_expression': source, 'from_output_name': output,
             'to_expression': field, 'to_input_name': pin})
    rgb = add('Custom', 0, 0, dict(code='return Field.rgb;', outputType='CMOT_Float3',
                                  inputs=[dict(inputName='Field')]))
    alpha = add('Custom', 0, 220, dict(code='return Field.a;', outputType='CMOT_Float1',
                                    inputs=[dict(inputName='Field')]))
    for node in (rgb, alpha):
        call(M, 'connect_expressions', {'from_expression': field, 'from_output_name': '',
             'to_expression': node, 'to_input_name': 'Field'})
    rough = add('Constant', 0, 420, dict(r=.94))
    spec = add('Constant', 0, 560, dict(r=.10))
    # A small ambient fill keeps the gap readable in the mouth's colored light.
    fill = add('Custom', 0, 710, dict(code='return Field.rgb * 0.09;', outputType='CMOT_Float3',
                                   inputs=[dict(inputName='Field')]))
    call(M, 'connect_expressions', {'from_expression': field, 'from_output_name': '',
         'to_expression': fill, 'to_input_name': 'Field'})
    for source, target in ((rgb, 'MP_BaseColor'), (alpha, 'MP_OpacityMask'),
                           (rough, 'MP_Roughness'), (spec, 'MP_Specular'), (fill, 'MP_EmissiveColor')):
        call(M, 'connect_to_output', {'expression': source, 'output_name': '', 'material_property': target})
    call(M, 'recompile', {'material_or_function': mat})
    call(A, 'update_metadata_tags', {'asset_path': PATH, 'set_tags': {'MCNutShellSurfaceVersion': '2'}})
    if not call(A, 'save_assets', {'asset_paths': [PATH]}):
        raise RuntimeError('Material could not be saved')
    after = json.loads(call(O, 'get_properties', {'instance': mat, 'properties': list(settings)}))
    if after != settings:
        raise RuntimeError('Saved settings differ')
    report = dict(asset=PATH, before=before, after=after, compiled=True, saved=True,
                  source='Procedural grain and owned native vertex colors; no new textures',
                  visual_validation='Pending solo gameplay camera check')
    dest = Path(__file__).resolve().parents[2]/'Saved/Checks/NutVFXPolish/ShellMaterial.json'
    dest.parent.mkdir(parents=True, exist_ok=True)
    dest.write_text(json.dumps(report, indent=2)+'\n', encoding='utf8')
    print(json.dumps(report))


if __name__ == '__main__':
    main()
