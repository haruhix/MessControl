"""Real native 3D blocking: three mouth meshes, morphs, live lights, no art textures.

Only task-owned MainMenu3D assets and its isolated map are modified.
This is a functional layout/motion review, explicitly not final art.
"""
import json
from pathlib import Path
import unreal as u

ROOT=Path(u.Paths.project_dir())/'ArtSource/UI/MainMenu3D/20261015_refB_v1'
BASE='/Game/UI/MainMenu3D'
HANDOFF=ROOT/'Assembly/UnrealHandoff/FunctionalBlocking_DAY01_DAY02_v01'
OUT=ROOT/'Assembly/Review/Unreal_FunctionalBlocking_v01'
OUT.mkdir(parents=True,exist_ok=True)
levels=u.get_editor_subsystem(u.LevelEditorSubsystem)
actors=u.get_editor_subsystem(u.EditorActorSubsystem)
world=u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
assert not levels.is_in_play_in_editor()
assert world.get_path_name().startswith(BASE+'/L_MainMenu3D')
asset_tools=u.AssetToolsHelpers.get_asset_tools()
report={'mode':'FUNCTIONAL_3D_BLOCKING_NOT_FINAL_ART','textures_used':False,'imported':[],'materials':[]}

def skeletal(day):
    name='SK_'+day+'_Blocking'
    path=BASE+'/Meshes/'+name
    if not u.EditorAssetLibrary.does_asset_exist(path):
        options=u.FbxImportUI()
        for name_,value in [('automated_import_should_detect_type',False),('import_mesh',True),
          ('import_as_skeletal',True),('import_materials',False),('import_textures',False),
          ('import_animations',False),('create_physics_asset',False),
          ('mesh_type_to_import',u.FBXImportType.FBXIT_SKELETAL_MESH),
          ('original_import_type',u.FBXImportType.FBXIT_SKELETAL_MESH)]:
            options.set_editor_property(name_,value)
        data=options.get_editor_property('skeletal_mesh_import_data')
        for field,value in [('convert_scene',False),('convert_scene_unit',False),('force_front_x_axis',False),
          ('import_morph_targets',True),('normal_import_method',u.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS)]:
            data.set_editor_property(field,value)
        task=u.AssetImportTask()
        for field,value in [('filename',str(HANDOFF/(name+'.fbx'))),('destination_path',BASE+'/Meshes'),
           ('destination_name',name),('automated',True),('save',True),('replace_existing',False),
           ('options',options),('factory',u.FbxFactory())]:
            task.set_editor_property(field,value)
        asset_tools.import_asset_tasks([task])
    mesh=u.load_asset(path)
    names=[str(x) for x in mesh.get_all_morph_target_names()]
    assert sorted(names)==['Pose_000','Pose_025','Pose_050','Pose_075'],names
    report['imported'].append({'mesh':path,'morphs':names,'bounds':str(mesh.get_bounds())})
    return mesh

def node(mat,cls,**fields):
    ob=u.MaterialEditingLibrary.create_material_expression(mat,cls)
    for name,value in fields.items():ob.set_editor_property(name,value)
    return ob

def link(a,out,b,inp):
    names=[str(n) for n in u.MaterialEditingLibrary.get_material_expression_input_names(b)]
    if inp=='Input' and inp not in names:
        assert names,(b,names)
        inp=names[0]
    assert u.MaterialEditingLibrary.connect_material_expressions(a,out,b,inp),(a,b,inp)

def material(name,color,roughness,closed_half):
    path=BASE+'/Blocking/Materials/'+name
    mat=u.load_asset(path) if u.EditorAssetLibrary.does_asset_exist(path) else asset_tools.create_asset(name,BASE+'/Blocking/Materials',u.Material,u.MaterialFactoryNew())
    u.MaterialEditingLibrary.delete_all_material_expressions(mat)
    for key,value in [('shading_model',u.MaterialShadingModel.MSM_DEFAULT_LIT),
        ('blend_mode',u.BlendMode.BLEND_MASKED),('two_sided',False),
        ('used_with_skeletal_mesh',True),('used_with_morph_targets',True)]:
        mat.set_editor_property(key,value)
    bc=node(mat,u.MaterialExpressionConstant3Vector,constant=u.LinearColor(r=color[0],g=color[1],b=color[2],a=1.))
    rough=node(mat,u.MaterialExpressionConstant,r=roughness)
    u.MaterialEditingLibrary.connect_material_property(bc,'',u.MaterialProperty.MP_BASE_COLOR)
    u.MaterialEditingLibrary.connect_material_property(rough,'',u.MaterialProperty.MP_ROUGHNESS)
    # The physical mesh is trimmed to each card's current height. No screenshot,
    # projected concept, image plane or texture sample participates in this graph.
    pos=node(mat,u.MaterialExpressionWorldPosition)
    local=node(mat,u.MaterialExpressionTransformPosition,
        transform_source_type=u.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_WORLD,
        transform_type=u.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_LOCAL)
    z=node(mat,u.MaterialExpressionComponentMask,r=False,g=False,b=True,a=False)
    absolute=node(mat,u.MaterialExpressionAbs)
    amount=node(mat,u.MaterialExpressionScalarParameter,parameter_name='OpenAmount',default_value=1.)
    half=node(mat,u.MaterialExpressionLinearInterpolate,const_a=closed_half,const_b=340.5)
    distance=node(mat,u.MaterialExpressionSubtract)
    link(pos,'',local,'Input');link(local,'',z,'Input');link(z,'',absolute,'Input')
    link(amount,'',half,'Alpha');link(half,'',distance,'A');link(absolute,'',distance,'B')
    u.MaterialEditingLibrary.connect_material_property(distance,'',u.MaterialProperty.MP_OPACITY_MASK)
    u.MaterialEditingLibrary.recompile_material(mat)
    u.EditorAssetLibrary.save_loaded_asset(mat)
    report['materials'].append({'path':path,'base_color':color,'roughness':roughness,'texture_nodes':0})
    return mat

enamel=material('M_Blocking_Enamel',(.48,.52,.55),.28,213.5)
gum=material('M_Blocking_Gum',(.15,.18,.2),.42,213.5)
enamel3=material('M_Blocking_Enamel_DAY03',(.48,.52,.55),.28,189.48)
gum3=material('M_Blocking_Gum_DAY03',(.15,.18,.2),.42,189.48)
inside=material('M_Blocking_Interior',(.035,.045,.052),.55,189.48)
tongue=material('M_Blocking_Tongue',(.09,.11,.12),.38,189.48)

def tagged(tag):return list(u.GameplayStatics.get_all_actors_with_tag(world,tag))
for day in ('DAY01','DAY02'):
    meshes=skeletal(day)
    found=tagged('Menu3D_'+day)
    actor=found[0] if found else actors.spawn_actor_from_class(u.SkeletalMeshActor,u.Vector(0,0,0))
    actor.set_actor_label('Menu3D_'+day)
    actor.set_editor_property('tags',[u.Name('Menu3D_'+day)])
    actor.set_folder_path('MainMenu3D/Blocking')
    comp=actor.get_component_by_class(u.SkeletalMeshComponent)
    comp.set_skeletal_mesh_asset(meshes)
    comp.set_material(0,enamel);comp.set_material(1,gum)
    comp.set_collision_enabled(u.CollisionEnabled.NO_COLLISION)
    comp.set_editor_property('cast_shadow',True)
    # Save distinct stage positions; runtime owns their animated card layout.
    actor.set_actor_location(u.Vector(0,0,1300 if day=='DAY01' else 750),False,False)
for actor in tagged('Menu3D_DAY03'):
    comp=actor.get_component_by_class(u.SkeletalMeshComponent)
    comp.set_material(0,enamel3);comp.set_material(1,gum3)
    comp.set_editor_property('cast_shadow',True)
for label,mat in [('Interior',inside),('Tongue',tongue)]:
    for actor in tagged('Menu3D_DAY03_'+label):
        comp=actor.get_component_by_class(u.StaticMeshComponent)
        comp.set_material(0,mat);comp.set_editor_property('cast_shadow',True)
for actor in tagged('Menu3D_DAY03_Character'):
    actor.get_component_by_class(u.SkeletalMeshComponent).set_visibility(False,True)

camera=tagged('Menu3D_Camera')[0]
camera.set_actor_location(u.Vector(0,2200,0),False,False)
camera.set_actor_rotation(u.Rotator(pitch=0.,yaw=-90.,roll=0.),False)
cam=camera.get_component_by_class(u.CameraComponent)
cam.set_projection_mode(u.CameraProjectionMode.PERSPECTIVE)
cam.set_field_of_view(39.97)
pp=cam.get_editor_property('post_process_settings')
for field,value in [('tone_curve_amount',1.),('auto_exposure_bias',-.7),('ambient_occlusion_intensity',.6)]:
    pp.set_editor_property('override_'+field,True);pp.set_editor_property(field,value)
cam.set_editor_property('post_process_settings',pp)
for label,z in [('Header',800.),('Footer',-800.)]:
    for actor in tagged('Menu3D_Mask_'+label):
        ratio=700./2200.
        actor.set_actor_location(u.Vector(0,1500,z*ratio),False,False)
        actor.set_actor_scale3d(u.Vector(40.,.1,8.6*ratio))

light_specs=[('Key',(-650,900,650),600.,500.,700.,(1.,.91,.8)),
             ('Fill',(700,1100,100),170.,900.,900.,(.74,.86,1.)),
             ('BlockingRim',(600,-450,550),450.,400.,800.,(.55,.75,1.)),
             ('BlockingCavity',(0,800,-100),70.,1200.,600.,(.85,.9,1.))]
for label,position,intensity,width,height,color in light_specs:
    tag='Menu3D_'+label
    found=tagged(tag)
    actor=found[0] if found else actors.spawn_actor_from_class(u.RectLight,u.Vector(*position))
    actor.set_actor_label(tag);actor.set_editor_property('tags',[u.Name(tag)])
    actor.set_actor_location(u.Vector(*position),False,False)
    actor.set_actor_rotation(u.MathLibrary.find_look_at_rotation(u.Vector(*position),u.Vector(0,0,0)),False)
    comp=actor.get_component_by_class(u.RectLightComponent)
    comp.set_editor_property('mobility',u.ComponentMobility.MOVABLE)
    comp.set_editor_property('intensity_units',u.LightUnits.CANDELAS)
    comp.set_editor_property('intensity',intensity)
    comp.set_editor_property('source_width',width);comp.set_editor_property('source_height',height)
    comp.set_editor_property('attenuation_radius',6000.)
    comp.set_editor_property('cast_shadows',True)
    comp.set_light_color(u.LinearColor(r=color[0],g=color[1],b=color[2],a=1.))
levels.pilot_level_actor(camera)
assert levels.save_current_level()
(OUT/'native_blocking_assembly.json').write_text(json.dumps(report,indent=2),encoding='utf8')
u.log('MC_MENU3D_FUNCTIONAL_BLOCKING '+json.dumps(report))
