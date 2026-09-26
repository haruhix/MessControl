"""Import baked clips without changing the artist mesh or skeleton; build the editable T menu."""
import unreal as u
from pathlib import Path
root=Path(u.Paths.project_dir()).resolve();lib=u.EditorAssetLibrary;assets=u.AssetToolsHelpers.get_asset_tools()
u.SystemLibrary.execute_console_command(None,'Interchange.FeatureFlags.Import.FBX 0')
mesh=lib.load_asset('/Game/Gameplay/Character/SK_TeethGameplay')
skeleton=mesh.get_editor_property('skeleton')
clips={}
for name in ['Hello','Highfive']:
    task=u.AssetImportTask();task.filename=str(root/'ArtSource/CharacterAnimation'/('A_Teeth_'+name+'.fbx'))
    task.destination_path='/Game/Gameplay/Character/Animations';task.destination_name='A_Teeth_'+name
    task.automated=True;task.replace_existing=True;task.save=True;task.factory=u.FbxFactory()
    opt=u.FbxImportUI();opt.automated_import_should_detect_type=False;opt.import_mesh=False;opt.import_as_skeletal=True
    opt.mesh_type_to_import=u.FBXImportType.FBXIT_ANIMATION;opt.import_animations=True;opt.skeleton=skeleton
    opt.import_materials=False;opt.import_textures=False;opt.create_physics_asset=False
    opt.anim_sequence_import_data.set_editor_property('animation_length',u.FBXAnimationLengthImportType.FBXALIT_EXPORTED_TIME)
    opt.anim_sequence_import_data.set_editor_property('import_bone_tracks',True)
    task.options=opt;assets.import_asset_tasks([task])
    imported=[lib.load_asset(p) for p in task.imported_object_paths]
    seq=next((a for a in imported if isinstance(a,u.AnimSequence)),None)
    assert seq,'No animation imported: '+str(task.imported_object_paths)
    assert seq.get_editor_property('skeleton')==skeleton
    clips[name]=seq
    u.log('MC_CLIP_IMPORTED '+seq.get_path_name()+' seconds='+str(seq.get_play_length()))
path='/Game/Data/DA_Emotes'
if not lib.does_asset_exist(path):
    f=u.DataAssetFactory();f.set_editor_property('data_asset_class',u.MCEmoteLibrary)
    assets.create_asset('DA_Emotes','/Game/Data',u.MCEmoteLibrary,f)
profile=lib.load_asset(path)
entries=[]
for id,label,clip,emotion in [('hello','Помахать рукой',clips['Hello'],u.MCEmotion.HAPPY),
        ('highfive','Дай пять',clips['Highfive'],u.MCEmotion.HAPPY),
        ('happy','Улыбка',None,u.MCEmotion.HAPPY),('angry','Злость',None,u.MCEmotion.ANGRY),
        ('sad','Грусть',None,u.MCEmotion.SAD),('surprise','Удивление',None,u.MCEmotion.SURPRISE)]:
    e=u.MCEmoteEntry();e.id=id;e.label=label;e.animation=clip;e.emotion=emotion;e.duration=3;entries.append(e)
profile.set_editor_property('entries',entries);lib.save_loaded_asset(profile,only_if_is_dirty=False)
u.log('MC_EMOTE_IMPORT_PASS');u.SystemLibrary.quit_editor()
