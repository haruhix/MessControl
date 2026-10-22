"""Owned short actual-walking test of four cabin doors and the real vending front.

Uses the existing Hub solo lifecycle/capture helpers unchanged. Requires idle,
saved Hub and -MCHubOwned. Walks normally from the real spawn, presses against
the actual prop surface for 1.5 seconds, then backs away. No teleport, spawned
test actors, collision overrides, E/UI interaction, asset save, build or peers.
Optional MCHUB_STATION_COLLISION_APPROACH supplies inspected XY approach routes
per test name; a blocked approach fails instead of moving the pawn through props.
"""
import builtins
import importlib.util
import math
from pathlib import Path
import traceback

import unreal as u


ROOT=Path('E:/DEVGAME/MessControl')
OUT=Path(globals().get('MCHUB_STATION_COLLISION_OUTPUT',
    'E:/CodexWork/HubWardrobeRewards_20261022/StationCollision/Solo01')).resolve()
KEY='mc_hub_station_collision_solo'
spec=importlib.util.spec_from_file_location('mc_station_collision_owned_base',
    ROOT/'Tools/Unreal/Hub/check_hub_pier_fishing_solo.py')
base=importlib.util.module_from_spec(spec);base.MCHUB_PIER_OUTPUT=str(OUT);spec.loader.exec_module(base)
require,valid,path,vec=base.require,base.valid,base.path,base.vec


class StationCollisionSolo(base.HubPierSolo):
    def __init__(self):
        super().__init__()
        self.tests=[];self.test_index=0;self.route=[];self.route_index=0
        self.capture_hz=max(0.,min(10.,float(globals().get('MCHUB_STATION_COLLISION_CAPTURE_HZ',3))))
        self.max_frames=max(0,min(600,int(globals().get('MCHUB_STATION_COLLISION_MAX_FRAMES',450))))
        self.approach_routes=globals().get('MCHUB_STATION_COLLISION_APPROACH',{})
        require(isinstance(self.approach_routes,dict),'Approach routes must be a test-name to XY route mapping')
        self.report.update(scope=__doc__,capture_hz=self.capture_hz,tests=[],teleports=False,
            source_sha256=base.hashlib.sha256((ROOT/'Tools/Unreal/Hub/check_hub_station_collision_solo.py').read_bytes()).hexdigest(),
            limitations=['Actual pawn AddMovementInput tests collision against scene props; no physical WASD input is simulated.',
                'Four fronts test the individual real closed doors; the fifth tests the existing vending complex collision.',
                'Cabin side/back/top faces are not reached by this short route; their simple-shape inspection is separate evidence.',
                'No wardrobe/shop E action, networking, cook/package or broad level test.',
                'Videos repeat actual native captures at the reported cadence; successful assertions are not art approval.'])

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
        for index,(body_id,door_id) in enumerate(((109,108),(110,111),(113,114),(112,115)),1):
            body=self.scene.get('StaticMeshActor_'+str(body_id));door=self.scene.get('StaticMeshActor_'+str(door_id))
            require(valid(body) and valid(door),'Actual cabin pair is missing')
            body_component=body.get_component_by_class(u.StaticMeshComponent)
            relative=u.MathLibrary.inverse_transform_location(body_component.get_world_transform(),door.get_actor_location())
            axis=0 if abs(relative.x)>abs(relative.y) else 1;sign=1 if vec(relative)[axis]>0 else -1
            self.add_test('Cabin'+str(index)+'Door',door,axis,sign)
        shop=self.scene.get('StaticMeshActor_103');require(valid(shop),'Actual vending mesh actor is missing')
        stations=list(u.GameplayStatics.get_all_actors_of_class(self.world,u.MCHubShopStation))
        require(len(stations)==1,'Expected the existing vending interaction station')
        component=shop.get_component_by_class(u.StaticMeshComponent)
        toward=u.MathLibrary.inverse_transform_location(component.get_world_transform(),stations[0].get_actor_location())
        axis=0 if abs(toward.x)>abs(toward.y) else 1;sign=1 if vec(toward)[axis]>0 else -1
        self.add_test('VendingBody',shop,axis,sign)
        self.report.update(world=path(self.world),pawn=self.pawn_path,spawn_cm=vec(self.initial_spawn),capsule_radius_cm=self.radius)
        self.stage('spawn_observe','StationCollisionSpawn');self.capture('00_ActualSpawn.png')
        return True

    def add_test(self,name,actor,axis,sign):
        component=actor.get_component_by_class(u.StaticMeshComponent)
        require(component and component.static_mesh,'Actual prop has no visible mesh')
        minimum,maximum=component.get_local_bounds();low,high=vec(minimum),vec(maximum)
        surface=[(a+b)*.5 for a,b in zip(low,high)];surface[axis]=high[axis] if sign>0 else low[axis]
        transform=component.get_world_transform();face=u.MathLibrary.transform_location(transform,u.Vector(*surface))
        local_axis=[0.,0.,0.];local_axis[axis]=float(sign)
        point=u.MathLibrary.transform_location(transform,u.Vector(*surface)+u.Vector(*local_axis))
        delta=point-face;length=math.hypot(delta.x,delta.y);require(length>.01,'Invalid prop normal')
        normal=u.Vector(delta.x/length,delta.y/length,0.)
        approach=face+normal*(self.radius+155.)
        row={'name':name,'actor':path(actor),'mesh':component.static_mesh.get_path_name(),
             'front_axis':'XY'[axis],'front_sign':sign,'face_center_cm':vec(face),'outward_xy':vec(normal),
             'approach_xy_cm':[float(approach.x),float(approach.y)],
             'pawn_response':str(component.get_collision_response_to_channel(u.CollisionChannel.ECC_PAWN))}
        self.tests.append({'row':row,'actor':actor,'face':face,'normal':normal,'approach':approach})
        self.report['tests'].append(row)

    def current_test(self):
        return self.tests[self.test_index]

    def signed_distance(self):
        test=self.current_test();delta=self.hero.get_actor_location()-test['face']
        return float(delta.x*test['normal'].x+delta.y*test['normal'].y)

    def sample(self):
        self.report['samples'].append({'phase':self.phase,'game_seconds':self.now(),
            'position_cm':vec(self.hero.get_actor_location()),'velocity_cm_s':vec(self.hero.get_velocity()),
            'test':self.current_test()['row']['name'] if self.tests and self.test_index<len(self.tests) else None,
            'surface_signed_distance_cm':self.signed_distance() if self.tests and self.test_index<len(self.tests) else None})

    def begin_test(self):
        test=self.current_test();name=test['row']['name']
        route=self.approach_routes.get(name,[])
        require(isinstance(route,list) and all(len(point)==2 and all(math.isfinite(float(value)) for value in point) for point in route),
                'Invalid explicit approach route for '+name)
        self.route=list(route)+[[test['approach'].x,test['approach'].y]];self.route_index=0
        self.prepare_walk(self.route[0],'walk_approach',name)

    def aim_review_camera(self):
        """Use the ordinary orbit-input API so each real contact is visible."""
        camera=self.hero.get_editor_property('Camera');view=camera.get_world_rotation()
        normal=self.current_test()['normal']
        yaw=math.degrees(math.atan2(-normal.y,-normal.x))
        difference=(yaw-float(view.yaw)+180.)%360.-180.
        sensitivity=max(.02,min(1.,float(self.hero.get_editor_property('CameraOrbitSensitivity'))))
        self.hero.apply_camera_orbit_input(u.Vector2D(difference/sensitivity,(-18.-float(view.pitch))/sensitivity))
        self.current_test()['row']['review_camera']={'method':'normal ApplyCameraOrbitInput',
            'yaw_target':yaw,'pitch_target':-18.,'look_input_ignored':bool(self.pc.is_look_input_ignored())}

    def inspect_approach(self):
        """Record the real walkable floor and query the same capsule/profile.

        These are read-only queries at the naturally reached pawn position;
        no review placement, movement disabling or scene geometry is used.
        """
        test=self.current_test();row=test['row'];name=row['name']
        floor=self.hero.character_movement.get_editor_property('CurrentFloor')
        floor_hit=self.statics.call_method('BreakHitResult',args=(floor.get_editor_property('hit_result'),))
        row['approach_floor']={'blocking':bool(floor.get_editor_property('blocking_hit')),
            'walkable':bool(floor.get_editor_property('walkable_floor')),
            'floor_distance_cm':float(floor.get_editor_property('floor_dist')),
            'actor':path(floor_hit[9]),'component':path(floor_hit[10]),
            'impact_point_cm':vec(floor_hit[5]),'normal':vec(floor_hit[7])}
        self.check(name+'_approach_has_actual_walkable_floor',
            row['approach_floor']['blocking'] and row['approach_floor']['walkable'] and floor_hit[7].z>.7)
        start=self.hero.get_actor_location()+u.Vector(0.,0.,3.)
        profile=self.hero.capsule_component.get_collision_profile_name()
        clear=u.SystemLibrary.capsule_trace_single_by_profile(self.world,start,start+u.Vector(0.,0.,.1),
            self.radius,self.half_height,profile,False,[self.hero],u.DrawDebugTrace.NONE,True)
        clear_parts=self.statics.call_method('BreakHitResult',args=(clear,)) if clear else None
        row['approach_clearance']={'center_cm':vec(start),'radius_cm':self.radius,
            'half_height_cm':self.half_height,'profile':str(profile),'raised_query_cm':3.,
            'blocking':bool(clear_parts and clear_parts[0]),
            'actor':path(clear_parts[9]) if clear_parts else None}
        self.check(name+'_approach_capsule_is_clear',not row['approach_clearance']['blocking'])
        normal=test['normal'];end=start-normal*(self.radius+260.)
        hit=u.SystemLibrary.capsule_trace_single_by_profile(self.world,start,end,self.radius,self.half_height,
            profile,False,[self.hero],u.DrawDebugTrace.NONE,True)
        require(hit,'Actual target surface did not block the player-profile capsule query: '+name)
        parts=self.statics.call_method('BreakHitResult',args=(hit,))
        impact_normal=parts[7]
        normal_dot=float(impact_normal.x*normal.x+impact_normal.y*normal.y)
        expected=float((parts[4]-test['face']).x*normal.x+(parts[4]-test['face']).y*normal.y)
        row['contact_query']={'blocking':bool(parts[0]),'initial_overlap':bool(parts[1]),
            'actor':path(parts[9]),'component':path(parts[10]),'capsule_center_at_hit_cm':vec(parts[4]),
            'impact_point_cm':vec(parts[5]),'impact_normal':vec(impact_normal),'outward_dot_xy':normal_dot,
            'expected_signed_capsule_distance_cm':expected,'complex_query':False}
        self.check(name+'_swept_capsule_hits_exact_prop_front',
            bool(parts[0]) and not parts[1] and parts[9]==test['actor'] and normal_dot>.5)
        test['expected_contact']=expected

    def advance(self):
        age=self.now()-self.phase_at
        if self.phase=='spawn_observe' and age>=.8:
            self.begin_test()
        elif self.phase=='walk_approach' and self.walk():
            self.route_index+=1
            if self.route_index<len(self.route):
                self.prepare_walk(self.route[self.route_index],'walk_approach')
            else:
                self.aim_review_camera()
                self.stage('approach_settle')
        elif self.phase=='approach_settle' and age>=.4:
            self.inspect_approach()
            self.push_start=self.hero.get_actor_location();self.minimum_signed=self.signed_distance()
            self.check(self.current_test()['row']['name']+'_starts_outside_surface',self.minimum_signed>self.radius+60.)
            self.stage('push_contact')
        elif self.phase=='push_contact':
            self.minimum_signed=min(self.minimum_signed,self.signed_distance())
            normal=self.current_test()['normal']
            self.hero.add_movement_input(u.Vector(-normal.x,-normal.y,0.),1.,False)
            if age>=1.5:
                self.hero.character_movement.stop_movement_immediately();self.stage('contact_settle')
        elif self.phase=='contact_settle' and age>=.25:
            test=self.current_test();name=test['row']['name'];distance=self.signed_distance()
            movement=(self.hero.get_actor_location()-self.push_start).length()
            self.minimum_signed=min(self.minimum_signed,distance)
            test['row'].update(push_start_cm=vec(self.push_start),contact_cm=vec(self.hero.get_actor_location()),
                approach_movement_cm=float(movement),end_signed_distance_cm=distance,
                minimum_signed_distance_cm=self.minimum_signed,capsule_radius_cm=self.radius)
            expected=test['expected_contact']
            self.check(name+'_reaches_the_actual_surface',movement>40. and abs(distance-expected)<=20.)
            self.check(name+'_blocks_capsule_not_pass_through',self.minimum_signed>=expected-10.)
            self.capture('%02d_%s_Blocked.png'%(self.test_index+1,name))
            self.prepare_walk(test['row']['approach_xy_cm'],'walk_backoff')
        elif self.phase=='walk_backoff' and self.walk():
            name=self.current_test()['row']['name']
            self.check(name+'_can_back_away',self.signed_distance()>self.radius+60.)
            self.test_index+=1
            if self.test_index<len(self.tests):
                self.begin_test()
            else:
                self.begin_finish()


def main():
    previous=getattr(builtins,KEY,None)
    require(previous is None or getattr(previous,'done',True),'A station collision probe is already active')
    probe=StationCollisionSolo();setattr(builtins,KEY,probe)
    try:
        probe.start()
    except Exception:
        probe.begin_finish(traceback.format_exc())
    return probe


if __name__=='__main__':
    main()
