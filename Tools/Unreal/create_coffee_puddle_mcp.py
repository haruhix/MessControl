"""Send the puddle graph to Epic's native toolsets. No third-party Unreal server."""
import ast,json,sys
from pathlib import Path
from native_mcp_client import NativeMCP

source=ast.parse(Path('Tools/Unreal/create_coffee_puddle.py').read_text())
shader=next(ast.literal_eval(n.value) for n in source.body if isinstance(n,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='shader' for t in n.targets))
normal_tail=next(ast.literal_eval(n.value.right) for n in source.body if isinstance(n,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='normal_code' for t in n.targets))
sheen_code=next(ast.literal_eval(n.value) for n in source.body if isinstance(n,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='sheen_code' for t in n.targets))
nodes=[]; edges=[]; outputs=[]
def node(key,kind,props=None):
    nodes.append(dict(key=key,kind=kind,props=props or {})); return key
def scalar(key,name,value): return node(key,'ScalarParameter',dict(parameterName=name,defaultValue=value,group='Liquid'))
def vector(key,name,value): return node(key,'VectorParameter',dict(parameterName=name,defaultValue=dict(zip('rgba',value)),group='Liquid'))
def custom(key,code,inputs,typ='Float1'):
    node(key,'Custom',dict(description=key,code=code,outputType='CMOT_'+typ,inputs=[dict(inputName=k) for k in inputs]))
    for pin,(src,output) in inputs.items(): edges.append([src,output,key,pin])
    return key
node('uv','TextureCoordinate')
node('mask','TextureObjectParameter',dict(parameterName='WipeMask',texture={'refPath':'/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture'},samplerType='SAMPLERTYPE_LinearColor'))
node('time','Time'); node('vn','VertexNormalWS')
common={'UV':('uv',''),'Wipe':('mask',''),'Time':('time',''),
        'Seed':(scalar('seed','Seed',1),''),'Finish':(scalar('finish','Finish',0),''),'Age':(scalar('age','BrushAge',100),''),
        'Brush':(vector('brush','Brush',(.5,.5,1,0)),'RGBA'),'Depth':(scalar('depth','Depth',2.6),''),'Rim':(scalar('rim','Rim',.8),''),'WorldSize':(scalar('worldsize','WorldSize',184),'')}
custom('field',shader+'return S.surface(UV,Wipe,WipeSampler,Seed,Finish,Age,Brush,Time,Depth,Rim);',common,'Float4')
custom('opacity','return F.g;',{'F':('field','')}); outputs.append(['opacity','MP_OpacityMask'])
custom('normal',shader+normal_tail,common,'Float3'); outputs.append(['normal','MP_Normal'])
custom('height','return N*F.x;',{'F':('field',''),'N':('vn','')},'Float3'); outputs.append(['height','MP_WorldPositionOffset'])
vector('tint','LiquidColor',(.24,.075,.011,1)); vector('edge','EdgeColor',(.40,.15,.03,1))
custom('color','return lerp(Edge,Body,saturate(F.w*.85+.15))*(1+F.z*.12)*(1+.06*sin(Seed*4.137));',{'F':('field',''),'Body':('tint',''),'Edge':('edge',''),'Seed':('seed','')},'Float3'); outputs.append(['color','MP_BaseColor'])
for key,name,value,prop in [('rough','Roughness',.10,'MP_Roughness'),('spec','Specular',.85,'MP_Specular')]:
    scalar(key,name,value); outputs.append([key,prop])
node('view','CameraVectorWS'); node('pixelnormal','PixelNormalWS')
custom('sheen',sheen_code,{'View':('view',''),'Normal':('pixelnormal',''),'Strength':(scalar('wet','WetSheen',.45),'')},'Float3')
outputs.append(['sheen','MP_EmissiveColor'])
plan=json.dumps(dict(nodes=nodes,edges=edges,outputs=outputs))
script='''import json
M="editor_toolset.toolsets.material.MaterialTools."
O="editor_toolset.toolsets.object.ObjectTools."
def call(name,args): return execute_tool(name,json.dumps(args))
def expressions(mat): return call(M+"get_expressions",{"material_or_function":mat})["returnValue"]
def remove(mat,expr): call(M+"delete_expression",{"material_or_function":mat,"expression":expr})
def create(mat,kind,x,y): return call(M+"add_expression",{"material_or_function":mat,"expression_class":{"refPath":"/Script/Engine.MaterialExpression"+kind},"x":x,"y":y})["returnValue"]
def properties(obj,values):
    if "inputs" in values:
        current=json.loads(call(O+"get_properties",{"instance":obj,"properties":["inputs"]})["returnValue"])["inputs"]
        if len(current)!=len(values["inputs"]):
            resized=list(current)+values["inputs"][len(current):]
            call(O+"set_properties",{"instance":obj,"values":json.dumps({"inputs":resized})})
    ok=call(O+"set_properties",{"instance":obj,"values":json.dumps(values)})["returnValue"]
    if not ok: raise RuntimeError("set_properties failed: "+str(obj))
def connect(a,ap,b,bp): call(M+"connect_expressions",{"from_expression":a,"from_output_name":ap,"to_expression":b,"to_input_name":bp})
def output(expr,prop): call(M+"connect_to_output",{"expression":expr,"output_name":"","material_property":prop})
def run():
    mat={"refPath":"/Game/Gameplay/Liquid/M_CoffeePuddle.M_CoffeePuddle"}
    if not call("editor_toolset.toolsets.asset.AssetTools.exists",{"path":mat["refPath"]})["returnValue"]:
        mat=call(M+"create_material",{"folder_path":"/Game/Gameplay/Liquid","asset_name":"M_CoffeePuddle"})["returnValue"]
    for expr in expressions(mat): remove(mat,expr)
    properties(mat,{"blendMode":"BLEND_Masked","shadingModel":"MSM_DefaultLit","twoSided":False,"opacityMaskClipValue":.35})
    plan=json.loads(PLAN)
    made={}
    for i,n in enumerate(plan["nodes"]):
        obj=create(mat,n["kind"],(i%4)*450,(i//4)*200)
        if n["props"]: properties(obj,n["props"])
        made[n["key"]]=obj
    for a,ap,b,bp in plan["edges"]: connect(made[a],ap,made[b],bp)
    for a,p in plan["outputs"]: output(made[a],p)
    call(M+"recompile",{"material_or_function":mat})
    mi="/Game/Gameplay/Liquid/MI_CoffeePuddle"
    if not call("editor_toolset.toolsets.asset.AssetTools.exists",{"path":mi})["returnValue"]:
        call("editor_toolset.toolsets.material_instance.MaterialInstanceTools.create",{"folder_path":"/Game/Gameplay/Liquid","asset_name":"MI_CoffeePuddle","parent":mat})
    saved=[mat["refPath"],mi]
    for name,body,edge,rough,depth in [("MI_CurryPuddle",[.44,.17,.009,1],[.62,.31,.025,1],.19,1.8),("MI_FoodSaucePuddle",[.36,.028,.012,1],[.52,.07,.022,1],.2,2.2)]:
        path="/Game/Gameplay/Liquid/"+name
        if not call("editor_toolset.toolsets.asset.AssetTools.exists",{"path":path})["returnValue"]:
            preset=call("editor_toolset.toolsets.material_instance.MaterialInstanceTools.create",{"folder_path":"/Game/Gameplay/Liquid","asset_name":name,"parent":mat})["returnValue"]
            for parameter,value in [("LiquidColor",body),("EdgeColor",edge)]:
                call("editor_toolset.toolsets.material_instance.MaterialInstanceTools.set_vector_parameter",{"instance":preset,"name":parameter,"value":dict(zip("rgba",value))})
            for parameter,value in [("Roughness",rough),("Depth",depth)]:
                call("editor_toolset.toolsets.material_instance.MaterialInstanceTools.set_scalar_parameter",{"instance":preset,"name":parameter,"value":value})
        saved.append(path)
    call("editor_toolset.toolsets.asset.AssetTools.save_assets",{"asset_paths":saved})
    return {"nodes":made,"count":len(made)}
'''.replace('PLAN',repr(plan))
c=NativeMCP()
state=c.tool('call_tool',dict(toolset_name='EditorToolset.EditorAppToolset',tool_name='IsPIERunning',arguments={}))
if state.get('isError'): raise RuntimeError(state)
if json.loads(state['content'][0]['text'])['returnValue']:
    raise RuntimeError('Stop Play-In-Editor before rebuilding material assets.')
c.tool('describe_toolset',{'toolset_name':'editor_toolset.toolsets.programmatic.ProgrammaticToolset'})
c.tool('call_tool',dict(toolset_name='editor_toolset.toolsets.programmatic.ProgrammaticToolset',tool_name='get_execution_environment',arguments={}))
r=c.tool('call_tool',dict(toolset_name='editor_toolset.toolsets.programmatic.ProgrammaticToolset',tool_name='execute_tool_script',arguments=dict(script=script)))
Path('Saved/PuddleBuildResult.json').write_text(json.dumps(r))
if r.get('isError'): raise RuntimeError(r)
print('MC_COFFEE_PUDDLE_MCP_PASS; details: Saved/PuddleBuildResult.json')
