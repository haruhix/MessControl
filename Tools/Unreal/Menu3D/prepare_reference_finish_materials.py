"""Create new physical PBR masters and an isolated native look-dev map.

Does not bind materials to the blocking mesh or change the accepted source map.
Texture maps are parameters; final look is calibrated only after v08 geometry.
"""
import json
from pathlib import Path
import unreal as u

root=Path(u.Paths.project_dir())
base='/Game/UI/MainMenu3D/ReferenceFinish'
levels=u.get_editor_subsystem(u.LevelEditorSubsystem)
assert not levels.is_in_play_in_editor()
world=u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
assert world.get_path_name().startswith('/Game/UI/MainMenu3D/L_MainMenu3D')
assert not u.EditorLoadingAndSavingUtils.get_dirty_map_packages()
source_map='/Game/UI/MainMenu3D/L_MainMenu3D'
work_map='/Game/UI/MainMenu3D/L_MainMenu3D_RefFinish_v08'
if not u.EditorAssetLibrary.does_asset_exist(work_map):
    assert u.EditorAssetLibrary.duplicate_asset(source_map,work_map)
    assert u.EditorAssetLibrary.save_asset(work_map)
assets=u.AssetToolsHelpers.get_asset_tools()
inputs=root/'ArtSource/UI/MainMenu3D/20261015_refB_v1/Assembly/UnrealHandoff/v08_ReferenceFinish/ShaderDefaults'
textures={}
for name,srgb,normal in [('White',True,False),('WhiteMask',False,False),('Gray',False,False),('Normal',False,True)]:
    path=base+'/Textures/T_Default_'+name
    tex=u.load_asset(path)
    if not tex:
        task=u.AssetImportTask()
        source_name='White' if name=='WhiteMask' else name
        for key,value in [('filename',str(inputs/(source_name+'.png'))),('destination_path',base+'/Textures'),('destination_name','T_Default_'+name),('automated',True),('save',False)]:
            task.set_editor_property(key,value)
        assets.import_asset_tasks([task]);tex=u.load_asset(path)
    assert tex
    tex.set_editor_property('srgb',srgb)
    tex.set_editor_property('compression_settings',u.TextureCompressionSettings.TC_NORMALMAP if normal else
        u.TextureCompressionSettings.TC_MASKS if name in ('WhiteMask','Gray') else u.TextureCompressionSettings.TC_DEFAULT)
    u.EditorAssetLibrary.save_loaded_asset(tex);textures[name]=tex

def node(mat,cls,**fields):
    ob=u.MaterialEditingLibrary.create_material_expression(mat,cls)
    for key,value in fields.items():ob.set_editor_property(key,value)
    return ob
def connect(a,b,pin,out=''):
    names=[str(n) for n in u.MaterialEditingLibrary.get_material_expression_input_names(b)]
    if pin=='Input' and pin not in names:pin=names[0]
    assert u.MaterialEditingLibrary.connect_material_expressions(a,out,b,pin),(a,b,pin)
def output(a,prop,out=''):
    assert u.MaterialEditingLibrary.connect_material_property(a,out,prop)
def scalar(mat,name,value):return node(mat,u.MaterialExpressionScalarParameter,parameter_name=name,default_value=value)
def vector(mat,name,color):return node(mat,u.MaterialExpressionVectorParameter,parameter_name=name,default_value=u.LinearColor(*color,1.))
def texture(mat,name,default,normal=False):
    t=node(mat,u.MaterialExpressionTextureSampleParameter2D,parameter_name=name,texture=textures[default])
    if normal:t.set_editor_property('sampler_type',u.MaterialSamplerType.SAMPLERTYPE_NORMAL)
    elif default in ('Gray','WhiteMask'):t.set_editor_property('sampler_type',u.MaterialSamplerType.SAMPLERTYPE_MASKS)
    return t

def upgrade_data_samplers(mat):
    # These indices come from this deterministic builder. Assert each parameter
    # before upgrading existing v08 masters so the task never edits another node.
    for index,name,default in [(1,'RoughnessTexture','Gray'),(3,'AOTexture','WhiteMask'),(4,'ThicknessTexture','Gray')]:
        expression=u.load_object(None,mat.get_path_name()+':MaterialExpressionTextureSampleParameter2D_'+str(index))
        assert expression and str(expression.get_editor_property('parameter_name'))==name
        expression.set_editor_property('texture',textures[default])
        expression.set_editor_property('sampler_type',u.MaterialSamplerType.SAMPLERTYPE_MASKS)
    u.MaterialEditingLibrary.recompile_material(mat)
    u.EditorAssetLibrary.save_loaded_asset(mat)

report={'status':'UNBOUND_MATERIAL_MASTERS_CREATED_PENDING_GEOMETRY_TEXTURE_LOOK_REVIEW','work_map':work_map,'materials':[]}
recipes=[('Enamel',(.89,.74,.55),.36,.55,(.9,.7,.4),.78),
    ('Gum',(.665,.029,.045),.5,.5,(.8,.07,.1),.55),
    ('Interior',(.055,.002,.006),.7,.45,(.35,.008,.025),.7),
    ('Tongue',(.35,.018,.035),.5,.5,(.55,.035,.06),.6)]
for name,color,rough,spec,scatter,density in recipes:
    asset_name='M_Menu'+name+'_v08';path=base+'/Materials/'+asset_name
    mat=u.load_asset(path)
    if mat:
        upgrade_data_samplers(mat)
        report['materials'].append({'path':path,'status':'existing_with_explicit_linear_mask_samplers'})
        continue
    mat=assets.create_asset(asset_name,base+'/Materials',u.Material,u.MaterialFactoryNew());assert mat
    for key,value in [('shading_model',u.MaterialShadingModel.MSM_SUBSURFACE),('blend_mode',u.BlendMode.BLEND_MASKED),
        ('two_sided',False),('used_with_skeletal_mesh',True),('used_with_morph_targets',True),('opacity_mask_clip_value',.01)]:
        mat.set_editor_property(key,value)
    bc=texture(mat,'BaseColorTexture','White');tint=vector(mat,'BaseColorTint',color)
    mul=node(mat,u.MaterialExpressionMultiply);connect(bc,mul,'A','RGB');connect(tint,mul,'B');output(mul,u.MaterialProperty.MP_BASE_COLOR)
    roughtex=texture(mat,'RoughnessTexture','Gray');roughscale=scalar(mat,'RoughnessScale',rough)
    rm=node(mat,u.MaterialExpressionMultiply);connect(roughtex,rm,'A','R');connect(roughscale,rm,'B');output(rm,u.MaterialProperty.MP_ROUGHNESS)
    output(scalar(mat,'Specular',spec),u.MaterialProperty.MP_SPECULAR)
    normal=texture(mat,'NormalTexture','Normal',True);output(normal,u.MaterialProperty.MP_NORMAL,'RGB')
    ao=texture(mat,'AOTexture','WhiteMask');output(ao,u.MaterialProperty.MP_AMBIENT_OCCLUSION,'R')
    output(vector(mat,'SubsurfaceTint',scatter),u.MaterialProperty.MP_SUBSURFACE_COLOR)
    thickness=texture(mat,'ThicknessTexture','Gray');densityscale=scalar(mat,'ScatterDensity',density)
    dm=node(mat,u.MaterialExpressionMultiply);connect(thickness,dm,'A','R');connect(densityscale,dm,'B');output(dm,u.MaterialProperty.MP_OPACITY)
    pos=node(mat,u.MaterialExpressionWorldPosition)
    local=node(mat,u.MaterialExpressionTransformPosition,transform_source_type=u.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_WORLD,
        transform_type=u.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_LOCAL)
    z=node(mat,u.MaterialExpressionComponentMask,r=False,g=False,b=True,a=False)
    absolute=node(mat,u.MaterialExpressionAbs);half=scalar(mat,'CardHalfHeight',344.)
    distance=node(mat,u.MaterialExpressionSubtract)
    connect(pos,local,'Input');connect(local,z,'Input');connect(z,absolute,'Input');connect(half,distance,'A');connect(absolute,distance,'B')
    output(distance,u.MaterialProperty.MP_OPACITY_MASK)
    scalar(mat,'OpenAmount',1.)
    u.MaterialEditingLibrary.recompile_material(mat)
    u.EditorAssetLibrary.save_loaded_asset(mat)
    report['materials'].append({'path':path,'shading':'Subsurface physical PBR','status':'created','radiance_or_emissive_nodes':0})
(root/'Tools/Unreal/Menu3D/Evidence/v08_material_master_report.json').write_text(json.dumps(report,indent=2),encoding='utf8')
u.log('MC_REF_MATERIALS '+json.dumps(report))
