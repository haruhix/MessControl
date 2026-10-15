"""Observe CoffeeSlime v2 through the existing F3 / ordinary tool solo proof.

Run in idle L_Mouth with remote_python.py. This only adds read-only material
evidence and a 3.5-second real hero-camera clip inside the existing 12-second
untouched jelly observation. It does not save packages, assign animation/HP,
steer jellies, change shader parameters, or start a build/network test.
Cancellation: builtins.mc_coffee_slime_v2_solo.finish('Stopped by operator').
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
OUTPUT = ROOT / 'Saved/CoffeeSlimeV2/Capture'
support = runpy.run_path(str(ROOT/'Tools/Unreal/probe_coffee_slime_solo.py'),
    run_name='coffee_slime_v2_support', init_globals={
        'MC_COFFEE_AUTOSTART': False, 'MC_COFFEE_OUTPUT': OUTPUT})
Parent, prop, valid, path, vec = (support[n] for n in ('CoffeeSlimeSolo', 'prop', 'valid', 'path', 'vec'))
KEY = 'mc_coffee_slime_v2_solo'
MANIFEST = ROOT/'ArtSource/CoffeeSlime/v2/manifest.json'


def rgba(value):
    return [float(getattr(value, component)) for component in ('r', 'g', 'b', 'a')]


def warm(rgb):
    return rgb[0] > rgb[1] * 1.5 and rgb[1] > rgb[2] * 1.5 and rgb[2] > 0.


class CoffeeSlimeV2Solo(Parent):
    def __init__(self):
        super().__init__()
        self.manifest = json.loads(MANIFEST.read_text(encoding='utf-8'))
        self.expected_materials = ['/Game/Cataclysm/Jelly/Coffee/'+name+'.'+name
                                   for name in self.manifest['material_slots']]
        self.hero_camera_active = False
        self.hero_camera_started = False
        self.hero_frame_at = self.hero_follow_at = 0.
        self.body_seen = self.trail_seen = self.splatter_seen = False
        self.report['purpose'] = 'CoffeeSlime v2 body, coffee trail/rupture shaders, retained native paint/tool gameplay'
        self.report['source_sha256']['Tools/Unreal/probe_coffee_slime_v2_solo.py'] = hashlib.sha256(
            (ROOT/'Tools/Unreal/probe_coffee_slime_v2_solo.py').read_bytes()).hexdigest()
        self.report['v2_manifest_sha256'] = hashlib.sha256(MANIFEST.read_bytes()).hexdigest()
        self.report['clip_labels']['jelly_paint_gameplay'] = 'Настоящая игровая камера: кофейное желе и его след'
        self.report['shader_observations'] = []
        self.report['limitations'].append(
            'Inside the existing natural 12-second wander interval, seconds 3–6.5 use the actual possessed hero Camera and view target. '
            'A disclosed position/control-facing-only hero fixture follows the moving jelly; no camera boom/FOV/native camera tuning is assigned. '
            'Material paths and effective MID parameters are technical evidence; ROOT must inspect rendered colors/highlights/trail readability.')

    def jelly_metrics(self, jelly):
        result = super().jelly_metrics(jelly)
        mesh = prop(jelly, 'jelly_mesh')
        materials = [path(mesh.get_material(index)) for index in range(mesh.get_num_materials())]
        self.require(materials == self.expected_materials and len(materials) == 3,
                     'Native body uses stale/non-v2 materials: '+str(materials))
        result['coffee']['materials'] = materials
        self.body_seen = True
        return result

    def shader_evidence(self, now):
        entry = dict(time=now, trail=[], splatter=[])
        for tile in self.actors(u.MCMouthSurface):
            if tile.get_owner() != self.event or not bool(prop(tile, 'paint_tile')):
                continue
            mid = prop(tile, 'liquid').get_material(0)
            if not valid(mid):
                continue
            color = rgba(mid.call_method('K2_GetVectorParameterValue', args=(u.Name('LiquidColor'),)))
            self.require(isinstance(mid, u.MaterialInstanceDynamic) and warm(color),
                         'Actual cleanable paint MID is missing its warm LiquidColor')
            entry['trail'].append(dict(tile=path(tile), material=path(mid), liquid_color=color))
            self.trail_seen = True
        for jelly in prop(self.event, 'jellies'):
            if not valid(jelly):
                continue
            # Read the material actually assigned to a drop and ground burst,
            # rather than trusting the actor's configured soft reference.
            drops = list(prop(jelly, 'droplets'))
            for component in [prop(jelly, 'ground_burst')] + drops[:1]:
                mid = component.get_material(0)
                if not valid(mid):
                    continue
                self.require(isinstance(mid, u.MaterialInstanceDynamic), 'Rupture component has no actual MID')
                theme = float(mid.call_method('K2_GetScalarParameterValue', args=(u.Name('CoffeeTheme'),)))
                color = rgba(mid.call_method('K2_GetVectorParameterValue', args=(u.Name('CoffeeColor'),)))
                self.require(abs(theme-1.) < .001 and warm(color), 'Rupture still resolves the legacy green gel profile')
                entry['splatter'].append(dict(jelly=path(jelly), component=path(component), material=path(mid),
                    visible=component.is_visible(), coffee_theme=theme, coffee_color=color))
                self.splatter_seen = True
        self.report['shader_observations'].append(entry)

    def sample(self, now):
        super().sample(now)
        if valid(self.event):
            self.shader_evidence(now)

    def keep_camera(self):
        if self.hero_camera_active:
            self.pc.set_view_target_with_blend(self.hero, 0.)
        else:
            super().keep_camera()

    def camera_for_jelly(self, jelly, wider=False):
        if not self.hero_camera_active:
            super().camera_for_jelly(jelly, wider)

    def camera_evidence(self):
        if not self.hero_camera_active:
            return super().camera_evidence()
        manager = prop(self.pc, 'player_camera_manager')
        component = prop(self.hero, 'camera')
        actual, expected = manager.get_camera_location(), component.get_world_location()
        rotation, target_rotation = manager.get_camera_rotation(), component.get_world_rotation()
        return dict(kind='actual_possessed_hero_camera', view_target=path(self.pc.get_view_target()),
            expected_target=path(self.hero), possessed_pawn=path(self.pc.get_controlled_pawn()),
            camera_component=path(component), actual_cm=vec(actual), expected_cm=vec(expected),
            location_error_cm=math.dist(vec(actual), vec(expected)),
            rotation_error_degrees=max(abs((a-b+180.)%360.-180.) for a, b in
                ((rotation.pitch,target_rotation.pitch),(rotation.yaw,target_rotation.yaw),(rotation.roll,target_rotation.roll))),
            fov=float(manager.get_fov_angle()))

    def update_jellies(self, now, elapsed):
        if self.phase == 'paint_trail':
            active = 3. <= elapsed < 6.5
            if active:
                self.hero_camera_active = self.hero_camera_started = True
                if now >= self.hero_follow_at:
                    self.hero_follow_at = now+.2
                    center = self.tracked_jelly.get_actor_location()
                    foot = self.supported_foot(center+u.Vector(-340., -140., 0.))
                    self.require(foot is not None, 'No supported hero-camera position beside native wandering jelly')
                    yaw = math.degrees(math.atan2(center.y-foot.y, center.x-foot.x))
                    self.place_observer(foot, yaw, 'Position-only hero-camera observation; native jelly and game camera remain active')
                    # Ordinary look input angle; keep native camera boom/FOV.
                    self.pc.set_control_rotation(u.Rotator(pitch=-18., yaw=yaw))
                self.keep_camera()
                evidence = self.camera_evidence()
                if self.capture_hz and now >= self.hero_frame_at and self.pc.get_view_target() == self.hero \
                        and evidence['location_error_cm'] < 12. and evidence['rotation_error_degrees'] < 5.:
                    self.hero_frame_at = now+1./self.capture_hz
                    old_clip = self.clip
                    self.clip = 'jelly_paint_gameplay'
                    self.report['clips'].setdefault(self.clip, [])
                    self.screenshot('%05d.png' % len(self.report['frames']), True)
                    self.clip = old_clip
            elif self.hero_camera_active:
                self.hero_camera_active = False
                self.keep_camera()
        super().update_jellies(now, elapsed)

    def finish(self, error=None):
        if not error:
            frames = self.report['clips'].get('jelly_paint_gameplay', [])
            if not (self.body_seen and self.trail_seen and self.splatter_seen):
                error = 'Missing actual v2 body / warm trail MID / coffee-profile drop MID observation'
            elif self.capture_hz and (len(frames) < 8 or not all(
                    f['camera_evidence']['view_target'] == f['camera_evidence']['possessed_pawn']
                    and f['camera_evidence']['kind'] == 'actual_possessed_hero_camera' for f in frames)):
                error = 'Missing verified actual hero-camera frames'
            else:
                self.report['checks'].update(v2_three_body_materials_observed=True,
                    actual_paint_mid_has_warm_coffee_color=True, actual_drop_mid_uses_coffee_theme=True,
                    actual_hero_gameplay_camera_observed=self.hero_camera_started)
        super().finish(error)


if __name__ == '__main__':
    previous = getattr(builtins, KEY, None)
    if previous and not getattr(previous, 'done', True):
        raise RuntimeError('Preserve the active owned CoffeeSlime v2 probe')
    probe = CoffeeSlimeV2Solo()
    setattr(builtins, KEY, probe)
    try:
        probe.start()
    except Exception:
        probe.finish(traceback.format_exc())
        raise
