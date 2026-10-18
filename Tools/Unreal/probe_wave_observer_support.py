"""Position-only wave observer fixture, followed by real held RMB on a real tooth.

Used by the short cleaning/jelly evidence probes. No health, movement mode,
velocity, body state, phase, collision or native force is assigned here.
"""
import math
import unreal as u


def brace_observer_for_wave(probe):
    tongue = probe.tongue.get_editor_property('surface')
    mouth, _, _ = u.SystemLibrary.get_component_bounds(tongue)
    radius = float(probe.hero.capsule_component.get_scaled_capsule_radius())
    options = []
    for tooth in probe.actors(u.MCArenaTooth):
        if not tooth.is_available():
            continue
        body = tooth.get_editor_property('body')
        center, extent, _ = u.SystemLibrary.get_component_bounds(body)
        inward = math.atan2(mouth.y-center.y, mouth.x-center.x)
        for turn in (0., .4, -.4, .8, -.8):
            angle = inward+turn
            dx, dy = math.cos(angle), math.sin(angle)
            exits = [extent.x/abs(dx) if abs(dx) > .001 else 1.e9,
                     extent.y/abs(dy) if abs(dy) > .001 else 1.e9]
            near = center+u.Vector(dx, dy, 0)*(min(exits)+radius+14.)
            foot = probe.floor(near)
            if foot is None:
                continue
            safe = True
            for offset in (u.Vector(radius+4, 0, 0), u.Vector(-radius-4, 0, 0),
                           u.Vector(0, radius+4, 0), u.Vector(0, -radius-4, 0)):
                edge = probe.floor(foot+offset)
                if edge is None or abs(edge.z-foot.z) > 25.:
                    safe = False
                    break
            if safe:
                separation = math.hypot(foot.x-mouth.x, foot.y-mouth.y)
                options.append((separation, tooth, foot))
    options.sort(key=lambda item: item[0])
    grip = probe.hero.get_editor_property('grip')
    attempts = []
    probe.primary_input(False)
    for _, tooth, foot in options:
        probe.hero.call_method('SetBraceInputHeld', args=(False,))
        target = tooth.get_actor_location()
        yaw = math.degrees(math.atan2(target.y-foot.y, target.x-foot.x))
        probe.place_observer(foot, yaw, 'Supported tooth-side fixture; actual held RMB braces through the real opening wave')
        probe.hero.call_method('SetBraceInputHeld', args=(True,))
        anchored = bool(grip.is_bracing() and grip.is_world_anchored())
        attempts.append(dict(tooth=tooth.get_path_name(), anchored=anchored))
        if anchored:
            point = grip.brace_point()
            anchor = grip.brace_target()
            probe.report['opening_wave_observer'] = dict(
                api='AMCToothCharacter::SetBraceInputHeld(true)',
                anchor=anchor.get_path_name(), point_cm=[point.x, point.y, point.z],
                foot_cm=[foot.x, foot.y, foot.z], attempts=attempts,
                health_or_movement_or_phase_written=False,
                purpose='Hold the observer for a bounded subsystem check; not a wave-survival difficulty test')
            return
    probe.hero.call_method('SetBraceInputHeld', args=(False,))
    probe.report['opening_wave_observer'] = dict(attempts=attempts, anchored=False)
    raise RuntimeError('No supported native tooth-side RMB handhold for the opening-wave observer')


def release_wave_observer(probe):
    probe.hero.call_method('SetBraceInputHeld', args=(False,))
    row = probe.report.get('opening_wave_observer', {})
    row['released_at_seconds'] = float(u.GameplayStatics.get_time_seconds(probe.world))
    row['released'] = not probe.hero.get_editor_property('grip').is_brace_input_held()
    probe.require(row['released'], 'Opening-wave RMB input did not release before the normal work input')
