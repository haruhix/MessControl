"""Author the three wind materials through Epic's native Material/Object tools.

Run with the editor open and PIE stopped. Existing materials are preserved unless
--rebuild deliberately replaces their graphs. HLSL comes from build_throat_vortex.py.
"""
import argparse
import ast
import json
from pathlib import Path

from native_mcp_client import NativeMCP


def shader_plans():
    tree = ast.parse(Path(__file__).with_name('build_throat_vortex.py').read_text())
    emission_code = next(ast.literal_eval(n.args[2]) for n in ast.walk(tree)
                         if isinstance(n, ast.Call) and isinstance(n.func, ast.Name)
                         and n.func.id == 'custom' and len(n.args) > 2
                         and isinstance(n.args[1], ast.Constant)
                         and n.args[1].value == 'Neutral wind shading without additive glow')
    plans = []
    for item in tree.body:
        if not isinstance(item, ast.Expr) or not isinstance(item.value, ast.Call):
            continue
        call = item.value
        if not isinstance(call.func, ast.Name) or call.func.id != 'build':
            continue
        plans.append(dict(name=ast.literal_eval(call.args[0]), code=ast.literal_eval(call.args[1]), emission_code=emission_code,
                          depth_fade=next(ast.literal_eval(k.value) for k in call.keywords if k.arg == 'depth_fade')))
    assert len(plans) == 3
    return plans


SCRIPT = r'''
import json
M = "editor_toolset.toolsets.material.MaterialTools."
O = "editor_toolset.toolsets.object.ObjectTools."
A = "editor_toolset.toolsets.asset.AssetTools."

def call(name, args):
    return execute_tool(name, json.dumps(args))

def get_properties(obj, names):
    return json.loads(call(O+"get_properties", {"instance": obj, "properties": names})["returnValue"])

def set_properties(obj, values):
    available = json.loads(call(O+"list_properties", {"instance": obj})["returnValue"])
    if not all(key in available for key in values):
        raise RuntimeError("Unknown property: "+str(values))
    if "inputs" in values:
        current = get_properties(obj, ["inputs"])["inputs"]
        wanted = values["inputs"]
        if len(current) != len(wanted):
            resized = current[:len(wanted)] + wanted[len(current):]
            if not call(O+"set_properties", {"instance": obj, "values": json.dumps({"inputs": resized})})["returnValue"]:
                raise RuntimeError("Cannot resize custom inputs")
    if not call(O+"set_properties", {"instance": obj, "values": json.dumps(values)})["returnValue"]:
        raise RuntimeError("Cannot set properties: "+str(obj))

def expressions(mat):
    return call(M+"get_expressions", {"material_or_function": mat})["returnValue"]

def remove(mat, expr):
    call(M+"delete_expression", {"material_or_function": mat, "expression": expr})

def node(mat, kind, x, y, props):
    obj = call(M+"add_expression", {"material_or_function": mat,
        "expression_class": {"refPath": "/Script/Engine.MaterialExpression"+kind}, "x": x, "y": y})["returnValue"]
    if props:
        set_properties(obj, props)
    return obj

def connect(source, output, target, pin):
    pins = call(M+"get_expression_input_names", {"expression": target})["returnValue"]
    outputs = call(M+"get_expression_output_names", {"expression": source})["returnValue"]
    if pin not in pins or (output and output not in outputs):
        raise RuntimeError("Invalid material pins: "+str([output,pin,outputs,pins]))
    call(M+"connect_expressions", {"from_expression": source, "from_output_name": output,
        "to_expression": target, "to_input_name": pin})

def output(expr, prop):
    call(M+"connect_to_output", {"expression": expr, "output_name": "", "material_property": prop})

def compile_material(mat):
    # Native recompile raises on shader failure; save only after it succeeds.
    call(M+"recompile", {"material_or_function": mat})

def save(paths):
    if not call(A+"save_assets", {"asset_paths": paths})["returnValue"]:
        raise RuntimeError("Material save failed")

def run():
    reports, saved = [], []
    settings = {"blendMode": "BLEND_Translucent", "shadingModel": "MSM_Unlit",
                "twoSided": True, "bDisableDepthTest": False, "materialDomain": "MD_Surface"}
    for plan in PLANS:
        path = "/Game/Gameplay/VFX/Throat/"+plan["name"]
        exists = call(A+"exists", {"path": path})["returnValue"]
        if exists and not REBUILD:
            reports.append({"path": path, "preserved": True})
            continue
        if exists:
            mat = call(A+"load_asset", {"asset_path": path})["returnValue"]
        else:
            mat = call(M+"create_material", {"folder_path": "/Game/Gameplay/VFX/Throat", "asset_name": plan["name"]})["returnValue"]
        for expr in expressions(mat):
            remove(mat, expr)
        set_properties(mat, settings)
        uv = node(mat, "TextureCoordinate", -1100, 0, {})
        color = node(mat, "VertexColor", -1100, 180, {})
        age = node(mat, "ScalarParameter", -1100, 380, {"parameterName": "VortexAge", "defaultValue": 0, "group": "Intake Vortex"})
        opacity = node(mat, "ScalarParameter", -1100, 520, {"parameterName": "Opacity", "defaultValue": 1, "group": "Intake Vortex"})
        field = node(mat, "Custom", -550, 0, {"description": "Soft turbulent wind with irregular feathered edges",
            "code": plan["code"], "outputType": "CMOT_Float2", "inputs": [{"inputName": "UV"}, {"inputName": "Age"}]})
        emission = node(mat, "Custom", -200, -100, {"description": "Neutral wind shading without additive glow",
            "code": plan["emission_code"], "outputType": "CMOT_Float3", "inputs": [{"inputName": "Tint"}, {"inputName": "F"}]})
        alpha = node(mat, "Custom", -200, 150, {"description": "Section, particle, and synchronized meal envelope",
            "code": "return saturate(F.x * Alpha * Envelope);", "outputType": "CMOT_Float1",
            "inputs": [{"inputName": "F"}, {"inputName": "Alpha"}, {"inputName": "Envelope"}]})
        fade = node(mat, "DepthFade", 120, 150, {"fadeDistanceDefault": plan["depth_fade"]})
        edges = [(uv,"",field,"UV"),(age,"",field,"Age"),(color,"",emission,"Tint"),(field,"",emission,"F"),
                 (field,"",alpha,"F"),(color,"A",alpha,"Alpha"),(opacity,"",alpha,"Envelope"),(alpha,"",fade,"Opacity")]
        for source,pin,target,input_name in edges:
            connect(source,pin,target,input_name)
        output(emission, "MP_EmissiveColor")
        output(fade, "MP_Opacity")
        # Read actual native graph links, rather than assuming connection calls worked.
        for source,pin,target,input_name in edges:
            links = call(M+"get_expression_inputs", {"material_or_function": mat, "expression": target})["returnValue"]
            link = next(value for value in links if value["input_name"] == input_name)
            if link["expression"] != source:
                raise RuntimeError("Material graph link differs: "+str(link))
        for expr,prop in [(emission,"MP_EmissiveColor"),(fade,"MP_Opacity")]:
            if call(M+"get_property_input", {"material": mat, "material_property": prop})["returnValue"]["expression"] != expr:
                raise RuntimeError("Material output disconnected: "+prop)
        if get_properties(mat, list(settings)) != settings:
            raise RuntimeError("Material settings differ")
        if get_properties(field, ["code"])["code"] != plan["code"]:
            raise RuntimeError("Authored shader differs from source")
        compile_material(mat)
        saved.append(path)
        reports.append({"path": mat["refPath"], "preserved": False, "compiled": True,
                        "graph_verified": True, "expressions": len(expressions(mat)), "settings": settings,
                        "depth_fade": plan["depth_fade"]})
    if saved:
        save(saved)
    return {"materials": reports, "saved_assets": saved, "saved": True, "native_tools": True,
            "palette": "neutral white / gray", "sections": 3, "max_food_wakes": 24}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--rebuild', action='store_true', help='Deliberately replace existing wind material graphs.')
    args = parser.parse_args()
    client = NativeMCP()

    def invoke(toolset, name, arguments):
        response = client.tool('call_tool', dict(toolset_name=toolset, tool_name=name, arguments=arguments))
        if response.get('isError'):
            raise RuntimeError(response)
        payload = json.loads(response['content'][0]['text'])
        if payload.get('error'):
            raise RuntimeError(payload)
        return payload.get('returnValue')

    if invoke('EditorToolset.EditorAppToolset', 'IsPIERunning', {}):
        raise RuntimeError('Stop PIE before authoring wind materials.')
    for toolset in ['editor_toolset.toolsets.material.MaterialTools', 'editor_toolset.toolsets.object.ObjectTools',
                    'editor_toolset.toolsets.asset.AssetTools', 'editor_toolset.toolsets.programmatic.ProgrammaticToolset']:
        schema = client.tool('describe_toolset', {'toolset_name': toolset})
        if schema.get('isError'):
            raise RuntimeError(schema)
    invoke('editor_toolset.toolsets.programmatic.ProgrammaticToolset', 'get_execution_environment', {})
    script = SCRIPT.replace('PLANS', repr(shader_plans())).replace('REBUILD', repr(args.rebuild))
    result = invoke('editor_toolset.toolsets.programmatic.ProgrammaticToolset', 'execute_tool_script', {'script': script})
    report = json.loads(result)
    destination = Path(__file__).resolve().parents[2] / 'Saved' / 'ThroatVortexMCPBuildResult.json'
    destination.write_text(json.dumps(report, indent=2))
    print('MC_THROAT_VORTEX_MCP_PASS '+json.dumps(report))


if __name__ == '__main__':
    main()
