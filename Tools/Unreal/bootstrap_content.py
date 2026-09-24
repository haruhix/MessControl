"""Run inside Unreal AFTER compiling the project and generating Blender/audio sources.
Creates/refreshes prototype content. Refuses to replace an existing map unless MC_REBUILD_MAP=1.
"""
import unreal as ue
import math
import os
from pathlib import Path

ROOT = Path(ue.Paths.project_dir()).resolve()
assets = ue.AssetToolsHelpers.get_asset_tools()
library = ue.EditorAssetLibrary
actors = ue.get_editor_subsystem(ue.EditorActorSubsystem)
levels = ue.get_editor_subsystem(ue.LevelEditorSubsystem)
for folder in ("/Game/Art/Meshes","/Game/Art/Materials","/Game/Audio","/Game/Data","/Game/Maps","/Game/Blueprints"):
    library.make_directory(folder)

palette={
    "Enamel":((0.94,0.86,0.65),0.28),"Ink":((0.024,0.052,0.061),0.45),
    "Blush":((0.96,0.28,0.29),0.5),"Mint":((0.07,0.59,0.49),0.32),
    "Mango":((1,0.48,0.065),0.5),"Bristles":((0.53,0.89,0.81),0.45),
    "Tongue":((0.66,0.15,0.21),0.5),"Gum":((0.51,0.065,0.11),0.45),
    "Cheek":((0.31,0.025,0.053),0.75),"Throat":((0.055,0.008,0.02),0.85),
    "Coffee":((0.17,0.06,0.025),0.36),"Broccoli":((0.21,0.34,0.055),0.7)}
materials={}
for name,(color,roughness) in palette.items():
    path="/Game/Art/Materials/M_"+name
    if library.does_asset_exist(path):
        materials[name]=library.load_asset(path)
        continue
    mat=assets.create_asset("M_"+name,"/Game/Art/Materials",ue.Material,ue.MaterialFactoryNew())
    color_node=ue.MaterialEditingLibrary.create_material_expression(mat,ue.MaterialExpressionVectorParameter,-350,0)
    color_node.set_editor_property("parameter_name","Tint")
    color_node.set_editor_property("default_value",ue.LinearColor(*color,1))
    ue.MaterialEditingLibrary.connect_material_property(color_node,"",ue.MaterialProperty.MP_BASE_COLOR)
    rough=ue.MaterialEditingLibrary.create_material_expression(mat,ue.MaterialExpressionScalarParameter,-350,160)
    rough.set_editor_property("parameter_name","Roughness"); rough.set_editor_property("default_value",roughness)
    ue.MaterialEditingLibrary.connect_material_property(rough,"",ue.MaterialProperty.MP_ROUGHNESS)
    ue.MaterialEditingLibrary.recompile_material(mat)
    library.save_loaded_asset(mat); materials[name]=mat

# Use the legacy FBX importer explicitly so material-slot names remain reproducible.
ue.SystemLibrary.execute_console_command(None,"Interchange.FeatureFlags.Import.FBX 0")
meshes={}
for file in sorted((ROOT/"ArtSource"/"Exports").glob("SM_*.fbx")):
    if library.does_asset_exist("/Game/Art/Meshes/"+file.stem) and os.environ.get("MC_REIMPORT")!="1":
        meshes[file.stem]=library.load_asset("/Game/Art/Meshes/"+file.stem)
        continue
    task=ue.AssetImportTask(); task.filename=str(file); task.destination_path="/Game/Art/Meshes"
    task.destination_name=file.stem; task.automated=True; task.replace_existing=True; task.save=True
    options=ue.FbxImportUI(); options.import_mesh=True; options.import_as_skeletal=False
    options.import_materials=False; options.import_textures=False; options.import_animations=False
    options.mesh_type_to_import=ue.FBXImportType.FBXIT_STATIC_MESH
    options.static_mesh_import_data.combine_meshes=True
    options.static_mesh_import_data.generate_lightmap_u_vs=False
    options.static_mesh_import_data.auto_generate_collision=False
    options.static_mesh_import_data.import_rotation=ue.Rotator(0,0,0)
    task.options=options; task.factory=ue.FbxFactory()
    assets.import_asset_tasks([task])
    mesh=library.load_asset("/Game/Art/Meshes/"+file.stem)
    if not mesh: raise RuntimeError("Failed mesh import: "+str(file))
    for i,slot in enumerate(mesh.get_editor_property("static_materials")):
        name=str(slot.get_editor_property("material_slot_name"))
        if name in materials: mesh.set_material(i,materials[name])
        else: ue.log_warning("Unmapped material: "+name)
    library.save_loaded_asset(mesh); meshes[file.stem]=mesh

sounds={}
for file in sorted((ROOT/"ArtSource"/"Audio").glob("*.wav")):
    if library.does_asset_exist("/Game/Audio/S_"+file.stem) and os.environ.get("MC_REIMPORT")!="1":
        sounds[file.stem]=library.load_asset("/Game/Audio/S_"+file.stem)
        continue
    task=ue.AssetImportTask(); task.filename=str(file); task.destination_path="/Game/Audio"; task.destination_name="S_"+file.stem
    task.automated=True; task.replace_existing=True; task.save=True; assets.import_asset_tasks([task])
    sounds[file.stem]=library.load_asset("/Game/Audio/S_"+file.stem)

def data(name,cls):
    path="/Game/Data/"+name
    if library.does_asset_exist(path): return library.load_asset(path)
    factory=ue.DataAssetFactory(); factory.set_editor_property("data_asset_class",cls)
    return assets.create_asset(name,"/Game/Data",cls,factory)

anim=data("DA_ToothAnimation",ue.MCAnimationProfile)
library.save_loaded_asset(anim)
run_rules=data("DA_RunRules",ue.MCRunRules)
library.save_loaded_asset(run_rules)
audio=data("DA_MouthSounds",ue.MCSoundPalette)
events={}
for name,sound in sounds.items():
    entry=ue.MCSoundVariation(); entry.set_editor_property("sounds",[sound]); entry.set_editor_property("volume",0.22 if name in ("Brush","Step","Pull") else 0.6)
    events[name]=entry
audio.set_editor_property("events",events); library.save_loaded_asset(audio,only_if_is_dirty=False)
for name,kind,title,instruction,count,seconds in [
    ("Coffee",ue.MCTaskKind.COFFEE,"COFFEE BREAK","Hold LMB near a brown stain. Scrub it sparkling clean.",4,2.8),
    ("Food",ue.MCTaskKind.FOOD,"SNACK ATTACK","Hold E near a stuck snack. Pull together to finish faster.",3,4.0),
    ("LooseTooth",ue.MCTaskKind.LOOSE_TOOTH,"WOBBLY BUSINESS","Hold E near a loose tooth. Help it stand straight again.",3,4.5)]:
    event=data("DA_"+name,ue.MCDayEvent)
    for key,value in dict(kind=kind,title=title,instruction=instruction,base_task_count=count,work_seconds=seconds,duration=95.0,missed_task_damage=12.0).items(): event.set_editor_property(key,value)
    library.save_loaded_asset(event,only_if_is_dirty=False)

# Editable Blueprint extension points. Existing Blueprints retain designer changes.
for name,cls in [("BP_ToothCharacter",ue.MCToothCharacter),("BP_MouthTask",ue.MCTaskActor),("BP_MouthGameMode",ue.MCGameMode)]:
    if not library.does_asset_exist("/Game/Blueprints/"+name):
        factory=ue.BlueprintFactory(); factory.set_editor_property("parent_class",cls)
        bp=assets.create_asset(name,"/Game/Blueprints",ue.Blueprint,factory); library.save_loaded_asset(bp)

map_path="/Game/Maps/L_Mouth"
if library.does_asset_exist(map_path) and os.environ.get("MC_REBUILD_MAP")!="1":
    ue.log("MC_CONTENT_COMPLETE: kept existing L_Mouth map")
else:
    if library.does_asset_exist(map_path):
        if not levels.load_level(map_path): raise RuntimeError("Could not load mouth map")
        for existing in actors.get_all_level_actors():
            if existing.get_actor_label().startswith(("ART |", "COLLISION |", "START |", "LIGHT |")) or isinstance(existing,ue.SkyLight):
                actors.destroy_actor(existing)
    elif not levels.new_level(map_path):
        raise RuntimeError("Could not create mouth map")
    def mesh_actor(name,mesh,loc=(0,0,0),scale=(1,1,1),rot=(0,0,0),collision=False):
        actor=actors.spawn_actor_from_class(ue.StaticMeshActor,ue.Vector(*loc),ue.Rotator(*rot))
        actor.set_actor_label(name); component=actor.static_mesh_component
        component.set_static_mesh(mesh); actor.set_actor_scale3d(ue.Vector(*scale)); actor.set_actor_enable_collision(collision)
        return actor
    mesh_actor("ART | Tongue arena",meshes["SM_Tongue"])
    mesh_actor("ART | Mouth shell",meshes["SM_MouthShell"])
    for side in (-1,1):
        for i in range(8):
            mesh_actor("ART | Lower molar %s %s"%(side,i),meshes["SM_ToothProp"],(-890+i*265,side*(815+35*math.sin(i)),0),(2.15,2.15,2.45),(0,0,3*math.sin(i)))
            mesh_actor("ART | Upper molar %s %s"%(side,i),meshes["SM_ToothProp"],(-740+i*275,side*865,990),(2.2,2.2,2.15),(0,0,180))
    cube=library.load_asset("/Engine/BasicShapes/Cube")
    for name,loc,scale in [
        ("Tongue floor",(0,0,-15),(21,15,0.9)),
        ("Left boundary",(0,-775,180),(23,0.5,5)),("Right boundary",(0,775,180),(23,0.5,5)),
        ("Front boundary",(-1060,0,180),(0.5,16,5)),("Back boundary",(1060,0,180),(0.5,16,5))]:
        collider=mesh_actor("COLLISION | "+name,cube,loc,scale,collision=True)
        collider.set_actor_hidden_in_game(True)
        collider.static_mesh_component.set_editor_property("cast_shadow",False)
        collider.set_is_temporarily_hidden_in_editor(True)
    for i,y in enumerate((-240,-80,80,240)):
        start=actors.spawn_actor_from_class(ue.PlayerStart,ue.Vector(-720,y,105),ue.Rotator(0,180,0)); start.set_actor_label("START | Tooth %d"%(i+1))
    sun=actors.spawn_actor_from_class(ue.DirectionalLight,ue.Vector(-700,0,1000),ue.Rotator(-35,-10,0))
    sun.set_actor_label("LIGHT | Warm key"); sun.light_component.set_mobility(ue.ComponentMobility.MOVABLE); sun.light_component.set_intensity(1.2); sun.light_component.set_light_color(ue.LinearColor(1,0.81,0.65))
    for name,loc,intensity,color in [
        ("Front softbox",(-700,-350,650),650,(1,0.76,0.6)),
        ("Mint fill",(-100,600,500),500,(0.58,0.84,1)),
        ("Throat glow",(950,0,650),750,(1,0.38,0.19))]:
        light=actors.spawn_actor_from_class(ue.PointLight,ue.Vector(*loc),ue.Rotator())
        light.set_actor_label("LIGHT | "+name); c=light.point_light_component
        c.set_mobility(ue.ComponentMobility.MOVABLE); c.set_intensity_units(ue.LightUnits.LUMENS); c.set_intensity(intensity)
        c.set_attenuation_radius(2400); c.set_source_radius(150); c.set_light_color(ue.LinearColor(*color)); c.set_cast_shadows(False)
    sky=actors.spawn_actor_from_class(ue.SkyLight,ue.Vector(0,0,600),ue.Rotator()); sky.light_component.set_mobility(ue.ComponentMobility.MOVABLE); sky.light_component.set_intensity(0.6)
    sky.light_component.set_editor_property("lower_hemisphere_is_black",False)
    world=ue.get_editor_subsystem(ue.UnrealEditorSubsystem).get_editor_world()
    world.get_world_settings().set_editor_property("kill_z",-800)
    world.get_world_settings().set_editor_property("force_no_precomputed_lighting",True)
    levels.set_level_viewport_camera_info(ue.Vector(-1810,0,820),ue.Rotator(-22,0,0),"")
    if not levels.save_current_level(): raise RuntimeError("Could not save mouth map")
    ue.log("MC_CONTENT_COMPLETE: created mouth map")
library.save_directory("/Game",only_if_is_dirty=True,recursive=True)
