"""Task-only native scene support for the three-visible-mouth accordion blocking."""
import json
from pathlib import Path
import unreal as u

BASE='/Game/UI/MainMenu3D'
OUT=Path(u.Paths.project_dir())/'ArtSource/UI/MainMenu3D/20261015_refB_v1/Assembly/Review/Unreal_FunctionalBlocking_v02_ThreeMouths'
OUT.mkdir(parents=True,exist_ok=True)
levels=u.get_editor_subsystem(u.LevelEditorSubsystem)
actors=u.get_editor_subsystem(u.EditorActorSubsystem)
world=u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
assert not levels.is_in_play_in_editor()
assert world.get_path_name().startswith(BASE+'/L_MainMenu3D')
report={'purpose':'FUNCTIONAL_NATIVE_3D_BLOCKING_THREE_VISIBLE_ROWS','textures_used':False,'materials':[],'characters':[]}

def node(mat,cls,**fields):
    ob=u.MaterialEditingLibrary.create_material_expression(mat,cls)
    for name,value in fields.items():ob.set_editor_property(name,value)
    return ob

def link(a,b,pin='Input'):
    names=[str(n) for n in u.MaterialEditingLibrary.get_material_expression_input_names(b)]
    if pin=='Input' and pin not in names:pin=names[0]
    assert u.MaterialEditingLibrary.connect_material_expressions(a,'',b,pin),(a,b,pin)

for name,color,roughness in [
    ('Enamel',(.48,.52,.55),.28),('Gum',(.15,.18,.2),.42),
    ('Enamel_DAY03',(.48,.52,.55),.28),('Gum_DAY03',(.15,.18,.2),.42),
    ('Interior',(.035,.045,.052),.55),('Tongue',(.09,.11,.12),.38)]:
    mat=u.load_asset(BASE+'/Blocking/Materials/M_Blocking_'+name)
    assert mat
    u.MaterialEditingLibrary.delete_all_material_expressions(mat)
    bc=node(mat,u.MaterialExpressionConstant3Vector,constant=u.LinearColor(r=color[0],g=color[1],b=color[2],a=1.))
    rough=node(mat,u.MaterialExpressionConstant,r=roughness)
    u.MaterialEditingLibrary.connect_material_property(bc,'',u.MaterialProperty.MP_BASE_COLOR)
    u.MaterialEditingLibrary.connect_material_property(rough,'',u.MaterialProperty.MP_ROUGHNESS)
    pos=node(mat,u.MaterialExpressionWorldPosition)
    local=node(mat,u.MaterialExpressionTransformPosition,
        transform_source_type=u.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_WORLD,
        transform_type=u.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_LOCAL)
    z=node(mat,u.MaterialExpressionComponentMask,r=False,g=False,b=True,a=False)
    absolute=node(mat,u.MaterialExpressionAbs)
    half=node(mat,u.MaterialExpressionScalarParameter,parameter_name='CardHalfHeight',default_value=340.5)
    distance=node(mat,u.MaterialExpressionSubtract)
    link(pos,local);link(local,z);link(z,absolute)
    link(half,distance,'A');link(absolute,distance,'B')
    u.MaterialEditingLibrary.connect_material_property(distance,'',u.MaterialProperty.MP_OPACITY_MASK)
    u.MaterialEditingLibrary.recompile_material(mat)
    assert u.EditorAssetLibrary.save_loaded_asset(mat)
    report['materials'].append({'asset':mat.get_path_name(),'clip_parameter':'CardHalfHeight','texture_nodes':0})

def tagged(tag):return list(u.GameplayStatics.get_all_actors_with_tag(world,tag))

mesh=u.load_asset('/Game/Gameplay/CharacterCurrent/SK_CharacterGameplay')
animation=u.load_asset('/Game/Gameplay/Character/Animations/Teeth3/A_Teeth_Hello1')
assert mesh and animation
for day in (1,2,3):
    label='Menu3D_DAY0'+str(day)+'_Character'
    parent=tagged('Menu3D_DAY0'+str(day))[0]
    found=tagged(label)
    actor=found[0] if found else actors.spawn_actor_from_class(u.SkeletalMeshActor,u.Vector(0,0,0))
    actor.set_actor_label(label)
    actor.set_editor_property('tags',[u.Name(label)])
    actor.set_folder_path('MainMenu3D/Blocking')
    comp=actor.get_component_by_class(u.SkeletalMeshComponent)
    comp.set_skeletal_mesh_asset(mesh)
    comp.set_collision_enabled(u.CollisionEnabled.NO_COLLISION)
    comp.set_editor_property('cast_shadow',True)
    comp.set_visibility(True,True)
    comp.override_animation_data(animation,True,True,.75,1.)
    actor.attach_to_actor(parent,'',u.AttachmentRule.SNAP_TO_TARGET,u.AttachmentRule.SNAP_TO_TARGET,u.AttachmentRule.SNAP_TO_TARGET,False)
    actor.set_actor_relative_location(u.Vector(-575.7,-288.9,-217.78),False,False)
    actor.set_actor_relative_rotation(u.Rotator(pitch=0.,yaw=195.,roll=0.),False,False)
    comp.set_relative_scale3d(u.Vector(2.,2.,2.))
    report['characters'].append({'actor':actor.get_path_name(),'parent':parent.get_path_name(),
        'mesh':mesh.get_path_name(),'existing_animation':animation.get_path_name(),
        'relative_location':str(comp.get_editor_property('relative_location')),
        'relative_scale':str(comp.get_editor_property('relative_scale3d'))})

assert levels.save_current_level()
(OUT/'native_compact_stage.json').write_text(json.dumps(report,indent=2),encoding='utf8')
u.log('MC_THREE_MOUTH_NATIVE_STAGE '+json.dumps(report))
