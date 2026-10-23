"""Copy the saved L_Mouth appearance into L_MainMenu as passive decoration.

The source world is loaded as an asset, never activated or played. Only native
visual actors are duplicated. Gameplay markers become ordinary static meshes;
the tongue and fixed throat pose reuse the game's existing baked geometry.
"""
import unreal as u,json,hashlib,shutil
from pathlib import Path

ROOT=Path(u.Paths.project_dir()).resolve();OUT=ROOT/'Saved/MenuGameLocation20261023'
OUT.mkdir(parents=True,exist_ok=True)
es=u.get_editor_subsystem(u.EditorActorSubsystem);levels=u.get_editor_subsystem(u.LevelEditorSubsystem)
world=u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
assert not levels.is_in_play_in_editor() and world.get_path_name()=='/Game/Maps/L_MainMenu.L_MainMenu'
assert not (OUT/'Integration.json').exists(),'This one-time location swap has already run'
source=u.load_asset('/Game/Maps/L_Mouth');prefix=source.get_path_name()+':'
source_actors=[a for a in u.ObjectIterator(u.Actor) if a.get_path_name().startswith(prefix)]
source_file=ROOT/'Content/Maps/L_Mouth.umap';source_hash=hashlib.sha256(source_file.read_bytes()).hexdigest()
visuals=[a for a in source_actors if a.get_class()==u.StaticMeshActor.static_class() and a.static_mesh_component.static_mesh and not a.get_editor_property('hidden') and a.static_mesh_component.is_visible() and not a.static_mesh_component.get_editor_property('hidden_in_game') and not a.get_actor_label().startswith('COLLISION')]
lighting_classes=(u.DirectionalLight,u.SkyLight,u.SkyAtmosphere,u.VolumetricCloud,u.ExponentialHeightFog,u.PostProcessVolume)
lighting=[a for a in source_actors if isinstance(a,lighting_classes)]
sockets=[a for a in source_actors if isinstance(a,u.MCArenaToothSocket)]
tongue=next(a for a in source_actors if isinstance(a,u.MCTongue))
throat=next(a for a in source_actors if isinstance(a,u.MCThroat))
assert len(visuals)==19 and len(sockets)==12 and len(lighting)==6,(len(visuals),len(sockets),len(lighting))
contact=u.load_asset('/Game/Gameplay/Arena/ManualIntake/SM_ThroatTissueContact')
assert abs(float(u.EditorAssetLibrary.get_metadata_tag(contact,'MCContactExpansion'))-throat.get_editor_property('manual_authored_expansion'))<1e-5
current=list(es.get_all_level_actors())
hero=next(a for a in current if 'MC_MenuV2_Hero' in [str(t) for t in a.tags])
camera=next(a for a in current if 'MC_MenuV2_Camera' in [str(t) for t in a.tags])
player=next(a for a in current if isinstance(a,u.LevelSequenceActor));seq=player.get_sequence()
decals=[a for a in current if isinstance(a,u.DecalActor)]
assert hero.skeletal_mesh_component.skeletal_mesh_asset.get_path_name().startswith('/Game/UI/MenuShowcase/CharacterMenu20261023/')
assert world.get_world_settings().get_editor_property('default_game_mode')==u.MCMainMenuGameMode.static_class()
assert all(p.get_name()=='/Game/Maps/L_MainMenu' for p in u.EditorLoadingAndSavingUtils.get_dirty_map_packages())
assert not u.EditorLoadingAndSavingUtils.get_dirty_content_packages()
u.MCGameplayUIEditorLibrary.close_main_menu_preview();u.LevelSequenceEditorBlueprintLibrary.close_level_sequence()
before=OUT/'Before';before.mkdir(exist_ok=True)
shutil.copy2(ROOT/'Content/Maps/L_MainMenu.umap',before/'L_MainMenu_Disk.umap')
shutil.copy2(ROOT/'Content/UI/MenuShowcase/AnatomyV2/LS_MainMenuCleaningV2.uasset',before/'LS_MainMenuCleaningV2.uasset')
assert levels.save_current_level()
shutil.copy2(ROOT/'Content/Maps/L_MainMenu.umap',before/'L_MainMenu_CurrentArtistEdits.umap')
report={'source_map':source.get_path_name(),'source_sha256':source_hash,'copied':[],'removed':[],'preserved_hero_mesh':hero.skeletal_mesh_component.skeletal_mesh_asset.get_path_name(),'before_hero':hero.get_actor_transform().export_text(),'before_camera':camera.get_actor_transform().export_text()}
camera_offset=camera.get_actor_location()-hero.get_actor_location()
keep={a.get_path_name() for a in [hero,camera,player,*decals]}
for binding in list(seq.get_bindings()):
    if binding.get_name() not in ['MENU V2 | Cleaner','MENU V2 | camera']:binding.remove()
for a in current:
    if a.get_path_name() not in keep:
        report['removed'].append({'label':a.get_actor_label(),'class':a.get_class().get_name()});assert es.destroy_actor(a)

def passive(actor):
    actor.set_actor_enable_collision(False);actor.set_actor_tick_enabled(False)
    assert not actor.get_editor_property('replicates')
    for comp in actor.get_components_by_class(u.PrimitiveComponent):
        comp.set_collision_enabled(u.CollisionEnabled.NO_COLLISION);comp.set_editor_property('generate_overlap_events',False)
        if isinstance(comp,u.MeshComponent):comp.set_simulate_physics(False)
    actor.tags=[u.Name('MC_MenuGameLocation')]
    actor.set_folder_path('Menu/GameLocation')

def record(actor,src,kind):
    report['copied'].append({'actor':actor.get_path_name(),'label':actor.get_actor_label(),'source':src,'kind':kind,'transform':actor.get_actor_transform().export_text()})

def static(label,mesh,transform,materials,src):
    a=es.spawn_actor_from_class(u.StaticMeshActor,u.Vector())
    a.set_actor_label('MENU | '+label);c=a.static_mesh_component;c.set_mobility(u.ComponentMobility.MOVABLE)
    c.set_static_mesh(mesh)
    for i,m in enumerate(materials):
        if m:c.set_material(i,m)
    a.set_actor_transform(transform,False,True);c.set_visibility(True);c.set_hidden_in_game(False)
    passive(a);record(a,src,'static_visual');return a

for a in visuals:
    copy=es.duplicate_actor(a,world,u.Vector())
    assert copy and copy.get_world()==world,a.get_actor_label()
    copy.set_actor_label('MENU | '+a.get_actor_label());passive(copy);record(copy,a.get_path_name(),'static_visual')
for a in lighting:
    copy=es.duplicate_actor(a,world,u.Vector());assert copy and copy.get_world()==world
    copy.set_actor_label('MENU | '+a.get_actor_label());passive(copy);record(copy,a.get_path_name(),'lighting')
for a in sockets:
    c=a.preview
    static(a.get_actor_label(),c.static_mesh,c.get_world_transform(),[c.get_material(i) for i in range(c.get_num_materials())],a.get_path_name())
floor=static('Tongue',tongue.source_mesh,tongue.get_actor_transform(),[tongue.surface_material],tongue.get_path_name())
mouth=throat.authored_mouth
static('Throat | fixed game pose',contact,mouth.get_world_transform(),[mouth.get_material(i) for i in range(mouth.get_num_materials())],mouth.get_path_name())
c=throat.uvula
static('Uvula',c.static_mesh,c.get_world_transform(),[c.get_material(i) for i in range(c.get_num_materials())],c.get_path_name())

# Place the retained cleaning performance beside the same unscaled game tooth.
tooth=next(a for a in es.get_all_level_actors() if a.get_actor_label()=='MENU | ARENA | Tooth 04')
def bvh_for(actor):
    mesh=u.new_object(u.DynamicMesh)
    copied=u.GeometryScript_AssetUtils.copy_mesh_from_static_mesh(actor.static_mesh_component.static_mesh,mesh,u.GeometryScriptCopyMeshFromAssetOptions(),u.GeometryScriptMeshReadLOD())
    assert copied[-1]==u.GeometryScriptOutcomePins.SUCCESS
    u.GeometryScript_MeshTransforms.transform_mesh(mesh,actor.get_actor_transform())
    return u.GeometryScript_MeshSpatial.build_bvh_for_mesh(mesh)
fm,fbvh=bvh_for(floor);tm,tbvh=bvh_for(tooth)
def floor_hit(x,y,required=True):
    _,hit,_=u.GeometryScript_MeshSpatial.find_nearest_ray_intersection_with_mesh(fm,fbvh,u.Vector(x,y,2500),u.Vector(0,0,-1),u.GeometryScriptSpatialQueryOptions())
    if required:assert hit.hit,('No tongue below menu character',x,y)
    return hit.hit_position if hit.hit else None
hero.set_actor_rotation(u.Rotator(yaw=120.),False)
pose=hero.get_actor_transform();pose.translation=tooth.get_actor_location()+u.Vector(0,-360,0)
landmark=json.loads((ROOT/'ArtSource/UI/CharacterMenu20261023/ExportReport.json').read_text())['landmarks']['0']['MenuBrush_brush']
local=u.Vector(landmark[0]*100,-landmark[1]*100,landmark[2]*100)
fit=[]
for i in range(5):
    p=floor_hit(pose.translation.x,pose.translation.y);pose.translation=u.Vector(p.x,p.y,p.z)
    head=pose.transform_location(local)
    _,near,_=u.GeometryScript_MeshSpatial.find_nearest_point_on_mesh(tm,tbvh,head,u.GeometryScriptSpatialQueryOptions())
    delta=near.position-head
    fit.append({'iteration':i,'head':list(head.to_tuple()),'surface':list(near.position.to_tuple()),'gap_cm':delta.length()})
    if i<4:
        step=u.Vector(delta.x*.75,delta.y*.75,0)
        for backtrack in range(6):
            candidate=pose.translation+step
            if floor_hit(candidate.x,candidate.y,False) is not None:
                pose.translation=candidate;break
            step*=.5
hero.set_actor_transform(pose,False,True)
camera.set_actor_location(pose.translation+camera_offset,False,True)
# The copied game postprocess/lighting determine the environment appearance.
camera.camera_component.set_editor_property('post_process_blend_weight',0.)
for a in decals:
    p=a.get_actor_location();relative=p-u.Vector(-650.,250.,3.525917)
    hit=floor_hit(pose.translation.x+relative.x,pose.translation.y+relative.y)
    a.set_actor_location(hit+u.Vector(0,0,.15),False,True)
settings=player.playback_settings;settings.auto_play=True;settings.loop_count=u.MovieSceneSequenceLoopCount(-1);player.set_editor_property('playback_settings',settings)
assert u.EditorAssetLibrary.save_loaded_asset(seq);assert levels.save_current_level()
assert hashlib.sha256(source_file.read_bytes()).hexdigest()==source_hash
report.update(status='saved_pending_solo',source_map_unchanged=True,visual_count=len(report['copied']),hero=hero.get_actor_transform().export_text(),camera=camera.get_actor_transform().export_text(),contact_fit=fit,sequence=seq.get_path_name(),loop_seconds=9.125,game_mode='MCMainMenuGameMode',preload='All reused meshes and materials are reflected references on saved menu components.')
(OUT/'Integration.json').write_text(json.dumps(report,indent=2),encoding='utf8')
print('MENU_GAME_LOCATION_SAVED',json.dumps({k:report[k] for k in ['visual_count','hero','camera','source_map_unchanged','contact_fit']}))
