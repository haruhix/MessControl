"""Author an isolated visual proposal; never touches real death/respawn rules.

Run in a clean, idle MessControl Editor using Tools/Unreal/remote_python.py.
Assets stay under /Game/Tests/RespawnReplacement. PIE plays the take on a loop.
"""
import json, math
from pathlib import Path
import unreal as u

ROOT='/Game/Tests/RespawnReplacement'
MAP=ROOT+'/L_RespawnReplacementStage'
SEQ=ROOT+'/LS_ReplacementTake'
TAG='MC.RespawnReplacement.Sandbox'
OUT=Path('E:/CodexWork/RespawnReplacement_20261024')
FPS=30
END=180

def main():
    editor=u.get_editor_subsystem(u.UnrealEditorSubsystem)
    actors=u.get_editor_subsystem(u.EditorActorSubsystem)
    assert not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor()
    active=editor.get_editor_world()
    active_path=active.get_path_name()
    selected=[a.get_path_name() for a in actors.get_selected_level_actors()]
    dirty=list(u.EditorLoadingAndSavingUtils.get_dirty_content_packages())+list(u.EditorLoadingAndSavingUtils.get_dirty_map_packages())
    assert all(p.get_path_name().startswith(ROOT) for p in dirty), 'Preserve unrelated unsaved edits before authoring the isolated map'
    assert active.get_path_name().split('.')[0]!=MAP, 'Author from another clean map'
    lib=u.EditorAssetLibrary
    assets=u.AssetToolsHelpers.get_asset_tools()
    world=None
    sequence=lib.load_asset(SEQ) if lib.does_asset_exist(SEQ) else None
    if not sequence:sequence=assets.create_asset('LS_ReplacementTake',ROOT,u.LevelSequence,u.LevelSequenceFactoryNew())
    for binding in list(sequence.get_bindings()):binding.remove()
    for track in list(sequence.get_tracks()):sequence.remove_track(track)
    sequence.set_display_rate(u.FrameRate(FPS,1))
    sequence.set_playback_start(0);sequence.set_playback_end(END)
    sequence.set_view_range_start(0);sequence.set_view_range_end(END/FPS)
    sequence.set_work_range_start(0);sequence.set_work_range_end(END/FPS)
    created=[]
    def actor(cls,name,p=(0,0,0),r=(0,0,0),s=(1,1,1)):
        assert editor.get_editor_world()==world
        a=actors.spawn_actor_from_class(cls,u.Vector(*p))
        assert a and a.get_world()==world
        a.set_actor_label('Respawn_'+name);a.tags=[u.Name(TAG),u.Name(name)]
        a.set_actor_location(u.Vector(*p),False,False)
        a.set_actor_rotation(u.Rotator(pitch=r[0],yaw=r[1],roll=r[2]),False)
        a.set_actor_scale3d(u.Vector(*s));created.append(a)
        return a
    def static(name,mesh,p,s=(1,1,1),material=None,r=(0,0,0)):
        a=actor(u.StaticMeshActor,name,p,r,s)
        c=a.static_mesh_component;c.set_mobility(u.ComponentMobility.MOVABLE)
        assert c.set_static_mesh(u.load_asset(mesh))
        c.set_collision_enabled(u.CollisionEnabled.NO_COLLISION)
        if material:c.set_material(0,u.load_asset(material))
        return a
    def transform(a,keys):
        binding=sequence.add_possessable(a)
        track=binding.add_track(u.MovieScene3DTransformTrack)
        sec=track.add_section();sec.set_range(0,END)
        sec.set_completion_mode(u.MovieSceneCompletionMode.RESTORE_STATE)
        channels=sec.get_all_channels()
        for frame,p,r,s in keys:
            values=(*p,r[2],r[0],r[1],*s)
            for channel,value in zip(channels,values):
                channel.add_key(u.FrameNumber(frame),float(value),interpolation=u.MovieSceneKeyInterpolation.AUTO)
        return binding
    def anim(binding,clip,start,end):
        track=binding.add_track(u.MovieSceneSkeletalAnimationTrack)
        sec=track.add_section();sec.set_range(start,end)
        sec.set_completion_mode(u.MovieSceneCompletionMode.RESTORE_STATE)
        params=sec.params;params.animation=u.load_asset(clip);sec.params=params
    def variant(name,parent,vectors={},scalars={}):
        path=ROOT+'/'+name
        mat=lib.load_asset(path) if lib.does_asset_exist(path) else assets.create_asset(name,ROOT,u.MaterialInstanceConstant,u.MaterialInstanceConstantFactoryNew())
        u.MaterialEditingLibrary.set_material_instance_parent(mat,u.load_asset(parent))
        for key,value in vectors.items():u.MaterialEditingLibrary.set_material_instance_vector_parameter_value(mat,key,u.LinearColor(*value,1))
        for key,value in scalars.items():u.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(mat,key,value)
        u.MaterialEditingLibrary.update_material_instance(mat)
        assert lib.save_loaded_asset(mat,only_if_is_dirty=False)
        return path
    def matte(name,color,roughness):
        path=ROOT+'/'+name
        mat=lib.load_asset(path) if lib.does_asset_exist(path) else None
        if not mat:
            mat=assets.create_asset(name,ROOT,u.Material,u.MaterialFactoryNew())
            tint=u.MaterialEditingLibrary.create_material_expression(mat,u.MaterialExpressionConstant3Vector,-300,-60)
            tint.set_editor_property('constant',u.LinearColor(*color,1))
            rough=u.MaterialEditingLibrary.create_material_expression(mat,u.MaterialExpressionConstant,-300,80)
            rough.set_editor_property('r',roughness)
            u.MaterialEditingLibrary.connect_material_property(tint,'',u.MaterialProperty.MP_BASE_COLOR)
            u.MaterialEditingLibrary.connect_material_property(rough,'',u.MaterialProperty.MP_ROUGHNESS)
            u.MaterialEditingLibrary.recompile_material(mat)
            assert lib.save_loaded_asset(mat,only_if_is_dirty=False)
        return path
    gum=matte('M_SoftGum',(.42,.135,.165),.64)
    backdrop=matte('M_StudioBackdrop',(.045,.052,.073),.86)
    enamel='/Game/Art/Materials/M_ArenaTooth'
    appearance=u.load_asset('/Game/Data/DA_PlayerAppearance')
    bag=variant('MI_PlayerBag','/Game/Gameplay/CharacterCurrent/Materials/MI_Bag',{'Albedo Color':(.035,.35,.52)})
    # Compression may collect inactive-world actors. Finish asset work before
    # duplicating actors into the inactive destination World.
    helper=Path(__file__).resolve().parent/'bake_respawn_replacement_clips.py'
    assert helper.exists(),'Pose authoring helper is required'
    namespace={};exec(compile(helper.read_text(encoding='utf8'),str(helper),'exec'),namespace)
    old_clip,new_clip=namespace['bake_clips'](ROOT,END)
    # Native spawning in the isolated active map survives asset compression/GC.
    level_editor=u.get_editor_subsystem(u.LevelEditorSubsystem)
    if lib.does_asset_exist(MAP):assert level_editor.load_level(MAP)
    else:assert level_editor.new_level(MAP)
    world=editor.get_editor_world();level=world.get_world_settings().get_outer()
    for a in list(actors.get_all_level_actors()):
        if TAG in [str(t) for t in a.tags]:a.destroy_actor()
    actor(u.PlayerStart,'OffstagePlayerStart',(0,-2300,500))
    static('Backdrop','/Engine/BasicShapes/Cube',(0,1050,250),(28,.2,16),backdrop)
    # Existing mouth surfaces/materials, referenced directly by the sandbox map.
    static('TongueStage','/Engine/BasicShapes/Cube',(0,-180,-15),(20,16,.3),gum)
    static('GumBed','/Engine/BasicShapes/Sphere',(75,235,-60),(17,5.0,2.0),gum)
    static('EmptySocket','/Game/Gameplay/Throat/SM_GingivalCuff',(75,170,33),(.80,.80,.25),gum)
    arena=u.load_asset('/Game/Maps/L_Mouth')
    socket_class=u.load_class(None,'/Script/MessControl.MCArenaToothSocket')
    sockets=sorted(u.GameplayStatics.get_all_actors_of_class(arena,socket_class),key=lambda a:a.get_editor_property('tooth_id'))
    assert len(sockets)>=3
    source=sockets[0].get_editor_property('preview')
    real_mesh=source.static_mesh.get_path_name()
    real_scale=tuple(source.get_world_transform().scale3d.to_tuple())
    source_height=source.get_world_transform().translation.z
    # Actual arena asset and scale, including roots seated below the gum.
    for i,x in enumerate([-425,575]):
        preview=sockets[0 if i==0 else 2].get_editor_property('preview')
        static('ReserveNeighbor_%d'%i,preview.static_mesh.get_path_name(),(x,190,source_height),real_scale,enamel,r=(0,-90,0))
    reserve=static('ActualReserve',real_mesh,(75,170,source_height),real_scale,enamel,r=(0,-90,0))
    transform(reserve,[
        (0,(75,170,source_height),(0,-90,0),real_scale),
        (22,(75,170,source_height),(0,-90,0),real_scale),
        (27,(75,170,source_height),(0,-90,-3),real_scale),
        (32,(75,170,source_height),(0,-90,2),real_scale),
        (37,(75,170,source_height-15),(0,-90,0),real_scale),
        (39,(75,170,source_height-15),(0,-90,0),real_scale),
        (42,(75,170,-650),(0,-90,0),real_scale),
        (179,(75,170,-650),(0,-90,0),real_scale)])
    def hero(name,p):
        a=actor(u.SkeletalMeshActor,name,p,(0,180,0))
        c=a.skeletal_mesh_component
        c.set_skeletal_mesh_asset(appearance.skeletal_mesh)
        c.set_material(0,appearance.material)
        c.set_material(1,u.load_asset(bag))
        c.set_collision_enabled(u.CollisionEnabled.NO_COLLISION)
        c.set_animation_mode(u.AnimationMode.ANIMATION_SINGLE_NODE)
        c.set_update_animation_in_editor(True)
        return a
    old=hero('OldBody',(-150,-110,0))
    old.skeletal_mesh_component.set_animation(old_clip)
    b=transform(old,[
        (0,(-150,-110,0),(0,180,0),(1,1,1)),
        (5,(-154,-115,5),(5,177,-18),(1,1,1)),
        (11,(-175,-135,47),(0,170,-65),(1,1,1)),
        (15,(-195,-142,57),(0,168,-83),(1,1,1)),
        (20,(-201,-145,25),(0,168,-76),(1,1,1)),
        (29,(-205,-145,25),(0,168,-76),(1,1,1)),
        (179,(-205,-145,25),(0,168,-76),(1,1,1))])
    anim(b,old_clip.get_path_name(),0,END)
    new=hero('NewBody',(75,170,-140))
    new.skeletal_mesh_component.set_animation(new_clip)
    # A real second body starts seated in its socket. No teleport or model morph.
    keys=[
        (0,(75,170,-140),(0,180,0),(1,1,1)),
        (37,(75,170,-140),(-8,180,0),(1.035,1.035,.93)),
        (41,(75,162,-140),(-8,180,0),(1.035,1.035,.93)),
        (43,(75,144,111),(-10,180,0),(.98,.98,1.04)),
        (48,(75,95,195),(-8,180,-3),(1,1,1)),
        (53,(75,33,229),(-3,180,-2),(1,1,1)),
        (57,(75,-25,197),(4,180,2),(1,1,1)),
        (61,(75,-80,115),(9,180,3),(1,1,1)),
        (65,(75,-116,0),(6,180,0),(1.055,1.055,.90)),
        (68,(75,-120,0),(3,180,-2),(1.055,1.055,.90)),
        (73,(75,-122,0),(-3,180,1),(.99,.99,1.02)),
        (79,(75,-122,0),(0,180,0),(1,1,1)),
        (92,(75,-128,0),(0,172,0),(1,1,1)),
        (110,(75,-132,0),(0,180,0),(1,1,1)),
        (179,(75,-132,0),(0,180,0),(1,1,1))]
    b=transform(new,keys)
    anim(b,new_clip.get_path_name(),0,END)
    # Motivated broad lights: soft warm key, cooler enamel edge, warm gum bounce.
    def area(name,p,target,power,color,width,height):
        a=actor(u.RectLight,name,p)
        a.set_actor_rotation(u.MathLibrary.find_look_at_rotation(u.Vector(*p),u.Vector(*target)),False)
        c=a.rect_light_component;c.set_mobility(u.ComponentMobility.MOVABLE)
        c.set_editor_property('intensity_units',u.LightUnits.CANDELAS)
        c.set_editor_property('intensity',power);c.set_editor_property('source_width',width);c.set_editor_property('source_height',height)
        c.set_editor_property('attenuation_radius',2400.)
        c.set_light_color(u.LinearColor(*color,1))
        return a
    area('Key',(-230,-320,560),(0,60,60),46000,(1,.87,.72),420,350)
    area('Rim',(300,350,440),(0,40,90),65000,(.60,.83,1),320,280)
    area('Fill',(-460,150,180),(0,0,85),14000,(1,.43,.35),400,300)
    sky=actor(u.SkyLight,'Ambient',(0,0,500))
    sky.light_component.set_mobility(u.ComponentMobility.MOVABLE)
    sky.light_component.set_editor_property('intensity',.5)
    sky.light_component.set_editor_property('lower_hemisphere_is_black',False)
    camera=actor(u.CineCameraActor,'PresentationCamera',(-460,-890,470))
    camera.set_actor_rotation(u.MathLibrary.find_look_at_rotation(camera.get_actor_location(),u.Vector(10,50,145)),False)
    cc=camera.get_cine_camera_component()
    film=cc.filmback;film.sensor_width=36;film.sensor_height=20.25;cc.filmback=film
    cc.set_editor_property('current_focal_length',42.);cc.set_editor_property('current_aperture',5.6)
    focus=cc.focus_settings;focus.focus_method=u.CameraFocusMethod.DISABLE;cc.focus_settings=focus
    pp=cc.post_process_settings
    pp.set_editor_property('override_auto_exposure_method',True);pp.set_editor_property('auto_exposure_method',u.AutoExposureMethod.AEM_MANUAL)
    pp.set_editor_property('override_auto_exposure_bias',True);pp.set_editor_property('auto_exposure_bias',-7.)
    pp.set_editor_property('override_auto_exposure_apply_physical_camera_exposure',True);pp.set_editor_property('auto_exposure_apply_physical_camera_exposure',False)
    pp.set_editor_property('override_vignette_intensity',True);pp.set_editor_property('vignette_intensity',.22)
    cc.post_process_settings=pp;cc.post_process_blend_weight=1.
    camera.set_editor_property('auto_activate_for_player',u.AutoReceiveInput.PLAYER0)
    binding=sequence.add_possessable(camera)
    cuts=sequence.add_track(u.MovieSceneCameraCutTrack)
    cut=cuts.add_section();cut.set_range(0,END)
    cut.set_camera_binding_id(u.MovieSceneSequenceExtensions.get_binding_id(sequence,binding))
    cut.set_completion_mode(u.MovieSceneCompletionMode.RESTORE_STATE)
    player=actor(u.LevelSequenceActor,'LoopTake')
    player.set_sequence(sequence)
    settings=player.playback_settings;settings.auto_play=True;settings.loop_count=u.MovieSceneSequenceLoopCount(-1)
    settings.hide_hud=True;settings.disable_movement_input=True;settings.disable_look_at_input=True
    player.set_editor_property('playback_settings',settings)
    world.get_world_settings().set_editor_property('default_game_mode',u.GameModeBase)
    assert lib.save_loaded_asset(sequence,only_if_is_dirty=False)
    assert level_editor.save_current_level()
    labels=[a.get_actor_label() for a in created]
    actors.set_selected_level_actors([])
    report={'map':MAP,'sequence':SEQ,'source_map_untouched':active_path,'restore_selection':selected,'actors':labels,'reserve_mesh':real_mesh,'reserve_scale':real_scale,'duration_seconds':END/FPS,'fps':FPS,'production_respawn_changed':False,'network_tested':False,'visual_review':'pending'}
    OUT.mkdir(parents=True,exist_ok=True)
    (OUT/'Authoring.json').write_text(json.dumps(report,indent=2),encoding='utf8')
    u.log('RESPAWN_SANDBOX_AUTHORED '+MAP)

main()
