"""Narrow owned solo Hub proof after merging the colleague's head cosmetics.

Requires an idle clean Hub in an Editor launched with -MCHubTransientProfile.
Uses the existing CleanRespawnSolo start/finish setting-restoration code, with
Hub-specific readiness and UI checks. No purchase, Apply, ownership/coin edit,
profile read/write, map travel, package save, build or network peer is requested.
The real pawn is temporarily placed at capsule-clear supported space inside the
actual E interaction OBB. Exact PIE SWindow screenshots disclose that fixture.
The normal exported mouse-orbit/wheel callbacks frame the wardrobe from that
point; camera collision remains enabled and no separate view target is created.
Cancel: builtins.mc_colleague_wardrobe_solo.finish('cancelled').
"""
import builtins
import datetime
import hashlib
import json
import math
from pathlib import Path
import runpy
import time
import traceback
import uuid

import unreal as u


SUPPORT = runpy.run_path(str(Path(__file__).with_name('probe_clean_respawn_solo.py')),
                         run_name='wardrobe_probe_support')
Base,prop,valid,require = (SUPPORT[key] for key in ('CleanRespawnSolo','prop','valid','require'))
KEY = 'mc_colleague_wardrobe_solo'
PRICES = {'Hat_Head_CowboyHat':80,'Face_Head_SquareGlasses':60,'Hat_Head_Feather':40,
          'Face_Head_Brows':45,'Hat_Head_ConeHat':55,'Hat_Head_ArtistCap':0,'Hat_Head_FootballHelmet':90}


def path(value):
    return value.get_path_name() if valid(value) else None


def vec(value):
    return [float(value.x),float(value.y),float(value.z)]


def outer_is(obj, owner):
    for _ in range(24):
        if not valid(obj):
            return False
        if obj == owner:
            return True
        obj = obj.get_outer()
    return False


def control(tile, name, cls):
    values = [obj for obj in u.ObjectIterator(cls) if obj.get_name() == name and outer_is(obj,tile)]
    require(len(values) == 1, 'Missing or ambiguous '+name+' in '+path(tile))
    return values[0]


def equipped_items(economy):
    loadout = prop(economy,'EquippedLoadout')
    return {slot:str(u.MCCosmeticCatalog.get_equipped_item(loadout,slot)) for slot in ('Face','Hat')}


def angle_delta(wanted, current):
    return (float(wanted)-float(current)+180.)%360.-180.


class ColleagueWardrobeSolo(Base):
    def __init__(self):
        self.done = self.requested_pie = self.busy = False
        self.callback = self.pie_start = self.world = self.pc = self.identity = None
        self.viewport_overrides = []
        self.phase = 'waiting_pie';self.phase_at = self.started = time.monotonic();self.ready_since = None
        self.phase_game_at = 0.;self.orbit_passes = 0
        self.editor = u.get_editor_subsystem(u.UnrealEditorSubsystem)
        self.levels = u.get_editor_subsystem(u.LevelEditorSubsystem)
        require(not self.levels.is_in_play_in_editor() and not u.EditorLevelLibrary.get_pie_worlds(True),
                'Preexisting Play session preserved')
        require('-mchubtransientprofile' in u.SystemLibrary.get_command_line().lower(),
                '-MCHubTransientProfile is mandatory; this probe never edits profile files')
        self.editor_world = self.editor.get_editor_world();self.editor_path = self.editor_world.get_path_name()
        require(self.editor_path == '/Game/Maps/Hub.Hub', 'Open the clean saved Hub first')
        dirty = list(u.EditorLoadingAndSavingUtils.get_dirty_content_packages())+list(u.EditorLoadingAndSavingUtils.get_dirty_map_packages())
        require(not dirty, 'Unrelated dirty packages preserved')
        self.play = u.load_object(None,'/Script/UnrealEd.Default__LevelEditorPlaySettings')
        self.old_play = json.loads(u.ToolsetLibrary.get_object_properties(self.play,list(SUPPORT['PLAY'])))
        self.engine = next(obj for obj in u.ObjectIterator(u.load_class(None,'/Script/Engine.Engine'))
                           if obj.get_path_name().startswith('/Engine/Transient.'))
        self.old_engine = (prop(self.engine,'bUseFixedFrameRate'),prop(self.engine,'FixedFrameRate'))
        self.perf = u.get_default_object(u.load_class(None,'/Script/UnrealEd.EditorPerformanceSettings'))
        self.old_throttle = prop(self.perf,'bThrottleCPUWhenNotForeground')
        self.old_idle = u.SystemLibrary.get_console_variable_int_value('t.IdleWhenNotForeground')
        stamp = datetime.datetime.now(datetime.timezone.utc).strftime('Run_%Y%m%dT%H%M%SZ_')+uuid.uuid4().hex[:6]
        self.output = Path('E:/DEVGAME/MessControl/Artifacts/ColleagueWardrobe_20261024')/stamp
        self.output.mkdir(parents=True,exist_ok=False)
        self.report = {'status':'running','passed':False,'phase':self.phase,'screenshots':[],
            'editor_world':self.editor_path,'dirty_before':[],'previous_play_settings':self.old_play,
            'network_tested':False,'profile_files_read_or_written':False,'transient_profile_flag_required':True,
            'purchases_requested':False,'apply_requested':False,'placement_queries':[],
            'fixtures':['Real pawn temporarily positioned on verified floor/capsule-clear point inside the actual wardrobe E OBB.',
                        'Normal ApplyCameraOrbitInput and ZoomCamera callbacks temporarily frame the E prompt; no camera properties/view target are overridden.',
                        'Normal try_open_wardrobe route, then actual tile selection OnClicked; no Buy/Apply delegate.',
                        'Screenshots are the actual PIE SWindow including native UI and window chrome.'],
            'limitations':['Placement is a UI isolation fixture, not a walking approach proof.',
                           'Camera preflight samples a bounded grid and authored orbit inputs; it does not prove that every possible point/angle in the zone is blocked.',
                           'Solo only; no multiplayer isolation or packaged cold-start claim.',
                           'Assertions do not replace root visual review of both screenshots.']}
        self.hero = self.widget = self.preview = self.economy = None

    def stage(self, name):
        self.phase_game_at = float(u.GameplayStatics.get_time_seconds(self.world)) if valid(self.world) else 0.
        super().stage(name)

    def frame_e_camera(self, reset_zoom=False):
        require(self.pc.get_view_target() == self.hero and not self.pc.is_look_input_ignored(),
                'Normal locally controlled player camera is required for the E proof')
        current = self.pc.player_camera_manager.get_camera_rotation()
        toward = prop(self.station,'InteractionPrompt').get_world_location()-self.hero.get_actor_location()
        wanted_yaw = math.degrees(math.atan2(toward.y,toward.x))
        wanted_pitch = self.e_camera_plan['pitch_degrees'];distance = self.e_camera_plan['distance_cm']
        sensitivity = max(.02,min(1.,float(prop(self.hero,'CameraOrbitSensitivity'))))
        step = max(10.,min(300.,float(prop(self.hero,'CameraZoomStep'))))
        delta_yaw,delta_pitch = angle_delta(wanted_yaw,current.yaw),wanted_pitch-float(current.pitch)
        self.hero.apply_camera_orbit_input(u.Vector2D(delta_yaw/sensitivity,delta_pitch/sensitivity))
        if reset_zoom:
            # A collided eye cannot reveal the private desired arm distance.
            # Normal wheel input first reaches its known native 250-cm clamp,
            # then requests the preflighted distance using the authored step.
            self.hero.zoom_camera(1000.)
            self.hero.zoom_camera((250.-distance)/step)
        self.orbit_passes += 1
        self.report.setdefault('E_camera_orbit_fixture',[]).append({
            'pass':self.orbit_passes,'current_rotation':str(current),
            'target_yaw':wanted_yaw,'target_pitch':wanted_pitch,'input_delta':[delta_yaw/sensitivity,delta_pitch/sensitivity],
            'actual_sensitivity':sensitivity,'actual_zoom_step_cm':step,'target_distance_cm':distance,
            'zoom_reset_via_native_clamp':bool(reset_zoom),
            'game_seconds':float(u.GameplayStatics.get_time_seconds(self.world))})
        return max(abs(delta_yaw),abs(delta_pitch))

    def assert_e_in_view(self):
        prompt = prop(self.station,'InteractionPrompt');anchor = prompt.get_world_location()
        require(prompt.is_visible(),'Actual nearby E prompt is not visible')
        # Installed 5.8 GameplayStatics.h declares BlueprintPure ProjectWorldToScreen;
        # accept the typed Python out value, never a guessed controller property.
        projected = u.GameplayStatics.project_world_to_screen(self.pc,anchor,True)
        if isinstance(projected,u.Vector2D):
            screen,success = projected,True
        elif isinstance(projected,tuple):
            points = [value for value in projected if isinstance(value,u.Vector2D)]
            flags = [value for value in projected if isinstance(value,bool)]
            require(len(points) == 1 and len(flags) <= 1,'Unexpected typed ProjectWorldToScreen result')
            screen,success = points[0],all(flags)
        else:
            screen,success = None,False
        width,height = self.pc.get_viewport_size()
        camera = self.pc.player_camera_manager;eye = camera.get_camera_location()
        pivot = self.hero.get_actor_location()+u.Vector(0,0,max(30.,min(250.,float(prop(self.hero,'CameraOrbitHeight')))))
        actual_distance = (eye-pivot).length()
        self.report['E_prompt'] = {'visible':True,'world_anchor':vec(anchor),
            'projection_succeeded':bool(success),
            'projected_anchor_px':[float(screen.x),float(screen.y)] if screen is not None else None,
            'actual_viewport_px':[int(width),int(height)],
            'actual_camera_eye':vec(eye),'actual_camera_rotation':str(camera.get_camera_rotation()),
            'actual_distance_to_pivot_cm':actual_distance,'native_collision_unchanged':True,
            'visual_review':'pending_root; on-screen projection alone does not certify the screenshot'}
        require(success and width > 0 and height > 0,'E world anchor failed native screen projection')
        require(40. <= screen.x <= width-40. and 80. <= screen.y <= height-30.,
                'E keycap is outside the actual viewport or clipped by its edges: '+str(projected))
        require(max(245.,self.e_camera_plan['distance_cm']-30.) <= actual_distance <= 650.,
                'Actual camera retracted from the preflighted 250-520 cm framing path (collision/zoom); E screenshot is not valid evidence: '+str(actual_distance))
        player_screen = u.GameplayStatics.project_world_to_screen(self.pc,self.hero.get_actor_location(),True)
        require(isinstance(player_screen,u.Vector2D) and 40. <= player_screen.x <= width-40.
                and 40. <= player_screen.y <= height-40.,'The real player is not visible alongside the E prompt')
        self.report['E_prompt']['projected_player_center_px'] = [float(player_screen.x),float(player_screen.y)]
        require(self.pc.get_view_target() == self.hero,'E proof switched away from the real player camera')

    def camera_preflight(self, candidate, row):
        """Read-only native spring-arm sphere path plus conservative frustum checks."""
        boom = prop(self.hero,'CameraBoom')
        require(prop(boom,'ProbeChannel') == u.CollisionChannel.ECC_CAMERA,'Unexpected native camera collision channel')
        probe_radius = max(1.,float(prop(boom,'ProbeSize')))
        height = max(30.,min(250.,float(prop(self.hero,'CameraOrbitHeight'))))
        anchor = prop(self.station,'InteractionPrompt').get_world_location()
        toward = anchor-candidate;yaw = math.atan2(toward.y,toward.x)
        width,height_pixels = self.pc.get_viewport_size()
        require(width > 0 and height_pixels > 0,'Actual viewport unavailable for camera preflight')
        half_x = math.tan(math.radians(float(prop(self.hero,'FollowFOV'))*.5))
        half_y = half_x/(float(width)/height_pixels)
        origin,pivot = candidate+u.Vector(0,0,30),candidate+u.Vector(0,0,height)
        row['camera_trials'] = []
        for distance in (520.,450.,380.,320.,280.,250.):
            options = []
            for pitch in (-12.,-6.,0.,-28.,-40.,-55.,-65.):
                angle = math.radians(pitch)
                forward = u.Vector(math.cos(angle)*math.cos(yaw),math.cos(angle)*math.sin(yaw),math.sin(angle))
                right = u.Vector(-math.sin(yaw),math.cos(yaw),0)
                up = u.Vector(-math.sin(angle)*math.cos(yaw),-math.sin(angle)*math.sin(yaw),math.cos(angle))
                eye = pivot-forward*distance
                def project(point):
                    delta = point-eye
                    depth = delta.x*forward.x+delta.y*forward.y+delta.z*forward.z
                    if depth <= 1.:
                        return None
                    horizontal = delta.x*right.x+delta.y*right.y+delta.z*right.z
                    vertical = delta.x*up.x+delta.y*up.y+delta.z*up.z
                    return [.5+.5*horizontal/(depth*half_x),.5-.5*vertical/(depth*half_y)]
                ep,pp = project(anchor),project(candidate)
                trial = {'distance_cm':distance,'pitch_degrees':pitch,'predicted_E_uv':ep,'predicted_player_uv':pp}
                row['camera_trials'].append(trial)
                if not ep or not pp or not (.05 <= ep[0] <= .95 and .12 <= ep[1] <= .94
                        and .05 <= pp[0] <= .95 and .06 <= pp[1] <= .92):
                    trial['rejected'] = 'E/player predicted outside useful viewport'
                    continue
                # CollisionProfile.cpp maps Visibility first, Camera second;
                # this is the same ECC_Camera sphere24 default used by the arm.
                hit = u.SystemLibrary.sphere_trace_single(self.world,origin,eye,probe_radius,
                    u.TraceTypeQuery.TRACE_TYPE_QUERY2,False,[self.hero],u.DrawDebugTrace.NONE,True)
                data = u.get_default_object(u.GameplayStatics).call_method('BreakHitResult',args=(hit,)) if hit else None
                blocked = bool(data and data[0])
                trial['camera_blocked'] = blocked
                if blocked:
                    trial.update(blocker=path(data[9]),component=path(data[10]),hit_location=vec(data[4]))
                    counts = self.report.setdefault('camera_blocker_counts',{})
                    identity = path(data[9]) or '<unknown>';counts[identity] = counts.get(identity,0)+1
                    continue
                cost = abs(ep[0]-.5)+abs(ep[1]-.32)+.5*abs(pp[0]-.5)
                options.append({'distance_cm':distance,'pitch_degrees':pitch,'yaw_degrees':math.degrees(yaw),
                    'predicted_E_uv':ep,'predicted_player_uv':pp,'eye':vec(eye),'sweep_origin':vec(origin),
                    'sphere_radius_cm':probe_radius,'collision_channel':'ECC_Camera','framing_cost':cost})
            if options:
                return min(options,key=lambda option:option['framing_cost'])
        return None

    def screenshot(self, name):
        target = self.output/(name+'.png')
        require(u.MCGameplayUIEditorLibrary.capture_pie_game_window(self.world,str(target)), 'Actual PIE SWindow capture failed')
        data = target.read_bytes();require(data.startswith(b'\x89PNG\r\n\x1a\n') and len(data)>24,'Invalid PNG capture')
        self.report['screenshots'].append({'file':str(target),'sha256':hashlib.sha256(data).hexdigest(),
            'width':int.from_bytes(data[16:20],'big'),'height':int.from_bytes(data[20:24],'big'),
            'game_seconds':float(u.GameplayStatics.get_time_seconds(self.world)),'visual_review':'pending_root'})

    def hit(self, start, end, capsule=None):
        args = (self.world,start,end,u.TraceTypeQuery.TRACE_TYPE_QUERY1,False,[self.hero],u.DrawDebugTrace.NONE,True)
        if capsule:
            result = u.SystemLibrary.capsule_trace_single(self.world,start,end,*capsule,
                u.TraceTypeQuery.TRACE_TYPE_QUERY1,False,[self.hero],u.DrawDebugTrace.NONE,True)
        else:
            result = u.SystemLibrary.line_trace_single(*args)
        return u.get_default_object(u.GameplayStatics).call_method('BreakHitResult',args=(result,)) if result else None

    def place_supported(self):
        volume = prop(self.station,'InteractionVolume');frame = volume.get_world_transform();extent = volume.get_unscaled_box_extent()
        capsule = self.hero.capsule_component
        radius,half = capsule.get_scaled_capsule_radius(),capsule.get_scaled_capsule_half_height()
        choices = [];capsule_clear = 0
        # The first capsule-clear gap may still force the camera into the pawn.
        # Include the front edge and wide X positions, keeping real OBB membership.
        for fy in (.98,.9,.75,.5,.25,-.25,-.5,-.75,-.9,-.98,0.):
            for fx in (0.,-.35,.35,-.7,.7,-.9,.9,-.98,.98):
                point = u.MathLibrary.transform_location(frame,u.Vector(extent.x*fx,extent.y*fy,0))
                floor = self.hit(point+u.Vector(0,0,80),point-u.Vector(0,0,1400))
                row = {'fraction':[fx,fy],'floor_actor':path(floor[9]) if floor else None,
                       'floor':vec(floor[5]) if floor else None,'normal':vec(floor[7]) if floor else None}
                self.report['placement_queries'].append(row)
                if not floor or not floor[0] or floor[7].z < .75:
                    continue
                candidate = floor[5]+u.Vector(0,0,half+3)
                local = u.MathLibrary.inverse_transform_location(frame,candidate)
                if any(abs(a)>b-2. for a,b in zip(vec(local),vec(extent))):
                    continue
                obstruction = self.hit(candidate,candidate+u.Vector(0,0,.1),(radius,half))
                row['capsule_blocked'] = bool(obstruction and obstruction[0])
                if row['capsule_blocked']:
                    continue
                capsule_clear += 1
                plan = self.camera_preflight(candidate,row)
                row['camera_plan'] = plan
                if plan:
                    choices.append((candidate,float(floor[5].z),plan))
        self.report['placement_summary'] = {'grid_points':len(self.report['placement_queries']),
            'supported_capsule_clear_points':capsule_clear,'points_with_clear_camera_and_E_player_framing':len(choices),
            'tested_camera_distances_cm':[520,450,380,320,280,250], 'tested_pitch_degrees':[-12,-6,0,-28,-40,-55,-65],
            'yaw_target':'Actual E prompt anchor, not the station origin'}
        require(choices,'No inspected E-zone grid point has both supported capsule clearance and a clear native camera path; inspect placement_summary/camera_blocker_counts before changing scene assets')
        target,self.support_z,self.e_camera_plan = max(choices,key=lambda option:(option[2]['distance_cm'],-option[2]['framing_cost']))
        before = self.hero.get_actor_location()
        self.hero.character_movement.stop_movement_immediately()
        self.hero.set_actor_location_and_rotation(target,self.station.get_actor_rotation(),False,True)
        require(self.station.contains_pawn(self.hero),'Actual wardrobe ContainsPawn rejected the placement')
        self.report['placement'] = {'station':path(self.station),'before':vec(before),'after':vec(self.hero.get_actor_location()),
                                    'floor_z':self.support_z,'capsule_radius':radius,'capsule_half_height':half,
                                    'normal_camera_plan':self.e_camera_plan}

    def tile(self, item_id):
        values = [obj for obj in u.ObjectIterator(u.MCCosmeticItemWidget)
                  if obj.get_owning_player() == self.pc and str(obj.get_item_id()) == item_id and outer_is(obj,self.widget)]
        require(len(values) == 1,'Expected one actual card for '+item_id)
        return values[0]

    def tick(self, _dt):
        if self.done or self.busy:
            return
        self.busy = True
        try:
            require(time.monotonic()-self.started < 90,'Wardrobe solo probe timed out')
            require(self.pie_start is None or not prop(self.pie_start,'Error'),'Owned StartPIE failed')
            worlds = list(u.EditorLevelLibrary.get_pie_worlds(True))
            if self.world is None:
                if not worlds:
                    return
                require(len(worlds) == 1 and u.SystemLibrary.is_server(worlds[0]),'Expected one standalone authority world')
                self.world = worlds[0]
                require(u.GameplayStatics.get_current_level_name(self.world,True) == 'Hub','Wrong PIE map')
                self.pc = u.GameplayStatics.get_player_controller(self.world,0)
                self.gs = u.GameplayStatics.get_game_state(self.world)
                require(isinstance(self.pc,u.MCPlayerController),'Expected normal gameplay controller')
                self.stage('startup_ready')
            require(len(worlds) == 1 and worlds[0] == self.world,'Owned PIE was replaced; replacement must be preserved')
            age = time.monotonic()-self.phase_at
            game_age = float(u.GameplayStatics.get_time_seconds(self.world))-self.phase_game_at
            if self.phase == 'startup_ready':
                self.hero = self.pc.get_controlled_pawn()
                if not valid(self.hero) or not self.ready():
                    return
                stations = [obj for obj in u.GameplayStatics.get_all_actors_of_class(self.world,u.MCHubWardrobeStation)
                            if bool(prop(obj,'bEnabled'))]
                require(len(stations) == 1,'Expected one actual enabled wardrobe station')
                self.station = stations[0];self.identity = prop(self.pc,'player_state');self.economy = prop(self.identity,'economy')
                require(int(prop(self.economy,'Coins')) == 0,'Fresh transient profile must have zero coins')
                require(not self.economy.owns_cosmetic('Hat_Head_CowboyHat'),'Cowboy test item must be unowned')
                self.initial_loadout = equipped_items(self.economy)
                self.place_supported();self.stage('orbit_initial_settle')
            elif self.phase == 'orbit_initial_settle' and game_age >= 1.5:
                self.frame_e_camera(reset_zoom=True);self.stage('orbit_input_settle')
            elif self.phase == 'orbit_input_settle' and game_age >= 1.5:
                current = self.pc.player_camera_manager.get_camera_rotation()
                toward = prop(self.station,'InteractionPrompt').get_world_location()-self.hero.get_actor_location()
                error = max(abs(angle_delta(math.degrees(math.atan2(toward.y,toward.x)),current.yaw)),
                            abs(self.e_camera_plan['pitch_degrees']-float(current.pitch)))
                if error > 2. and self.orbit_passes < 3:
                    self.frame_e_camera();self.stage('orbit_input_settle');return
                require(error <= 2.,'Normal camera orbit did not settle onto the wardrobe: '+str(error))
                self.assert_e_in_view()
                self.screenshot('01_NearWardrobe_E');self.stage('open_wardrobe')
            elif self.phase == 'open_wardrobe':
                require(self.pc.try_open_wardrobe(),'Normal try_open_wardrobe was rejected')
                self.stage('wardrobe_settle')
            elif self.phase == 'wardrobe_settle' and age >= .6:
                widgets = [obj for obj in u.ObjectIterator(u.MCHubWardrobeWidget)
                           if obj.get_owning_player() == self.pc and obj.is_in_viewport()]
                require(len(widgets) == 1,'Expected one real wardrobe widget')
                self.widget = widgets[0]
                self.report['cards'] = []
                for item_id,price in PRICES.items():
                    tile = self.tile(item_id);copy = str(control(tile,'CosmeticPrice',u.TextBlock).get_text())
                    buy = control(tile,'CosmeticBuy',u.Button)
                    if price:
                        digits = ''.join(character for character in copy if character.isdigit())
                        require(digits == str(price),'Wrong visible paid price for '+item_id+': '+copy)
                        require(not buy.get_is_enabled(),'A paid item was affordable with zero coins')
                    else:
                        require(copy in ('Бесплатно','Free'),'Artist cap is not visibly free: '+copy)
                    self.report['cards'].append({'id':item_id,'expected_price':price,'visible_price':copy,
                        'buy_enabled':bool(buy.get_is_enabled()),'owned':bool(self.economy.owns_cosmetic(item_id))})
                cowboy = self.tile('Hat_Head_CowboyHat')
                require(control(cowboy,'CosmeticButton',u.Button).get_is_enabled(),'Unowned card selection is disabled')
                control(cowboy,'CosmeticButton',u.Button).on_clicked.broadcast()
                self.stage('unowned_preview_settle')
            elif self.phase == 'unowned_preview_settle' and age >= .6:
                previews = [obj for obj in u.GameplayStatics.get_all_actors_of_class(self.world,u.MCWardrobePreviewActor)
                            if obj.get_owner() == self.pc]
                require(len(previews) == 1,'Expected one owner-local preview actor')
                self.preview = previews[0]
                parts = [part for part in self.preview.get_components_by_class(u.StaticMeshComponent)
                         if part.is_visible() and part.static_mesh and part.static_mesh.get_name() == 'SM_HeadCosmetic_CowboyHat']
                require(len(parts) == 1,'The unpaid cowboy selection is not visibly equipped on the preview')
                require(not self.economy.owns_cosmetic('Hat_Head_CowboyHat') and int(prop(self.economy,'Coins')) == 0,
                        'Preview changed ownership or wallet')
                require(equipped_items(self.economy) == self.initial_loadout,'Preview changed the authoritative equipped loadout')
                center,extent,_ = u.SystemLibrary.get_component_bounds(self.preview.body)
                manager = self.pc.player_camera_manager;eye = manager.get_camera_location()
                forward = self.preview.get_actor_forward_vector();to_eye = eye-center
                dot = (forward.x*to_eye.x+forward.y*to_eye.y)/max(.001,math.hypot(forward.x,forward.y)*math.hypot(to_eye.x,to_eye.y))
                require(dot > .65,'Preview is not facing its player camera')
                floor = self.hit(u.Vector(center.x,center.y,self.support_z+80),u.Vector(center.x,center.y,self.support_z-1400))
                require(floor and floor[0] and floor[7].z > .7,'Preview lacks inspected ground support')
                feet = float(center.z-extent.z);gap = feet-float(floor[5].z)
                require(abs(gap) < max(10.,float(extent.z)*.2),'Preview floats/stands on a roof above the inspected floor: '+str(gap))
                self.report['unowned_preview'] = {'actor':path(self.preview),'cowboy_part':path(parts[0]),'coins':0,
                    'owned':False,'authoritative_loadout_unchanged':True,'camera_facing_dot_xy':dot,
                    'body_bounds_bottom_z':feet,'inspected_ground_z':float(floor[5].z),'ground_gap_cm':gap,
                    'ground_actor':path(floor[9]),'camera':vec(eye)}
                self.screenshot('02_UnownedCowboy_ZeroCoins');self.finish()
        except Exception:
            error = traceback.format_exc()
            if self.phase == 'orbit_input_settle' and valid(self.world):
                try:
                    self.screenshot('01_E_Framing_REJECTED')
                except Exception:
                    self.report['rejected_capture_error'] = traceback.format_exc()
            self.finish(error)
        finally:
            self.busy = False

    def finish(self, error=None):
        if self.done:
            return
        if valid(self.pc) and self.pc.is_wardrobe_open():
            try:
                self.pc.call_method('CancelWardrobe')
            except Exception:
                error = error or traceback.format_exc()
        super().finish(error)


def main():
    previous = getattr(builtins,KEY,None)
    require(previous is None or getattr(previous,'done',True),'Wardrobe probe already active')
    probe = ColleagueWardrobeSolo();setattr(builtins,KEY,probe)
    try:
        probe.start()
    except Exception:
        probe.finish(traceback.format_exc())
        raise
    return probe


if __name__ == '__main__':
    main()
