"""Create/verify the shield fracture vertex-color material through the live Epic MCP.

Only this generated material is edited. Existing custom graphs are preserved.
Native curved ribbons supply the fissure colors and silhouette; the material
keeps those colors unlit instead of turning every crack into a white beam.
"""
import json
from pathlib import Path

from native_mcp_client import NativeMCP


PATH = '/Game/Gameplay/VFX/NutCombat/M_NutShieldFracture'
MATERIAL = 'editor_toolset.toolsets.material.MaterialTools'
OBJECT = 'editor_toolset.toolsets.object.ObjectTools'
ASSET = 'editor_toolset.toolsets.asset.AssetTools'


def main():
    client = NativeMCP()

    def call(group, name, arguments):
        result = client.tool('call_tool', dict(toolset_name=group, tool_name=name, arguments=arguments))
        if result.get('isError'):
            raise RuntimeError(result)
        return json.loads(result['content'][0]['text'])['returnValue']

    if call('EditorToolset.EditorAppToolset', 'IsPIERunning', {}):
        raise RuntimeError('Finish the owned PIE before authoring a saved material')
    exists = call(ASSET, 'exists', {'path': PATH})
    material = call(ASSET, 'load_asset', {'asset_path': PATH}) if exists else call(MATERIAL, 'create_material',
        {'folder_path': PATH.rsplit('/', 1)[0], 'asset_name': PATH.rsplit('/', 1)[1]})
    expressions = call(MATERIAL, 'get_expressions', {'material_or_function': material})
    if expressions:
        if len(expressions) != 1 or call(OBJECT, 'get_class', {'instance': expressions[0]})['refPath'] != '/Script/Engine.MaterialExpressionVertexColor':
            raise RuntimeError('Preserved an existing custom fracture material graph intact')
        vertex = expressions[0]
    else:
        vertex = call(MATERIAL, 'add_expression', {'material_or_function': material,
            'expression_class': {'refPath': '/Script/Engine.MaterialExpressionVertexColor'}, 'x': -350, 'y': 20})
    schema = json.loads(call(OBJECT, 'list_properties', {'instance': material}))
    settings = dict(materialDomain='MD_Surface', blendMode='BLEND_Masked', shadingModel='MSM_Unlit',
                    opacityMaskClipValue=.25, twoSided=True)
    if not set(settings).issubset(schema):
        raise RuntimeError('The live engine material schema changed')
    before = json.loads(call(OBJECT, 'get_properties', {'instance': material, 'properties': list(settings)}))
    for name in ('MP_EmissiveColor', 'MP_OpacityMask'):
        source = call(MATERIAL, 'get_property_input', {'material': material, 'material_property': name})['expression']
        if isinstance(source, dict) and source != vertex:
            raise RuntimeError('Preserved an existing custom fracture output intact: ' + name)
    call(OBJECT, 'set_properties', {'instance': material, 'values': json.dumps(settings)})
    names = call(MATERIAL, 'get_expression_output_names', {'expression': vertex})
    if '' not in names or 'A' not in names:
        raise RuntimeError('Vertex color outputs changed')
    for output, target in (('', 'MP_EmissiveColor'), ('A', 'MP_OpacityMask')):
        call(MATERIAL, 'connect_to_output', {'expression': vertex, 'output_name': output, 'material_property': target})
    call(MATERIAL, 'recompile', {'material_or_function': material})
    call(ASSET, 'update_metadata_tags', {'asset_path': PATH, 'set_tags': {'MCNutShieldFractureVersion': '1'}})
    if not call(ASSET, 'save_assets', {'asset_paths': [PATH]}):
        raise RuntimeError('Could not save the shield fracture material')
    after = json.loads(call(OBJECT, 'get_properties', {'instance': material, 'properties': list(settings)}))
    if after != settings:
        raise RuntimeError('Material settings did not survive readback')
    report = dict(asset=PATH, created=not exists, before=before, after=after,
                  compiled=True, saved=True, color='Native vertex RGB; alpha mask, no arcane beam tint',
                  visual_validation='Requires actual shield damage footage')
    destination = Path(__file__).resolve().parents[2]/'Saved/Checks/NutFightRedesign_20261013/ShieldFractureMaterial.json'
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(json.dumps(report, indent=2)+'\n', encoding='utf8')
    print(json.dumps(report))


if __name__ == '__main__':
    main()
