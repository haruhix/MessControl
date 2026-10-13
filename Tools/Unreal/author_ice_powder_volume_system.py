"""Duplicate only the phase-front powder emitter; preserve shared wind assets.

Run through remote_python.py with the MessControl editor idle. The standard
Niagara MCP stack exposes no modules/renderers for this lightweight emitter,
so its reflected stateless properties are authored through editor Python.
This script refuses to overwrite an existing target package.
"""
import hashlib
import json
from pathlib import Path
import unreal as u

FOLDER = '/Game/Gameplay/Cold/VFX'
SOURCE = FOLDER + '/NS_IceWindWisps'
TARGET = FOLDER + '/NS_IcePhasePowder'
MATERIAL = FOLDER + '/M_IcePhasePowder'


def main():
    assert str(u.Paths.get_project_file_path()).lower().endswith('messcontrol.uproject')
    level = u.get_editor_subsystem(u.LevelEditorSubsystem)
    assert not level.is_in_play_in_editor(), 'Preserve active PIE'
    lib = u.EditorAssetLibrary
    target_disk = Path(u.Paths.project_content_dir()) / 'Gameplay/Cold/VFX/NS_IcePhasePowder.uasset'
    assert not target_disk.exists() and not lib.does_asset_exist(TARGET), 'Target already exists; do not overwrite'
    source_disk = Path(u.Paths.project_content_dir()) / 'Gameplay/Cold/VFX/NS_IceWindWisps.uasset'
    source_hash = hashlib.sha256(source_disk.read_bytes()).hexdigest()
    dirty_before = {p.get_name() for p in u.EditorLoadingAndSavingUtils.get_dirty_content_packages()}
    assert SOURCE not in dirty_before and TARGET not in dirty_before, 'Preserve unsaved source/target changes'
    source = u.load_asset(SOURCE)
    material = u.load_asset(MATERIAL)
    assert source and material, 'Owned powder material and source system are required'

    def emitter_for(system):
        stateless_class = u.load_class(None, '/Script/Niagara.NiagaraStatelessEmitter')
        emitters = [obj for obj in u.ObjectIterator(stateless_class) if obj.get_outer() == system]
        assert len(emitters) == 1, 'Expected one direct lightweight emitter'
        return emitters[0]

    def distribution(obj, prop, text):
        value = obj.get_editor_property(prop).copy()
        assert value.import_text(text), (prop, text)
        obj.set_editor_property(prop, value)

    def float_range(obj, prop, low, high):
        distribution(obj, prop,
            '(Mode=UniformRange,Min=%s,Max=%s,ChannelConstantsAndRanges=(%s,%s),ChannelCurves=)'
            % (low, high, low, high))

    with u.ScopedEditorTransaction('Author phase-front powder volume emitter'):
        system = lib.duplicate_asset(SOURCE, TARGET)
        assert system, 'Could not duplicate source system'
        emitter = emitter_for(system)
        modules = {m.get_class().get_name().removeprefix('NiagaraStatelessModule_'): m
            for m in emitter.get_editor_property('Modules')}
        init = modules['InitializeParticle']
        float_range(init, 'LifetimeDistribution', .90, 1.35)
        float_range(init, 'SpriteRotationDistribution', -180, 180)
        # ParticleSize, ParticleColor and SpeedScale retain their User bindings.
        shape = modules['ShapeLocation']
        shape.set_editor_property('bBoxSurfaceOnly', False)
        distribution(shape, 'BoxSize',
            '(Mode=NonUniformConstant,Min=(X=80,Y=170,Z=140),Max=(X=80,Y=170,Z=140),'
            'ChannelConstantsAndRanges=(80,170,140),ChannelCurves=)')
        velocity = modules['AddVelocity']
        # Native radial-facing components provide yaw; the volume rises above
        # the ground instead of preserving the previous nearly flat fan.
        distribution(velocity, 'LinearVelocityDistribution',
            '(Mode=NonUniformRange,Min=(X=1700,Y=-1050,Z=120),Max=(X=2100,Y=1050,Z=500),'
            'ChannelConstantsAndRanges=(1700,-1050,120,2100,1050,500),ChannelCurves=)')
        assert 'User.SpeedScale' in velocity.get_editor_property('LinearVelocityScale').export_text()
        curl = modules.get('CurlNoiseForce')
        if curl:
            curl.set_editor_properties({'bModuleEnabled': True, 'NoiseStrength': 35.0, 'NoiseFrequency': .008})
        spin = modules.get('SpriteRotationRate')
        if spin:
            spin.set_editor_properties({'bModuleEnabled': True, 'bUseRateScale': False})
            float_range(spin, 'RotationRateDistribution', -22, 22)
        # The existing DynamicMaterialParameter.x curve is normalized age and
        # must remain intact; the powder material uses it for erosion and fade.
        age = modules['DynamicMaterialParameters']
        assert age.get_editor_property('bParameter0Enabled')
        age_parameter = age.get_editor_property('Parameter0')
        assert age_parameter.get_editor_property('bXChannelEnabled')
        assert 'UniformCurve' in age_parameter.get_editor_property('XChannelDistribution').export_text()
        renderers = list(emitter.get_editor_property('RendererProperties'))
        assert len(renderers) == 1 and isinstance(renderers[0], u.NiagaraSpriteRendererProperties)
        renderer = renderers[0]
        renderer.set_editor_properties({'Alignment': u.NiagaraSpriteAlignment.UNALIGNED,
            'FacingMode': u.NiagaraSpriteFacingMode.FACE_CAMERA_POSITION, 'Material': material})
        emitter.set_editor_property('RendererProperties', renderers)
        bounds = u.Box(min=u.Vector(-3400, -1700, -600), max=u.Vector(3400, 1700, 1600))
        emitter.set_editor_property('FixedBounds', bounds)
        system.set_editor_property('fixed_bounds', bounds)
        lib.set_metadata_tag(system, 'MC.IcePowder.Authoring', 'volume-1')
        lib.set_metadata_tag(system, 'MC.IcePowder.Source', SOURCE)
        assert lib.save_loaded_asset(system, only_if_is_dirty=False), 'Save failed'

    assert hashlib.sha256(source_disk.read_bytes()).hexdigest() == source_hash, 'Shared source package changed'
    report = {'system': system.get_path_name(), 'source': SOURCE, 'source_sha256': source_hash,
        'source_unchanged': True, 'new_package_saved': True,
        'alignment': 'Unaligned', 'facing': 'FaceCameraPosition', 'lifetime': [.90, 1.35],
        'spawn_box_cm': [80, 170, 140], 'local_velocity_min': [1700, -1050, 120],
        'local_velocity_max': [2100, 1050, 500], 'speed_binding': 'User.SpeedScale',
        'rotation_degrees': [-180, 180], 'spin_degrees_per_second': [-22, 22],
        'curl_strength': 35, 'curl_frequency': .008, 'material_age': 'DynamicMaterialParameter.x',
        'material': MATERIAL, 'compile_validation': 'Query Niagara MCP after authoring',
        'default_parameters': 'Retained; native component overrides provide layer size/rate/material',
        'dirty_before': sorted(dirty_before),
        'dirty_after': sorted(p.get_name() for p in u.EditorLoadingAndSavingUtils.get_dirty_content_packages())}
    report_path = Path(u.Paths.project_saved_dir()) / 'Checks/IcePowderVolume_20261013/AuthorSystem.json'
    report_path.parent.mkdir(parents=True, exist_ok=True)
    report_path.write_text(json.dumps(report, indent=2), encoding='utf-8')
    u.log('ICE_POWDER_VOLUME_READY ' + json.dumps(report))


main()
