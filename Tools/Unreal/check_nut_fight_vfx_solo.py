"""Short owned PIE footage of real shield hits/cracking/break and wizard fire.

Uses the existing F3 nut encounter and its natural rain/boss arrival on L_Mouth.
After arrival, readiness delays and zero boss walking speed isolate the two
visual clips. Shield durability is an explicitly labelled initial PIE fixture
(four ordinary base Pickaxe hits); it is never changed during the recording.
Held primary input supplies real damage, and the normal wizard scheduler fires
the projectile. Native damage, push, projectile speed/collision, flame geometry
and timers are not bypassed. No packages, networking, build or existing PIE.
Optional MC_NUT_VFX_OUTPUT / MC_NUT_VFX_CAPTURE_HZ (default 6).
Cancel: builtins.mc_nut_fight_vfx_solo.finish('Stopped by operator').
"""
import builtins
import hashlib
import json
import math
import re
import runpy
import time
import traceback
from pathlib import Path
import unreal as u

KEY = 'mc_nut_fight_vfx_solo'
OUTPUT = Path(globals().get('MC_NUT_VFX_OUTPUT',
    Path(u.Paths.project_saved_dir())/'Checks/NutFightVFXSolo')).resolve()
support = runpy.run_path(str(Path(u.Paths.project_dir())/'Tools/Unreal/check_nut_fight_redesign_solo.py'),
    run_name='nut_fight_vfx_support', init_globals={'MC_NUT_FIGHT_OUTPUT': OUTPUT,
        'MC_NUT_FIGHT_CAPTURE_HZ': globals().get('MC_NUT_VFX_CAPTURE_HZ', 6)})
Parent, prop, vec, valid = (support[name] for name in ('NutFightRedesignSolo', 'prop', 'vec', 'valid'))


class NutFightVFXSolo(Parent):
    def __init__(self):
        super().__init__()
        self.tank = None
        self.shield_hits = []
        self.last_shield = self.shield_broken_at = None
        self.fire_started = False
        self.fire_paths = set()
        self.fire_impacts = set()
        self.clip = None
        self.initial_tank_health = None
        layout_path = Path(u.Paths.project_dir())/'Source/MessControl/Private/MCNutShieldCrackLayout.h'
        layout = layout_path.read_text(encoding='utf8')
        self.shield_layout_path = str(layout_path.resolve())
        self.shield_layout_sha256 = hashlib.sha256(layout_path.read_bytes()).hexdigest()
        def layout_vector(name):
            match = re.search(r'inline const FVector\s+'+name+r'\(([^)]+)\);', layout)
            self.require(match is not None, 'Native shield layout has no '+name)
            values = [float(value) for value in match.group(1).split(',')]
            self.require(len(values) == 3, 'Native shield layout '+name+' is not a vector')
            return u.Vector(*values)
        self.shield_local_center = layout_vector('Center')
        self.shield_local_normal = layout_vector('Normal')
        self.report.update(scope='One owned standalone PIE; real Pickaxe shield hits and scheduled fire nut',
            clips={'shield_damage': [], 'fire_nut': []}, fixture_tuning={},
            limitations=[
                'Natural F3 rain and boss entrance precede the clips; position-only dodging protects the observer.',
                'The shield starts with four base Pickaxe hits of durability as an explicit PIE preview fixture; native default 500 is covered separately.',
                'Both boss walking speeds and unrelated readiness clocks are isolated only in PIE; boss/projectile damage and player health stay native.',
                'Held primary input damages the shield; no Python damage resolver or attack helper is invoked.',
                'Crack component/clock telemetry must be paired with inspection of the genuine rendered footage.',
                'No content saves, network, complete battle, build, cook or package.'])

    def screenshot(self, name, sequence=False):
        # The parent's clip-aware writer uses our two replacement clip lists.
        super().screenshot(name, sequence)

    def sample(self, now):
        # During the shared natural arrival, retain the ordinary event telemetry.
        if self.phase in ('arrival', 'rain'):
            Parent.sample(self, now)
            if self.phase == 'rain' and not self.report.get('red_warning_frame') and .8 <= now-self.rain_start <= 1.45:
                self.screenshot('00_red_landing_warning.png')
                self.report['red_warning_frame'] = self.report['screenshots'][-1]
            return
        cracks = next((component for component in self.tank.get_components_by_class(u.ProceduralMeshComponent)
                       if component.get_name() == 'ShieldDamageCracks'), None)
        material = cracks.get_material(0) if cracks else None
        # The engine's private CPD cache is unavailable through editor-property
        # reflection. Observe the actual generated section through its public
        # engine API, alongside the boss's real durability and attachment.
        section = u.ProceduralMeshLibrary.get_section_from_procedural_mesh(cracks, 0) if cracks else None
        model = prop(self.tank, 'tank_model')
        bone = next((name for name in ('hand.l', 'hand_l') if model.get_bone_index(name) >= 0), None)
        spells = [actor for actor in self.actors(u.MCNutSpellProjectile) if prop(actor, 'source_actor') == self.mage]
        flights = []
        for actor in spells:
            self.fire_paths.add(actor.get_path_name())
            impact = prop(actor, 'impact')
            if prop(impact, 'impacted'):
                self.fire_impacts.add(actor.get_path_name())
            flight = prop(actor, 'flight')
            flights.append(dict(path=actor.get_path_name(), location_cm=vec(actor.get_actor_location()),
                radius_cm=float(prop(flight, 'radius')), seconds=float(prop(flight, 'seconds')),
                started_at=float(prop(flight, 'started_at')), start_cm=vec(prop(flight, 'start')),
                target_cm=vec(prop(flight, 'target')), impacted=bool(prop(impact, 'impacted')),
                impact_cm=vec(prop(impact, 'point')), damage_applications=int(prop(actor, 'damage_applications')),
                flames_visible=bool(prop(actor, 'flames').is_visible()),
                trail_active=bool(prop(actor, 'trail').is_active())))
        shield = float(prop(self.tank, 'shield_health'))
        if self.last_shield is not None and shield < self.last_shield-.001:
            self.shield_hits.append(dict(game_seconds=now, before=self.last_shield, after=shield,
                boss_health=float(prop(self.tank, 'health'))))
        self.last_shield = shield
        if shield <= 0 and self.shield_broken_at is None:
            self.shield_broken_at = now
        self.report['samples'].append(dict(game_seconds=now, phase=self.phase,
            player_health=float(prop(prop(self.hero, 'status'), 'state').health),
            player_cm=vec(self.hero.get_actor_location()), primary_held=self.primary,
            shield_health=shield, shield_broken_at=float(prop(self.tank, 'shield_broken_at')),
            tank_health=float(prop(self.tank, 'health')), tank_state=str(prop(self.tank, 'state')),
            tank_render_mesh=prop(model, 'skeletal_mesh_asset').get_path_name(),
            shield_bone=bone, shield_bone_world_cm=vec(model.get_socket_location(bone)) if bone else None,
            shield_surface_frame=self.shield_surface_frame(model, bone) if bone else None,
            cracks_visible=bool(cracks and cracks.is_visible()),
            cracks_attach_socket=str(cracks.get_attach_socket_name()) if cracks else None,
            cracks_section_count=int(cracks.get_num_sections()) if cracks else None,
            cracks_damage_fraction=1.-shield/max(1., float(prop(prop(self.tank, 'boss_settings'), 'shield_max_health'))),
            cracks_rendered_vertex_count=len(section[0]) if section else 0,
            cracks_rendered_triangle_count=len(section[1])//3 if section else 0,
            cracks_material_parent=material.get_base_material().get_path_name() if material else None,
            mage_attack=str(prop(self.mage, 'attack')), mage_state=str(prop(self.mage, 'state')), projectiles=flights,
            camera_evidence=self.camera_evidence()))

    def supported(self, point, margin=80.):
        floor = self.floor(point)
        if floor is None:
            return None
        nearby = [self.floor(floor+u.Vector(dx, dy, 0)) for dx, dy in
                  ((margin, 0), (-margin, 0), (0, margin), (0, -margin))]
        if any(item is None or abs(item.z-floor.z) > 35. for item in nearby):
            return None
        return floor

    def shield_surface_frame(self, model, bone):
        pose = model.get_socket_transform(bone, u.RelativeTransformSpace.RTS_WORLD)
        center = pose.transform_location(self.shield_local_center)
        direction = pose.transform_location(self.shield_local_normal)-pose.transform_location(u.Vector())
        length = math.sqrt(direction.x**2+direction.y**2+direction.z**2)
        self.require(length > .001, 'Native shield bone transform has a degenerate surface normal')
        normal = direction/length
        rotation = pose.rotation
        return dict(bone=bone, center_cm=vec(center), outward_normal_world=vec(normal),
            bone_world_location_cm=vec(pose.translation), bone_world_scale=vec(pose.scale3d),
            bone_world_quaternion=[float(rotation.x), float(rotation.y), float(rotation.z), float(rotation.w)],
            native_layout_source=self.shield_layout_path, native_layout_sha256=self.shield_layout_sha256,
            derivation='Actual rendering bone world transform applied to the same baked center/normal used by native shield cracks')

    def fixture_points(self):
        center, extent, _ = u.SystemLibrary.get_component_bounds(prop(self.tongue, 'surface'))
        radius = float(prop(prop(self.tank, 'settings'), 'body_radius'))
        for x in (-.15, 0., .15, -.3, .3, -.45, .45):
            for y in (0., -.15, .15, -.3, .3, -.45, .45):
                boss = self.supported(center+u.Vector(extent.x*x, extent.y*y, 0), radius+20.)
                hero = self.supported(boss+u.Vector(radius+85., 0, 0)) if boss else None
                # The shield lies on the opposite side from the old club-heavy
                # shot. Park the wizard away from that face for this close-up.
                caster = self.supported(boss+u.Vector(0, 700., 0), radius+20.) if boss else None
                if boss is not None and hero is not None and caster is not None:
                    # Keep normal food collision; choose clear approach and shield
                    # footprints so a persistent ground nut does not hide the cracks.
                    foods = self.foods()
                    if any(math.hypot(food.get_actor_location().x-hero.x, food.get_actor_location().y-hero.y) < 190.
                           or math.hypot(food.get_actor_location().x-boss.x, food.get_actor_location().y-boss.y) < radius+120.
                           for food in foods):
                        continue
                    return boss, hero, caster
        raise RuntimeError('No supported clear shield approach fixture was found')

    def freeze_walking(self, boss):
        settings = prop(boss, 'settings').copy()
        settings.set_editor_property('move_speed', 0.)
        boss.set_editor_property('settings', settings)

    def begin_shield(self, now):
        self.tank = next(actor for actor in self.actors(u.MCNutBoss) if prop(actor, 'boss_role') == u.MCNutBossRole.TANK)
        self.require(prop(self.tank, 'state') == u.MCNutBossState.IDLE
                     and prop(self.mage, 'state') == u.MCNutBossState.IDLE, 'Both native entrances must finish before the preview')
        self.ready(None, now)
        self.freeze_walking(self.tank)
        self.freeze_walking(self.mage)
        boss_floor, self.shield_hero_floor, mage_floor = self.fixture_points()
        for actor, floor in ((self.tank, boss_floor), (self.mage, mage_floor)):
            radius = float(prop(prop(actor, 'settings'), 'body_radius'))
            actor.set_actor_location_and_rotation(floor+u.Vector(0, 0, radius), u.Rotator(), False, True)
        self.move_to(self.shield_hero_floor, 180.)
        inventory = prop(self.hero, 'inventory')
        inventory.server_select(u.MCToolSlot.PICKAXE)
        # Settings is a private native cache and is not exposed by Python. The
        # public Profile is the same asset loaded by native BeginPlay/Damage.
        equipment = prop(inventory, 'profile')
        base_damage = float(prop(equipment, 'pickaxe_damage')) if equipment else 40.
        durability = max(100., min(250., base_damage*4.))
        boss_settings = prop(self.tank, 'boss_settings').copy()
        boss_settings.set_editor_property('shield_max_health', durability)
        self.tank.set_editor_property('boss_settings', boss_settings)
        self.tank.set_editor_property('shield_health', durability)
        self.last_shield = durability
        self.initial_tank_health = float(prop(self.tank, 'health'))
        self.report['fixture_tuning'] = dict(preview_shield_max_health=durability,
            initial_preview_shield_health=durability, ordinary_base_pickaxe_damage=base_damage,
            equipment_profile=equipment.get_path_name() if equipment else None,
            equipment_profile_read_source='Public MCInventoryComponent.Profile; native BeginPlay loads this into its private Settings cache, with the same 40 damage fallback',
            tank_move_speed=0., mage_move_speed=0., boss_floor_cm=vec(boss_floor),
            player_floor_cm=vec(self.shield_hero_floor), mage_floor_cm=vec(mage_floor),
            reason='Initial durability and position-only staging keep the short visual clip readable; subsequent health loss is real held-primary damage')
        model = prop(self.tank, 'tank_model')
        bone = next((name for name in ('hand.l', 'hand_l') if model.get_bone_index(name) >= 0), None)
        self.require(bone is not None, 'The actual tank model has no imported shield-hand bone')
        surface = self.shield_surface_frame(model, bone)
        point = u.Vector(*surface['center_cm'])
        outward = u.Vector(*surface['outward_normal_world'])
        # Looking almost along the shield's outward normal exposes its curved
        # outer surface; the actor-front camera instead framed the club/back.
        location = point+outward*380.+u.Vector(0, 0, 70.)
        self.frame_camera(location, point)
        prop(self.camera, 'camera_component').set_editor_property('field_of_view', 52.)
        self.report['shield_review_camera'] = dict(location_cm=vec(location), target_cm=vec(point),
            fov=52., outward_face_distance_cm=380., vertical_offset_cm=70., native_surface_frame=surface,
            placement='Actual hand bone and native baked shield face, facing the outer surface rather than actor front; wizard parked away')
        self.keep_review_camera()
        self.clip = 'shield_damage'
        self.stage('shield_ready')

    def begin_fire(self, now):
        self.primary_input(False)
        self.ready(None, now)
        center, extent, _ = u.SystemLibrary.get_component_bounds(prop(self.tongue, 'surface'))
        foods = self.foods()
        selected = None
        for x in (-.15, 0., .15, -.3, .3):
            for y in (-.15, 0., .15):
                caster = self.supported(center+u.Vector(extent.x*x, extent.y*y, 0), 140.)
                target = self.supported(caster+u.Vector(740, 0, 0)) if caster else None
                if caster is None or target is None:
                    continue
                tank = self.tank.get_actor_location()
                along = max(0., min(1., (tank.x-caster.x)/(target.x-caster.x)))
                nearest = caster+u.Vector((target.x-caster.x)*along, 0, 0)
                if math.hypot(tank.x-nearest.x, tank.y-nearest.y) < 280.:
                    continue
                if any(caster.x-100 < food.get_actor_location().x < target.x+100
                       and abs(food.get_actor_location().y-caster.y) < 160 for food in foods):
                    continue
                selected = caster, target
                break
            if selected:
                break
        self.require(selected is not None, 'No supported fire-nut flight corridor clear of real fallen nuts')
        caster, target = selected
        radius = float(prop(prop(self.mage, 'settings'), 'body_radius'))
        self.mage.set_actor_location_and_rotation(caster+u.Vector(0, 0, radius), u.Rotator(), False, True)
        self.move_to(target, 180.)
        self.report['fire_fixture'] = dict(caster_floor_cm=vec(caster), player_floor_cm=vec(target),
            native_fireball_radius=float(prop(prop(self.mage, 'boss_settings'), 'fireball_radius')),
            native_fireball_speed=float(prop(prop(self.mage, 'boss_settings'), 'fireball_speed')),
            player_health_before=float(prop(prop(self.hero, 'status'), 'state').health))
        midpoint=(caster+target)*.5+u.Vector(0, 0, 140)
        self.frame_camera(midpoint+u.Vector(160, 680, 260), midpoint)
        prop(self.camera, 'camera_component').set_editor_property('field_of_view', 78.)
        self.keep_review_camera()
        self.clip = 'fire_nut'
        self.ready('NextFireballAt', now+.55)
        self.stage('fire_nut')

    def tick(self, delta):
        if self.done:
            return
        if self.phase in ('waiting_pie', 'first_perk', 'perk_resolution', 'arrival', 'rain'):
            # No arrival frames belong to the two isolated VFX clips.
            Parent.tick(self, delta)
            if self.clip == 'storm_arrival':
                self.clip = None
            return
        try:
            self.require(time.monotonic()-self.started < 140., 'VFX probe exceeded 140 wall seconds')
            worlds = list(u.EditorLevelLibrary.get_pie_worlds(True))
            self.require(len(worlds) == 1 and self.world in worlds, 'Owned standalone PIE ended or was replaced')
            self.hero = self.pc.get_controlled_pawn()
            self.require(valid(self.hero) and isinstance(self.hero, u.MCToothCharacter), 'Owned tooth player was replaced')
            now = float(u.GameplayStatics.get_time_seconds(self.world))
            if self.last_game_time is not None and now <= self.last_game_time:
                return
            self.last_game_time = now
            age = now-self.phase_at
            self.keep_review_camera()
            if self.phase == 'boss_arrival':
                # The two queued native arrival alarms finish before the close-up.
                if age >= 8.:
                    self.report['boss_alarm_clearance_wait_seconds'] = age
                    self.begin_shield(now)
                return
            self.require(prop(self.hero, 'status').is_alive(), 'Observer died; native hazard damage was preserved')
            if now >= self.sample_at:
                self.sample_at = now+.08
                self.sample(now)
            if self.capture_hz and self.clip and now >= self.frame_at:
                self.frame_at = now+1./self.capture_hz
                camera = self.camera_evidence()
                if self.pc.get_view_target() == self.camera and camera['location_error_cm'] < 2. and camera['rotation_error_degrees'] < 2.:
                    self.screenshot('%05d.png'%len(self.report['frames']), True)
                    self.report['frames'][-1]['camera_evidence'] = camera
                else:
                    self.report.setdefault('skipped_camera_requests', []).append(dict(game_seconds=now, phase=self.phase,
                        clip_window=self.clip, reason='Actual camera manager has not adopted the owned review target/pose',
                        camera_evidence=camera))
            if self.phase == 'shield_ready' and age >= .8:
                evidence = self.camera_evidence()
                self.require(self.pc.get_view_target() == self.camera
                             and evidence['location_error_cm'] < 2. and evidence['rotation_error_degrees'] < 2.,
                             'The real camera did not adopt the shield face review pose before primary input')
                self.report['shield_healthy_camera_evidence'] = evidence
                self.report['checks']['shield_face_review_camera_verified'] = True
                self.screenshot('00_shield_healthy.png')
                self.primary_input(True)
                self.stage('shield_hits')
            elif self.phase == 'shield_hits':
                self.require(age < 9., 'Held normal Pickaxe input did not break the preview shield')
                self.move_to(self.shield_hero_floor, 180.)
                if self.shield_broken_at is not None:
                    self.primary_input(False)
                    self.report['checks']['ordinary_primary_hits_damage_shield_and_boss'] = (len(self.shield_hits) >= 3
                        and float(prop(self.tank, 'health')) < self.initial_tank_health)
                    self.require(self.report['checks']['ordinary_primary_hits_damage_shield_and_boss'],
                        'Preview needs at least three separate real shield damage observations')
                    self.report['shield_hits'] = self.shield_hits
                    self.stage('shield_break_tail')
            elif self.phase == 'shield_break_tail' and age >= 1.5:
                model = prop(self.tank, 'tank_model')
                variant = prop(prop(self.tank, 'boss_settings'), 'tank_unshielded_skeletal_mesh')
                self.require(valid(variant) and prop(model, 'skeletal_mesh_asset') == variant,
                    'Breaking the shield did not select the actual unshielded skeletal mesh')
                cracked = [sample for sample in self.report['samples'] if sample.get('cracks_visible')
                           and sample.get('cracks_damage_fraction', 0) > .001]
                self.require(cracked and len({sample['cracks_rendered_vertex_count'] for sample in cracked}) >= 3,
                             'Real shield damage did not grow the actual native mesh section through three stages')
                self.require(all(sample['cracks_material_parent'] == '/Game/Gameplay/VFX/NutCombat/M_NutShieldFracture.M_NutShieldFracture'
                                 and sample['cracks_attach_socket'] == sample['shield_bone'] for sample in cracked),
                             'Fractures must use their actual dedicated material and rendering bone')
                self.report['checks']['native_crack_growth_following_shield_bone'] = True
                self.report['checks']['native_break_changes_mesh_to_unshielded_variant'] = True
                self.screenshot('01_shield_broken.png')
                self.begin_fire(now)
            elif self.phase == 'fire_nut':
                if prop(self.mage, 'attack') == u.MCNutBossAttack.FIREBALL:
                    self.fire_started = True
                if age >= 6.:
                    self.require(self.fire_started and self.fire_paths and self.fire_impacts,
                        'Native wizard attack produced no observed projectile and terminal impact')
                    self.report['checks']['scheduled_fire_nut_projectile_and_terminal_impact'] = True
                    self.report['fire_projectiles'] = sorted(self.fire_paths)
                    self.report['fire_impacts'] = sorted(self.fire_impacts)
                    self.report['fire_player_health_after'] = float(prop(prop(self.hero, 'status'), 'state').health)
                    self.screenshot('02_fire_nut_after_impact.png')
                    self.stage('capture_flush')
            elif self.phase == 'capture_flush' and age >= .6:
                self.clip = None
                self.finish()
        except Exception:
            self.finish(traceback.format_exc())


def main():
    old = getattr(builtins, KEY, None)
    if old is not None and not getattr(old, 'done', True):
        raise RuntimeError('VFX probe already active; finish it before another starts')
    probe = NutFightVFXSolo()
    setattr(builtins, KEY, probe)
    try:
        probe.start()
    except Exception:
        probe.finish(traceback.format_exc())
        raise


if __name__ == '__main__':
    main()
