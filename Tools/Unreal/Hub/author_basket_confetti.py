"""Author owned paper meshes/material and a bounded one-shot lightweight Niagara effect.
Run in the idle Unreal editor. Never saves Hub or unrelated designer packages.
"""
import unreal as u
import json, math
from pathlib import Path

FOLDER='/Game/Hub/VFX'
ROOT=Path(u.Paths.project_dir())
OUT=ROOT/'Saved/HubBasketballConfetti_20261021'
OUT.mkdir(exist_ok=True)
assert not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor()
lib=u.EditorAssetLibrary
tools=u.AssetToolsHelpers.get_asset_tools()
edit=u.MaterialEditingLibrary
assert not lib.does_asset_exist(FOLDER+'/NS_BasketConfetti'), 'Refuse to overwrite an existing authored effect'

def save(asset):
    assert lib.save_loaded_asset(asset,only_if_is_dirty=False),asset.get_path_name()

def dist(obj,prop,text):
    v=obj.get_editor_property(prop).copy()
    assert v.import_text(text),(prop,text)
    obj.set_editor_property(prop,v)

def scalar(obj,prop,lo,hi):
    dist(obj,prop,f'(Mode=UniformRange,Min={lo},Max={hi},ParameterBinding=(Name=""),ChannelConstantsAndRanges=({lo},{hi}),ChannelCurves=)')

def vector(obj,prop,lo,hi):
    v=lambda a:f'(X={a[0]},Y={a[1]},Z={a[2]})'
    values=','.join(map(str,lo+hi))
    dist(obj,prop,f'(Mode=NonUniformRange,Min={v(lo)},Max={v(hi)},ParameterBinding=(Name=""),ChannelConstantsAndRanges=({values}),ChannelCurves=)')

mat=tools.create_asset('M_ConfettiPaper',FOLDER,u.Material,u.MaterialFactoryNew())
assert mat
mat.set_editor_properties({'two_sided':True,'blend_mode':u.BlendMode.BLEND_OPAQUE,'shading_model':u.MaterialShadingModel.MSM_DEFAULT_LIT})
edit.set_material_usage(mat,u.MaterialUsage.MATUSAGE_NIAGARA_MESH_PARTICLES)
particle=edit.create_material_expression(mat,u.MaterialExpressionParticleColor,-700,0)
palette=edit.create_material_expression(mat,u.MaterialExpressionCustom,-420,0)
entry=u.CustomInput();entry.set_editor_property('input_name','Seed')
palette.set_editor_properties({'inputs':[entry],'output_type':u.CustomMaterialOutputType.CMOT_FLOAT3,
 'description':'Six saturated paper colors, constant for each particle',
 'code':'int i=min(5,(int)floor(saturate(Seed.r)*6));\n'
 'if(i==0)return float3(1,0.025,0.04);\nif(i==1)return float3(0.02,0.19,1);\n'
 'if(i==2)return float3(1,0.65,0.008);\nif(i==3)return float3(0.015,0.72,0.045);\n'
 'if(i==4)return float3(0.58,0.025,1);\nreturn float3(1,0.025,0.42);'})
assert edit.connect_material_expressions(particle,'RGB',palette,'Seed')
assert edit.connect_material_property(palette,'',u.MaterialProperty.MP_BASE_COLOR)
fill=edit.create_material_expression(mat,u.MaterialExpressionMultiply,-140,190)
fill.set_editor_property('const_b',.45)
edit.connect_material_expressions(palette,'',fill,'A')
edit.connect_material_property(fill,'',u.MaterialProperty.MP_EMISSIVE_COLOR)
rough=edit.create_material_expression(mat,u.MaterialExpressionConstant,-150,320);rough.set_editor_property('r',.68)
edit.connect_material_property(rough,'',u.MaterialProperty.MP_ROUGHNESS)
edit.recompile_material(mat);save(mat)

source=ROOT/'ArtSource/VFX/Confetti';source.mkdir(parents=True,exist_ok=True)
meshes=[]
for name,ribbon in [('SM_ConfettiPaper',False),('SM_ConfettiRibbon',True)]:
    n=24 if ribbon else 4
    lines=['# Owned procedural confetti, centimeters; no downloaded source assets']
    for i in range(n+1):
        t=i/n
        for side in [-1,1]:
            if ribbon:
                a=t*math.pi*4
                x=(3.2+side*.9)*math.cos(a);y=(3.2+side*.9)*math.sin(a);z=(t-.5)*24
            else:
                x=side*2.7;y=(t-.5)*9;z=math.sin(t*math.pi)*1.2
            lines.append(f'v {x:.6f} {y:.6f} {z:.6f}')
    for i in range(n+1):
        lines.extend([f'vt 0 {i/n:.6f}',f'vt 1 {i/n:.6f}'])
    for i in range(n):
        a=i*2+1;b=a+1;c=a+2;d=a+3
        lines.extend([f'f {a}/{a} {b}/{b} {d}/{d}',f'f {a}/{a} {d}/{d} {c}/{c}'])
    path=source/(name+'.obj');path.write_text('\n'.join(lines)+'\n')
    task=u.AssetImportTask()
    task.set_editor_properties({'filename':str(path),'destination_path':FOLDER,'destination_name':name,'automated':True,'replace_existing':False,'save':False})
    opt=u.FbxImportUI();opt.set_editor_properties({'import_mesh':True,'import_materials':False,'import_textures':False,'import_as_skeletal':False,'mesh_type_to_import':u.FBXImportType.FBXIT_STATIC_MESH})
    opt.static_mesh_import_data.set_editor_properties({'auto_generate_collision':False,'generate_lightmap_u_vs':False,'normal_import_method':u.FBXNormalImportMethod.FBXNIM_COMPUTE_NORMALS,'convert_scene':False,'convert_scene_unit':False})
    task.set_editor_property('options',opt);tools.import_asset_tasks([task])
    mesh=u.load_asset(FOLDER+'/'+name);assert isinstance(mesh,u.StaticMesh)
    mesh.set_material(0,mat);save(mesh);meshes.append(mesh)

system=lib.duplicate_asset('/Game/Gameplay/Cold/VFX/NS_IceShardBurst',FOLDER+'/NS_BasketConfetti');assert system
e=next(x for x in u.ObjectIterator(u.load_class(None,'/Script/Niagara.NiagaraStatelessEmitter')) if x.get_outer()==system)
mods={m.get_class().get_name().removeprefix('NiagaraStatelessModule_'):m for m in e.get_editor_property('Modules')}
spawn=e.get_editor_property('SpawnInfos')[0].copy()
dist(spawn,'Amount','(Mode=UniformConstant,Min=132,Max=132,ParameterBinding=(Name=""),ChannelConstantsAndRanges=(132))')
spawn.set_editor_property('SpawnTime',.01);e.set_editor_property('SpawnInfos',[spawn])
init=mods['InitializeParticle']
scalar(init,'LifetimeDistribution',2.4,3.5)
dist(init,'ColorDistribution','(Mode=NonUniformRange,Values=((R=0,G=0,B=0,A=1),(R=1,G=1,B=1,A=1)),ParameterBinding=(Name=""),ChannelConstantsAndRanges=(0,0,0,1,1,1,1,1),ChannelCurves=)')
vector(init,'MeshScaleDistribution',[1.6,1.6,1.6],[3.,3.,3.])
scalar(mods['ShapeLocation'],'SphereRadius',0,35)
velocity=mods['AddVelocity'];velocity.set_editor_property('ConeAngle',145.)
scalar(velocity,'ConeVelocityDistribution',280,620);scalar(velocity,'ConeVelocityScale',1,1)
vector(mods['GravityForce'],'GravityDistribution',[0,0,-310],[0,0,-310])
scalar(mods['Drag'],'DragDistribution',1.05,1.3)
mods['CurlNoiseForce'].set_editor_properties({'bModuleEnabled':True,'NoiseStrength':28.,'NoiseFrequency':.012})
vector(mods['MeshRotationRate'],'RotationRateDistribution',[-220,-280,-180],[220,280,180])
index=mods['MeshIndex'];index.set_editor_property('bModuleEnabled',True)
dist(index,'MeshIndex','(Mode=UniformRange,Min=0,Max=1,ParameterBinding=(Name=""),ChannelConstantsAndRanges=(0,1))')
index.set_editor_property('MeshIndexWeight',[.88,.12])
# Preserve the template's normalized-age mesh-scale curve: paper gently shrinks away at the end.
renderer=e.get_editor_property('RendererProperties')[0]
entries=[]
for mesh in meshes:
    entry=u.NiagaraMeshRendererMeshProperties();entry.set_editor_property('mesh',mesh);entries.append(entry)
renderer.set_editor_property('Meshes',entries)
override=renderer.get_editor_property('OverrideMaterials')[0].copy();override.set_editor_property('ExplicitMat',mat)
renderer.set_editor_property('OverrideMaterials',[override])
renderer.set_editor_property('bCastShadows',False)
e.set_editor_property('RendererProperties',[renderer])
bounds=u.Box(min=u.Vector(-850,-850,-1000),max=u.Vector(850,850,650))
e.set_editor_property('FixedBounds',bounds);system.set_editor_property('fixed_bounds',bounds)
lib.set_metadata_tag(system,'MC.Confetti.Version','1')
save(system)
report={'system':system.get_path_name(),'particles':132,'paper_ribbon_weights':[.88,.12],'life_seconds':[2.4,3.5],
 'material':'Opaque, two-sided, six-color palette; no translucent overdraw','mesh_triangles':[8,48],
 'mesh_scale_range':[1.6,3.],'paper_emission_fill':.45,
 'dirty_maps_preserved':[p.get_name() for p in u.EditorLoadingAndSavingUtils.get_dirty_map_packages()]}
(OUT/'author.json').write_text(json.dumps(report,indent=2));print('BASKET_CONFETTI_AUTHORED',json.dumps(report))
