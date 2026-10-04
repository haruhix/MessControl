"""Author scale-stable plaque optics in the stopped project editor.

Preserves the canonical arena wipe/coverage and player Coffee/face/fracture
contracts. Existing artist pigment and ORM supply detail; no new render pass.
Run with Tools/Unreal/remote_python.py. Reapplication updates named nodes.
"""
import json
import shutil
from datetime import datetime, timezone
from pathlib import Path
import unreal as u

edit, lib = u.MaterialEditingLibrary, u.EditorAssetLibrary
headless='-run=pythonscript' in u.SystemLibrary.get_command_line().lower()
if not headless:
    level_editor=u.get_editor_subsystem(u.LevelEditorSubsystem)
    assert not (level_editor and level_editor.is_in_play_in_editor()), 'Stop PIE before material authoring'
paths = ['/Game/Gameplay/Care/M_ArenaToothRelief', '/Game/Art/Materials/M_TeethGameplay']
instances = ['/Game/Gameplay/CharacterCurrent/Materials/MI_Character', '/Game/Gameplay/CharacterCurrent/Materials/MI_Bag', '/Game/Art/Materials/MI_TeethPlayer']
dirty = {p.get_path_name() for p in u.EditorLoadingAndSavingUtils.get_dirty_content_packages()}
assert not dirty.intersection(paths+instances), 'Preserve unsaved material edits before authoring: '+str(dirty.intersection(paths+instances))
root = Path(u.Paths.project_dir()).resolve()
saved = Path(u.Paths.project_saved_dir()).resolve()
backup = saved/'GrimeReview'/'MaterialBackups'/datetime.now(timezone.utc).strftime('%Y%m%d_%H%M%S_%f')
backup.mkdir(parents=True)
for path in paths+instances:
    shutil.copy2(root/'Content'/Path(path.removeprefix('/Game/')+'.uasset'), backup/(path.rsplit('/',1)[1]+'.uasset'))

report = {'backup':str(backup), 'materials':{}, 'texture_density':'Object/rest coordinates; centimetres for arena detail'}

def nodes_for(mat):
    nodes=list(edit.get_material_expressions(mat))
    customs={str(n.get_editor_property('description')):n for n in nodes if isinstance(n,u.MaterialExpressionCustom)}
    params={str(n.get_editor_property('parameter_name')):n for n in nodes if isinstance(n,(u.MaterialExpressionScalarParameter,u.MaterialExpressionVectorParameter,u.MaterialExpressionTextureObjectParameter))}
    return nodes,customs,params

def scalar(mat,params,name,value):
    n=params.get(name)
    if n is None:
        n=edit.create_material_expression(mat,u.MaterialExpressionScalarParameter,1100,3400+len(params)*80)
        n.set_editor_property('parameter_name',name)
        params[name]=n
    n.set_editor_property('default_value',value)
    n.set_editor_property('group','Grime optics')
    return n

def vector(mat,params,name,value):
    n=params.get(name)
    if n is None:
        n=edit.create_material_expression(mat,u.MaterialExpressionVectorParameter,900,3400+len(params)*80)
        n.set_editor_property('parameter_name',name)
        params[name]=n
    n.set_editor_property('default_value',u.LinearColor(*value))
    n.set_editor_property('group','Grime optics')
    return n

def texture(mat,params,name,path,linear=False):
    n=params.get(name)
    if n is None:
        n=edit.create_material_expression(mat,u.MaterialExpressionTextureObjectParameter,700,3400+len(params)*80)
        n.set_editor_property('parameter_name',name)
        params[name]=n
    t=lib.load_asset(path)
    assert t, path
    n.set_editor_property('texture',t)
    n.set_editor_property('sampler_type',u.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR if linear else u.MaterialSamplerType.SAMPLERTYPE_COLOR)
    n.set_editor_property('group','Grime optics')
    return n

def custom(mat,customs,description,code,inputs,kind):
    n=customs.get(description)
    if n is None:
        n=edit.create_material_expression(mat,u.MaterialExpressionCustom,1600,3400+len(customs)*200)
        n.set_editor_property('description',description)
        customs[description]=n
    entries=[e for e in n.get_editor_property('inputs') if str(e.get_editor_property('input_name')) not in ('','None')]
    names={str(e.get_editor_property('input_name')) for e in entries}
    for name in inputs:
        if name not in names:
            e=u.CustomInput()
            e.set_editor_property('input_name',name)
            entries.append(e)
    n.set_editor_property('inputs',entries)
    n.set_editor_property('output_type',kind)
    n.set_editor_property('code',code)
    for name,src in inputs.items():
        assert edit.connect_material_expressions(src,'',n,name), description+':'+name
    return n

DETAIL='''
[branch] if (Amount<=.0001) return float4(.5,1,.5,0);
// Coordinates follow the surface, including piano movement and skeletal pose.
float3 q=P*Scale/max(Tile,8.0)+Seed*float3(.37,.19,.73);
float3 w=abs(normalize(N)); w*=w; w*=w; w/=max(dot(w,1),.0001);
float3 p=Texture2DSample(Pigment,PigmentSampler,q.yz).rgb*w.x
        +Texture2DSample(Pigment,PigmentSampler,q.xz).rgb*w.y
        +Texture2DSample(Pigment,PigmentSampler,q.xy).rgb*w.z;
float2 o=Texture2DSample(Surface,SurfaceSampler,q.yz).rg*w.x
        +Texture2DSample(Surface,SurfaceSampler,q.xz).rg*w.y
        +Texture2DSample(Surface,SurfaceSampler,q.xy).rg*w.z;
float grain=saturate(p.r*4.2);
// ORM uses linear data. Height is a tiny presentation detail, not displacement.
return float4(grain,saturate(o.r),saturate(o.g),(1-saturate(o.r))*.07+(grain-.5)*.035);
'''

with u.ScopedEditorTransaction('Scale-stable tooth grime optics'):
    mat=lib.load_asset(paths[0])
    nodes,cs,ps=nodes_for(mat)
    assert all(label in cs for label in ('MC Care: coatingMask','MC Care: coatingColor','MC Care: coatingRoughness','MC Care: meniscusNormal'))
    assert 'Texture2DSampleLevel(Wipe' in cs['MC Care: coatingMask'].get_editor_property('code')
    def interpolated(cls):
        return next(n for n in nodes if isinstance(n,u.MaterialExpressionVertexInterpolator)
                    and any(isinstance(src,cls) for src in edit.get_inputs_for_material_expression(mat,n)))
    position=interpolated(u.MaterialExpressionPreSkinnedPosition)
    local_normal=interpolated(u.MaterialExpressionPreSkinnedNormal)
    pigment=texture(mat,ps,'GrimePigment','/Game/Gameplay/Care/Textures/T_Grime_BaseColor')
    surface=texture(mat,ps,'GrimeSurface','/Game/Gameplay/Care/Textures/T_Grime_OcclusionRoughnessMetallic',True)
    tile=scalar(mat,ps,'GrimeDetailSize',48.)
    seed=scalar(mat,ps,'GrimeSeed',1.)
    strength=scalar(mat,ps,'GrimeMicroRelief',1.)
    light=vector(mat,ps,'GrimeLightColor',(.28,.14,.040,1))
    dark=vector(mat,ps,'GrimeDarkColor',(.075,.027,.009,1))
    rim=vector(mat,ps,'GrimeRimColor',(.43,.255,.095,1))
    detail=custom(mat,cs,'MC Care: anchored plaque detail',DETAIL,
        {'P':position,'N':local_normal,'Scale':ps['GrimeScale'],'Tile':tile,'Seed':seed,'Pigment':pigment,'Surface':surface,'Amount':ps['GrimeAmount']},u.CustomMaterialOutputType.CMOT_FLOAT4)
    custom(mat,cs,'MC Care: coatingColor','''
float body=saturate(F.z*.85+(1-D.x)*.30+F.w*.13);
float3 color=lerp(Light,Dark,smoothstep(.06,.83,body));
color*=lerp(.84,1.08,D.x)*lerp(.92,1,D.y);
float margin=1-smoothstep(.30,.78,F.x);
color=lerp(color,Rim,margin*.58);
return lerp(color,float3(.92,.97,.94),F.y*.96);
''',{'D':detail,'Light':light,'Dark':dark,'Rim':rim},u.CustomMaterialOutputType.CMOT_FLOAT3)
    custom(mat,cs,'MC Care: coatingRoughness','''
float margin=1-smoothstep(.30,.78,F.x);
float rough=lerp(.37,.55,D.z)+F.w*.025;
rough=lerp(rough,.27,margin*.72);
return lerp(rough,.43,F.y);
''',{'D':detail},u.CustomMaterialOutputType.CMOT_FLOAT1)
    custom(mat,cs,'MC Care: meniscusNormal','''
float3 n=normalize(N);
float h=D.w*max(Strength,0);
float3 dx=ddx(World),dy=ddy(World),rx=cross(dy,n),ry=cross(n,dx);
float det=dot(dx,rx);
return normalize(n-(ddx(h)*rx+ddy(h)*ry)*sign(det)/max(abs(det),1e-5));
''',{'D':detail,'Strength':strength},u.CustomMaterialOutputType.CMOT_FLOAT3)
    scalar(mat,ps,'Specular',.46)
    errors=list(edit.recompile_material(mat))
    assert not errors, errors
    assert lib.save_loaded_asset(mat,False)
    report['materials'][paths[0]]={'compile_errors':errors,'detail_size_cm':48,'coverage_preserved':True}

    mat=lib.load_asset(paths[1])
    nodes,cs,ps=nodes_for(mat)
    by_name={n.get_name():n for n in nodes}
    position=by_name['MaterialExpressionVertexInterpolator_0']
    color=cs['Gameplay stains over the original textured enamel']
    code=color.get_editor_property('code')
    assert 'float crack' in code and 'Fracture' in code and 'saturate(Flash)' in code
    tail='float crack'+code.split('float crack',1)[1]
    # Preserve the mask and all face exclusions. Detail adds relief/color only.
    normal=next((n for n in nodes if isinstance(n,u.MaterialExpressionPreSkinnedNormal)),None)
    if normal is None:
        normal=edit.create_material_expression(mat,u.MaterialExpressionPreSkinnedNormal,800,3500)
    interpolator=next((n for n in nodes if isinstance(n,u.MaterialExpressionVertexInterpolator) and str(n.get_editor_property('desc'))=='Grime rest normal'),None)
    if interpolator is None:
        interpolator=edit.create_material_expression(mat,u.MaterialExpressionVertexInterpolator,1000,3500)
        interpolator.set_editor_property('desc','Grime rest normal')
    assert edit.connect_material_expressions(normal,'',interpolator,'')
    scale=vector(mat,ps,'GrimeRestScale',(1,1,1,1))
    tile=scalar(mat,ps,'GrimeDetailSize',22.)
    seed=scalar(mat,ps,'GrimeDetailSeed',1.)
    strength=scalar(mat,ps,'GrimeMicroRelief',.7)
    pigment=texture(mat,ps,'GrimePigment','/Game/Gameplay/Care/Textures/T_Grime_BaseColor')
    surface=texture(mat,ps,'GrimeSurface','/Game/Gameplay/Care/Textures/T_Grime_OcclusionRoughnessMetallic',True)
    light=vector(mat,ps,'GrimeLightColor',(.28,.14,.040,1))
    dark=vector(mat,ps,'GrimeDarkColor',(.075,.027,.009,1))
    rim=vector(mat,ps,'GrimeRimColor',(.43,.255,.095,1))
    detail=custom(mat,cs,'Anchored player grime detail',DETAIL,
        {'P':position,'N':interpolator,'Scale':scale,'Tile':tile,'Seed':seed,'Pigment':pigment,'Surface':surface,'Amount':ps['Coffee']},u.CustomMaterialOutputType.CMOT_FLOAT4)
    custom(mat,cs,'Gameplay stains over the original textured enamel','''
float body=saturate(Stain.y*.80+(1-D.x)*.32);
float3 dirt=lerp(GrimeLight,GrimeDark,body);
dirt*=lerp(.86,1.08,D.x)*lerp(.93,1,D.y);
dirt=lerp(dirt,GrimeRim,Stain.z*.48);
float enamel=smoothstep(.20,.48,min(Enamel.r,min(Enamel.g,Enamel.b)));
float patch=Stain.x*saturate(GrimeOpacity)*enamel;
float3 color=lerp(Enamel,dirt,patch);
'''+tail,{'D':detail},u.CustomMaterialOutputType.CMOT_FLOAT3)
    custom(mat,cs,'Matte player grime','''
float enamel=smoothstep(.20,.48,min(Enamel.r,min(Enamel.g,Enamel.b)));
float rough=lerp(.39,.57,D.z);
rough=lerp(rough,.30,Stain.z*.65);
return lerp(Base,rough,Stain.x*enamel);
''',{'D':detail},u.CustomMaterialOutputType.CMOT_FLOAT1)
    custom(mat,cs,'Reduced player grime shine','''
float enamel=smoothstep(.20,.48,min(Enamel.r,min(Enamel.g,Enamel.b)));
return lerp(Base,.43,Stain.x*enamel*.85);
''',{},u.CustomMaterialOutputType.CMOT_FLOAT1)
    custom(mat,cs,'Recessed enamel surface normal','''
float3 n=normalize(N);
float3 dx=ddx(WP),dy=ddy(WP),rx=cross(dy,n),ry=cross(n,dx);
float determinant=dot(dx,rx);
float enamel=smoothstep(.20,.48,min(Enamel.r,min(Enamel.g,Enamel.b)));
float plaque=Stain.x*enamel*max(Strength,0)*(D.w+.065*Stain.y);
float height=max(Depth,0)*Fracture.z+plaque;
float3 gradient=sign(determinant)*(ddx(height)*rx+ddy(height)*ry);
return normalize(max(abs(determinant),.00001)*n-gradient);
''',{'Stain':cs['Rounded player grime mask'],'D':detail,'Strength':strength,'Enamel':by_name['MaterialExpressionLinearInterpolate_1']},u.CustomMaterialOutputType.CMOT_FLOAT3)
    errors=list(edit.recompile_material(mat))
    assert not errors,errors
    assert lib.save_loaded_asset(mat,False)
    report['materials'][paths[1]]={'compile_errors':errors,'detail_size_rest_cm':22,'preserved':['Coffee mask','face exclusion','fractures','HitFlash','BodyStretch','artist texture normals']}

for path in instances:
    instance=lib.load_asset(path)
    if instance:
        # These saved instances carry palette overrides from the earlier look.
        for name,value in [('GrimeLightColor',(.28,.14,.040,1)),('GrimeDarkColor',(.075,.027,.009,1)),('GrimeRimColor',(.43,.255,.095,1))]:
            edit.set_material_instance_vector_parameter_value(instance,name,u.LinearColor(*value))
        edit.update_material_instance(instance)
        lib.save_loaded_asset(instance,True)
for path in paths:
    stats=edit.get_statistics(lib.load_asset(path))
    report['materials'][path]['statistics']={name:getattr(stats,name) for name in
        ('num_vertex_shader_instructions','num_pixel_shader_instructions','num_samplers','num_pixel_texture_samples')}
(saved/'GrimeReview'/'MaterialBuild.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
u.log('MC_GRIME_OPTICS_SAVED '+json.dumps(report))
