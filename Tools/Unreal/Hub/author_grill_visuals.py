"""Author the finite grill/consumable materials, editable hologram, and measured assembly links.

Run only with current native classes, stopped PIE and no dirty owned packages.
Every write stays on E: and a copy of each existing owned package is retained.
"""
from pathlib import Path
import json,hashlib,shutil,time,sys,math
import unreal as u

ROOT=Path('E:/DEVGAME/MessControl')
OUT=Path('E:/CodexWork/HubSystems_20261023/GrillAuthoring')/time.strftime('%Y%m%d_%H%M%S')
DEST='/Game/Gameplay/Hub/Economy'
OUT.mkdir(parents=True,exist_ok=False)
lib=u.EditorAssetLibrary;edit=u.MaterialEditingLibrary;assets=u.AssetToolsHelpers.get_asset_tools()
assert not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor()
dirty={p.get_path_name() for p in list(u.EditorLoadingAndSavingUtils.get_dirty_content_packages())+list(u.EditorLoadingAndSavingUtils.get_dirty_map_packages())}
world=u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
assert world and world.get_path_name().split('.')[0]=='/Game/Maps/Hub','Open Hub before authoring.'
assert '/Game/Maps/Hub' not in dirty,'Unsaved Hub preserved.'
assert hasattr(u,'MCHubGrillHologramWidget') and hasattr(u,'MCHubGrillStation'),'Build current Editor classes first.'
all_actors=u.get_editor_subsystem(u.EditorActorSubsystem).get_all_level_actors()
for required in ['SM_Grill_Bot_007.001','SM_Grill_Bot_007.002','SM_Roulette_1','SM_Roulette_2','SM_Roulette_3']:
    assert sum(a.get_actor_label()==required for a in all_actors)==1,'Missing/ambiguous assembly part: '+required
report={'backups':[],'saved':[],'angles':{}}
def backup(package):
    assert package not in dirty,'Unsaved package preserved: '+package
    for suffix in ('.uasset','.umap','.uexp','.ubulk'):
        source=Path(str(ROOT/'Content'/package.removeprefix('/Game/'))+suffix)
        if not source.is_file():continue
        target=OUT/'Before'/source.relative_to(ROOT);target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source,target)
        assert hashlib.sha256(source.read_bytes()).digest()==hashlib.sha256(target.read_bytes()).digest()
        report['backups'].append(str(target))
def save(asset):
    assert lib.save_loaded_asset(asset,only_if_is_dirty=False)
    report['saved'].append(asset.get_path_name())
def material(name,domain=None):
    path=DEST+'/'+name;backup(path)
    m=lib.load_asset(path) if lib.does_asset_exist(path) else assets.create_asset(name,DEST,u.Material,u.MaterialFactoryNew())
    tag=lib.get_metadata_tag(m,'MCHubSystemsOwner')
    assert not tag or tag=='HubSystems20261023'
    if edit.get_num_material_expressions(m):
        assert tag=='HubSystems20261023','Existing unrelated material preserved: '+path
        for node in list(edit.get_material_expressions(m)):edit.delete_material_expression(m,node)
    lib.set_metadata_tag(m,'MCHubSystemsOwner','HubSystems20261023')
    if domain is not None:m.set_editor_property('material_domain',domain)
    return m
def node(m,cls,x=0,y=0,**props):
    n=edit.create_material_expression(m,cls,x,y)
    for k,v in props.items():n.set_editor_property(k,v)
    return n
def scalar(m,name,value,y):return node(m,u.MaterialExpressionScalarParameter,-600,y,parameter_name=name,default_value=value)
def custom(m,code,inputs,kind):
    c=node(m,u.MaterialExpressionCustom,0,0,code=code,description='Bounded colored CRT reel',output_type=kind)
    ci=[]
    for name in inputs:
        p=u.CustomInput();p.set_editor_property('input_name',name);ci.append(p)
    c.set_editor_property('inputs',ci)
    for name,value in inputs.items():assert edit.connect_material_expressions(value,'',c,name)
    return c

m=material('M_GrillHologramReel',u.MaterialDomain.MD_UI);m.set_editor_property('blend_mode',u.BlendMode.BLEND_TRANSLUCENT)
uv=node(m,u.MaterialExpressionTextureCoordinate,-900,0)
atlas=node(m,u.MaterialExpressionTextureObjectParameter,-900,160,parameter_name='Atlas',texture=lib.load_asset('/Game/Art/Texture/hub/SM_Roulette_low_DefaultMaterial_BaseColor'))
clock=node(m,u.MaterialExpressionTime,-900,320)
code='''
float2 p=UV;
float wobble=sin(p.y*31.0+Time*1.6)*0.00035;
float2 tc=float2(lerp(0.67529296875,0.00593948364,p.x)+wobble,frac(VCenter-(p.y-0.5)*0.105));
float delta=0.0009*saturate(ColorSeparation);
float3 c=Texture2DSample(Atlas,AtlasSampler,tc).rgb;
c.r=Texture2DSample(Atlas,AtlasSampler,tc+float2(delta,0)).r;
c.b=Texture2DSample(Atlas,AtlasSampler,tc-float2(delta,0)).b;
float chroma=max(c.r,max(c.g,c.b))-min(c.r,min(c.g,c.b));
float alpha=saturate(abs((c.r+c.g+c.b)/3.0-0.3185)*24.0+chroma*10.0);
float scan=1.0-saturate(ScanlineStrength)*(0.5+0.5*sin((p.y*52.0+Time*0.65)*6.2831853));
float noise=frac(sin(dot(floor(p*float2(90,70))+floor(Time*20.0),float2(12.9898,78.233)))*43758.5453);
c=lerp(c,c*float3(0.70,1.05,1.12),0.17)*scan+(noise-0.5)*0.018;
float edge=saturate(p.x*30.0)*saturate((1-p.x)*30.0)*saturate(p.y*25.0)*saturate((1-p.y)*25.0);
return float4(max(c,0),alpha*edge*0.94);
'''
c=custom(m,code,dict(UV=uv,Atlas=atlas,Time=clock,VCenter=scalar(m,'VCenter',.99462890625,460),ScanlineStrength=scalar(m,'ScanlineStrength',.12,570),ColorSeparation=scalar(m,'ColorSeparation',.15,680)),u.CustomMaterialOutputType.CMOT_FLOAT4)
rgb=node(m,u.MaterialExpressionComponentMask,300,0,r=True,g=True,b=True,a=False)
alpha=node(m,u.MaterialExpressionComponentMask,300,180,r=False,g=False,b=False,a=True)
assert edit.connect_material_expressions(c,'',rgb,str(edit.get_material_expression_input_names(rgb)[0])) and edit.connect_material_expressions(c,'',alpha,str(edit.get_material_expression_input_names(alpha)[0]))
assert edit.connect_material_property(rgb,'',u.MaterialProperty.MP_EMISSIVE_COLOR)
assert edit.connect_material_property(alpha,'',u.MaterialProperty.MP_OPACITY)
assert not edit.recompile_material(m),'Hologram shader failed to compile'
save(m)

pill=material('M_CalciumCapsule')
uv=node(pill,u.MaterialExpressionTextureCoordinate,-600,0)
color=custom(pill,'float band=1-smoothstep(0.015,0.04,abs(UV.y-0.5)); return lerp(float3(0.91,0.98,0.94),float3(0.05,0.7,0.55),band);',dict(UV=uv),u.CustomMaterialOutputType.CMOT_FLOAT3)
rough=node(pill,u.MaterialExpressionConstant,-200,250,r=.35)
assert edit.connect_material_property(color,'',u.MaterialProperty.MP_BASE_COLOR)
assert edit.connect_material_property(rough,'',u.MaterialProperty.MP_ROUGHNESS)
assert not edit.recompile_material(pill),'Calcium shader failed to compile'
save(pill)

bp_path='/Game/Gameplay/UI/WBP_GrillHologram';backup(bp_path)
bp=lib.load_asset(bp_path) if lib.does_asset_exist(bp_path) else u.MCGameplayUIEditorLibrary.create_editable_gameplay_widget(bp_path,u.MCHubGrillHologramWidget)
assert bp,'Could not create hologram WBP'
u.BlueprintEditorLibrary.compile_blueprint(bp);save(bp)
theme=lib.load_asset('/Game/Gameplay/UI/Theme/DA_GameplayUITheme');backup('/Game/Gameplay/UI/Theme/DA_GameplayUITheme')
roles=dict(theme.get_editor_property('widget_classes'));roles[u.Name('GrillHologram')]=bp.generated_class();theme.set_editor_property('widget_classes',roles);save(theme)
sys.path.insert(0,str(ROOT/'Tools/Unreal/UI'))
from localization_catalog import migrate_localization_row
text_path='/Game/Gameplay/UI/Data/DT_UI_Localization';backup(text_path)
text_table=lib.load_asset(text_path);assert text_table
rows=json.loads(u.DataTableFunctionLibrary.export_data_table_to_json_string(text_table))
keys={r['Name'].casefold() for r in rows}
copy=[('UI.Grill.Rolling','КРУТИМ…','ROLLING…'),('UI.Grill.Accepted','РЫБА ПРИНЯТА','FISH ACCEPTED'),
    ('UI.Grill.Combination','ВАША КОМБИНАЦИЯ','YOUR COMBINATION'),('UI.Grill.Idle','1 РЫБА · 1 ПОПЫТКА','1 FISH · 1 ROLL'),
    ('UI.Grill.Coins','+{0} монет','+{0} coins'),('UI.Grill.NoPrize','Без выигрыша · попробуй ещё','No prize · try again'),
    ('UI.Grill.Item','{0} ×{1}','{0} ×{1}')]
for key,ru,en in copy:
    if key.casefold() not in keys:rows.append(migrate_localization_row(dict(Name=key,RU=ru,EN=en,SourceText=ru,Context='Hub grill hologram')))
assert u.DataTableFunctionLibrary.fill_data_table_from_json_string(text_table,json.dumps(rows,ensure_ascii=False))
save(text_table)

world=u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
assert world.get_path_name().split('.')[0]=='/Game/Maps/Hub','Open Hub before assembly authoring.'
backup('/Game/Maps/Hub')
actors=u.get_editor_subsystem(u.EditorActorSubsystem)
all_actors=actors.get_all_level_actors()
by_label={a.get_actor_label():a for a in all_actors}
body=by_label['SM_Grill_Bot_007.001'];lid=by_label['SM_Grill_Bot_007.002']
reels=[by_label['SM_Roulette_'+str(i)] for i in (1,2,3)]
found=[a for a in all_actors if isinstance(a,u.MCHubGrillStation)]
assert len(found)<=1
station=found[0] if found else actors.spawn_actor_from_class(u.MCHubGrillStation,body.get_actor_location(),u.Rotator(yaw=40))
# Only the non-rendering driver/interaction box moves. The colleague's physical
# assembly remains untouched. +Y is the reel-window side facing the plaza.
assembly_before={a.get_path_name():a.get_actor_transform().export_text() for a in [body,lid]+reels}
front=u.MathLibrary.transform_location(body.get_actor_transform(),u.Vector(0,240,0))
station.set_actor_location(front,False,False)
station.set_actor_rotation(u.Rotator(yaw=40),False)
station.set_actor_label('HubGrill_SlotMachine');station.tags=list(dict.fromkeys(list(station.tags)+[u.Name('MCHubSystems20261023')]))
station.set_editor_property('lid_actor',lid);station.set_editor_property('reel_actors',reels)
station.set_editor_property('reel_spin_axis_local',u.Vector(1,0,0))
# Measured centres of the head's exposed outer aperture, in each reel YZ frame.
# Keep this window alignment in physical angles; the hologram removes it when
# sampling the padded atlas, so both displays share the same logical symbol.
station.set_editor_property('reel_angle_offsets',[19.2466733,19.5019897,19.7723341])
for a in [lid]+reels:a.get_component_by_class(u.StaticMeshComponent).set_mobility(u.ComponentMobility.MOVABLE)
# The third imported reel had an arbitrary spun orientation. Give all three a
# common measured mesh reference; every later physical/hologram stop uses it.
reels[2].set_actor_rotation(reels[0].get_actor_rotation(),False)
centers=dict(Cherry=130,Lemon=350,Watermelon=560,Plum=777,Bell=1004,Seven=1234,Bar=1490,X=1690,Star=1917)
facet_v=[.005603790283203125,.050537109375,.09552001953125,.140380859375,.1854248046875,
    .2303466796875,.275390625,.3203125,.365234375,.41015625,.455078125,.5,.544921875,
    .58984375,.634765625,.6796875,.724609375,.76953125,.814453125,.859375,.904296875,
    .94970703125,.99462890625]
def stop_angle(v):
    i=next(i for i in range(22) if facet_v[i]<=v<=facet_v[i+1])
    t=(v-facet_v[i])/(facet_v[i+1]-facet_v[i]);step=2*math.pi/22
    phi=i*step+math.atan2(t*math.sin(step),1-t+t*math.cos(step))
    return (-math.degrees(phi))%360
angles={u.Name(k):stop_angle(y/2048) for k,y in centers.items()}
station.set_editor_property('symbol_stop_angles',angles);report['angles']={str(k):v for k,v in angles.items()}
# Interaction is in front of the reel window, never inside the body.
vol=station.get_editor_property('interaction_volume');vol.set_box_extent(u.Vector(170,110,150))
body_local=u.MathLibrary.inverse_transform_location(station.get_actor_transform(),body.get_actor_location())
presentation=station.get_editor_property('presentation')
presentation.set_editor_property('hologram_local_offset',u.Vector(body_local.x,body_local.y,290))
prompt=station.get_editor_property('interaction_prompt');prompt.set_relative_location(u.Vector(body_local.x,body_local.y,220),False,False)
# The closed lid's lower rear edge meets the body top (about Z=142cm).
# Use measured local bounds rather than the lid actor's center, 45cm higher.
# Rear is -Y; positive X rotation lifts the +Y front edge.
lid_component=lid.get_component_by_class(u.StaticMeshComponent)
lid_min,lid_max=lid_component.get_local_bounds()
hinge_world=u.MathLibrary.transform_location(lid_component.get_world_transform(),u.Vector(0,lid_min.y+4,lid_min.z+2))
station.set_editor_property('lid_hinge_local',u.MathLibrary.inverse_transform_location(station.get_actor_transform(),hinge_world))
station.set_editor_property('lid_hinge_axis_local',u.Vector(1,0,0));station.set_editor_property('lid_open_degrees',38.)
for a in [body,lid]+reels[:2]:
    assert a.get_actor_transform().export_text()==assembly_before[a.get_path_name()],'Physical assembly transform changed: '+a.get_actor_label()
assert u.EditorLoadingAndSavingUtils.save_map(world,'/Game/Maps/Hub')
report['saved'].append('/Game/Maps/Hub')
(OUT/'Report.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print(json.dumps({'output':str(OUT),'saved':report['saved'],'angles':report['angles']}))
