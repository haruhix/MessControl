"""Bounded owned solo: saved BroccoliCutA overlaps a stationary worker.

Requires MC_OVERLAP_EDITOR_PID, MC_OVERLAP_LOGFILE and MC_OVERLAP_OUTPUT on E:.
Uses normal landing, then freezes only owned food and shifts only its XY.
No real-cut/screenshot-identity claim; native captured geometry covers that.
No app/build/assets, direct HitFood/intake, collision tuning or player push.
Cancel: builtins.mc_food_overlap_separation_solo.finish('Stopped by operator').
"""
import builtins, hashlib, json, os, runpy, time, traceback
from pathlib import Path
import unreal as u

ROOT=Path('E:/DEVGAME/MessControl')
PID=int(globals()['MC_OVERLAP_EDITOR_PID'])
LOG=Path(globals()['MC_OVERLAP_LOGFILE']).resolve()
OUTPUT=Path(globals()['MC_OVERLAP_OUTPUT']).resolve()
assert os.getpid()==PID and OUTPUT.drive.upper()==LOG.drive.upper()=='E:' and LOG.is_file()
support=runpy.run_path(str(ROOT/'Tools/Unreal/check_food_walking_slab_solo.py'),
    run_name='food_overlap_support',init_globals={'MC_SLAB_EDITOR_PID':PID,
        'MC_SLAB_LOGFILE':str(LOG),'MC_SLAB_OUTPUT':str(OUTPUT)})
Parent,prop,valid,path,vec,asset_ref=(support[k] for k in
    ('FoodWalkingSlabSolo','prop','valid','path','vec','asset_ref'))
KEY='mc_food_overlap_separation_solo'
MESH='/Game/Stylized_Vegetables/Meshes/SM_BroccoliCutA.SM_BroccoliCutA'

class FoodOverlapSeparationSolo(Parent):
    def __init__(self):
        super().__init__()
        self.capture_hz=6.;self.overlap_rows=[]
        self.report.update(mode='food_overlap_stationary_saved_broccoli',
            scope='Saved BroccoliCutA: food-only clearance of a stationary worker, then proxy arming.',
            overlap_samples=self.overlap_rows,two_real_knife_cuts=False,
            screenshot_object_identity_verified=False)
        self.report['limitations'][0]='Authored saved Broccoli fragment, not a real cut or the captured screenshot piece; the retained native Broccoli fixture covers exact geometry separately.'
        self.report['limitations'][4]='Only the owned food is frozen after natural landing and translated in XY; this is not dynamic Chaos, network or performance validation.'
        self.report['source_sha256']['Tools/Unreal/check_food_overlap_separation_solo.py']=hashlib.sha256(
            (ROOT/'Tools/Unreal/check_food_overlap_separation_solo.py').read_bytes()).hexdigest()

    def counters(self):
        values=super().counters()
        values['meal_sequence']=sum(int(prop(t,'meal_sequence')) for t in self.actors(u.MCThroat))
        return values

    def choose_rows(self):
        limits=vec(prop(prop(u.load_asset('/Game/Data/DA_FoodPhysics'),'settings'),'max_auto_consume_dimensions'))
        self.require(all(abs(v-110.)<.001 for v in limits),'Saved110cubed threshold changed')
        raw=u.DataTableFunctionLibrary.export_data_table_to_json_string(u.load_asset('/Game/Data/DT_BreakfastMenu'))
        if isinstance(raw,(tuple,list)):raw=next(v for v in raw if isinstance(v,str))
        rows=[row for row in json.loads(raw) if row.get('Name')=='Broccoli']
        self.require(len(rows)==1,'Expected saved Broccoli row');self.row=rows[0]
        self.require(MESH in [asset_ref(v) for v in self.row.get('FragmentMeshes',[])],'Saved BroccoliCutA membership changed')
        self.mesh=u.load_asset(MESH);self.require(valid(self.mesh),'Saved BroccoliCutA missing')
        self.object_type=u.ObjectTypeQuery.ECC_FOOD_WALKING_OBSTACLE
        self.report['saved_auto_portion_limits_cm']=limits

    def prepare_fragment(self):
        super().prepare_fragment()
        if self.phase=='obstacle_settle':self.stage('overlap_baseline')

    def exact_overlap(self):
        capsule=self.hero.capsule_component
        hits=u.SystemLibrary.component_overlap_components(capsule,capsule.get_world_transform(),
            [u.ObjectTypeQuery.ECC_FOOD_FRAGMENT,u.ObjectTypeQuery.ECC_PHYSICS_BODY],u.PrimitiveComponent,[self.hero])
        return prop(self.food,'body') in (hits or [])

    def begin_overlap(self):
        self.hero_origin=self.hero.get_actor_location();self.hero_health=self.health();self.hero_floor=self.floor_record()
        self.overlap_counters=self.counters();center,extent,_=u.SystemLibrary.get_component_bounds(prop(self.food,'visual'))
        initial=self.food.get_actor_location();rotation=self.food.get_actor_rotation()
        self.food_rotation=(rotation.pitch,rotation.yaw,rotation.roll)
        candidates=[(0.,0.),(.25*extent.x,0.),(-.25*extent.x,0.),(0.,.25*extent.y),(0.,-.25*extent.y)]
        for dx,dy in candidates:
            self.food.set_actor_location(u.Vector(initial.x+self.hero_origin.x-center.x+dx,
                initial.y+self.hero_origin.y-center.y+dy,initial.z),False,True)
            if self.exact_overlap():break
        self.require(self.exact_overlap(),'Food-only XY placement did not actually overlap its physical hull')
        self.food_origin=self.food.get_actor_location()
        self.report['overlap_baseline']=dict(hero_cm=vec(self.hero_origin),health=self.hero_health,
            floor=self.hero_floor,counters=self.overlap_counters,food_cm=vec(self.food_origin),
            exact_food_body_overlap=True,player_moved_after_baseline=False)
        self.stage('overlap_separation')

    def observe_separation(self,now):
        position=self.hero.get_actor_location();velocity=self.hero.get_velocity();food=self.food.get_actor_location()
        floor=self.floor_record();rotation=self.food.get_actor_rotation()
        row=dict(time=now,phase=self.phase,hero_cm=vec(position),velocity_cm_s=vec(velocity),
            hero_displacement_cm=float((position-self.hero_origin).length()),food_cm=vec(food),
            food_translation_cm=vec(food-self.food_origin),exact_overlap=self.exact_overlap(),
            proxy_active=self.blocker_is_active(),floor=floor,health=self.health(),
            standing=prop(self.hero,'tooth_physics').get_body_state()==u.MCBodyState.STANDING)
        self.overlap_rows.append(row)
        self.require(row['hero_displacement_cm']<=1. and velocity.length()<=1.,'Food clearance moved or accelerated the stationary worker')
        self.require(row['standing'] and abs(row['health']-self.hero_health)<.01,'Food clearance changed health/body state')
        self.require(prop(self.hero.get_movement_component(),'movement_mode')==u.MovementMode.MOVE_WALKING
            and floor['walkable'] and floor['component']==self.hero_floor['component'],'Worker lost original world ground')
        self.require(abs(food.z-self.food_origin.z)<.1 and all(abs(a-b)<.02 for a,b in
            zip((rotation.pitch,rotation.yaw,rotation.roll),self.food_rotation)),'Clearance altered food Z/rotation')
        self.require(self.counters()==self.overlap_counters,'Clearance changed intake/reward counters')
        return row

    def tick(self,delta):
        if self.done or self.phase not in ('overlap_baseline','overlap_separation','overlap_tail'):return super().tick(delta)
        try:
            self.require(time.monotonic()-self.started<210.,'Owned overlap probe exceeded210 wall seconds')
            self.require(self.owned_pie_is_active(),'Owned PIE stopped/replaced')
            now=float(u.GameplayStatics.get_time_seconds(self.world))
            if self.last_game_time is not None and now<=self.last_game_time:return
            self.last_game_time=now;elapsed=now-self.phase_at
            if now>=self.sample_at:self.sample_at=now+.08;self.observe(now)
            if now>=self.frame_at:
                self.frame_at=now+1/self.capture_hz;self.screenshot('%05d.png'%len(self.report['frames']),True)
            if self.phase=='overlap_baseline':
                self.require(elapsed<2.,'Stationary worker did not settle on world floor')
                if elapsed>=.4 and self.floor_record()['walkable'] and self.hero.get_velocity().length()<=1.:self.begin_overlap()
                return
            row=self.observe_separation(now)
            if self.phase=='overlap_separation':
                self.require(elapsed<8.,'Food remained overlapping or failed to arm within8 seconds')
                if not row['exact_overlap'] and row['proxy_active']:
                    self.require((self.food.get_actor_location()-self.food_origin).length()>1.,'Food-only separation did not move food')
                    self.stage('overlap_tail')
            elif elapsed>=.4:
                self.require(not row['exact_overlap'] and row['proxy_active'],'Cleared/armed state did not persist')
                self.report['checks'].update(real_saved_broccoli_fragment=True,natural_landing=True,
                    initial_exact_physical_overlap=True,food_only_xy_separation=True,stationary_player_preserved=True,
                    original_ground_health_standing_preserved=True,proxy_armed_after_clear=True,counters_unchanged=True)
                self.finish()
        except Exception:self.finish(traceback.format_exc())

def main():
    old=getattr(builtins,KEY,None)
    if old is not None and not getattr(old,'done',True):raise RuntimeError('Preserve active owned overlap probe')
    probe=FoodOverlapSeparationSolo();setattr(builtins,KEY,probe)
    try:probe.start()
    except Exception:probe.finish(traceback.format_exc());raise
    return probe

if __name__=='__main__':main()
