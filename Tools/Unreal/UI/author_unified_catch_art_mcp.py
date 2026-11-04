"""Scoped native-MCP authoring for six PSD Catch/wallet glyphs.

Engine Python is used only for fresh, read-only global idle/dirty guards: the MCP
editor-app toolset exposes IsPIERunning but no global dirty-package query. Every
asset import, metadata edit, material graph/MI operation and save uses native MCP.
No PIE, build, GC, loaded-object scan, widget/package rebuild or runtime MID.
"""
import hashlib
import json
import os
import struct
import shutil
import sys
import time
from collections import Counter
from pathlib import Path

PROJECT = Path('E:/DEVGAME/MessControl')
OUT = Path('E:/CodexWork/MessControl/Validation/UICatch_20261104')
TEMP = Path('E:/CodexTemp/MessControl-UICatch')
for directory in (OUT, TEMP):
    directory.mkdir(parents=True, exist_ok=True)
os.environ['TEMP'] = os.environ['TMP'] = str(TEMP)
os.environ['PYTHONDONTWRITEBYTECODE'] = '1'
sys.dont_write_bytecode = True
sys.stdout.reconfigure(encoding='utf-8')
sys.path.insert(0, str(PROJECT / 'Tools/Unreal'))
from native_mcp_client import NativeMCP

GLYPHS = ('Fish', 'FishPair', 'Octopus', 'Chest', 'WalletFish', 'WalletCoin')
ART = '/Game/Gameplay/UI/V2/Art/Catch'
MASTER = '/Game/Gameplay/UI/V2/Materials/M_UI_PSD_WhiteGlyph'
MARKER = 'MessControl PSD WhiteGlyph v1: opacity=GlyphTexture.R*GlyphTexture.A; finalColor=white'
OWNER, DIGEST = 'MCPSDWhiteGlyph.Owner', 'MCPSDWhiteGlyph.SourceSHA256'
A = 'editor_toolset.toolsets.asset.AssetTools'
T = 'editor_toolset.toolsets.texture.TextureTools'
O = 'editor_toolset.toolsets.object.ObjectTools'
M = 'editor_toolset.toolsets.material.MaterialTools'
I = 'editor_toolset.toolsets.material_instance.MaterialInstanceTools'
E = 'EditorToolset.EditorAppToolset'
tex_path = lambda name: ART + '/T_UI_PSD_Catch_' + name
mi_path = lambda name: ART + '/MI_UI_PSD_Catch_' + name
SCOPE = {MASTER} | {tex_path(name) for name in GLYPHS} | {mi_path(name) for name in GLYPHS}
RESUME_OWNED = '--resume-owned' in sys.argv
assert set(sys.argv[1:]) <= {'--resume-owned'}
if RESUME_OWNED and (OUT / 'ArtAuthorReport.json').is_file():
    shutil.copy2(OUT / 'ArtAuthorReport.json', OUT / ('ArtAuthorReport_Prior_' + time.strftime('%Y%m%d_%H%M%S') + '.json'))
report = {'passed': False, 'authoring': 'native MCP', 'scope': sorted(SCOPE),
          'created': [], 'reused': [], 'textures': [], 'instances': [], 'calls': [],
          'pythonFallbacks': ['Read-only global idle/dirty guards only'],
          'runtimeDynamicInstances': False, 'runtimeValidation': 'Not run; root owns WBP/prewarm integration.'}
client = NativeMCP()

def decode(result):
    assert not result.get('isError'), result
    content = [item['text'] for item in result['content'] if item['type'] == 'text']
    assert len(content) == 1, result
    return json.loads(content[0])

def call(group, name, arguments=None):
    payload = decode(client.tool('call_tool', {'toolset_name': group, 'tool_name': name, 'arguments': arguments or {}}))
    report['calls'].append(group + '.' + name)
    return payload['returnValue'] if 'returnValue' in payload else None

def ref(path):
    return {'refPath': path + '.' + path.rsplit('/', 1)[1]}

def asset_path(reference):
    return reference['refPath'].split('.')[0]

def fresh_guard(label):
    assert call(E, 'IsPIERunning') is False, 'Preserve active PIE'
    sys.path.insert(0, 'E:/UE/UE_5.8/Engine/Plugins/Experimental/PythonScriptPlugin/Content/Python')
    import remote_execution as remote
    destination = OUT / ('ArtGuard_' + label + '.json')
    code = "import json\nfrom pathlib import Path\nimport unreal as u\n"
    code += "data={'project':str(Path(u.Paths.project_dir()).resolve()),'pieWorlds':len(u.EditorLevelLibrary.get_pie_worlds(True)),'playing':u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor(),'dirtyContent':[p.get_path_name() for p in u.EditorLoadingAndSavingUtils.get_dirty_content_packages()],'dirtyMaps':[p.get_path_name() for p in u.EditorLoadingAndSavingUtils.get_dirty_map_packages()]}\n"
    code += 'Path(' + repr(str(destination)) + ").write_text(json.dumps(data,indent=2),encoding='utf-8')\n"
    code += "assert Path(data['project'])==Path('E:/DEVGAME/MessControl')\nassert data['pieWorlds']==0 and not data['playing']\nassert not data['dirtyMaps'],data\n"
    allowed_dirty = sorted(SCOPE) if RESUME_OWNED and label == 'Before' else []
    code += "assert set(data['dirtyContent']) <= set(" + repr(allowed_dirty) + "),data\n"
    command = 'def _mc_catch_guard():\n    scope={"__name__":"__main__"}\n    try:\n        exec(' + repr(code) + ',scope)\n    finally:\n        scope.clear()\ntry:\n    _mc_catch_guard()\nfinally:\n    del _mc_catch_guard\n'
    session = remote.RemoteExecution()
    try:
        session.start()
        nodes, deadline = [], time.monotonic() + 8
        while time.monotonic() < deadline:
            nodes = [node for node in session.remote_nodes if node.get('project_name') == 'MessControl']
            if len(nodes) == 1:
                break
            time.sleep(.15)
        assert len(nodes) == 1, 'Expected exactly one MessControl Editor'
        session.open_command_connection(nodes[0]['node_id'])
        session.run_command("exec(" + repr(command) + ", {'__name__':'__main__'})", exec_mode=remote.MODE_EXEC_STATEMENT, unattended=True, raise_on_failure=True)
    finally:
        session.stop()
    return json.loads(destination.read_text(encoding='utf-8'))

schemas = {}
property_schemas = {}

def normalized(value):
    return value.lower().replace('_', '')

def properties(kind, wanted):
    schema = property_schemas[kind]
    result = {}
    for name, value in wanted.items():
        key = next((key for key in schema if normalized(key) == normalized(name)), None)
        assert key, (kind, name, 'Required property missing')
        if 'enum' in schema[key]:
            assert value in schema[key]['enum'], (kind, key, value)
        result[key] = value
    return result

def get_props(obj, kind, names):
    keys = []
    for name in names:
        keys.append(next(key for key in property_schemas[kind] if normalized(key) == normalized(name)))
    return json.loads(call(O, 'get_properties', {'instance': obj, 'properties': keys}))

def set_props(obj, kind, values):
    assert asset_path(obj).split(':')[0] in SCOPE, ('Out of scope', obj)
    desired = properties(kind, values)
    call(O, 'get_properties', {'instance': obj, 'properties': list(desired)})
    assert call(O, 'set_properties', {'instance': obj, 'values': json.dumps(desired)}) is True
    actual = json.loads(call(O, 'get_properties', {'instance': obj, 'properties': list(desired)}))
    for key, value in desired.items():
        assert actual[key] == value, (obj, key, value, actual[key])
    return actual

def mark(path, tags=None):
    assert path in SCOPE
    values = {OWNER: MARKER}
    values.update(tags or {})
    call(A, 'update_metadata_tags', {'asset_path': path, 'set_tags': values})
    actual = call(A, 'get_metadata_tags', {'asset_path': path})
    assert all(actual[key] == value for key, value in values.items()), path

try:
    report['guardBefore'] = fresh_guard('Before')
    for group in (M, I, A, T, O):
        schemas[group] = decode(client.tool('describe_toolset', {'toolset_name': group}))
    (OUT / 'MCPArtToolSchemas.json').write_text(json.dumps(schemas, ensure_ascii=False, indent=2), encoding='utf-8')
    for kind in ('Texture2D', 'Material', 'MaterialExpressionTextureSampleParameter2D', 'MaterialExpressionConstant3Vector', 'MaterialInstanceConstant'):
        property_schemas[kind] = json.loads(call(O, 'list_properties', {'instance': {'refPath': '/Script/Engine.' + kind}}))
    (OUT / 'MCPArtPropertySchemas.json').write_text(json.dumps(property_schemas, ensure_ascii=False, indent=2), encoding='utf-8')
    inputs, existing = {}, {}
    for name in GLYPHS:
        source = PROJECT / ('ArtSource/UI/V2/Icons/Catch/T_UI_PSD_Catch_' + name + '.png')
        data = source.read_bytes()
        assert data[:8] == b'\x89PNG\r\n\x1a\n'
        inputs[name] = {'file': str(source), 'sha256': hashlib.sha256(data).hexdigest(), 'size': list(struct.unpack('>II', data[16:24]))}
    for path in sorted(SCOPE):
        existing[path] = call(A, 'exists', {'path': path})
        if existing[path]:
            assert RESUME_OWNED or call(A, 'is_dirty', {'asset_path': path}) is False, path
            kind = 'Material' if path == MASTER else 'MaterialInstanceConstant' if '/MI_' in path else 'Texture2D'
            assert call(A, 'get_asset_class', {'asset_path': path}) == kind, path
            metadata = call(A, 'get_metadata_tags', {'asset_path': path})
            assert metadata.get(OWNER) == MARKER, ('Preserve unowned asset', path)
            report['reused'].append(path)
    # Validate every setter against live schemas before creating a package.
    texture_settings = {'compressionSettings': 'TC_EditorIcon', 'mipGenSettings': 'TMGS_NoMipmaps',
                        'LODGroup': 'TEXTUREGROUP_UI', 'sRGB': False, 'neverStream': True}
    properties('Texture2D', texture_settings)
    properties('Material', {'materialDomain': 'MD_UI', 'blendMode': 'BLEND_Translucent'})
    print('Fresh idle/clean guards and native MCP schemas: PASS', flush=True)
    textures = {}
    for name in GLYPHS:
        path, source = tex_path(name), inputs[name]
        if not existing[path]:
            imported = call(T, 'import_file', {'folder_path': ART, 'asset_name': path.rsplit('/', 1)[1], 'source_file': source['file']})
            assert len(imported) == 1 and asset_path(imported[0]) == path, imported
            report['created'].append(path)
            mark(path, {DIGEST: source['sha256']})
        else:
            metadata = call(A, 'get_metadata_tags', {'asset_path': path})
            assert metadata[DIGEST] == source['sha256'], ('MCP import_file has no reimport capability; preserve existing source for review', path)
        texture = ref(path)
        actual = set_props(texture, 'Texture2D', texture_settings)
        size = call(T, 'get_size', {'texture': texture})
        assert [size['x'], size['y']] == source['size'], (name, size)
        textures[name] = texture
        report['textures'].append(dict(name=name, asset=path, settings=actual, **source))
        print('Texture ' + name + ': ready', flush=True)
    material = ref(MASTER)
    if not existing[MASTER]:
        folder, name = MASTER.rsplit('/', 1)
        material = call(M, 'create_material', {'folder_path': folder, 'asset_name': name})
        assert asset_path(material) == MASTER
        report['created'].append(MASTER)
        mark(MASTER)
        set_props(material, 'Material', {'materialDomain': 'MD_UI', 'blendMode': 'BLEND_Translucent'})
        nodes = {}
        for kind, x, y in (('MaterialExpressionTextureSampleParameter2D', -600, 0), ('MaterialExpressionMultiply', -280, 120), ('MaterialExpressionConstant3Vector', -280, -100)):
            nodes[kind] = call(M, 'add_expression', {'material_or_function': material, 'expression_class': {'refPath': '/Script/Engine.' + kind}, 'x': x, 'y': y})
        sample, opacity, white = (nodes[kind] for kind in ('MaterialExpressionTextureSampleParameter2D', 'MaterialExpressionMultiply', 'MaterialExpressionConstant3Vector'))
        set_props(sample, 'MaterialExpressionTextureSampleParameter2D', {'parameterName': 'GlyphTexture', 'texture': textures['Fish'], 'samplerType': 'SAMPLERTYPE_LinearColor'})
        set_props(white, 'MaterialExpressionConstant3Vector', {'constant': {'r': 1, 'g': 1, 'b': 1, 'a': 1}})
        for channel, pin in (('R', 'A'), ('A', 'B')):
            call(M, 'connect_expressions', {'from_expression': sample, 'from_output_name': channel, 'to_expression': opacity, 'to_input_name': pin})
        call(M, 'connect_to_output', {'expression': white, 'output_name': '', 'material_property': 'MP_EmissiveColor'})
        call(M, 'connect_to_output', {'expression': opacity, 'output_name': '', 'material_property': 'MP_Opacity'})
    else:
        expressions = call(M, 'get_expressions', {'material_or_function': material})
        assert len(expressions) in (3, 4), 'Preserve edited/partial graph; no automatic rebuild'
        nodes = {call(O, 'get_class', {'instance': expression})['refPath'].rsplit('.', 1)[1]: expression for expression in expressions}
        sample, opacity, white = (nodes[kind] for kind in ('MaterialExpressionTextureSampleParameter2D', 'MaterialExpressionMultiply', 'MaterialExpressionConstant3Vector'))
    expected_nodes = {'MaterialExpressionTextureSampleParameter2D', 'MaterialExpressionMultiply', 'MaterialExpressionConstant3Vector', 'MaterialExpressionReroute'}
    assert set(nodes) <= expected_nodes and len(nodes) in (3, 4), 'Preserve an unrelated graph'
    # Native MCP reader resolves the first pin when two inputs use one source.
    # An ordinary alpha-wire reroute makes per-pin readback unambiguous without
    # changing sample.R * sample.A or requiring a second texture sample.
    alpha = nodes.get('MaterialExpressionReroute')
    if alpha is None:
        alpha = call(M, 'add_expression', {'material_or_function': material, 'expression_class': {'refPath': '/Script/Engine.MaterialExpressionReroute'}, 'x': -440, 'y': 210})
    alpha_inputs = call(M, 'get_expression_input_names', {'expression': alpha})
    assert len(alpha_inputs) == 1, alpha_inputs
    call(M, 'connect_expressions', {'from_expression': sample, 'from_output_name': 'A', 'to_expression': alpha, 'to_input_name': alpha_inputs[0]})
    call(M, 'connect_expressions', {'from_expression': alpha, 'from_output_name': '', 'to_expression': opacity, 'to_input_name': 'B'})
    report['graph'] = {'expressions': call(M, 'get_expressions', {'material_or_function': material}),
        'finalColor': call(M, 'get_property_input', {'material': material, 'material_property': 'MP_EmissiveColor'}),
        'opacity': call(M, 'get_property_input', {'material': material, 'material_property': 'MP_Opacity'}),
        'multiplyInputs': call(M, 'get_expression_inputs', {'material_or_function': material, 'expression': opacity})}
    assert report['graph']['finalColor']['expression'] == white
    assert report['graph']['opacity']['expression'] == opacity
    links = {link['input_name']: (link['expression'], link['output_name']) for link in report['graph']['multiplyInputs']}
    assert links['A'] == (sample, 'R') and links['B'][0] == alpha, links
    alpha_links = call(M, 'get_expression_inputs', {'material_or_function': material, 'expression': alpha})
    assert len(alpha_links) == 1 and alpha_links[0]['expression'] == sample and alpha_links[0]['output_name'] == 'A', alpha_links
    report['graph']['alphaRerouteInputs'] = alpha_links
    report['graph']['readerWorkaround'] = 'Native get_expression_inputs cannot distinguish two output pins from one source; alpha-wire reroute preserves R*A and makes verification unambiguous.'
    call(M, 'recompile', {'material_or_function': material})
    report['material'] = {'asset': MASTER, 'domain': 'MD_UI', 'blendMode': 'BLEND_Translucent', 'parameter': 'GlyphTexture', 'opacity': 'sample.R * sample.A', 'finalColor': 'white', 'compile': 'Native MCP recompile passed'}
    print('Shared WhiteGlyph graph and native recompile: PASS', flush=True)
    for name in GLYPHS:
        path = mi_path(name)
        if not existing[path]:
            instance = call(I, 'create', {'folder_path': ART, 'asset_name': path.rsplit('/', 1)[1], 'parent': material})
            assert asset_path(instance) == path
            report['created'].append(path)
            mark(path)
        else:
            instance = ref(path)
        parent = get_props(instance, 'MaterialInstanceConstant', ['parent'])['parent']
        assert parent == material, (path, parent)
        call(I, 'set_texture_parameter', {'instance': instance, 'name': 'GlyphTexture', 'value': textures[name]})
        assert call(I, 'get_texture_parameter', {'instance': instance, 'name': 'GlyphTexture'}) == textures[name]
        report['instances'].append({'name': name, 'asset': path, 'parent': MASTER, 'texture': tex_path(name)})
        print('Static MI ' + name + ': ready', flush=True)
    assert call(A, 'save_assets', {'asset_paths': sorted(SCOPE)}) is True
    assert all(call(A, 'is_dirty', {'asset_path': path}) is False for path in sorted(SCOPE))
    report['guardAfter'] = fresh_guard('After')
    report['passed'] = True
except Exception as error:
    report['error'] = str(error)
    raise
finally:
    report['callCounts'] = dict(Counter(report['calls']))
    (OUT / 'ArtAuthorReport.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    print(json.dumps({'passed': report['passed'], 'created': len(report['created']), 'reused': len(report['reused']), 'report': str(OUT / 'ArtAuthorReport.json')}, ensure_ascii=False), flush=True)
