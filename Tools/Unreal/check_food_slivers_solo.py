"""One owned solo L_Mouth PIE: ordinary Knife cuts and real E collection.

Run in an owned, idle transient-profile
Editor after the current C++ build. No package/profile saves, extra clients,
build, cook, direct HitFood or direct collection calls. Default-scale Fibre is
cut by normal held primary input; a second transient 200x100x80 cm fixture makes
the ready-size handoff efficient. Both paths reach native HitFood through the
ordinary tool mechanic. HitFood/Collect are not reflected Python APIs.

Cancel: builtins.mc_food_slivers_solo.finish('Stopped by operator').
"""
import builtins
import datetime
import hashlib
import json
import math
import runpy
import time
import traceback
from pathlib import Path

import unreal as u

ROOT=Path('E:/DEVGAME/MessControl')
OUTPUT=Path('E:/CodexWork/MessControl/Validation/FoodSlivers_20261107/Solo')
RUN=OUTPUT/'SoloRuns'/datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%SZ')
support=runpy.run_path(str(ROOT/'Tools/Unreal/check_approved_environment_solo.py'),
    run_name='food_portion_support',init_globals={'MC_ENV_OUTPUT':RUN,'MC_ENV_CAPTURE_HZ':8})
Base,prop,vec=(support[k] for k in ('ApprovedEnvironmentSolo','prop','vec'))
KEY='mc_food_slivers_solo'
DEFAULT_BATCH=917071
READY_BATCH=917072


def valid(obj):
    return obj is not None and u.SystemLibrary.is_valid(obj)


def path(obj):
    return obj.get_path_name() if valid(obj) else None


def asset_ref(value):
    return value.split("'",1)[1].rstrip("'") if "'" in value else value


class FoodPortionsSolo(Base):
    def __init__(self):
        assert 'mchubtransientprofile' in u.SystemLibrary.get_command_line().lower(), \
            'Use the owned -MCHubTransientProfile Editor; user profiles are left untouched'
        super().__init__()
        self.scheduler=None
        self.scheduler_tick=None
        self.row=None
        self.mesh=None
        self.fixtures=[]
        self.batch=DEFAULT_BATCH
        self.clip=None
        self.capture=None
        self.capture_target=None
        self.capture_request=None
        self.capture_ticks=0
        self.setup_poll=0.
        self.next_select_at=0.
        self.knife_verified_at=None
        self.observed_ready_at=None
        self.health_observations={}
        self.health_drop_count=0
        self.sliver_cut_count=0
        self.sliver_before=[]
        self.sliver_target=None
        self.sliver_yaws=[30.,150.]
        self.report.setdefault("sliver_cuts",[])
        self.report.pop('fixture_tuning',None)
        self.report.pop('tuning_reason',None)
        self.report.update(scope='One owned standalone authority PIE on current L_Mouth; real Knife and ServerToggleFoodCollection input',
            mode='food_slivers',run_directory=str(RUN),clips={},samples=[],checks={},fixtures=[],
            limitations=[
                'Default-scale Fibre retains saved Scale, FragmentScale, Health, Mass, Fragments, Resistance, freshness and source mesh. Simple row fields are copied to a transient actor; native CollisionData and other nested fields are not copied or claimed as validated.',
                'The second transient fixture alone uses full dimensions 200x100x80 cm, preserving source triangles while using a diagnostic nonuniform mesh scale.',
                'Ordinary Knife primary input drives the original cut and two additional oblique cuts at different actual hero yaw angles; no food transforms are authored after spawn. A controlled small fixture then exercises carrying. HitFood and Collect are C++ APIs without UFUNCTION exposure; direct calls are covered by the native tests instead.',
                'Whole fixture physics is temporarily disabled for stable tool contact. Generated halves are frozen briefly for geometry/pickup checks, then ready pieces resume normal actor tick for the real handoff.',
                'The existing single-day director is paused only inside this owned PIE to isolate the check. This is not an earned/balanced playthrough.',
                'Motion frames come from the actual gameplay camera and exact PIE window. Front and side close views use temporary review CameraActors in the same actual game viewport, with no fake geometry or extra lighting.',
                'No content/profile save, build, cook/package or multiplayer validation is performed.'])
        self.report['native_diagnostic_availability']=dict(
            CanWork='Not a UFUNCTION; observed work gates are reported explicitly, without claiming a native return value',
            ConfirmedHitCount='Not a UPROPERTY; health-drop observations and generated cuts are recorded instead of a fabricated native counter',
            bArenaArrivalHeld='Protected Python read rejected; official ToolsetLibrary get_object_properties returned no field in the idle CDO probe. Unknown, not forced.',
            bPrimaryHeld='Protected Python read unavailable; official ToolsetLibrary get_object_properties returned no field in the idle CDO probe. Requested input is recorded separately.')
        self.report['source_sha256']={name:hashlib.sha256((ROOT/name).read_bytes()).hexdigest() for name in (
            'Source/MessControl/Private/MCFoodCutPrototype.cpp','Source/MessControl/Private/MCFoodActor.cpp',
            'Source/MessControl/Private/MCFoodCollectionComponent.cpp','Source/MessControl/Private/Tests/MCFoodPortionTests.cpp')}

    def write(self):
        self.output.mkdir(parents=True,exist_ok=True)
        text=json.dumps(self.report,ensure_ascii=False,indent=2,allow_nan=False)+'\n'
        (self.output/'Report.json').write_text(text,encoding='utf8')
        OUTPUT.mkdir(parents=True,exist_ok=True)
        (OUTPUT/'LatestSoloReport.json').write_text(text,encoding='utf8')

    def spawn(self,cls,pose):
        actor=self.statics.call_method('BeginDeferredActorSpawnFromClass',args=(self.world,cls,pose,
            u.SpawnActorCollisionHandlingMethod.ALWAYS_SPAWN,None,u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
        self.require(valid(actor),'Deferred spawn failed')
        return actor

    def finish_spawn(self,actor,pose):
        self.statics.call_method('FinishSpawningActor',args=(actor,pose,u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
        self.fixtures.append(actor)
        return actor

    def pieces(self,batch=None):
        return [a for a in self.actors(u.MCFoodActor) if int(prop(a,'batch'))==(self.batch if batch is None else batch)
            and bool(prop(a,'fragment')) and prop(a,'phase')!=u.MCFoodPhase.DISPOSED]

    def geometry(self,food):
        mesh=prop(food,'cut_visual') or prop(food,'visual')
        center,extent,_=u.SystemLibrary.get_component_bounds(mesh)
        return dict(actor=path(food),batch=int(prop(food,'batch')),phase=str(prop(food,'phase')),
            fragment=bool(prop(food,'fragment')),mesh=path(prop(food,'item_mesh')),
            root_cm=vec(food.get_actor_location()),visible_center_cm=vec(center),visible_extent_cm=vec(extent),
            hand_size_cm=vec(food.get_hand_portion_size()),fits_hand=bool(food.fits_hand_portion()),
            food_mass_kg=float(prop(prop(food,'food_data'),'mass')),body_mass_kg=float(prop(food,'body').get_mass()),
            health=float(prop(food,'health')),cut_visual_sections=int(mesh.get_num_sections()) if prop(food,'cut_visual') else 0,
            stack_carrier=path(prop(food,'stack_carrier')))

    def load_row(self):
        table=u.load_asset('/Game/Data/DT_BreakfastMenu')
        self.require(valid(table),'Breakfast menu missing')
        data=u.DataTableFunctionLibrary.export_data_table_to_json_string(table)
        if isinstance(data,(tuple,list)): data=next(v for v in data if isinstance(v,str))
        rows=json.loads(data)
        matches=[r for r in rows if r.get('Name')=='Fibre']
        self.require(len(matches)==1,'Current menu must contain exactly one Fibre row')
        self.row=dict(matches[0])
        choices=[p for p in self.row['WholeMeshes'] if p and p!='None']
        self.require(choices,'Current Fibre row has no source mesh')
        self.mesh=u.load_asset(asset_ref(choices[0]))
        self.require(valid(self.mesh),'Current Fibre mesh failed to load')
        self.report['saved_fibre_row']=self.row

    def supported_patch(self):
        center,extent,_=u.SystemLibrary.get_component_bounds(prop(self.tongue,'surface'))
        zones=[]
        for zone in self.actors(u.MCFoodDisposal):
            volume=prop(zone,'volume')
            zones.append((volume.get_world_transform(),volume.get_unscaled_box_extent()))
        for dx,dy in ((0,0),(.15,0),(-.15,0),(0,.15),(0,-.15),(.22,.15),(-.22,-.15)):
            floor=self.floor(center+u.Vector(extent.x*dx,extent.y*dy,0))
            if floor is None: continue
            excluded=False
            for transform,half in zones:
                local=transform.inverse_transform_location(floor)
                scale=transform.scale3d
                if abs(local.x)<half.x+350/max(.01,abs(scale.x)) and abs(local.y)<half.y+350/max(.01,abs(scale.y)):
                    excluded=True
            if excluded: continue
            supports=[self.floor(floor+u.Vector(x,y,0)) for x,y in ((0,0),(250,0),(-250,0),(0,300),(0,-300),(150,-400))]
            if all(p is not None and abs(p.z-floor.z)<65 for p in supports):
                self.report['supported_patch_cm']=[vec(p) for p in supports]
                return floor,max(p.z for p in supports)
        raise RuntimeError('No supported interior patch outside delivery zones; inspect map before retrying')

    def fixture(self,controlled=False):
        self.batch=READY_BATCH if controlled else DEFAULT_BATCH
        self.food=None
        row=json.loads(json.dumps(self.row))
        name=row.pop('Name')
        raw=self.mesh.get_bounding_box()
        native=raw.max-raw.min
        if controlled:
            row['Scale']={'X':200/native.x,'Y':100/native.y,'Z':80/native.z}
            row['FragmentScale']=dict(row['Scale'])
            row['Health']=100.
        scale=row['Scale']
        ext=u.Vector(native.x*scale['X'],native.y*scale['Y'],native.z*scale['Z'])*.5
        floor,support_z=self.supported_patch()
        pose=u.Transform(location=u.Vector(floor.x,floor.y,support_z+ext.z+8))
        food=self.spawn(u.MCFoodActor.static_class(),pose)
        # Native-only CollisionData payloads and non-Python cut-plane structs
        # cannot safely roundtrip through the generic JSON property serializer.
        # Copy known supported typed fields before ordinary BeginPlay instead.
        data=food.get_editor_property('food_data')
        for field,key in (('scale','Scale'),('fragment_scale','FragmentScale')):
            value=row[key]
            data.set_editor_property(field,u.Vector(*[float(value[k]) for k in ('X','Y','Z')]))
        for field,key in (('health','Health'),('mass','Mass'),('fragments','Fragments'),
                ('spoil_seconds','SpoilSeconds'),('absorb_seconds','AbsorbSeconds')):
            if key in row: data.set_editor_property(field,row[key])
        resistance=str(row.get('Resistance','Automatic')).split('::')[-1].upper()
        kind=str(row.get('Kind','Food')).split('::')[-1].upper()
        data.set_editor_property('resistance',getattr(u.MCFoodResistance,resistance))
        data.set_editor_property('kind',getattr(u.MCFoodKind,kind))
        if isinstance(row.get('Label'),str): data.set_editor_property('label',u.Text(row['Label']))
        food.set_editor_property('food_data',data)
        food.set_editor_property('item_name',name)
        food.set_editor_property('item_mesh',self.mesh)
        food.set_editor_property('health',float(row['Health']))
        food.set_editor_property('batch',self.batch)
        self.finish_spawn(food,pose)
        self.food=food
        prop(food,'body').set_simulate_physics(False)
        prop(food,'label').set_visibility(False)
        # Face +Y so the ordinary Knife's actor-right cut normal lies on the
        # controlled fixture's long X axis. FindToolContact owns the cut point.
        center,extent,_=u.SystemLibrary.get_component_bounds(prop(food,'visual'))
        approach=self.floor(center-u.Vector(0,extent.y+75,0))
        self.require(approach is not None,'No supported normal knife approach')
        self.move_to(approach,90.)
        self.pc.set_control_rotation(u.Rotator(pitch=-12,yaw=90.))
        self.pc.set_view_target_with_blend(self.hero,0.)
        prop(self.hero,'inventory').call_method('ServerSelect',args=(u.MCToolSlot.KNIFE,))
        self.knife_verified_at=None
        self.next_select_at=0.
        self.report['fixtures'].append(dict(controlled=controlled,physics_frozen_for_contact=True,
            actual_geometry=self.geometry(food),row_scale=scale,approach_floor_cm=vec(approach),
            normal_cut_input='SetPrimaryInputHeld; actual FindToolContact and HitFood',health=float(row['Health']),
            copied_fields=['Scale','FragmentScale','Health','Mass','Fragments','Resistance','Kind','SpoilSeconds','AbsorbSeconds','Label'],
            nested_fields_not_copied=['CollisionData','Collision','Stack']))
        self.stage('ready_before' if controlled else 'default_before')

    def work_state(self):
        game=u.GameplayStatics.get_game_state(self.world)
        physics=prop(self.hero,'tooth_physics')
        state=dict(selected=str(prop(prop(self.hero,'inventory'),'selected')),
            arrival_held=None,
            body_state=str(physics.get_body_state()),
            standing=physics.get_body_state()==u.MCBodyState.STANDING,
            alive=bool(prop(self.hero,'status').is_alive()),
            freezing_to_death=bool(self.hero.is_freezing_to_death()),
            mimic_captured=bool(self.hero.is_mimic_captured()),
            swallowed_by=path(prop(self.hero,'swallowed_by')),
            reward_interaction=path(prop(self.hero,'reward_interaction')),
            reward_menu_open=bool(self.pc.is_reward_menu_open()),
            boss_intro=bool(self.pc.is_boss_intro_playing()),
            move_input_ignored=bool(self.pc.is_move_input_ignored()),
            look_input_ignored=bool(self.pc.is_look_input_ignored()),
            lobby_waiting=bool(prop(game,'lobby_waiting')),
            working=prop(game,'phase')==u.MCShiftPhase.WORKING,
            walking=prop(self.hero.get_movement_component(),'movement_mode')==u.MovementMode.MOVE_WALKING,
            primary_requested=bool(self.primary),primary_held_native=None,
            CanWork_native=None,ConfirmedHitCount_native=None)
        state['CanWork_observed_gates']=state['standing'] and state['alive'] and state['working'] \
            and not any(state[k] for k in ('freezing_to_death','mimic_captured','swallowed_by',
                'reward_interaction','reward_menu_open','boss_intro','lobby_waiting'))
        return state

    def contact_probe(self):
        visual=prop(self.food,'visual')
        center,extent,_=u.SystemLibrary.get_component_bounds(visual)
        hero=self.hero.get_actor_location()
        forward=self.hero.get_actor_forward_vector()
        closest=u.Vector(*[max(c-e,min(h,c+e)) for h,c,e in zip(vec(hero),vec(center),vec(extent))])
        traces=[]
        grip=prop(self.food,'grip_surface')
        for height in (-45.,15.,75.):
            start=hero+forward*45+u.Vector(0,0,height)
            end=hero+forward*135+u.Vector(0,0,height)
            hit=grip.sphere_trace_component(start,end,45.,True,False,False)
            record=dict(height_cm=height,start_cm=vec(start),end_cm=vec(end),hit=False)
            if hit is not None and hit is not False:
                values=list(hit) if isinstance(hit,(tuple,list)) else [hit]
                native=next((v for v in values if isinstance(v,u.HitResult)),None)
                if native is not None:
                    fields=self.statics.call_method('BreakHitResult',args=(native,))
                    self.require(len(fields)==18,'BreakHitResult signature changed')
                    point=fields[5]
                    delta=point-hero
                    projected=sum(a*b for a,b in zip(vec(delta),vec(forward)))
                    record.update(hit=True,impact_cm=vec(point),distance_cm=float(delta.length()),
                        forward_projection_cm=projected,within_assumed_180cm_reach=delta.length()<=180 and projected>=-2)
            traces.append(record)
        return dict(actor=path(self.food),root_cm=vec(self.food.get_actor_location()),
            hero_cm=vec(hero),hero_forward=vec(forward),visual_center_cm=vec(center),visual_extent_cm=vec(extent),
            closest_aabb_cm=vec(closest),aabb_gap_cm=float((closest-hero).length()),
            method='Read-only GripSurface component sweeps; no damage or gameplay helper calls',sweeps=traces)

    def queue_close(self,name,subjects):
        bounds=[]
        for actor in subjects:
            component=prop(actor,'mesh') if actor==self.hero else prop(actor,'cut_visual') or prop(actor,'visual')
            center,extent,_=u.SystemLibrary.get_component_bounds(component)
            bounds.append((center,extent))
        low=u.Vector(*[min(vec(c-e)[i] for c,e in bounds) for i in range(3)])
        high=u.Vector(*[max(vec(c+e)[i] for c,e in bounds) for i in range(3)])
        center=(low+high)*.5
        radius=max((high-low).length()*.5,100.)
        half_vertical=math.atan(math.tan(math.radians(47.)*.5)/(1280./720.))
        distance=radius/math.sin(half_vertical)*1.12
        forward=self.hero.get_actor_forward_vector()
        right=self.hero.get_actor_right_vector()
        views=[]
        for suffix,raw in (('Front',forward*2.4-right*.3+u.Vector(0,0,.65)),
                ('Side',forward*.3+right*2.4+u.Vector(0,0,.65))):
            eye=center+raw*(distance/raw.length())
            views.append(dict(name=name+'_'+suffix,camera=eye,target=center))
        if not valid(self.capture):
            pose=u.Transform(location=views[0]['camera'],rotation=u.MathLibrary.find_look_at_rotation(views[0]['camera'],center))
            self.capture=self.finish_spawn(self.spawn(u.CameraActor.static_class(),pose),pose)
            self.capture.get_component_by_class(u.CameraComponent).set_editor_property('field_of_view',47.)
        self.capture_request=dict(views=views,index=0)
        self.begin_close_view()

    def begin_close_view(self):
        request=self.capture_request
        view=request['views'][request['index']]
        pose=u.Transform(location=view['camera'],rotation=u.MathLibrary.find_look_at_rotation(view['camera'],view['target']))
        self.capture.set_actor_transform(pose,False,True)
        self.pc.set_view_target_with_blend(self.capture,0.)
        request['at']=float(u.GameplayStatics.get_time_seconds(self.world))+.9
        self.report.setdefault('close_captures',[]).append(dict(file=str(self.output/'screenshots'/(view['name']+'.png')),
            camera_cm=vec(view['camera']),target_cm=vec(view['target']),source='Actual PIE viewport through temporary review CameraActor',
            fov_horizontal=47.,fit='Bounding sphere fitted to vertical FOV, 12 percent framing margin'))

    def poll_close(self):
        request=self.capture_request
        if request is None or float(u.GameplayStatics.get_time_seconds(self.world))<request['at']: return
        view=request['views'][request['index']]
        self.require(self.pc.get_view_target()==self.capture,'Review camera was replaced')
        self.screenshot(view['name']+'.png')
        request['index']+=1
        if request['index']<len(request['views']):
            self.begin_close_view()
        else:
            self.pc.set_view_target_with_blend(self.hero,0.)
            self.capture_request=None

    def sample(self,now):
        collection=prop(self.hero,'food_collection')
        manager=u.GameplayStatics.get_player_camera_manager(self.world,0)
        health=float(prop(self.food,'health')) if valid(self.food) else None
        if health is not None:
            key=path(self.food)
            previous=self.health_observations.get(key)
            if previous is not None and health<previous: self.health_drop_count+=1
            self.health_observations[key]=health
        self.report['samples'].append(dict(time=now,phase=self.phase,
            hero_cm=vec(self.hero.get_actor_location()),velocity_cm_s=vec(self.hero.get_velocity()),
            hero_forward=vec(self.hero.get_actor_forward_vector()),hero_rotation=str(self.hero.get_actor_rotation()),
            hero_state=self.work_state(),food_health=health,
            health_drop_observations=self.health_drop_count,
            food_root_cm=vec(self.food.get_actor_location()) if valid(self.food) else None,
            actual_view_target=path(self.pc.get_view_target()),camera_cm=vec(manager.get_camera_location()),
            collecting=bool(prop(collection,'collecting')),held=[self.geometry(f) for f in prop(collection,'pieces')],
            pieces=[self.geometry(p) for p in self.pieces()]))

    def screenshot(self,name,sequence=False):
        super().screenshot(name,sequence)
        entry=self.report['frames' if sequence else 'screenshots'][-1]
        manager=u.GameplayStatics.get_player_camera_manager(self.world,0)
        entry.update(clip=self.clip,view_target=path(self.pc.get_view_target()),camera_cm=vec(manager.get_camera_location()),
            camera_rotation=str(manager.get_camera_rotation()),gameplay_camera=self.pc.get_view_target()==self.hero)
        if self.clip: self.report['clips'].setdefault(self.clip,[]).append(entry)

    def render_audit(self,food):
        mesh=prop(food,'cut_visual')
        self.require(valid(mesh),'Expected actual generated geometry')
        transform=mesh.get_world_transform()
        skin_count=prop(food,'item_mesh').get_num_sections(0)
        all_points=[]
        sections=[]
        def sub(a,b): return [a[i]-b[i] for i in range(3)]
        def dot(a,b): return sum(a[i]*b[i] for i in range(3))
        def cross(a,b): return [a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]]
        for section in range(mesh.get_num_sections()):
            result=u.ProceduralMeshLibrary.get_section_from_procedural_mesh(mesh,section)
            self.require(isinstance(result,(tuple,list)) and len(result)==5,'Unexpected procedural section output signature')
            vertices,indices=result[0],result[1]
            self.require(len(indices)%3==0,'Render section has an incomplete index triplet')
            used=[];triangles=[]
            for index in indices:
                self.require(0<=index<len(vertices),'Out-of-range render index')
                v=vertices[index]
                self.require(all(math.isfinite(q) for q in vec(v)),'Nonfinite render position')
                point=vec(transform.transform_location(v));used.append(point);all_points.append(point)
            for offset in range(0,len(used),3):
                tri=used[offset:offset+3]
                n=cross(sub(tri[1],tri[0]),sub(tri[2],tri[0]))
                self.require(dot(n,n)>1.e-12,'Zero-area render triangle')
                triangles.append((tri,n))
            record=dict(section=section,vertices=len(vertices),triangles=len(indices)//3,cap=section>=skin_count)
            if section>=skin_count and triangles:
                origin=triangles[0][0][0];normal=triangles[0][1]
                length=math.sqrt(dot(normal,normal));normal=[v/length for v in normal]
                deviation=max(abs(dot(sub(point,origin),normal)) for point in used)
                self.require(deviation<=.05,'A cap section contains off-plane render geometry')
                record['maximum_world_plane_error_cm']=deviation
            sections.append(record)
        self.require(all_points,'Generated food has no referenced triangles')
        low=[min(p[i] for p in all_points) for i in range(3)]
        high=[max(p[i] for p in all_points) for i in range(3)]
        return dict(actor=path(food),sections=sections,bounds_min_cm=low,bounds_max_cm=high,
            source='Actual procedural section buffers; no generated replacement geometry',
            limitation='Private native CutPlanes are not reflected to Python; all retained-halfspace and intrinsic-width assertions are covered by the current native regression tests')

    def prepare_sliver_approach(self):
        pieces=self.pieces(DEFAULT_BATCH)
        self.require(len(pieces)==self.sliver_cut_count+1,'Unexpected live batch count before recut')
        audits=[]
        for piece in pieces:
            prop(piece,'body').set_simulate_physics(False)
            piece.set_actor_tick_enabled(False)
            prop(piece,'label').set_visibility(False)
            audits.append(self.render_audit(piece))
        candidates=[p for p in pieces if not p.fits_hand_portion()]
        self.require(candidates,'No oversized real piece remains for the next oblique cut')
        target=max(candidates,key=lambda p:max(vec(p.get_hand_portion_size())))
        audit=next(a for a in audits if a['actor']==path(target))
        low=u.Vector(*audit['bounds_min_cm']);high=u.Vector(*audit['bounds_max_cm'])
        center=(low+high)*.5;extent=(high-low)*.5
        yaw=self.sliver_yaws[self.sliver_cut_count-1]
        forward=u.Vector(math.cos(math.radians(yaw)),math.sin(math.radians(yaw)),0)
        along=abs(forward.x)*extent.x+abs(forward.y)*extent.y
        approach=self.floor(center-forward*(along+50.))
        self.require(approach is not None,'No supported native knife approach for the remaining piece')
        self.move_to(approach,yaw)
        self.pc.set_control_rotation(u.Rotator(pitch=-12,yaw=yaw))
        self.pc.set_view_target_with_blend(self.hero,0.)
        self.food=target;self.sliver_target=target
        self.sliver_before=[path(p) for p in pieces]
        self.knife_verified_at=None;self.next_select_at=0.
        self.report.setdefault('oblique_approaches',[]).append(dict(cut=self.sliver_cut_count+1,
            requested_hero_yaw=yaw,target=path(target),target_bounds=audit,approach_floor_cm=vec(approach),
            pieces_before=[self.geometry(p) for p in pieces],render_before=audits,
            food_pose_changes='None. Physics only; pieces are frozen for native contact after 1.2 seconds of actual physics settling.'))
        self.stage('sliver_before')

    def tick(self,_):
        if self.done: return
        try:
            self.require(time.monotonic()-self.started<600,'Solo portion check exceeded 600 wall seconds')
            worlds=list(u.EditorLevelLibrary.get_pie_worlds(True))
            if self.world is None:
                if not worlds: return
                self.require(len(worlds)==1 and 'UEDPIE_0_L_Mouth' in worlds[0].get_path_name(),'Expected one owned solo arena')
                self.world=worlds[0]
            self.require(self.owned_pie_is_active(worlds),'Owned PIE was externally stopped/replaced')
            self.pc=u.GameplayStatics.get_player_controller(self.world,0)
            self.hero=self.pc.get_controlled_pawn() if self.pc else None
            if not valid(self.hero): return
            if self.pc.is_reward_menu_open():
                self.pc.choose_reward_perk(0)
                self.report.setdefault('diagnostic_reward_choices',[]).append(dict(index=0,phase=self.phase))
                return
            state=u.GameplayStatics.get_game_state(self.world)
            if not state or prop(state,'phase')!=u.MCShiftPhase.WORKING: return
            now=float(u.GameplayStatics.get_time_seconds(self.world))
            if self.last_game_time is not None and now<=self.last_game_time: return
            self.last_game_time=now
            self.poll_close()
            elapsed=now-self.phase_at
            if self.phase=='waiting_pie':
                if now>=self.sample_at:
                    self.sample_at=now+.2
                    self.sample(now)
                readiness=self.work_state()
                if not readiness['CanWork_observed_gates'] \
                        or not readiness['walking'] or readiness['move_input_ignored'] or readiness['look_input_ignored']:
                    self.observed_ready_at=None
                    return
                if self.observed_ready_at is None:
                    self.observed_ready_at=now
                    return
                if now-self.observed_ready_at<.5:
                    return
                self.report['arrival_ready_state']=readiness
                self.tongue=next(iter(self.actors(u.MCTongue)),None)
                self.require(valid(self.tongue),'No native tongue')
                self.scheduler=prop(state,'single_day_director')
                if valid(self.scheduler):
                    self.scheduler_tick=self.scheduler.is_actor_tick_enabled()
                    self.scheduler.set_actor_tick_enabled(False)
                self.load_row()
                self.fixture(False)
                return
            if now>=self.sample_at:
                self.sample_at=now+.2
                self.sample(now)
            if self.clip and now>=self.frame_at:
                self.frame_at=now+1/self.capture_hz
                self.screenshot('%05d.png'%len(self.report['frames']),True)
            if self.phase in ('default_before','ready_before','sliver_before'):
                readiness=self.work_state()
                selected=prop(prop(self.hero,'inventory'),'selected')
                if selected!=u.MCToolSlot.KNIFE or not readiness['CanWork_observed_gates'] or not readiness['walking']:
                    self.knife_verified_at=None
                    self.require(elapsed<18,'Knife selection/work readiness did not settle; inspect hero_state samples')
                    if now>=self.next_select_at:
                        self.next_select_at=now+.25
                        prop(self.hero,'inventory').call_method('ServerSelect',args=(u.MCToolSlot.KNIFE,))
                        self.report.setdefault('selection_attempts',[]).append(dict(time=now,batch=self.batch,state=readiness))
                    return
                if self.knife_verified_at is None:
                    self.knife_verified_at=now
                    self.report.setdefault('verified_knife_states',[]).append(dict(time=now,batch=self.batch,state=readiness,
                        contact=self.contact_probe() if self.phase!='sliver_before' else None))
                if now-self.knife_verified_at>.5:
                    self.primary_input(True)
                    self.stage('sliver_cut' if self.phase=='sliver_before' else 'default_cut' if self.batch==DEFAULT_BATCH else 'ready_cut')
            elif self.phase in ('default_cut','ready_cut'):
                self.require(elapsed<28,'Normal Knife input did not create two generated halves')
                pieces=self.pieces()
                if len(pieces)<2: return
                self.primary_input(False)
                self.require(len(pieces)==2,'Knife produced an unexpected fragment count')
                if self.batch==DEFAULT_BATCH:
                    self.sliver_cut_count=1
                    self.report['sliver_cuts'].append(dict(cut=1,input='normal held Knife primary input',
                        hero_yaw=float(self.hero.get_actor_rotation().yaw),pieces=[self.geometry(p) for p in pieces]))
                    self.stage('sliver_settle')
                    return
                for p in pieces:
                    prop(p,'body').set_simulate_physics(False)
                    p.set_actor_tick_enabled(False)
                    prop(p,'label').set_visibility(False)
                self.report.setdefault('cut_results',[]).append(dict(batch=self.batch,pieces=[self.geometry(p) for p in pieces],input='normal Knife held primary'))
                if self.batch==DEFAULT_BATCH:
                    self.require(all(not p.fits_hand_portion() for p in pieces),'Default-scale half unexpectedly fits')
                    self.hero.call_method('ServerToggleFoodCollection')
                    self.stage('reject_large_pickup')
                else:
                    self.require(all(p.fits_hand_portion() for p in pieces),'Controlled cut not yet ready; inspect real cut plane before adapting fixture')
                    for p in pieces: p.set_actor_tick_enabled(True)
                    self.hero.call_method('ServerToggleFoodCollection')
                    self.stage('ready_pickup')
            elif self.phase=='sliver_cut':
                self.require(elapsed<12,'Normal oblique Knife input did not produce an accepted additional cut')
                pieces=self.pieces(DEFAULT_BATCH)
                if len(pieces)==len(self.sliver_before): return
                self.primary_input(False)
                self.require(len(pieces)==len(self.sliver_before)+1,'An accepted oblique cut did not add exactly one live result')
                self.sliver_cut_count+=1
                self.report['sliver_cuts'].append(dict(cut=self.sliver_cut_count,input='normal held Knife primary input',
                    actual_hero_yaw=float(self.hero.get_actor_rotation().yaw),expected_target=path(self.sliver_target),
                    before=self.sliver_before,after=[self.geometry(p) for p in pieces]))
                self.stage('sliver_settle')
            elif self.phase=='sliver_settle' and elapsed>1.2:
                if self.sliver_cut_count<3:
                    self.prepare_sliver_approach()
                else:
                    pieces=self.pieces(DEFAULT_BATCH)
                    self.require(len(pieces)==4,'Three accepted cuts must retain four actual live pieces')
                    for p in pieces:
                        prop(p,'body').set_simulate_physics(False)
                        p.set_actor_tick_enabled(False)
                    self.report['repeated_render_audit']=[self.render_audit(p) for p in pieces]
                    self.report['checks']['three_actual_native_input_cuts']=True
                    self.report['checks']['different_oblique_hero_yaws']=len({round(v.get('actual_hero_yaw',v.get('hero_yaw',0)),1) for v in self.report['sliver_cuts']})>=3
                    self.report['checks']['repeated_caps_finite_valid_planar']=True
                    self.hero.call_method('ServerToggleFoodCollection')
                    self.stage('reject_large_pickup')
            elif self.phase=='reject_large_pickup' and elapsed>1:
                held=list(prop(prop(self.hero,'food_collection'),'pieces'))
                self.require(not any(int(prop(p,'batch'))==DEFAULT_BATCH and not p.fits_hand_portion() for p in held),'An oversized real piece entered the hand through normal E input')
                self.report['checks']['default_scale_cut']=True
                self.report['ready_default_pieces_collected_normally']=sum(int(prop(p,'batch'))==DEFAULT_BATCH for p in held)
                self.report['checks']['large_half_normal_e_rejected']=True
                self.screenshot('01_DefaultLargeHalves_GameCamera.png')
                self.queue_close('02_DefaultLargeHalves_Close',self.pieces()+[self.hero])
                self.stage('large_close_wait')
            elif self.phase=='large_close_wait' and self.capture_request is None and elapsed>.2:
                # E can discover unrelated ready food in this temporary arena.
                # End that normal collection state before staging the next cut,
                # so it cannot pick up the controlled halves before our observer.
                if bool(prop(prop(self.hero,'food_collection'),'collecting')):
                    self.hero.call_method('ServerToggleFoodCollection')
                for p in self.pieces(DEFAULT_BATCH): p.destroy_actor()
                self.report['default_halves_cleanup']='Owned diagnostic halves destroyed after rejection evidence, without delivery/reward'
                self.fixture(True)
            elif self.phase=='ready_pickup':
                self.require(elapsed<5,'Real collection did not gather both ready portions')
                held=[p for p in prop(prop(self.hero,'food_collection'),'pieces') if int(prop(p,'batch'))==READY_BATCH]
                if len(held)==2 and elapsed>1:
                    self.report['checks']['ready_portions_real_collection']=True
                    self.report['held_before_motion']=[self.geometry(p) for p in held]
                    self.require(self.pc.get_view_target()==self.hero,'Gameplay camera was replaced')
                    self.clip='ReadyPortions_ActualGameCamera'
                    self.stage('held_motion')
            elif self.phase=='held_motion':
                held=[p for p in prop(prop(self.hero,'food_collection'),'pieces') if int(prop(p,'batch'))==READY_BATCH]
                self.require(len(held)==2,'Ready portions were lost during short normal movement')
                if .25<elapsed<.7: self.hero.add_movement_input(self.hero.get_actor_forward_vector(),.4)
                elif 1.25<elapsed<1.7: self.hero.add_movement_input(-self.hero.get_actor_forward_vector(),.4)
                if elapsed>2.7:
                    self.clip=None
                    self.report['checks']['held_motion_retained_two']=True
                    self.screenshot('03_ReadyHeld_GameCamera.png')
                    self.queue_close('04_ReadyHeld_Close',held+[self.hero])
                    self.stage('held_close_wait')
            elif self.phase=='held_close_wait' and self.capture_request is None and elapsed>.2:
                self.report['checks']['actual_gameplay_camera_motion']=bool(self.report['clips'].get('ReadyPortions_ActualGameCamera'))
                self.report['checks']['close_review_camera']=True
                self.require(all(self.report['checks'].values()),'A required runtime check failed')
                self.finish()
        except Exception:
            self.finish(traceback.format_exc())

    def finish(self,error=None):
        if self.done: return
        if valid(self.scheduler) and self.scheduler_tick is not None:
            try: self.scheduler.set_actor_tick_enabled(self.scheduler_tick)
            except Exception: self.report['scheduler_restore_error']=traceback.format_exc()
        super().finish(error)


def main():
    old=getattr(builtins,KEY,None)
    if old is not None and not getattr(old,'done',True):
        raise RuntimeError('A portion check is already active; it was preserved')
    probe=FoodPortionsSolo()
    setattr(builtins,KEY,probe)
    try: probe.start()
    except Exception:
        probe.finish(traceback.format_exc())
        raise


if __name__=='__main__': main()
