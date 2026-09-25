"""Build the pour/drain assets. Rebuild masters explicitly with MC_REBUILD_COFFEE_WATER=1.
Material instance overrides and existing water tuning are preserved. No map edits.
"""
import os, sys
from pathlib import Path
import unreal as u
sys.path.insert(0,str(Path(__file__).resolve().parent))
from coffee_material_graph import Graph
root=Path(u.Paths.project_dir()).resolve()
lib=u.EditorAssetLibrary; edit=u.MaterialEditingLibrary; assets=u.AssetToolsHelpers.get_asset_tools()

for name in ('SM_CoffeeSurface','SM_CoffeeJet','SM_CoffeeCrown','SM_CoffeeDrain','SM_CoffeeDrop'):
    path='/Game/Art/Meshes/'+name
    if not lib.does_asset_exist(path) or (name!='SM_CoffeeSurface' and os.environ.get('MC_REBUILD_COFFEE_GEOMETRY')=='1'):
        u.SystemLibrary.execute_console_command(None,'Interchange.FeatureFlags.Import.FBX 0')
        task=u.AssetImportTask(); task.filename=str(root/'ArtSource/CoffeeWater'/(name+'.fbx'))
        task.destination_path='/Game/Art/Meshes'; task.destination_name=name; task.automated=True; task.save=True; task.replace_existing=True
        opt=u.FbxImportUI(); opt.import_mesh=True; opt.import_as_skeletal=False; opt.import_materials=False; opt.import_textures=False
        opt.mesh_type_to_import=u.FBXImportType.FBXIT_STATIC_MESH; opt.static_mesh_import_data.combine_meshes=True
        opt.static_mesh_import_data.auto_generate_collision=False; opt.static_mesh_import_data.generate_lightmap_u_vs=False
        task.options=opt; task.factory=u.FbxFactory(); assets.import_asset_tasks([task])
    mesh=lib.load_asset(path); assert mesh,path
    for side in ('positive_bounds_extension','negative_bounds_extension'):
        b=mesh.get_editor_property(side)
        mesh.set_editor_property(side,u.Vector(max(b.x,12),max(b.y,12),max(b.z,256 if name=='SM_CoffeeSurface' else 16)))
    lib.save_loaded_asset(mesh,only_if_is_dirty=True)

rebuild=os.environ.get('MC_REBUILD_COFFEE_WATER')=='1' or not lib.does_asset_exist('/Game/Art/Materials/M_CoffeePour')
if rebuild:
    g=Graph('M_CoffeeSurface',world_normal=True)
    P=g.node(u.MaterialExpressionWorldPosition)
    P.set_editor_property('world_position_shader_offset',u.WorldPositionIncludedOffsets.WPT_EXCLUDE_ALL_SHADER_OFFSETS)
    runtime={'P':P}
    for key,value in dict(WaterTime=0,RippleHeight=6,RippleLength=260,RippleSpeed=1.4,FillAmount=0,DrainAmount=0,
            JetAmount=0,Filling=1,FrontRadius=0,FrontWidth=110,FrontHeight=26,DrainRadius=320,DrainDepth=65).items():
        runtime[key]=g.scalar(key,value,'Runtime')
    runtime['Inlet']=g.vector('Inlet',(420,-100,0,0),'Runtime'); runtime['Outlet']=g.vector('Outlet',(920,0,0,0),'Runtime')
    base='''
float k=6.2831853/max(RippleLength,1), t=WaterTime*RippleSpeed;
float a=k*(P.x+.28*P.y)-t, b=k*(-.45*P.x+.9*P.y)*1.73+t*1.21, c=k*(.2*P.x+P.y)*2.7-t*.73;
float2 from=P.xy-Inlet.xy, to=P.xy-Outlet.xy;
float d=max(length(from),.01), r=max(length(to),.01), band=(d-FrontRadius)/max(FrontWidth,1);
float ridge=Filling*FrontHeight*exp(-band*band)*exp(-d/1600);
float mound=JetAmount*18*exp(-d*d/(140*140));
float pit=DrainAmount*DrainDepth*exp(-r*r/(DrainRadius*DrainRadius));
float filled=saturate(FillAmount*4);
'''
    wpo=g.custom('Shared height: ripples, expanding impact front, inlet mound, throat depression',base+'''
return float3(0,0,RippleHeight*(.6*sin(a)+.28*sin(b)+.12*sin(c))*filled+ridge+mound-pit);
''',runtime,3); g.output(wpo,u.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    normal=g.custom('Analytic surface slopes and directional small ripples',base+'''
float2 slope=RippleHeight*k*(.6*cos(a)*float2(1,.28)+.28*cos(b)*1.73*float2(-.45,.9)+.12*cos(c)*2.7*float2(.2,1))*filled;
slope+=(ridge*(-2*band/FrontWidth-1.0/1600)-mound*2*d/(140*140))*from/d;
slope+=pit*2*to/(DrainRadius*DrainRadius);
float2 flow=lerp(from/d,-to/r,DrainAmount);
float2 q=P.xy-flow*WaterTime*VisualFlowSpeed;
slope+=NormalDetail*float2(sin(q.x*.09+q.y*.04),cos(q.y*.115-q.x*.037));
slope+=from/d*JetAmount*.085*cos(d*.075-WaterTime*9)*exp(-d/650);
return normalize(float3(-slope,1));
''',dict(runtime,NormalDetail=g.scalar('NormalDetail',.10),VisualFlowSpeed=g.scalar('VisualFlowSpeed',170)),3)
    g.output(normal,u.MaterialProperty.MP_NORMAL)
    depth=g.depth('DepthTintDistance',210); edge=g.depth('EdgeFadeDistance',16); shore=g.depth('ShoreFoamWidth',34)
    fresnel=g.node(u.MaterialExpressionFresnel); fresnel.set_editor_property('exponent',4)
    wakes={f'Wake{i}':g.vector(f'Wake{i}',(0,0,0,0),'Runtime') for i in range(4)}
    foam=g.custom('Advected microfoam, impact froth, wave crest and player wakes',base+'''
float2 flow=lerp(from/d,-to/r,DrainAmount);
float2 q=P.xy-flow*WaterTime*(45+DrainAmount*160);
float n=.5+.23*sin(q.x*.019+1.8*sin(q.y*.011))+.18*cos(q.y*.024+sin(q.x*.015))+.09*sin(q.x*.067+q.y*.051);
float2 cell=floor(q*.085), f=frac(q*.085)-.5;
float2 jitter=.19*sin(float2(dot(cell,float2(127.1,311.7)),dot(cell,float2(269.5,183.3))));
float rr=length(f-jitter);
float bubble=smoothstep(.15,.21,rr)*(1-smoothstep(.245,.31,rr));
float small=(.22+.78*bubble);
float froth=JetAmount*exp(-pow((d-155)/120,2))*(.3+.7*n);
float crest=Filling*exp(-band*band*1.8)*exp(-d/1800);
float rim=(1-Shore)*(.3+.4*bubble);
float drain=DrainAmount*exp(-r/520)*pow(saturate(.5+.5*sin(r*.044+WaterTime*13+3*atan2(to.y,to.x)+n*3)),7)*.22*smoothstep(.25,.65,n);
float wake=0; float4 points[4]={Wake0,Wake1,Wake2,Wake3};
[unroll] for(int i=0;i<4;i++) {
    float wd=length(P.xy-points[i].xy);
    wake+=pow(saturate(.5+.5*sin(wd*.105-WaterTime*6+n*1.4)),5)*exp(-wd/85)*smoothstep(14,30,wd)*points[i].z*points[i].w*(.2+.8*n);
}
float flecks=smoothstep(.81,.94,n)*.13;
return saturate(FoamAmount*(flecks+small*(froth+crest*.75+rim)+drain+WakeStrength*wake));
''',dict(runtime,Shore=shore,FoamAmount=g.scalar('FoamAmount',.8),WakeStrength=g.scalar('WakeStrength',.4),**wakes))
    color=g.custom('Rich coffee body, golden thin edges, pale aerated crema','''
float3 body=lerp(Shallow.rgb,Deep.rgb,saturate(Depth));
return lerp(body,FoamColor.rgb,Foam);
''',dict(Depth=depth,Foam=foam,Shallow=g.vector('ShallowColor',(.27,.083,.017,1)),Deep=g.vector('DeepColor',(.068,.017,.004,1)),FoamColor=g.vector('FoamColor',(.83,.54,.25,1))),3)
    g.output(color,u.MaterialProperty.MP_BASE_COLOR)
    opacity=g.custom('Wet front expands from inlet; softened arena and object intersections',base+'''
float wet=lerp(1,1-smoothstep(FrontRadius-FrontWidth,FrontRadius+FrontWidth,d),Filling);
float border=saturate(min(ArenaSize.x-abs(P.x-ArenaCenter.x),ArenaSize.y-abs(P.y-ArenaCenter.y))/40);
return saturate(lerp(ShallowOpacity,DeepOpacity,Depth)+Fresnel*.10+Foam*.2)*Edge*border*wet;
''',dict(runtime,ArenaSize=g.vector('ArenaSize',(1050,740,0,0),'Runtime'),ArenaCenter=g.vector('ArenaCenter',(0,0,0,0),'Runtime'),Depth=depth,Edge=edge,Fresnel=fresnel,Foam=foam,ShallowOpacity=g.scalar('ShallowOpacity',.60),DeepOpacity=g.scalar('DeepOpacity',.92)))
    g.output(opacity,u.MaterialProperty.MP_OPACITY)
    g.output(g.custom('Liquid gloss, softer foam','return lerp(Roughness,.38,Foam);',dict(Roughness=g.scalar('Roughness',.105),Foam=foam)),u.MaterialProperty.MP_ROUGHNESS)
    g.output(g.scalar('Specular',.65),u.MaterialProperty.MP_SPECULAR)
    g.output(g.scalar('Refraction',1.012),u.MaterialProperty.MP_REFRACTION)
    g.output(g.custom('Restrained warm fill for the enclosed mouth','return Color*Fill;',dict(Color=color,Fill=g.scalar('AmbientFill',.65)),3),u.MaterialProperty.MP_EMISSIVE_COLOR)
    g.save('MI_CoffeeSurface')

    g=Graph('M_CoffeePour')
    raw_uv=g.node(u.MaterialExpressionTextureCoordinate)
    # Legacy FBX import flips V for DirectX; restore the Blender grid's root-to-tip coordinate.
    uv=g.custom('Authored grid UV','return float2(UV.x,1-UV.y);',dict(UV=raw_uv),2)
    normal=g.node(u.MaterialExpressionVertexNormalWS)
    time=g.scalar('WaterTime',0,'Runtime'); strength=g.scalar('Strength',1,'Runtime')
    crown=g.scalar('IsCrown',0,'Runtime'); drain=g.scalar('IsDrain',0,'Runtime')
    shared=dict(UV=uv,Time=time,Strength=strength,IsCrown=crown,IsDrain=drain)
    flow='''
float direction=1-2*saturate(IsCrown+IsDrain);
float y=UV.y*9+Time*FlowSpeed*direction;
float veins=.5+.28*sin(UV.x*69+2.2*sin(y*.8))+.14*sin(UV.x*131-y*1.8)+.08*cos(UV.x*203+y*2.3);
float ridges=pow(saturate(veins),3);
'''
    shared['FlowSpeed']=g.scalar('FlowSpeed',4.5)
    g.output(g.custom('Moving liquid fingers and asymmetric jet silhouette',flow+'''
float envelope=sin(saturate(UV.y)*3.14159);
float swell=sin(UV.x*37+Time*7+UV.y*19)+.45*sin(UV.x*81-Time*11);
float3 offset=N*Amplitude*envelope*swell;
offset.z+=IsCrown*Amplitude*1.6*sin(UV.x*51-Time*9)*UV.y*UV.y;
return offset;
''',dict(shared,N=normal,Amplitude=g.scalar('SilhouetteRipple',5)),3),u.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    g.output(g.custom('Longitudinal highlights and falling striations',flow+'''
return normalize(float3(NormalDetail*(sin(UV.x*90+2*sin(y))+.35*sin(UV.x*211-y)),NormalDetail*.4*cos(y*2+UV.x*24),1));
''',dict(shared,NormalDetail=g.scalar('NormalDetail',.28)),3),u.MaterialProperty.MP_NORMAL)
    fresnel=g.node(u.MaterialExpressionFresnel); fresnel.set_editor_property('exponent',3)
    pourcolor=g.custom('Coffee body, amber thin sheets and aerated fingertips',flow+'''
float tips=IsCrown*smoothstep(.6,1,UV.y)*(.3+.7*veins);
float3 body=lerp(Body.rgb,Amber.rgb,saturate(ridges*.6+Fresnel*.55));
return lerp(body,Crema.rgb,tips*.48);
''',dict(shared,Fresnel=fresnel,Body=g.vector('BodyColor',(.075,.018,.003,1)),Amber=g.vector('ThinColor',(.42,.135,.025,1)),Crema=g.vector('CremaColor',(.85,.55,.25,1))),3)
    g.output(pourcolor,u.MaterialProperty.MP_BASE_COLOR)
    fade=g.depth('ContactFade',7)
    g.output(g.custom('Soft splash sheet margins and stream shutoff',flow+'''
float edge=lerp(1,1-smoothstep(.88,1,UV.y),IsCrown);
float width=lerp(1,smoothstep(0,.10,UV.x)*(1-smoothstep(.9,1,UV.x)),IsDrain);
return saturate(Opacity+Fresnel*.18+ridges*.12)*Strength*edge*width*Fade;
''',dict(shared,Fresnel=fresnel,Opacity=g.scalar('Opacity',.82),Fade=fade)),u.MaterialProperty.MP_OPACITY)
    g.output(g.scalar('Roughness',.085),u.MaterialProperty.MP_ROUGHNESS)
    g.output(g.scalar('Specular',.7),u.MaterialProperty.MP_SPECULAR)
    g.output(g.scalar('Refraction',1.018),u.MaterialProperty.MP_REFRACTION)
    g.output(g.custom('Warm reflected fill and narrow moving glints',flow+'''
float glint=pow(saturate(.5+.5*sin(UV.x*157+1.3*sin(y*.75))),24);
glint*=smoothstep(.52,.88,veins)*(.15+.85*Fresnel);
return Color*Fill+float3(.85,.52,.25)*(glint*.5+Fresnel*.12);
''',dict(shared,Color=pourcolor,Fill=g.scalar('AmbientFill',.85),Fresnel=fresnel),3),u.MaterialProperty.MP_EMISSIVE_COLOR)
    g.save('MI_CoffeePour')

    g=Graph('M_CoffeeDrops',translucent=False)
    fresnel=g.node(u.MaterialExpressionFresnel); fresnel.set_editor_property('exponent',3)
    color=g.custom('Amber drop rim over a dark coffee centre','return lerp(Body.rgb,Rim.rgb,Fresnel);',dict(Body=g.vector('BodyColor',(.065,.016,.0025,1)),Rim=g.vector('RimColor',(.48,.19,.035,1)),Fresnel=fresnel),3)
    g.output(color,u.MaterialProperty.MP_BASE_COLOR); g.output(g.scalar('Roughness',.07),u.MaterialProperty.MP_ROUGHNESS)
    g.output(g.scalar('Specular',.8),u.MaterialProperty.MP_SPECULAR)
    g.output(g.custom('Warm fill','return Color*Fill;',dict(Color=color,Fill=g.scalar('AmbientFill',.55)),3),u.MaterialProperty.MP_EMISSIVE_COLOR)
    g.mat.set_editor_property('used_with_instanced_static_meshes',True)
    g.save('MI_CoffeeDrops')

profile=lib.load_asset('/Game/Data/DA_CoffeeWater')
if not profile:
    factory=u.DataAssetFactory(); factory.set_editor_property('data_asset_class',u.MCCoffeeProfile)
    profile=assets.create_asset('DA_CoffeeWater','/Game/Data',u.MCCoffeeProfile,factory)
links=dict(surface_mesh='/Game/Art/Meshes/SM_CoffeeSurface',surface_material='/Game/Art/Materials/MI_CoffeeSurface',
    jet_mesh='/Game/Art/Meshes/SM_CoffeeJet',crown_mesh='/Game/Art/Meshes/SM_CoffeeCrown',drain_mesh='/Game/Art/Meshes/SM_CoffeeDrain',
    drop_mesh='/Game/Art/Meshes/SM_CoffeeDrop',pour_material='/Game/Art/Materials/MI_CoffeePour',drop_material='/Game/Art/Materials/MI_CoffeeDrops')
for key,path in links.items():
    if not profile.get_editor_property(key): profile.set_editor_property(key,lib.load_asset(path))
# Move the initial pour clear of the colleague's solid teeth_low2 preview at (190,20).
tuning=profile.get_editor_property('settings'); inlet=tuning.get_editor_property('inlet')
if (abs(inlet.x-180)<.01 and abs(inlet.y)<.01) or (abs(inlet.x-420)<.01 and abs(inlet.y+100)<.01 and abs(inlet.z-700)<.01):
    tuning.set_editor_property('inlet',u.Vector(420,-100,1100)); profile.set_editor_property('settings',tuning)
lib.save_loaded_asset(profile,only_if_is_dirty=False)
plan=lib.load_asset('/Game/Data/DA_Day01')
if plan:
    steps=plan.get_editor_property('steps')
    for step in steps:
        if step.get_editor_property('step')==u.MCDayStep.COFFEE_WAVES:
            step.set_editor_property('seconds',6)
            step.set_editor_property('title',u.Text('COFFEE / POUR AND DRAIN'))
            step.set_editor_property('instruction',u.Text('Dodge the jet. WASD: paddle. Hold E near an arena tooth: cling through the drain.'))
    plan.set_editor_property('steps',steps); lib.save_loaded_asset(plan,only_if_is_dirty=False)
u.log('MC_COFFEE_POUR_ASSETS_PASS')
u.SystemLibrary.quit_editor()
