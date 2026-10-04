"""Import the seven owned Zombie bone clips and assign native presentation.

Run after compiling the current Editor module. No level actors are created,
no player skeleton is touched, and no game package/cook is performed.
"""
import json
from pathlib import Path
import unreal as u

ROOT=Path(u.Paths.project_dir()).resolve()
SOURCE=ROOT/'ArtSource/ZombieBossAnimations'
BOSS='/Game/Gameplay/Boss'
DEST=BOSS+'/Zombie/Animations'
lib=u.EditorAssetLibrary
assets=u.AssetToolsHelpers.get_asset_tools()
manifest=json.loads((SOURCE/'ZombieAnimationReport.json').read_text(encoding='utf8'))
mesh=lib.load_asset(BOSS+'/Zombie/SK_ZombieBoss')
skeleton=mesh.get_editor_property('skeleton')
assert skeleton.get_path_name()==BOSS+'/Zombie/SK_ZombieBoss_Skeleton.SK_ZombieBoss_Skeleton'
u.SystemLibrary.execute_console_command(None,'Interchange.FeatureFlags.Import.FBX 0')
tasks=[]
for record in manifest['clips']:
    task=u.AssetImportTask()
    task.filename=str(SOURCE/record['file'])
    task.destination_path=DEST
    task.destination_name=record['name']
    task.automated=task.save=True
    task.replace_existing=task.replace_existing_settings=True
    task.factory=u.FbxFactory()
    options=u.FbxImportUI()
    options.set_editor_property('automated_import_should_detect_type',False)
    options.set_editor_property('mesh_type_to_import',u.FBXImportType.FBXIT_ANIMATION)
    options.set_editor_property('import_mesh',False)
    options.set_editor_property('import_animations',True)
    options.set_editor_property('import_materials',False)
    options.set_editor_property('import_textures',False)
    options.set_editor_property('skeleton',skeleton)
    data=options.get_editor_property('anim_sequence_import_data')
    data.set_editor_property('animation_length',u.FBXAnimationLengthImportType.FBXALIT_EXPORTED_TIME)
    data.set_editor_property('use_default_sample_rate',True)
    data.set_editor_property('import_bone_tracks',True)
    data.set_editor_property('preserve_local_transform',True)
    data.set_editor_property('import_custom_attribute',False)
    task.options=options
    tasks.append(task)
assets.import_asset_tasks(tasks)
clips={}
for task,record in zip(tasks,manifest['clips']):
    candidates=[lib.load_asset(path) for path in task.imported_object_paths]
    sequence=next((obj for obj in candidates if isinstance(obj,u.AnimSequence)),None)
    assert sequence, (record['name'],task.imported_object_paths)
    target=DEST+'/'+record['name']
    if sequence.get_path_name().split('.')[0]!=target:
        assert not lib.does_asset_exist(target), ('Unexpected import name',sequence.get_path_name(),target)
        assert lib.rename_asset(sequence.get_path_name(),target)
        sequence=lib.load_asset(target)
    assert sequence.get_editor_property('skeleton')==skeleton
    length=sequence.get_play_length()
    assert abs(length-record['duration_seconds'])<.04, (record['name'],length,record['duration_seconds'])
    sequence.set_editor_property('enable_root_motion',False)
    assert lib.save_loaded_asset(sequence,only_if_is_dirty=False)
    clips[record['name']]=sequence

profile=lib.load_asset(BOSS+'/DA_ZombieBoss')
assert profile
for prop,name in (('idle_animation','Idle'),('walk_animation','Shamble'),('hurt_animation','Hurt'),('death_animation','Death')):
    profile.set_editor_property(prop,clips['AN_Zombie_'+name])
profile.set_editor_property('animation_class',None)
attacks=[]
for name,damage,reach,weight in (('PunchLeft',20,250,1),('PunchRight',24,250,1),('Kick',30,290,.65)):
    record=next(r for r in manifest['clips'] if r['name']=='AN_Zombie_'+name)
    attack=u.MCBossAttackDefinition()
    attack.attack_id=name
    attack.damage=damage
    attack.range=reach
    attack.half_angle_degrees=55 if name!='Kick' else 35
    attack.selection_weight=weight
    attack.windup_seconds=record['impact_seconds']
    attack.active_seconds=.10
    attack.recovery_seconds=record['duration_seconds']-record['impact_seconds']-.10
    attack.cooldown_seconds=1.7 if name!='Kick' else 3
    attack.animation=clips[record['name']]
    attacks.append(attack)
profile.set_editor_property('attacks',attacks)
assert lib.save_loaded_asset(profile,only_if_is_dirty=False)
bp=lib.load_asset(BOSS+'/BP_ZombieBoss')
cdo=u.get_default_object(bp.generated_class())
cdo.set_editor_property('start_awake',False)
u.BlueprintEditorLibrary.compile_blueprint(bp)
assert lib.save_loaded_asset(bp,only_if_is_dirty=False)
assert lib.save_loaded_asset(skeleton,only_if_is_dirty=False)
report={'clips':[{**record,'asset':clips[record['name']].get_path_name()} for record in manifest['clips']],
        'profile':profile.get_path_name(),'boss_spawn':'Explicit F3 only; this script creates no actor'}
out=ROOT/'Saved/ChestBossReview/ZombieAnimationImport.json'
out.parent.mkdir(parents=True,exist_ok=True)
out.write_text(json.dumps(report,indent=2),encoding='utf8')
u.log('MC_ZOMBIE_ANIMATION_IMPORT_PASS '+json.dumps(report))
