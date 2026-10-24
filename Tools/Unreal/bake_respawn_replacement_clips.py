"""Root-invoked Unreal-only animation baking for the isolated replacement take.

Importing this helper performs no Unreal calls. Root explicitly calls
``bake_clips('/Game/Tests/RespawnReplacement', end_frame=180)`` in its Editor.
Only the two owned destination animation packages are created/saved; production
clips, the production skeleton and the mesh are read-only inputs.
"""

DEST = '/Game/Tests/RespawnReplacement'
MESH = '/Game/Gameplay/CharacterCurrent/SK_CharacterGameplay'
OLD_SOURCE = '/Game/Gameplay/Character/Animations/Teeth3/A_Teeth_Tired'
NEW_SOURCE = '/Game/Gameplay/Character/Animations/Character2/A_Teeth_Happy_Jump'
FPS = 30


def _new_source_time(frame):
    # At frame 40 the requested second segment starts at 0.24 s.
    # Its preceding segment approaches 0.20 s; this is the requested small cut.
    if frame <= 25:
        return 0.0
    if frame < 40:
        return 0.2 * (frame - 25) / 15.0
    if frame < 65:
        return 0.24 + (0.95 - 0.24) * (frame - 40) / 25.0
    if frame < 80:
        return 0.95 + (1.25 - 0.95) * (frame - 65) / 15.0
    return 1.25


def bake_clips(root_folder, end_frame=180):
    """Return (old_clip, new_clip), both saved 30 Hz non-looping AnimSequences.

    root_folder is the Unreal package folder and must be the sandbox's fixed
    destination. end_frame is the last sampled frame, inclusive: 180 => 6 s,
    181 keys per bone. This function must only be called by the owning root
    after it has checked Editor identity and preserved unrelated edits.
    """
    import unreal as u

    assert str(root_folder).rstrip('/') == DEST, 'Only the sandbox folder is writable'
    assert isinstance(end_frame, int) and end_frame >= 80, 'End must include landing'
    assert not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor()

    lib = u.EditorAssetLibrary
    mesh = lib.load_asset(MESH)
    assert isinstance(mesh, u.SkeletalMesh), MESH
    skeleton = mesh.get_editor_property('skeleton')
    old_source = lib.load_asset(OLD_SOURCE)
    new_source = lib.load_asset(NEW_SOURCE)
    for source in (old_source, new_source):
        assert isinstance(source, u.AnimSequence), 'Missing source AnimSequence'
        assert source.get_editor_property('skeleton') == skeleton, source.get_path_name()
    assert new_source.get_play_length() >= 1.25 - 1e-5, 'Happy_Jump shorter than requested samples'

    ref = skeleton.get_reference_pose()
    names = list(ref.get_bone_names())
    root_names = {'root', 'root_x'}
    assert root_names.issubset({str(name) for name in names}), 'Expected root/root_x hierarchy'
    rest = {str(name): ref.get_bone_pose(name, u.AnimPoseSpaces.LOCAL) for name in names}
    options = u.AnimPoseEvaluationOptions()
    options.set_editor_property('optional_skeletal_mesh', mesh)
    options.set_editor_property('extract_root_motion', False)
    options.set_editor_property('evaluate_curves', False)

    # Source objects are never passed to any writer/controller operation.
    pose_cache = {}
    def local_pose(source, seconds):
        key = (source.get_path_name(), round(seconds, 10))
        if key not in pose_cache:
            pose = u.AnimPoseExtensions.get_anim_pose_at_time(source, seconds, options)
            assert u.AnimPoseExtensions.is_valid(pose), key
            pose_cache[key] = {
                str(name): pose.get_bone_pose(name, u.AnimPoseSpaces.LOCAL)
                for name in names
            }
        return pose_cache[key]

    # Evaluate all inputs before mutating either destination package.
    old_pose = local_pose(old_source, 0.0)
    new_poses = [local_pose(new_source, _new_source_time(frame))
                 for frame in range(end_frame + 1)]
    expected_length = end_frame / float(FPS)

    def bake_one(asset_name, poses):
        path = DEST + '/' + asset_name
        clip = lib.load_asset(path) if lib.does_asset_exist(path) else None
        if clip:
            assert isinstance(clip, u.AnimSequence), path
            assert clip.get_editor_property('skeleton') == skeleton, path
        else:
            factory = u.AnimSequenceFactory()
            factory.target_skeleton = skeleton
            factory.preview_skeletal_mesh = mesh
            clip = u.AssetToolsHelpers.get_asset_tools().create_asset(
                asset_name, DEST, u.AnimSequence, factory)
        assert clip and clip not in (old_source, new_source)
        assert clip.get_path_name().split('.')[0] == path, clip.get_path_name()

        controller = clip.controller
        controller.open_bracket('Bake isolated replacement body; normalize roots', False)
        try:
            controller.remove_all_bone_tracks(False)
            controller.set_frame_rate(u.FrameRate(FPS, 1), False)
            controller.set_number_of_frames(u.FrameNumber(end_frame), False)
            for name in names:
                bone = str(name)
                sampled = [pose[bone] for pose in poses]
                # Preserve source limb rotations/translations/scales. Root and
                # root_x alone lose baked trajectory and inconsistent -90X.
                if bone in root_names:
                    translations = [rest[bone].translation] * len(sampled)
                    rotations = [rest[bone].rotation] * len(sampled)
                else:
                    translations = [value.translation for value in sampled]
                    rotations = [value.rotation for value in sampled]
                scales = [value.scale3d for value in sampled]
                assert controller.add_bone_curve(name, False), bone
                assert controller.set_bone_track_keys(
                    name, translations, rotations, scales, False), bone
        finally:
            controller.close_bracket(False)

        # Remove stale curves only on this owned destination when rerunning.
        # No skeleton curve metadata is added or changed by this helper.
        u.AnimationLibrary.remove_all_curve_data(clip)
        clip.set_editor_property('enable_root_motion', False)
        clip.set_editor_property('force_root_lock', False)
        assert abs(clip.get_play_length() - expected_length) < 1e-4, (
            path, clip.get_play_length(), expected_length)
        assert lib.save_loaded_asset(clip, only_if_is_dirty=False), path
        return clip

    old_clip = bake_one('A_OldBody', [old_pose] * (end_frame + 1))
    new_clip = bake_one('A_NewBody', new_poses)
    u.log('MC_REPLACEMENT_CLIPS_SAVED old={} new={} frames={} fps={} normalized=root,root_x'.format(
        old_clip.get_path_name(), new_clip.get_path_name(), end_frame, FPS))
    return old_clip, new_clip
