"""Author scale-stable plaque optics in the stopped project editor.

Preserves the canonical arena wipe/coverage and player Coffee/face/fracture
contracts. Substance height, normals and pigment supply lit surface relief.
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
rich_paths = {name:'/Game/Gameplay/Care/Textures/T_GrimeRich_'+name
              for name in ('BaseColor','Height','Normal','Roughness')}
report['rich_texture_maps']=rich_paths
# Fail before editing either material if the coordinated texture import is incomplete.
assert all(lib.load_asset(path) for path in rich_paths.values()), 'Import all four GrimeRich maps first'

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

def texture(mat,params,name,path,linear=False,normal=False):
    n=params.get(name)
    if n is None:
        n=edit.create_material_expression(mat,u.MaterialExpressionTextureObjectParameter,700,3400+len(params)*80)
        n.set_editor_property('parameter_name',name)
        params[name]=n
    t=lib.load_asset(path)
    assert t, path
    n.set_editor_property('texture',t)
    sampler=(u.MaterialSamplerType.SAMPLERTYPE_NORMAL if normal else
             u.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR if linear else u.MaterialSamplerType.SAMPLERTYPE_COLOR)
    n.set_editor_property('sampler_type',sampler)
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
float h=Texture2DSample(Height,HeightSampler,q.yz).r*w.x
       +Texture2DSample(Height,HeightSampler,q.xz).r*w.y
       +Texture2DSample(Height,HeightSampler,q.xy).r*w.z;
float rough=Texture2DSample(Surface,SurfaceSampler,q.yz).r*w.x
           +Texture2DSample(Surface,SurfaceSampler,q.xz).r*w.y
           +Texture2DSample(Surface,SurfaceSampler,q.xy).r*w.z;
// Height is independent of albedo: pits, granules and built-up plateaus remain
// visible under moving lights rather than collapsing to one saturated brown.
float plateau=smoothstep(.24,.78,h);
float pit=1-smoothstep(.13,.43,h);
float relief=(h-.5)*.72+plateau*.28-pit*.13;
return float4(h,1-pit*.52,saturate(rough),relief);
'''

PIGMENT='''
[branch] if (Amount<=.0001) return float3(.2,.08,.018);
float3 q=P*Scale/max(Tile,8.0)+Seed*float3(.37,.19,.73);
float3 w=abs(normalize(N)); w*=w; w*=w; w/=max(dot(w,1),.0001);
return Texture2DSample(Pigment,PigmentSampler,q.yz).rgb*w.x
      +Texture2DSample(Pigment,PigmentSampler,q.xz).rgb*w.y
      +Texture2DSample(Pigment,PigmentSampler,q.xy).rgb*w.z;
'''

SLOPE='''
[branch] if (Amount<=.0001) return float3(0,0,0);
float3 q=P*Scale/max(Tile,8.0)+Seed*float3(.37,.19,.73);
float3 w=abs(normalize(N)); w*=w; w*=w; w/=max(dot(w,1),.0001);
// BC5 normal textures store signed tangent X/Y in their two sampled channels.
// Reconstruct Z, then express the three projection slopes in rest-space cm.
float2 x=Texture2DSample(NormalMap,NormalMapSampler,q.yz).rg*2-1;
float2 y=Texture2DSample(NormalMap,NormalMapSampler,q.xz).rg*2-1;
float2 z=Texture2DSample(NormalMap,NormalMapSampler,q.xy).rg*2-1;
x/=-max(sqrt(saturate(1-dot(x,x))),.35);
y/=-max(sqrt(saturate(1-dot(y,y))),.35);
z/=-max(sqrt(saturate(1-dot(z,z))),.35);
return float3(0,x.x,x.y)*w.x+float3(y.x,0,y.y)*w.y+float3(z.x,z.y,0)*w.z;
'''

def rich_detail(mat,cs,ps,label,position,normal,scale,tile,seed,amount):
    common={'P':position,'N':normal,'Scale':scale,'Tile':tile,'Seed':seed,'Amount':amount}
    pigment=texture(mat,ps,'GrimePigment',rich_paths['BaseColor'])
    height=texture(mat,ps,'GrimeHeight',rich_paths['Height'],True)
    surface=texture(mat,ps,'GrimeSurface',rich_paths['Roughness'],True)
    normal_map=texture(mat,ps,'GrimeNormal',rich_paths['Normal'],normal=True)
    detail=custom(mat,cs,label,DETAIL,dict(common,Height=height,Surface=surface),u.CustomMaterialOutputType.CMOT_FLOAT4)
    color=custom(mat,cs,label+' pigment',PIGMENT,dict(common,Pigment=pigment),u.CustomMaterialOutputType.CMOT_FLOAT3)
    slope=custom(mat,cs,label+' normal slope',SLOPE,dict(common,NormalMap=normal_map),u.CustomMaterialOutputType.CMOT_FLOAT3)
    return detail,color,slope

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
    mat.set_editor_property('shading_model',u.MaterialShadingModel.MSM_DEFAULT_LIT)
    mat.set_editor_property('tangent_space_normal',False)
    # The 1024px Substance recipe has roughly 12 granule cells and five broad
    # cloud islands per tile: 36cm keeps them visible from the gameplay camera.
    tile=scalar(mat,ps,'GrimeDetailSize',36.)
    seed=scalar(mat,ps,'GrimeSeed',1.)
    strength=scalar(mat,ps,'GrimeMicroRelief',1.15)
    normal_strength=scalar(mat,ps,'GrimeNormalStrength',.65)
    light=vector(mat,ps,'GrimeLightColor',(.42,.21,.065,1))
    dark=vector(mat,ps,'GrimeDarkColor',(.105,.032,.008,1))
    rim=vector(mat,ps,'GrimeRimColor',(.53,.30,.10,1))
    detail,pigment,slope=rich_detail(mat,cs,ps,'MC Care: anchored plaque detail',
        position,local_normal,ps['GrimeScale'],tile,seed,ps['GrimeAmount'])
    custom(mat,cs,'MC Care: coatingColor','''
float body=saturate(F.z*.62+(1-D.x)*.28+F.w*.10);
float3 color=lerp(lerp(Light,Dark,smoothstep(.06,.83,body)),Pigment,.72);
color*=lerp(.54,1.22,smoothstep(.08,.92,D.x))*D.y;
float margin=1-smoothstep(.18,.56,F.x);
color=lerp(color,Rim,margin*.38);
return lerp(color,float3(.92,.97,.94),F.y*.96);
''',{'D':detail,'Pigment':pigment,'Light':light,'Dark':dark,'Rim':rim},u.CustomMaterialOutputType.CMOT_FLOAT3)
    custom(mat,cs,'MC Care: coatingRoughness','''
float margin=1-smoothstep(.18,.56,F.x);
float rough=lerp(.33,.83,D.z)+(1-D.y)*.06;
rough=lerp(rough,.17,margin*.84);
return lerp(rough,.43,F.y);
''',{'D':detail},u.CustomMaterialOutputType.CMOT_FLOAT1)
    custom(mat,cs,'MC Care: meniscusNormal','''
float3 n=normalize(N);
// The mesh supplies the silhouette; these centimetre-scale derivatives add
// built-up body, tapered edges and actual pits without moving contact geometry.
float coverage=saturate(F.x);
float h=coverage*(.10+.64*smoothstep(.12,.92,F.z)+D.w*max(Strength,0));
float3 dx=ddx(World),dy=ddy(World),rx=cross(dy,n),ry=cross(n,dx);
float det=dot(dx,rx);
// Map the rest-space normal slopes through current surface derivatives. This
// remains attached under rotation and pose instead of treating them as world XY.
float sx=ddx(h)+coverage*max(NormalStrength,0)*dot(Slope,ddx(P*Scale));
float sy=ddy(h)+coverage*max(NormalStrength,0)*dot(Slope,ddy(P*Scale));
return normalize(n-(sx*rx+sy*ry)*sign(det)/max(abs(det),1e-5));
''',{'D':detail,'Slope':slope,'P':position,'Scale':ps['GrimeScale'],
     'Strength':strength,'NormalStrength':normal_strength},u.CustomMaterialOutputType.CMOT_FLOAT3)
    scalar(mat,ps,'Specular',.52)
    errors=list(edit.recompile_material(mat))
    assert not errors, errors
    assert lib.save_loaded_asset(mat,False)
    report['materials'][paths[0]]={'compile_errors':errors,'detail_size_cm':36,'coverage_preserved':True,
        'relief':'Substance pits/plateaus + rest normal slopes + coating body/edge',
        'geometry':'Existing procedural relief; no WPO or contact geometry changes'}

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
    mat.set_editor_property('shading_model',u.MaterialShadingModel.MSM_DEFAULT_LIT)
    assert mat.get_editor_property('tangent_space_normal'), 'Preserve the player world-to-tangent normal chain'
    tile=scalar(mat,ps,'GrimeDetailSize',24.)
    seed=scalar(mat,ps,'GrimeDetailSeed',1.)
    strength=scalar(mat,ps,'GrimeMicroRelief',.85)
    normal_strength=scalar(mat,ps,'GrimeNormalStrength',.55)
    light=vector(mat,ps,'GrimeLightColor',(.42,.21,.065,1))
    dark=vector(mat,ps,'GrimeDarkColor',(.105,.032,.008,1))
    rim=vector(mat,ps,'GrimeRimColor',(.53,.30,.10,1))
    detail,pigment,slope=rich_detail(mat,cs,ps,'Anchored player grime detail',
        position,interpolator,scale,tile,seed,ps['Coffee'])
    custom(mat,cs,'Gameplay stains over the original textured enamel','''
float body=saturate(Stain.y*.68+(1-D.x)*.28);
float3 dirt=lerp(lerp(GrimeLight,GrimeDark,body),Pigment,.72);
dirt*=lerp(.54,1.22,smoothstep(.08,.92,D.x))*D.y;
dirt=lerp(dirt,GrimeRim,Stain.z*.38);
float enamel=smoothstep(.20,.48,min(Enamel.r,min(Enamel.g,Enamel.b)));
float patch=Stain.x*saturate(GrimeOpacity)*enamel;
float3 color=lerp(Enamel,dirt,patch);
'''+tail,{'D':detail,'Pigment':pigment},u.CustomMaterialOutputType.CMOT_FLOAT3)
    custom(mat,cs,'Matte player grime','''
float enamel=smoothstep(.20,.48,min(Enamel.r,min(Enamel.g,Enamel.b)));
float rough=lerp(.33,.83,D.z)+(1-D.y)*.06;
rough=lerp(rough,.18,Stain.z*.84);
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
float coverage=Stain.x*enamel*saturate(Opacity);
float plaque=coverage*(max(Strength,0)*D.w+.35*Stain.y);
float height=max(Depth,0)*Fracture.z+plaque;
float sx=ddx(height)+coverage*max(NormalStrength,0)*dot(Slope,ddx(P*Scale));
float sy=ddy(height)+coverage*max(NormalStrength,0)*dot(Slope,ddy(P*Scale));
float3 gradient=sign(determinant)*(sx*rx+sy*ry);
return normalize(max(abs(determinant),.00001)*n-gradient);
''',{'Stain':cs['Rounded player grime mask'],'D':detail,'Slope':slope,'Strength':strength,
     'NormalStrength':normal_strength,'P':position,'Scale':scale,'Opacity':ps['GrimeOpacity'],
     'Enamel':by_name['MaterialExpressionLinearInterpolate_1']},u.CustomMaterialOutputType.CMOT_FLOAT3)
    errors=list(edit.recompile_material(mat))
    assert not errors,errors
    assert lib.save_loaded_asset(mat,False)
    report['materials'][paths[1]]={'compile_errors':errors,'detail_size_rest_cm':24,
        'relief':'Substance height/roughness/pigment and pose-following normal slopes',
        'preserved':['Coffee mask','face exclusion','fractures','HitFlash','BodyStretch','artist texture normals']}

for path in instances:
    instance=lib.load_asset(path)
    if instance:
        # These saved instances carry palette overrides from the earlier look.
        for name,value in [('GrimeLightColor',(.42,.21,.065,1)),('GrimeDarkColor',(.105,.032,.008,1)),('GrimeRimColor',(.53,.30,.10,1))]:
            edit.set_material_instance_vector_parameter_value(instance,name,u.LinearColor(*value))
        edit.update_material_instance(instance)
        lib.save_loaded_asset(instance,True)
for path in paths:
    stats=edit.get_statistics(lib.load_asset(path))
    report['materials'][path]['statistics']={name:getattr(stats,name) for name in
        ('num_vertex_shader_instructions','num_pixel_shader_instructions','num_samplers','num_pixel_texture_samples')}
(saved/'GrimeReview'/'MaterialBuild.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
u.log('MC_GRIME_OPTICS_SAVED '+json.dumps(report))
