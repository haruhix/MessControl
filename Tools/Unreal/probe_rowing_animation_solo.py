"""A short owned solo PIE capture of the real brush-rowing animation.

Run in idle L_Mouth after native integration. Reuses the storm probe's safe
PIE/input/capture lifecycle, but owns a separate report and run directory.
11 seconds of review: one rower from the side and front three-quarter, walking
while rowing, two rowers, then release. Selects Brush and uses native held LMB.
No hand/bone/animation assignment, package saves, force
helpers, network clients or application calls outside the owned PIE world.
Cancel: builtins.mc_rowing_animation_solo.finish('Stopped by operator').
"""
import builtins
import hashlib
import json
import math
import runpy
import traceback
from pathlib import Path

import unreal as u

KEY = 'mc_rowing_animation_solo'
ROOT = Path(u.Paths.project_dir()).resolve()
OUTPUT = Path(globals().get('MC_ROWING_OUTPUT',
    Path(u.Paths.project_saved_dir()) / 'Checks/RowingAnimationSolo')).resolve()
support = runpy.run_path(str(ROOT / 'Tools/Unreal/probe_cataclysm_storm_solo.py'),
    run_name='rowing_animation_support', init_globals={
        'MC_STORM_FOCUS': 'rowing', 'MC_STORM_OUTPUT': OUTPUT,
        'MC_STORM_CAPTURE_HZ': globals().get('MC_ROWING_CAPTURE_HZ', 12)})
Parent, prop, vec, valid, path, enum_name, length = (
    support[name] for name in ('CataclysmStormSolo', 'prop', 'vec', 'valid', 'path', 'enum_name', 'length'))
LifecycleParent = support['Parent']

MOUTH_SHAPES = ('Mouth_A', 'Mouth_E', 'Mouth_O', 'Mouth_MBP', 'Mouth_FV', 'Mouth_I',
                'Mouth_U', 'Mouth_L', 'Mouth_TH', 'Mouth_CH', 'Mouth_S', 'Mouth_D',
                'Mouth_Smile', 'Mouth_Frown', 'Mouth_Angry', 'Mouth_Pain',
                'Mouth_Surprise', 'Mouth_Effort')
READ_MORPHS = MOUTH_SHAPES + ('Eyes_Blink', 'BlinkCancel_Mouth_Effort')


def quat_values(rotation):
    return [float(getattr(rotation, axis)) for axis in ('x', 'y', 'z', 'w')]


def quat_angle(a, b):
    denominator = math.sqrt(sum(v*v for v in a) * sum(v*v for v in b))
    if denominator < 1.e-10: return None
    return math.degrees(2. * math.acos(min(1., abs(sum(x*y for x, y in zip(a, b))) / denominator)))


def vector_angle(a, b):
    denominator = math.sqrt(sum(v*v for v in a) * sum(v*v for v in b))
    if denominator < 1.e-10: return None
    return math.degrees(math.acos(max(-1., min(1., sum(x*y for x, y in zip(a, b)) / denominator))))


def smooth_step(low, high, value):
    t = max(0., min(1., (value-low)/(high-low)))
    return t*t*(3.-2.*t)


class RowingAnimationSolo(Parent):
    def __init__(self):
        super().__init__()
        self.review_view = 'side'
        self.pose_read_error = None
        self.input_yaw = None
        self.bot_input_at = 0.
        self.walk_before = None
        self.walk_finished = False
        self.walk_stopped_at = None
        self.walk_sign = None
        self.opposed = False
        self.torque_aim = None
        self.torque_yaw = None
        self.torque_sign = None
        self.presentation_previous = {}
        self.presentation_slots = {}
        self.presentation_read_limits = {
            'voice': 'Voice, VoiceViseme and SpeechAmount() are native non-reflected members; their actual values cannot be read through the Python wrapper. Speech morph weights are recorded separately, never substituted for the voice envelope.',
            'derived_alpha': 'EmoteAlpha(), BodyAlpha() and PainAlpha() are native non-reflected getters. The labelled derived values use only reflected state, library entries, animation length and the checked current source formula in this standalone world.',
            'grip_targets': 'GetRowingHandTargets() is native non-reflected. Expected right wrist is reconstructed from the actual brush world transform and the character Brush/BrushPivot relative attachment transforms. The labelled left target is 20 cm along the actual brush X axis, matching the checked source; it is not a native getter result.',
            'morph_weights': 'USkeletalMeshComponent.GetMorphTarget() reads the current MorphTargetCurves map, returning zero when no curve is present. This does not prove that a zero-valued morph asset exists or has zero final GPU deformation.',
        }
        self.report.update(mode='storm', focus='rowing', purpose='rowing_animation',
            scope='One owned standalone authority PIE, real F3 storm raft, selected Brush and native held LMB rowing',
            clip_labels={'storm_brush_rowing': 'Гребля щёткой: сбоку, спереди, вдвоём и отпускание',
                         'rowing_point_torque': 'Встречные гребки: вращение и смена направления'},
            requested_capture_hz=self.capture_hz,
            limitations=[
                'The local pawn is placed on an actual walkable drifting raft; the second nonspectator authority AI pawn is a disclosed position fixture, not a network client.',
                'Native F3 creates the full Cataclysm. Its tick is paused before any log spawns solely for pose review; water, storm, raft, movement, animation and game clocks keep advancing. The unrelated single-day scheduler is paused.',
                'PIE-only flood timing is 3s fill / 26s hold / 3s drain. The probe stops within 40 game seconds after F3 and 240 wall seconds.',
                'Both rowers select Brush through Inventory.ServerSelect and use SetPrimaryInputHeld, the ordinary LMB path. The authority AI is locally controlled and its own pawn ticker sends the native aim heartbeat. No scripted direction RPC, dedicated SetRowingInputHeld, raft force, rowing-state, hand, bone or animation assignment is used.',
                'The local rower uses the actual PlayerCameraManager viewing direction, including the side/front review CameraActor. Its controller rotation is not used as a substitute. The AI controller aim and normal input RPC follow the same actual viewing direction.',
                'A brief genuine AddMovementInput on the deck checks that walking remains possible while LMB is held; no position or velocity assignment is made during this movement check.',
                'Side and front three-quarter cameras track the real actors in the mouth lighting; these are temporary review cameras rather than the normal gameplay camera.',
                'Bone/socket and brush transform samples are read-only rendered-pose evidence when available. They do not automatically establish anatomical quality or artistic acceptance.',
                'SHOWUI PNGs are genuine requested engine frames at up to 12Hz. Final PNG hashes and camera-target gates are required before video export; motion quality needs visual review.',
                'This focused capture does not repeat contact/drowning checks, profile the GPU, complete the event or test network clients.'
            ])
        self.report['source_sha256']['Tools/Unreal/probe_rowing_animation_solo.py'] = hashlib.sha256(
            (ROOT / 'Tools/Unreal/probe_rowing_animation_solo.py').read_bytes()).hexdigest()
        for source in ('Source/MessControl/Private/MCRowingStroke.h', 'Source/MessControl/Public/MCCataclysmRaft.h',
                       'Source/MessControl/Public/MCToothCharacter.h', 'Source/MessControl/Private/MCGameplayHUD.cpp',
                       'Source/MessControl/Private/MCExpressionComponent.cpp', 'Source/MessControl/Public/MCExpressionComponent.h',
                       'Source/MessControl/Private/MCGazeComponent.cpp', 'Source/MessControl/Public/MCGazeComponent.h',
                       'Source/MessControl/Private/MCToothStatusComponent.cpp'):
            self.report['source_sha256'][source] = hashlib.sha256((ROOT/source).read_bytes()).hexdigest()

    def write(self):
        self.summarize_presentation()
        self.output.mkdir(parents=True, exist_ok=True)
        data = json.dumps(self.report, ensure_ascii=False, indent=2, allow_nan=False)
        (self.output / 'Report.json').write_text(data, encoding='utf-8')
        OUTPUT.mkdir(parents=True, exist_ok=True)
        (OUTPUT / 'Report_rowing_animation.json').write_text(data, encoding='utf-8')
        (OUTPUT / 'Report_storm.json').write_text(data, encoding='utf-8')

    def aim_rowing(self):
        if not valid(self.pc): return
        rotation = prop(self.pc, 'player_camera_manager').get_camera_rotation()
        self.input_yaw = float(rotation.yaw)
        if valid(self.bot):
            if self.opposed: rotation = u.Rotator(yaw=rotation.yaw+180.)
            self.bot_controllers[0].set_control_rotation(rotation)

    def rowing(self, pawn, held):
        self.aim_rowing()
        if held:
            prop(pawn, 'inventory').server_select(u.MCToolSlot.BRUSH)
            self.require(enum_name(prop(prop(pawn, 'inventory'), 'selected')) == 'BRUSH',
                         'Native inventory did not select Brush before LMB')
        pawn.call_method('SetPrimaryInputHeld', args=(bool(held),))
        if pawn == self.hero: self.primary = bool(held)
        self.report.setdefault('input_actions', []).append(dict(
            time=float(u.GameplayStatics.get_time_seconds(self.world)), pawn=path(pawn),
            action='SetPrimaryInputHeld', held=bool(held), selected_tool=enum_name(prop(prop(pawn, 'inventory'), 'selected')),
            actual_view_yaw_degrees=self.input_yaw))

    def camera_for_stage(self):
        foot = self.hero.get_actor_location() - u.Vector(0, 0, self.hero.capsule_component.get_scaled_capsule_half_height())
        target = foot + u.Vector(0, 0, 50.)
        if self.review_view == 'torque':
            target=self.raft.get_actor_location()+u.Vector(0,0,65.)
        elif self.review_view == 'two_front':
            target = (self.hero.get_actor_location() + self.bot.get_actor_location()) * .5 + u.Vector(0, 0, 5.)
        key = (self.phase, self.review_view)
        if getattr(self, 'review_camera_basis_key', None) != key:
            # The actual viewing camera also selects native rowing aim. Keep
            # its world bearing fixed during each stage: following hero facing
            # would feed the resulting stroke turn back into the next aim.
            right = self.raft.get_actor_right_vector()
            front = self.hero.get_actor_forward_vector()
            if self.review_view == 'torque':
                offset = -self.torque_aim * 460. + u.Vector(0, 0, 170.)
            elif self.review_view == 'side':
                offset = -right * 520. + front * 110. + u.Vector(0, 0, 165.)
            elif self.review_view == 'two_front':
                offset = front * 480. - right * 330. + u.Vector(0, 0, 195.)
            else:
                local = prop(self.raft, 'deck').get_world_transform().inverse_transform_location(foot)
                side = 1. if local.y > 0 else -1.
                offset = front * 380. + right * (side * 400.) + u.Vector(0, 0, 180.)
            self.review_camera_basis_key = key
            self.review_camera_offset = offset
            self.report.setdefault('review_camera_stage_offsets', []).append(dict(
                phase=self.phase, review_view=self.review_view, world_offset_cm=vec(offset),
                basis='Latched once per stage; translation follows the target; no hero-facing or deck-side feedback'))
        location = target + self.review_camera_offset
        self.frame_camera(location, target)
        prop(self.camera, 'camera_component').set_editor_property('FieldOfView', 60.)

    def camera_evidence(self):
        evidence = super().camera_evidence()
        if evidence:
            evidence.update(review_view=self.review_view, field_of_view=60.,
                actual_rowing_view_yaw_degrees=float(prop(self.pc, 'player_camera_manager').get_camera_rotation().yaw))
        return evidence

    def sample(self, now):
        super().sample(now)
        entry = self.report['samples'][-1]
        entry['review_view'] = self.review_view
        scene_audio = prop(self.pc, 'scene_audio')
        entry['scene_audio_context'] = str(prop(scene_audio, 'scene_context')) if valid(scene_audio) else None
        yaw = float(prop(self.pc, 'player_camera_manager').get_camera_rotation().yaw)
        expected = [math.cos(math.radians(yaw)), math.sin(math.radians(yaw))]
        entry['actual_rowing_view_yaw_degrees'] = yaw
        entry['actual_view_forward_xy'] = expected
        entry['controller_yaw_degrees'] = float(self.pc.get_control_rotation().yaw)
        directions = prop(self.raft, 'rowing_directions')
        entry['rowing_directions'] = [[float(d.x), float(d.y)] for d in directions]
        entry['raft_yaw_degrees'] = float(self.raft.get_actor_rotation().yaw)
        entry['stroke_started_at'] = [float(t) for t in prop(self.raft, 'rowing_started_at')]
        entry['rower_direction_alignment'] = [dict(pawn=player, slot=index,
            direction_xy=entry['rowing_directions'][index],
            view_dot=float(directions[index].x * expected[0] + directions[index].y * expected[1]))
            for index, player in enumerate(entry['rowing_players']) if player and index < len(directions)]
        entry['rower_facing'] = []
        for pawn in (self.hero, self.bot):
            if not valid(pawn) or not pawn.is_rowing(): continue
            players = list(prop(self.raft, 'rowing_players'))
            slot = next((i for i, player in enumerate(players) if player == pawn), None)
            if slot is None: continue
            forward = pawn.get_actor_forward_vector()
            direction = directions[slot]
            entry['rower_facing'].append(dict(pawn=path(pawn), slot=slot,
                actor_forward_xy=[float(forward.x), float(forward.y)],
                stroke_direction_xy=[float(direction.x), float(direction.y)],
                facing_dot=float(forward.x*direction.x+forward.y*direction.y)))
        entry['selected_tool'] = enum_name(prop(prop(self.hero, 'inventory'), 'selected'))
        brushes = []
        for component in self.raft.get_components_by_class(u.StaticMeshComponent):
            if 'RowingBrush' not in component.get_name():
                continue
            brushes.append(dict(name=component.get_name(), visible=bool(component.is_visible()),
                location_cm=vec(component.get_world_location()),
                raft_local_cm=vec(prop(self.raft, 'deck').get_world_transform().inverse_transform_location(component.get_world_location())),
                rotation_degrees=[float(component.get_world_rotation().pitch), float(component.get_world_rotation().yaw), float(component.get_world_rotation().roll)],
                mesh=path(prop(component, 'static_mesh'))))
        entry['rendered_brushes'] = brushes
        self.record_presentation(now, entry)
        if self.pose_read_error is not None:
            return
        try:
            mesh = prop(self.hero,'mesh')
            appearance = prop(self.hero, 'appearance')
            mapping = {str(k): str(v) for k, v in prop(appearance, 'bone_map').items()} if valid(appearance) else {}
            pose = mesh.get_world_transform()
            rendered = {}
            for role in ('body', 'hand_l', 'hand_r', 'forearm_l', 'forearm_r'):
                bone = mapping.get(role, role)
                if mesh.get_bone_index(bone) >= 0:
                    location = mesh.get_socket_location(bone)
                    rendered[role] = dict(bone=bone, world_cm=vec(location), mesh_local_cm=vec(pose.inverse_transform_location(location)))
            entry['rendered_bones'] = rendered
        except Exception as error:
            # A missing Python read wrapper must not falsely fail a native
            # animation. Visual evidence still remains mandatory for acceptance.
            self.pose_read_error = str(error)
            self.report['pose_read_limit'] = self.pose_read_error

    def optional_presentation_read(self, key, reader):
        if key in self.presentation_read_limits: return None
        try:
            return reader()
        except Exception as error:
            # Diagnostic wrappers are optional. A failed cosmetic read must
            # never change gameplay or manufacture a failed native check.
            self.presentation_read_limits[key] = str(error)
            return None

    def read_expression(self, pawn, now):
        expression, status = prop(pawn, 'expression'), prop(pawn, 'status')
        entry = dict(current_emotion=enum_name(prop(expression, 'current_emotion')),
            suction=float(prop(expression, 'food_suction_reaction')),
            social_disgust=float(prop(expression, 'social_disgust')),
            stink_reaction=float(prop(expression, 'stink_reaction')),
            stink_gesture_alpha=float(prop(expression, 'stink_gesture_alpha')),
            spray_reaction=float(prop(expression, 'spray_reaction')),
            voice=None, voice_viseme=None,
            emote=None, emote_alpha_derived=None, body_alpha_derived=None)
        status_state = prop(status, 'state')
        damage_at = float(prop(status_state, 'damage_at'))
        age = now-damage_at
        entry['pain'] = dict(damage_at=damage_at, age_seconds=age,
            alpha_derived=(1.-smooth_step(.35, 1.1, age)) if 0. <= age < 1.1 else 0.)
        emote = self.optional_presentation_read('expression.emote_state',
            lambda: self.read_emote(expression, now))
        if emote is not None:
            entry['emote'] = emote
            entry['emote_alpha_derived'] = emote['alpha_derived']
            entry['body_alpha_derived'] = emote['body_alpha_derived']
        return entry

    def read_emote(self, expression, now):
        # Native State is not exported to the current Python wrapper. Keep this
        # optional read isolated so its absence cannot discard the visible face.
        state = prop(expression, 'state')
        result = dict(id=str(prop(state, 'id')), started_at=float(prop(state, 'started_at')),
                      stopped_at=float(prop(state, 'stopped_at')), serial=int(prop(state, 'serial')))
        library = prop(expression, 'library')
        emote = next((e for e in prop(library, 'entries')
                      if str(prop(e, 'id')) == result['id']), None) if valid(library) else None
        emote_alpha = body_alpha = 0.
        if emote is not None:
            animation = prop(emote, 'animation')
            hold, loop = bool(prop(emote, 'hold_final_pose')), bool(prop(emote, 'looping'))
            duration = (float(animation.get_play_length()) if valid(animation) and not hold and not loop
                        else max(.5, min(10., float(prop(emote, 'duration')))))
            started, stopped = result['started_at'], result['stopped_at']
            end = min(stopped, started+duration) if stopped >= 0. else started+duration
            emote_alpha = smooth_step(0., 1., min(max(0., min(1., (now-started)/.18)),
                                               max(0., min(1., (end+.22-now)/.22))))
            body_alpha = emote_alpha if valid(animation) and not bool(prop(emote, 'face_only')) else 0.
            result.update(animation=path(animation), face_only=bool(prop(emote, 'face_only')),
                          hold_final_pose=hold, looping=loop, duration_seconds=duration)
        result['alpha_derived'] = emote_alpha
        result['body_alpha_derived'] = body_alpha
        return result

    def read_gaze(self, pawn):
        gaze = prop(pawn, 'gaze')
        target = prop(gaze, 'target')
        actor = prop(target, 'actor')
        point = prop(target, 'point')
        offset = self.optional_presentation_read('gaze.offset', lambda: prop(target, 'offset'))
        derived_point = (vec(actor.get_actor_transform().transform_location(offset))
                         if valid(actor) and offset is not None else vec(point) if not valid(actor) else None)
        return dict(interest=enum_name(prop(target, 'interest')), actor=path(actor),
            saved_point_cm=vec(point), offset_cm=vec(offset) if offset is not None else None,
            target_world_cm_derived=derived_point,
            changed_at=self.optional_presentation_read('gaze.changed_at', lambda: float(prop(target, 'changed_at'))),
            serial=self.optional_presentation_read('gaze.serial', lambda: int(prop(target, 'serial'))),
            blink=float(prop(gaze, 'blink')),
            blink_started_at=self.optional_presentation_read('gaze.blink_started_at', lambda: float(prop(gaze, 'blink_started_at'))),
            pupil_scale=float(prop(gaze, 'pupil_scale')),
            left_angles_degrees=[float(prop(gaze, 'left_angles').x), float(prop(gaze, 'left_angles').y)],
            right_angles_degrees=[float(prop(gaze, 'right_angles').x), float(prop(gaze, 'right_angles').y)])

    def read_rendered_pose(self, pawn):
        mesh, appearance = prop(pawn, 'mesh'), prop(pawn, 'appearance')
        mapping = {str(k): str(v) for k, v in prop(appearance, 'bone_map').items()} if valid(appearance) else {}
        rendered = {}
        for role in ('body', 'gaze_head', 'hand_l', 'hand_r', 'forearm_l', 'forearm_r', 'eye_l', 'eye_r'):
            bone = mapping.get(role, role)
            if mesh.get_bone_index(bone) < 0: continue
            world = mesh.get_socket_transform(bone, u.RelativeTransformSpace.RTS_WORLD)
            rendered[role] = dict(bone=bone, parent_bone=str(mesh.get_parent_bone(bone)),
                world_cm=vec(world.translation), world_quaternion_xyzw=quat_values(world.rotation))
        return rendered

    def read_grip(self, pawn, slot, bones, row_active):
        if slot is None: return None
        name = 'RaftLeftRowingBrush' if slot == 0 else 'RaftRightRowingBrush'
        brush = next((c for c in self.raft.get_components_by_class(u.StaticMeshComponent)
                      if c.get_name() == name), None)
        if not valid(brush): return None
        own_brush, pivot = prop(pawn, 'brush'), prop(pawn, 'brush_pivot')
        # FTransform A*B applies A and then B, exactly as the native grip getter.
        mount = u.MathLibrary.compose_transforms(own_brush.get_relative_transform(), pivot.get_relative_transform())
        world = brush.get_world_transform()
        right = u.MathLibrary.compose_transforms(u.MathLibrary.invert_transform(mount), world)
        shaft = brush.get_forward_vector()
        left = right.translation + shaft*20.
        result = dict(slot=slot, slot_is_active=bool(row_active), brush=path(brush),
            visible=bool(brush.is_visible()), brush_world_cm=vec(world.translation),
            brush_world_quaternion_xyzw=quat_values(world.rotation), shaft_axis_world=vec(shaft),
            right_target_world_cm_reconstructed=vec(right.translation),
            right_target_world_quaternion_xyzw_reconstructed=quat_values(right.rotation),
            left_target_world_cm_reconstructed=vec(left))
        for role, target in (('hand_r', right.translation), ('hand_l', left)):
            if role not in bones: continue
            hand = bones[role]
            point = hand['world_cm']
            delta = [a-b for a, b in zip(point, vec(right.translation))]
            along = sum(a*b for a, b in zip(delta, vec(shaft)))
            perpendicular = math.sqrt(max(0., sum(a*a for a in delta)-along*along))
            result[role] = dict(target_error_cm=math.dist(point, vec(target)),
                shaft_along_cm=along, shaft_perpendicular_cm=perpendicular)
        if 'hand_r' in bones:
            result['hand_r']['target_rotation_error_degrees'] = quat_angle(
                bones['hand_r']['world_quaternion_xyzw'], quat_values(right.rotation))
        return result

    def record_presentation(self, now, entry):
        # Observation only: no getter below changes expression, gaze, poses,
        # attachment transforms, tools, forces or physics blend state.
        rowers = list(prop(self.raft, 'rowing_players'))
        data = []
        for pawn in (self.hero, self.bot):
            if not valid(pawn): continue
            pawn_path = path(pawn)
            slot = next((i for i, candidate in enumerate(rowers) if candidate == pawn), None)
            if slot is not None: self.presentation_slots[pawn_path] = slot
            remembered_slot = slot if slot is not None else self.presentation_slots.get(pawn_path)
            result = dict(pawn=pawn_path, rowing=bool(pawn.is_rowing()), active_slot=slot,
                native_direction_xy=(entry['rowing_directions'][slot] if slot is not None else None))
            result['expression'] = self.optional_presentation_read('expression', lambda: self.read_expression(pawn, now))
            result['gaze'] = self.optional_presentation_read('gaze', lambda: self.read_gaze(pawn))
            mesh = prop(pawn, 'mesh')
            result['morph_weights'] = self.optional_presentation_read('morph_getter',
                lambda: {name: float(mesh.get_morph_target(name)) for name in READ_MORPHS})
            result['rendered_bones'] = self.optional_presentation_read('diagnostic_bones', lambda: self.read_rendered_pose(pawn)) or {}
            result['grip'] = self.optional_presentation_read('diagnostic_grip',
                lambda: self.read_grip(pawn, remembered_slot, result['rendered_bones'], slot is not None))
            own = prop(pawn, 'brush')
            result['ordinary_wrist_brush'] = dict(visible=bool(own.is_visible()),
                world_cm=vec(own.get_world_location()), world_quaternion_xyzw=quat_values(own.get_world_transform().rotation))
            previous = self.presentation_previous.get(pawn_path)
            if previous is not None:
                dt = now-previous['game_seconds']
                motion = dict(dt_seconds=dt, previous_phase=previous['phase'], current_phase=self.phase)
                if result['native_direction_xy'] is not None and previous.get('native_direction_xy') is not None:
                    motion['native_direction_delta_degrees'] = vector_angle(result['native_direction_xy'], previous['native_direction_xy'])
                if result['grip'] and previous.get('grip'):
                    motion['brush_rotation_delta_degrees'] = quat_angle(result['grip']['brush_world_quaternion_xyzw'],
                        previous['grip']['brush_world_quaternion_xyzw'])
                    motion['brush_translation_delta_cm'] = math.dist(result['grip']['brush_world_cm'], previous['grip']['brush_world_cm'])
                for role in ('hand_l', 'hand_r'):
                    bone, old_bone = result['rendered_bones'].get(role), previous.get('rendered_bones', {}).get(role)
                    if bone and old_bone:
                        angle = quat_angle(bone['world_quaternion_xyzw'], old_bone['world_quaternion_xyzw'])
                        motion[role] = dict(rotation_delta_degrees=angle,
                            angular_speed_degrees_per_second=angle/dt if angle is not None and dt > 0. else None,
                            translation_delta_cm=math.dist(bone['world_cm'], old_bone['world_cm']))
                result['temporal_change'] = motion
            self.presentation_previous[pawn_path] = dict(result, game_seconds=now, phase=self.phase)
            data.append(result)
        entry['presentation_diagnostics'] = data

    def summarize_presentation(self):
        if not hasattr(self, 'presentation_read_limits'): return
        self.report['presentation_read_limits'] = dict(self.presentation_read_limits)
        summaries = {}
        for sample in self.report.get('samples', []):
            for actor in sample.get('presentation_diagnostics', []):
                key = sample['phase'] + ' | ' + actor['pawn']
                summary = summaries.setdefault(key, dict(phase=sample['phase'], pawn=actor['pawn'], samples=0,
                    emotion_counts={}, measurements={}, morph_weight_ranges={}))
                summary['samples'] += 1
                def record(name, value, ranges):
                    if value is None: return
                    bounds = ranges.setdefault(name, dict(min=float(value), max=float(value), samples=0))
                    bounds['min'] = min(bounds['min'], float(value))
                    bounds['max'] = max(bounds['max'], float(value))
                    bounds['samples'] += 1
                expression = actor.get('expression')
                if expression:
                    emotion = expression['current_emotion']
                    summary['emotion_counts'][emotion] = summary['emotion_counts'].get(emotion, 0)+1
                    for field in ('suction', 'social_disgust', 'stink_reaction', 'stink_gesture_alpha',
                                  'spray_reaction', 'emote_alpha_derived', 'body_alpha_derived'):
                        record(field, expression[field], summary['measurements'])
                    record('pain_alpha_derived', expression['pain']['alpha_derived'], summary['measurements'])
                for name, value in (actor.get('morph_weights') or {}).items():
                    record(name, value, summary['morph_weight_ranges'])
                for side in ('hand_l', 'hand_r'):
                    for field, value in (actor.get('grip') or {}).get(side, {}).items():
                        record('grip_'+side+'_'+field, value, summary['measurements'])
                    for field, value in actor.get('temporal_change', {}).get(side, {}).items():
                        record('temporal_'+side+'_'+field, value, summary['measurements'])
                for field in ('native_direction_delta_degrees', 'brush_rotation_delta_degrees', 'brush_translation_delta_cm'):
                    record('temporal_'+field, actor.get('temporal_change', {}).get(field), summary['measurements'])
        self.report['presentation_measurements'] = dict(
            basis='Read-only live reflected state, morph curves and rendered world bone/component transforms; ranges include entry/exit/camera changes and are measurements, not new pass/fail checks.',
            summaries=list(summaries.values()))

    def require_active(self, stage, count):
        entries = self.stage_samples(stage)
        stable = [e for e in entries if e['hero_rowing'] and e['rowing_count'] == count
                  and e['movement_base'] == path(prop(self.raft, 'deck'))
                  and (count == 1 or e['bot_rowing'])]
        self.require(len(stable) >= 12, 'Native rowing did not remain active on the deck in ' + stage)
        self.require(any(sum(1 for b in e['rendered_brushes'] if b['visible']) == count for e in stable),
                     'Actual rowing brush presentation count does not match ' + stage)
        self.report['checks'][stage + '_native_input_and_visible_brushes'] = True
        aim_samples = [e for e in stable if e['game_seconds'] >= stable[0]['game_seconds'] + .6]
        self.require(aim_samples and all(e['scene_audio_context'] == 'CataclysmRafts' for e in aim_samples),
                     'Local controller did not select CataclysmRafts audio context after settling in ' + stage)
        self.report['checks'][stage + '_automatic_scene_audio_context'] = True
        aligned = [e for e in aim_samples if len(e['rower_direction_alignment']) == count
                   and all(row['view_dot'] >= .95 for row in e['rower_direction_alignment'])]
        self.require(aim_samples and len(aligned) >= len(aim_samples) * .75,
                     'Own native rowing directions did not follow the actual viewing camera in ' + stage)
        self.report['checks'][stage + '_own_directions_follow_actual_view'] = True
        facing_samples = [e for e in stable if e['game_seconds'] >= stable[0]['game_seconds'] + 1.0
                          and len(e.get('rower_facing', [])) == count]
        self.require(facing_samples and sum(all(row['facing_dot'] >= .85 for row in e['rower_facing'])
                     for e in facing_samples) >= len(facing_samples)*.75,
                     'Rowers faced away from their own stroke after turning in ' + stage)
        self.report['checks'][stage + '_characters_face_own_stroke'] = True
        grips = [actor['grip']['hand_l']['target_error_cm']
                 for entry in facing_samples for actor in entry.get('presentation_diagnostics', [])
                 if actor.get('rowing') and actor.get('grip') and actor['grip'].get('hand_l')]
        self.require(grips and sum(error<3. for error in grips)>=len(grips)*.8,
                     'Support palm did not stay on the actual reachable brush shaft in '+stage)
        self.report.setdefault('settled_support_grip_cm', {})[stage] = dict(
            maximum=max(grips), samples=len(grips), fraction_within_3cm=sum(error<3. for error in grips)/len(grips))
        self.report['checks'][stage + '_support_palm_holds_actual_shaft'] = True
        if stage=='solo_side':
            settled=[e for e in stable if e['game_seconds']>=stable[0]['game_seconds']+.6
                     and all(h in e.get('rendered_bones',{}) for h in ('hand_l','hand_r'))]
            if settled:
                distances=[math.dist(e['rendered_bones']['hand_l']['world_cm'],e['rendered_bones']['hand_r']['world_cm']) for e in settled]
                self.require(sum(d<35. for d in distances)>=len(distances)*.75,
                             'Rendered wrists stayed apart instead of holding the same brush handle')
                self.report['rendered_hand_separation_cm']=dict(min=min(distances),max=max(distances),samples=len(distances))
                self.report['checks']['rendered_wrists_hold_same_brush_handle']=True
        if count == 2:
            self.require(any(e['rowing_vector_magnitude'] >= 1.8 for e in aligned),
                         'Two genuine LMB camera-aligned row inputs did not sum')
            self.report['checks']['two_camera_aligned_native_row_inputs_sum'] = True

    def update_episode(self, now, elapsed):
        if self.phase == 'wait_hold':
            self.require(elapsed < 24., 'Natural F3 raft/holding branch did not arrive')
            if enum_name(prop(self.event, 'phase')) != 'FLOODING': return
            self.liquid = prop(self.event, 'liquid')
            if not valid(self.liquid) or enum_name(self.liquid.get_phase()) != 'HOLDING': return
            rafts = [r for r in prop(self.event, 'rafts') if valid(r)]
            self.require(len(rafts) == 2 and self.liquid.is_cataclysm_storm(), 'Expected native storm and two real rafts')
            self.require(not any(valid(d) for d in prop(self.event, 'floating_debris')),
                         'Pose-review isolation must precede any incoming log; existing hazards are preserved')
            self.event_tick = self.event.is_actor_tick_enabled()
            self.event.set_actor_tick_enabled(False)
            self.report['rowing_scheduler_isolation'] = dict(actor=path(self.event), original_tick=self.event_tick,
                paused_at=now, reason='Pose review before native incoming debris; water, raft, animation and CMC remain active')
            center, _ = self.tongue.get_actor_bounds(False)
            self.raft = min(rafts, key=lambda r: length(r.get_actor_location() - center))
            self.place_crew()
            self.aim_rowing()
            self.report['checks']['real_f3_64_starts_storm_rafts'] = True
            self.stage('settle_review')
        elif self.phase == 'settle_review' and elapsed >= .7:
            self.require(self.base(self.hero) == prop(self.raft, 'deck'), 'Local pawn did not naturally settle on the deck')
            self.rowing(self.hero, True)
            self.review_view = 'side'
            self.begin_clip('storm_brush_rowing', 11.)
            self.stage('solo_side')
        elif self.phase == 'solo_side' and elapsed >= 3.5:
            self.require_active('solo_side', 1)
            self.review_view = 'front_three_quarter'
            self.stage('solo_front')
        elif self.phase == 'solo_front' and elapsed >= 3.5:
            self.require_active('solo_front', 1)
            self.require(self.walk_finished, 'Native walking-while-rowing checkpoint did not finish')
            self.make_bot()
            foot = self.deck_point(45., 32.)
            self.place_pawn(self.bot, foot, self.hero.get_actor_rotation().yaw)
            self.stage('settle_second')
        elif self.phase == 'settle_second' and elapsed >= .35:
            self.require(self.base(self.bot) == prop(self.raft, 'deck'), 'Second rower did not naturally settle on deck')
            self.rowing(self.bot, True)
            self.review_view = 'two_front'
            self.stage('two_rowers')
        elif self.phase == 'two_rowers' and elapsed >= 2.5:
            self.require_active('two_rowers', 2)
            self.rowing(self.hero, False)
            self.rowing(self.bot, False)
            self.stage('released')
        elif self.phase == 'released' and elapsed >= 1.:
            self.require(not self.hero.is_rowing() and not self.bot.is_rowing()
                and int(prop(self.raft, 'rowing_count')) == 0, 'Releasing held input did not stop both rowers')
            self.require(all(not component.is_visible() for component in self.raft.get_components_by_class(u.StaticMeshComponent)
                if 'RowingBrush' in component.get_name()), 'Rowing brush presentation remained after release')
            self.report['checks']['release_stops_native_rowing_and_hides_raft_brushes'] = True
            self.clip = None
            self.opposed=True
            self.review_view='torque'
            across=self.raft.get_actor_right_vector()
            span=max(.001,math.hypot(across.x,across.y))
            self.torque_aim=u.Vector(across.x/span,across.y/span,0)
            self.place_pawn(self.hero,self.deck_point(-80.,-25.),self.hero.get_actor_rotation().yaw)
            self.place_pawn(self.bot,self.deck_point(80.,25.),self.bot.get_actor_rotation().yaw)
            self.stage('settle_torque')
        elif self.phase == 'settle_torque' and elapsed >= .65:
            self.require(self.base(self.hero)==prop(self.raft,'deck') and self.base(self.bot)==prop(self.raft,'deck'), 'Torque crew did not settle on real deck')
            self.rowing(self.hero,True)
            self.rowing(self.bot,True)
            arm=self.hero.get_actor_location()-self.raft.get_actor_location()
            self.torque_sign=1. if arm.x*self.torque_aim.y-arm.y*self.torque_aim.x>0 else -1.
            self.torque_yaw=float(self.raft.get_actor_rotation().yaw)
            self.report['torque_fixture']=dict(initial_yaw=self.torque_yaw,expected_sign=self.torque_sign,
                aim_xy=vec(self.torque_aim),hero_cm=vec(self.hero.get_actor_location()),bot_cm=vec(self.bot.get_actor_location()),
                native_input='Selected Brush + held LMB on both pawns; opposite controller aim, no force or yaw assignment')
            self.begin_clip('rowing_point_torque',7.1)
            self.stage('couple_turn')
        elif self.phase == 'couple_turn' and elapsed >= 2.4:
            self.require_couple('couple_turn',self.torque_sign)
            self.torque_yaw=float(self.raft.get_actor_rotation().yaw)
            self.torque_aim=self.torque_aim*-1.
            self.stage('couple_reverse')
        elif self.phase == 'couple_reverse' and elapsed >= 3.6:
            self.require_couple('couple_reverse',-self.torque_sign)
            self.rowing(self.hero,False)
            self.rowing(self.bot,False)
            self.stage('torque_release')
        elif self.phase == 'torque_release' and elapsed >= .7:
            self.require(not self.hero.is_rowing() and not self.bot.is_rowing(), 'Torque LMB release did not stop the rowers')
            self.clip=None
            self.stage('review_flush')
        elif self.phase == 'review_flush' and elapsed >= .7:
            for entry in self.report['frames']:
                frame = Path(entry['file'])
                self.require(frame.is_file(), 'Engine frame missing: ' + str(frame))
                data = frame.read_bytes()
                self.require(data[:8] == b'\x89PNG\r\n\x1a\n', 'Actual engine capture is not PNG')
                entry.update(sha256=hashlib.sha256(data).hexdigest(),
                    width=int.from_bytes(data[16:20], 'big'), height=int.from_bytes(data[20:24], 'big'))
            if self.capture_hz:
                for phase in ('solo_side', 'solo_front', 'two_rowers', 'released'):
                    self.require(sum(1 for e in self.report['frames'] if e['phase'] == phase) >= 6,
                                 'Too few genuine captured frames for pose-review stage ' + phase)
            self.report['manual_checkpoints'] = [
                'Review full strokes from side and front three-quarter: brush entry, pull, lift and recovery; hand contact, torso rhythm, foot support and release blending.',
                'Review two rowers for hand/brush intersections and phase/readability. Telemetry alone is not visual acceptance.',
                'The normal gameplay camera, network clients and full event balance are separate checks.']
            self.finish()

    def require_couple(self, stage, sign):
        entries=self.stage_samples(stage)
        stable=[e for e in entries if e['rowing_count']==2 and e['hero_rowing'] and e['bot_rowing']]
        cancelled=[e for e in stable if e['rowing_vector_magnitude']<.15
                   and abs(e['stroke_started_at'][0]-e['stroke_started_at'][1])<.03]
        self.require(len(stable)>=20 and len(cancelled)>=len(stable)*.75, 'Opposing native aims/phases did not cancel requested translation')
        yaw=float(self.raft.get_actor_rotation().yaw)
        delta=(yaw-self.torque_yaw+180.)%360.-180.
        self.require(delta*sign>4., 'The real off-centre rowing couple did not turn/reverse the raft')
        self.report.setdefault('torque_results',{})[stage]=dict(yaw_delta_degrees=delta,expected_sign=sign,
            stable_samples=len(stable),cancelled_translation_samples=len(cancelled))
        self.report['checks'][stage+'_opposing_inputs_cancel_translation_and_turn']=True

    def tick(self, delta):
        if not self.done and valid(self.world) and valid(self.hero):
            try:
                self.aim_rowing()
                now = float(u.GameplayStatics.get_time_seconds(self.world))
                if valid(self.bot) and self.bot.is_rowing():
                    # Suppress the inherited probe's scripted heartbeat. The
                    # locally controlled AI pawn sends its own native input.
                    self.bot_direction_at = now + 1.
                    self.require(self.bot.is_locally_controlled(), 'AI fixture needs its native local input ticker')
                if self.phase == 'solo_front':
                    elapsed = now - self.phase_at
                    if .9 <= elapsed < 2.65 and self.walk_stopped_at is None:
                        foot = self.hero.get_actor_location() - u.Vector(0, 0, self.hero.capsule_component.get_scaled_capsule_half_height())
                        if self.walk_before is None:
                            self.walk_before = prop(self.raft, 'deck').get_world_transform().inverse_transform_location(foot)
                            self.walk_sign = -1. if self.walk_before.y > 0. else 1.
                            self.report['walking_fixture'] = dict(start_game_seconds=now,
                                start_deck_local_cm=vec(self.walk_before), input_scale=.25,
                                maximum_requested_input_seconds=1.75, inward_sign=self.walk_sign,
                                native_input='AddMovementInput inward from observed deck side; stop after crossing centre')
                        local = prop(self.raft, 'deck').get_world_transform().inverse_transform_location(foot)
                        if local.y*self.walk_sign >= 12.:
                            self.walk_stopped_at = now
                        else:
                            self.hero.add_movement_input(self.raft.get_actor_right_vector(), .25*self.walk_sign, False)
                    if self.walk_before is not None and not self.walk_finished and (
                            (self.walk_stopped_at is not None and now-self.walk_stopped_at >= .25) or elapsed >= 2.95):
                        foot = self.hero.get_actor_location() - u.Vector(0, 0, self.hero.capsule_component.get_scaled_capsule_half_height())
                        local = prop(self.raft, 'deck').get_world_transform().inverse_transform_location(foot)
                        offset = local - self.walk_before
                        displacement = (offset.x * offset.x + offset.y * offset.y) ** .5
                        self.require(30. <= displacement < 100. and self.walk_before.y*local.y<0,
                                     'Genuine inward movement did not safely cross the deck centre')
                        self.require(self.hero.is_rowing() and self.base(self.hero) == prop(self.raft, 'deck'),
                                     'Walking on deck cancelled native held-LMB rowing')
                        self.report['walking_fixture'].update(end_game_seconds=now,
                            end_deck_local_cm=vec(local), actual_displacement_cm=displacement)
                        self.report['checks']['walk_on_deck_with_lmb_rowing_keeps_rowing'] = True
                        self.walk_finished = True
            except Exception:
                self.finish(traceback.format_exc())
                return
        super().tick(delta)

    def finish(self, error=None):
        # Skip the storm parent's dedicated-rowing cleanup: this probe starts
        # and releases only the actual LMB path, including on cancellation.
        if not self.done and self.world in u.EditorLevelLibrary.get_pie_worlds(True):
            try:
                for pawn in (self.hero, self.bot):
                    if valid(pawn): self.rowing(pawn, False)
                if valid(self.hero): self.hero.call_method('SetBraceInputHeld', args=(False,))
                if self.menu_opened and valid(self.pc): self.pc.toggle_dev_panel()
                if valid(self.scheduler) and self.scheduler_tick is not None:
                    self.scheduler.set_actor_tick_enabled(self.scheduler_tick)
                self.restore_event_tick()
                if self.episode_started_at is not None:
                    self.report['episode_game_seconds'] = float(u.GameplayStatics.get_time_seconds(self.world)) - self.episode_started_at
            except Exception:
                error = error or traceback.format_exc()
        LifecycleParent.finish(self, error)


def main():
    for key in (KEY, 'mc_cataclysm_storm_solo', 'mc_cataclysm_solo',
                'mc_cataclysm_drowning_solo', 'mc_approved_environment_solo'):
        old = getattr(builtins, key, None)
        if old is not None and not getattr(old, 'done', True):
            raise RuntimeError('Existing owned probe is active and preserved: ' + key)
    probe = RowingAnimationSolo()
    setattr(builtins, KEY, probe)
    try:
        probe.start()
    except Exception:
        probe.finish(traceback.format_exc())
        raise


if __name__ == '__main__':
    main()
