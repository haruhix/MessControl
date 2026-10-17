"""Short real F3 first-wave proof: progressive tongue and tooth dirt, then brush.

Root-run only in idle L_Mouth after integration. Three existing arena teeth have
their native coffee amount cleared BEFORE F3 as a disclosed visibility fixture;
no masks, water clock/deposition, damage, AI or care helpers are assigned after
launch. Normal held brush input proves the real wave residue remains cleanable.
One standalone authority PIE; no saves/builds/network. Genuine 12 Hz captures.
Cancel: builtins.mc_coffee_wave_stains_solo.finish('Stopped by operator').
"""
import builtins
import hashlib
import json
import math
import runpy
import traceback
from pathlib import Path
import unreal as u

ROOT = Path(u.Paths.project_dir()).resolve()
OUTPUT = Path(globals().get('MC_COFFEE_WAVE_OUTPUT', ROOT/'Saved/CoffeeWaveStains/Capture')).resolve()
support = runpy.run_path(str(ROOT/'Tools/Unreal/probe_jelly_paint_solo.py'),
    run_name='coffee_wave_support', init_globals={'MC_PAINT_OUTPUT': OUTPUT, 'MC_PAINT_CAPTURE_HZ': 12})
Parent, Base, prop, valid, path, vec, enum_name = (
    support[n] for n in ('JellyPaintSolo', 'Parent', 'prop', 'valid', 'path', 'vec', 'enum_name'))
KEY = 'mc_coffee_wave_stains_solo'


def fit_tooth_review(low, high, inward):
    """Fit an 18%-padded world AABB in the clear right-hand gameplay area.

    This is ordinary perspective math, with no engine state or tooth changes.
    The actual crown centre is the subject anchor; a calculated pan composes it
    at x=.60 instead of hiding the crown beneath the left HUD. All eight padded
    corners must fit, including their different depths along the viewing ray.
    """
    def dot(a, b):
        return sum(x*y for x, y in zip(a, b))

    def unit(a):
        scale = math.sqrt(dot(a, a))
        if not math.isfinite(scale) or scale < 1.e-6:
            raise RuntimeError('Degenerate tooth-review camera basis')
        return tuple(v/scale for v in a)

    def cross(a, b):
        return (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])

    center = tuple((a+b)*.5 for a, b in zip(low, high))
    extent = tuple((b-a)*.5 for a, b in zip(low, high))
    if not all(math.isfinite(v) and v > 0. for v in extent):
        raise RuntimeError('Invalid actual tooth Visual bounds')
    outward = unit((inward[0], inward[1], .28))
    to_anchor = tuple(-v for v in outward)
    tan_h, aspect = math.tan(math.radians(68.)*.5), 16./9.
    tan_v = tan_h/aspect
    safe = (.32, .26, .82, .80)  # left HUD, top progress strip, bottom prompts

    def axes(pan):
        c, s = math.cos(pan), math.sin(pan)
        forward = (to_anchor[0]*c+to_anchor[1]*s,
                   to_anchor[1]*c-to_anchor[0]*s, to_anchor[2])
        right = unit((-forward[1], forward[0], 0.))
        return forward, right, cross(forward, right)

    # Solve the pan from the desired on-screen crown centre, not a world offset.
    left, right_pan = 0., math.radians(35.)
    for _ in range(40):
        pan = (left+right_pan)*.5
        forward, right, _up = axes(pan)
        anchor_x = .5+.5*dot(to_anchor, right)/(dot(to_anchor, forward)*tan_h)
        if anchor_x < .60:
            left = pan
        else:
            right_pan = pan
    forward, right, up = axes((left+right_pan)*.5)
    advance = dot(to_anchor, forward)
    x_advance, y_advance = dot(to_anchor, right), dot(to_anchor, up)
    x_limits = ((2.*safe[0]-1.)*tan_h, (2.*safe[2]-1.)*tan_h)
    y_limits = ((1.-2.*safe[3])*tan_v, (1.-2.*safe[1])*tan_v)
    padded = [tuple(extent[i]*1.18*sign[i] for i in range(3))
              for sign in ((x, y, z) for x in (-1, 1) for y in (-1, 1) for z in (-1, 1))]
    distance = 0.
    for corner in padded:
        depth = dot(corner, forward)
        # Keep the camera safely in front of the entire padded bounding box.
        distance = max(distance, (max(extent)*.02-depth)/advance)
        for coord, drift, limits in ((dot(corner, right), x_advance, x_limits),
                                     (dot(corner, up), y_advance, y_limits)):
            lo, hi = limits
            distance = max(distance, (lo*depth-coord)/(drift-lo*advance),
                           (coord-hi*depth)/(hi*advance-drift))
    distance *= 1.005
    eye = tuple(center[i]+outward[i]*distance for i in range(3))
    look_target = tuple(eye[i]+forward[i]*distance for i in range(3))
    projected = []
    for corner in padded:
        delta = tuple(corner[i]+to_anchor[i]*distance for i in range(3))
        depth = dot(delta, forward)
        projected.append((.5+.5*dot(delta, right)/(depth*tan_h),
                          .5-.5*dot(delta, up)/(depth*tan_v)))
    if not all(safe[0]-1.e-6 <= x <= safe[2]+1.e-6
               and safe[1]-1.e-6 <= y <= safe[3]+1.e-6 for x, y in projected):
        raise RuntimeError('Tooth review failed the padded eight-corner frustum fit')
    return dict(anchor_cm=center, eye_cm=eye, look_target_cm=look_target,
                bounds_min_cm=low, bounds_max_cm=high, padding_scale=1.18,
                horizontal_fov_degrees=68., aspect_ratio=aspect,
                hud_safe_rect=safe, padded_corner_screen_xy=projected,
                distance_cm=distance, centre_screen_x=.60)


class CoffeeWaveStainsSolo(Parent):
    def __init__(self):
        super().__init__()
        self.prepared = False
        self.fixture_teeth = []
        self.wave_history = []
        self.liquid = None
        self.report.update(mode='coffee_wave', purpose='Native advancing wave creates tongue and tooth dirt before finishing',
            samples=[], checks={}, clips={}, frames=[], screenshots=[],
            clip_labels={'coffee_wave_progressive': 'Кофейная волна оставляет пятна по мере прохождения',
                         'coffee_wave_teeth': 'Настоящие пятна волны на зубах',
                         'coffee_film_closeup': 'Неподвижный близкий кадр мокрой кофейной плёнки',
                         'coffee_wave_brush': 'Обычная щётка стирает кофейную плёнку',
                         'coffee_film_after_wipe': 'Тот же близкий кадр после настоящей уборки'},
            limitations=['Exactly three existing teeth are cleared with native SetCoffee(0) before F3, to make new deposition visible. Their before/after state is recorded; no tooth mask is assigned.',
                'The native F3 jelly branch first wave advances naturally. No water/phase clock, deposit, event health, damage or jelly AI tuning is changed.',
                'The unrelated single-day scheduler is paused/restored by the existing parent probe.',
                'A temporary wide/tooth review camera and a disclosed supported observer placement for normal held brush are fixtures.',
                'Only one wave patch is brushed. Full encounter cleanup, network behavior, balance and GPU cost are deferred.',
                'Genuine 12 Hz engine frames require visual review; telemetry alone does not establish shader quality.'])
        for source in ('Tools/Unreal/probe_coffee_wave_stains_solo.py',
                       'Source/MessControl/Private/MCCoffeeFlood.cpp', 'Source/MessControl/Public/MCCoffeeFlood.h',
                       'Source/MessControl/Private/MCMouthLiquid.cpp', 'Tools/Unreal/author_coffee_stain_film_v3.py'):
            self.report['source_sha256'][source] = hashlib.sha256((ROOT/source).read_bytes()).hexdigest()

    def write(self):
        self.output.mkdir(parents=True, exist_ok=True)
        data = json.dumps(self.report, ensure_ascii=False, indent=2, allow_nan=False)
        (self.output/'Report.json').write_text(data, encoding='utf-8')
        OUTPUT.mkdir(parents=True, exist_ok=True)
        (OUTPUT/'Report_coffee_wave.json').write_text(data, encoding='utf-8')

    def tooth_state(self, tooth):
        state = prop(prop(tooth, 'status'), 'state')
        mask = [int(v) for v in prop(tooth, 'grime_mask')]
        return dict(path=path(tooth), tooth_id=int(prop(prop(tooth, 'state'), 'tooth_id')),
            coffee_left=int(prop(state, 'coffee_left')), mask_sum=sum(mask),
            masked_cells=sum(v > 63 for v in mask), visible_care=int(prop(state, 'coffee_left')) > 0)

    def prepare_baseline(self):
        teeth = sorted((t for t in self.actors(u.MCArenaTooth) if t.is_available()),
                       key=lambda t: t.get_actor_location().x)
        self.require(len(teeth) >= 3, 'Need three existing teeth for the pre-wave visibility fixture')
        self.fixture_teeth = [teeth[0], teeth[len(teeth)//2], teeth[-1]]
        entries = []
        for tooth in self.fixture_teeth:
            before = self.tooth_state(tooth)
            tooth.set_coffee(0.)
            after = self.tooth_state(tooth)
            self.require(not after['visible_care'], 'Native pre-event tooth clear failed')
            entries.append(dict(before=before, after=after, native_call='SetCoffee(0)', timing='Before F3'))
        self.report['tooth_visibility_fixture'] = entries
        self.prepared = True

    def wave_patches(self):
        return [p for p in self.actors(u.MCMouthSurface) if p.actor_has_tag('MCCoffeeWaveResidue')
                and path(p) not in self.initial_patch_paths]

    @staticmethod
    def public_wave_duration(liquid):
        # Seconds is a native-only replicated field. These authored settings are
        # explicitly exposed and describe the same natural complete cycle.
        settings = prop(liquid, 'water_settings')
        return sum(float(prop(settings, key)) for key in ('fill_seconds', 'hold_seconds', 'drain_seconds')) \
            * int(prop(settings, 'cycles'))

    def sample(self, now):
        Base.sample(self, now)
        liquid = prop(self.event, 'liquid')
        if not valid(liquid):
            return
        self.liquid = liquid
        patches = self.wave_patches()
        teeth = [self.tooth_state(t) for t in self.fixture_teeth if valid(t)]
        entry = dict(game_seconds=now, wave_seconds=now-float(prop(liquid, 'started_at')),
            wave_duration=self.public_wave_duration(liquid), active=bool(prop(liquid, 'active')),
            phase=str(liquid.get_phase()), patch_count=len(patches),
            tongue_wet_cells=sum(sum(int(v) > 63 for v in prop(p, 'wipe_mask')) for p in patches), teeth=teeth,
            patches=[dict(path=path(p), center_cm=vec(p.get_actor_location()),
                wet_cells=sum(int(v) > 63 for v in prop(p, 'wipe_mask')),
                paint_tile=bool(prop(p, 'paint_tile')), material=str(prop(p, 'liquid_material'))) for p in patches])
        self.wave_history.append(entry)
        self.report['samples'][-1]['coffee_wave'] = entry
        if self.phase == 'wave_brush':
            self.brush_contact_seen |= prop(self.hero, 'care_target') == self.brush_target

    def prepare_wave_brush(self):
        if hasattr(self, 'tooth_camera_restore'):
            camera = prop(self.camera, 'camera_component')
            for key, value in self.tooth_camera_restore.items():
                camera.set_editor_property(key, value)
            del self.tooth_camera_restore
        candidates = []
        live = [j for j in prop(self.event, 'jellies') if valid(j)]
        for tile in self.wave_patches():
            mask = [int(v) for v in prop(tile, 'wipe_mask')]
            size, half = math.isqrt(len(mask)), float(prop(tile, 'liquid_half_size'))
            if not size or size*size != len(mask):
                continue
            for index, value in enumerate(mask):
                if value <= 180:
                    continue
                near = tile.get_actor_transform().transform_location(u.Vector(
                    ((index % size+.5)/size-.5)*half*2, ((index//size+.5)/size-.5)*half*2, 0))
                separation = min((math.hypot(near.x-j.get_actor_location().x, near.y-j.get_actor_location().y)
                                  for j in live), default=10000.)
                candidates.append((separation, tile, near))
        candidates.sort(key=lambda c: c[0], reverse=True)
        for separation, tile, near in candidates[:80]:
            point = self.floor(near)
            if point is None or not tile.is_paint_wet_at(point):
                continue
            for angle in (0., math.pi/2., -math.pi/2., math.pi):
                foot = self.supported_foot(point-u.Vector(math.cos(angle), math.sin(angle), 0)*60.)
                if foot is None:
                    continue
                self.brush_target, self.brush_point = tile, point
                self.place_observer(foot, math.degrees(math.atan2(point.y-foot.y, point.x-foot.x)),
                    'Position-only supported approach to actual wave-deposited paint')
                self.hero.inventory.server_select(u.MCToolSlot.BRUSH)
                wet = [(index % size, index//size) for index, value in enumerate(prop(tile, 'wipe_mask'))
                       if int(value) > 63]
                lo = [min(p[axis] for p in wet) for axis in (0, 1)]
                hi = [max(p[axis] for p in wet) for axis in (0, 1)]
                focus = tile.get_actor_transform().transform_location(u.Vector(
                    (((lo[0]+hi[0]+1)*.5)/size-.5)*half*2,
                    (((lo[1]+hi[1]+1)*.5)/size-.5)*half*2, 0))
                self.film_focus = self.floor(focus) or point
                span = max(hi[axis]-lo[axis]+1 for axis in (0, 1))/size*half*2
                self.film_camera_distance = max(460., min(900., span*1.15))
                self.frame_film_closeup()
                self.report['film_closeup_fixture'] = dict(tile=path(tile), focus_cm=vec(self.film_focus),
                    actual_wet_uv_bounds=[lo, hi], camera_distance_cm=self.film_camera_distance,
                    before_wet_cells=len(wet), primary_held=False,
                    reason='Static three-second review of native wave film before real held brush; same camera after wiping')
                self.stage('wave_film_review')
                self.begin_clip('coffee_film_closeup', 3.3)
                return
        self.require(False, 'No supported real wave-paint cell for the held brush checkpoint')

    def frame_film_closeup(self):
        self.frame_camera(self.film_focus+u.Vector(.44, -.62, .65)*self.film_camera_distance,
                          self.film_focus)
        prop(self.camera, 'camera_component').set_editor_property('FieldOfView', 68.)

    def start_wave_brush(self):
        self.brush_wipe_before_cells = [int(v) for v in prop(self.brush_target, 'wipe_mask')]
        self.frame_camera(self.brush_point+u.Vector(240, -340, 285), self.brush_point+u.Vector(0, 0, 20))
        prop(self.camera, 'camera_component').set_editor_property('FieldOfView', 73.)
        self.primary_input(True)
        self.stage('wave_brush')
        self.begin_clip('coffee_wave_brush', 3.9)

    def frame_wave_tooth(self, tooth):
        center, extent, _ = u.SystemLibrary.get_component_bounds(prop(tooth, 'visual'))
        arena, _, _ = u.SystemLibrary.get_component_bounds(prop(self.tongue, 'surface'))
        dx, dy = arena.x-center.x, arena.y-center.y
        length = max(1., math.hypot(dx, dy))
        inward = u.Vector(dx/length, dy/length, 0.)
        low = [center.x-extent.x, center.y-extent.y, center.z-extent.z]
        high = [center.x+extent.x, center.y+extent.y, center.z+extent.z]
        # The authored tooth can include roots buried beneath the tongue. Clip
        # only at an actual native tongue hit, so the anchor is the visible
        # crown's bounds centre rather than the buried full mesh or +55cm.
        floor = self.floor(center)
        floor_probe = center
        if floor is None:
            floor_probe = center+inward*max(extent.x, extent.y)
            floor = self.floor(floor_probe)
        visible_low = list(low)
        if floor is not None and low[2] < floor.z < high[2]:
            visible_low[2] = floor.z
        fit = fit_tooth_review(visible_low, high, (inward.x, inward.y))
        self.frame_camera(u.Vector(*fit['eye_cm']), u.Vector(*fit['look_target_cm']))
        camera = prop(self.camera, 'camera_component')
        self.tooth_camera_restore = {key: prop(camera, key)
            for key in ('aspect_ratio', 'constrain_aspect_ratio')}
        camera.set_editor_property('FieldOfView', 68.)
        camera.set_editor_property('aspect_ratio', fit['aspect_ratio'])
        camera.set_editor_property('constrain_aspect_ratio', True)
        self.report['tooth_review_camera'] = dict(fit, tooth=path(tooth),
            visual_bounds_center_cm=vec(center), visual_bounds_extent_cm=vec(extent),
            full_visual_bounds_min_cm=low, full_visual_bounds_max_cm=high,
            native_tongue_floor_cm=vec(floor) if floor is not None else None,
            native_tongue_floor_probe_cm=vec(floor_probe),
            clipping='Visible crown above native tongue hit' if visible_low != low else 'Full actual Visual bounds')

    def update_jellies(self, now, elapsed):
        if self.phase == 'wait_branch':
            self.require(elapsed < 25., 'Natural first wave exceeded the focused observation')
            liquid = prop(self.event, 'liquid')
            if valid(liquid) and self.clip != 'coffee_wave_progressive':
                self.begin_clip('coffee_wave_progressive', 25.)
            if enum_name(prop(self.event, 'phase')) != 'JELLY_FIGHT':
                return
            active = [s for s in self.wave_history if s['active']]
            counts = sorted({s['tongue_wet_cells'] for s in active if s['tongue_wet_cells'] > 0})
            self.require(len(counts) >= 2, 'Tongue dirt did not grow while the native wave was active')
            touched = [s for s in active if any(t['visible_care'] for t in s['teeth'])]
            self.require(touched, 'Prepared teeth acquired no actual grime before the wave finished')
            self.require(self.wave_patches() and all(prop(p, 'paint_tile') for p in self.wave_patches()),
                         'Wave residue did not use the conforming paint path')
            self.report['checks'].update(tongue_deposits_before_wave_end=True,
                tongue_mask_grows_during_wave=True, native_tooth_grime_appears_during_wave=True,
                wave_patches_use_conforming_paint=True)
            self.report['wave_checkpoint'] = dict(distinct_positive_tongue_counts=counts,
                first_tongue_sample=next(s for s in active if s['tongue_wet_cells'] > 0), first_tooth_sample=touched[0])
            tooth = next(t for t in self.fixture_teeth if valid(t) and self.tooth_state(t)['visible_care'])
            self.frame_wave_tooth(tooth)
            self.stage('wave_teeth_review')
            self.begin_clip('coffee_wave_teeth', 2.)
        elif self.phase == 'wave_teeth_review' and elapsed >= 1.8:
            self.prepare_wave_brush()
        elif self.phase == 'wave_film_review' and elapsed >= 3.:
            self.start_wave_brush()
        elif self.phase == 'wave_brush' and elapsed >= 3.6:
            self.primary_input(False)
            after = [int(v) for v in prop(self.brush_target, 'wipe_mask')]
            before = self.brush_wipe_before_cells
            removed = sum(max(0, a-b) for a, b in zip(before, after))
            cleared = sum(a > 100 and b <= 63 for a, b in zip(before, after))
            self.require(removed > 16 and cleared > 0 and self.brush_contact_seen,
                         'Normal held brush did not clear actual contacted wave-paint cells')
            self.report['checks']['normal_brush_erases_real_wave_stain'] = True
            self.report['brush_checkpoint'] = dict(tile=path(self.brush_target), removed_bytes=removed,
                wet_cells_cleared=cleared, native_contact_seen=self.brush_contact_seen)
            self.frame_film_closeup()
            self.report['film_closeup_fixture']['after_wet_cells'] = sum(v > 63 for v in after)
            self.stage('wave_after_wipe_review')
            self.begin_clip('coffee_film_after_wipe', 2.3)
        elif self.phase == 'wave_after_wipe_review' and elapsed >= 2.:
            self.stage('wave_flush')
        elif self.phase == 'wave_flush' and elapsed >= .8:
            for name in ('coffee_wave_progressive', 'coffee_wave_teeth', 'coffee_film_closeup',
                         'coffee_wave_brush', 'coffee_film_after_wipe'):
                self.require(len(self.report['clips'].get(name, [])) >= 8, 'Too few genuine frames for '+name)
            for frame in self.report['frames']:
                data = Path(frame['file']).read_bytes()
                self.require(data[:8] == b'\x89PNG\r\n\x1a\n', 'Missing genuine PNG')
                frame.update(sha256=hashlib.sha256(data).hexdigest(),
                    width=int.from_bytes(data[16:20], 'big'), height=int.from_bytes(data[20:24], 'big'))
            self.finish()

    def tick(self, delta):
        if not self.done and self.world is not None and self.phase == 'perk_resolution' and not self.prepared:
            try:
                self.prepare_baseline()
            except Exception:
                self.finish(traceback.format_exc())
                return
        super().tick(delta)


if __name__ == '__main__':
    previous = getattr(builtins, KEY, None)
    if previous and not getattr(previous, 'done', True):
        raise RuntimeError('Preserve existing coffee-wave probe')
    probe = CoffeeWaveStainsSolo()
    setattr(builtins, KEY, probe)
    try:
        probe.start()
    except Exception:
        probe.finish(traceback.format_exc())
        raise
