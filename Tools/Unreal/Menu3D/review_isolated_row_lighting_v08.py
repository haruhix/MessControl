"""Capture three lighting candidates in an already-open DAY03 solo PIE.

Run only through editor Python after selecting DAY03 in the three-row catalog.
This helper does not navigate, move the camera, edit materials, save packages, or
change rendering configuration. The two isolated candidates use 12 unattached,
deferred-spawned runtime RectLights. Call
``unreal._mc_isolated_lighting_review.finish('cancelled')`` to cancel safely.
"""
import json
from pathlib import Path
import time
import traceback
import unreal as u


class _MCIsolatedLightingReview:
    LIGHT_TAGS = ('Menu3D_Key', 'Menu3D_Fill', 'Menu3D_BlockingRim', 'Menu3D_BlockingCavity')
    # Reflected names checked against local UE 5.8 LightComponent{,Base},
    # LocalLightComponent and RectLightComponent headers. Copy before FinishSpawning.
    COPY_PROPERTIES = (
        'mobility', 'intensity_units', 'intensity', 'light_color', 'affects_world',
        'source_width', 'source_height', 'attenuation_radius', 'source_texture',
        'source_texture_scale', 'source_texture_offset', 'barn_door_angle', 'barn_door_length',
        'light_function_cone_angle', 'inverse_exposure_blend',
        'cast_shadows', 'cast_static_shadows', 'cast_dynamic_shadows', 'cast_translucent_shadows',
        'cast_raytraced_shadow', 'cast_volumetric_shadow', 'cast_deep_shadow',
        'affect_translucent_lighting', 'transmission', 'affect_reflection', 'affect_global_illumination',
        'indirect_lighting_intensity', 'volumetric_scattering_intensity', 'samples_per_pixel',
        'use_temperature', 'temperature', 'specular_scale', 'diffuse_scale',
        'shadow_resolution_scale', 'shadow_bias', 'shadow_slope_bias', 'shadow_sharpen',
        'contact_shadow_length', 'contact_shadow_length_in_ws',
        'contact_shadow_casting_intensity', 'contact_shadow_non_casting_intensity',
        'cast_shadows_from_cinematic_objects_only', 'force_cached_shadows_for_movable_primitives',
        'allow_mega_lights', 'mega_lights_shadow_method', 'override_ray_end_bias', 'ray_end_bias',
        'use_ray_traced_distance_field_shadows', 'ray_start_offset_depth_scale',
        'max_draw_distance', 'max_distance_fade_range', 'view_lighting_channels',
        'light_function_material', 'light_function_scale', 'light_function_fade_distance',
        'disabled_brightness', 'ies_texture', 'use_ies_brightness', 'ies_brightness_scale',
        'enable_light_shaft_bloom', 'bloom_scale', 'bloom_threshold', 'bloom_max_brightness', 'bloom_tint',
        'hidden_in_game',
    )
    POINTER_PROPERTIES = ('LastPointerX', 'LastPointerY', 'PointerReady', 'HasPointerMotion', 'PendingHoverDay')
    CANDIDATES = ('A_global_baseline', 'B_isolated_same_world_transforms', 'C_day03_centered_key_shadow_only')

    @staticmethod
    def valid(obj):
        return obj is not None and u.SystemLibrary.is_valid(obj)

    @staticmethod
    def channels(component):
        value = component.get_editor_property('lighting_channels')
        return tuple(bool(value.get_editor_property('channel' + str(i))) for i in range(3))

    @staticmethod
    def serial(value):
        if value is None or isinstance(value, (str, int, float, bool)):
            return value
        if isinstance(value, (tuple, list)):
            return [_MCIsolatedLightingReview.serial(item) for item in value]
        if isinstance(value, dict):
            return {str(key): _MCIsolatedLightingReview.serial(item) for key, item in value.items()}
        if hasattr(value, 'export_text'):
            return value.export_text()  # Stable struct contents, without wrapper memory addresses.
        if hasattr(value, 'get_path_name'):
            return value.get_path_name()
        return str(value)

    def __init__(self):
        self.handle = None
        self.spawned = []
        self.rigs = {}
        self.originals = {}
        self.primitives = []
        self.widget = None
        self.mutated = False
        self.finished = False
        self.owns_output = False
        self.pending = None
        self.index = -1
        self.applied_at = None
        self.started = time.monotonic()
        self.state = {'status': 'preflight', 'captures': [], 'cleanup_errors': [],
                      'scope': 'Solo PIE only; no asset/material/camera/configuration changes',
                      'candidate_B': 'Exact source world transforms/properties; exclusive row channels only',
                      'candidate_C': 'Only DAY03 rig recentered/scaled; only its key casts shadows; other row rigs stay at B'}
        self.out = (Path(u.Paths.project_dir()) /
                    'ArtSource/UI/MainMenu3D/20261015_refB_v1/Assembly/Review/Unreal_FunctionalBlocking_v03_FullRoute/IsolatedLighting_v08_01')

    def preflight(self):
        assert not self.out.exists(), 'Preserve the existing IsolatedLighting_v08_01 run; choose a fresh explicit directory'
        assert not getattr(u, '_mc_pbr_compare_handle', None) and not getattr(u, '_mc_lower_review_handle', None), 'Another runtime render review is still running'
        self.world = u.EditorLevelLibrary.get_game_world()
        pie_worlds = list(u.EditorLevelLibrary.get_pie_worlds(True))
        assert self.valid(self.world) and len(pie_worlds) == 1 and pie_worlds[0] == self.world, 'Requires exactly one solo PIE world'
        assert u.SystemLibrary.is_standalone(self.world), 'Network PIE is outside this helper'
        assert u.GameplayStatics.get_num_player_controllers(self.world) == 1
        assert '/Game/UI/MainMenu3D/' in self.world.get_path_name()
        cls = u.load_class(None, '/Game/UI/MainMenu3D/WBP_MainMenu3D.WBP_MainMenu3D_C')
        lib = u.get_default_object(u.load_class(None, '/Script/UMG.WidgetBlueprintLibrary'))
        widgets = lib.call_method('GetAllWidgetsOfClass', args=(self.world, cls, False))
        assert len(widgets) == 1
        self.widget = widgets[0]
        self.check_route()
        assert float(self.widget.get_editor_property('Open3')) > .98, 'Let DAY03 finish opening before running'
        self.previous_visibility = self.widget.get_visibility()
        assert self.previous_visibility not in (u.SlateVisibility.HIDDEN, u.SlateVisibility.COLLAPSED)
        self.pointer_previous = {name: self.widget.get_editor_property(name) for name in self.POINTER_PROPERTIES}
        self.pc = u.GameplayStatics.get_player_controller(self.world, 0)
        self.gameplay = u.get_default_object(u.GameplayStatics)
        self.view_target = self.pc.get_view_target()
        self.view_transform = self.view_target.get_actor_transform()
        self.mouths = {}
        all_actors = list(u.GameplayStatics.get_all_actors_of_class(self.world, u.Actor))
        for day in (1, 2, 3):
            tag = 'Menu3D_DAY%02d' % day
            matches = [actor for actor in all_actors if tag in [str(t) for t in actor.tags]]
            assert len(matches) == 1, tag
            self.mouths[day] = matches[0]
        for actor in all_actors:
            actor_tags = [str(tag) for tag in actor.tags]
            for primitive in actor.get_components_by_class(u.PrimitiveComponent):
                tags = actor_tags + [str(tag) for tag in primitive.get_editor_property('component_tags')]
                days = [day for day in (1, 2, 3) if any(tag.startswith('Menu3D_DAY%02d' % day) for tag in tags)]
                assert len(days) <= 1, 'A primitive has conflicting row tags: ' + primitive.get_path_name()
                if days:
                    self.primitives.append((primitive, days[0], self.channels(primitive)))
        assert all(any(day == item[1] for item in self.primitives) for day in (1, 2, 3))
        for tag in self.LIGHT_TAGS:
            actors = list(u.GameplayStatics.get_all_actors_with_tag(self.world, tag))
            assert len(actors) == 1, tag
            actor = actors[0]
            component = actor.get_component_by_class(u.RectLightComponent)
            assert component and component.get_editor_property('mobility') == u.ComponentMobility.MOVABLE
            assert component.is_visible() and not actor.get_editor_property('hidden'), tag + ' must be active for the baseline'
            assert not component.get_editor_property('hidden_in_game') and component.get_editor_property('affects_world')
            assert self.channels(component) == (True, False, False), tag + ' baseline must be shared channel 0'
            properties = {name: component.get_editor_property(name) for name in self.COPY_PROPERTIES}
            self.originals[tag] = {'actor': actor, 'component': component, 'transform': actor.get_actor_transform(),
                                   'properties': properties, 'channels': self.channels(component),
                                   'visible': component.is_visible()}
        assert all(mask == (True, False, False) for _, _, mask in self.primitives), 'The baseline expects original row channel 0'
        scale = self.mouths[3].get_actor_scale3d()
        assert min(scale.x, scale.y, scale.z) > 0 and max(scale.x, scale.y, scale.z) - min(scale.x, scale.y, scale.z) < 1e-4
        self.native_scale = float(scale.x)
        self.origin = self.mouths[3].get_actor_location()
        for snapshot in self.originals.values():
            assert snapshot['properties']['intensity_units'] in (u.LightUnits.CANDELAS, u.LightUnits.LUMENS, u.LightUnits.NITS), 'Centered candidate needs explicit physical light units'
        self.state.update(world=self.world.get_path_name(), native_scale=self.native_scale,
                          selected_row_origin=str(self.origin), original_widget_visibility=str(self.previous_visibility),
                          before=self.receipt())
        self.out.mkdir(parents=True, exist_ok=False)
        self.owns_output = True

    def check_route(self):
        assert self.widget.get_editor_property('PageIndex') == 1, 'Open the three-row catalog before running'
        assert self.widget.get_editor_property('SelectedDay') == 3 and self.widget.get_editor_property('ActiveDay') == 3, 'Select DAY03 before running'
        assert self.widget.get_editor_property('WindowFirstDay') == 1, 'Requires visible DAY01/DAY02/DAY03 window'

    def receipt(self):
        result = {'primitives': [{'path': p.get_path_name(), 'day': day, 'channels': self.channels(p),
                                  'cast_shadow': p.get_editor_property('cast_shadow')}
                                 for p, day, _ in self.primitives if self.valid(p)],
                  'lights': [], 'page': self.widget.get_editor_property('PageIndex'),
                  'selected_day': self.widget.get_editor_property('SelectedDay'),
                  'open3': self.widget.get_editor_property('Open3'),
                  'widget_visibility': str(self.widget.get_visibility()),
                  'view_target': self.pc.get_view_target().get_path_name(),
                  'view_transform': str(self.pc.get_view_target().get_actor_transform()),
                  'rows': {day: {'transform': str(actor.get_actor_transform()), 'hidden': actor.get_editor_property('hidden')}
                           for day, actor in self.mouths.items()}}
        for day, tag, actor, component in [(0, tag, s['actor'], s['component']) for tag, s in self.originals.items()] + [
                (day, tag, actor, component) for (day, tag), (actor, component) in self.rigs.items()]:
            if self.valid(actor) and self.valid(component):
                result['lights'].append({'day': day, 'source_tag': tag, 'actor': actor.get_path_name(),
                    'transform': str(actor.get_actor_transform()), 'attached_to': self.serial(actor.get_attach_parent_actor()),
                    'visible': component.is_visible(), 'channels': self.channels(component),
                    'properties': self.serial({name: component.get_editor_property(name) for name in self.COPY_PROPERTIES})})
        return self.serial(result)

    def spawn_rigs(self):
        for day in (1, 2, 3):
            for tag, source in self.originals.items():
                # BlueprintInternalUseOnly prevents normal Python glue for these
                # two UFUNCTIONs; call_method builds reflection glue on the CDO.
                actor = self.gameplay.call_method('BeginDeferredActorSpawnFromClass', args=(
                    self.world, u.RectLight, source['transform'], u.SpawnActorCollisionHandlingMethod.ALWAYS_SPAWN,
                    None, u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
                assert self.valid(actor), 'Deferred RectLight spawn failed'
                self.spawned.append(actor)  # Track immediately, including unfinished actors on errors.
                component = actor.get_component_by_class(u.RectLightComponent)
                assert component
                for name, value in source['properties'].items():
                    component.set_editor_property(name, value)
                component.set_lighting_channels(day == 1, day == 2, day == 3)
                component.set_visibility(False, False)
                actor = self.gameplay.call_method('FinishSpawningActor', args=(
                    actor, source['transform'], u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
                assert self.valid(actor) and actor.get_path_name().startswith(self.world.get_path_name() + ':')
                assert actor.get_attach_parent_actor() is None
                mismatches = [name for name, value in source['properties'].items()
                              if self.serial(component.get_editor_property(name)) != self.serial(value)]
                assert not mismatches, 'Light property copy readback mismatch: ' + ', '.join(mismatches)
                self.rigs[(day, tag)] = (actor, component)

    def apply(self, index):
        if index == 0:
            # Actual baseline: do not override source light/material values.
            pass
        elif index == 1:
            self.spawn_rigs()
            for primitive, day, _ in self.primitives:
                primitive.set_lighting_channels(day == 1, day == 2, day == 3)
            for source in self.originals.values():
                source['component'].set_visibility(False, False)
                source['component'].set_intensity(0.)
            for _, component in self.rigs.values():
                component.set_visibility(True, False)
        elif index == 2:
            # Recenter only DAY03; DAY01/02 keep the exact-world copied B rig.
            # Source coordinates are authored around world origin at native scale 1.
            # For candela/lumen sources, scale output by distance squared; for Nits,
            # scaling source area already supplies that factor (RectLightComponent.cpp).
            for tag, source in self.originals.items():
                actor, component = self.rigs[(3, tag)]
                position = source['actor'].get_actor_location()
                location = u.Vector(self.origin.x + position.x * self.native_scale,
                                    self.origin.y + position.y * self.native_scale,
                                    self.origin.z + position.z * self.native_scale)
                actor.set_actor_location(location, False, True)
                actor.set_actor_rotation(u.MathLibrary.find_look_at_rotation(location, self.origin), True)
                component.set_source_width(source['properties']['source_width'] * self.native_scale)
                component.set_source_height(source['properties']['source_height'] * self.native_scale)
                component.set_barn_door_length(source['properties']['barn_door_length'] * self.native_scale)
                component.set_attenuation_radius(source['properties']['attenuation_radius'] * self.native_scale)
                factor = 1. if source['properties']['intensity_units'] == u.LightUnits.NITS else self.native_scale ** 2
                component.set_intensity(source['properties']['intensity'] * factor)
                component.set_cast_shadows(tag == 'Menu3D_Key')
                # Contact shadows are screen-space and are not isolated by channel masks.
                # Disable only on the unshadowed C fill/rim/cavity lights.
                if tag != 'Menu3D_Key':
                    component.set_editor_property('contact_shadow_length', 0.)
        self.index = index
        self.applied_at = time.monotonic()
        self.state['status'] = self.CANDIDATES[index] + '_settling'
        self.write_state()

    def write_state(self):
        (self.out / 'candidate_states.json').write_text(json.dumps(self.state, indent=2), encoding='utf8')

    def start(self):
        try:
            self.preflight()
            self.mutated = True
            self.widget.set_visibility(u.SlateVisibility.HIT_TEST_INVISIBLE)
            self.apply(0)
            self.handle = u.register_slate_post_tick_callback(self.tick)
            u.log('MC_ISOLATED_LIGHTING_STARTED ' + str(self.out))
        except Exception:
            self.finish(traceback.format_exc())
            raise

    def tick(self, delta):
        try:
            now = time.monotonic()
            assert self.valid(self.world) and self.valid(self.widget), 'PIE ended during lighting review'
            assert now - self.started < 45., 'Lighting review watchdog expired'
            self.check_route()
            assert self.pc.get_view_target() == self.view_target, 'View target changed during review'
            assert self.serial(self.view_target.get_actor_transform()) == self.serial(self.view_transform), 'Camera actor moved during review'
            self.widget.call_method('ResetPointerIntent')
            if self.pending:
                path = self.out / self.pending['file']
                if path.is_file():
                    data = path.read_bytes()
                    if data.startswith(b'\x89PNG\r\n\x1a\n') and data.endswith(b'IEND\xaeB`\x82'):
                        self.pending.update(file_bytes=len(data), completed_wall=time.time(),
                                            completed_state=self.receipt())
                        self.state['captures'].append(self.pending)
                        self.pending = None
                        if self.index + 1 == len(self.CANDIDATES):
                            self.finish()
                        else:
                            self.apply(self.index + 1)
                        return
                assert now - self.pending['requested_monotonic'] < 8., 'Screenshot did not complete: ' + str(path)
            elif now - self.applied_at >= 1.:
                self.pending = {'file': self.CANDIDATES[self.index] + '.png', 'candidate': self.CANDIDATES[self.index],
                                'requested_monotonic': now, 'requested_wall': time.time(),
                                'settled_seconds': now - self.applied_at, 'requested_state': self.receipt()}
                self.state['status'] = self.CANDIDATES[self.index] + '_awaiting_actual_png'
                self.write_state()
                path = self.out / self.pending['file']
                u.SystemLibrary.execute_console_command(self.world, 'Shot SHOWUI filename=' + path.as_posix() + ' -nosuffix')
        except Exception:
            self.finish(traceback.format_exc())
            u.log_error('MC_ISOLATED_LIGHTING_FAILED ' + self.state['error'])

    def finish(self, error=None):
        if self.finished:
            return
        self.finished = True
        self.state['error'] = error
        def attempt(label, action):
            try:
                action()
            except Exception as cleanup_error:
                self.state['cleanup_errors'].append(label + ': ' + str(cleanup_error))
        if self.handle is not None:
            attempt('unregister callback', lambda: u.unregister_slate_post_tick_callback(self.handle))
            self.handle = None
        # Disable/destroy copies first, even if a later restoration encounters an error.
        for actor in reversed(self.spawned):
            if self.valid(actor):
                attempt('hide runtime light', lambda actor=actor: actor.set_actor_hidden_in_game(True))
                attempt('destroy runtime light', actor.destroy_actor)
        if self.mutated:
            for primitive, _, mask in self.primitives:
                if self.valid(primitive):
                    attempt('restore primitive channels', lambda p=primitive, m=mask: p.set_lighting_channels(*m))
            for source in self.originals.values():
                component = source['component']
                if self.valid(component):
                    attempt('restore original intensity', lambda c=component, s=source: c.set_intensity(s['properties']['intensity']))
                    attempt('restore original channels', lambda c=component, s=source: c.set_lighting_channels(*s['channels']))
                    attempt('restore original visibility', lambda c=component, s=source: c.set_visibility(s['visible'], False))
            if self.valid(self.widget):
                attempt('restore widget visibility', lambda: self.widget.set_visibility(self.previous_visibility))
                # These transient Blueprint variables are protected on instances.
                # Refresh intent from the current pointer instead of restoring
                # obsolete coordinates through editor-property writes.
                attempt('reset pointer intent', lambda: self.widget.call_method('ResetPointerIntent'))
        if self.valid(getattr(self, 'world', None)) and self.valid(self.widget) and self.originals:
            attempt('cleanup readback', lambda: self.state.update(after=self.receipt()))
            for primitive, _, mask in self.primitives:
                if self.valid(primitive) and self.channels(primitive) != mask:
                    self.state['cleanup_errors'].append('Primitive channel readback mismatch: ' + primitive.get_path_name())
            for tag, source in self.originals.items():
                component = source['component']
                if self.valid(component) and (self.channels(component) != source['channels']
                        or component.is_visible() != source['visible']
                        or component.get_editor_property('intensity') != source['properties']['intensity']):
                    self.state['cleanup_errors'].append('Original light restore readback mismatch: ' + tag)
        self.state['runtime_actors_spawned'] = len(self.spawned)
        self.state['runtime_actors_remaining'] = [actor.get_path_name() for actor in self.spawned if self.valid(actor)]
        self.state['status'] = 'failed' if error or self.state['cleanup_errors'] or self.state['runtime_actors_remaining'] else 'complete'
        if self.owns_output:
            attempt('write final receipt', self.write_state)
        u._mc_isolated_lighting_review = None
        u.log('MC_ISOLATED_LIGHTING_FINISHED ' + self.state['status'])


assert not getattr(u, '_mc_isolated_lighting_review', None), 'An isolated-lighting review is already running'
_mc_review = _MCIsolatedLightingReview()
u._mc_isolated_lighting_review = _mc_review
_mc_review.start()
