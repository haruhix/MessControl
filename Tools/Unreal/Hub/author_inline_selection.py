"""Prepare the existing selector as an always-loaded, isolated part of Hub.

run() is read-only. run('stage') saves a new owned stage clone; run('hub')
connects that saved clone to Hub. run('backdrop') isolates its camera background.
Run hub/backdrop in a fresh editor after stage.
Only the new stage and Hub may be saved; unrelated Hub actors are verified.
"""
from pathlib import Path
import hashlib
import json
import shutil
import unreal as u

HUB='/Game/Maps/Hub'
SOURCE='/Game/Maps/L_HubSelection'
STAGE='/Game/Maps/L_HubSelectionStage'
OFFSET=u.Vector(200000,0,0)
OWNER='MCInlineSelectionStage_v1'
OUT=Path('E:/CodexWork/HubInlineSelection_20261023/Authoring')
ROOT=Path('E:/DEVGAME/MessControl')

def require(ok,message):
    if not ok:raise RuntimeError(message)

def dirty():
    return [x.get_path_name() for x in list(u.EditorLoadingAndSavingUtils.get_dirty_content_packages())+list(u.EditorLoadingAndSavingUtils.get_dirty_map_packages())]

def digest(file):return hashlib.sha256(Path(file).read_bytes()).hexdigest()

def backup(package):
    source=ROOT/'Content'/((package.removeprefix('/Game/'))+'.umap')
    if not source.exists():return None
    target=OUT/'Backups'/source.relative_to(ROOT)
    target.parent.mkdir(parents=True,exist_ok=True)
    if not target.exists():shutil.copy2(source,target)
    require(digest(source)==digest(target),'Existing backup differs; choose a new authoring output before changing '+package)
    return {'source':str(source),'backup':str(target),'sha256':digest(source)}

def actors(world):return u.GameplayStatics.get_all_actors_of_class(world,u.Actor)

def record(actor):
    return {'path':actor.get_path_name(),'class':actor.get_class().get_path_name(),
        'transform':str(actor.get_actor_transform()),'tags':[str(x) for x in actor.tags]}

def run(mode='plan'):
    require(mode in ('plan','stage','hub','backdrop'),'Unsupported mode')
    levels=u.get_editor_subsystem(u.LevelEditorSubsystem)
    require(not levels.is_in_play_in_editor(),'Preserve active Play')
    allowed_dirty=[STAGE] if mode=='stage' and not (ROOT/'Content/Maps/L_HubSelectionStage.umap').exists() else []
    require(all(x in allowed_dirty for x in dirty()),'Preserve dirty packages: '+str(dirty()))
    require(Path(u.Paths.project_dir()).resolve()==ROOT.resolve(),'Wrong project')
    OUT.mkdir(parents=True,exist_ok=True)
    report={'mode':mode,'stage':STAGE,'hub':HUB,'offset':list(OFFSET.to_tuple()),'saved':[]}
    if mode=='plan':
        source=u.load_asset(SOURCE)
        report['source_actors']=[record(a) for a in actors(source)]
    elif mode=='stage':
        existing=u.find_object(None,STAGE+'.L_HubSelectionStage')
        require(not (ROOT/'Content/Maps/L_HubSelectionStage.umap').exists(),'Stage already saved; preserve it')
        if existing:
            require(u.EditorAssetLibrary.get_metadata_tag(existing,'MCInlineSelectionOwner')==OWNER,'Unowned stage preserved')
            world=existing
        else:world=u.EditorAssetLibrary.duplicate_asset(SOURCE,STAGE)
        require(isinstance(world,u.World),'Selector clone failed')
        u.EditorAssetLibrary.set_metadata_tag(world,'MCInlineSelectionOwner',OWNER)
        stage_actors=[a for a in actors(world) if any(str(t).startswith('Menu3D_') for t in a.tags)]
        require(len(stage_actors)==23,'Unexpected selector source layout; inspect before applying')
        for actor in stage_actors:
            require(any(str(t).startswith('Menu3D_') for t in actor.tags),'Unexpected untagged selector actor')
            actor.modify();actor.set_actor_enable_collision(False)
            actor.tags=list(dict.fromkeys(list(actor.tags)+[u.Name(OWNER)]))
            if isinstance(actor,u.CameraActor):actor.set_editor_property('AutoActivateForPlayer',u.AutoReceiveInput.DISABLED)
            for component in actor.get_components_by_class(u.PrimitiveComponent):
                component.modify();component.set_collision_enabled(u.CollisionEnabled.NO_COLLISION)
                component.call_method('SetGenerateOverlapEvents',args=(False,))
                component.set_lighting_channels(False,True,False)
            for light in actor.get_components_by_class(u.LightComponent):
                light.modify();light.set_lighting_channels(False,True,False)
        require(u.EditorLoadingAndSavingUtils.save_map(world,STAGE),'Could not save stage clone')
        report.update(saved=[STAGE],stage_actor_count=len(stage_actors),fresh_process_required=True)
    elif mode=='backdrop':
        original=u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world().get_path_name().split('.')[0]
        require(levels.load_level(STAGE),'Could not open owned stage')
        world=u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
        require(u.EditorAssetLibrary.get_metadata_tag(world,'MCInlineSelectionOwner')==OWNER,'Unowned stage preserved')
        found=u.GameplayStatics.get_all_actors_with_tag(world,'MCInlineSelectionBackdrop')
        require(len(found)<=1,'Duplicate stage backdrops')
        if not found:
            actor=u.get_editor_subsystem(u.EditorActorSubsystem).spawn_actor_from_class(u.StaticMeshActor,
                u.Vector(0,-3000,0),u.Rotator(pitch=0,yaw=0,roll=-90))
            actor.set_actor_label('HubSelection_Backdrop')
            actor.tags=[u.Name(OWNER),u.Name('MCInlineSelectionBackdrop')]
            actor.set_actor_scale3d(u.Vector(120,120,120));actor.set_actor_enable_collision(False)
            component=actor.get_component_by_class(u.StaticMeshComponent)
            component.set_static_mesh(u.load_asset('/Engine/BasicShapes/Plane'))
            component.set_material(0,u.load_asset('/Game/UI/MainMenu3D/Materials/M_Menu3D_Background'))
            component.set_collision_enabled(u.CollisionEnabled.NO_COLLISION)
            component.set_editor_property('cast_shadow',False)
            component.set_lighting_channels(False,True,False)
            require(u.EditorLoadingAndSavingUtils.save_map(world,STAGE),'Could not save isolated backdrop')
            report['saved']=[STAGE]
        require(not dirty(),'Unexpected dirty packages preserved')
        if original!=STAGE:require(levels.load_level(original),'Could not restore original map')
    else:
        original=u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world().get_path_name().split('.')[0]
        require(levels.load_level(HUB),'Could not open Hub')
        world=u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
        report['backup']=backup(HUB)
        original_actors={a.get_path_name():record(a) for a in actors(world)}
        stage=u.load_asset(STAGE)
        require(stage and u.EditorAssetLibrary.get_metadata_tag(stage,'MCInlineSelectionOwner')==OWNER,'Stage ownership check failed')
        # The exact package lookup is also the idempotency guard.
        existing=u.GameplayStatics.get_streaming_level(world,STAGE)
        require(existing is None,'Hub already contains this stage; preserve saved layout')
        stream=u.EditorLevelUtils.add_level_to_world_with_transform(world,STAGE,
            u.LevelStreamingAlwaysLoaded,u.Transform(location=OFFSET))
        require(stream is not None and stream.get_loaded_level() is not None,'Always-loaded stage could not be attached')
        after={a.get_path_name():record(a) for a in actors(world)}
        for name,value in original_actors.items():require(after.get(name)==value,'Unrelated Hub actor changed: '+name)
        require(u.EditorLoadingAndSavingUtils.save_map(world,HUB),'Could not save Hub streaming reference')
        report.update(saved=[HUB],original_actor_count=len(original_actors),stage_actor_count=len(actors(world))-len(original_actors))
        require(not dirty(),'Unexpected dirty packages preserved: '+str(dirty()))
        if original!=HUB:require(levels.load_level(original),'Could not restore original editor map')
    (OUT/(mode+'_Report.json')).write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    u.log('MC_INLINE_SELECTION_AUTHOR '+json.dumps(report,ensure_ascii=False))
    return report
