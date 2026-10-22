"""One camera-local grade for root's new focused-key menu light rig.

Preparation is data/source only. Root may call apply() in the existing idle
editor. It never changes exposure, lens/DOF, GI, actors/lights/materials, UI,
CVars or project configuration, and never saves the level automatically.
"""
from pathlib import Path
import json

ROOT = Path('E:/DEVGAME/MessControl')
MAP = '/Game/UI/MenuShowcase/AnatomyV2/L_MenuAnatomyV2'
VECTORS = {
    # Global contrast is modest; no global tint/gamma/exposure lift.
    'color_saturation': [.97, .97, .97, 1.],
    'color_contrast': [1.08, 1.08, 1.08, 1.],
    'color_gamma': [1., 1., 1., 1.],
    'color_gain': [1., 1., 1., 1.],
    'color_offset': [0., 0., 0., 0.],
    # Low-value tissue: less red chroma, a restrained plum bias, open folds.
    'color_saturation_shadows': [.90, .90, .90, 1.],
    'color_contrast_shadows': [.98, .98, .98, 1.],
    'color_gamma_shadows': [1.03, 1.03, 1.03, 1.],
    'color_gain_shadows': [1.02, .965, 1.045, 1.],
    'color_offset_shadows': [0., 0., 0., 0.],
    # Near-neutral coral mids retain saturation and reveal the shaped key.
    'color_saturation_midtones': [1.04, 1.04, 1.04, 1.],
    'color_contrast_midtones': [1.02, 1.02, 1.02, 1.],
    'color_gamma_midtones': [1., 1., 1., 1.],
    'color_gain_midtones': [1., 1., 1., 1.],
    'color_offset_midtones': [0., 0., 0., 0.],
    # Cream whites: modest blue reduction, less chroma, gentler contrast.
    'color_saturation_highlights': [.91, .91, .91, 1.],
    'color_contrast_highlights': [.96, .96, .96, 1.],
    'color_gamma_highlights': [1., 1., 1., 1.],
    'color_gain_highlights': [1., .99, .965, 1.],
    'color_offset_highlights': [0., 0., 0., 0.],
}
SCALARS = {
    'color_correction_shadows_max': .16,
    'color_correction_highlights_min': .60,
    'color_correction_highlights_max': 1.20,
    'bloom_intensity': .08,
    'bloom_threshold': 1.80,
    'vignette_intensity': .16,
}
WHY = [
    'Global contrast1.08 replaces the almost-neutral1.035. Combined shadow contrast is1.0584 and gamma1.03 lifts low nonzero values; no FilmBlackClip increase or negative offset crushes the opening/folds.',
    'Shadow saturation combines to.873; shadow red/blue gain is only+2%/+4.5%, green-.035. It biases low tissue toward plum without imposing a red/cyan wash on every surface.',
    'Mid saturation combines to1.0088, essentially preserving the material coral. The directed key and cool rim must create spatial separation; grading is luminance-based, not an anatomy/depth mask.',
    'Highlight contrast combines to1.0368; gain never exceeds1. The blue multiplier.965 and reduced highlight saturation bias ivory slightly warm without lifting white brightness.',
    'Existing film curve, manual exposure-.65, white balance, lens and DOF are retained. Bloom.08/threshold1.8 adds only restrained bright accents; vignette.16 is subtle.'
]
CRITERIA = [
    'Actual same-frame A/B at the new light rig: face/working enamel are the brightest readable subject, with eye sockets, enamel curvature and coffee remaining distinct; no broad clipped white patch.',
    'Three planes remain readable: coral foreground, warm-cream cleaner/tooth working plane, quieter plum arches/uvula. Deep folds remain visible; the throat opening can be black. No purple or yellow global cast.',
    'The foreground tongue highlight and far tissue do not compete with the brush/eyes. Reject if the image merely becomes darker/redder or if shadow desaturation makes grey plastic. Grade cannot repair an unfocused key or missing rim.'
]


def candidate():
    return {'name': 'Focused key / cream-coral-plum v2', 'vectors': VECTORS, 'scalars': SCALARS,
            'why': WHY, 'criteria': CRITERIA, 'validation': 'Prepared offline; native A/B pending.',
            'source_evidence': [
                'UE5.8 Engine/Classes/Engine/Scene.h:1512-1584 reflected grading fields and override flags',
                'UE5.8 Engine/Shaders/Private/PostProcessCombineLUTs.usf:103-128 region/global multiplication and luminance masks'],
            'scope': 'Camera-local PP only, no exposure/lens/GI/project/scene changes.'}


def require(ok, message):
    if not ok:
        raise RuntimeError(message)


def apply(*, output='E:/CodexWork/MessControlMenuAnimation_20261022/CameraGradeApplyV2.json'):
    import unreal as u
    out = Path(output).resolve()
    require(out.drive.upper() == 'E:' and out.suffix == '.json', 'Choose E: JSON report')
    out.parent.mkdir(parents=True, exist_ok=True)
    require(Path(u.Paths.project_dir()).resolve() == ROOT, 'Wrong project')
    require(not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor(), 'Preserve active PIE')
    world = u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
    require(world.get_path_name() == MAP + '.L_MenuAnatomyV2', 'Open owned preview; this script does not travel')
    dirty = sorted(p.get_path_name() for p in list(u.EditorLoadingAndSavingUtils.get_dirty_content_packages()) + list(u.EditorLoadingAndSavingUtils.get_dirty_map_packages()))
    require(all(name == MAP for name in dirty), 'Preserve unrelated dirty packages: ' + ', '.join(dirty))
    cameras = [a for a in u.get_editor_subsystem(u.EditorActorSubsystem).get_all_level_actors()
               if 'MC_MenuV2_Camera' in [str(t) for t in a.tags]]
    require(len(cameras) == 1 and isinstance(cameras[0], u.CameraActor), 'Expected one owned menu camera')
    camera, component = cameras[0], cameras[0].camera_component
    require(float(component.get_editor_property('post_process_blend_weight')) > .99, 'Camera PP blend is not full; review stacking first')
    fields = {**{key: u.Vector4(*values) for key, values in VECTORS.items()}, **SCALARS}
    # Validate all reflected names on an independent struct before mutation.
    probe = u.PostProcessSettings()
    for key, value in fields.items():
        probe.set_editor_property('override_' + key, True)
        probe.set_editor_property(key, value)
    pp = component.get_editor_property('post_process_settings')
    def read(settings, key):
        value = settings.get_editor_property(key)
        return [value.x, value.y, value.z, value.w] if key in VECTORS else float(value)
    before = {key: {'override': bool(pp.get_editor_property('override_' + key)), 'value': read(pp, key)} for key in fields}
    protected_names = ['auto_exposure_method', 'auto_exposure_bias', 'dynamic_global_illumination_method']
    protected = {key: str(pp.get_editor_property(key)) for key in protected_names}
    pose, fov = str(camera.get_actor_transform()), float(component.get_editor_property('field_of_view'))
    out.write_text(json.dumps({**candidate(), 'status': 'before_apply', 'camera': camera.get_path_name(),
                              'before': before, 'protected': protected}, indent=2), encoding='utf8')
    component.modify()
    for key, value in fields.items():
        pp.set_editor_property('override_' + key, True)
        pp.set_editor_property(key, value)
    component.set_editor_property('post_process_settings', pp)
    result = component.get_editor_property('post_process_settings')
    require(protected == {key: str(result.get_editor_property(key)) for key in protected_names}, 'Protected exposure/GI changed')
    require(pose == str(camera.get_actor_transform()) and abs(fov - float(component.get_editor_property('field_of_view'))) < 1e-6, 'Camera pose/FOV changed')
    after = {key: {'override': bool(result.get_editor_property('override_' + key)), 'value': read(result, key)} for key in fields}
    report = {**candidate(), 'camera': camera.get_path_name(), 'dirty_before': dirty, 'before': before,
              'after': after, 'protected_unchanged': protected, 'saved': False,
              'validation': 'Reflected names preflight and property readback only. Root must capture native A/B and save owned map after review.'}
    out.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf8')
    u.log('MC_CAMERA_GRADE_V2_PENDING_VISUAL_REVIEW ' + str(out))
    return report
