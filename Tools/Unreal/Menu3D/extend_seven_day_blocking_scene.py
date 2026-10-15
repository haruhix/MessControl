"""Seven native blocking rows; future identities intentionally unassigned."""
import json
from pathlib import Path
import unreal as u

BASE='/Game/UI/MainMenu3D'
levels=u.get_editor_subsystem(u.LevelEditorSubsystem)
actors=u.get_editor_subsystem(u.EditorActorSubsystem)
world=u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
assert not levels.is_in_play_in_editor()
assert world.get_path_name().startswith(BASE+'/L_MainMenu3D')
out=Path(u.Paths.project_dir())/'ArtSource/UI/MainMenu3D/20261015_refB_v1/Assembly/Review/Unreal_FunctionalBlocking_v02_ThreeMouths'
report={'future_rows':'Neutral repeated blocking geometry only; identity unassigned, Coming Soon, no Play','mouths':[],'interiors':[]}
def tagged(tag):return list(u.GameplayStatics.get_all_actors_with_tag(world,tag))
def label(actor,tag):
    actor.set_actor_label(tag);actor.set_editor_property('tags',[u.Name(tag)])
    actor.set_folder_path('MainMenu3D/Blocking')

template=u.load_asset(BASE+'/Meshes/SK_DAY01_Blocking')
enamel=u.load_asset(BASE+'/Blocking/Materials/M_Blocking_Enamel')
gum=u.load_asset(BASE+'/Blocking/Materials/M_Blocking_Gum')
for day in range(4,8):
    tag='Menu3D_DAY0'+str(day)
    found=tagged(tag)
    actor=found[0] if found else actors.spawn_actor_from_class(u.SkeletalMeshActor,u.Vector(0,0,-1000.-750.*(day-4)))
    label(actor,tag)
    comp=actor.get_component_by_class(u.SkeletalMeshComponent)
    comp.set_skeletal_mesh_asset(template)
    comp.set_material(0,enamel);comp.set_material(1,gum)
    comp.set_collision_enabled(u.CollisionEnabled.NO_COLLISION)
    comp.set_editor_property('cast_shadow',True)
    report['mouths'].append({'actor':actor.get_path_name(),'mesh':template.get_path_name(),'unassigned':True})

source_width=u.load_asset(BASE+'/Meshes/SK_DAY03_Seated_v04').get_bounds().box_extent.x
for day in (1,2):
    parent=tagged('Menu3D_DAY0'+str(day))[0]
    width=parent.get_component_by_class(u.SkeletalMeshComponent).get_skeletal_mesh_asset().get_bounds().box_extent.x
    for name in ('Interior','Tongue'):
        tag='Menu3D_DAY0'+str(day)+'_'+name
        found=tagged(tag)
        actor=found[0] if found else actors.spawn_actor_from_class(u.StaticMeshActor,u.Vector(0,0,0))
        label(actor,tag)
        comp=actor.get_component_by_class(u.StaticMeshComponent)
        comp.set_editor_property('mobility',u.ComponentMobility.MOVABLE)
        comp.set_static_mesh(u.load_asset(BASE+'/Meshes/SM_DAY03_'+name+'_Seated_v04'))
        comp.set_material(0,u.load_asset(BASE+'/Blocking/Materials/M_Blocking_'+name))
        comp.set_collision_enabled(u.CollisionEnabled.NO_COLLISION)
        comp.set_editor_property('cast_shadow',True)
        actor.attach_to_actor(parent,'',u.AttachmentRule.SNAP_TO_TARGET,u.AttachmentRule.SNAP_TO_TARGET,u.AttachmentRule.SNAP_TO_TARGET,False)
        actor.set_actor_relative_location(u.Vector(0,0,0),False,False)
        actor.set_actor_relative_rotation(u.Rotator(pitch=0.,yaw=0.,roll=0.),False,False)
        comp.set_relative_scale3d(u.Vector(width/source_width,1.,1.))
        report['interiors'].append({'actor':actor.get_path_name(),'parent':parent.get_path_name(),'width_ratio':width/source_width})

assert levels.save_current_level()
(out/'seven_day_blocking_scene.json').write_text(json.dumps(report,indent=2),encoding='utf8')
u.log('MC_SEVEN_BLOCKING_SCENE '+json.dumps(report))
