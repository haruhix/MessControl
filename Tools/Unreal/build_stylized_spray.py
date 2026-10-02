"""Author a smoke-like treatment spray in the running UE 5.8 editor.

Reference: https://www.youtube.com/watch?v=HRagD5L-WF8
Run with PIE stopped. The original Niagara system is backed up on first run.
"""
import json
import math
from pathlib import Path
import unreal as u

ROOT = '/Game/Gameplay/VFX'
SYSTEM = ROOT + '/NS_SprayMist'
BACKUP = ROOT + '/Backups/NS_SprayMist_BeforeStylized'
FOLDER = ROOT + '/Spray'
lib = u.EditorAssetLibrary
edit = u.MaterialEditingLibrary
tools = u.AssetToolsHelpers.get_asset_tools()
assert not u.EditorLevelLibrary.get_pie_worlds(False), 'Stop PIE before authoring.'
system = u.load_asset(SYSTEM)
assert system
if not lib.does_asset_exist(BACKUP):
    backup = lib.duplicate_asset(SYSTEM, BACKUP)
    assert backup and lib.save_loaded_asset(backup, only_if_is_dirty=False)

# A closed low-poly lobe, with spherical UVs, rather than a smooth water droplet.
# The source radius matches the existing emitter's .022-.034 initialization scale.
source = Path(u.Paths.project_dir()) / 'ArtSource/VFX/Spray/SM_SprayCloud.obj'
source.parent.mkdir(parents=True, exist_ok=True)
segments, rings = 24, 12
lines = ['# Procedural smoke lobe; 300 cm source radius; UE centimeters', 'o SM_SprayCloud']
vertices = []
for j in range(rings + 1):
    theta = math.pi * max(.0001, min(.9999, j / rings))
    for i in range(segments + 1):
        phi = 2 * math.pi * i / segments
        x, y, z = math.sin(theta)*math.cos(phi), math.sin(theta)*math.sin(phi), math.cos(theta)
        radius = 300 * (1 + .15*math.sin(3*phi + 1.2)*math.sin(theta)**2 + .13*math.cos(4*theta-phi)*math.sin(theta))
        vertices.append((x*radius, y*radius, z*radius*.92))
        lines.append('v %.6f %.6f %.6f' % vertices[-1])
for j in range(rings + 1):
    for i in range(segments + 1):
        lines.append('vt %.6f %.6f' % (i/segments, j/rings))
lines.append('s 1')
for j in range(rings):
    for i in range(segments):
        a=j*(segments+1)+i+1; b=a+1; c=a+segments+1; d=c+1
        # Winding follows the outward normal of the latitude/longitude surface.
        lines.extend(['f %d/%d %d/%d %d/%d' % (a,a,c,c,b,b), 'f %d/%d %d/%d %d/%d' % (b,b,c,c,d,d)])
source.write_text('\n'.join(lines)+'\n', encoding='ascii')
mesh = u.load_asset(FOLDER + '/SM_SprayCloud')
if not mesh:
    task = u.AssetImportTask()
    task.set_editor_properties(dict(filename=str(source), destination_path=FOLDER,
        destination_name='SM_SprayCloud', automated=True, replace_existing=False, save=True))
    options = u.FbxImportUI()
    options.set_editor_properties(dict(import_mesh=True, import_materials=False, import_textures=False,
        import_as_skeletal=False, mesh_type_to_import=u.FBXImportType.FBXIT_STATIC_MESH))
    data = options.get_editor_property('static_mesh_import_data')
    data.set_editor_properties(dict(auto_generate_collision=False, generate_lightmap_u_vs=False,
        normal_import_method=u.FBXNormalImportMethod.FBXNIM_COMPUTE_NORMALS))
    task.set_editor_property('options', options)
    tools.import_asset_tasks([task])
    mesh = u.load_asset(FOLDER + '/SM_SprayCloud')
assert isinstance(mesh, u.StaticMesh), 'Cloud mesh import failed.'

mat_path = FOLDER + '/M_SprayCloud'
mat = u.load_asset(mat_path)
if mat is None:
    mat = tools.create_asset('M_SprayCloud', FOLDER, u.Material, u.MaterialFactoryNew())
assert isinstance(mat, u.Material)
edit.delete_all_material_expressions(mat)
mat.set_editor_properties(dict(blend_mode=u.BlendMode.BLEND_TRANSLUCENT,
    shading_model=u.MaterialShadingModel.MSM_UNLIT, two_sided=True,
    used_with_niagara_mesh_particles=True))

def node(cls, x, y, **props):
    n=edit.create_material_expression(mat, cls, x, y)
    n.set_editor_properties(props)
    return n
def connect(a, out, b, pin):
    assert edit.connect_material_expressions(a,out,b,pin), pin
def scalar(name, value, y):
    return node(u.MaterialExpressionScalarParameter,-1600,y,parameter_name=name,
        default_value=value,group='Spray Cloud')
def custom(label, code, inputs, x, y, output):
    n=node(u.MaterialExpressionCustom,x,y,description=label,code=code,output_type=output)
    entries=[]
    for key in inputs:
        entry=u.CustomInput();entry.set_editor_property('input_name',key);entries.append(entry)
    n.set_editor_property('inputs',entries)
    for key,(other,pin) in inputs.items():connect(other,pin,n,key)
    return n
uv=node(u.MaterialExpressionTextureCoordinate,-1900,-300)
time=node(u.MaterialExpressionTime,-1900,-120)
# Lightweight emitters do not output Particles.NormalizedAge. Pass age through
# DynamicMaterialParameter.x so both the material and stateless simulation agree.
age=node(u.MaterialExpressionDynamicParameter,-1900,40,
    param_names=['Normalized Age','Unused Y','Unused Z','Unused W'])
color=node(u.MaterialExpressionParticleColor,-1900,200)
normal=node(u.MaterialExpressionPixelNormalWS,-1900,370)
view=node(u.MaterialExpressionCameraVectorWS,-1900,530)
vertexnormal=node(u.MaterialExpressionVertexNormalWS,-1900,710)
density=scalar('Density',.54,-320)
erosion=scalar('Erosion',.36,-160)
flow=scalar('Noise Flow',1.7,0)
detail=scalar('Noise Scale',3.4,160)
deform=scalar('Billow Displacement (cm)',1.5,650)
noise=r'''
struct SmokeNoise {
 float hash(float3 p) { return frac(sin(dot(p,float3(127.1,311.7,74.7)))*43758.5453); }
 float value(float3 p) {
  float3 i=floor(p),f=frac(p); f=f*f*(3.0-2.0*f);
  return lerp(lerp(lerp(hash(i),hash(i+float3(1,0,0)),f.x),lerp(hash(i+float3(0,1,0)),hash(i+float3(1,1,0)),f.x),f.y),
   lerp(lerp(hash(i+float3(0,0,1)),hash(i+float3(1,0,1)),f.x),lerp(hash(i+float3(0,1,1)),hash(i+1),f.x),f.y),f.z);
 }
}; SmokeNoise S;
float a=UV.x*6.2831853,b=UV.y*3.14159265;
float3 q=float3(sin(b)*cos(a),sin(b)*sin(a),cos(b))*Scale;
q+=float3(0.19,-0.13,-0.62)*(T*Flow+Age*2.0);
float warp=S.value(q*1.4+7.3)-0.5;
q+=warp*0.7;
float field=S.value(q)*0.64+S.value(q*2.07+11.0)*0.26+S.value(q*4.17+23.0)*0.10;
'''
smoke=custom('Animated 3D billows, soft edge and age erosion',noise+r'''
float cut=0.28+saturate(Age)*Erosion;
float holes=smoothstep(cut,cut+0.17,field);
float face=abs(dot(normalize(N),normalize(V)));
float softEdge=smoothstep(0.04,0.32,face);
float life=smoothstep(0.0,0.08,Age)*(1.0-smoothstep(0.53,1.0,Age));
float opacity=holes*softEdge*life*Density;
float shade=lerp(0.67,1.1,smoothstep(0.3,0.72,field))*lerp(0.88,1.0,face);
return float4(shade,shade,shade,opacity);
''',{'UV':(uv,''),'T':(time,''),'Age':(age,''),'Scale':(detail,''),'Flow':(flow,''),
    'Erosion':(erosion,''),'Density':(density,''),'N':(normal,''),'V':(view,'')},
    -900,-200,u.CustomMaterialOutputType.CMOT_FLOAT4)
emissive=node(u.MaterialExpressionMultiply,-500,-300)
brightness=node(u.MaterialExpressionComponentMask,-650,-300,r=True,g=True,b=True,a=False)
connect(smoke,'',brightness,'')
connect(brightness,'',emissive,'A');connect(color,'RGB',emissive,'B')
assert edit.connect_material_property(emissive,'',u.MaterialProperty.MP_EMISSIVE_COLOR)
alpha=node(u.MaterialExpressionComponentMask,-600,0,r=False,g=False,b=False,a=True)
connect(smoke,'',alpha,'')
fade=node(u.MaterialExpressionDepthFade,-340,40,fade_distance_default=14)
connect(alpha,'',fade,'')
opacity=node(u.MaterialExpressionMultiply,-100,40)
connect(fade,'',opacity,'A');connect(color,'A',opacity,'B')
assert edit.connect_material_property(opacity,'',u.MaterialProperty.MP_OPACITY)
wpo=custom('Moving billow silhouette',noise+'return N*(field-0.48)*Amplitude*smoothstep(0.0,0.15,Age);',
    {'UV':(uv,''),'T':(time,''),'Age':(age,''),'Scale':(detail,''),'Flow':(flow,''),
     'N':(vertexnormal,''),'Amplitude':(deform,'')},-850,600,u.CustomMaterialOutputType.CMOT_FLOAT3)
assert edit.connect_material_property(wpo,'',u.MaterialProperty.MP_WORLD_POSITION_OFFSET)
errors=edit.recompile_material(mat)
assert not errors, list(errors)
assert lib.save_loaded_asset(mat,only_if_is_dirty=False)

emitter=u.find_object(system,'Fountain_1')
assert emitter
modules={m.get_class().get_name().removeprefix('NiagaraStatelessModule_'):m for m in emitter.get_editor_property('Modules')}
def distribution(obj, prop, content):
    value=obj.get_editor_property(prop).copy()
    assert value.import_text(content), (prop,content)
    obj.set_editor_property(prop,value)
def constant_float(obj,prop,value):
    distribution(obj,prop,'(Mode=UniformConstant,Min=%s,Max=%s,ChannelConstantsAndRanges=(%s),ChannelCurves=)'%(value,value,value))
def range_float(obj,prop,low,high):
    distribution(obj,prop,'(Mode=UniformRange,Min=%s,Max=%s,ChannelConstantsAndRanges=(%s,%s),ChannelCurves=)'%(low,high,low,high))
def vector_constant(obj,prop,x,y,z):
    distribution(obj,prop,'(Mode=NonUniformConstant,Min=(X=%s,Y=%s,Z=%s),Max=(X=%s,Y=%s,Z=%s),ChannelConstantsAndRanges=(%s,%s,%s),ChannelCurves=)'%(x,y,z,x,y,z,x,y,z))

with u.ScopedEditorTransaction('Stylized smoke treatment spray'):
    init=modules['InitializeParticle']
    range_float(init,'LifetimeDistribution',.36,.48)
    distribution(init,'ColorDistribution','(Mode=NonUniformConstant,Values=((R=0.86,G=0.95,B=1,A=1)),ChannelConstantsAndRanges=(0.86,0.95,1,1),ChannelCurves=)')
    velocity=modules['AddVelocity']
    range_float(velocity,'ConeVelocityDistribution',520,680)
    velocity.set_editor_property('ConeAngle',18)
    constant_float(modules['Drag'],'DragDistribution',.85)
    vector_constant(modules['GravityForce'],'GravityDistribution',0,0,-35)
    curl=modules['CurlNoiseForce'];curl.set_editor_properties({'bModuleEnabled':True,'NoiseStrength':70.0,'NoiseFrequency':.035})
    dynamic=modules['DynamicMaterialParameters']
    dynamic.set_editor_properties({'bModuleEnabled':True,'bParameter0Enabled':True})
    parameter=dynamic.get_editor_property('Parameter0').copy()
    parameter.set_editor_properties({'bXChannelEnabled':True,'bYChannelEnabled':False,
        'bZChannelEnabled':False,'bWChannelEnabled':False})
    distribution(parameter,'XChannelDistribution',
        '(Mode=UniformCurve,Values=(0,1),ValuesTimeRange=(X=0,Y=1),ChannelConstantsAndRanges=,ChannelCurves=((Keys=((InterpMode=RCIM_Linear,Time=0,Value=0),(InterpMode=RCIM_Linear,Time=1,Value=1)))))')
    dynamic.set_editor_property('Parameter0',parameter)
    rotation=modules['InitialMeshOrientation'];rotation.set_editor_property('bModuleEnabled',True)
    distribution(rotation,'Rotation','(Mode=NonUniformRange,Min=(X=0,Y=0,Z=0),Max=(X=360,Y=360,Z=360),ChannelConstantsAndRanges=(0,0,0,360,360,360),ChannelCurves=)')
    spin=modules['MeshRotationRate'];spin.set_editor_property('bModuleEnabled',True)
    distribution(spin,'RotationRateDistribution','(Mode=NonUniformRange,Min=(X=-65,Y=-90,Z=-75),Max=(X=65,Y=90,Z=75),ChannelConstantsAndRanges=(-65,-90,-75,65,90,75),ChannelCurves=)')
    scale=modules['ScaleMeshSize']
    curve='((InterpMode=RCIM_Cubic,Time=0,Value=0.22),(InterpMode=RCIM_Cubic,Time=0.18,Value=0.72),(InterpMode=RCIM_Cubic,Time=0.5,Value=1.6),(InterpMode=RCIM_Cubic,Time=0.8,Value=2.5),(InterpMode=RCIM_Cubic,Time=1,Value=2.8))'
    distribution(scale,'ScaleDistribution','(Mode=UniformCurve,ChannelConstantsAndRanges=,ChannelCurves=((Keys=%s),(Keys=%s),(Keys=%s)))'%(curve,curve,curve))
    # Array and non-Blueprint Niagara properties use their actual reflected names.
    spawns=list(emitter.get_editor_property('SpawnInfos'))
    for spawn in spawns:
        constant_float(spawn,'Rate',145)
    emitter.set_editor_property('SpawnInfos',spawns)
    renderers=list(emitter.get_editor_property('RendererProperties'))
    assert len(renderers)==1
    renderer=renderers[0]
    meshes=list(renderer.get_editor_property('Meshes'))
    assert len(meshes)==1
    meshes[0].set_editor_property('mesh',mesh)
    renderer.set_editor_property('Meshes',meshes)
    overrides=list(renderer.get_editor_property('OverrideMaterials'))
    overrides[0].set_editor_property('ExplicitMat',mat)
    renderer.set_editor_property('OverrideMaterials',overrides)
    # Reapply the array to notify the owner, rebuild stateless data and instances.
    emitter.set_editor_property('RendererProperties',renderers)
    bounds=u.Box(min=u.Vector(-120,-120,-60),max=u.Vector(120,120,340))
    emitter.set_editor_property('FixedBounds',bounds)
    system.set_editor_property('fixed_bounds',bounds)
    lib.set_metadata_tag(system,'Spray.Reference','https://www.youtube.com/watch?v=HRagD5L-WF8')
    lib.set_metadata_tag(system,'Spray.Backup',BACKUP)
    assert lib.save_loaded_asset(mesh,only_if_is_dirty=False)
    assert lib.save_loaded_asset(system,only_if_is_dirty=False)

report=dict(system=SYSTEM,backup=BACKUP,mesh=mesh.get_path_name(),material=mat_path,
    rate=145,lifetime=[.36,.48],cone_degrees=18,velocity=[520,680],
    curl=dict(strength=70,frequency=.035),material_age='DynamicMaterialParameter.x',
    compile_errors=list(errors),source_mesh=str(source))
Path(u.Paths.project_saved_dir()+'StylizedSprayBuild.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
subsystem=u.get_editor_subsystem(u.AssetEditorSubsystem)
subsystem.close_all_editors_for_asset(system)
subsystem.open_editor_for_assets([system])
u.log('STYLIZED_SPRAY_READY '+json.dumps(report))
