"""Bounded solo fallback: saved Fibre/CabbageGreenCutA walking contact.

This is an authored production fragment, NOT a two-knife-cut reproduction or a
claim that it is the user's screenshot object. Native FoodWalkingUsesRemaining-
Bounds and FoodWalkingRotatedBoundsClearance cover the actual thin-cut geometry.
Requires rebuilt idle L_Mouth with -MCHubTransientProfile. Globals:
MC_SLAB_EDITOR_PID, MC_SLAB_LOGFILE; optional MC_SLAB_OUTPUT, always on E:.
No direct HitFood/intake, asset/profile saves, tuning, launch, build or peers.
Cancel: builtins.mc_food_walking_slab_solo.finish('Stopped by operator').
"""
import builtins
import hashlib
import json
import math
import os
import runpy
import time
import traceback
from pathlib import Path

import unreal as u

ROOT = Path('E:/DEVGAME/MessControl')
OUTPUT = Path(globals().get('MC_SLAB_OUTPUT',
    'E:/CodexWork/MessControl/Validation/FoodWalkingOverlap_20261109/SlabSolo')).resolve()
PID = int(globals()['MC_SLAB_EDITOR_PID'])
LOG = Path(globals()['MC_SLAB_LOGFILE']).resolve()
assert os.getpid() == PID and OUTPUT.drive.upper() == LOG.drive.upper() == 'E:' and LOG.is_file()
support = runpy.run_path(str(ROOT/'Tools/Unreal/check_food_walking_collision_solo.py'),
    run_name='food_slab_walking_support', init_globals={
        'MC_WALKING_EDITOR_PID': PID, 'MC_WALKING_LOGFILE': str(LOG), 'MC_WALKING_OUTPUT': str(OUTPUT)})
Parent, prop, valid, path, vec, dot_xy = (
    support[k] for k in ('FoodWalkingSolo', 'prop', 'valid', 'path', 'vec', 'dot_xy'))
KEY, BATCH = 'mc_food_walking_slab_solo', support['BATCH']
MESH = '/Game/Stylized_Vegetables/Meshes/SM_CabbageGreenCutA.SM_CabbageGreenCutA'


def asset_ref(value):
    return value.split("'", 1)[1].rstrip("'") if "'" in value else value


class FoodWalkingSlabSolo(Parent):
    def __init__(self):
        super().__init__()
        self.report.update(mode='food_walking_saved_fibre_fragment_fallback',
            scope='Authored saved Fibre/CabbageGreenCutA at unchanged fragment scale; ordinary movement into its real walking proxy.',
            two_real_knife_cuts=False, screenshot_object_identity_verified=False,
            complementary_native_cases=[
                'MessControl.Networking.MovementContacts.FoodWalkingUsesRemainingBounds',
                'MessControl.Networking.MovementContacts.FoodWalkingRotatedBoundsClearance'])
        self.report['limitations'] = [
            'This bounded fallback spawns a saved production fragment; it does not claim two actual knife cuts, a sub50cm remaining thickness, or identity with the screenshot.',
            'The production row scales, mass, resistance, editable collision settings and automatic 110 cubed threshold are retained. Only owned fixture placement/orientation and post-landing body freeze are controlled.',
            'The native read-only CollisionData array cannot be transferred by this Python fixture. Its saved references are inspected only; the production missing-data fallback prepares the same saved mesh authored collision. This is not proof of the exact row baked-hull payload.',
            'AddMovementInput is the real production movement path, not physical WASD automation. No settled flag, collider response or collision geometry is overridden.',
            'A frozen authored fragment does not validate dynamic Chaos jitter, actual cut generation, networking, small-debris yielding, packaged behavior or performance.',
            'One owned transient-profile PIE only; scheduler is paused within that world and settings restored. No map/content/profile saves.',
            'Actual gameplay-camera frames are retained. Root visual inspection and native results are separate evidence.']
        self.report['source_sha256']['Tools/Unreal/check_food_walking_slab_solo.py'] = hashlib.sha256(
            (ROOT/'Tools/Unreal/check_food_walking_slab_solo.py').read_bytes()).hexdigest()

    def choose_rows(self):
        settings = prop(u.load_asset('/Game/Data/DA_FoodPhysics'), 'settings')
        limits = vec(prop(settings, 'max_auto_consume_dimensions'))
        self.require(all(abs(value-110.) < .001 for value in limits), 'Saved approved110cubed threshold changed')
        self.report['saved_auto_portion_limits_cm'] = limits
        table = u.load_asset('/Game/Data/DT_BreakfastMenu')
        raw = u.DataTableFunctionLibrary.export_data_table_to_json_string(table)
        if isinstance(raw, (tuple, list)):
            raw = next(value for value in raw if isinstance(value, str))
        rows = [row for row in json.loads(raw) if row.get('Name') == 'Fibre']
        self.require(len(rows) == 1, 'Expected the saved Fibre row')
        self.row = rows[0]
        self.require(MESH in [asset_ref(value) for value in self.row.get('FragmentMeshes', [])],
                     'CabbageGreenCutA is not a saved Fibre fragment; preserve this diagnostic')
        self.mesh = u.load_asset(MESH)
        self.require(valid(self.mesh), 'Saved CabbageGreenCutA mesh missing')
        self.object_type = u.ObjectTypeQuery.ECC_FOOD_WALKING_OBSTACLE
        self.report['walking_object_query'] = dict(object_type=str(self.object_type), exact_proxy_required=True)

    def fixture(self, _):
        row = self.row
        bounds = self.mesh.get_bounding_box()
        dimensions = [abs(getattr(bounds.max-bounds.min, axis.lower())*float(row['FragmentScale'][axis]))
                      for axis in ('X', 'Y', 'Z')]
        center, _, _ = u.SystemLibrary.get_component_bounds(prop(self.tongue, 'surface'))
        floor = self.floor(center)
        approach = self.floor(center-u.Vector(350., 0., 0.))
        self.require(floor is not None and approach is not None, 'No supported fixture/worker placement on the real tongue')
        self.move_to(approach, 0.)
        self.pc.set_view_target_with_blend(self.hero, 0.)
        pose = u.Transform(location=floor+u.Vector(100., 0., dimensions[2]*.5+8.), rotation=u.Rotator(yaw=45.))
        food = self.statics.call_method('BeginDeferredActorSpawnFromClass', args=(self.world,
            u.MCFoodActor.static_class(), pose, u.SpawnActorCollisionHandlingMethod.ALWAYS_SPAWN,
            None, u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
        self.require(valid(food), 'Deferred authored fragment spawn failed')
        data = prop(food, 'food_data')
        for field, key in (('scale', 'Scale'), ('fragment_scale', 'FragmentScale')):
            data.set_editor_property(field, u.Vector(*[float(row[key][axis]) for axis in ('X', 'Y', 'Z')]))
        count = max(1, min(64, int(row.get('Fragments', 1))))
        for field, value in (('mass', float(row['Mass'])/count), ('health', 25.), ('fragments', count),
                             ('spoil_seconds', float(row['SpoilSeconds'])), ('absorb_seconds', float(row['AbsorbSeconds']))):
            data.set_editor_property(field, value)
        data.set_editor_property('kind', u.MCFoodKind.FOOD)
        data.set_editor_property('resistance', getattr(u.MCFoodResistance, str(row['Resistance']).split('::')[-1].upper()))
        collision = prop(data, 'collision')
        for field, key in (('auto_optimize', 'bAutoOptimize'), ('whole_hull_limit', 'WholeHullLimit'),
                           ('fragment_hull_limit', 'FragmentHullLimit'), ('hull_vertex_limit', 'HullVertexLimit'),
                           ('voxel_resolution', 'VoxelResolution')):
            collision.set_editor_property(field, row['Collision'][key])
        data.set_editor_property('collision', collision)
        collision_assets = [u.load_asset(asset_ref(reference)) for reference in row.get('CollisionData', [])
                            if reference and reference != 'None']
        self.require(all(valid(asset) for asset in collision_assets), 'Saved collision-data references must remain valid')
        for field, value in (('food_data', data), ('item_name', row['Name']), ('item_mesh', self.mesh),
                             ('fragment', True), ('health', 25.), ('batch', BATCH)):
            food.set_editor_property(field, value)
        self.statics.call_method('FinishSpawningActor', args=(food, pose, u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
        self.food = self.remnant = food
        self.blocker = prop(food, 'walking_blocker')
        self.require(valid(self.blocker) and food.uses_automatic_cut_intake() and not food.fits_automatic_intake_portion(),
                     'Saved fragment must remain physical above its unchanged volume gate')
        self.baselines['walking'] = self.counters()
        self.report['fixtures'] = [dict(actor=path(food), mesh=path(self.mesh), row=row['Name'], saved_row=row,
            authored_fragment=True, generated_by_test_cut=False, saved_dimensions_cm=dimensions,
            saved_fragment_scale=row['FragmentScale'], collision_data_inspected=[path(asset) for asset in collision_assets],
            collision_data_copied=False, collision_source='Production missing-data fallback to saved mesh authored hulls',
            initial_pose=str(pose), physics_frozen_before_landing=False)]
        self.stage('fragment_landing')

    def prepare_fragment(self):
        if prop(self.food, 'phase') not in (u.MCFoodPhase.FREE, u.MCFoodPhase.STUCK):
            return
        body = prop(self.food, 'body')
        body.set_physics_linear_velocity(u.Vector())
        body.set_physics_angular_velocity_in_degrees(u.Vector())
        body.set_simulate_physics(False)
        center, extent, _ = u.SystemLibrary.get_component_bounds(prop(self.food, 'visual'))
        self.radius = float(self.hero.capsule_component.get_scaled_capsule_radius())
        self.half_height = float(self.hero.capsule_component.get_scaled_capsule_half_height())
        approach = self.floor(center-u.Vector(extent.x+self.radius+155., 0., 0.))
        self.require(approach is not None, 'Landed fragment has no supported real walking approach')
        self.move_to(approach, 0.)
        self.report['landed_fragment'] = dict(phase=str(prop(self.food, 'phase')), actor_pose=str(self.food.get_actor_transform()),
            body_frozen_after_natural_landing=True, visual_center_cm=vec(center), visual_extent_cm=vec(extent))
        self.stage('obstacle_settle')

    def prepare_contact(self):
        start = self.hero.get_actor_location()+u.Vector(0., 0., 3.)
        center, extent, _ = u.SystemLibrary.get_component_bounds(prop(self.food, 'visual'))
        hit = self.capsule_query(start, u.Vector(center.x+extent.x+self.radius+150., start.y, start.z))
        normal = hit[7]
        length = math.hypot(normal.x, normal.y)
        self.require(length > .6, 'Saved fragment front does not supply a substantial walking contact')
        normal = u.Vector(normal.x/length, normal.y/length, 0.)
        approach = self.floor(hit[5]+normal*(self.radius+155.))
        self.require(approach is not None, 'Exact fragment front has no supported approach')
        self.direction = -normal
        yaw = math.degrees(math.atan2(self.direction.y, self.direction.x))
        self.move_to(approach, yaw)
        self.pc.set_control_rotation(u.Rotator(pitch=-12., yaw=yaw))
        self.report['front_normal'] = vec(normal)
        self.stage('approach_settle')

    def tick(self, delta):
        if self.done:
            return
        if self.phase == 'waiting_pie':
            return super().tick(delta)
        try:
            self.require(time.monotonic()-self.started < 210., 'Owned fragment walking check exceeded210 wall seconds')
            self.require(self.owned_pie_is_active(), 'Owned PIE stopped or was replaced')
            now = float(u.GameplayStatics.get_time_seconds(self.world))
            if self.last_game_time is not None and now <= self.last_game_time:
                return
            self.last_game_time = now
            if now >= self.sample_at:
                self.sample_at = now+.08
                self.observe(now)
            if now >= self.frame_at:
                self.frame_at = now+1/self.capture_hz
                self.screenshot('%05d.png' % len(self.report['frames']), True)
            elapsed = now-self.phase_at
            if self.phase == 'fragment_landing':
                self.require(elapsed < 8., 'Authored saved fragment did not naturally land into Free/Stuck')
                if elapsed >= .8:
                    self.prepare_fragment()
            elif self.phase == 'obstacle_settle':
                self.require(elapsed < 4., 'Production fragment proxy did not arm in the clear footprint')
                if elapsed >= .6 and self.blocker_is_active():
                    self.prepare_contact()
            elif self.phase == 'approach_settle' and elapsed >= .4:
                self.begin_walk()
            elif self.phase == 'walk_into_remnant':
                self.observe_walk(now)
                self.hero.add_movement_input(self.direction, 1., False)
                if elapsed >= 1.8:
                    self.hero.get_movement_component().stop_movement_immediately()
                    self.stage('blocked_tail')
            elif self.phase == 'blocked_tail':
                self.observe_walk(now)
                if elapsed >= .3:
                    progress = dot_xy(self.hero.get_actor_location()-self.walk_start, self.direction)
                    self.require(progress > 40. and abs(progress-self.expected_progress) <= 25., 'Worker did not reach the real queried fragment front')
                    self.require(self.max_progress <= self.expected_progress+10., 'Worker crossed the actual fragment proxy contact plane')
                    self.require(self.counters() == self.baselines['walking'], 'Walking alone changed intake/score counters')
                    self.report['checks'].update(saved_fibre_fragment_scale_preserved=True,
                        natural_landing_observed=True, production_proxy_armed=True, exact_proxy_capsule_query=True,
                        actual_movement_reaches_and_stops_at_fragment=True, world_floor_health_standing_preserved=True,
                        no_extra_intake_without_input=True)
                    self.report.update(end_progress_cm=progress, maximum_progress_cm=self.max_progress,
                        expected_progress_cm=self.expected_progress, result_scope='authored_saved_fibre_fragment_walking_fallback_only')
                    self.finish()
        except Exception:
            self.finish(traceback.format_exc())


def main():
    previous = getattr(builtins, KEY, None)
    if previous is not None and not getattr(previous, 'done', True):
        raise RuntimeError('An owned slab probe is already active; it was preserved')
    probe = FoodWalkingSlabSolo()
    setattr(builtins, KEY, probe)
    try:
        probe.start()
    except Exception:
        probe.finish(traceback.format_exc())
        raise
    return probe


if __name__ == '__main__':
    main()
