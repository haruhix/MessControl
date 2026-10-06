"""Import ARP FBXs onto one stable skeleton, optionally connect the phase-3 boss.

Run inside the current Unreal Editor. The mesh defines the reference skeleton
once. Future animation imports always select it and never replace its rest pose.
"""
import json
from pathlib import Path
import unreal as u

ROOT = Path(u.Paths.project_dir()).resolve()
SOURCE = ROOT / 'ArtSource/BossPhase3ARP'
DEST = '/Game/Gameplay/Boss/Phase3'
RIG = DEST + '/Rig'
ANIM = DEST + '/Animations'
lib = u.EditorAssetLibrary
tools = u.AssetToolsHelpers.get_asset_tools()
manifest = json.loads((SOURCE / 'ExportReport.json').read_text(encoding='utf8'))
u.SystemLibrary.execute_console_command(None,'Interchange.FeatureFlags.Import.FBX 0')

def run_import(filename, destination, name, skeleton=None, mesh=False):
    task = u.AssetImportTask()
    task.filename = str(SOURCE / filename)
    task.destination_path, task.destination_name = destination, name
    task.automated = True
    task.save = False
    task.replace_existing = task.replace_existing_settings = True
    task.factory = u.FbxFactory()
    options = u.FbxImportUI()
    options.automated_import_should_detect_type = False
    options.mesh_type_to_import = u.FBXImportType.FBXIT_SKELETAL_MESH if mesh else u.FBXImportType.FBXIT_ANIMATION
    options.import_mesh = options.import_as_skeletal = mesh
    options.import_animations = not mesh
    options.import_materials = options.import_textures = options.create_physics_asset = False
    if skeleton:
        options.skeleton = skeleton
    data = options.skeletal_mesh_import_data if mesh else options.anim_sequence_import_data
    data.set_editor_property('import_uniform_scale',1.)
    if mesh:
        data.set_editor_property('import_morph_targets',True)
        data.set_editor_property('update_skeleton_reference_pose',False)
        data.set_editor_property('use_t0_as_ref_pose',False)
        data.set_editor_property('normal_import_method',u.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS)
    else:
        data.set_editor_property('animation_length',u.FBXAnimationLengthImportType.FBXALIT_EXPORTED_TIME)
        data.set_editor_property('use_default_sample_rate',True)
        data.set_editor_property('import_bone_tracks',True)
        data.set_editor_property('preserve_local_transform',True)
        data.set_editor_property('import_custom_attribute',False)
    task.options = options
    tools.import_asset_tasks([task])
    cls = u.SkeletalMesh if mesh else u.AnimSequence
    obj = next((u.load_asset(p) for p in task.imported_object_paths if isinstance(u.load_asset(p),cls)),None)
    assert obj, (name,list(task.imported_object_paths))
    target = destination + '/' + name
    if obj.get_path_name().split('.')[0] != target:
        assert not lib.does_asset_exist(target)
        assert lib.rename_asset(obj.get_path_name(),target)
        obj = u.load_asset(target)
    return obj

existing = u.load_asset(RIG+'/SK_BossPhase3_ARP') if lib.does_asset_exist(RIG+'/SK_BossPhase3_ARP') else None
contract_file = SOURCE / 'RigContract.json'
contract = json.loads(contract_file.read_text()) if contract_file.exists() else None
if contract:
    assert manifest['source_rig_signature']==contract['source_rig_signature'], 'Rig rest pose changed; use a deliberate migration.'
    assert manifest['settings']==contract['settings'], 'Export settings changed; restore the stable rig settings.'
# Animation updates reuse the saved mesh/skeleton, never reconstruct its bind pose.
mesh = existing or run_import(manifest['mesh_file'],RIG,'SK_BossPhase3_ARP',mesh=True)
skeleton = mesh.get_editor_property('skeleton')
assert skeleton.get_path_name().startswith(RIG+'/')
material = u.load_asset('/Game/Art/Materials/Bosses/Guardian/MI_Boss')
slots = list(mesh.get_editor_property('materials'))
for slot in slots:
    slot.material_interface = material
mesh.set_editor_property('materials',slots)
component = u.new_object(u.SkeletalMeshComponent)
component.set_skeletal_mesh_asset(mesh)
hierarchy = [{'name':str(component.get_bone_name(i)), 'parent':str(component.get_parent_bone(component.get_bone_name(i)))}
             for i in range(component.get_num_bones())]
assert len(hierarchy)>90
if contract:
    assert hierarchy==contract['hierarchy'] and skeleton.get_path_name()==contract['skeleton']
clips = {}
rows = []
for record in manifest['clips']:
    clip = run_import(record['file'],ANIM,record['name'],skeleton)
    assert clip.get_editor_property('skeleton') == skeleton
    assert abs(clip.get_play_length()-record['duration_seconds']) < .04
    clip.set_editor_property('enable_root_motion',False)
    clip.set_preview_skeletal_mesh(mesh)
    clips[record['name']] = clip
    options = u.AnimPoseEvaluationOptions()
    options.optional_skeletal_mesh = mesh
    frames = []
    for fraction in [0.,.25,.5,.75,1.]:
        t=clip.get_play_length()*fraction
        pose=u.AnimPoseExtensions.get_anim_pose_at_time(clip,t,options)
        bone_names=[str(n) for n in u.AnimPoseExtensions.get_bone_names(pose)]
        bones={}
        for n in bone_names:
            if n in ['root','root_x','pelvis','head','head_x','foot_l','foot_r','hand_l','hand_r']:
                tr=u.AnimPoseExtensions.get_bone_pose(pose,n,u.AnimPoseSpaces.WORLD)
                bones[n]={'position':list(tr.translation.to_tuple()),'scale':list(tr.scale3d.to_tuple())}
        frames.append({'time':t,'bone_count':len(bone_names),'bones':bones})
    rows.append({**record,'asset':clip.get_path_name(),'duration':clip.get_play_length(),'poses':frames})
u.AnimationLibrary.set_skeleton_preview_mesh(skeleton,mesh)
for obj in [mesh,skeleton,*clips.values()]:
    assert lib.save_loaded_asset(obj,only_if_is_dirty=False)
report={'mesh':mesh.get_path_name(),'skeleton':skeleton.get_path_name(),'bounds':str(mesh.get_bounds()),
        'hierarchy':hierarchy,'clips':rows,'material':material.get_path_name()}
(SOURCE/'ImportReport.json').write_text(json.dumps(report,indent=2))
if not contract:
    contract_file.write_text(json.dumps({'source_rig_signature':manifest['source_rig_signature'],
        'settings':manifest['settings'],'hierarchy':hierarchy,'skeleton':skeleton.get_path_name()},indent=2))
print('ARP_IMPORT_PASS',json.dumps({k:v for k,v in report.items() if k not in ['hierarchy','clips']}))
