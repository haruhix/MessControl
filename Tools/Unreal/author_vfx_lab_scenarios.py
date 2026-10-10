"""Author real repeating gameplay fixtures in the open L_VFXLab after LiveCoding.

Every case has a separate flat support and only its local gameplay fixtures.
No main-arena scenery, roof or tongue mesh is required or duplicated.
The main arena and third-party source effects are never saved or modified.
"""
import unreal as u,json,re
from pathlib import Path

LAB='/Game/Tests/L_VFXLab'
ROOT=Path(u.Paths.project_dir())
ACTIVATION_DISTANCE=3000
RELEASE_DISTANCE=3600
GROUPS=[
    ('01_Boss','MCVFXLabBossStation','MCVFXLabBossKind','kind',1),
    ('02_Tools','MCVFXLabToolStation','MCVFXLabToolCase','station_kind',6),
    ('03_Ice','MCVFXLabIceStation','MCVFXLabIceKind','kind',11),
]

def main():
    editor=u.get_editor_subsystem(u.UnrealEditorSubsystem)
    es=u.get_editor_subsystem(u.EditorActorSubsystem)
    levels=u.get_editor_subsystem(u.LevelEditorSubsystem)
    world=editor.get_editor_world()
    assert world.get_path_name().startswith(LAB+'.'),world.get_path_name()
    assert not levels.is_in_play_in_editor()
    u.get_blueprint_generated_types(['/Script/MessControl.'+x for x in
        ['MCVFXLabFloor','MCVFXLab','MCVFXLabGameMode']+[g[1] for g in GROUPS]])
    # Live Coding registers native classes before Python regenerates wrappers.
    # Epic's reflected JSON tools work directly with those live UClasses/enums.
    def native_class(name):
        cls=u.load_class(None,'/Script/MessControl.'+name)
        assert cls,name+' is not loaded'
        return cls
    floor_class=native_class('MCVFXLabFloor')
    lib=u.EditorAssetLibrary
    mesh=lib.load_asset('/Game/Tests/VFXLab/SM_LabSupport')
    if not mesh:
        mesh=lib.duplicate_asset('/Engine/BasicShapes/Cube','/Game/Tests/VFXLab/SM_LabSupport')
    assert mesh
    mesh.set_editor_property('allow_cpu_access',True)
    assert lib.save_loaded_asset(mesh,only_if_is_dirty=False)
    material=lib.load_asset('/Game/Tests/VFXLab/M_LabFloor')
    assert material
    for a in list(es.get_all_level_actors()):
        if 'MCVFXLabScenario' in [str(t) for t in a.tags]: es.destroy_actor(a)
    created=[]
    def actor(cls,label,point):
        a=es.spawn_actor_from_class(cls,u.Vector(*point),u.Rotator())
        assert a,label
        a.set_actor_label(label)
        a.set_folder_path('VFXLab/Scenarios/'+label.split('/')[0])
        a.tags=[u.Name('MCVFXLabScenario')]
        created.append(a)
        return a
    rows=[]
    for group,cls_name,enum_name,prop,start_row in GROUPS:
        header=(ROOT/'Source/MessControl/Public'/(cls_name+'.h')).read_text(encoding='utf-8')
        match=re.search(r'enum class E'+enum_name+r'\s*:\s*uint8\s*\{([^}]+)\}',header,re.S)
        assert match,enum_name
        cases=[x.strip() for x in match.group(1).split(',') if x.strip()]
        station_class=native_class(cls_name)
        for index,case in enumerate(cases):
            center=((index%5)*9000,(start_row+index//5)*7000,0)
            name=group+'/'+str(index+1).zfill(2)+'_'+case
            floor=actor(floor_class,name+'_Support',(center[0],center[1],-20))
            floor.set_actor_scale3d(u.Vector(60,40,.4))
            floor.set_editor_property('source_mesh',mesh)
            floor.set_editor_property('surface_material',material)
            floor.call_method('RebuildSurface')
            floor.set_editor_property('bAutomaticYawns',False)
            station=actor(station_class,name,center)
            station.tags=[u.Name('MCVFXLabStation'),u.Name(name),u.Name('MCVFXLabScenario')]
            station.set_editor_property('floor',floor)
            for label_component in station.get_components_by_class(u.TextRenderComponent):
                label_component.set_relative_rotation(u.Rotator(yaw=180),False,False)
                label_component.set_cull_distance(2500)
            reflected_prop='stationKind' if prop=='station_kind' else prop
            assert u.ToolsetLibrary.set_object_properties(station,json.dumps({reflected_prop:case})),name
            station.set_editor_property('bAutoRun',True)
            if cls_name=='MCVFXLabToolStation' and case in ('FloorBrush','FloorMeshaBrush','ToothBrush','ToothMeshaBrush','ToothRepair','PickaxeCalculus','BufferCalculus'):
                nav=actor(u.NavMeshBoundsVolume,name+'_Navigation',(center[0],center[1],300))
                nav.set_actor_scale3d(u.Vector(15,10,5))
            if cls_name=='MCVFXLabBossStation':
                station.set_editor_property('StartDelaySeconds',.5+(index%5)*.2)
            rows.append(dict(group=group,case=case,station=station.get_path_name(),floor=floor.get_path_name(),center=center))
    lab=actor(native_class('MCVFXLab'),'00_LabCatalogue',(0,0,0))
    lab.set_editor_property('bProximityActivation',True)
    lab.set_editor_property('ActivationDistance',ACTIVATION_DISTANCE)
    lab.set_editor_property('ReleaseDistance',RELEASE_DISTANCE)
    world.get_world_settings().set_editor_property('default_game_mode',native_class('MCVFXLabGameMode'))
    # Keep the saved auto-activation flags: the native manager distinguishes
    # loops from repeated bursts and controls both by each local viewer's range.
    for a in es.get_all_level_actors():
        if isinstance(a,u.TextRenderActor) and a.get_actor_label().startswith('VFXLab_Fab_'):
            text=str(a.text_render.text).replace('manual: Details > Reset','auto repeat nearby').replace('auto repeat in play','auto repeat nearby')
            a.text_render.set_text(text)
    es.set_selected_level_actors([])
    u.SystemLibrary.execute_console_command(world,'RebuildNavigation')
    assert levels.save_current_level()
    report=dict(map=LAB,layout='open_flat',scenario_count=len(rows),scenarios=rows,arena_reference_copies=0,source_assets_saved=False,
                proximity=dict(enabled=True,activation_cm=ACTIVATION_DISTANCE,release_cm=RELEASE_DISTANCE,human_observers_only=True,distance='nearest platform bounds',samples='local viewer distance'),
                controls='PageUp/PageDown: next station; R: reset; P: pause/resume; WASD/mouse: fly')
    out=ROOT/'Saved/Codex/vfx_lab_manifest.json'
    out.write_text(json.dumps(report,indent=2),encoding='utf-8')
    print('VFX_LAB_SCENARIOS '+json.dumps(dict(map=LAB,scenario_count=len(rows),by_group={g:sum(r['group']==g for r in rows) for g,_,_,_,_ in GROUPS})))

main()
