"""Refine coffee and create a distinct wipeable vomit shader. No UI input or PIE."""
from pathlib import Path
import json
import unreal as u

lib=u.EditorAssetLibrary
edit=u.MaterialEditingLibrary
assets=u.AssetToolsHelpers.get_asset_tools()
assert not u.EditorLevelLibrary.get_pie_worlds(False)

def save(obj):
    assert lib.save_loaded_asset(obj,only_if_is_dirty=False),obj.get_path_name()

def refresh(mat,instances):
    assert not edit.recompile_material(mat)
    save(mat)
    for mi in instances:
        edit.set_material_instance_parent(mi,None)
        edit.set_material_instance_parent(mi,mat)
        edit.update_material_instance(mi)
        save(mi)

def expressions(mat): return list(edit.get_material_expressions(mat))
def described(mat,name):
    aliases={'Context colour and wet meniscus':'color','Liquid height, coverage, meniscus, thickness':'field','Rounded liquid edge and brush wake normals':'normal','Stylized wet reflection':'sheen'}
    return next(n for n in expressions(mat) if isinstance(n,u.MaterialExpressionCustom) and n.get_editor_property('description') in (name,aliases.get(name)))
def add_input(custom,name,source,pin=''):
    entries=list(custom.get_editor_property('inputs'))
    if name not in [str(i.get_editor_property('input_name')) for i in entries]:
        inp=u.CustomInput(); inp.set_editor_property('input_name',name); entries.append(inp)
        custom.set_editor_property('inputs',entries)
    assert edit.connect_material_expressions(source,pin,custom,name)
def scalar(mat,name,value):
    n=next((n for n in expressions(mat) if isinstance(n,u.MaterialExpressionScalarParameter) and str(n.get_editor_property('parameter_name'))==name),None)
    if n is None: n=edit.create_material_expression(mat,u.MaterialExpressionScalarParameter)
    n.set_editor_property('parameter_name',name); n.set_editor_property('default_value',value); n.set_editor_property('group','Vomit appearance')
    return n
def vector(mat,name,value):
    n=next((n for n in expressions(mat) if isinstance(n,u.MaterialExpressionVectorParameter) and str(n.get_editor_property('parameter_name'))==name),None)
    if n is None: n=edit.create_material_expression(mat,u.MaterialExpressionVectorParameter)
    n.set_editor_property('parameter_name',name); n.set_editor_property('default_value',u.LinearColor(*value))
    n.set_editor_property('group','Vomit appearance'); return n

# A separate parent keeps coffee/sauce cleaning and appearance intact.
path='/Game/Gameplay/Hazards/M_VomitPuddle'
vomit=lib.load_asset(path) if lib.does_asset_exist(path) else lib.duplicate_asset('/Game/Gameplay/Liquid/M_CoffeePuddle',path)
assert vomit
for n in expressions(vomit):
    if isinstance(n,u.MaterialExpressionCustom) and 'struct CoffeeSurface' in n.get_editor_property('code'):
        code=n.get_editor_property('code')
        addition='''
        // A slow viscous skin, rather than the fast thin coffee film.
        float2 q=uv+float2(.004*sin(time*.22+seed),.003*cos(time*.19-seed));
        float folds=sin(q.x*36+1.9*sin(q.y*22+seed))*sin(q.y*31-seed);
        height+=.20*folds*interior*liquid;
'''
        marker='        // At the last gameplay contact'
        if '// A slow viscous skin' not in code:
            code=code.replace(marker,addition+marker)
        code=code.replace('time*.8','time*.24').replace('time*.7','time*.19')
        n.set_editor_property('code',code)

uv=next(n for n in expressions(vomit) if isinstance(n,u.MaterialExpressionTextureCoordinate))
clock=next(n for n in expressions(vomit) if isinstance(n,u.MaterialExpressionTime))
parameters={str(n.get_editor_property('parameter_name')):n for n in expressions(vomit) if isinstance(n,(u.MaterialExpressionScalarParameter,u.MaterialExpressionVectorParameter))}
color=described(vomit,'Context colour and wet meniscus')
detail='''
float2 q=UV*max(WorldSize,20)/18;
q+=float2(.08*sin(Time*.21+Seed),.06*cos(Time*.17-Seed));
float fold=.5+.24*sin(q.x*.82+2.4*sin(q.y*.71+Seed))+.16*cos(q.y*1.12-q.x*.37+Seed);
float2 cell=floor(q), f=frac(q)-.5;
float hash=frac(sin(dot(cell+Seed,float2(127.1,311.7)))*43758.5453);
float2 jitter=.21*sin(float2(hash*71,hash*93));
float radius=length((f-jitter)*float2(.85+hash*.35,1.12-hash*.24));
float bubble_radius=.13+hash*.095;
float bubble=(smoothstep(bubble_radius-.035,bubble_radius,radius)-smoothstep(bubble_radius+.028,bubble_radius+.068,radius))*step(.22,hash);
float aerated=smoothstep(.59,.83,fold)*(.16+.84*bubble);
float rim=F.z*.20*(.4+.6*fold);
float foam=saturate(FoamAmount*(aerated+rim));
float chunks=(1-smoothstep(.075,.18,radius))*step(1-ChunkAmount,hash)*F.w;
'''
color.set_editor_property('code',detail+'''
float3 body=lerp(Edge,Body,saturate(F.w*.8+.2))*(.83+.28*fold);
float3 bits=lerp(float3(.048,.064,.013),float3(.33,.105,.022),step(.77,hash));
return lerp(lerp(body,FoamColor.rgb,foam),bits,chunks*.83);
''')
for name,source,pin in [('UV',uv,''),('Time',clock,''),('WorldSize',parameters['WorldSize'],''),('FoamAmount',scalar(vomit,'FoamAmount',.60),''),('ChunkAmount',scalar(vomit,'ChunkAmount',.27),''),('FoamColor',vector(vomit,'FoamColor',(.55,.40,.13,1)),'RGB')]: add_input(color,name,source,pin)

field=described(vomit,'Liquid height, coverage, meniscus, thickness')
rough=next((n for n in expressions(vomit) if isinstance(n,u.MaterialExpressionCustom) and n.get_editor_property('description')=='Viscous body and porous foam roughness'),None)
if rough is None: rough=edit.create_material_expression(vomit,u.MaterialExpressionCustom)
rough.set_editor_property('description','Viscous body and porous foam roughness')
rough.set_editor_property('code',detail+'return lerp(Roughness,.46,foam)+chunks*.12;')
rough.set_editor_property('output_type',u.CustomMaterialOutputType.CMOT_FLOAT1)
for name,source,pin in [('F',field,''),('UV',uv,''),('Time',clock,''),('Seed',parameters['Seed'],''),('WorldSize',parameters['WorldSize'],''),('FoamAmount',scalar(vomit,'FoamAmount',.60),''),('ChunkAmount',scalar(vomit,'ChunkAmount',.27),''),('Roughness',parameters['Roughness'],'')]: add_input(rough,name,source,pin)
assert edit.connect_material_property(rough,'',u.MaterialProperty.MP_ROUGHNESS)

normal=described(vomit,'Rounded liquid edge and brush wake normals')
normal_code=normal.get_editor_property('code')
if '// Aerated skin normal' not in normal_code:
    normal_code=normal_code.replace('return normalize(float3(-hx/(e*2*WorldSize),-hy/(e*2*WorldSize),1));','''
// Aerated skin normal remains subtle at player scale and slows with viscosity.
float2 micro=float2(sin(UV.x*WorldSize*.42+sin(UV.y*WorldSize*.21)+Seed+Time*.24),
                   cos(UV.y*WorldSize*.38+Seed-Time*.19));
return normalize(float3(-hx/(e*2*WorldSize)+micro.x*.075,-hy/(e*2*WorldSize)+micro.y*.075,1));''')
    normal.set_editor_property('code',normal_code)
sheen=described(vomit,'Stylized wet reflection')
sheen.set_editor_property('code',sheen.get_editor_property('code').replace('key*2.5+fill*1.2','key*1.5+fill*.8'))
mi=lib.load_asset('/Game/Gameplay/Hazards/MI_VomitPuddle')
for name,value in [('LiquidColor',(.205,.133,.025,1)),('EdgeColor',(.31,.21,.047,1)),('FoamColor',(.55,.40,.13,1))]: edit.set_material_instance_vector_parameter_value(mi,name,u.LinearColor(*value))
for name,value in [('Roughness',.18),('Depth',2.1),('Rim',1.0),('WetSheen',.23),('FoamAmount',.60),('ChunkAmount',.27)]: edit.set_material_instance_scalar_parameter_value(mi,name,value)
refresh(vomit,[mi])

# Keep the server-shared wave height/WPO exactly as authored. Refine only the
# optical skin, so swimmers still meet the same visible height as buoyancy.
surface=lib.load_asset('/Game/Art/Materials/M_CoffeeSurface')
surface.set_editor_property('translucency_lighting_mode',u.TranslucencyLightingMode.TLM_SURFACE)
normal=described(surface,'Analytic surface slopes and directional small ripples')
code=normal.get_editor_property('code')
old='slope+=NormalDetail*float2(sin(q.x*.09+q.y*.04),cos(q.y*.115-q.x*.037));'
new='''// Two scales break the regular crossed ripples into a flowing wet skin.
slope+=NormalDetail*float2(sin(q.x*.058+q.y*.025+1.6*sin(q.y*.013)),cos(q.y*.073-q.x*.024+1.4*cos(q.x*.011)));
slope+=NormalDetail*.28*float2(cos(q.x*.17+q.y*.071),sin(q.y*.153-q.x*.067));'''
normal.set_editor_property('code',code.replace(old,new))
foam=described(surface,'Advected microfoam, impact froth, wave crest and player wakes')
code=foam.get_editor_property('code')
code=code.replace('float rim=(1-Shore)*(.3+.4*bubble);','float rim=(1-Shore)*(.20+.46*bubble)*(.45+.55*n);')
code=code.replace('float flecks=smoothstep(.81,.94,n)*.13;','float flecks=smoothstep(.76,.91,n)*(.08+.12*bubble);')
foam.set_editor_property('code',code)
appearance=lib.load_asset('/Game/Art/Materials/MI_CoffeeSurface')
for name,value in [('ShallowColor',(.29,.105,.025,1)),('DeepColor',(.080,.025,.007,1)),('FoamColor',(.65,.39,.16,1))]: edit.set_material_instance_vector_parameter_value(appearance,name,u.LinearColor(*value))
for name,value in [('DepthTintDistance',110),('EdgeFadeDistance',12),('ShoreFoamWidth',25),('FoamAmount',.62),('NormalDetail',.075),('Roughness',.14),('Specular',.65),('AmbientFill',.38),('Refraction',1.008),('ShallowOpacity',.76),('DeepOpacity',.98),('WakeStrength',.35)]: edit.set_material_instance_scalar_parameter_value(appearance,name,value)
refresh(surface,[appearance])

pour=lib.load_asset('/Game/Art/Materials/MI_CoffeePour')
pour_master=pour.get_editor_property('parent')
pour_master.set_editor_property('translucency_lighting_mode',u.TranslucencyLightingMode.TLM_SURFACE)
assert not edit.recompile_material(pour_master)
save(pour_master)
for name,value in [('BodyColor',(.08,.026,.008,1)),('ThinColor',(.33,.12,.025,1)),('CremaColor',(.65,.40,.16,1))]: edit.set_material_instance_vector_parameter_value(pour,name,u.LinearColor(*value))
for name,value in [('AmbientFill',.30),('Roughness',.13),('Opacity',.85),('Refraction',1.009),('NormalDetail',.21),('SilhouetteRipple',3.8)]: edit.set_material_instance_scalar_parameter_value(pour,name,value)
edit.update_material_instance(pour); save(pour)
drops=lib.load_asset('/Game/Art/Materials/MI_CoffeeDrops')
for name,value in [('BodyColor',(.08,.026,.008,1)),('RimColor',(.33,.12,.025,1))]: edit.set_material_instance_vector_parameter_value(drops,name,u.LinearColor(*value))
edit.set_material_instance_scalar_parameter_value(drops,'AmbientFill',.30); edit.update_material_instance(drops); save(drops)

# Align the flood rectangle with the playable tongue before the throat. The old
# Y centre left a dry strip at one side of the artist's wider arena.
plan=lib.load_asset('/Game/Data/DA_Day01')
plan.set_editor_property('arena_center',u.Vector(100,-25,0))
plan.set_editor_property('arena_half_size',u.Vector(1350,850,300))
save(plan)

report=dict(vomit=vomit.get_path_name(),puddle_parent=mi.get_editor_property('parent').get_path_name(),coffee=surface.get_path_name(),coffee_wpo_unchanged=True,small_vomit_only=True)
Path(u.Paths.project_saved_dir(),'FluidRefinement.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
u.log('MC_FLUID_REFINEMENT_PASS')
