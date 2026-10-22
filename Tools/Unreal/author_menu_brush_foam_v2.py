"""Menu-only small contact foam; proposed authoring script, caller executes.

No C++ build, live/editor automation or shared asset mutation is performed by
writing this file. Run its main() in an idle owned Unreal Editor after review.
It duplicates NS_BrushFoam into the menu namespace, edits reflected stateless
distributions and saves/reloads ONLY that new package. No actors/maps are edited.

The Python pattern already exists in author_ice_powder_volume_system.py and
build_stylized_spray.py. UNiagaraSystem::RequestCompile is NOT a UFUNCTION;
this script does not invent a request_compile()/wait_for_compilation() method.
"""
import hashlib
import json
from pathlib import Path
import traceback

import unreal as u


SOURCE = '/Game/Gameplay/VFX/NS_BrushFoam'
TARGET = '/Game/UI/MenuShowcase/AnatomyV2/VFX/NS_MenuBrushFoam'
MATERIALS = {
    'foam': '/Game/Gameplay/Care/M_BrushFoam',
    'bubbles': '/Game/Gameplay/Care/M_BrushBubble',
}
PROFILES = {
    'foam': {'rate': 18.0, 'lifetime': [.18, .26], 'size_cm': [3.5, 5.5],
             'plane_cm': [3.8, 2.4], 'normal_offset_cm': .4,
             'velocity_cm_s': [.5, 2.0], 'cone_deg': 55.0, 'drag': .9},
    'bubbles': {'rate': 3.0, 'lifetime': [.65, .9], 'size_cm': [2.0, 3.6],
                'plane_cm': [3.0, 2.0], 'normal_offset_cm': 1.0,
                'velocity_cm_s': [4.0, 8.0], 'cone_deg': 12.0, 'drag': .1},
}
REPORT = Path('E:/CodexWork/MessControlMenuAnimation_20261022/VFX/MenuBrushFoamAuthoring.json')


def _disk(asset):
    if not asset.startswith('/Game/'):
        raise ValueError('Only project assets are supported.')
    return Path(u.Paths.project_content_dir()) / (asset[6:] + '.uasset')


def _sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def _emitters(system):
    cls = u.load_class(None, '/Script/Niagara.NiagaraStatelessEmitter')
    if cls is None:
        raise RuntimeError('Niagara stateless emitter reflected class is unavailable.')
    return [obj for obj in u.ObjectIterator(cls) if obj.get_outer() == system]


def _modules(emitter):
    return {module.get_class().get_name().removeprefix('NiagaraStatelessModule_'): module
            for module in emitter.get_editor_property('Modules')}


def _role(emitter):
    renderers = list(emitter.get_editor_property('RendererProperties'))
    if len(renderers) != 1 or not isinstance(renderers[0], u.NiagaraSpriteRendererProperties):
        raise RuntimeError('Expected the existing single sprite renderer on each emitter.')
    material = renderers[0].get_editor_property('Material')
    path = material.get_path_name().split('.')[0] if material else None
    roles = [name for name, expected in MATERIALS.items() if path == expected]
    if len(roles) != 1:
        raise RuntimeError('Unexpected source renderer material: ' + str(path))
    return roles[0], renderers


def _dist(owner, property_name, text):
    value = owner.get_editor_property(property_name).copy()
    if not value.import_text(text):
        raise RuntimeError('Distribution import failed: ' + property_name + ' ' + text)
    owner.set_editor_property(property_name, value)


def _float_range(owner, property_name, low, high):
    mode = 'UniformConstant' if low == high else 'UniformRange'
    channels = str(low) if low == high else f'{low},{high}'
    _dist(owner, property_name,
          f'(Mode={mode},Min={low},Max={high},ChannelConstantsAndRanges=({channels}),ChannelCurves=)')


def _sprite_range(owner, low, high):
    _dist(owner, 'SpriteSizeDistribution',
          f'(Mode=UniformRange,Min=(X={low},Y={low}),Max=(X={high},Y={high}),'
          f'ChannelConstantsAndRanges=({low},{high}),ChannelCurves=)')


def _vec2(owner, property_name, x, y):
    _dist(owner, property_name,
          f'(Mode=NonUniformConstant,Min=(X={x},Y={y}),Max=(X={x},Y={y}),'
          f'ChannelConstantsAndRanges=({x},{y}),ChannelCurves=)')


def _vec3_range_constant(owner, property_name, x, y, z):
    _dist(owner, property_name,
          f'(Mode=NonUniformConstant,Min=(X={x},Y={y},Z={z}),Max=(X={x},Y={y},Z={z}),'
          f'ChannelConstantsAndRanges=({x},{y},{z}),ChannelCurves=)')


def _position(owner, x, y, z):
    # Position inherits FNiagaraDistributionVector3: Values, not Min/Max.
    _dist(owner, 'InitialPositionDistribution',
          f'(Mode=NonUniformConstant,Values=((X={x},Y={y},Z={z})),ValuesTimeRange=(X=0,Y=1),'
          f'ChannelConstantsAndRanges=({x},{y},{z}),ChannelCurves=)')


def _snapshot(system):
    rows = []
    for emitter in _emitters(system):
        role, renderers = _role(emitter)
        modules = _modules(emitter)
        properties = {}
        for module_name, names in {
            'InitializeParticle': ('LifetimeDistribution', 'SpriteSizeDistribution', 'InitialPositionDistribution'),
            'AddVelocity': ('ConeVelocityDistribution', 'ConeDirection'),
            'ShapeLocation': ('PlaneSize',),
        }.items():
            module = modules[module_name]
            properties[module_name] = {name: module.get_editor_property(name).export_text() for name in names}
        rows.append({'role': role, 'emitter_path': emitter.get_path_name(),
                     'properties': properties,
                     'spawn_infos': [spawn.export_text() for spawn in emitter.get_editor_property('SpawnInfos')],
                     'material': renderers[0].get_editor_property('Material').get_path_name()})
    return sorted(rows, key=lambda row: row['role'])


def main():
    if not str(u.Paths.get_project_file_path()).lower().endswith('messcontrol.uproject'):
        raise RuntimeError('Expected the MessControl project.')
    if u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor():
        raise RuntimeError('Preserve active PIE; execute this authoring utility while idle.')
    lib = u.EditorAssetLibrary
    resume=bool(globals().get('MC_MENU_FOAM_RESUME_CREATED_TARGET',False))
    if (lib.does_asset_exist(TARGET) or _disk(TARGET).exists()) and not resume:
        raise RuntimeError('Target already exists; no automatic overwrite: ' + TARGET)
    dirty_before = {package.get_name() for package in u.EditorLoadingAndSavingUtils.get_dirty_content_packages()}
    protected = [SOURCE] + list(MATERIALS.values())
    if dirty_before.intersection(protected):
        raise RuntimeError('A shared source/material package has unsaved changes; preserve them.')
    hashes = {asset: _sha(_disk(asset)) for asset in protected}
    source = u.load_asset(SOURCE)
    if source is None:
        raise RuntimeError('Shared source system is unavailable.')
    before = _snapshot(source)
    if {row['role'] for row in before} != set(PROFILES) or len(before) != 2:
        raise RuntimeError('Expected the reviewed ContactFoam + FloatingBubbles pair; do not invent/remove emitters.')
    report = {'status': 'running', 'source': SOURCE, 'target': TARGET, 'source_snapshot': before,
              'protected_package_hashes_before': hashes, 'profiles': PROFILES,
              'existing_materials_preserved': True, 'actor_or_map_changes': False,
              'reference_library': 'Docs/VFXReferences.md; existing foam/bubble assets reused, no new effect/material technique.',
              'compile_api_audit': 'RequestCompile/WaitForCompilationComplete are not UFUNCTIONs. Reflected property notification plus reload rebuilds stateless data; compile/visual status is not claimed.',
              'dirty_before': sorted(dirty_before)}
    try:
        with u.ScopedEditorTransaction('Create small menu-only brush contact foam'):
            system = lib.load_asset(TARGET) if resume else lib.duplicate_asset(SOURCE, TARGET)
            if system is None:
                raise RuntimeError('Could not duplicate the source into the menu namespace.')
            emitters = _emitters(system)
            if len(emitters) != 2:
                raise RuntimeError('Duplicated stateless emitters were not found as direct children.')
            for emitter in emitters:
                role, renderers = _role(emitter)
                profile = PROFILES[role]
                modules = _modules(emitter)
                for module in modules.values():
                    if module.get_outer() != emitter:
                        raise RuntimeError('A cloned module has a shared/nonlocal outer; refusing to mutate it.')
                init, velocity, shape = (modules[name] for name in ('InitializeParticle', 'AddVelocity', 'ShapeLocation'))
                # Keep original module types/enums and curves; these values are
                # already the reviewed InCone / Plane setup from RefineBrushFoam.
                # UE's non-Blueprint ENSM enums cannot be Pythonized in 5.8.
                # Their copied modes are retained; no enum is assigned here.
                _float_range(init, 'LifetimeDistribution', *profile['lifetime'])
                _sprite_range(init, *profile['size_cm'])
                _position(init, 0, 0, profile['normal_offset_cm'])
                _float_range(init, 'SpriteRotationDistribution', -18, 18)
                _vec2(shape, 'PlaneSize', *profile['plane_cm'])
                shape.set_editor_property('bPlaneEdgesOnly', False)
                _float_range(velocity, 'ConeVelocityDistribution', *profile['velocity_cm_s'])
                _float_range(velocity, 'ConeVelocityScale', 1.0, 1.0)
                _vec3_range_constant(velocity, 'ConeDirection', 0, 0, 1)
                velocity.set_editor_property('ConeAngle', profile['cone_deg'])
                # Source already has Local contact velocity, World bubbles;
                # retain those enum values to avoid an unverified Python enum.
                if 'Drag' in modules:
                    _float_range(modules['Drag'], 'DragDistribution', profile['drag'], profile['drag'])
                spawns = [spawn.copy() for spawn in emitter.get_editor_property('SpawnInfos')]
                if not spawns:
                    raise RuntimeError('Expected existing spawn info; no unsupported add-spawn Python method is assumed.')
                for index, spawn in enumerate(spawns):
                    spawn.set_editor_property('bEnabled', index == 0)
                    spawn.set_editor_property('bLoopCountLimitEnabled', False)
                    spawn.set_editor_property('bSpawnProbabilityEnabled', False)
                    _float_range(spawn, 'Rate', profile['rate'], profile['rate'])
                # Struct-array edits must be assigned back to the emitter.
                emitter.set_editor_property('SpawnInfos', spawns)
                # Existing fade/age/size curves and both material references stay.
                # Reassign owned arrays to send emitter PostEditChangeProperty.
                emitter.set_editor_property('Modules', list(modules.values()))
                emitter.set_editor_property('RendererProperties', renderers)
            lib.set_metadata_tag(system, 'MC.MenuFoam.Source', SOURCE)
            lib.set_metadata_tag(system, 'MC.MenuFoam.Version', 'small-contact-v2')
            report['authored_snapshot'] = _snapshot(system)
            if not lib.save_loaded_asset(system, only_if_is_dirty=False):
                raise RuntimeError('Saving the new menu package failed.')
        # Newly created, saved, and not yet assigned to actors. Reload only this
        # package so PostLoad->CacheFromCompiledData rebuilds constants even when
        # no component exists to enter FNiagaraSystemUpdateContext's recache set.
        package = system.get_outer()
        reply = u.EditorLoadingAndSavingUtils.reload_packages(
            [package], interaction_mode=u.ReloadPackagesInteractionMode.ASSUME_POSITIVE)
        report['new_package_reload_reply'] = str(reply)
        if not isinstance(reply, tuple) or not reply[0]:
            raise RuntimeError('New package reload was not confirmed: ' + str(reply))
        system = u.load_asset(TARGET)
        if system is None:
            raise RuntimeError('Reloaded menu system is unavailable.')
        report['reloaded_snapshot'] = _snapshot(system)
        # Paths remain identical after package reload; all edited distributions
        # must roundtrip unchanged through the saved asset.
        if report['authored_snapshot'] != report['reloaded_snapshot']:
            raise RuntimeError('Edited stateless properties changed during save/reload.')
        report['protected_package_hashes_after'] = {asset: _sha(_disk(asset)) for asset in protected}
        if report['protected_package_hashes_after'] != hashes or _snapshot(source) != before:
            raise RuntimeError('Shared source or materials changed unexpectedly.')
        report.update(status='saved_reloaded_pending_root_visual_QA',
            new_package_sha256=_sha(_disk(TARGET)),
            steady_state_estimate={'contact_foam_mean_live': 18 * (.18 + .26) / 2,
                                   'floating_bubbles_mean_live': 3 * (.65 + .9) / 2,
                                   'interpretation': 'Rate*lifetime estimate, not a hard particle cap or GPU profile.'},
            placement_note='Place component at bristle contact and orient local +Z along enamel outward normal. Do not use actor scale as the sprite-size control.',
            validation='No C++ build; constants roundtripped. Shader compile status, moving contact, inspection pause and native camera appearance require root checks.',
            dirty_after=sorted(package.get_name() for package in u.EditorLoadingAndSavingUtils.get_dirty_content_packages()))
        return report
    except Exception as error:
        report.update(status='failed', error=str(error), traceback=traceback.format_exc())
        raise
    finally:
        REPORT.parent.mkdir(parents=True, exist_ok=True)
        REPORT.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf8')
        u.log('MC_MENU_FOAM_AUTHORING ' + json.dumps({'status': report['status'], 'report': str(REPORT)}))


if __name__ == '__main__':
    main()
