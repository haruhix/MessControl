"""Hub-only collision for the grill and scenery boundaries; no source meshes changed.

Run in an idle Hub editor. Owns only actors tagged MC_HubPlaytestBlockers_20261026.
Existing actor transforms, gameplay stations, water and ready zones are preserved.
"""
import hashlib
import json
from pathlib import Path
import unreal as u

ROOT=Path('E:/DEVGAME/MessControl')
OUT=Path(globals().get('MC_HUB_BLOCKERS_OUTPUT','E:/CodexWork/Batch2_20261026/CollisionAuthoring'))
TAG='MC_HubPlaytestBlockers_20261026'
assert OUT.drive.upper()=='E:'
assert not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor()
world=u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
assert world.get_path_name().split('.')[0]=='/Game/Maps/Hub'
ed=u.get_editor_subsystem(u.EditorActorSubsystem)
actors=list(ed.get_all_level_actors())
assert not u.EditorLoadingAndSavingUtils.get_dirty_content_packages(), 'Preserve unrelated dirty assets'
OUT.mkdir(parents=True,exist_ok=True)
source=ROOT/'Content/Maps/Hub.umap'
backup=OUT/'Hub.Before.umap'
if not backup.exists():
    backup.write_bytes(source.read_bytes())
    assert hashlib.sha256(backup.read_bytes()).digest()==hashlib.sha256(source.read_bytes()).digest()
cube=u.load_asset('/Engine/BasicShapes/Cube')
assert cube
by_name={a.get_name():a for a in actors if a.get_path_name().startswith('/Game/Maps/Hub.Hub:PersistentLevel.')}
owned={a.get_actor_label():a for a in actors if TAG in [str(t) for t in a.tags]}
plan=[]
def block(label,center,extent,rotation=None,camera=False):
    assert min(extent)>0
    a=owned.get(label)
    if not a:
        a=ed.spawn_actor_from_class(u.StaticMeshActor,u.Vector(*center),rotation or u.Rotator())
        a.set_actor_label(label)
        a.tags=[TAG]
    c=a.static_mesh_component
    c.set_static_mesh(cube)
    c.set_mobility(u.ComponentMobility.STATIC)
    a.set_actor_location_and_rotation(u.Vector(*center),rotation or u.Rotator(),False,True)
    a.set_actor_scale3d(u.Vector(*(v/50. for v in extent)))
    a.set_actor_hidden_in_game(True)
    c.set_visibility(False)
    a.set_actor_enable_collision(True)
    c.set_cast_shadow(False)
    c.set_editor_property('generate_overlap_events',False)
    c.set_editor_property('can_ever_affect_navigation',False)
    c.set_collision_profile_name('BlockAll')
    c.set_collision_enabled(u.CollisionEnabled.QUERY_AND_PHYSICS)
    c.set_collision_object_type(u.CollisionChannel.ECC_WORLD_STATIC)
    c.set_collision_response_to_all_channels(u.CollisionResponseType.ECR_IGNORE)
    for channel in (u.CollisionChannel.ECC_PAWN,u.CollisionChannel.ECC_PHYSICS_BODY):
        c.set_collision_response_to_channel(channel,u.CollisionResponseType.ECR_BLOCK)
    c.set_collision_response_to_channel(u.CollisionChannel.ECC_CAMERA,
        u.CollisionResponseType.ECR_BLOCK if camera else u.CollisionResponseType.ECR_IGNORE)
    c.set_editor_property('can_character_step_up_on',u.CanBeCharacterBase.ECB_NO)
    plan.append(dict(label=label,actor=a.get_path_name(),center=center,extent=extent,
                     rotation=str(rotation),blocks_camera=camera))
    return a

# The visible lower assembly has BlockAll but no simple/convex body. A private
# map proxy follows its oriented local bounds, without changing/reimporting art.
base=by_name['StaticMeshActor_85']
assert base.static_mesh_component.static_mesh.get_path_name().startswith('/Game/FromBlender11/SM_Grill_Bot_007_001.')
lo,hi=base.static_mesh_component.get_local_bounds()
scale=base.get_actor_scale3d()
local_center=(lo+hi)*.5
center=u.MathLibrary.transform_location(base.get_actor_transform(),local_center)
extent=(hi-lo)*.5*scale
block('HubCollision_GrillBody',list(center.to_tuple()),list(extent.to_tuple()),base.get_actor_rotation(),True)

# Tall boundaries sit behind the natural perimeter. They include the complete
# READY area, pier, shop and basketball court, and leave the playable pond open.
x0,x1,y0,y1=-3450.,2450.,-2000.,3340.
z0,z1=-1200.,2600.
block('HubBoundary_West',[x0-25,(y0+y1)*.5,(z0+z1)*.5],[25,(y1-y0)*.5+50,(z1-z0)*.5])
block('HubBoundary_East',[x1+25,(y0+y1)*.5,(z0+z1)*.5],[25,(y1-y0)*.5+50,(z1-z0)*.5])
block('HubBoundary_South',[(x0+x1)*.5,y0-25,(z0+z1)*.5],[(x1-x0)*.5+50,25,(z1-z0)*.5])
block('HubBoundary_North',[(x0+x1)*.5,y1+25,(z0+z1)*.5],[(x1-x0)*.5+50,25,(z1-z0)*.5])

# Coconut scenery remains visible/collidable, but cannot be used as a stairway
# onto the backdrop. These player-only columns do not pull the orbit camera in.
for name in ('StaticMeshActor_48','StaticMeshActor_79','StaticMeshActor_80','StaticMeshActor_83','StaticMeshActor_105','StaticMeshActor_106'):
    a=by_name[name]
    assert a.static_mesh_component.static_mesh.get_path_name().startswith('/Game/Stylized_Fruits/Meshes/SM_CoconutBottom.')
    center,extent=a.get_actor_bounds(False)
    block('HubNoClimb_'+name,[center.x,center.y,(z0+z1)*.5],[extent.x,extent.y,(z1-z0)*.5])

# Keep the shop roof out of the player route without obstructing its interior.
shop=next(a for a in actors if a.get_actor_label()=='Shop' and a.get_actor_location().x<10000)
lo,hi=shop.static_mesh_component.get_local_bounds()
scale=shop.get_actor_scale3d()
top=shop.get_actor_location().z+hi.z*scale.z-30.
center=u.MathLibrary.transform_location(shop.get_actor_transform(),u.Vector((lo.x+hi.x)*.5,(lo.y+hi.y)*.5,0))
block('HubNoClimb_ShopRoof',[center.x,center.y,(top+z1)*.5],[(hi.x-lo.x)*scale.x*.5,(hi.y-lo.y)*scale.y*.5,(z1-top)*.5],shop.get_actor_rotation())

assert u.EditorLoadingAndSavingUtils.save_map(world,'/Game/Maps/Hub'), 'Hub save failed'
(OUT/'Authoring.json').write_text(json.dumps(dict(passed=True,backup=str(backup),actors=plan,
    limitations=['Placement and channel authoring only; saved-world sweeps, walking/jumping and camera tests are separate.']),ensure_ascii=False,indent=2),encoding='utf8')
u.log('MC_BATCH2_HUB_BLOCKERS_SAVED '+str(len(plan)))
