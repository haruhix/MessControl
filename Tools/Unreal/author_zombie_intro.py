"""Author the owned five-second boss intro camera sequence; never place a boss.

Run through Unreal Python after the current Editor module is compiled. The
possessable camera is rebound by MC_IntroCamera at runtime. No level is saved.
"""
import json
from pathlib import Path
import unreal as u

DEST='/Game/Gameplay/Boss/Sequences'
NAME='LS_ZombieIntro'
lib=u.EditorAssetLibrary
tools=u.AssetToolsHelpers.get_asset_tools()
actors=u.get_editor_subsystem(u.EditorActorSubsystem)
lib.make_directory(DEST)
sequence=lib.load_asset(DEST+'/'+NAME)
if sequence is None:
    sequence=tools.create_asset(NAME,DEST,u.LevelSequence,u.LevelSequenceFactoryNew())
assert isinstance(sequence,u.LevelSequence)

# Rebuild only this owned sequence. All bindings use relative, authored camera
# coordinates; the runtime instance applies the boss's floor/yaw as its origin.
for binding in list(sequence.get_bindings()):
    binding.remove()
for track in list(sequence.get_tracks()):
    sequence.remove_track(track)
sequence.set_display_rate(u.FrameRate(30,1))
sequence.set_playback_start(0)
sequence.set_playback_end(150)
sequence.set_view_range_start(0)
sequence.set_view_range_end(5)
sequence.set_work_range_start(0)
sequence.set_work_range_end(5)

camera=actors.spawn_actor_from_class(u.CineCameraActor,u.Vector(),u.Rotator(),True)
assert camera
try:
    camera.set_actor_label('MC_IntroCamera')
    camera.get_cine_camera_component().set_editor_property('current_focal_length',28)
    binding=sequence.add_possessable(camera)
    binding.set_name('MC_IntroCamera')
    transform=binding.add_track(u.MovieScene3DTransformTrack)
    section=transform.add_section()
    section.set_range(0,150)
    section.set_completion_mode(u.MovieSceneCompletionMode.RESTORE_STATE)
    channels=section.get_all_channels()
    assert len(channels)==9, len(channels)
    shots=[(0,(520,-180,145),(0,0,138)),
           (55,(470,-140,155),(0,0,148)),
           (100,(415,-100,168),(0,0,156)),
           (149,(380,-75,175),(0,0,162))]
    for frame,position,target in shots:
        aim=u.MathLibrary.find_look_at_rotation(u.Vector(*position),u.Vector(*target))
        values=(*position,aim.roll,aim.pitch,aim.yaw,1.,1.,1.)
        for channel,value in zip(channels,values):
            channel.add_key(u.FrameNumber(frame),value,interpolation=u.MovieSceneKeyInterpolation.AUTO)
    cuts=sequence.add_track(u.MovieSceneCameraCutTrack)
    cut=cuts.add_section()
    cut.set_range(0,150)
    cut.set_camera_binding_id(u.MovieSceneSequenceExtensions.get_binding_id(sequence,binding))
    assert lib.save_loaded_asset(sequence,only_if_is_dirty=False)
    report={'asset':sequence.get_path_name(),'duration_seconds':5,'fps':30,
            'camera_binding':'MC_IntroCamera','camera_keys':shots,
            'boss_animation':'/Game/Gameplay/Boss/Zombie/Animations/AN_Zombie_Roar',
            'map_modified':False,'activation':'F3 BossIntro only'}
    output=Path(u.Paths.project_dir())/'Saved/BossPresentationReview/IntroSequence.json'
    output.parent.mkdir(parents=True,exist_ok=True)
    output.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
    u.log('MC_BOSS_INTRO_AUTHORED '+json.dumps(report,ensure_ascii=False))
finally:
    actors.destroy_actor(camera)
