"""Prepare task-only BP variables/functions and record exact node pins."""
from pathlib import Path
import json
from menu_native_client import call, ref, task_asset, BP, OBJ
OUT=Path(__file__).resolve().parent/'Evidence'
w=task_asset('WBP_MainMenu3D');pc=task_asset('BP_Menu3DPlayerController');gm=task_asset('BP_Menu3DGameMode')
existing=set(call(BP,'list_variables',blueprint=w))
for name,kind in [('PageIndex','int'),('ActiveDay','int'),('Open1','float'),('Open2','float'),('Open3','float'),('LastScroll','float'),('HoverQuiet','float')]:
    if name not in existing:call(BP,'add_variable',blueprint=w,name=name,type_name=kind)
for name,cls,array in [('Day1Actors','/Script/Engine.Actor',True),('Day2Actors','/Script/Engine.Actor',True),('Day3Actors','/Script/Engine.Actor',True),('LegacyMenu','/Script/UMG.UserWidget',False)]:
    if name not in existing:
        args=dict(blueprint=w,name=name,object_class=ref(cls))
        if array:args['container_type']='ARRAY'
        call(BP,'add_object_variable',**args)
functions={}
for name,params in [('ShowPage',[('Index','int')]),('SelectDay',[('Day','int')]),('OpenConnection',[]),('Back',[]),('UpdateStage',[]),('FindStageActors',[])]:
    graph=call(BP,'add_function_graph',blueprint=w,graph_name=name);functions[name]=graph
    for pname,kind in params:call(BP,'add_function_param',graph=graph,param_name=pname,param_type=kind,input_param=True)
functions['OnPreviewKeyDown']=call(BP,'add_function_graph',blueprint=w,graph_name='OnPreviewKeyDown')
call(BP,'compile_blueprint',blueprint=w,warnings_as_errors=False)
g=call(BP,'get_graph',blueprint=w,graph_name='EventGraph')
queries=['Clamp','Abs','GetGameTimeInSeconds','SetOverallScalabilityLevel','ApplySettings','SaveSettings','GetFullscreenMode','SetFullscreenMode','GetKey','Equal(Key)','Handled','Unhandled','HasUserFocus','IsValid','Class|WidgetSwitcher|','Variables|Default|Get','Utilities|Casting|CastToSkeletalMeshActor','Array|','GetAllActorsOfClass','GetAllActorswithTag','SetViewTarget']
cat=json.loads((OUT/'node_catalog.json').read_text())
for q in queries:cat[q]=call(BP,'find_node_types',graph=g,type_id_filter=q,context_pins=[])
selected=['UserInterface|CreateWidget','UserInterface|Viewport|AddtoViewport','Input|SetInputModeUIOnly','Class|PlayerController|SetShowMouseCursor',
          'Switcher|SetActiveWidgetIndex','Widget|SetVisibility','Actor|GetAllActorswithTag','Components|SkeletalMesh|SetMorphTarget',
          'Utilities|Casting|CastToSkeletalMeshActor','Class|SkeletalMeshActor|GetSkeletalMeshComponent','Rendering|Material|SetScalarParameterValueonMaterials',
          'Widget|IsHovered','Widget|GetScrollOffset','Widget|SetKeyboardFocus','Settings|GetGameUserSettings','Game|QuitGame','Math|Interpolation|FInterpTo',
          'Layout|SizeBox|SetHeightOverride','Transformation|SetActorLocation','Transformation|SetActorScale3D','Rendering|SetActorHiddenInGame',
          'Widget|ScrollWidgetIntoView','Widget|GetOwningPlayer','Widget|AddChildtoOverlay','Widget|SetText(Text)']
pins={t:call(BP,'get_node_type_pins',graph=g,type_id=t) for t in selected}
(OUT/'logic_functions.json').write_text(json.dumps(functions,indent=2),encoding='utf8')
(OUT/'node_catalog.json').write_text(json.dumps(cat,indent=2),encoding='utf8')
(OUT/'node_pins.json').write_text(json.dumps(pins,indent=2),encoding='utf8')
(OUT/'key_override_initial.dsl').write_text(call(BP,'read_graph_dsl',graph=functions['OnPreviewKeyDown']),encoding='utf8')
print(json.dumps({'functions':functions,'selected_pins':{n:{k:[(p['name'],p['type_id']) for p in v[k]] for k in ('input_pins','output_pins')} for n,v in pins.items()}},indent=2),flush=True)
