"""Focused saved-scale food edge cut; reuse the existing owned solo recipe.

Requires the rebuilt, idle -MCHubTransientProfile L_Mouth Editor and its actual
E: log as MC_VOLUME_LOGFILE. No build, Editor launch, asset save or direct
HitFood/intake invocation. Source selection bounds are hints, never the oracle.
Cancel: builtins.mc_food_volume_solo.finish('Stopped by operator').
"""
import builtins
import hashlib
import json
import re
import runpy
import time
import traceback
from pathlib import Path

import unreal as u

ROOT = Path('E:/DEVGAME/MessControl')
OUTPUT = Path(globals().get('MC_VOLUME_OUTPUT', 'E:/CodexWork/MessControl/Validation/FoodVolume_20261108/Solo'))
LOGFILE = globals().get('MC_VOLUME_LOGFILE')
support = runpy.run_path(str(ROOT/'Tools/Unreal/check_food_auto_mouth_solo.py'),
    run_name='food_volume_support', init_globals={'MC_AUTO_OUTPUT': str(OUTPUT), 'MC_AUTO_LOGFILE': LOGFILE})
Parent, prop, vec, valid, path, asset_ref = (support[k] for k in ('FoodAutoMouthSolo', 'prop', 'vec', 'valid', 'path', 'asset_ref'))
KEY, BATCH = 'mc_food_volume_solo', 918207


class FoodVolumeSolo(Parent):
    def __init__(self):
        super().__init__()
        self.choice = None
        self.report.update(mode='food_volume_edge_cut', scope='One real knife input on unchanged saved-scale food; explicit long-sliver coverage result')
        self.report['active_niagara_after_cut'] = []
        self.report['source_sha256']['Source/MessControl/Private/MCFoodCutGeometry.cpp'] = hashlib.sha256((ROOT/'Source/MessControl/Private/MCFoodCutGeometry.cpp').read_bytes()).hexdigest()
        self.report['limitations'].append('An outer cap is selected by bounds only. Whether the actual child stays longer than 220 cm and leaves a large sibling is recorded, not assumed. A successful ordinary cut without that geometry is functional-only evidence; native sliver tests cover the exact metric.')

    def choose_rows(self):
        settings = prop(u.load_asset('/Game/Data/DA_FoodPhysics'), 'settings')
        limits = vec(prop(settings, 'max_auto_consume_dimensions'))
        volume_limit = abs(limits[0]*limits[1]*limits[2])
        self.report['saved_auto_portion_limits_cm'] = limits
        table = u.load_asset('/Game/Data/DT_BreakfastMenu')
        raw = u.DataTableFunctionLibrary.export_data_table_to_json_string(table)
        if isinstance(raw, (tuple, list)):
            raw = next(v for v in raw if isinstance(v, str))
        choices = []
        for row in json.loads(raw):
            if str(row.get('Kind', 'Food')).split('::')[-1] != 'Food' or str(row.get('Resistance', '')).split('::')[-1] == 'Hard':
                continue
            for fragment, field, scale_field in ((False, 'WholeMeshes', 'Scale'), (True, 'FragmentMeshes', 'FragmentScale')):
                for reference in row.get(field, []):
                    if not reference or reference == 'None':
                        continue
                    mesh = u.load_asset(asset_ref(reference))
                    if not valid(mesh) or not bool(prop(mesh, 'allow_cpu_access')):
                        continue
                    bounds = mesh.get_bounding_box()
                    size = [abs(getattr(bounds.max-bounds.min, a.lower())*float(row[scale_field][a])) for a in ('X', 'Y', 'Z')]
                    if min(size) <= .01 or max(size) <= max(limits):
                        continue
                    axis = min(range(3), key=lambda a: size[a])
                    box_volume = size[0]*size[1]*size[2]
                    fraction = min(.16, volume_limit/(2.*box_volume))
                    if fraction < .03:
                        continue
                    preferred = not any(name in reference for name in ('Apple', 'Strawberry'))
                    choices.append((preferred, box_volume <= volume_limit*2, fragment, reference,
                                    dict(row=row, mesh=mesh, fragment=fragment, size=size, axis=axis, fraction=fraction, box_volume=box_volume)))
        self.require(bool(choices), 'No unchanged saved CPU food has an oversized non-cut axis and a practical grazing cap; native sliver test is required instead')
        self.choice = sorted(choices, key=lambda c: c[:4])[0][-1]

    def fixture(self, _):
        c = self.choice
        row, mesh, size, axis = c['row'], c['mesh'], c['size'], c['axis']
        center, _, _ = u.SystemLibrary.get_component_bounds(prop(self.tongue, 'surface'))
        floor = self.floor(center)
        self.require(floor is not None, 'No supported knife approach')
        self.move_to(floor, 0.)
        self.pc.set_control_rotation(u.Rotator(pitch=-12, yaw=0))
        self.pc.set_view_target_with_blend(self.hero, 0.)
        rotation = u.Rotator(yaw=90) if axis == 0 else u.Rotator(roll=90) if axis == 2 else u.Rotator()
        world_size = [size[1], size[0], size[2]] if axis == 0 else [size[0], size[2], size[1]] if axis == 2 else size
        # Native knife plane is Hero.Right. Shift the real mesh across it;
        # never change authored scale or the acceptance threshold.
        edge_offset = world_size[1]*(.5-c['fraction'])
        pose = u.Transform(location=floor+u.Vector(world_size[0]*.5+75, edge_offset, world_size[2]*.5+8), rotation=rotation)
        food = self.statics.call_method('BeginDeferredActorSpawnFromClass', args=(self.world,
            u.MCFoodActor.static_class(), pose, u.SpawnActorCollisionHandlingMethod.ALWAYS_SPAWN,
            None, u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
        self.require(valid(food), 'Deferred real food spawn failed')
        data = prop(food, 'food_data')
        for field, key in (('scale', 'Scale'), ('fragment_scale', 'FragmentScale')):
            data.set_editor_property(field, u.Vector(*[float(row[key][a]) for a in ('X', 'Y', 'Z')]))
        count = max(1, min(64, int(row.get('Fragments', 1))))
        mass = float(row['Mass'])/count if c['fragment'] else float(row['Mass'])
        health = 25. if c['fragment'] else float(row['Health'])
        for field, value in (('mass', mass), ('health', health), ('fragments', count)):
            data.set_editor_property(field, value)
        for field, key in (('spoil_seconds', 'SpoilSeconds'), ('absorb_seconds', 'AbsorbSeconds')):
            if key in row:
                data.set_editor_property(field, float(row[key]))
        data.set_editor_property('kind', u.MCFoodKind.FOOD)
        data.set_editor_property('resistance', getattr(u.MCFoodResistance, str(row.get('Resistance', 'Automatic')).split('::')[-1].upper()))
        if 'CrumbColor' in row:
            data.set_editor_property('crumb_color', u.LinearColor(*[float(row['CrumbColor'].get(a, 1)) for a in ('R', 'G', 'B', 'A')]))
        for field, value in (('food_data', data), ('item_name', row['Name']), ('item_mesh', mesh),
                             ('fragment', c['fragment']), ('health', health), ('batch', BATCH)):
            food.set_editor_property(field, value)
        self.statics.call_method('FinishSpawningActor', args=(food, pose, u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
        prop(food, 'body').set_simulate_physics(False)
        prop(food, 'label').set_visibility(False)
        self.food = food
        self.require(food.uses_automatic_cut_intake() and not food.fits_automatic_intake_portion(), 'Saved source does not require this real cut')
        self.source = prop(u.GameplayStatics.get_game_state(self.world), 'food_cut_intake_presentation')
        self.baselines['volume'] = self.counters()
        prop(self.hero, 'inventory').call_method('ServerSelect', args=(u.MCToolSlot.KNIFE,))
        self.report['fixtures'] = [dict(actor=path(food), row=row['Name'], mesh=path(mesh), fragment=c['fragment'], batch=BATCH,
            saved_dimensions_cm=size, saved_row=row, cut_axis=axis, actor_rotation=str(rotation), edge_fraction_hint=c['fraction'],
            edge_offset_cm=edge_offset, cap_bbox_volume_upper_hint_cm3=c['box_volume']*c['fraction'], physics_frozen_for_contact=True)]
        self.stage('volume_before')

    def intake_log_lines(self):
        with Path(LOGFILE).open('rb') as stream:
            stream.seek(self.log_offset)
            return [line for line in stream.read().decode('utf8', errors='replace').splitlines()
                    if 'MC_FOOD_AUTO_INTAKE' in line and 'batch='+str(BATCH) in line]

    def observe(self, now):
        super().observe(now)
        if self.phase not in ('volume_cut', 'volume_tail') or not valid(self.food) or prop(self.food, 'phase') != u.MCFoodPhase.DISPOSED:
            return
        # A prewarmed component can be reused without changing its object ID.
        # Check its current activation after the real cut, independently of
        # Parent.niagara_seen (which also records inactive pooled components).
        for component in u.ObjectIterator(u.NiagaraComponent):
            if not valid(component) or component.get_world() != self.world:
                continue
            asset = component.get_asset()
            if valid(asset) and asset.get_name() == 'NS_Food_AutoMouth' and component.is_active():
                self.report['active_niagara_after_cut'].append(dict(time=now, phase=self.phase, component=path(component), system=path(asset), active=True))

    def inspect_children(self):
        records = []
        for part in self.actors(u.MCFoodActor):
            if part == self.food or int(prop(part, 'batch')) != BATCH:
                continue
            body = prop(part, 'body')
            disposed = prop(part, 'phase') == u.MCFoodPhase.DISPOSED
            records.append(dict(actor=path(part), actor_name=part.get_name(), size_cm=vec(part.get_hand_portion_size()),
                disposed=disposed, fits=bool(part.fits_automatic_intake_portion()), hidden=bool(prop(part, 'hidden')),
                mass=float(prop(prop(part, 'food_data'), 'mass')), replicated=bool(prop(part, 'replicates')),
                simulating=body.is_simulating_physics(), collision=str(body.get_collision_enabled())))
        return records

    def tick(self, delta):
        if self.done:
            return
        if self.phase == 'waiting_pie':
            return super().tick(delta)
        try:
            self.require(time.monotonic()-self.started < 210, 'Owned volume solo exceeded 210 wall seconds')
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
            if self.phase == 'volume_before':
                self.require(elapsed < 10, 'Real knife selection/readiness did not settle')
                if elapsed >= 2 and self.work_state()['ready'] and prop(prop(self.hero, 'inventory'), 'selected') == u.MCToolSlot.KNIFE:
                    self.primary_input(True)
                    self.stage('volume_cut')
            elif self.phase == 'volume_cut':
                self.require(elapsed < 3, 'Grazing real knife made no accepted cut; inspect contact recording without resizing the asset')
                if prop(self.food, 'phase') == u.MCFoodPhase.DISPOSED:
                    self.primary_input(False)
                    # Keep immediate actor state even if receipts expire during
                    # the tail. The file log is buffered; correlate it later.
                    self.report['children_at_acceptance'] = self.inspect_children()
                    self.report['accepted_baseline'] = self.counters()
                    self.stage('volume_tail')
            elif self.phase == 'volume_tail' and elapsed >= 2.5:
                lines = self.intake_log_lines()
                accepted_names = {re.search(r' part=(\S+)', line).group(1) for line in lines if re.search(r' part=(\S+)', line)}
                records = self.report['children_at_acceptance']
                for record in records:
                    record['accepted_logged'] = record['actor_name'] in accepted_names
                accepted = [r for r in records if r['accepted_logged']]
                large = [r for r in records if not r['disposed'] and not r['fits']]
                self.require(bool(accepted), 'The genuine cut accepted no logged child after the log-flush tail; keep this as a diagnostic, not a volume PASS')
                self.require(all(r['disposed'] if r['fits'] else not r['hidden'] for r in records), 'A ready receipt remained stranded or a large child is hidden')
                self.report['checks'].update(real_primary_cut_completed=True, accepted_children=len(accepted), large_children=len(large),
                    long_over_220_child_accepted=any(max(r['size_cm']) > 220.01 for r in accepted), large_sibling_remains=bool(large))
                self.report['metric_case_covered'] = self.report['checks']['long_over_220_child_accepted'] and bool(large)
                if not self.report['metric_case_covered']:
                    self.report['limitations'].append('This unchanged saved-scale contact did not produce both an accepted child longer than 220 cm and a large sibling. It verifies real cut/intake only; native LongThinCutUsesOwnVolumeAndCompletesOnce supplies the exact sliver boundary.')
                self.report['result_scope'] = 'long_sliver_and_large_sibling' if self.report['metric_case_covered'] else 'functional_real_cut_only'
                self.report['checks']['no_extra_intake_without_input'] = self.counters() == self.report['accepted_baseline']
                self.report['checks']['actual_niagara_observed'] = bool(self.report['active_niagara_after_cut'])
                self.report['checks']['gameplay_camera_retained'] = self.pc.get_view_target() == self.hero
                self.require(all(self.report['checks'][k] for k in ('no_extra_intake_without_input', 'actual_niagara_observed', 'gameplay_camera_retained')), 'Tail/camera/Niagara check failed')
                self.finish()
        except Exception:
            self.finish(traceback.format_exc())


def main():
    old = getattr(builtins, KEY, None)
    if old is not None and not getattr(old, 'done', True):
        raise RuntimeError('An owned volume probe is already active; it was preserved')
    probe = FoodVolumeSolo()
    setattr(builtins, KEY, probe)
    try:
        probe.start()
    except Exception:
        probe.finish(traceback.format_exc())
        raise


if __name__ == '__main__':
    main()
