"""Uvula-only owned solo PIE: upper attachment, idle, real watergun, recovery.

Normal first perk is resolved, then only the unrelated day scheduler is paused.
No food fixtures, throws, bosses, waves, phase writes, geometry edits or direct
tickle call. The review camera supplies the existing ServerCommitSprayView input
RPC; actual held LMB, shot collision and native uvula response remain in control.
Output Saved/UvulaLocation_20261018/Preview. No package saves/build/network.
Cancel: builtins.mc_uvula_attachment_solo.finish('Stopped by operator').
"""
import builtins
import datetime
import hashlib
import json
import math
import runpy
import time
import traceback
import uuid
from pathlib import Path
import unreal as u

ROOT=Path(u.Paths.project_dir()).resolve()
OUTPUT=ROOT/'Saved/UvulaLocation_20261018/Preview'
if OUTPUT.drive.upper()!='E:':
    raise RuntimeError('Uvula evidence must remain on E:')
support=runpy.run_path(str(ROOT/'Tools/Unreal/check_manual_intake_solo.py'),run_name='uvula_attachment_support')
Parent,prop,vec,valid,path=(support[key] for key in ('ManualIntakeSolo','prop','vec','valid','path'))
KEY='mc_uvula_attachment_solo'


def transform_row(pose):
    return dict(translation_cm=vec(pose.translation),scale=vec(pose.scale3d),
        rotation_xyzw=[float(getattr(pose.rotation,key)) for key in ('x','y','z','w')])


class UvulaAttachmentSolo(Parent):
    def __init__(self):
        super().__init__()
        stamp=datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%SZ')
        self.output=OUTPUT/'Runs'/(stamp+'_'+uuid.uuid4().hex[:8])
        self.output.mkdir(parents=True,exist_ok=True)
        self.schedulers=[]
        self.shot_started=False
        self.shot_released=False
        self.uvula=self.mouth=None
        self.anchor_local=self.tip_local=None
        self.anchor_before=self.tip_before=None
        self.tickle_before=-100.
        self.maximum_anchor_drift=self.maximum_tip_motion=0.
        self.report.update(mode='uvula_attachment_only',scope=__doc__,run_directory=str(self.output),
            samples=[],checks={},clips={},frames=[],screenshots=[],fixture_tuning={},
            uvula_only=True,food_delivery_tested=False,network_tested=False,
            limitations=[
                'Only the active SM_InteractiveUvula, its upper attachment, visible reaction and recovery are tested.',
                'The native day scheduler is paused after normal first-perk selection and restored during teardown.',
                'Observer location and one review camera are disclosed PIE fixtures; no gameplay phase, health, uvula transform or morph is assigned.',
                'The existing ServerCommitSprayView RPC supplies review-camera aim; real held primary input drives native shots/collision/tickle.',
                'Capture uses the synchronous exact owned PIE SWindow helper with timestamps and SHA256; rendered attachment quality awaits root visual review.',
                'No food fixture, throws, bosses, waves, package save, build, network clients or performance claims.'])
        self.report['source_sha256']['Tools/Unreal/probe_uvula_attachment_solo.py']=hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
        change_report=ROOT/'Saved/UvulaLocation_20261018/HeightApplied.json'
        if change_report.is_file():
            self.report['local_asset_change_evidence']=dict(path=str(change_report),
                sha256=hashlib.sha256(change_report.read_bytes()).hexdigest(),
                scope='Root-authored saved asset change; read-only reference, not authored by this probe')
        editor_throats=list(u.GameplayStatics.get_all_actors_of_class(self.editor_world,u.MCThroat))
        self.require(len(editor_throats)==1,'Expected one existing authored throat in the editor map')
        self.editor_throat=editor_throats[0]
        self.report['saved_editor_baseline']=self.art_row(self.editor_throat)

    def write(self):
        self.output.mkdir(parents=True,exist_ok=True)
        data=json.dumps(self.report,ensure_ascii=False,indent=2,allow_nan=False)
        (self.output/'Report.json').write_text(data,encoding='utf-8')
        OUTPUT.mkdir(parents=True,exist_ok=True)
        (OUTPUT/'Report.json').write_text(data,encoding='utf-8')

    def art_row(self,throat):
        mouth=prop(throat,'authored_mouth')
        return dict(root_world=transform_row(throat.get_actor_transform()),
            mouth_world=transform_row(mouth.get_world_transform()),
            mouth_mesh=path(mouth.get_skeletal_mesh_asset()),
            manual_authored_expansion=float(prop(throat,'manual_authored_expansion')),
            authored_uvula_rest=transform_row(prop(throat,'authored_uvula_rest_transform')))

    @staticmethod
    def same_art(actual,expected):
        if actual['mouth_mesh']!=expected['mouth_mesh'] or abs(actual['manual_authored_expansion']-expected['manual_authored_expansion'])>.00001:
            return False
        for key in ('root_world','mouth_world','authored_uvula_rest'):
            a,b=actual[key],expected[key]
            if math.dist(a['translation_cm'],b['translation_cm'])>.02 or math.dist(a['scale'],b['scale'])>.00001:
                return False
            qa,qb=a['rotation_xyzw'],b['rotation_xyzw']
            if min(math.dist(qa,qb),math.dist(qa,[-value for value in qb]))>.00001:
                return False
        return True

    def setup(self):
        self.hero=self.pc.get_controlled_pawn()
        self.require(valid(self.hero) and prop(self.hero,'status').is_alive(),'A living real observer is required')
        throats=self.actors(u.MCThroat)
        self.require(len(throats)==1,'Expected exactly one native throat')
        self.throat=throats[0]
        self.tongue=self.actors(u.MCTongue)[0]
        self.collection=prop(self.hero,'food_collection')
        self.uvula=prop(self.throat,'uvula')
        self.mouth=prop(self.throat,'authored_mouth')
        for scheduler in self.actors(u.MCSingleDayDirector):
            before=bool(scheduler.is_actor_tick_enabled())
            self.schedulers.append((scheduler,before))
            scheduler.set_actor_tick_enabled(False)
        self.report['scheduler_pause_fixture']=[dict(actor=path(actor),previous_tick_enabled=before)
            for actor,before in self.schedulers]
        self.report['checks']['normal_first_perk_resolved']=True
        self.require(prop(self.throat,'use_authored_uvula') and self.uvula.static_mesh.get_name()=='SM_InteractiveUvula',
            'The active component must use the new authored uvula')
        self.require(self.uvula.is_visible() and not prop(self.uvula,'hidden_in_game'),'New uvula is hidden')
        self.require(self.uvula.get_collision_enabled()==u.CollisionEnabled.QUERY_ONLY,'Native watergun query collision is missing')
        visible=[]
        for actor in self.actors(u.Actor):
            for component in actor.get_components_by_class(u.StaticMeshComponent):
                mesh=component.static_mesh
                if mesh and mesh.get_name() in ('SM_InteractiveUvula','SM_MiniTong','SM_Uvula'):
                    visible.append(dict(component=path(component),mesh=mesh.get_name(),visible=bool(component.is_visible()),
                        hidden_in_game=bool(prop(component,'hidden_in_game'))))
        shown=[row for row in visible if row['visible'] and not row['hidden_in_game']]
        self.report['uvula_component_census']=visible
        self.require(len(shown)==1 and shown[0]['component']==path(self.uvula),'Duplicate or old visible uvula remains')
        self.report['checks']['one_visible_new_uvula_and_no_visible_old_mesh']=True
        baseline=self.report['saved_editor_baseline']
        actual=self.art_row(self.throat)
        self.require(self.same_art(actual,baseline),'PIE throat/root/mouth/rest differs from the existing editor-authored placement')
        self.report['pie_art_baseline']=actual
        self.fixed_morph=float(self.mouth.get_morph_target('Open'))
        local_bounds=self.uvula.static_mesh.get_bounds()
        self.anchor_local=u.Vector(0,0,local_bounds.origin.z+local_bounds.box_extent.z)
        self.tip_local=u.Vector(0,0,local_bounds.origin.z-local_bounds.box_extent.z)
        pose=self.uvula.get_world_transform()
        self.anchor_before=pose.transform_location(self.anchor_local)
        self.tip_before=pose.transform_location(self.tip_local)
        center,extent,_=u.SystemLibrary.get_component_bounds(self.uvula)
        self.require(abs(self.anchor_before.z-1458.)<90. and abs(center.z-1100.)<90.,
            'The authored uvula is not at the expected upper attachment; inspect report')
        self.report['upper_attachment']=dict(anchor_cm=vec(self.anchor_before),tip_cm=vec(self.tip_before),
            bounds_center_cm=vec(center),bounds_extent_cm=vec(extent),expected_anchor_z_cm=1458.,
            expected_center_z_cm=1100.,height_tolerance_cm=90.,native_pivot_local_cm=vec(self.anchor_local))
        approaches=[]
        for distance in (500.,650.,800.,950.):
            floor=self.floor(center-u.Vector(distance,0,0))
            if floor is not None:approaches.append((distance,floor))
        self.require(approaches,'No supported tongue position before the upper uvula')
        distance,floor=approaches[0]
        observer=floor+u.Vector(0,0,self.hero.capsule_component.get_scaled_capsule_half_height()+3.)
        self.hero.set_actor_location_and_rotation(observer,u.Rotator(),False,True)
        self.report['observer_position_fixture']=dict(location_cm=vec(observer),distance_before_uvula_cm=distance,
            method='Position only over actual tongue; native movement, health and body state unchanged')
        self.uvula_target=center
        eye=center+u.Vector(-1200,0,150)
        self.frame_camera(eye,self.uvula_target)
        prop(self.camera,'camera_component').set_editor_property('field_of_view',80.)
        self.pc.set_control_rotation(u.MathLibrary.find_look_at_rotation(self.hero.get_actor_location(),self.uvula_target))
        points=[self.anchor_before,self.tip_before,self.anchor_before+u.Vector(0,-90,60),self.anchor_before+u.Vector(0,90,60)]
        projected=[self.project_review_point(point) for point in points]
        self.require(all(point is not None and .10<point[0]<.90 and .06<point[1]<.94 for point in projected),
            'Review camera clips the upper attachment or the hanging tip')
        self.report['review_camera']=dict(eye_cm=vec(eye),target_cm=vec(self.uvula_target),
            framing_points_cm=[vec(point) for point in points],projected_uv=projected,
            framing_scope='Anchor through tip plus palate context; no geometry hidden. Projection is not visual approval.')
        self.pc.call_method('RequestDevAction',args=(u.MCDevAction.GRANT_ALL_TOOL_BOOSTERS,0))
        self.tickle_before=float(prop(self.throat,'tickle_started_at'))
        self.report['camera_settle_seconds']=.5
        self.stage('uvula_camera_warmup')

    def sample(self,now):
        if not valid(self.uvula):return
        pose=self.uvula.get_world_transform()
        anchor=pose.transform_location(self.anchor_local)
        tip=pose.transform_location(self.tip_local)
        self.maximum_anchor_drift=max(self.maximum_anchor_drift,math.dist(vec(anchor),vec(self.anchor_before)))
        self.maximum_tip_motion=max(self.maximum_tip_motion,math.dist(vec(tip),vec(self.tip_before)))
        inventory=prop(self.hero,'inventory')
        self.report['samples'].append(dict(time=now,phase=self.phase,
            tickle_started_at=float(prop(self.throat,'tickle_started_at')),anchor_cm=vec(anchor),tip_cm=vec(tip),
            uvula_world=transform_row(pose),mouth_world=transform_row(self.mouth.get_world_transform()),
            open_morph=float(self.mouth.get_morph_target('Open')),last_water_shot_at=float(prop(inventory,'last_water_shot_at')),
            ammo=int(prop(inventory,'water_ammo')),hero_alive=bool(prop(self.hero,'status').is_alive())))

    def tick(self,_delta):
        if self.done:return
        try:
            self.require(time.monotonic()-self.started<90.,'Short uvula probe timed out')
            worlds=list(u.EditorLevelLibrary.get_pie_worlds(True))
            if self.world is None:
                if not worlds:return
                self.require(len(worlds)==1 and u.SystemLibrary.is_standalone(worlds[0]),'Expected one owned standalone PIE')
                self.world=worlds[0]
                self.pc=u.GameplayStatics.get_player_controller(self.world,0)
                if not valid(self.pc) or not valid(self.pc.get_controlled_pawn()):
                    self.world=None;return
                self.stage('first_perk');return
            self.require(self.owned_pie_is_active(worlds),'Owned PIE ended or was replaced')
            self.hero=self.pc.get_controlled_pawn()
            now=float(u.GameplayStatics.get_time_seconds(self.world))
            if self.pc.is_reward_menu_open():
                self.require(self.phase=='first_perk','Unexpected reward UI in uvula-only scenario')
                self.pc.choose_reward_perk(0)
                self.stage('perk_resolution')
                return
            if self.last_game_time is not None and now<=self.last_game_time:return
            self.last_game_time=now
            elapsed=now-self.phase_at
            if self.phase=='first_perk':
                self.require(elapsed<25.,'Normal first perk did not appear');return
            if self.phase=='perk_resolution':self.setup();return
            if self.phase=='uvula_camera_warmup':
                self.pc.set_view_target_with_blend(self.camera,0.)
                if elapsed>=.5:
                    self.stage('uvula_idle_tickle_recovery')
                    self.begin_clip('uvula_upper_attachment_watergun',5.5)
                return
            self.require(prop(self.hero,'status').is_alive(),'Native observer died')
            self.require(self.same_art(self.art_row(self.throat),self.report['pie_art_baseline']),'AuthoredMouth/root/rest changed during uvula reaction')
            self.require(abs(self.mouth.get_morph_target('Open')-self.fixed_morph)<.001,'AuthoredMouth fixed morph changed')
            self.pc.set_view_target_with_blend(self.camera,0.)
            if now>=self.sample_at:
                self.sample_at=now+.05;self.sample(now)
            if elapsed>=.5 and not self.shot_started:
                inventory=prop(self.hero,'inventory')
                if not inventory.has_upgrade(u.MCToolUpgrade.WATERGUN):
                    self.pc.call_method('RequestDevAction',args=(u.MCDevAction.GRANT_ALL_TOOL_BOOSTERS,1))
                inventory.server_select(u.MCToolSlot.SPRAY)
                if not prop(inventory,'pressure_mode'):inventory.server_select(u.MCToolSlot.SPRAY)
                origin=self.camera.get_actor_location()
                direction=u.MathLibrary.get_forward_vector(self.camera.get_actor_rotation())
                inventory.call_method('ServerCommitSprayView',args=(u.Vector_NetQuantize(origin.x,origin.y,origin.z),
                    u.Vector_NetQuantizeNormal(direction.x,direction.y,direction.z)))
                self.report['watergun_camera_input_fixture']=dict(origin_cm=vec(origin),direction=vec(direction),
                    method='Existing ServerCommitSprayView, real held primary input, native muzzle sweep and tickle')
                self.primary_input(True);self.shot_started=True
            if elapsed>=3.5 and not self.shot_released:
                self.primary_input(False);self.shot_released=True
            if self.clip and now>=self.frame_at:
                self.frame_at=now+1./self.capture_hz
                camera=self.camera_evidence()
                if camera and camera['view_target']==camera['expected_target'] and camera['location_error_cm']<12. and camera['rotation_error_degrees']<5.:
                    self.screenshot('%05d.png'%len(self.report['frames']),True)
            # Inspect each newly written frame while it is fresh. The shared
            # manual probe's pending-frame deadline is five wall seconds.
            self.verify_pending_capture_dimensions()
            if elapsed>=5.5:
                self.primary_input(False)
                self.require(float(prop(self.throat,'tickle_started_at'))>self.tickle_before,'Actual watergun shots did not tickle the raised uvula')
                self.require(self.maximum_anchor_drift<.2,'Native upper attachment moved during reaction')
                self.require(self.maximum_tip_motion>5.,'No meaningful native uvula reaction was observed')
                self.require(len(self.report['frames'])>=30,'Too few genuine game-window frames')
                self.verify_pending_capture_dimensions()
                for entry in self.report['frames']:
                    data=Path(entry['file']).read_bytes()
                    self.require(entry['sha256']==hashlib.sha256(data).hexdigest(),'Source PNG changed after capture')
                self.report['checks'].update(raised_upper_attachment=True,actual_watergun_tickle=True,
                    fixed_upper_anchor_during_motion=True,authored_mouth_root_and_morph_unchanged=True,
                    genuine_owned_pie_window_frames=True)
                self.report['motion_evidence']=dict(maximum_anchor_drift_cm=self.maximum_anchor_drift,
                    maximum_tip_displacement_cm=self.maximum_tip_motion,tickle_before=self.tickle_before,
                    tickle_after=float(prop(self.throat,'tickle_started_at')))
                self.finish()
        except Exception:self.finish(traceback.format_exc())

    def finish(self,error=None):
        if not self.done:
            for scheduler,before in self.schedulers:
                if valid(scheduler):scheduler.set_actor_tick_enabled(before)
            self.report['scheduler_tick_restored']=True
        super().finish(error)


if __name__=='__main__':
    previous=getattr(builtins,KEY,None)
    if previous and not getattr(previous,'done',True):raise RuntimeError('Preserve active uvula probe')
    probe=UvulaAttachmentSolo()
    setattr(builtins,KEY,probe)
    try:probe.start()
    except Exception:
        probe.finish(traceback.format_exc())
        raise
