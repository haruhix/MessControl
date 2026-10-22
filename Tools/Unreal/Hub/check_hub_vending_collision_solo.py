"""Focused existing-vending collision check on its real shop porch.

Solo01 reached Shop stairs before the vending mesh. This probe explicitly places
ONLY its owned PIE pawn on a queried walkable, capsule-clear porch point after
the real spawn, then uses ordinary movement to press the original vending mesh.
This is a disclosed local fixture, not proof that stairs can be walked from spawn.
No prop, collision, timer, HP, map or package is edited. No blockers are ignored
by movement or by the target capsule query. E is observed, never activated.
"""
import builtins
import importlib.util
import math
from pathlib import Path
import traceback

import unreal as u


ROOT=Path('E:/DEVGAME/MessControl')
OUT=Path(globals().get('MCHUB_VENDING_COLLISION_OUTPUT',
    'E:/CodexWork/HubWardrobeRewards_20261022/StationCollision/VendorSolo02')).resolve()
KEY='mc_hub_vending_collision_solo'
spec=importlib.util.spec_from_file_location('mc_vending_collision_base',
    ROOT/'Tools/Unreal/Hub/check_hub_station_collision_solo.py')
station=importlib.util.module_from_spec(spec)
station.MCHUB_STATION_COLLISION_OUTPUT=str(OUT)
station.MCHUB_STATION_COLLISION_CAPTURE_HZ=3
station.MCHUB_STATION_COLLISION_MAX_FRAMES=90
spec.loader.exec_module(station)
require,valid,path,vec=station.require,station.valid,station.path,station.vec


class VendingCollisionSolo(station.StationCollisionSolo):
    def __init__(self):
        super().__init__()
        self.report.update(scope=__doc__,teleports=True,review_fixtures=[],candidate_queries=[],
            source_sha256=station.base.hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
            limitations=['ONE disclosed owned-pawn placement on the actual queried Shop porch.',
                'This does not establish that the real Shop stairs are traversable from spawn.',
                'Vending collision is original ComplexAsSimple; no geometry/collision override or blocker exclusion.',
                'No E action, network, cook/package, build or full Hub walkthrough.',
                'Native 3Hz captures are repeated for video; no motion interpolation.'])

    def initialize_world(self,worlds):
        if not worlds:
            return False
        require(len(worlds)==1 and u.SystemLibrary.is_server(worlds[0]),'Expected one standalone authority Hub')
        if self.world is None:
            self.world=worlds[0];self.instance=u.GameplayStatics.get_game_instance(self.world)
            require(u.GameplayStatics.get_current_level_name(self.world,True)=='Hub','Expected saved Hub')
        require(worlds[0]==self.world and valid(self.instance),'Owned PIE was replaced')
        self.pc=u.GameplayStatics.get_player_controller(self.world,0)
        if not valid(self.pc):
            return False
        self.hero=self.pc.get_controlled_pawn()
        if not isinstance(self.hero,u.MCToothCharacter) or self.now()<2. or not self.pc.is_world_presentation_ready() or self.pc.is_move_input_ignored():
            return False
        self.pawn_path=path(self.hero);self.initial_spawn=self.hero.get_actor_location()
        self.radius=float(self.hero.capsule_component.get_scaled_capsule_radius())
        self.half_height=float(self.hero.capsule_component.get_scaled_capsule_half_height())
        self.statics=u.get_default_object(u.GameplayStatics)
        self.scene={actor.get_name():actor for actor in u.GameplayStatics.get_all_actors_of_class(self.world,u.Actor)}
        shop=self.scene.get('StaticMeshActor_103');porch=self.scene.get('StaticMeshActor_8')
        require(valid(shop) and valid(porch),'Original vending and Shop geometry required')
        stations=list(u.GameplayStatics.get_all_actors_of_class(self.world,u.MCHubShopStation))
        require(len(stations)==1,'Expected one real vending interaction station')
        component=shop.get_component_by_class(u.StaticMeshComponent)
        toward=u.MathLibrary.inverse_transform_location(component.get_world_transform(),stations[0].get_actor_location())
        axis=0 if abs(toward.x)>abs(toward.y) else 1;sign=1 if vec(toward)[axis]>0 else -1
        self.add_test('VendingBody',shop,axis,sign)
        self.porch=porch
        self.report.update(world=path(self.world),pawn=self.pawn_path,spawn_cm=vec(self.initial_spawn),capsule_radius_cm=self.radius)
        self.stage('vending_spawn_observe','VendorActualSpawn');self.capture('00_ActualSpawn.png')
        return True

    def place_on_real_porch(self):
        test=self.current_test();normal=test['normal'];side=u.Vector(-normal.y,normal.x,0.)
        profile=self.hero.capsule_component.get_collision_profile_name()
        candidate=None
        for clearance in (95.,85.,75.,65.,55.,45.):
            for lateral in (0.,25.,-25.,50.,-50.):
                point=test['face']+normal*clearance+side*lateral
                hit=u.SystemLibrary.line_trace_single(self.world,point+u.Vector(0.,0.,450.),point-u.Vector(0.,0.,500.),
                    u.TraceTypeQuery.TRACE_TYPE_QUERY1,False,[self.hero],u.DrawDebugTrace.NONE,True)
                parts=self.statics.call_method('BreakHitResult',args=(hit,)) if hit else None
                row={'outward_distance_cm':clearance,'lateral_cm':lateral,
                    'floor_actor':path(parts[9]) if parts else None,
                    'floor_point_cm':vec(parts[5]) if parts else None,
                    'floor_normal':vec(parts[7]) if parts else None}
                self.report['candidate_queries'].append(row)
                if not parts or not parts[0] or parts[9]!=self.porch or parts[7].z<.75:
                    continue
                center=parts[5]+u.Vector(0.,0.,self.half_height+3.)
                clear=u.SystemLibrary.capsule_trace_single_by_profile(self.world,center,center+u.Vector(0.,0.,.1),
                    self.radius,self.half_height,profile,False,[self.hero],u.DrawDebugTrace.NONE,True)
                obstruction=self.statics.call_method('BreakHitResult',args=(clear,)) if clear else None
                row['capsule_clear']=not bool(obstruction and obstruction[0])
                if not row['capsule_clear']:
                    continue
                target=u.SystemLibrary.capsule_trace_single_by_profile(self.world,center,center-normal*240.,
                    self.radius,self.half_height,profile,False,[self.hero],u.DrawDebugTrace.NONE,True)
                contact=self.statics.call_method('BreakHitResult',args=(target,)) if target else None
                row['first_forward_hit_actor']=path(contact[9]) if contact else None
                row['forward_hit_is_target']=bool(contact and contact[0] and not contact[1] and contact[9]==test['actor'])
                if not row['forward_hit_is_target']:
                    continue
                expected=(contact[4]-test['face']).x*normal.x+(contact[4]-test['face']).y*normal.y
                row['forward_travel_to_target_cm']=float(clearance-expected)
                if clearance-expected<15.:
                    continue
                candidate=center;break
            if candidate is not None:
                break
        require(candidate is not None,'No actual walkable capsule-clear Shop porch point with unobstructed vending approach was found')
        before=self.hero.get_actor_location()
        self.hero.character_movement.stop_movement_immediately()
        self.hero.set_actor_location(candidate,False,True)
        require((self.hero.get_actor_location()-candidate).length()<.5,'Explicit owned-pawn fixture did not reach selected clear point')
        test['approach']=candidate
        test['row']['approach_xy_cm']=[float(candidate.x),float(candidate.y)]
        self.report['review_fixtures'].append({'kind':'ONE owned pawn placement; no props changed',
            'reason':'Solo01 front route capsule hit existing Shop stair riser before vending; isolate the actual original vending body',
            'before_cm':vec(before),'after_cm':vec(candidate),'floor_actor':path(self.porch),
            'floor_and_capsule_clear_candidate':row,'not_a_spawn_route_proof':True})
        self.aim_review_camera();self.stage('vending_fixture_settle','VendorPorchContact')
        self.capture('01_DeclaredPorchPlacement.png')

    def advance(self):
        age=self.now()-self.phase_at
        if self.phase=='vending_spawn_observe' and age>=.6:
            self.place_on_real_porch()
        elif self.phase=='vending_fixture_settle' and age>=.6:
            self.inspect_approach()
            self.push_start=self.hero.get_actor_location();self.minimum_signed=self.signed_distance()
            self.check('VendingBody_fixture_allows_real_forward_movement',
                self.minimum_signed-self.current_test()['expected_contact']>15.)
            self.stage('push_contact')
        elif self.phase=='push_contact':
            self.minimum_signed=min(self.minimum_signed,self.signed_distance())
            normal=self.current_test()['normal']
            self.hero.add_movement_input(u.Vector(-normal.x,-normal.y,0.),1.,False)
            if age>=1.5:
                self.hero.character_movement.stop_movement_immediately();self.stage('vending_contact_settle')
        elif self.phase=='vending_contact_settle' and age>=.3:
            test=self.current_test();distance=self.signed_distance();expected=test['expected_contact']
            movement=(self.hero.get_actor_location()-self.push_start).length()
            self.minimum_signed=min(self.minimum_signed,distance)
            test['row'].update(push_start_cm=vec(self.push_start),contact_cm=vec(self.hero.get_actor_location()),
                approach_movement_cm=float(movement),end_signed_distance_cm=distance,
                minimum_signed_distance_cm=self.minimum_signed,capsule_radius_cm=self.radius)
            self.check('VendingBody_reaches_original_mesh_surface',movement>15. and abs(distance-expected)<20.)
            self.check('VendingBody_blocks_capsule_not_pass_through',self.minimum_signed>=expected-10.)
            self.capture('02_OriginalVendingBlocksPlayer.png')
            self.prepare_walk(test['row']['approach_xy_cm'],'vending_walk_backoff')
        elif self.phase=='vending_walk_backoff':
            target=self.current_test()['approach'];current=self.hero.get_actor_location()
            delta=target-current;length=math.hypot(delta.x,delta.y)
            if length>8.:
                self.hero.add_movement_input(u.Vector(delta.x/length,delta.y/length,0.),1.,False)
            else:
                self.hero.character_movement.stop_movement_immediately()
                self.check('VendingBody_can_back_away',self.signed_distance()>self.current_test()['expected_contact']+12.)
                self.capture('03_VendingBackedAway.png');self.begin_finish()


def main():
    previous=getattr(builtins,KEY,None)
    require(previous is None or getattr(previous,'done',True),'A focused vending probe is already active')
    probe=VendingCollisionSolo();setattr(builtins,KEY,probe)
    try:
        probe.start()
    except Exception:
        probe.begin_finish(traceback.format_exc())
    return probe


if __name__=='__main__':
    main()
