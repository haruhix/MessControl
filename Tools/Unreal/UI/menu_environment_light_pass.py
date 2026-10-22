"""One scoped menu light/grade proposal; root executes and judges actual PIE.

run('plan') is read-only. run('apply', output=FRESH_E_FOLDER) changes only four
existing named PointLights, the existing fog and camera-local PP in AnatomyV2.
No camera pose/FOV/DOF, meshes, materials, UI, GI mode or project CVars change.
The inherited point-light falloff is deliberately unitless, NOT lumen values.
"""
from pathlib import Path
import hashlib
import json

ROOT = Path('E:/DEVGAME/MessControl')
MAP = '/Game/UI/MenuShowcase/AnatomyV2/L_MenuAnatomyV2'
LIGHTS = {
    'Cream key': {'location': [-850, 135, 315], 'intensity': 8.0,
                  'color': [1.0, .91, .80], 'radius': 1000., 'specular': .24,
                  'source_radius': 55., 'soft_source_radius': 25., 'shadows': True},
    'Warm entrance': {'location': [-1040, 15, 235], 'intensity': 2.2,
                      'color': [.80, .85, .93], 'radius': 1550., 'specular': .06,
                      'source_radius': 65., 'soft_source_radius': 30., 'shadows': False},
    'Throat depth': {'location': [190, -80, 175], 'intensity': 6.0,
                     'color': [.82, .68, .78], 'radius': 2600., 'specular': .10,
                     'source_radius': 60., 'soft_source_radius': 25., 'shadows': False},
    'Mint edge': {'location': [-510, 500, 195], 'intensity': 1.6,
                  'color': [.56, .78, .84], 'radius': 520., 'specular': .18,
                  'source_radius': 35., 'soft_source_radius': 15., 'shadows': False},
}
FOG = {'fog_density': .014, 'fog_height_falloff': .12, 'fog_max_opacity': .28,
       'start_distance': 700., 'fog_inscattering_luminance': [.060, .025, .045]}
PP_SCALARS = {'auto_exposure_bias': -.65, 'bloom_intensity': .10,
              'bloom_threshold': 1.5, 'vignette_intensity': .15,
              'ambient_occlusion_intensity': .55, 'ambient_occlusion_radius': 28.,
              'ambient_occlusion_power': 1.05, 'ambient_occlusion_quality': 80.}
PP_COLORS = {'color_saturation': [1.02, 1.02, 1.02, 1.],
             'color_contrast': [1.035, 1.035, 1.035, 1.],
             'color_saturation_shadows': [.92, .92, .92, 1.],
             'color_saturation_midtones': [1.035, 1.035, 1.035, 1.],
             'color_saturation_highlights': [.96, .96, .96, 1.],
             'color_gain_highlights': [.995, .990, .975, 1.],
             'color_gain_shadows': [.965, .980, 1.020, 1.]}
CRITERIA = [
    'Same actual PIE camera/frame: face and working enamel retain contour and fine surface detail; no joined white clipped plate, black brush-shaped slab or broad black halo at the soles.',
    'Tongue is coral under the key and quieter outside it. The large pink floor specular hotspot must not compete with the hero/brush; cool edge stays a narrow accent rather than cyan tooth undersides.',
    'Palate/uvula/teeth recede through readable plum values. Fog begins behind the roughly 650-cm working subject, without veiling its eyes/hands; far opening may be dark but the surrounding arches remain distinct.'
]

# Bounded follow-up to the actual PIE frame with the white oval at U~.30.
# Root applies ONE option manually. Do not re-run the legacy full run('apply')
# for this comparison: keep all other live light/fog/PP adjustments intact.
# Both options share the same depth change, so A/B isolates the hotspot strategy.
# All numbers remain unitless brightness with inverse-square OFF/falloff 2.2.
AB_REVISIONS = {
    'A': {
        'name': 'Move the key reflection outside the frame',
        'lights_delta': {
            'Cream key': {'location': [-850, -70, 315], 'intensity': 9.0, 'specular': .24},
            'Throat depth': {'location': [190, -250, 260], 'intensity': 5.4,
                             'color': [.68, .66, .86], 'radius': 2600.}
        },
        'camera_local_pp_delta': {'color_saturation_shadows': [.88, .88, .88, 1.]},
        'why': [
            'Key Y135->-70 moves the flat-floor mirror point from inside the left-centre image to beyond its left edge. Key specular scale stays .24 so enamel/foam can retain a real highlight.',
            'Key power 8->9 compensates part of the longer hero-light distance; it is not a global exposure lift.',
            'Depth shifts laterally/upward to light arch normals asymmetrically instead of filling them almost frontally. Radius2600 retains the reach that was lost at1100.',
            'Depth power6->5.4 plus neutral lavender .68/.66/.86 reduces red wash while preserving blue-channel light and readable folds. Shadow-only saturation .88 mutes deep red without desaturating coral foreground/highlight whites.'
        ]
    },
    'B': {
        'name': 'Keep key modelling; suppress only its specular contribution',
        'lights_delta': {
            'Cream key': {'location': [-850, 135, 315], 'intensity': 8.0, 'specular': .06},
            'Throat depth': {'location': [190, -250, 260], 'intensity': 5.4,
                             'color': [.68, .66, .86], 'radius': 2600.}
        },
        'camera_local_pp_delta': {'color_saturation_shadows': [.88, .88, .88, 1.]},
        'why': [
            'Key remains at[-850,135,315], intensity8. Only its SpecularScale drops .24->.06: the diffuse form/shadow modelling does not change. Other named lights retain their specular terms, so this is not a matte-material conversion.',
            'This is the conservative alternative if moving the key in A damages face/working-tooth modelling. It may leave a weaker oval; unchanged oval would disprove the key-only specular hypothesis.',
            'Identical depth/PP deltas to A keep the background comparison controlled.'
        ]
    }
}


def require(ok, message):
    if not ok:
        raise RuntimeError(message)


def run(action='plan', *, output='E:/CodexWork/MessControlMenuAnimation_20261022/EnvironmentLightPass01'):
    import unreal as u
    require(action in ('plan', 'apply'), 'Expected plan or apply')
    require(Path(u.Paths.project_dir()).resolve() == ROOT, 'Wrong project')
    levels = u.get_editor_subsystem(u.LevelEditorSubsystem)
    require(not levels.is_in_play_in_editor(), 'Preserve active PIE')
    world = u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
    require(world.get_path_name() == MAP + '.L_MenuAnatomyV2', 'Owned AnatomyV2 preview only; no level travel')
    actors = u.get_editor_subsystem(u.EditorActorSubsystem).get_all_level_actors()
    camera = [a for a in actors if 'MC_MenuV2_Camera' in [str(t) for t in a.tags]]
    fog = [a for a in actors if a.get_actor_label() == 'MENU V2 | Plum depth atmosphere']
    require(len(camera) == len(fog) == 1, 'Expected the existing owned camera and fog')
    camera = camera[0]
    cam = camera.camera_component
    fog = fog[0].get_component_by_class(u.ExponentialHeightFogComponent)
    require(fog is not None, 'Expected ExponentialHeightFogComponent')
    named = {}
    for name in LIGHTS:
        matches = [a for a in actors if a.get_actor_label() == 'MENU V2 | ' + name]
        require(len(matches) == 1 and isinstance(matches[0], u.PointLight), 'Expected named PointLight: ' + name)
        named[name] = matches[0]

    def pose(a):
        loc, rot, scale = a.get_actor_location(), a.get_actor_rotation(), a.get_actor_scale3d()
        return [loc.x, loc.y, loc.z, rot.pitch, rot.yaw, rot.roll, scale.x, scale.y, scale.z]

    pp = cam.get_editor_property('post_process_settings')
    protected_camera = {'pose': pose(camera), 'fov': cam.get_editor_property('field_of_view'),
                        'focus': pp.get_editor_property('depth_of_field_focal_distance'),
                        'fstop': pp.get_editor_property('depth_of_field_fstop'),
                        'sensor': pp.get_editor_property('depth_of_field_sensor_width'),
                        'gi': str(pp.get_editor_property('dynamic_global_illumination_method'))}
    require(pp.get_editor_property('dynamic_global_illumination_method') == u.DynamicGlobalIlluminationMethod.NONE,
            'GI must already be explicitly NONE; this script will not switch rendering methods')
    dirty = sorted(p.get_path_name() for p in
                   list(u.EditorLoadingAndSavingUtils.get_dirty_content_packages()) +
                   list(u.EditorLoadingAndSavingUtils.get_dirty_map_packages()))
    report = {'status': 'proposal', 'map': MAP, 'lights': LIGHTS, 'fog': FOG,
              'post_process_scalars': PP_SCALARS, 'post_process_colors': PP_COLORS,
              'protected_camera': protected_camera, 'diagnostic_criteria': CRITERIA,
              'before': {}, 'dirty_before': dirty,
              'limits': ['Values are one unexecuted look proposal, not a visual PASS.',
                         'SourceRadius alone does not create soft conventional shadow maps; rendering backend is unchanged.',
                         'Existing material texture/roughness quality, camera composition and UI remain root-owned.',
                         'Only the named key casts mapped shadows; screen-space contact-shadow overlay is disabled.']}
    for name, actor in named.items():
        c = actor.point_light_component
        report['before'][name] = {'pose': pose(actor), 'properties': {key: str(c.get_editor_property(key)) for key in
            ('intensity', 'light_color', 'attenuation_radius', 'use_inverse_squared_falloff', 'light_falloff_exponent', 'specular_scale', 'cast_shadows')}}
    if action == 'plan':
        u.log(json.dumps(report, indent=2))
        return report
    require(not dirty, 'Preserve unsaved editor packages; root must finish/save its owned work first')
    out = Path(output).resolve()
    require(out.drive.upper() == 'E:' and not out.exists(), 'Use a fresh E: report directory')
    out.mkdir(parents=True)
    source = ROOT / 'Content' / (MAP.removeprefix('/Game/') + '.umap')
    require(source.is_file(), 'Saved preview map is missing')
    before_bytes = source.read_bytes()
    backup = out / 'Before_L_MenuAnatomyV2.umap'
    backup.write_bytes(before_bytes)
    require(hashlib.sha256(backup.read_bytes()).digest() == hashlib.sha256(before_bytes).digest(), 'Map backup mismatch')
    with u.ScopedEditorTransaction('Menu environment warm key and plum depth'):
        for name, values in LIGHTS.items():
            actor, c = named[name], named[name].point_light_component
            actor.modify(); c.modify()
            actor.set_actor_location(u.Vector(*values['location']), False, True)
            c.set_editor_property('use_inverse_squared_falloff', False)
            c.set_editor_property('light_falloff_exponent', 2.2)
            c.set_intensity(values['intensity'])
            c.set_light_color(u.LinearColor(*values['color'], 1.), False)
            c.set_attenuation_radius(values['radius'])
            c.set_source_radius(values['source_radius'])
            c.set_soft_source_radius(values['soft_source_radius'])
            c.set_cast_shadows(values['shadows'])
            for key, value in {'specular_scale': values['specular'], 'contact_shadow_length': 0.,
                               'shadow_sharpen': 0., 'shadow_bias': .45, 'shadow_slope_bias': .55}.items():
                c.set_editor_property(key, value)
        fog.modify()
        for key, value in FOG.items():
            fog.set_editor_property(key, u.LinearColor(*value, 1.) if isinstance(value, list) else value)
        fog.set_volumetric_fog(False)
        camera.modify(); cam.modify()
        for key, value in PP_SCALARS.items():
            pp.set_editor_property('override_' + key, True)
            pp.set_editor_property(key, value)
        for key, value in PP_COLORS.items():
            pp.set_editor_property('override_' + key, True)
            pp.set_editor_property(key, u.Vector4(*value))
        cam.set_editor_property('post_process_settings', pp)
        # Do not touch blend weight, exposure method, DOF or any camera pose/lens setting.
        require(pose(camera) == protected_camera['pose'] and cam.get_editor_property('field_of_view') == protected_camera['fov'],
                'Camera pose/lens changed unexpectedly')
        require(all(pp.get_editor_property(key) == value for key,value in {
            'depth_of_field_focal_distance': protected_camera['focus'], 'depth_of_field_fstop': protected_camera['fstop'],
            'depth_of_field_sensor_width': protected_camera['sensor']}.items()), 'DOF changed unexpectedly')
        require(levels.save_current_level(), 'Could not save the owned preview map')
    report.update(status='applied_pending_root_actual_PIE_comparison', backup=str(backup),
                  backup_sha256=hashlib.sha256(before_bytes).hexdigest(), changed_packages=[MAP])
    (out / 'EnvironmentLightPass.json').write_text(json.dumps(report, indent=2), encoding='utf8')
    u.log('MC_MENU_ENVIRONMENT_LIGHT_PASS_PENDING_VISUAL_REVIEW ' + str(out))
    return report
