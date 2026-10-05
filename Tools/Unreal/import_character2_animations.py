"""Import the eight new Character2 actions, preserving the live player skin/rig.

Run with a current Editor C++ build after export_character2_animations.py.
Only the new animation packages and existing editable DA_Emotes are saved.
"""
import json
from pathlib import Path
import unreal as u

ROOT = Path(u.Paths.project_dir()).resolve()
SOURCE = ROOT / 'ArtSource/CharacterAnimation/Character2'
DEST = '/Game/Gameplay/Character/Animations/Character2'
lib = u.EditorAssetLibrary
assets = u.AssetToolsHelpers.get_asset_tools()
u.SystemLibrary.execute_console_command(None, 'Interchange.FeatureFlags.Import.FBX 0')
appearance = lib.load_asset('/Game/Data/DA_PlayerAppearance')
mesh = appearance.get_editor_property('skeletal_mesh')
skeleton = mesh.get_editor_property('skeleton')
assert skeleton, 'Current player mesh needs its saved gameplay skeleton.'
report = json.loads((SOURCE / 'BakeReport.json').read_text(encoding='utf8'))
clips = {}
for entry in report['clips']:
    name = 'A_Teeth_' + entry['name'].title()
    task = u.AssetImportTask()
    task.filename = str(SOURCE / (name + '.fbx'))
    task.destination_path = DEST
    task.destination_name = name
    task.automated = True
    task.replace_existing = True
    task.save = True
    task.factory = u.FbxFactory()
    options = u.FbxImportUI()
    options.automated_import_should_detect_type = False
    options.import_mesh = False
    options.import_as_skeletal = True
    options.mesh_type_to_import = u.FBXImportType.FBXIT_ANIMATION
    options.import_animations = True
    options.skeleton = skeleton
    options.import_materials = False
    options.import_textures = False
    options.create_physics_asset = False
    options.anim_sequence_import_data.set_editor_property('animation_length', u.FBXAnimationLengthImportType.FBXALIT_EXPORTED_TIME)
    options.anim_sequence_import_data.set_editor_property('import_bone_tracks', True)
    options.anim_sequence_import_data.set_editor_property('use_default_sample_rate', False)
    options.anim_sequence_import_data.set_editor_property('custom_sample_rate', round(report['fps']))
    task.options = options
    assets.import_asset_tasks([task])
    imported = [lib.load_asset(path) for path in task.imported_object_paths]
    clip = next((asset for asset in imported if isinstance(asset, u.AnimSequence)), None)
    assert clip and clip.get_editor_property('skeleton') == skeleton, entry['name']
    expected = (entry['frames'][1] - entry['frames'][0]) / report['fps']
    assert abs(clip.get_play_length() - expected) < .06, (entry['name'], clip.get_play_length(), expected)
    clips[entry['name']] = clip
    entry['asset'] = clip.get_path_name()
    entry['seconds'] = clip.get_play_length()

profile = lib.load_asset('/Game/Data/DA_Emotes')
entries = list(profile.get_editor_property('entries'))
for id, label, mood, looping, duration in [
    ('laugh1', 'Насмешка 1', u.MCEmotion.HAPPY, False, 3),
    ('laugh2', 'Насмешка 2', u.MCEmotion.HAPPY, False, 3),
    ('point', 'Указать', u.MCEmotion.NEUTRAL, False, 2),
    ('dance3', 'Танец 3', u.MCEmotion.HAPPY, True, 4),
    ('dance4', 'Танец 4', u.MCEmotion.HAPPY, True, 4),
    ('dance5', 'Танец 5', u.MCEmotion.HAPPY, True, 4),
    ('happy_jump', 'Прыжок радости', u.MCEmotion.HAPPY, False, 3),
    ('punch', 'Удар рукой — жест', u.MCEmotion.EFFORT, False, 3),
]:
    entry = next((value for value in entries if str(value.id) == id), None)
    if entry is None:
        entry = u.MCEmoteEntry()
        entries.append(entry)
    entry.id = id
    entry.label = label
    entry.animation = clips[id]
    entry.emotion = mood
    entry.set_editor_property('face_only', False)
    entry.set_editor_property('hold_final_pose', id == 'point')
    entry.set_editor_property('looping', looping)
    entry.duration = duration
profile.set_editor_property('entries', entries)
assert lib.save_loaded_asset(profile, only_if_is_dirty=False)
(SOURCE / 'ImportReport.json').write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding='utf8')
u.log('MC_CHARACTER2_IMPORT_PASS eight_clips existing_game_skeleton')
