"""Bounded Sponge revision observer; no automatic Editor launch/build.

Use runpy with MC_SPONGE_REVISION_CASE='repeat' or 'timer' and a fresh E:
MC_ACTIVE_OUTPUT. Both use real slot input, evaluated skeletons and WGC gates.
The timer case adds two visible native box blockers before Ready. After input
starts, the observer never edits poses, health, masks, action state or velocity.
MC_SPONGE_DIAGNOSTIC_VIEWS enables separate unlit front/side readbacks; keep it
False in the main gameplay recording to avoid render-target readback hitches.
"""
import builtins, copy, hashlib, json, math, runpy, time
from pathlib import Path
import unreal as u

ROOT=Path(u.Paths.project_dir()).resolve()
SCENARIO=str(globals().get('MC_SPONGE_REVISION_CASE','repeat'))
assert SCENARIO in ('repeat','timer')
DIAGNOSTIC_VIEWS=bool(globals().get('MC_SPONGE_DIAGNOSTIC_VIEWS',False))
OUT=Path(globals().get('MC_ACTIVE_OUTPUT',
    'E:/CodexWork/MessControl/Validation/SpongeRevision_20261030/'+SCENARIO)).resolve()
assert OUT.drive.upper()=='E:' and not OUT.exists()
REPEAT_AT=1.1
POST_ACTION_SECONDS=3.2
STAIN_HALF_CM=80.
source=Path(__file__).read_bytes()
base=runpy.run_path(str(ROOT/'Tools/Unreal/check_active_skills_solo.py'),run_name='sponge_revision_base',init_globals={
    'MC_ACTIVE_CASE':'Sponge','MC_ACTIVE_VARIANT':SCENARIO,'MC_ACTIVE_LAST_CHARGE':False,
    'MC_ACTIVE_OUTPUT':str(OUT),'MC_ACTIVE_CAMERA_PITCH':float(globals().get('MC_ACTIVE_CAMERA_PITCH',-10)),
    'MC_ACTIVE_CAMERA_YAW':float(globals().get('MC_ACTIVE_CAMERA_YAW',0)),
    'MC_ACTIVE_LOADING_SECONDS':float(globals().get('MC_ACTIVE_LOADING_SECONDS',40)),
    'MC_ACTIVE_POSE_AUDIT':True,'MC_ACTIVE_POSE_SNAPSHOTS':DIAGNOSTIC_VIEWS})
Base=base['ActiveSkillsSolo'];prop=base['prop'];valid=base['valid'];path=base['path'];vec=base['vec']
transform_row=base['transform_row'];guid_text=base['guid_text']
for relative in ('Source/MessControl/Private/MCToothAnimInstance.cpp',
                 'Source/MessControl/Private/MCSpongeSoapTrail.cpp',
                 'Source/MessControl/Public/MCSpongeSkillAction.h'):
    assert (ROOT/'Binaries/Win64/UnrealEditor-MessControl.dll').stat().st_mtime_ns >= (ROOT/relative).stat().st_mtime_ns,relative
(OUT/'RevisionRecipe.py').write_bytes(source)


class SpongeRevisionSolo(Base):
    def __init__(self):
        super().__init__()
        self.trails={};self.trail_events=[];self.native_end_at=None;self.pose_rows=[]
        self.report['revision_freshness']=dict(recipe_sha256=hashlib.sha256(source).hexdigest(),
            recipe_snapshot=str(OUT/'RevisionRecipe.py'),base_recipe_sha256=base['RECIPE_SHA256'])
        self.report['sponge_revision']=dict(scenario=SCENARIO,repeat_after_seconds=REPEAT_AT,
            minimum_post_action_capture_seconds=POST_ACTION_SECONDS,trail_events=self.trail_events,
            pose_samples=self.pose_rows,diagnostic_views=DIAGNOSTIC_VIEWS,
            particle_count_api='Unavailable in Python. Actual OnSystemFinished event and component lifecycle are observed instead.',
            input_policy='Only ordinary slot press/release/repeat after Ready; base steering dispatch receives zero vector. No velocity/action/pose/mask outcome writes.',
            visual_scope='Evaluated bone/physical hand output plus actual gameplay-window movie; numerical checks are not artistic acceptance.')

    def write(self):
        if not self.done and self.phase in ('running','failure_drain'):
            # Serialising every retained bone frame to a growing multi-MB JSON
            # once per second stalls the game thread we are trying to observe.
            # Keep all evidence in memory; emit the full report at the end gate
            # and finish, plus a small independent progress/error heartbeat now.
            latest=self.report['samples'][-1] if self.report['samples'] else {}
            progress=dict(status='observing',phase=self.phase,samples=len(self.report['samples']),
                game_seconds=latest.get('game_seconds'),age=latest.get('age'),
                action_paths=[row['path'] for row in latest.get('actions',[])],
                pending_error=self.pending_error,full_evidence_deferred_until_capture_end=True)
            (OUT/'Progress.json').write_text(json.dumps(progress,indent=2),encoding='utf-8')
            return
        super().write()

    def prepare_targets(self,now):
        # Front-view camera variants must not reverse the skill's intended
        # initial direction. Fixtures follow the source pawn, not camera aim.
        direction=self.hero.get_actor_forward_vector();length=math.hypot(direction.x,direction.y)
        self.require(length>.5,'Source pawn needs a usable initial planar heading')
        self.forward=u.Vector(direction.x/length,direction.y/length,0)
        start=self.hero.get_actor_location();self.start_location=start
        point=self.floor(start+self.forward*360)
        self.require(point is not None,'Supported native stain location required')
        self.patch=self.spawn(u.MCMouthSurface,point+u.Vector(0,0,3),prepare=lambda a:a.set_editor_properties(dict(
            randomize_liquid_size=False,liquid_half_size=STAIN_HALF_CM,ulcer=False,paint_tile=False,ground_response=u.MCGroundSurface.NORMAL)))
        self.stain_spawn_frame=self.patch.get_actor_transform()
        status=prop(self.patch,'status');state=prop(status,'state');total=int(prop(state,'coffee_total'))
        self.require(total>0,'Ordinary native stain must have a positive total')
        state.set_editor_property('coffee_left',total);status.set_editor_property('State',state)
        self.report['stain_fixture']=dict(path=path(self.patch),initial_coffee_left=total,
            mask_untouched=all(int(x)==255 for x in prop(self.patch,'wipe_mask')),writes='Only initial native fixture setup before Ready',
            half_size_cm=STAIN_HALF_CM,spawn_frame=transform_row(self.stain_spawn_frame),
            native_recovery_clearance_cm=STAIN_HALF_CM*1.415+65.,
            clearance_note='Native anti-softlock reserves half-size*1.415+65cm. Prior 210cm fixture overlapped the corridor wall and relocated itself before input.')
        if SCENARIO=='timer':
            mesh=u.load_asset('/Engine/BasicShapes/Cube');self.require(mesh is not None,'Native diagnostic cube required')
            yaw=math.degrees(math.atan2(self.forward.y,self.forward.x));walls=[]
            for distance_cm in (-260.,650.):
                floor=self.floor(start+self.forward*distance_cm)
                self.require(floor is not None,'Corridor end has no native tongue support')
                def prepare(actor):
                    body=actor.get_component_by_class(u.StaticMeshComponent)
                    body.set_mobility(u.ComponentMobility.MOVABLE);body.set_static_mesh(mesh)
                    body.set_collision_profile_name('BlockAll');body.set_simulate_physics(False)
                wall=self.spawn(u.StaticMeshActor,floor+u.Vector(0,0,130),u.Vector(.36,4.8,2.6),prepare)
                wall.set_actor_rotation(u.Rotator(yaw=yaw),False)
                walls.append(dict(path=path(wall),distance_from_start_cm=distance_cm,
                    half_size_cm=[18,240,130],collision='BlockAll',moved_during_capture=False))
            self.report['sponge_revision']['corridor']=dict(walls=walls,
                disclosure='Two visible non-simulating native cube box fixtures configured once before Ready; ordinary skill reflection handles contact.')
        self.foods=[];self.food_ids={};self.fixture_sealed_at=now;self.phase='fixture_settle'
        self.report['fixture_sealed_game_seconds']=now

    def ready(self,now):
        frame=self.patch.get_actor_transform()
        shift=float(u.MathLibrary.vector_distance(frame.translation,self.stain_spawn_frame.translation))
        self.require(shift<35.,'Native stain relocated before Ready; fixture no longer intersects the intended ride')
        self.require(all(int(value)==255 for value in prop(self.patch,'wipe_mask')),
            'Initial full native mask must be retained until actual input')
        self.report['stain_fixture'].update(ready_frame=transform_row(frame),pre_ready_shift_cm=shift)
        super().ready(now)

    def track_trails(self):
        for action in self.current_actions():
            component=prop(action,'soap_trail_component')
            if not valid(component):continue
            name=path(component)
            if name in self.trails:continue
            row=dict(component=component,first_seen=float(u.GameplayStatics.get_time_seconds(self.world)),
                owner=path(component.get_owner()),finished_at=None)
            preview=component.get_variable_bool('User.PreviewMotion')
            # UE's Python wrapper consumes the native bool return as a success
            # flag and exposes the out-validity as bool-or-None. A false value
            # therefore reads None; it cannot independently prove validity.
            row.update(preview_motion_python_getter=preview)
            def make_callback(ident):
                def callback(comp):
                    self.trails[ident]['finished_at']=float(u.GameplayStatics.get_time_seconds(self.world))
                    self.trail_events.append(dict(component=ident,event='OnSystemFinished',game_seconds=self.trails[ident]['finished_at']))
                return callback
            row['callback']=make_callback(name)
            self.trails[name]=row
            prop(component,'on_system_finished').add_callable(row['callback'])

    def active_fx(self):
        self.track_trails()
        result=super().active_fx()
        for row in self.trails.values():
            # Deactivate() sets is_active false while existing world particles
            # can still be fading. Wait for their native completion delegate.
            component=row['component']
            if row['finished_at'] is None and valid(component) and component not in result:result.append(component)
        return result

    def sample_case(self,now):
        super().sample_case(now)
        live=self.current_actions();sample=self.report['samples'][-1]
        if valid(self.patch):
            patch_frame=self.patch.get_actor_transform();mask=prop(self.patch,'wipe_mask');side=math.isqrt(len(mask))
            self.require(side>0 and side*side==len(mask),'Native stain mask must retain its square layout')
            half=float(prop(self.patch,'liquid_half_size'));cell=side//2
            # The centre texel is at its texel centre, not exactly local (0,0).
            local_center=u.Vector(((cell+.5)/side-.5)*2*half,((cell+.5)/side-.5)*2*half,0)
            world_center=patch_frame.transform_location(local_center)
            foot=self.hero.get_actor_location()-u.Vector(0,0,self.hero.capsule_component.get_scaled_capsule_half_height())
            local_foot=patch_frame.inverse_transform_location(foot)
            sample['native_stain']=dict(frame=transform_row(patch_frame),half_size_cm=half,
                center_texel_xy=[cell,cell],center_texel_value=int(mask[cell*side+cell]),
                center_texel_world_cm=vec(world_center),hero_capsule_foot_cm=vec(foot),
                hero_foot_stain_local_cm=vec(local_foot),
                center_planar_foot_distance_cm=math.hypot(local_foot.x-local_center.x,local_foot.y-local_center.y),
                local_foot_height_cm=float(local_foot.z),
                shift_from_spawn_cm=float(u.MathLibrary.vector_distance(patch_frame.translation,self.stain_spawn_frame.translation)))
        if self.actions and not live and self.native_end_at is None:self.native_end_at=now
        sample['soap_trails']=[dict(component=name,valid=valid(row['component']),
            active=bool(row['component'].is_active()) if valid(row['component']) else False,
            natural_finished_at=row['finished_at']) for name,row in self.trails.items()]
        if not live:return
        action=live[0];frame=action.get_ride_frame();pose=sample['pose_audit']['pose']
        bones={role:vec(frame.inverse_transform_location(u.Vector(*bone['world']['cm'])))
            for role,bone in pose['bones'].items() if role in ('body','foot_l','foot_r','hand_l','hand_r')}
        row=dict(game_seconds=now,age=action.get_presentation_age(),frame=transform_row(frame),
            board_forward=vec(action.get_actor_forward_vector()),bones_board_local_cm=bones,
            bounce_count=int(prop(action,'bounce_count')),last_bounce_normal=vec(prop(action,'last_bounce_normal')),
            rendered_pose=pose)
        row['actor_created_at_game_seconds']=self.optional(lambda:now-float(action.get_game_time_since_creation()))
        row['native_started_at_game_seconds']=self.optional(lambda:float(prop(action,'started_at')))
        row['native_expected_end_game_seconds_from_start_and_authored_duration']=None if row['native_started_at_game_seconds'] is None else row['native_started_at_game_seconds']+self.param('RideSeconds',8)
        self.pose_rows.append(row);sample['sponge_revision']=row
        if SCENARIO=='repeat' and not self.repeated and now-self.case_started>=REPEAT_AT:
            self.require(len(live)==1,'One original native ride is required before repeat')
            self.repeat_action_id=guid_text(action.get_action_id());self.repeat_before=now
            self.press(now,'Started_repeat');self.release(now);self.repeated=True

    def complete_checks(self,now):
        super().complete_checks(now)
        self.check('rendered_feet_body_and_both_hands_observed',bool(self.pose_rows) and all(
            all(role in r['bones_board_local_cm'] for role in ('body','foot_l','foot_r','hand_l','hand_r')) for r in self.pose_rows))
        deltas=[];reflections=[]
        for previous,current in zip(self.pose_rows,self.pose_rows[1:]):
            dt=current['game_seconds']-previous['game_seconds']
            old,new=previous['bounce_count'],current['bounce_count']
            if new>old:
                normal=current['last_bounce_normal'];incoming=previous['board_forward'];outgoing=current['board_forward']
                dot=sum(a*b for a,b in zip(incoming,normal));expected=[a-2*dot*b for a,b in zip(incoming,normal)]
                reflected_dot=sum(a*b for a,b in zip(expected,outgoing))
                reflections.append(dict(game_seconds=current['game_seconds'],count_delta=new-old,
                    incoming=incoming,outgoing=outgoing,normal=normal,reflection_dot=reflected_dot))
            if previous['age']<.35 or not 0<dt<.08 or new!=old:continue
            # A reflection is intentionally abrupt. Exclude its short pose
            # response window from the straight-run stride-jitter measurement.
            if any(current['game_seconds']-event['game_seconds']<.3 for event in reflections):continue
            deltas.append({role:math.dist(previous['bones_board_local_cm'][role],current['bones_board_local_cm'][role])
                for role in ('body','foot_l','foot_r') if role in previous['bones_board_local_cm'] and role in current['bones_board_local_cm']})
        maxima={role:max((r.get(role,0) for r in deltas),default=None) for role in ('body','foot_l','foot_r')}
        self.report['sponge_revision'].update(stable_frame_pairs=len(deltas),max_local_frame_delta_cm=maxima,
            bounce_observations=reflections,action_ended_at=self.native_end_at,
            trail_completion=[dict(component=name,first_seen=row['first_seen'],finished_at=row['finished_at'],
                owner=row['owner'],still_valid=valid(row['component'])) for name,row in self.trails.items()])
        self.check('steady_pose_has_enough_actual_samples',len(deltas)>=12)
        self.check('steady_pose_does_not_stride',bool(deltas) and all(v is not None and v<=3 for v in maxima.values()))
        self.check('independent_soap_trail_observed',bool(self.trails))
        self.check('soap_trail_is_not_owned_by_the_action',bool(self.trails) and all(r['owner'] not in self.actions for r in self.trails.values()))
        self.check('soap_trail_finished_naturally',bool(self.trails) and all(r['finished_at'] is not None for r in self.trails.values()))
        self.check('soap_trail_outlived_action',self.native_end_at is not None and any(
            r['finished_at'] is not None and r['finished_at']>self.native_end_at+.1 for r in self.trails.values()))
        if SCENARIO=='timer':
            self.check('native_corridor_reflections_observed',len(reflections)>=2)
            self.check('native_mirror_direction_matches_wall_normal',any(r['count_delta']==1 for r in reflections) and all(
                r['reflection_dot']>=.98 for r in reflections if r['count_delta']==1))
        mask=list(prop(self.patch,'wipe_mask')) if valid(self.patch) else []
        side=math.isqrt(len(mask))
        stains=[sample['native_stain'] for sample in self.report['samples'] if 'native_stain' in sample]
        contact_stains=[sample['native_stain'] for sample in self.report['samples'] if sample.get('actions') and 'native_stain' in sample]
        self.report['stain_contact_observation']=dict(final_frame=transform_row(self.patch.get_actor_transform()) if valid(self.patch) else None,
            final_center_texel_value=mask[(side//2)*side+side//2] if side and side*side==len(mask) else None,
            final_changed_cells=sum(value!=255 for value in mask),final_cleared_cells=sum(value==0 for value in mask),
            min_live_center_planar_foot_distance_cm=min((row['center_planar_foot_distance_cm'] for row in contact_stains),default=None),
            max_native_shift_from_spawn_cm=max((row['shift_from_spawn_cm'] for row in stains),default=None),
            note='Actual native mask/frame read only; capsule-foot proximity is a contact witness, not a substitute for native cleaning success.')
        self.check('native_stain_fixture_stayed_on_intended_supported_path',bool(stains) and all(row['shift_from_spawn_cm']<35 for row in stains))
        self.check('ride_passed_over_actual_native_center',any(row['center_planar_foot_distance_cm']<self.param('CleanRadius',45)*.6 and abs(row['local_foot_height_cm'])<30 for row in contact_stains))
        self.check('contact_pass_cleaned_native_center_pixel',bool(side and side*side==len(mask)) and mask[(side//2)*side+side//2]==0)
        if SCENARIO=='timer':
            samples=self.report['samples'];last_live=max((row['game_seconds'] for row in samples if row['actions']),default=None)
            self.report['timer_observation'].update(first_input_game_seconds=self.case_started,
                last_live_game_seconds=last_live,first_absent_game_seconds=self.native_end_at,
                max_sample_gap_seconds=max((b['game_seconds']-a['game_seconds'] for a,b in zip(samples,samples[1:])),default=None),
                actual_native_started_times=sorted(set(row['native_started_at_game_seconds'] for row in self.pose_rows if row['native_started_at_game_seconds'] is not None)),
                actor_created_times=sorted(set(row['actor_created_at_game_seconds'] for row in self.pose_rows if row['actor_created_at_game_seconds'] is not None)),
                duration_assertion='Inherited authored-duration assertion remains unchanged; early native cancellation is a failure even if associated with a long frame.')

    def queue_end(self,now,error=None):
        if error is None and self.native_end_at is not None and now-self.native_end_at<POST_ACTION_SECONDS:
            self.quiet_since=None;return
        if self.native_end_at is not None:self.report['sponge_revision']['post_action_capture_seconds']=now-self.native_end_at
        super().queue_end(now,error)

    def tick(self,delta):
        if self.done:return
        # Native skill propulsion is sufficient. Sending a zero input vector
        # through the inherited steering dispatch never writes an outcome.
        forward=getattr(self,'forward',None)
        if self.phase=='running':self.forward=u.Vector()
        try:super().tick(delta)
        finally:
            if forward is not None:self.forward=forward

    def finish(self,error=None):
        if self.done:return
        for row in self.trails.values():
            if valid(row['component']):
                try:prop(row['component'],'on_system_finished').remove_callable(row['callback'])
                except Exception as failure:self.report.setdefault('trail_observer_cleanup_errors',[]).append(str(failure))
        super().finish(error)


def main():
    probe=SpongeRevisionSolo();setattr(builtins,base['KEY'],probe)
    try:probe.start()
    except Exception:
        import traceback
        probe.finish(traceback.format_exc());raise


if __name__=='__main__':main()
