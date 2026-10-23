"""Compare the live foam origin with the authored clip's same-frame bristles."""
import unreal as u,json,math,builtins,time,traceback
from pathlib import Path
ROOT=Path(u.Paths.project_dir()).resolve();OUT=ROOT/'Saved/MenuNiagaraBubbles20261023';r=json.loads((OUT/'Integration.json').read_text())
w=u.EditorLevelLibrary.get_pie_worlds(False)[0];aa=u.GameplayStatics.get_all_actors_of_class(w,u.Actor)
hero=next(a for a in aa if 'MC_MenuV2_Hero' in [str(t) for t in a.tags]);foam=next(a for a in aa if 'MC_MenuNiagaraBrushFoam' in [str(t) for t in a.tags]);player=next(a.sequence_player for a in aa if isinstance(a,u.LevelSequenceActor));clip=u.load_asset('/Game/UI/MenuShowcase/CharacterMenu20261023/A_CharacterMenu_Cleaning_Loop');options=u.AnimPoseEvaluationOptions();options.evaluation_type=u.AnimDataEvalType.COMPRESSED
offset=u.Vector(*r['relative_offset']);c=foam.get_component_by_class(u.NiagaraComponent)
class Check:
    def __init__(self):self.start=time.monotonic();self.first=None;self.last=-1.;self.prev=None;self.wraps=0;self.samples=[];self.handle=u.register_slate_post_tick_callback(self.tick)
    def finish(self,error=None):
        u.unregister_slate_post_tick_callback(self.handle)
        result={'status':'failed' if error else 'passed','error':error,'natural_wraps':self.wraps,'samples':self.samples,'reference':'Compressed source clip evaluated at the current Sequencer frame; not derived from the live socket transform.'}
        if self.samples:result['max_same_frame_source_clip_error_cm']=max(s['error_cm'] for s in self.samples)
        (OUT/'SyncPoseValidation.json').write_text(json.dumps(result,indent=2));u.log('MENU_NIAGARA_CLIP_PHASE '+result['status'])
    def tick(self,dt):
        try:
            assert c.is_active()
            if time.monotonic()-self.start>40:raise RuntimeError('Clip phase check timeout')
            now=u.GameplayStatics.get_time_seconds(w)
            if self.first is None:self.first=now
            qt=player.get_current_time();frame=qt.time.frame_number.value+qt.time.sub_frame;seconds=frame*qt.rate.denominator/qt.rate.numerator
            if self.prev is not None and frame+3<self.prev:self.wraps+=1
            self.prev=frame
            if now-self.last>.08:
                pose=u.AnimPoseExtensions.get_anim_pose_at_time(clip,seconds,options);bone=u.AnimPoseExtensions.get_bone_pose(pose,'MenuBrush_brush',u.AnimPoseSpaces.WORLD)
                expected=hero.skeletal_mesh_component.get_world_transform().transform_location(bone.transform_location(offset));actual=foam.get_actor_location();error=(actual-expected).length()
                self.samples.append({'frame':frame,'source_clip_seconds':seconds,'foam':list(actual.to_tuple()),'source_clip_contact':list(expected.to_tuple()),'error_cm':error});self.last=now
                assert error<.02,error
            if now-self.first>9.8:
                assert self.wraps>=1
                self.finish()
        except Exception:self.finish(traceback.format_exc())
builtins.mc_menu_niagara_clip_phase_check=Check();print('MENU_NIAGARA_CLIP_PHASE_STARTED')
