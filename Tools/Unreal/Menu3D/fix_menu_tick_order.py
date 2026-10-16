"""Narrow native PC/hover fix. Run only after root confirms PIE stopped.

Does not author UpdateStage, level, original characters, meshes or actions.
"""
from pathlib import Path
import hashlib
import json
import urllib.request
from menu_native_client import call, task_asset, CLIENT, BP, ASSET, BASE

ROOT=Path(__file__).resolve().parents[3]
OUT=Path(__file__).resolve().parent/'Evidence'
w=task_asset('WBP_MainMenu3D')
pc=task_asset('BP_Menu3DPlayerController')

def write(graph,code):
    CLIENT.sequence+=1
    payload={'jsonrpc':'2.0','id':CLIENT.sequence,'method':'tools/call','params':{
        'name':'call_tool','arguments':{'toolset_name':BP,'tool_name':'write_graph_dsl','arguments':{'graph':graph,'code':code}}}}
    headers={'Content-Type':'application/json','Accept':'application/json, text/event-stream',
             'Mcp-Session-Id':CLIENT.session,'MCP-Protocol-Version':CLIENT.version}
    request=urllib.request.Request('http://127.0.0.1:8000/mcp',json.dumps(payload).encode(),headers)
    with urllib.request.urlopen(request,timeout=180) as response:
        if 'text/event-stream' in response.headers.get('Content-Type',''):
            for line in response:
                if line.startswith(b'data: '):
                    result=json.loads(line[6:])
                    if result.get('id')==CLIENT.sequence:break
        else:result=json.load(response)
    if 'error' in result:raise RuntimeError(result['error'])
    if result['result'].get('isError'):raise RuntimeError(result['result'])

pc_code=(OUT/'accordion_PC_EventGraph.dsl').read_text(encoding='utf8')
setup=[]
mouth_tags=[f'Menu3D_DAY0{i}' for i in range(1,8)]
character_tags=[f'Menu3D_DAY0{i}_Character' for i in range(1,4)]
for i,tag in enumerate(mouth_tags+character_tags):
    setup+=[f'  (bind targets{i} (Actor|GetAllActorswithTag :Tag "{tag}"))',
            f'  (for actor targets{i}',
            '    (bind sk (Utilities|Casting|CastToSkeletalMeshActor :Object actor)',
            '      (:then (bind mesh (Class|SkeletalMeshActor|GetSkeletalMeshComponent :self sk))']
    if i<7:
        setup+=['        (Class|SkinnedMeshComponent|SetVisibilityBasedAnimTickOption :self mesh :VisibilityBasedAnimTickOption "AlwaysTickPoseAndRefreshBones")',
                '        (Class|SkinnedMeshComponent|SetEnableUpdateRateOptimizations :self mesh :bEnableUpdateRateOptimizations false)']
    setup+=['        (Components|Tick|AddTickPrerequisiteActor :self mesh :PrerequisiteActor self))',
            '      (:CastFailed)))']
if 'AddTickPrerequisiteActor' not in pc_code:
    marker='  (bind cameras '
    assert marker in pc_code
    pc_code=pc_code.replace(marker,'\n'.join(setup)+'\n'+marker,1)
assert pc_code.count('(event AddEvent|EventTick ')==1
assert pc_code.count('Components|Tick|AddTickPrerequisiteActor')==10
move='''(fn OnMouseMove (MyGeometry MouseEvent)
  (bind delta (Input|PointerEvent|GetCursorDelta :Input MouseEvent))
  (bind (dx dy) (Math|Vector2D|BreakVector2D :InVec delta))
  (if (> (+ (* dx dx) (* dy dy)) 0.0001)
    (Variables|Default|SetQueuedPointerScreen (Input|PointerEvent|GetScreenSpacePosition :Input MouseEvent))
    (Variables|Default|SetHasPointerMotion true))
  (return (Widget|EventReply|Unhandled))
)'''

# CPU-only provenance for source contracts intentionally not rewritten here.
protected_files=['accordion_UpdateStage.dsl','accordion_DriveMouthDay.dsl','accordion_OnMouseWheel.dsl',
                 'accordion_UpdatePointerIntent.dsl','accordion_OpenConnection.dsl']
before={n:hashlib.sha256((OUT/n).read_bytes()).hexdigest() for n in protected_files}
write(call(BP,'get_graph',blueprint=pc,graph_name='EventGraph'),pc_code)
print('PC_EVENTGRAPH_WRITTEN',flush=True)
write(call(BP,'get_graph',blueprint=w,graph_name='OnMouseMove'),move)
print('ONMOUSEMOVE_WRITTEN',flush=True)
for asset in [pc,w]:call(BP,'compile_blueprint',blueprint=asset,warnings_as_errors=True)
call(ASSET,'save_assets',asset_paths=[BASE+'/BP_Menu3DPlayerController',BASE+'/WBP_MainMenu3D'])
(OUT/'accordion_PC_EventGraph.dsl').write_text(pc_code,encoding='utf8')
(OUT/'accordion_OnMouseMove.dsl').write_text(move,encoding='utf8')
assert before=={n:hashlib.sha256((OUT/n).read_bytes()).hexdigest() for n in protected_files}
packages=[]
for name in ['BP_Menu3DPlayerController','WBP_MainMenu3D']:
    path=ROOT/f'Content/UI/MainMenu3D/{name}.uasset'
    packages.append({'asset':BASE+'/'+name,'bytes':path.stat().st_size,'sha256':hashlib.sha256(path.read_bytes()).hexdigest()})
report={
    'status':'TICK_ORDER_AND_ZERO_DELTA_HOVER_COMPILED_SAVED_PENDING_ROOT_PIE',
    'only_authored_graphs':['BP_Menu3DPlayerController:EventGraph','WBP_MainMenu3D:OnMouseMove'],
    'mouth_tags':mouth_tags,
    'mouth_runtime_policy':{'tick_prerequisite_actor':'owning menu PlayerController','visibility_based_anim_tick_option':'AlwaysTickPoseAndRefreshBones','enable_update_rate_optimizations':False},
    'character_tags_dependency_only':character_tags,
    'character_animation_mesh_action_assets_changed':False,
    'pointer_delta_gate':'OnMouseMove queues only event GetCursorDelta length>0.01 desktop pixel; existing accumulated viewport-motion threshold>2px retained, so slow real movement can accumulate.',
    'synthetic_zero_delta_events':'Do not queue a new pending hover; ReturnUnhandled retained.',
    'normal_input_assumption':'A nonzero event cursor delta indicates pointer motion. Actual resize/hover tests remain root-owned.',
    'source_evidence':[
        'Engine/Private/Components/SkeletalMeshComponent.cpp:4011 SetMorphTarget changes curve map without immediate refresh',
        'Engine/Private/Components/SkeletalMeshComponent.cpp:4819 RefreshMorphTargets consumes override curves',
        'Engine/Private/Components/ActorComponent.cpp:2613 AddTickPrerequisiteActor orders the component after the actor',
        'Engine/Classes/Components/SkinnedMeshComponent.h:793,932 menu runtime optimization properties'
    ],
    'compiled_warnings_as_errors':True,'saved_packages':packages,
    'protected_author_source_hashes_unchanged':before,
    'PIE_input_level_Git_operations':False,
    'validation':'Actual graph authoring, Blueprint compile and package save only. No runtime acceptance claim.'
}
(OUT/'menu_tick_order_compile_report.json').write_text(json.dumps(report,indent=2),encoding='utf8')
print(json.dumps(report,indent=2),flush=True)
