"""One owned solo PIE: real knife independently resolves small food islands.

Run after the current Editor build, in -MCHubTransientProfile L_Mouth.
MC_ISLAND_LOGFILE must name this Editor's actual E: log. MC_ISLAND_OUTPUT is
optional. No direct HitFood/intake calls, asset saves, rescaling or extra peers.
Cancel: builtins.mc_food_islands_solo.finish('Stopped by operator').
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
OUTPUT = globals().get('MC_ISLAND_OUTPUT', 'E:/CodexWork/MessControl/Validation/FoodIslands_20261108/Solo')
LOGFILE = globals().get('MC_ISLAND_LOGFILE')
support = runpy.run_path(str(ROOT/'Tools/Unreal/check_food_auto_mouth_solo.py'),
    run_name='food_island_support', init_globals={'MC_AUTO_OUTPUT': OUTPUT, 'MC_AUTO_LOGFILE': LOGFILE})
Parent, prop, vec, valid, path, asset_ref = (support[k] for k in ('FoodAutoMouthSolo', 'prop', 'vec', 'valid', 'path', 'asset_ref'))
KEY = 'mc_food_islands_solo'
BATCH = 918193


def component_bounds_hint(mesh, scale):
    """Fixture-selection hint only. Native gameplay owns actual classification.

    Shared quantized full edges find promising authored disconnections; this
    deliberately does not reproduce native cap/T-junction or intake decisions.
    Assertions below use the accepted native staged log and real child actors.
    """
    vertices, triangles = [], []
    for section in range(mesh.get_num_sections(0)):
        positions, indices, _, _, _ = u.ProceduralMeshLibrary.get_section_from_static_mesh(mesh, 0, section)
        offset = len(vertices)
        vertices.extend(tuple(vec(v)) for v in positions)
        triangles.extend(tuple(offset+int(indices[i+k]) for k in range(3)) for i in range(0, len(indices), 3))
    parents = list(range(len(triangles)))

    def root(index):
        while parents[index] != index:
            parents[index] = parents[parents[index]]
            index = parents[index]
        return index

    edges = {}
    for index, triangle in enumerate(triangles):
        positions = [tuple(round(c/.001) for c in vertices[v]) for v in triangle]
        for k in range(3):
            a, b = positions[k], positions[(k+1) % 3]
            if a == b:
                continue
            edge = tuple(sorted((a, b)))
            if edge in edges:
                parents[root(index)] = root(edges[edge])
            else:
                edges[edge] = index
    groups = {}
    for index, triangle in enumerate(triangles):
        points = groups.setdefault(root(index), [])
        points.extend(vertices[v] for v in triangle)
    return [[(max(p[a] for p in points)-min(p[a] for p in points))*abs(scale[a]) for a in range(3)]
            for points in groups.values()]


class FoodIslandsSolo(Parent):
    def __init__(self):
        super().__init__()
        self.report.update(mode='food_islands', scope='One genuine knife input on unchanged saved-scale food with mixed disconnected geometry')
        self.report['limitations'].append('Python edge connectivity is used only to select a promising fixture. Native staged logs and real child actors determine the result; no native topology or transport result is fabricated.')
        self.report['source_sha256']['Source/MessControl/Private/MCFoodCutGeometry.cpp'] = hashlib.sha256((ROOT/'Source/MessControl/Private/MCFoodCutGeometry.cpp').read_bytes()).hexdigest()
        self.choice = None
        self.selection_started = False

    def choose_rows(self):
        settings = prop(u.load_asset('/Game/Data/DA_FoodPhysics'), 'settings')
        limits = vec(prop(settings, 'max_auto_consume_dimensions'))
        self.report['saved_auto_portion_limits_cm'] = limits
        table = u.load_asset('/Game/Data/DT_BreakfastMenu')
        raw = u.DataTableFunctionLibrary.export_data_table_to_json_string(table)
        if isinstance(raw, (tuple, list)):
            raw = next(v for v in raw if isinstance(v, str))
        candidates = []
        for row in json.loads(raw):
            if str(row.get('Kind', 'Food')).split('::')[-1] != 'Food':
                continue
            for fragment, field, scale_field in ((True, 'FragmentMeshes', 'FragmentScale'), (False, 'WholeMeshes', 'Scale')):
                for reference in row.get(field, []):
                    if reference and any(name in reference for name in ('Strawberry', 'Grapes', 'Grape_B')):
                        candidates.append((not fragment, 'Strawberry' not in reference, reference, row, scale_field))
        for whole, _, reference, row, scale_field in sorted(candidates, key=lambda c: (c[0], c[1], c[2])):
            mesh = u.load_asset(asset_ref(reference))
            if not valid(mesh) or not bool(prop(mesh, 'allow_cpu_access')):
                continue
            scale = [float(row[scale_field][a]) for a in ('X', 'Y', 'Z')]
            bounds = component_bounds_hint(mesh, scale)
            small = sum(max(size) > .001 and all(a <= b+.001 for a, b in zip(size, limits)) for size in bounds)
            large = sum(any(a > b+.001 for a, b in zip(size, limits)) for size in bounds)
            if small and large:
                self.choice = dict(row=row, mesh=mesh, fragment=not whole, scale=scale,
                                   hint_small=small, hint_large=large, hints=bounds)
                return
        raise RuntimeError('No saved-scale strawberry/grape fixture has a promising small+large boundary. Do not resize food or alter thresholds to fabricate a PASS.')

    def fixture(self, _):
        chosen = self.choice
        row, mesh, fragment = chosen['row'], chosen['mesh'], chosen['fragment']
        center, _, _ = u.SystemLibrary.get_component_bounds(prop(self.tongue, 'surface'))
        floor = self.floor(center)
        self.require(floor is not None, 'No supported interior knife approach')
        self.move_to(floor, 0.)
        self.pc.set_control_rotation(u.Rotator(pitch=-12, yaw=0))
        self.pc.set_view_target_with_blend(self.hero, 0.)
        native = mesh.get_bounding_box()
        size = [(getattr(native.max, a.lower())-getattr(native.min, a.lower()))*abs(chosen['scale'][i]) for i, a in enumerate(('X', 'Y', 'Z'))]
        pose = u.Transform(location=floor+u.Vector(size[0]*.5+75, 0, size[2]*.5+8))
        food = self.statics.call_method('BeginDeferredActorSpawnFromClass', args=(self.world, u.MCFoodActor.static_class(), pose,
            u.SpawnActorCollisionHandlingMethod.ALWAYS_SPAWN, None, u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
        self.require(valid(food), 'Deferred saved food spawn failed')
        data = prop(food, 'food_data')
        for field, key in (('scale', 'Scale'), ('fragment_scale', 'FragmentScale')):
            data.set_editor_property(field, u.Vector(*[float(row[key][a]) for a in ('X', 'Y', 'Z')]))
        count = max(1, min(64, int(row.get('Fragments', 1))))
        mass = float(row['Mass'])/count if fragment else float(row['Mass'])
        health = 25. if fragment else float(row['Health'])
        for field, value in (('mass', mass), ('health', health), ('fragments', count)):
            data.set_editor_property(field, value)
        for field, key in (('spoil_seconds', 'SpoilSeconds'), ('absorb_seconds', 'AbsorbSeconds')):
            if key in row:
                data.set_editor_property(field, float(row[key]))
        data.set_editor_property('kind', u.MCFoodKind.FOOD)
        data.set_editor_property('resistance', getattr(u.MCFoodResistance, str(row.get('Resistance', 'Automatic')).split('::')[-1].upper()))
        if 'CrumbColor' in row:
            data.set_editor_property('crumb_color', u.LinearColor(*[float(row['CrumbColor'].get(a, 1)) for a in ('R', 'G', 'B', 'A')]))
        food.set_editor_property('food_data', data)
        food.set_editor_property('item_name', row['Name'])
        food.set_editor_property('item_mesh', mesh)
        food.set_editor_property('fragment', fragment)
        food.set_editor_property('health', health)
        food.set_editor_property('batch', BATCH)
        self.statics.call_method('FinishSpawningActor', args=(food, pose, u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
        prop(food, 'body').set_simulate_physics(False)
        prop(food, 'label').set_visibility(False)
        self.require(food.uses_automatic_cut_intake(), 'Saved profile did not enable automatic intake')
        self.food = food
        self.source = prop(u.GameplayStatics.get_game_state(self.world), 'food_cut_intake_presentation')
        self.before = self.counters()
        prop(self.hero, 'inventory').call_method('ServerSelect', args=(u.MCToolSlot.KNIFE,))
        self.report['fixtures'] = [dict(actor=path(food), row=row['Name'], mesh=path(mesh), fragment=fragment,
            batch=BATCH, saved_scale=chosen['scale'], saved_row=row, saved_bounds_cm=size,
            heuristic_component_sizes_cm=chosen['hints'], heuristic_is_not_gameplay_proof=True)]
        self.stage('island_before')

    def intake_log_lines(self):
        with Path(LOGFILE).open('rb') as stream:
            stream.seek(self.log_offset)
            lines = stream.read().decode('utf8', errors='replace').splitlines()
        return [line for line in lines if 'MC_FOOD_AUTO_INTAKE' in line and 'batch='+str(BATCH) in line]

    def staged(self):
        with Path(LOGFILE).open('rb') as stream:
            stream.seek(self.log_offset)
            lines = stream.read().decode('utf8', errors='replace').splitlines()
        for line in reversed(lines):
            if 'MC_FOOD_ISLANDS_STAGED' in line and 'batch='+str(BATCH) in line:
                match = re.search(r'parts=(\d+) ready=(\d+) mass=([\d.eE+-]+) units=(\d+)', line)
                if match:
                    return dict(parts=int(match[1]), ready=int(match[2]), mass=float(match[3]), units=int(match[4]), raw=line)
        return None

    def tick(self, delta):
        if self.done:
            return
        if self.phase == 'waiting_pie':
            return super().tick(delta)
        try:
            self.require(time.monotonic()-self.started < 210, 'Owned island solo exceeded 210 wall seconds')
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
            if self.phase == 'island_before':
                self.require(elapsed < 10, 'Actual knife selection or work gates did not settle')
                if elapsed >= 2 and prop(prop(self.hero, 'inventory'), 'selected') == u.MCToolSlot.KNIFE and self.work_state()['ready']:
                    self.primary_input(True)
                    self.stage('island_cut')
            elif self.phase == 'island_cut':
                self.require(elapsed < 3, 'Actual knife did not produce accepted independent parts')
                staged = self.staged()
                if staged and prop(self.food, 'phase') == u.MCFoodPhase.DISPOSED:
                    self.primary_input(False)
                    self.require(0 < staged['ready'] < staged['parts'], 'Native result did not contain both ready and large parts')
                    parts = [f for f in self.actors(u.MCFoodActor) if int(prop(f, 'batch')) == BATCH and f != self.food]
                    self.require(len(parts) == staged['parts'], 'Actual staged child count does not match the accepted source log')
                    records = []
                    for part in parts:
                        body = prop(part, 'body')
                        records.append(dict(actor=path(part), size_cm=vec(part.get_hand_portion_size()),
                            ready=bool(part.fits_automatic_intake_portion()), disposed=prop(part, 'phase') == u.MCFoodPhase.DISPOSED,
                            hidden=bool(prop(part, 'hidden')), simulating=body.is_simulating_physics(),
                            collision=str(body.get_collision_enabled()), mass=float(prop(prop(part, 'food_data'), 'mass'))))
                    ready = [r for r in records if r['ready']]
                    large = [r for r in records if not r['ready']]
                    self.require(len(ready) == staged['ready'] and len(large) > 0, 'Actual dimensions do not match independent readiness')
                    self.require(all(r['disposed'] and r['hidden'] and not r['simulating'] for r in ready), 'A completed small part remained physical/visible')
                    self.require(all(not r['disposed'] and not r['hidden'] for r in large), 'A large neighbour was incorrectly completed')
                    self.report['native_staged'] = staged
                    self.report['independent_parts'] = records
                    self.after = self.counters()
                    self.require(self.after['swallowed']-self.before['swallowed'] == staged['ready'], 'Small parts did not independently reach accepted intake')
                    self.report['checks'].update(small_parts_complete_independently=True, large_parts_remain=True,
                        accepted_small_count=staged['ready'], remaining_large_count=len(large),
                        source_hidden=bool(prop(self.food, 'hidden')), score_delta=self.after['points']-self.before['points'])
                    self.stage('island_tail')
            elif self.phase == 'island_tail' and elapsed >= 3 and now-self.video_start >= 9:
                current = self.counters()
                accepted = self.intake_log_lines()
                self.require(current == self.after, 'Released input produced duplicate credit/intake')
                self.require(len(accepted) == self.report['native_staged']['ready'], 'Accepted intake log count differs from staged small parts')
                active_effects = [r for r in self.report['niagara'] if r['active'] and 'MCPerkVFXCompositionPlayer_' in r['component']]
                self.require(active_effects, 'No actual active AutoMouth Niagara component was observed')
                self.report['checks'].update(no_duplicate_credit=True, accepted_native_intake_logged=True,
                    actual_active_niagara=True, gameplay_camera_retained=self.pc.get_view_target() == self.hero)
                self.finish()
        except Exception:
            self.finish(traceback.format_exc())


def main():
    old = getattr(builtins, KEY, None)
    if old is not None and not getattr(old, 'done', True):
        raise RuntimeError('An island solo check is already active; it was preserved')
    probe = FoodIslandsSolo()
    setattr(builtins, KEY, probe)
    try:
        probe.start()
    except Exception:
        probe.finish(traceback.format_exc())
        raise


if __name__ == '__main__':
    main()
