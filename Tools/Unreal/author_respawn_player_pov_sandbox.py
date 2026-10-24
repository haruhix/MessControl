"""Author a local-player death-camera proposal in a separate sandbox map.

Offline-authored script: run explicitly in a clean idle MessControl Editor.
Only L_RespawnPlayerPOV and LS_ReplacementPlayerPOV are saved. Original arena,
replacement map/sequence, meshes, animation clips and native respawn stay inputs.

UE 5.8 LevelEditorSubsystem has no SaveCurrentLevelAs Python function. Its
NewLevelFromTemplate is the native create/save/open-copy path used here; later
saves use SaveCurrentLevel, never EditorAssetLibrary.save_loaded_asset(World).

This is a Sequence preview seen through the local PIE PlayerController, not an
integrated native death/possession transition. Existing bodies remain animated
SkeletalMeshActors. Start/end framing uses the source free-orbit formula; it
does not claim to reproduce L_Mouth's automatic arena-camera initial position.
"""
import datetime
import copy
import hashlib
import json
import math
import re
import shutil
from pathlib import Path

import unreal as u


PROJECT = Path('E:/DEVGAME/MessControl')
ROOT = '/Game/Tests/RespawnReplacement'
SOURCE_MAP = ROOT+'/L_RespawnReplacementStage'
SOURCE_SEQUENCE = ROOT+'/LS_ReplacementFinal'
MAP = ROOT+'/L_RespawnPlayerPOV'
SEQUENCE = ROOT+'/LS_ReplacementPlayerPOV'
SOURCE_TAG = 'MC.RespawnReplacement.Sandbox'
OWNER = 'MC.RespawnReplacement.PlayerPOV.V1'
META_OWNER = 'MC_PlayerPOV_Owner'
META_SOURCE = 'MC_PlayerPOV_SourceSequence_SHA256'
OUT = Path('E:/CodexWork/RespawnPlayerPOV_20261024')
FPS, END = 30, 180


def require(ok, message):
    if not ok:
        raise RuntimeError(message)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def package_file(package, extension):
    require(package.startswith(ROOT+'/') and '..' not in package, 'Unexpected package scope')
    return PROJECT/'Content'/(package.removeprefix('/Game/')+extension)


def dirty():
    return list(u.EditorLoadingAndSavingUtils.get_dirty_content_packages()) + list(
        u.EditorLoadingAndSavingUtils.get_dirty_map_packages())


def vec(value):
    return u.Vector(*[float(x) for x in value])


def lerp(a, b, amount):
    return a+(b-a)*amount


def smooth(start, end, frame):
    amount = max(0.0, min(1.0, (frame-start)/(end-start)))
    return amount*amount*(3.0-2.0*amount)


def role(actors, name):
    found = [actor for actor in actors.get_all_level_actors()
             if name in [str(tag) for tag in actor.tags]]
    require(len(found) == 1 and SOURCE_TAG in [str(tag) for tag in found[0].tags],
            'Expected one owned sandbox actor: '+name)
    return found[0]


def sample_translation(sequence, binding):
    tracks = [track for track in binding.get_tracks() if isinstance(track, u.MovieScene3DTransformTrack)]
    require(len(tracks) == 1, 'Expected one source transform track: '+binding.get_name())
    sections = list(tracks[0].get_sections())
    require(len(sections) == 1, 'Expected one full source transform section')
    channels = list(sections[0].get_all_channels())
    require(len(channels) >= 9, 'Source transform channels are incomplete')
    sampled = [list(channel.evaluate_keys(sequence.make_range(0, END), u.FrameRate(FPS, 1)))
               for channel in channels[:3]]
    require(all(len(values) == END for values in sampled), 'Source channel sampling length changed')
    require(all(math.isfinite(float(value)) for values in sampled for value in values),
            'Invalid source transform values')
    return [[float(sampled[axis][frame]) for axis in range(3)] for frame in range(END)]


def inspect_source(editor, actors, sequence):
    world = editor.get_editor_world()
    require(world.get_path_name().split('.')[0] == SOURCE_MAP, 'Source sandbox was not opened')
    names, samples, initial, transform_keys = {}, {}, {}, {}
    allowed = {'OldBody', 'NewBody', 'ActualReserve', 'PresentationCamera'}
    for binding in sequence.get_bindings():
        bound = list(u.MovieSceneSequenceExtensions.locate_bound_objects(sequence, binding, world))
        require(len(bound) == 1 and isinstance(bound[0], u.Actor),
                'Source actor binding did not resolve: '+binding.get_name())
        tags = {str(tag) for tag in bound[0].tags}
        roles = allowed.intersection(tags)
        require(len(roles) == 1 and SOURCE_TAG in tags, 'Unknown source binding role')
        name = next(iter(roles))
        require(binding.get_name() not in names and name not in names.values(), 'Duplicate source binding')
        names[binding.get_name()] = name
        transform = bound[0].get_actor_transform()
        rotation = transform.rotation.rotator()
        initial[name] = dict(location=list(transform.translation.to_tuple()),
                             rotation=[float(rotation.pitch), float(rotation.yaw), float(rotation.roll)],
                             scale=list(transform.scale3d.to_tuple()))
        if name != 'PresentationCamera':
            samples[name] = sample_translation(sequence, binding)
            section = next(track for track in binding.get_tracks()
                           if isinstance(track, u.MovieScene3DTransformTrack)).get_sections()[0]
            transform_keys[name] = []
            for channel in list(section.get_all_channels())[:9]:
                keys = []
                for key in channel.get_keys():
                    frame = key.get_time()
                    require(abs(float(frame.sub_frame)) < .0001, 'Source transform keys must use whole display frames')
                    keys.append([int(frame.frame_number.value), float(key.get_value())])
                transform_keys[name].append(keys)
    require(set(names.values()) == allowed, 'Expected the four final source bindings')
    require(role(actors, 'LoopTake').get_sequence() == sequence, 'Source map does not reference the final take')
    require(world.get_world_settings().get_editor_property('default_game_mode') == u.GameModeBase.static_class(),
            'The isolated source must retain its neutral GameModeBase')
    helper_path = PROJECT/'Tools/Unreal/respawn_arena_patch.py'
    namespace = {}
    exec(compile(helper_path.read_text(encoding='utf8'), str(helper_path), 'exec'), namespace)
    query = namespace['make_support_query'](role(actors, 'ActualGum').static_mesh_component)
    origin = vec(samples['ActualReserve'][0])
    gum_hits = [query(origin+u.Vector(dx, dy, 0))
                for dx, dy in ((0, 0), (-45, 0), (45, 0), (0, -45), (0, 45))]
    hits = [hit for hit in gum_hits if hit is not None]
    require(hits, 'Actual gum support could not be sampled before authoring')
    return dict(binding_names=names, translations=samples, initial_transforms=initial,
                transform_keys=transform_keys, actual_gum_height=max(float(hit.z) for hit in hits),
                actual_gum_samples=[list(hit.to_tuple()) for hit in hits])


def inspect_background():
    """Read only real saved side-wall surfaces; retain actual material overrides."""
    report = json.loads((Path('E:/CodexWork/RespawnReplacementFinal_20261024')/'FinalAuthoring.json').read_text(encoding='utf8'))
    mapping = report['arena_patch']
    source_world = u.EditorAssetLibrary.load_asset('/Game/Maps/L_Mouth')
    require(source_world is not None, 'Saved source arena is unavailable')
    socket_class = u.load_class(None, '/Script/MessControl.MCArenaToothSocket')
    sockets = [actor for actor in u.GameplayStatics.get_all_actors_of_class(source_world, socket_class)
               if int(actor.get_editor_property('tooth_id')) == mapping['source_tooth_id']]
    require(len(sockets) == 1, 'Source reserve anchor is ambiguous')
    anchor = sockets[0].get_editor_property('preview').get_world_transform().translation
    require((anchor-vec(mapping['source_anchor'])).length() < .1,
            'Saved source arena moved since the patch; review the rigid mapping')
    rows = []
    for actor in u.GameplayStatics.get_all_actors_of_class(source_world, u.Actor):
        for component in actor.get_components_by_class(u.StaticMeshComponent):
            mesh = component.static_mesh
            if mesh is None or mesh.get_name() not in ('SM_Wall_L', 'SM_Wall_R'):
                continue
            transform = component.get_world_transform()
            q = transform.rotation
            materials = [component.get_material(index) or mesh.get_material(index)
                         for index in range(component.get_num_materials())]
            require(materials and all(material is not None and material.get_path_name().startswith('/Game/') for material in materials),
                    'Real source wall has an unsupported transient/missing material')
            rows.append(dict(source_actor=actor.get_path_name(), source_component=component.get_path_name(),
                             mesh=mesh.get_path_name(), materials=[material.get_path_name() for material in materials],
                             location=list(transform.translation.to_tuple()),
                             quaternion=[q.x, q.y, q.z, q.w], scale=list(transform.scale3d.to_tuple())))
    require({'SM_Wall_L', 'SM_Wall_R'} <= {row['mesh'].split('.')[-1] for row in rows},
            'Both real saved side walls must be found before adding the background')
    return dict(mapping=mapping, walls=rows)


def apply_background(actors, background):
    mapping = background['mapping']
    anchor = mapping['source_anchor']
    angle = math.radians(mapping['yaw_offset_degrees'])
    ca, sa, qz, qw = math.cos(angle), math.sin(angle), math.sin(angle*.5), math.cos(angle*.5)
    for row in background['walls']:
        name = 'POV_RealWall_'+hashlib.sha1(row['source_component'].encode('utf8')).hexdigest()[:10]
        matches = [actor for actor in actors.get_all_level_actors() if name in [str(tag) for tag in actor.tags]]
        require(len(matches) <= 1, 'Duplicate owned real-wall proxy')
        if matches:
            actor = matches[0]
            require(OWNER in [str(tag) for tag in actor.tags], 'An unowned wall occupies the POV role')
        else:
            actor = actors.spawn_actor_from_class(u.StaticMeshActor, u.Vector(0, 0, 0))
            actor.tags = [u.Name(OWNER), u.Name(name)]
        actor.set_actor_label(name)
        x, y, z = row['location']
        dx, dy = x-anchor[0], y-anchor[1]
        qx0, qy0, qz0, qw0 = row['quaternion']
        transform = u.Transform()
        transform.translation = u.Vector(75+ca*dx-sa*dy, 170+sa*dx+ca*dy, z)
        transform.rotation = u.Quat(qw*qx0-qz*qy0, qw*qy0+qz*qx0,
                                    qw*qz0+qz*qw0, qw*qw0-qz*qz0)
        transform.scale3d = vec(row['scale'])
        actor.set_actor_transform(transform, False, False)
        component = actor.static_mesh_component
        component.set_mobility(u.ComponentMobility.MOVABLE)
        mesh = u.load_asset(row['mesh'])
        if component.static_mesh != mesh:
            require(component.set_static_mesh(mesh), 'Could not assign the real arena wall')
        require(component.static_mesh == mesh, 'The real arena wall mesh was not retained')
        for index, material in enumerate(row['materials']):
            component.set_material(index, u.load_asset(material))
        component.set_collision_enabled(u.CollisionEnabled.NO_COLLISION)
    # The former observer set's studio plane is not an anatomical background.
    for actor in list(actors.get_all_level_actors()):
        if 'Backdrop' in [str(tag) for tag in actor.tags]:
            require(SOURCE_TAG in [str(tag) for tag in actor.tags], 'Preserve unowned scenery')
            actor.destroy_actor()


def owned_transform_channels(sequence, source, role_name):
    name = next(name for name, candidate in source['binding_names'].items() if candidate == role_name)
    bindings = [binding for binding in sequence.get_bindings() if binding.get_name() == name]
    require(len(bindings) == 1, 'Owned transform binding did not resolve: '+role_name)
    sections = [section for track in bindings[0].get_tracks() if isinstance(track, u.MovieScene3DTransformTrack)
                for section in track.get_sections()]
    require(len(sections) == 1, 'Expected one owned transform section: '+role_name)
    channels = list(sections[0].get_all_channels())
    require(len(channels) >= 9, 'Owned transform channels are incomplete')
    return bindings[0], channels[:9]


def replace_channel(channel, keys):
    for key in list(channel.get_keys()):
        channel.remove_key(key)
    for frame, value in keys:
        channel.add_key(u.FrameNumber(int(frame)), float(value), interpolation=u.MovieSceneKeyInterpolation.AUTO)


def stage_owned_motion(sequence, source, actors):
    # Reset all three tracks from the unmodified SOURCE on every invocation.
    # Nothing from the rejected late-withdrawal/+Y draft can accumulate.
    for role_name in ('ActualReserve', 'OldBody', 'NewBody'):
        _, channels = owned_transform_channels(sequence, source, role_name)
        for channel, keys in zip(channels, source['transform_keys'][role_name]):
            replace_channel(channel, keys)
    _, old_channels = owned_transform_channels(sequence, source, 'OldBody')
    replace_channel(old_channels[5], [[frame, value+180.0]
                                     for frame, value in source['transform_keys']['OldBody'][5]])
    old = role(actors, 'OldBody')
    # Use the SOURCE value rather than the prior owned actor value, too.
    source_rot = source['initial_transforms']['OldBody']['rotation']
    old.set_actor_rotation(u.Rotator(pitch=source_rot[0], yaw=source_rot[1]+180.0, roll=source_rot[2]), False)
    _, new_channels = owned_transform_channels(sequence, source, 'NewBody')
    gum = source['actual_gum_height']
    land = source['translations']['NewBody'][65][2]
    peak = max(gum+130.0, land+165.0)
    roots = [[0, gum-180], [37, gum-180], [39, gum-135], [41, gum-90],
             [44, gum-20], [48, gum+80], [52, peak],
             [57, land+(peak-land)*.72],
             [61, source['translations']['NewBody'][61][2]]]
    roots += [[frame, value] for frame, value in source['transform_keys']['NewBody'][2] if frame >= 65]
    replace_channel(new_channels[2], roots)
    replace_channel(new_channels[5], [[0, 180], [79, 180], [90, 90], [104, 0], [179, 0]])
    new = role(actors, 'NewBody')
    start = vec(source['translations']['NewBody'][0])
    start.z = gum-180.0
    new.set_actor_location(start, False, False)
    new.set_actor_rotation(u.Rotator(pitch=0, yaw=180, roll=0), False)
    staged = copy.deepcopy(source)
    for role_name in ('OldBody', 'NewBody', 'ActualReserve'):
        binding, _ = owned_transform_channels(sequence, source, role_name)
        staged['translations'][role_name] = sample_translation(sequence, binding)
    staged['motion_proposal'] = dict(actual_gum_height=gum, new_root_z_keys=roots,
        source_reserve_withdrawal_restored=True, old_yaw_add_degrees=180,
        new_yaw_keys=[[0, 180], [79, 180], [90, 90], [104, 0], [179, 0]],
        bridge_note='Frame57 Z connects the lower new apex to unchanged frame61/landing; source frame57 exceeded the new apex.')
    return staged


def gameplay_defaults():
    # These two free-orbit fields are native, non-UPROPERTY state. Read the
    # actual source instead of pretending that a Blueprint CDO can expose them.
    header = PROJECT/'Source/MessControl/Public/MCToothCharacter.h'
    text = header.read_text(encoding='utf8')
    match = re.search(r'CameraOrbitYaw\s*=\s*0\s*,\s*CameraOrbitPitch\s*=\s*(-?[\d.]+)\s*,\s*CameraOrbitDistance\s*=\s*([\d.]+)', text)
    require(match is not None, 'Native free-orbit defaults changed; review camera source')
    character_class = u.load_class(None, '/Game/Blueprints/BP_PlayerCharacter.BP_PlayerCharacter_C')
    require(character_class is not None, 'Current gameplay character Blueprint is missing')
    defaults = u.get_default_object(character_class)
    height = float(defaults.get_editor_property('camera_orbit_height'))
    fov = float(defaults.get_editor_property('follow_fov'))
    capsule = defaults.get_component_by_class(u.CapsuleComponent)
    require(capsule is not None, 'Gameplay character CDO has no native capsule')
    half_height = float(capsule.get_unscaled_capsule_half_height())
    character_scale = defaults.get_actor_scale3d()
    require(max(abs(float(axis)-1.0) for axis in character_scale.to_tuple()) < .001,
            'Gameplay Blueprint root is scaled; review the sandbox mesh/root camera conversion')
    result = dict(pitch=float(match.group(1)), distance=float(match.group(2)),
                  orbit_height=height, capsule_half_height=half_height, fov=fov,
                  actor_scale=list(character_scale.to_tuple()),
                  native_source=str(header), actual_blueprint_defaults=defaults.get_path_name())
    require(all(math.isfinite(result[key]) for key in ('pitch', 'distance', 'orbit_height', 'capsule_half_height', 'fov')),
            'Non-finite gameplay camera defaults')
    require(-85 <= result['pitch'] < 0 and 250 <= result['distance'] <= 1600
            and 30 <= height <= 250 and 45 <= fov <= 95 and half_height > 0,
            'Gameplay camera defaults need an explicit new shot design')
    return result


def make_camera_samples(source, defaults):
    old = [vec(value) for value in source['translations']['OldBody']]
    new = [vec(value) for value in source['translations']['NewBody']]
    reserve = [vec(value) for value in source['translations']['ActualReserve']]
    pivot = defaults['capsule_half_height']+defaults['orbit_height']
    # +Y camera was rejected because a row tooth occluded the death. Remain on
    # the inner tongue side (-Y), rotating only the owned display bodies so the
    # old body starts and new body finishes with its visual back to the player.
    shot_yaw = 90.0
    view = u.Rotator(pitch=defaults['pitch'], yaw=shot_yaw, roll=0.0)
    eye_offset = -view.get_forward_vector()*defaults['distance']+u.Vector(0, 0, pivot)
    start_eye, end_eye = old[0]+eye_offset, new[-1]+eye_offset
    require((end_eye-start_eye).length() < 350.0, 'Unexpected wide camera travel: review source take')
    reserve_focus = u.Vector(reserve[0].x, reserve[0].y, source['actual_gum_height']+150.0)
    samples, previous_yaw = [], shot_yaw
    for frame in range(END):
        eye = lerp(start_eye, end_eye, smooth(38, 96, frame))
        # Keep the original avatar's fall readable before directing attention
        # to its particular reserve. A fallen body's center is below its normal
        # upright camera pivot; this is deliberate death-camera presentation.
        old_height = pivot+(65.0-pivot)*smooth(5, 18, frame)
        focus = old[frame]+u.Vector(0, 0, old_height)
        focus = lerp(focus, reserve_focus, smooth(21, 38, frame))
        airborne_focus = new[frame]+u.Vector(0, 0, 75.0)
        focus = lerp(focus, airborne_focus, smooth(39, 48, frame))
        gameplay_focus = new[frame]+u.Vector(0, 0, pivot)
        focus = lerp(focus, gameplay_focus, smooth(73, 104, frame))
        rotation = u.MathLibrary.find_look_at_rotation(eye, focus)
        yaw = float(rotation.yaw)
        while yaw-previous_yaw > 180.0:
            yaw -= 360.0
        while yaw-previous_yaw < -180.0:
            yaw += 360.0
        previous_yaw = yaw
        samples.append(dict(frame=frame, eye=list(eye.to_tuple()), focus=list(focus.to_tuple()),
                            pitch=float(rotation.pitch), yaw=yaw, roll=0.0))
    require(abs(samples[0]['pitch']-defaults['pitch']) < .01
            and abs(samples[-1]['pitch']-defaults['pitch']) < .01,
            'Start/end do not match the source free-orbit pitch')
    return samples, dict(start_eye=list(start_eye.to_tuple()), end_eye=list(end_eye.to_tuple()),
                         total_eye_translation_cm=float((end_eye-start_eye).length()),
                         standing_pivot_above_skeletal_root_cm=pivot, shot_yaw=shot_yaw,
                         reserve_focus=list(reserve_focus.to_tuple()),
                         facing_evidence='Owned old yaw+180, new turns yaw180-to0 after landing; view from inner tongue -Y')


def main():
    editor = u.get_editor_subsystem(u.UnrealEditorSubsystem)
    levels = u.get_editor_subsystem(u.LevelEditorSubsystem)
    actors = u.get_editor_subsystem(u.EditorActorSubsystem)
    lib = u.EditorAssetLibrary
    require(not levels.is_in_play_in_editor() and not u.EditorLevelLibrary.get_pie_worlds(True),
            'Preserve preexisting PIE; stop it before authoring')
    require(Path(u.Paths.project_dir()).resolve() == PROJECT.resolve(), 'Wrong project')
    require(not dirty(), 'Preserve all unrelated unsaved packages; this script never uses SaveAll')
    require(hasattr(levels, 'new_level_from_template'), 'Native template-map copy API is unavailable')
    require(lib.does_asset_exist(SOURCE_MAP) and lib.does_asset_exist(SOURCE_SEQUENCE),
            'The finished replacement sandbox is required')
    source_files = [package_file(SOURCE_MAP, '.umap'), package_file(SOURCE_SEQUENCE, '.uasset'),
                    PROJECT/'Content/Maps/L_Mouth.umap']
    require(all(path.is_file() for path in source_files), 'Missing saved source package')
    source_hashes = {str(path): digest(path) for path in source_files}
    source_seq_hash = source_hashes[str(source_files[1])]
    sequence_exists, map_exists = lib.does_asset_exist(SEQUENCE), lib.does_asset_exist(MAP)
    if sequence_exists:
        owned_sequence = lib.load_asset(SEQUENCE)
        require(lib.get_metadata_tag(owned_sequence, META_OWNER) == OWNER,
                'Preserve an unowned existing POV sequence')
        require(lib.get_metadata_tag(owned_sequence, META_SOURCE) == source_seq_hash,
                'The source final sequence changed; review/recreate the owned POV copy explicitly')
        del owned_sequence
    defaults = gameplay_defaults()
    source_sequence = lib.load_asset(SOURCE_SEQUENCE)
    require(isinstance(source_sequence, u.LevelSequence), 'Source sequence has unexpected type')
    require(source_sequence.get_display_rate().numerator == FPS
            and source_sequence.get_display_rate().denominator == 1
            and source_sequence.get_playback_end() == END, 'Source timing changed')
    before_map = editor.get_editor_world().get_path_name()
    if before_map.split('.')[0] != SOURCE_MAP:
        require(levels.load_level(SOURCE_MAP), 'Cannot open source sandbox for readonly binding inspection')
    source = inspect_source(editor, actors, source_sequence)
    background = inspect_background()
    samples, camera_geometry = make_camera_samples(source, defaults)
    require(not dirty(), 'Readonly source inspection unexpectedly dirtied a package')

    run = OUT/datetime.datetime.now(datetime.timezone.utc).strftime('Author_%Y%m%dT%H%M%S%fZ')
    run.mkdir(parents=True, exist_ok=False)
    for package, extension in ((MAP, '.umap'), (SEQUENCE, '.uasset')):
        source_path = package_file(package, extension)
        if source_path.is_file():
            target = run/'BeforePackages'/source_path.relative_to(PROJECT)
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source_path, target)
            require(digest(target) == digest(source_path), 'Owned package backup failed')
    preflight = dict(source_hashes=source_hashes, source=source, real_background=background, gameplay_camera_defaults=defaults,
                     camera_geometry=camera_geometry, camera_samples=samples,
                     required_visual_review_frames=[0, 30, 45, 65, 90],
                     source_editor_map=before_map, target_map=MAP, target_sequence=SEQUENCE)
    (run/'Preflight.json').write_text(json.dumps(preflight, indent=2, allow_nan=False), encoding='utf8')

    # Switch through the native active-editor world lifecycle. Never construct
    # or save an inactive UWorld through EditorAssetLibrary.
    if map_exists:
        require(levels.load_level(MAP), 'Cannot open owned POV map')
        require(OWNER in [str(tag) for tag in editor.get_editor_world().get_world_settings().tags],
                'Preserve an unowned existing POV map')
    else:
        require(levels.new_level_from_template(MAP, SOURCE_MAP), 'Native isolated map-copy failed')
    world = editor.get_editor_world()
    require(world.get_path_name().split('.')[0] == MAP, 'Mutations must occur only in the POV copy')
    settings_actor = world.get_world_settings()
    if OWNER not in [str(tag) for tag in settings_actor.tags]:
        settings_actor.tags = list(settings_actor.tags)+[u.Name(OWNER)]
    apply_background(actors, background)
    sequence = lib.load_asset(SEQUENCE) if sequence_exists else lib.duplicate_asset(SOURCE_SEQUENCE, SEQUENCE)
    require(isinstance(sequence, u.LevelSequence), 'POV sequence duplication failed')
    lib.set_metadata_tag(sequence, META_OWNER, OWNER)
    lib.set_metadata_tag(sequence, META_SOURCE, source_seq_hash)
    staged_source = stage_owned_motion(sequence, source, actors)
    samples, camera_geometry = make_camera_samples(staged_source, defaults)
    (run/'AppliedMotionCamera.json').write_text(json.dumps(dict(
        motion=staged_source['motion_proposal'], camera_samples=samples,
        camera_geometry=camera_geometry), indent=2, allow_nan=False), encoding='utf8')
    camera = role(actors, 'PresentationCamera')
    loop = role(actors, 'LoopTake')
    # Skeleton/pose/audio tracks stay copied. The owned proposal explicitly
    # changes old yaw, new launch height and its landing turn, never the source.
    for binding in list(sequence.get_bindings()):
        if binding.get_possessed_object_class() == u.CineCameraActor.static_class():
            binding.remove()
    for track in list(sequence.get_tracks()):
        if isinstance(track, u.MovieSceneCameraCutTrack):
            sequence.remove_track(track)
    camera.set_actor_label('Respawn_PlayerPOVCamera')
    if 'PlayerPOVCamera' not in [str(tag) for tag in camera.tags]:
        camera.tags = list(camera.tags)+[u.Name('PlayerPOVCamera'), u.Name(OWNER)]
    camera.set_editor_property('auto_activate_for_player', u.AutoReceiveInput.PLAYER0)
    cc = camera.get_cine_camera_component()
    film = cc.filmback
    film.sensor_width, film.sensor_height = 36.0, 20.25
    cc.filmback = film
    cc.set_editor_property('current_focal_length', 36.0/(2.0*math.tan(math.radians(defaults['fov']*.5))))
    focus = cc.focus_settings
    focus.focus_method = u.CameraFocusMethod.DISABLE
    cc.focus_settings = focus
    # Retain final take exposure/material/light settings. Only lens/framing move.
    cc.set_editor_property('constrain_aspect_ratio', False)
    first = samples[0]
    camera.set_actor_location(vec(first['eye']), False, False)
    camera.set_actor_rotation(u.Rotator(pitch=first['pitch'], yaw=first['yaw'], roll=0), False)
    camera_binding = sequence.add_possessable(camera)
    track = camera_binding.add_track(u.MovieScene3DTransformTrack)
    section = track.add_section()
    section.set_range(0, END)
    section.set_completion_mode(u.MovieSceneCompletionMode.KEEP_STATE)
    channels = list(section.get_all_channels())
    require(len(channels) >= 9, 'POV transform channels are incomplete')
    for point in samples:
        values = (*point['eye'], point['roll'], point['pitch'], point['yaw'], 1.0, 1.0, 1.0)
        for channel, value in zip(channels, values):
            channel.add_key(u.FrameNumber(point['frame']), float(value),
                            interpolation=u.MovieSceneKeyInterpolation.LINEAR)
    cuts = sequence.add_track(u.MovieSceneCameraCutTrack)
    cut = cuts.add_section()
    cut.set_range(0, END)
    cut.set_camera_binding_id(u.MovieSceneSequenceExtensions.get_binding_id(sequence, camera_binding))
    cut.set_completion_mode(u.MovieSceneCompletionMode.KEEP_STATE)
    loop.set_sequence(sequence)
    loop.set_editor_property('replicate_playback', False)
    loop.reset_bindings()
    for binding in sequence.get_bindings():
        name = source['binding_names'].get(binding.get_name())
        if name and name != 'PresentationCamera':
            loop.set_binding(u.MovieSceneSequenceExtensions.get_binding_id(sequence, binding),
                             [role(actors, name)], False)
    # Camera binding also resolves explicitly to the actor in the copied map.
    loop.set_binding(u.MovieSceneSequenceExtensions.get_binding_id(sequence, camera_binding), [camera], False)
    playback = loop.playback_settings
    playback.auto_play = True
    playback.loop_count = u.MovieSceneSequenceLoopCount(0)
    playback.pause_at_end = True
    playback.hide_hud = False
    playback.disable_movement_input = True
    playback.disable_look_at_input = True
    loop.set_editor_property('playback_settings', playback)
    fill = role(actors, 'Fill')
    fill_position = lerp(vec(camera_geometry['start_eye']), vec(camera_geometry['end_eye']), .5)+u.Vector(0, -80, 50)
    fill.set_actor_location(fill_position, False, False)
    fill.set_actor_rotation(u.MathLibrary.find_look_at_rotation(fill_position,
                            vec(source['translations']['NewBody'][-1])+u.Vector(0, 0, 80)), False)
    light = fill.rect_light_component
    light.set_editor_property('intensity_units', u.LightUnits.LUMENS)
    light.set_editor_property('intensity', 20000.0)
    light.set_editor_property('source_width', 900.0)
    light.set_editor_property('source_height', 650.0)
    light.set_editor_property('attenuation_radius', 2600.0)
    light.set_light_color(u.LinearColor(1.0, .92, .86, 1.0))
    light.set_cast_shadows(False)
    role(actors, 'Key').rect_light_component.set_editor_property('intensity', 64000.0)
    role(actors, 'Rim').rect_light_component.set_editor_property('intensity', 40000.0)
    require(all(package.get_path_name() in (MAP, SEQUENCE) for package in dirty()),
            'Unexpected dirty package; preserve it without saving')
    require(lib.save_loaded_asset(sequence, only_if_is_dirty=False), 'POV sequence save failed')
    require(levels.save_current_level(), 'POV map save failed')
    require(not dirty(), 'Unexpected unsaved packages remain')
    require(all(digest(Path(path)) == sha for path, sha in source_hashes.items()),
            'An original sandbox source file changed')
    report = dict(status='authored_pending_runtime_and_visual_review', map=MAP, sequence=SEQUENCE,
                  report_directory=str(run), duration_seconds=END/FPS, gameplay_integration=False,
                  native_respawn_changed=False, source_packages_saved=False, source_hashes_unchanged=True,
                  network_tested=False, actual_pie_window_recorded=False,
                  local_view_route='Player0 auto-activated camera plus locally evaluated Sequencer camera cut',
                  controlled_pawn='Neutral GameModeBase default pawn; animated replacement bodies remain SkeletalMeshActors',
                  final_camera='Matches source free-orbit framing behind the new body; not a possession/input handoff',
                  gameplay_camera_defaults=defaults, camera_geometry=camera_geometry,
                  real_background=background, owned_motion=staged_source['motion_proposal'],
                  existing_fill_adjusted=dict(position=list(fill_position.to_tuple()), intensity_lumens=20000,
                                              width_cm=900, height_cm=650),
                  existing_key_lumens=64000, existing_rim_lumens=40000,
                  required_visual_review_frames=[0, 30, 45, 65, 90],
                  readability_departures=[
                      'Frames 5-18 lower aim with the fallen body instead of retaining the upright orbit pivot.',
                      'Frames 21-38 pan/tilt toward this specific reserve while eye position remains near the deceased.',
                      'Frames 38-96 translate the eye only between the old and new ordinary orbit positions.',
                      'Frames 39-73 track the visible jump center; frames 73-104 restore the upright gameplay pivot.',
                      'View remains on inner tongue -Y, yaw90. Old display yaw gains180; new turns180-to0 after landing so both endpoints show a back.',
                      'Launch is rebuilt from sampled actual gum height, not the former crown-height approximation.',
                      'Fast reserve withdrawal is restored from the SOURCE each invocation; no late-withdrawal keys accumulate.',
                      'Distances/pitch/FOV are gameplay-style framing, not a claim of the current automatic map view.',
                      'End pauses on the new third-person framing so the neutral offstage default pawn never replaces the preview view.'],
                  limitations=['No native death event, five-second respawn delay, reserve consumption or possession is driven here.',
                               'Camera collision/reveal and arbitrary player orbit starts require later production integration.',
                               'Runtime camera ownership, material appearance and occlusion still require actual PIE window review.'])
    (run/'Authoring.json').write_text(json.dumps(report, indent=2, allow_nan=False), encoding='utf8')
    (OUT/'LatestAuthoring.json').write_text(json.dumps(report, indent=2, allow_nan=False), encoding='utf8')
    u.log('RESPAWN_PLAYER_POV_AUTHORED '+str(run/'Authoring.json'))
    return report


if __name__ == '__main__':
    main()
