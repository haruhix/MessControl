"""Capture the owned Kenney phase-front powder in actual solo gameplay.

Reuses the existing guarded real 40-second cold-cycle probe. This adds public
component inventories and Niagara parameter samples; no gameplay clock,
material, VFX override, health or collision is changed by this wrapper.
"""
import builtins
import json
import runpy
from pathlib import Path

import unreal as u

OUTPUT = Path(globals().get('MC_KENNEY_POWDER_OUTPUT',
    Path(u.Paths.project_saved_dir()) / 'Checks/KenneySnowPowderSolo'))
support = runpy.run_path(str(Path(u.Paths.project_dir()) / 'Tools/Unreal/check_ice_phase_return_solo.py'),
    run_name='kenney_snow_powder_support', init_globals={'MC_ICE_RETURN_OUTPUT': OUTPUT,
                                                      'MC_ICE_RETURN_CAPTURE_HZ': 6})
Base, valid = support['IcePhaseReturnSolo'], support['valid']


class KenneyPowderSolo(Base):
    def __init__(self):
        super().__init__()
        self.report['powder_component_samples'] = []
        self.report['scope'] += '; current Kenney White Puff phase-front powder'
        material = u.load_asset('/Game/Gameplay/Cold/VFX/M_IcePhasePowder')
        texture = u.load_asset('/Game/Gameplay/Cold/VFX/T_KenneyWhitePuff12')
        self.require(isinstance(material, u.Material) and isinstance(texture, u.Texture2D),
                     'Saved Kenney material and texture are required')
        self.require(u.MaterialEditingLibrary.has_material_usage(material, u.MaterialUsage.MATUSAGE_NIAGARA_SPRITES),
                     'Saved powder material lacks the NiagaraSprites shader permutation')
        self.report['checks']['saved_kenney_material_has_niagara_sprite_usage'] = True
        self.report['powder_material'] = material.get_path_name()
        self.report['powder_texture'] = texture.get_path_name()
        self.require(isinstance(u.load_asset('/Game/Gameplay/Cold/VFX/NS_IcePhasePowder'), u.NiagaraSystem),
            'Saved three-dimensional powder spawn system is required')
        for name in ('MI_IcePowderLow', 'MI_IcePowderBody', 'MI_IcePowderHigh'):
            self.require(isinstance(u.load_asset('/Game/Gameplay/Cold/VFX/'+name), u.MaterialInstanceConstant),
                'Saved powder layer material is required: '+name)

    def sample(self, now):
        super().sample(now)
        systems = self.actors(u.MCIceEventVFX)
        components = []
        for system in systems:
            for component in system.get_components_by_class(u.NiagaraComponent):
                name = component.get_name()
                if not name.startswith('GlobalWinterWisps'):
                    continue
                value, available = component.get_variable_float('User.SpeedScale')
                size, size_available = component.get_variable_vec2('User.ParticleSize')
                rate, rate_available = component.get_variable_float('User.SpawnRate')
                position = component.get_world_location()
                asset = component.get_asset()
                components.append(dict(name=name, path=component.get_path_name(), active=component.is_active(),
                    speed_scale=float(value) if available else None,
                    spawn_rate=float(rate) if rate_available else None,
                    position_cm=[float(position.x), float(position.y), float(position.z)],
                    system=asset.get_path_name() if valid(asset) else None,
                    size_cm=[float(size.x), float(size.y)] if size_available else None))
        observation = dict(game_seconds=now, phase_seconds=None if self.native_latch_at is None else now-self.native_latch_at,
            return_age=None if self.clock_end is None else now-self.clock_end,
            clip_window=self.capture_window(now), components=components)
        self.report['powder_component_samples'].append(observation)
        (self.root_output / 'LatestPowderComponents.json').write_text(json.dumps(observation, indent=2))

    def finish(self, error=None):
        if self.done:
            return
        if error is None:
            entries = self.report['powder_component_samples']
            for label, accept in [('outgoing', lambda s: s['phase_seconds'] is not None and .6<s['phase_seconds']<2.7)]:
                rows = [s for s in entries if accept(s)]
                front = [c for s in rows for c in s['components']
                         if c['active'] and c['name'].startswith('GlobalWinterWisps')
                         and int(c['name'].replace('GlobalWinterWisps', ''))<36]
                if not front or not all(c['system'].endswith('/NS_IcePhasePowder.NS_IcePhasePowder') for c in front):
                    error = 'Current native Kenney powder components not observed during '+label
                    break
                self.report['checks']['current_powder_components_observed_during_'+label] = True
                layered = []
                for row in rows:
                    groups = {index: [c for c in row['components'] if c['active']
                        and int(c['name'].replace('GlobalWinterWisps', ''))//12 == index] for index in range(3)}
                    if all(groups.values()):
                        means = [sum(c['position_cm'][2] for c in group)/len(group) for group in groups.values()]
                        layered.append(max(means)-min(means))
                if not layered or max(layered)<80.:
                    error = 'Three vertically separated active powder layers were not observed during '+label
                    break
                self.report['checks']['overlapping_powder_layers_separated_in_height_during_'+label] = True
                self.report.setdefault('powder_layer_height_spread_cm', {})[label] = max(layered)
            returning = [s for s in entries if s['return_age'] is not None and .9<s['return_age']<3.2]
            if error is None:
                if len(returning)<5 or any(c['active'] for s in returning for c in s['components']
                        if int(c['name'].replace('GlobalWinterWisps', ''))<36):
                    error = 'The returning frost front retained active powder clouds'
                else:
                    self.report['checks']['returning_frost_front_has_no_active_powder_clouds'] = True
        super().finish(error)


def main():
    assert Path(u.Paths.project_dir()).resolve() == Path('E:/DEVGAME/MessControl').resolve()
    old = getattr(builtins, 'mc_ice_phase_return_solo', None)
    if old is not None and not getattr(old, 'done', True):
        raise RuntimeError('An owned cold-cycle capture is already running')
    probe = KenneyPowderSolo()
    setattr(builtins, 'mc_ice_phase_return_solo', probe)
    try:
        probe.start()
    except Exception:
        probe.finish('Kenney powder PIE bootstrap failed')
        raise


if __name__ == '__main__':
    main()
