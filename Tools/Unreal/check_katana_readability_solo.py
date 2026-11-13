"""Review the enlarged katana and its VFX through owned standalone PIE.

Runs the real F3 grant and primary input on L_Mouth. Retains the existing
contact/cap checks, adds expanded-distance fixtures and interrupts the charged
attack by selecting the brush. Output and recorder paths must be on E:.
"""
import builtins
import datetime
import math
import runpy
import traceback
from pathlib import Path

import unreal as u


ROOT = Path(u.Paths.project_dir()).resolve()
RUN = datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%S%fZ')
OUTPUT = Path(globals().get('MC_KATANA_READABILITY_OUTPUT',
    'E:/CodexWork/MessControl/Validation/KatanaReadability/' + RUN)).resolve()
BASE = (3.75, 0., 11.)
TIP = (-14.5, 0., 197.56)
LEGACY_TIP = (-10., 0., 95.8)
SERIES_FOOD = (('food', 245., 25., 70., 70.), ('food', 235., -40., 115., 60.))
SERIES_ENEMY = (('enemy', 240., -25., 65., 40.),)
CASES = (
    ('food_single', False, None, (('food', 245., 25., 38., 80.),)),
    ('enemy_single', False, None, (('enemy', 245., 25., 80., 32.),)),
    ('outside_sweep', False, None, (('food', 390., -35., 90., 36.), ('enemy', 390., 35., 90., 25.))),
    ('food_series', True, None, SERIES_FOOD),
    ('enemy_series', True, None, SERIES_ENEMY),
    ('food_series_front_review', True, 'front', SERIES_FOOD),
    ('enemy_series_side_review', True, 'side', SERIES_ENEMY),
)
namespace = runpy.run_path(str(ROOT / 'Tools/Unreal/check_katana_solo.py'), init_globals={
    'MC_KATANA_SOLO_OUTPUT': OUTPUT,
    'MC_KATANA_SOLO_TRACE_VERBOSITY': 2,
    'MC_KATANA_SOLO_CAPTURE_HZ': globals().get('MC_KATANA_READABILITY_CAPTURE_HZ', 0.),
    'MC_KATANA_SOLO_START_WINDOW_RECORDING': globals().get('MC_KATANA_READABILITY_START_RECORDING'),
})
KatanaSolo, prop, xyz = (namespace[key] for key in ('KatanaSolo', 'prop', 'xyz'))
# Existing methods share one module dictionary. Changing its case table keeps
# timing, contact parsing and all of the established assertions in one place.
KatanaSolo.tick.__globals__['CASES'] = CASES


def parameter_result(value, default):
    if isinstance(value,(tuple,list)):
        return value[0], bool(value[1])
    if value is None:
        return default, False
    return value, True


class KatanaReadabilitySolo(KatanaSolo):
    def __init__(self):
        super().__init__()
        self.report.update(scope=__doc__, blade_dimensions={}, niagara_samples=[], interruption={})
        self.interrupt_released_at = None
        self.interrupt_selected_at = None

    def niagara(self):
        prefix = self.world.get_path_name()
        records = []
        for component in u.ObjectIterator(u.NiagaraComponent):
            if not component.get_path_name().startswith(prefix):
                continue
            asset = component.get_asset()
            if not asset or 'Katana' not in asset.get_path_name():
                continue
            bounds = u.SystemLibrary.get_component_bounds(component)
            allowed = parameter_result(component.get_variable_bool('User.Native.TrailRenderAllowed'),False)
            serial = parameter_result(component.get_variable_int('User.Native.BladeBreakSerial'),0)
            records.append(dict(component=component.get_path_name(), system=asset.get_path_name(),
                active=bool(component.is_active()), visible=bool(component.is_visible()),
                location=xyz(component.get_world_location()), bounds_center=xyz(bounds[0]),
                bounds_extent=xyz(bounds[1]), bounds_radius_cm=float(bounds[2]),
                render_allowed=bool(allowed[0]), render_allowed_valid=bool(allowed[1]),
                break_serial=int(serial[0]), break_serial_valid=bool(serial[1])))
        return records

    def sample(self):
        row = super().sample()
        tool = self.tool()
        transform = tool.get_world_transform()
        base = transform.transform_location(u.Vector(*BASE))
        tip = transform.transform_location(u.Vector(*TIP))
        legacy_tip = transform.transform_location(u.Vector(*LEGACY_TIP))
        row['visible_blade'] = dict(base=xyz(base), tip=xyz(tip), length_cm=(tip-base).length(),
            legacy_tip=xyz(legacy_tip), frame=int(u.SystemLibrary.get_frame_count()) % 1000,
            captured_utc=datetime.datetime.now(datetime.timezone.utc).isoformat())
        projected = []
        for point in (base, tip):
            screen = self.pc.project_world_location_to_screen(point, False)
            if isinstance(screen, u.Vector2D):
                projected.append([float(screen.x), float(screen.y)])
        if len(projected) == 2:
            row['visible_blade']['screen_endpoints'] = projected
            row['visible_blade']['screen_length_px'] = math.dist(*projected)
        systems = self.niagara()
        row['katana_niagara'] = systems
        trail = self.hero.get_component_by_class(u.MCWeaponTrailComponent)
        row['trail'] = dict(emitting=bool(prop(trail,'emitting')), active_samples=int(prop(trail,'active_samples')),
                            rendered_vertices=int(prop(trail,'rendered_vertices')))
        release = next((request['time'] for request in reversed(self.report['requests'])
                        if request['phase']==self.phase and not request['held']),None)
        if release is not None and row['attack']==8:
            clip_time = self.now()-release+11./30.
            row['charged_stroke'] = next((index for index,(start,end) in enumerate(
                ((15./30.,17./30.),(19./30.,21./30.),(24./30.,26./30.)))
                if start-.03<=clip_time<=end+.03),None)
        if systems:
            self.report['niagara_samples'].append(dict(phase=self.phase,time=self.now(),systems=systems,
                trail=row['trail'],stroke=row.get('charged_stroke')))
        if self.phase == 'equip' and not self.report['blade_dimensions']:
            bounds = prop(tool, 'static_mesh').get_bounds()
            self.report['blade_dimensions'] = dict(mesh_extent_cm=xyz(bounds.box_extent),
                component_scale=xyz(tool.get_world_scale()), blade_base_local_cm=list(BASE),
                blade_tip_local_cm=list(TIP), edge_length_world_cm=(tip-base).length(),
                legacy_edge_length_world_cm=(legacy_tip-transform.transform_location(u.Vector(3.75,0,11))).length())
            self.check_case('Visible blade is at least twice its previous length',
                (tip-base).length() > 2.0*self.report['blade_dimensions']['legacy_edge_length_world_cm'])
        # Observe each rendered game frame so short charged windows cannot be
        # skipped merely because a 30 Hz sampling threshold fell between them.
        self.sample_at = self.now()
        return row

    def frame_review(self, side=False):
        center = self.hero.get_actor_location() + self.hero.get_actor_forward_vector()*95 + u.Vector(0,0,45)
        if side:
            location = center + self.hero.get_actor_forward_vector()*70 - self.hero.get_actor_right_vector()*490
        else:
            location = center + self.hero.get_actor_forward_vector()*490 - self.hero.get_actor_right_vector()*130
        self.frame_camera(location+u.Vector(0,0,105), center)

    def verify_case(self):
        super().verify_case()
        contacts = [row for row in self.report['contacts'] if row['case'] == self.phase]
        if self.phase.startswith('enemy') and contacts:
            origin = self.hero.get_actor_location()
            forward = self.hero.get_actor_forward_vector()
            for contact in contacts:
                point = u.Vector(*contact['point'])
                contact['forward_distance_cm'] = float(u.MathLibrary.dot_vector_vector(point-origin,forward))
            self.check_case(self.phase+' hit the enemy beyond two metres',
                all(contact['forward_distance_cm']>200. for contact in contacts))
        if self.phase.startswith('enemy_series'):
            emitted = [row for row in self.report['niagara_samples'] if row['phase']==self.phase
                and row['trail']['emitting'] and any(system['active'] and system['render_allowed']
                    and system['render_allowed_valid'] and system['bounds_radius_cm']>1.
                    for system in row['systems'])]
            self.check_case(self.phase+' emitted a trail in all three cut windows',
                {row['stroke'] for row in emitted}>={0,1,2})
            self.check_case(self.phase+' supplied two moving blade samples in each cut window',
                {row['stroke'] for row in emitted if row['trail']['active_samples']>=2}>={0,1,2})
            serials = {system['break_serial'] for row in emitted for system in row['systems']
                       if system['break_serial_valid'] and system['render_allowed']}
            self.report.setdefault('charged_trail_break_serials',{})[self.phase] = sorted(serials)
            self.check_case(self.phase+' separated all three trail segments',len(serials)>=3)

    def start_next_case(self):
        if self.case_index+1 < len(CASES):
            super().start_next_case()
            return
        self.clear_targets()
        self.move_to(self.origin)
        self.pc.set_view_target_with_blend(self.hero,0.)
        self.case_index = len(CASES)
        self.enter('interrupt_charge')
        self.shoot(True)

    def finish(self, error=None):
        if self.world and not self.done:
            active = [row for row in self.report['niagara_samples'] if any(system['active'] for system in row['systems'])]
            self.check_case('Katana Niagara played through the real primary action', bool(active))
        super().finish(error)

    def tick(self, delta):
        super().tick(delta)
        if self.done or self.world is None:
            return
        try:
            age = self.now()-self.phase_at
            if self.phase == 'interrupt_charge':
                if self.primary and age > .65:
                    self.shoot(False)
                    self.interrupt_released_at = self.now()
                elif self.interrupt_released_at is not None and self.now()-self.interrupt_released_at > .19:
                    self.check_case('Charged attack began before interruption', self.attack_id()==8)
                    self.inventory.server_select(u.MCToolSlot.BRUSH)
                    self.interrupt_selected_at = self.now()
                    self.report['interruption']['brush_selected_at'] = self.interrupt_selected_at
                    self.enter('interrupt_recovery')
            elif self.phase == 'interrupt_recovery' and age > .6:
                self.check_case('Brush interrupted the charged attack and released its pose',
                    not self.hero.is_katana_equipped() and self.attack_id()==0)
                self.report['interruption']['remaining_vfx'] = self.niagara()
                self.check_case('Katana VFX finished after brush interruption',
                    not any(row['active'] for row in self.report['interruption']['remaining_vfx']))
                self.enter('recording_end')
            elif self.phase == 'recording_end' and age > .12:
                self.finish('\n'.join(self.case_failures) or None)
        except Exception:
            self.finish(traceback.format_exc())


if __name__ == '__main__':
    previous = getattr(builtins, 'mc_katana_readability_solo', None)
    assert previous is None or previous.done, 'Readability probe already owns PIE'
    probe = KatanaReadabilitySolo()
    builtins.mc_katana_readability_solo = probe
    try:
        probe.start()
    except Exception:
        probe.finish(traceback.format_exc())
        raise
