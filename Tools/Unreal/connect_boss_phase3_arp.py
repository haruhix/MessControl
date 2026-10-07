"""Wire current ARP clips to the phase-3 profile and remove obsolete clips.

Run after import_boss_phase3_arp.py in the open editor. Repeating this script
reads the manifest, so future clips can be added without replacing the skeleton.
"""
import hashlib
import json
import shutil
from pathlib import Path
import unreal as u

ROOT = Path(u.Paths.project_dir()).resolve()
SOURCE = ROOT/'ArtSource/BossPhase3ARP'
DEST = '/Game/Gameplay/Boss/Phase3'
lib = u.EditorAssetLibrary
manifest = json.loads((SOURCE/'ExportReport.json').read_text(encoding='utf8'))
imported = json.loads((SOURCE/'ImportReport.json').read_text(encoding='utf8'))
mesh = u.load_asset(imported['mesh'])
skeleton = mesh.get_editor_property('skeleton')
profile = u.load_asset(DEST+'/DA_BossPhase3')
bp = u.load_asset(DEST+'/BP_BossPhase3')
assert not u.get_editor_subsystem(u.UnrealEditorSubsystem).get_game_world()
backup = ROOT/'Artifacts/BossPhase3ARP/Before'
backup.mkdir(parents=True,exist_ok=True)
backup_rows=[]
for path in (ROOT/'Content/Gameplay/Boss/Phase3').rglob('*.uasset'):
    if '/Rig/' in path.as_posix() or '_ARP_' in path.name:
        continue
    relative=path.relative_to(ROOT/'Content')
    target=backup/relative
    target.parent.mkdir(parents=True,exist_ok=True)
    if not target.exists():
        shutil.copy2(path,target)
    backup_rows.append({'file':str(relative),'sha256':hashlib.sha256(target.read_bytes()).hexdigest()})

bounds = mesh.get_bounds()
base_scale = 240. / (bounds.box_extent.z * 2.)
scale = base_scale * 1.25
ground_offset = -110. + (bounds.origin.z-bounds.box_extent.z)*(base_scale-scale)
transform=u.Transform(location=u.Vector(0,0,ground_offset),rotation=u.Rotator(pitch=0,yaw=-90,roll=0),scale=u.Vector(scale,scale,scale))
profile.set_editor_property('skeletal_mesh',mesh)
profile.set_editor_property('mesh_transform',transform)
profile.set_editor_property('body_hitbox_scale',1.25)
profile.set_editor_property('animation_class',None)
profile.set_editor_property('animation_blend_seconds',.30)
profile.set_editor_property('phases',[])
profile.set_editor_property('decision_interval',.1)
for role in ['idle','walk','hurt','death','roar']:
    match=next((r for r in manifest['clips'] if r['role']==role),None)
    clip=u.load_asset(DEST+'/Animations/'+match['name']) if match else None
    profile.set_editor_property(role+'_animation',clip)
attacks=[]
attack_ids=set()
for record in manifest['clips']:
    if record['role']!='attack':
        continue
    clip=u.load_asset(DEST+'/Animations/'+record['name'])
    assert record['attack_id'] and record['attack_id'] not in attack_ids, 'Attack IDs must be unique.'
    attack_ids.add(record['attack_id'])
    attack=u.MCBossAttackDefinition()
    attack.attack_id=record['attack_id']
    attack.animation=clip
    attack.damage=record.get('damage',24.)
    attack.range=record.get('range',250.)
    attack.half_angle_degrees=record.get('half_angle_degrees',55.)
    attack.selection_weight=record.get('weight',1.)
    attack.start_delay_seconds=record.get('start_delay_seconds',.50)
    attack.windup_seconds=(record['impact_frame']-record['start'])/manifest['fps']
    attack.mouth_clot_attack=record.get('mouth_clot_attack',False)
    attack.clot_count=record.get('clot_count',28)
    attack.clot_damage=record.get('clot_damage',4.)
    attack.wind_push_acceleration=record.get('wind_push_acceleration',260.)
    attack.active_seconds=record.get('active_seconds',.1)
    attack.recovery_seconds=clip.get_play_length()-attack.windup_seconds-attack.active_seconds
    assert attack.recovery_seconds>=.05
    attack.cooldown_seconds=record.get('cooldown',3.5)
    attacks.append(attack)
profile.set_editor_property('attacks',attacks)
assert lib.save_loaded_asset(profile,only_if_is_dirty=False)
u.BlueprintEditorLibrary.compile_blueprint(bp)
cdo=u.get_default_object(bp.generated_class())
cdo.set_editor_property('profile',profile)
cdo.set_editor_property('start_awake',False)
component=cdo.get_editor_property('mesh')
component.set_skeletal_mesh_asset(mesh)
component.set_relative_transform(transform,False,True)
component.set_editor_property('override_materials',[])
component.set_editor_property('bounds_scale',1.5)
u.BlueprintEditorLibrary.compile_blueprint(bp)
assert lib.save_loaded_asset(bp,only_if_is_dirty=False)

obsolete=[]
for name in ['Idle','Walk','PunchLeft','PunchRight','Kick','Hurt','Death','Roar']:
    path=DEST+'/Animations/AN_BossPhase3_'+name
    if not lib.does_asset_exist(path):
        continue
    references=list(lib.find_package_referencers_for_asset(path,load_assets_to_confirm=True))
    assert not references,(path,references)
    assert lib.delete_asset(path),path
    obsolete.append(path)
previous=json.loads((SOURCE/'IntegrationReport.json').read_text()) if (SOURCE/'IntegrationReport.json').exists() else {}
obsolete=sorted(set(obsolete+previous.get('deleted_old_animations',[])))
report={'blueprint':bp.get_path_name(),'profile':profile.get_path_name(),'mesh':mesh.get_path_name(),
        'skeleton':skeleton.get_path_name(),'scale':scale,'height_cm':300.,'body_hitbox_scale':1.25,
        'clips':[r['name'] for r in manifest['clips']],
        'attacks':[{'id':str(a.attack_id),'start_delay':a.start_delay_seconds,'windup':a.windup_seconds,'recovery':a.recovery_seconds} for a in attacks],
        'animation_blend_seconds':profile.get_editor_property('animation_blend_seconds'),
        'attack_playback':'Full weight from the first frame; blend out only after the clip completes.',
        'deleted_old_animations':obsolete,'backup':str(backup),'backup_files':backup_rows,
        'future_import':'Reuse this exact skeleton; append clips.json entries and run exporter/importer/connect.'}
(SOURCE/'IntegrationReport.json').write_text(json.dumps(report,indent=2))
print('ARP_CONNECTED',json.dumps({k:v for k,v in report.items() if k!='backup_files'}))
u.get_editor_subsystem(u.AssetEditorSubsystem).open_editor_for_assets([bp,u.load_asset(DEST+'/Animations/AN_BossPhase3_ARP_Walk')])
