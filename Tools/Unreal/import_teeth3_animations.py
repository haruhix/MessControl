"""Run through Epic native MCP: import Teeth2 (3) clips onto the existing skeleton."""
import json
from pathlib import Path
import unreal as u

root = Path(u.Paths.project_dir()).resolve()
lib = u.EditorAssetLibrary
assets = u.AssetToolsHelpers.get_asset_tools()
u.SystemLibrary.execute_console_command(None, 'Interchange.FeatureFlags.Import.FBX 0')
mesh = lib.load_asset('/Game/Gameplay/Character/SK_TeethGameplay')
skeleton = mesh.get_editor_property('skeleton')
report = json.loads((root/'ArtSource/CharacterAnimation/Teeth3/BakeReport.json').read_text())
clips = {}
for entry in report['clips']:
    name = 'A_Teeth_' + entry['name'].title()
    task = u.AssetImportTask()
    task.filename = str(root/'ArtSource/CharacterAnimation/Teeth3'/(name+'.fbx'))
    task.destination_path = '/Game/Gameplay/Character/Animations/Teeth3'
    task.destination_name = name
    task.automated = True
    task.replace_existing = True
    task.save = True
    task.factory = u.FbxFactory()
    opt = u.FbxImportUI()
    opt.automated_import_should_detect_type = False
    opt.import_mesh = False
    opt.import_as_skeletal = True
    opt.mesh_type_to_import = u.FBXImportType.FBXIT_ANIMATION
    opt.import_animations = True
    opt.skeleton = skeleton
    opt.import_materials = False
    opt.import_textures = False
    opt.create_physics_asset = False
    opt.anim_sequence_import_data.set_editor_property('animation_length', u.FBXAnimationLengthImportType.FBXALIT_EXPORTED_TIME)
    opt.anim_sequence_import_data.set_editor_property('import_bone_tracks', True)
    task.options = opt
    assets.import_asset_tasks([task])
    imported = [lib.load_asset(p) for p in task.imported_object_paths]
    seq = next((a for a in imported if isinstance(a, u.AnimSequence)), None)
    assert seq and seq.get_editor_property('skeleton') == skeleton, entry['name']
    clips[entry['name']] = seq
    entry['asset'] = seq.get_path_name()
    entry['seconds'] = seq.get_play_length()

profile = lib.load_asset('/Game/Data/DA_ToothAnimation')
for prop, name in [('grab_left','grab_left'), ('grab_right','grab_right'), ('push','push'), ('tired','tired')]:
    profile.set_editor_property(prop, clips[name])
lib.save_loaded_asset(profile)

emotes = lib.load_asset('/Game/Data/DA_Emotes')
entries = list(emotes.get_editor_property('entries'))
for id, label, clip, mood in [
    ('hello','Приветствие 1','hello1',u.MCEmotion.HAPPY),
    ('hello2','Приветствие 2','hello2',u.MCEmotion.HAPPY),
    ('highfive','Дай пять 1','highfive1',u.MCEmotion.HAPPY),
    ('highfive2','Дай пять 2','highfive2',u.MCEmotion.HAPPY),
    ('dance1','Танец 1','dance1',u.MCEmotion.HAPPY),
    ('dance2','Танец 2','dance2',u.MCEmotion.HAPPY),
    ('tired','Усталость','tired',u.MCEmotion.EFFORT),
    ('artist_happy','Радость','emo_happy',u.MCEmotion.HAPPY),
    ('artist_sad','Грусть — жест','emo_sad',u.MCEmotion.SAD),
    ('artist_shock','Испуг','emo_shock',u.MCEmotion.SURPRISE),
]:
    entry = next((e for e in entries if str(e.id) == id), None)
    if entry is None:
        entry = u.MCEmoteEntry()
        entries.append(entry)
    entry.id = id
    entry.label = label
    entry.animation = clips[clip]
    entry.emotion = mood
    entry.duration = 3
    entry.set_editor_property("face_only", clip.startswith("emo_"))
    entry.set_editor_property("hold_final_pose", clip.startswith("emo_") or clip == "tired")
emotes.set_editor_property('entries', entries)
lib.save_loaded_asset(emotes)
(root/'Artifacts/Teeth3_Import.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
u.log('MC_TEETH3_IMPORT_PASS')
