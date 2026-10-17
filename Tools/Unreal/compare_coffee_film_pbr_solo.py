"""One natural wave, three live PBR-only MID comparisons, then real brushing.

Root-run in idle L_Mouth after coffee_stain_film_v3:5 authoring. Inherits every
native coffee-wave gate and fixture from the focused probe without changing
masks, dirt, event clocks, AI or damage. Only the chosen wave patch's transient
MID roughness/normal parameters and the owned review camera vary. Each variant
gets three static seconds and a 1.2-second +/-6-degree orbit. All parameters are
restored before the usual close-up/held-brush/after-wipe checkpoints or on abort.
No package saves/builds/network clients. Cancel:
builtins.mc_coffee_pbr_compare_solo.finish('Stopped by operator').
"""
import builtins
import hashlib
import math
import runpy
import traceback
from pathlib import Path
import unreal as u

ROOT = Path(u.Paths.project_dir()).resolve()
OUTPUT = Path(globals().get('MC_COFFEE_PBR_OUTPUT', ROOT/'Saved/CoffeeFilmPBR/Capture')).resolve()
support = runpy.run_path(str(ROOT/'Tools/Unreal/probe_coffee_wave_stains_solo.py'),
    run_name='coffee_pbr_support', init_globals={'MC_COFFEE_WAVE_OUTPUT': OUTPUT})
Parent, prop, valid, path, vec = (support[key] for key in
    ('CoffeeWaveStainsSolo', 'prop', 'valid', 'path', 'vec'))
KEY = 'mc_coffee_pbr_compare_solo'
VARIANTS = tuple(globals().get('MC_COFFEE_COMPARE_VARIANTS', (
    ('coffee_pbr_rough012', {'CoffeeRoughness': .12, 'CoffeeMicroNormalStrength': .0025}),
    ('coffee_pbr_rough008', {'CoffeeRoughness': .08, 'CoffeeMicroNormalStrength': .0025}),
    ('coffee_pbr_rough005', {'CoffeeRoughness': .05, 'CoffeeMicroNormalStrength': .0025}),
)))
PARAMETERS = tuple(sorted({name for _label, values in VARIANTS for name in values}))


class CoffeePBRCompareSolo(Parent):
    def __init__(self):
        self.compare_mid = None
        self.compare_restore = {}
        self.compare_index = None
        self.compare_complete = False
        self.compare_started_at = None
        self.compare_postwipe = False
        self.post_comparison_started = False
        self.compare_mask_epoch_hash = None
        super().__init__()
        self.report['purpose'] = globals().get('MC_COFFEE_COMPARE_PURPOSE',
            'Three transient plain-PBR coffee film candidates in one real wave/brush PIE')
        self.report['pbr_comparison'] = dict(variants=[], fake_glints='Removed from rev5 shader; emissive is exactly zero',
            static_seconds=3., orbit_seconds=1.2, orbit_degrees=6., restored=False,
            anti_aliasing_cvar=int(u.SystemLibrary.get_console_variable_int_value('r.AntiAliasingMethod')))
        self.report['limitations'].append('Only roughness differs between live MID candidates; the same native stain, world lighting, camera anchor, exact mask and low fine-normal strength are retained. Native encounter movement keeps ticking.')
        self.report['source_sha256']['Tools/Unreal/compare_coffee_film_pbr_solo.py'] = \
            hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
        for name, values in VARIANTS:
            self.report['clip_labels'][name] = globals().get('MC_COFFEE_COMPARE_LABELS', {}).get(name,
                'Кофейная плёнка: PBR roughness '+str(values['CoffeeRoughness']))

    def mid_values(self):
        return {name: float(self.compare_mid.call_method('K2_GetScalarParameterValue', args=(u.Name(name),)))
                for name in PARAMETERS}

    def start_variant(self, index, now):
        name, values = VARIANTS[index]
        if self.compare_postwipe:
            name += '_after_wipe'
            self.report['clip_labels'][name] = self.report['clip_labels'][VARIANTS[index][0]]+' — после уборки'
        self.compare_index, self.compare_started_at = index, now
        for key, value in values.items():
            self.compare_mid.set_scalar_parameter_value(key, value)
        actual = self.mid_values()
        self.require(all(abs(actual[key]-value) < 1.e-5 for key, value in values.items()),
                     'PBR candidate MID values did not apply')
        self.frame_film_closeup()
        self.begin_clip(name, 4.5)
        mask = bytes(prop(self.brush_target, 'wipe_mask'))
        mask_sha = hashlib.sha256(mask).hexdigest()
        if index == 0:
            self.compare_mask_epoch_hash = mask_sha
        if globals().get('MC_COFFEE_REQUIRE_STABLE_MASK', False):
            self.require(mask_sha == self.compare_mask_epoch_hash, 'Native mask changed between edge candidates')
        self.report['pbr_comparison']['variants'].append(dict(clip=name, requested=values, actual=actual,
            started_at=now, tile=path(self.brush_target), mid=path(self.compare_mid),
            checkpoint='After real brush' if self.compare_postwipe else 'Before real brush',
            native_mask_sha256_at_start=mask_sha, native_zero_cells_at_start=sum(value == 0 for value in mask),
            native_mask_sum_at_start=sum(mask)))
        self.write()

    def restore_comparison(self):
        if valid(self.compare_mid) and self.compare_restore:
            for key, value in self.compare_restore.items():
                self.compare_mid.set_scalar_parameter_value(key, value)
            actual = self.mid_values()
            self.require(all(abs(actual[key]-value) < 1.e-5 for key, value in self.compare_restore.items()),
                         'PBR comparison restore failed')
            self.report['pbr_comparison'].update(restored=True, restored_values=actual)
            self.compare_restore = {}

    def screenshot(self, name, sequence=False):
        super().screenshot(name, sequence)
        if self.compare_index is not None and not self.compare_complete:
            frame = self.report['frames' if sequence else 'screenshots'][-1]
            frame['pbr_candidate'] = dict(name=VARIANTS[self.compare_index][0]
                + ('_after_wipe' if self.compare_postwipe else ''),
                checkpoint='After real brush' if self.compare_postwipe else 'Before real brush',
                actual_mid_parameters=self.mid_values(), fake_emissive=False)

    def prepare_wave_brush(self):
        super().prepare_wave_brush()
        if 'CoffeeEdgeSoftness' in PARAMETERS:
            self.require(int(u.SystemLibrary.get_console_variable_int_value('r.AntiAliasingMethod')) == 2,
                         'Native temporal coffee-edge review requires active TAA; settings were not changed')
        self.compare_mid = prop(self.brush_target, 'liquid').get_material(0)
        self.require(isinstance(self.compare_mid, u.MaterialInstanceDynamic),
                     'Expected the real native liquid MID on the selected wave patch')
        self.compare_restore = self.mid_values()
        self.require(.035 <= self.compare_restore['CoffeeRoughness'] <= .20,
                     'Missing rev5 CoffeeRoughness parameter; do not compare a different shader')
        # Switch clips in this same update, before the next capture, so the
        # normal before-brush close-up has no artificial gap across candidates.
        self.start_variant(0, float(u.GameplayStatics.get_time_seconds(self.world)))

    def update_jellies(self, now, elapsed):
        compare_phase = 'wave_after_wipe_review' if self.compare_postwipe else 'wave_film_review'
        if self.phase != compare_phase or self.compare_complete:
            result = super().update_jellies(now, elapsed)
            if globals().get('MC_COFFEE_COMPARE_AFTER_WIPE', False) \
                    and self.phase == 'wave_after_wipe_review' and not self.post_comparison_started:
                self.post_comparison_started = self.compare_postwipe = True
                self.compare_complete = False
                self.compare_restore = self.mid_values()
                self.report['pbr_comparison']['restored'] = False
                self.start_variant(0, now)
            return result
        self.require(not self.primary, 'Preserve the static comparison without held brush input')
        self.require(self.compare_index is not None, 'PBR comparison was not prepared')
        age = now-self.compare_started_at
        if age < 3.:
            return  # Same camera and actual native wet film for three seconds.
        if age < 4.2:
            t = (age-3.)/1.2
            angle = math.radians(6.)*math.sin(2.*math.pi*t)
            c, s = math.cos(angle), math.sin(angle)
            offset = u.Vector(.44, -.62, .65)*self.film_camera_distance
            self.frame_camera(self.film_focus+u.Vector(offset.x*c-offset.y*s,
                offset.x*s+offset.y*c, offset.z), self.film_focus)
            return
        item = self.report['pbr_comparison']['variants'][-1]
        item.update(ended_at=now, native_mask_sum_at_end=sum(prop(self.brush_target, 'wipe_mask')),
            native_mask_sha256_at_end=hashlib.sha256(bytes(prop(self.brush_target, 'wipe_mask'))).hexdigest())
        if globals().get('MC_COFFEE_REQUIRE_STABLE_MASK', False):
            self.require(item['native_mask_sha256_at_end'] == item['native_mask_sha256_at_start'],
                         'Native mask changed during an edge candidate')
        self.require(len(self.report['clips'].get(item['clip'], [])) >= 24,
                     'Not enough genuine candidate frames')
        if self.compare_index+1 < len(VARIANTS):
            self.start_variant(self.compare_index+1, now)
        else:
            self.restore_comparison()
            self.compare_complete = True
            self.frame_film_closeup()
            self.stage('wave_after_wipe_review' if self.compare_postwipe else 'wave_film_review')
            self.begin_clip('coffee_film_after_wipe' if self.compare_postwipe else 'coffee_film_closeup',
                            2.3 if self.compare_postwipe else 3.3)

    def finish(self, error=None):
        try:
            self.restore_comparison()
        except Exception:
            error = error or traceback.format_exc()
        super().finish(error)


if __name__ == '__main__':
    previous = getattr(builtins, KEY, None)
    if previous and not getattr(previous, 'done', True):
        raise RuntimeError('Preserve existing PBR comparison')
    material = u.EditorAssetLibrary.load_asset('/Game/Cataclysm/Jelly/M_CataclysmPaintTrail')
    if u.EditorAssetLibrary.get_metadata_tag(material, 'MCCoffeeSurfaceRevision') != 'coffee_stain_film_v3:5':
        raise RuntimeError('Author the reviewed rev5 PBR-only film before this comparison')
    if 'CoffeeEdgeReconstruction' in PARAMETERS \
            and u.EditorAssetLibrary.get_metadata_tag(material, 'MCCoffeeEdgeRevision') != 'coffee_edge_reconstruction_v1:1':
        raise RuntimeError('Author the reviewed scoped mask reconstruction before comparing its edge values')
    if 'CoffeeEdgeSoftness' in PARAMETERS \
            and u.EditorAssetLibrary.get_metadata_tag(material, 'MCCoffeeEdgeRevision') != 'coffee_edge_reconstruction_v1:2':
        raise RuntimeError('Author the reviewed native temporal edge before comparing its values')
    probe = CoffeePBRCompareSolo()
    setattr(builtins, KEY, probe)
    try:
        probe.start()
    except Exception:
        probe.finish(traceback.format_exc())
        raise
