"""One owned solo: real F3 coffee budget, then a controlled final brush stroke.

Requires current Development Editor, idle L_Mouth, -MCHubTransientProfile.
No direct cleaning/success-cue call, asset save, build, or extra peer.
"""
import builtins, hashlib, json, runpy, time, traceback
from pathlib import Path
import unreal as u

ROOT = Path('E:/DEVGAME/MessControl')
OUT = Path(globals().get('MC_GRIME_OUTPUT', 'E:/CodexWork/MessControl/Validation/ToothGrimeBudget/Solo'))
LOG = globals().get('MC_GRIME_LOGFILE')
support = runpy.run_path(str(ROOT/'Tools/Unreal/check_food_auto_mouth_solo.py'), run_name='grime_solo_support',
    init_globals={'MC_AUTO_OUTPUT': str(OUT), 'MC_AUTO_LOGFILE': LOG})
Parent, prop, vec, valid, path = (support[k] for k in ('FoodAutoMouthSolo', 'prop', 'vec', 'valid', 'path'))
KEY = 'mc_tooth_grime_budget_solo'


class GrimeSolo(Parent):
    def __init__(self):
        super().__init__()
        self.subject = None
        self.seen_completion_reactions = {}
        self.final_stroke_prepared = False
        self.old_dev_ui = u.SystemLibrary.get_console_variable_int_value('MC.DevUI')
        self.report.update(mode='tooth_grime_budget_and_final_stroke_success', scope='Owned solo F3 CoffeeDirt, then genuine held primary brush completing a controlled final-stroke mask',
            budget_samples=[], checks={}, completion_reaction_actors=[], limitations=[
                'The new AddDirt route is invoked by real F3 CoffeeDirt. River-pour selection and legacy Coffee StartDay are source/native coverage, not this solo run.',
                'Two F3 samples prove observed counts and identities; they do not provide a statistical distribution test or require unequal left/right counts.',
                'After budget observation this owned PIE isolates one saved tooth, clears other tooth/floor/hero dirt and pauses event schedulers. Tooth mesh/scale and brush rate remain unchanged; the worker is stationary and faces the tooth.',
                'This is a controlled FINAL-STROKE test, not full-crown cleaning coverage. After a genuine native stroke changes the original mask, preparation retains only changed still-dirty texels at their actual native values and clears the rest. Normal held primary input must complete the retained dirt. No completion/VFX callback is injected.',
                'BrushContact Target/LocalPoint/LocalNormal and ReactionVFX Effect are protected from Python. No native contact/normal or effect-kind readback is claimed; new reaction actors after cleaning are candidates requiring root visual confirmation of the green check.',
                'A point outside the enamel AABB is definitely outside the crown; a point inside that box may still be outside its curved surface. Box distance is diagnostic, not visual acceptance.',
                'Camera is the ordinary gameplay view; screenshots/reaction world coordinates require root visual review. This is not a network or performance test.'])
        self.report['source_sha256'] = {p: hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in (
            'Source/MessControl/Private/MCDayDirector.cpp', 'Source/MessControl/Private/MCCoffeeFlood.cpp',
            'Source/MessControl/Private/MCToothCharacter.cpp', 'Source/MessControl/Public/MCMouthSurface.h')}
        (self.output/'README.md').write_text(__doc__+'\n\nSee Report.json for actual scope/fixture/captures.\n', encoding='utf8')

    def intake_log_lines(self):
        return []

    def choose_rows(self):
        pass

    def dirty(self, tooth):
        return int(prop(prop(prop(tooth, 'status'), 'state'), 'coffee_left')) > 0

    def fixture(self, _):
        self.teeth = sorted([t for t in self.actors(u.MCArenaTooth)
            if not prop(prop(t, 'state'), 'lost') and not prop(prop(t, 'state'), 'consumed')],
            key=lambda t: int(prop(prop(t, 'state'), 'tooth_id')))
        self.require(len(self.teeth) == 12, 'Expected the actual saved 12 available teeth, not a substitute arena')
        for cls in (u.MCDayDirector, u.MCGameDirector):
            for actor in self.actors(cls): actor.set_actor_tick_enabled(False)
        for actor in self.actors(u.MCCoffeeFlood): actor.destroy_actor()
        # RequestDevAction intentionally ignores calls until Development tools
        # opt in. Enable the normal owned-editor CVar, then restore it in finish.
        self.console('MC.DevUI 1')
        self.require(u.SystemLibrary.get_console_variable_int_value('MC.DevUI') == 1, 'Owned Development controls did not enable')
        self.report['debug_ui_before'] = self.old_dev_ui
        for sample in range(2):
            for tooth in self.teeth: tooth.set_coffee(0.)
            self.pc.call_method('RequestDevAction', args=(u.MCDevAction.COFFEE_DIRT, -1))
            selected = [t for t in self.teeth if self.dirty(t)]
            self.report['budget_samples'].append(dict(sample=sample, available=len(self.teeth), dirty=len(selected),
                ids=[int(prop(prop(t, 'state'), 'tooth_id')) for t in selected],
                left=sum(t.get_actor_location().y < 0 for t in selected), right=sum(t.get_actor_location().y >= 0 for t in selected)))
            self.require(len(selected) == 6, 'Real F3/AddDirt did not halve the combined tooth budget to six')
        self.report['checks']['combined_twelve_to_six'] = True
        self.report['selection_changed_between_samples'] = self.report['budget_samples'][0]['ids'] != self.report['budget_samples'][1]['ids']
        subject_id = int(globals().get('MC_GRIME_SUBJECT_ID', 3))
        self.subject = next((t for t in self.teeth if int(prop(prop(t, 'state'), 'tooth_id')) == subject_id), None)
        self.require(valid(self.subject), 'Requested saved tooth is absent')
        for tooth in self.teeth: tooth.set_coffee(1. if tooth == self.subject else 0.)
        # Public mask no-op preflight on this owned actor. A refusal fails the
        # fixture; never bypass a protected/read-only engine property.
        self.initial_mask = list(self.subject.get_editor_property('GrimeMask'))
        self.require(len(self.initial_mask) == 4096, 'Expected the native 16-cubed wipe mask')
        self.subject.set_editor_property('GrimeMask', self.initial_mask)
        self.require(list(self.subject.get_editor_property('GrimeMask')) == self.initial_mask, 'Public mask no-op write failed')
        (self.output/'InitialGrimeMask.json').write_text(json.dumps(self.initial_mask), encoding='utf8')
        self.report['mask_public_noop_roundtrip'] = True
        self.report['initial_mask_sha256'] = hashlib.sha256(bytes(self.initial_mask)).hexdigest()
        for actor in self.actors(u.MCMouthSurface):
            if not prop(actor, 'ulcer'): actor.destroy_actor()
        status = prop(self.hero, 'status'); state = prop(status, 'state')
        state.set_editor_property('coffee_left', 0); status.set_editor_property('state', state)
        self.hero.set_editor_property('bInCoffee', False)
        self.hero.set_editor_property('bSelfCare', False)
        visual = prop(self.subject, 'visual')
        origin, extent = u.SystemLibrary.get_component_bounds(visual)[:2]
        self.report['subject'] = dict(id=subject_id, actor=path(self.subject), mesh=path(prop(visual, 'static_mesh')),
            actor_root=vec(self.subject.get_actor_location()), bounds_origin=vec(origin), bounds_extent=vec(extent),
            dimensions_cm=vec(extent*2), is_large_over_300_cm=max(vec(extent*2)) >= 300,
            saved_scale=vec(visual.get_world_scale()), initial_coffee_left=int(prop(prop(prop(self.subject, 'status'), 'state'), 'coffee_left')))
        root = self.subject.get_actor_location(); body_extent = prop(self.subject, 'body').get_scaled_box_extent()
        side = 1. if root.y > 0 else -1.
        approach = u.Vector(root.x, root.y-side*(body_extent.y+95.), root.z)
        floor = self.floor(approach)
        self.require(floor is not None, 'The real saved tooth has no supported brush approach')
        rotation = u.MathLibrary.find_look_at_rotation(approach, root); rotation.pitch = 0; rotation.roll = 0
        self.move_to(floor, rotation.yaw)
        self.pc.set_control_rotation(u.Rotator(pitch=-10, yaw=rotation.yaw))
        self.pc.set_view_target_with_blend(self.hero, 0.)
        self.start_x = root.x
        self.report['subject']['hero_approach'] = vec(self.hero.get_actor_location())
        self.ignored_reactions = {path(a) for a in self.actors(u.MCReactionVFX)}
        self.success_before = float(prop(self.hero, 'task_success_at'))
        prop(self.hero, 'inventory').call_method('ServerSelect', args=(u.MCToolSlot.BRUSH,))
        self.stage('tooth_before')

    def prepare_final_stroke(self):
        current = list(self.subject.get_editor_property('GrimeMask'))
        retained = [i for i, (before, value) in enumerate(zip(self.initial_mask, current)) if before != value and 65 < value < 255]
        if not retained: return False
        reduced = [0]*len(current)
        for index in retained: reduced[index] = current[index]
        # ResetGrime, through the public SetCoffee/ApplyCoffee route, clears
        # PreciseGrimeMask. Otherwise Stroke restores cleared bytes from its
        # old private fractional cache on the following genuine native stroke.
        self.subject.set_coffee(1.)
        self.subject.set_editor_property('GrimeMask', reduced)
        self.require(list(self.subject.get_editor_property('GrimeMask')) == reduced, 'Public final-stroke mask write failed')
        (self.output/'NativeMaskBeforeFinalPreparation.json').write_text(json.dumps(current), encoding='utf8')
        (self.output/'ControlledFinalMask.json').write_text(json.dumps(reduced), encoding='utf8')
        self.report['final_stroke_precondition'] = dict(genuine_native_mask_change_before_preparation=True,
            changed_native_texels=sum(a != b for a, b in zip(self.initial_mask, current)), retained_texels=len(retained),
            retained_min=min(current[i] for i in retained), retained_max=max(current[i] for i in retained),
            retained_values_equal_native_current=True, all_other_texels_cleared=True,
            before_sha256=hashlib.sha256(bytes(current)).hexdigest(), prepared_sha256=hashlib.sha256(bytes(reduced)).hexdigest(),
            fractional_cache_reset='public SetCoffee(1) -> Status.ApplyCoffee -> ResetGrime clears PreciseGrimeMask before public mask write',
            native_completion_injected=False, full_crown_coverage_claimed=False)
        self.final_stroke_prepared = True
        self.stage('tooth_final_stroke')
        return True

    def observe(self, now):
        reactions = self.actors(u.MCReactionVFX)
        # Existing/working reactions cannot be mistaken for a completion cue.
        if self.dirty(self.subject):
            self.ignored_reactions.update(path(actor) for actor in reactions)
        for actor in reactions:
            key = path(actor)
            if key in self.ignored_reactions or key in self.seen_completion_reactions or self.dirty(self.subject):
                continue
            position = vec(actor.get_actor_location())
            subject = self.report['subject']; origin = subject['bounds_origin']; extent = subject['bounds_extent']
            outside = [max(0., abs(position[k]-origin[k])-extent[k]) for k in range(3)]
            baseline = [subject['actor_root'][0], subject['actor_root'][1], subject['actor_root'][2]+110.]
            row = dict(actor=key, time=now, position=position, effect_kind_readback='unavailable_protected_property',
                distance_outside_visual_aabb_cm=sum(v*v for v in outside)**.5,
                old_root_plus_110_position=baseline,
                distance_from_old_root_plus_110_cm=sum((position[k]-baseline[k])**2 for k in range(3))**.5)
            self.seen_completion_reactions[key] = row; self.report['completion_reaction_actors'].append(row)
        manager = self.pc.player_camera_manager
        self.report['camera'] = dict(view_target=path(self.pc.get_view_target()), position=vec(manager.get_camera_location()), rotation=str(manager.get_camera_rotation()))
        self.report['samples'].append(dict(time=now, phase=self.phase,
            coffee_left=int(prop(prop(prop(self.subject, 'status'), 'state'), 'coffee_left')),
            task_success_at=float(prop(self.hero, 'task_success_at')), camera=path(self.pc.get_view_target())))

    def tick(self, delta):
        if self.done: return
        if self.phase == 'waiting_pie': return super().tick(delta)
        try:
            self.require(time.monotonic()-self.started < 210, 'Owned grime solo exceeded its bounded runtime')
            self.require(self.owned_pie_is_active(), 'Owned PIE stopped or was replaced')
            now = float(u.GameplayStatics.get_time_seconds(self.world))
            if self.last_game_time is not None and now <= self.last_game_time: return
            self.last_game_time = now
            if now >= self.sample_at: self.sample_at = now+.08; self.observe(now)
            if now >= self.frame_at:
                self.frame_at = now+1/8
                self.screenshot('%05d.png' % len(self.report['frames']), True)
            elapsed = now-self.phase_at
            if self.phase == 'tooth_before' and elapsed >= 1.:
                self.primary_input(True); self.stage('tooth_brush')
            elif self.phase in ('tooth_brush', 'tooth_final_stroke'):
                self.require(elapsed < 12, 'Genuine held brush did not reach/complete the controlled final-stroke fixture')
                self.hero.get_movement_component().stop_movement_immediately()
                facing=u.MathLibrary.find_look_at_rotation(self.hero.get_actor_location(),self.subject.get_actor_location())
                facing.pitch=0;facing.roll=0
                self.hero.set_actor_rotation(facing,False)
                self.pc.set_control_rotation(u.Rotator(pitch=-10,yaw=facing.yaw))
                if not self.final_stroke_prepared:
                    self.prepare_final_stroke()
                    return
                if not self.dirty(self.subject):
                    self.primary_input(False); self.hero.get_movement_component().stop_movement_immediately()
                    self.report['completion_contact_readback'] = 'unavailable: protected native fields/non-reflected getters; no endpoint equality claim'
                    self.report['checks']['real_brush_completed_controlled_final_stroke'] = True
                    self.success_at = float(prop(self.hero, 'task_success_at'))
                    self.report['checks']['accepted_success_timestamp_advanced'] = self.success_at > self.success_before
                    self.stage('tooth_tail')
            elif self.phase == 'tooth_tail' and elapsed >= 2.6:
                self.report['checks'].update(one_completion_reaction_actor=len(self.seen_completion_reactions)==1,
                    no_repeated_success_timestamp=float(prop(self.hero, 'task_success_at')) == self.success_at,
                    gameplay_camera_retained=self.pc.get_view_target() == self.hero)
                self.require(all(self.report['checks'].values()), 'Completion/Stars/held-input tail checks failed')
                self.finish()
        except Exception: self.finish(traceback.format_exc())

    def finish(self, error=None):
        if self.done: return
        # Real F3 actions retire the old SingleDayDirector. Its stale Python
        # wrapper cannot be passed to IsValid after collection. This owned PIE
        # is stopped below, so there is no surviving scheduler tick to restore.
        self.scheduler = None
        self.scheduler_tick = None
        try:
            self.console('MC.DevUI '+str(self.old_dev_ui))
            self.report['debug_ui_restored'] = u.SystemLibrary.get_console_variable_int_value('MC.DevUI') == self.old_dev_ui
            if not self.report['debug_ui_restored']: error = error or 'Development control CVar restoration failed'
        except Exception:
            error = error or traceback.format_exc()
        super().finish(error)


def main():
    previous = getattr(builtins, KEY, None)
    if previous and not previous.done: raise RuntimeError('Existing owned grime probe preserved')
    probe = GrimeSolo(); setattr(builtins, KEY, probe)
    try: probe.start()
    except Exception: probe.finish(traceback.format_exc()); raise

if __name__ == '__main__': main()
