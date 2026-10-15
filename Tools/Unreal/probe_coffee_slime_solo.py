"""Observe the authored coffee skin through the real F3 jelly/paint solo path.

Adds read-only skeletal/clip evidence to the existing normal tool-input check.
Does not assign animations, health, jelly movement or package properties.
"""
import builtins
import hashlib
import json
import runpy
import traceback
from pathlib import Path
import unreal as u

ROOT = Path(u.Paths.project_dir()).resolve()
OUTPUT = Path(globals().get('MC_COFFEE_OUTPUT', ROOT/'Saved/CoffeeSlime_20261015/Capture')).resolve()
support = runpy.run_path(str(ROOT/'Tools/Unreal/probe_jelly_paint_solo.py'),
    run_name='coffee_slime_support', init_globals={
        'MC_PAINT_OUTPUT': OUTPUT, 'MC_PAINT_CAPTURE_HZ': 12})
Parent, prop, valid, path, vec = (support[n] for n in ('JellyPaintSolo','prop','valid','path','vec'))
KEY = 'mc_coffee_slime_solo'

class CoffeeSlimeSolo(Parent):
    def __init__(self):
        super().__init__()
        self.seen_coffee_clips = set()
        self.report['purpose'] = 'Coffee skeletal presentation plus retained native paint/tool gameplay'
        self.report['source_sha256']['Tools/Unreal/probe_coffee_slime_solo.py'] = hashlib.sha256(
            (ROOT/'Tools/Unreal/probe_coffee_slime_solo.py').read_bytes()).hexdigest()
        self.report['limitations'].append('Idle is delivered as an authored asset; the existing alive jelly always hops. Solo does not validate network peers.')

    def jelly_metrics(self, jelly):
        result = super().jelly_metrics(jelly)
        mesh = prop(jelly, 'jelly_mesh')
        ready = bool(prop(jelly, 'coffee_presentation_ready'))
        self.require(ready and mesh.get_skeletal_mesh_asset() is not None,
                     'Native jelly did not activate the complete coffee mesh/clip set')
        node = mesh.get_anim_instance()
        clip = node.get_animation_asset()
        if clip is not None: self.seen_coffee_clips.add(clip.get_name())
        result['coffee'] = dict(mesh=path(mesh.get_skeletal_mesh_asset()),
            clip=path(clip), position_seconds=mesh.get_position(),
            visible=mesh.is_visible(), scale=vec(mesh.get_world_scale()),
            relative_location_cm=vec(prop(mesh,'relative_location')),
            gel_body_cm=vec(mesh.get_socket_location('gel_body')),
            foam_tip_cm=vec(mesh.get_socket_location('foam_tip')),
            primitive_visible=prop(jelly,'visual').is_visible(),
            old_eyes_visible=prop(jelly,'left_eye').is_visible() or prop(jelly,'right_eye').is_visible())
        self.require(not result['coffee']['primitive_visible'] and not result['coffee']['old_eyes_visible'],
                     'Primitive body/eyes overlap the coffee presentation')
        self.require(all(abs(v-1.)<.001 for v in result['coffee']['scale']),
                     'Native procedural scale was applied on top of authored coffee animation')
        return result

    def finish(self, error=None):
        if not error:
            required={'A_CoffeeSlime_Hop','A_CoffeeSlime_Hurt','A_CoffeeSlime_PreBurst','A_CoffeeSlime_Burst'}
            missing=required-self.seen_coffee_clips
            self.report['observed_coffee_clips']=sorted(self.seen_coffee_clips)
            if missing: error='Missing actual native clip observation: '+str(sorted(missing))
            else:
                self.report['checks']['native_coffee_skin_and_four_gameplay_clips']=True
                self.report['checks']['coffee_replaces_primitives_without_double_scale']=True
        super().finish(error)

previous=getattr(builtins,KEY,None)
if previous and not getattr(previous,'done',True):
    raise RuntimeError('Preserve the active owned coffee probe')
probe=CoffeeSlimeSolo()
setattr(builtins,KEY,probe)
try: probe.start()
except Exception:
    probe.finish(traceback.format_exc())
    raise
