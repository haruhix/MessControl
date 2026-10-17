"""Connect the new artist uvula and exact gum collision after the native build.

Default is read-only. Apply via runpy with MC_MANUAL_INTAKE_APPLY='manual_intake_v1'.
Only L_Mouth and owned /Game/Gameplay/Arena/ManualIntake derivatives are saved.
"""
import hashlib
import json
import shutil
from datetime import datetime, timezone
from pathlib import Path
import unreal as u

REVISION = 'manual_intake_v1'
APPLY = globals().get('MC_MANUAL_INTAKE_APPLY') == REVISION
FOLDER = '/Game/Gameplay/Arena/ManualIntake'
ROOT = Path(u.Paths.project_dir()).resolve()
OUT = Path(u.Paths.project_saved_dir()) / 'ManualIntake'
OUT.mkdir(parents=True, exist_ok=True)
actors = u.get_editor_subsystem(u.EditorActorSubsystem)
levels = u.get_editor_subsystem(u.LevelEditorSubsystem)
lib = u.EditorAssetLibrary
mesh_editor = u.get_editor_subsystem(u.StaticMeshEditorSubsystem)
world = u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
assert world and world.get_path_name() == '/Game/Maps/L_Mouth.L_Mouth', 'Open the authored arena first'
assert not u.EditorLevelLibrary.get_pie_worlds(False), 'Preserve active gameplay'
all_actors = actors.get_all_level_actors()
throats = [a for a in all_actors if isinstance(a, u.MCThroat)]
assert len(throats) == 1, 'Inspect missing or duplicate native throats'
throat = throats[0]
gums = [a for a in all_actors if isinstance(a, u.StaticMeshActor) and a.static_mesh_component.static_mesh
        and a.static_mesh_component.static_mesh.get_name() in ('SM_Gum_Bot', 'SM_Gum_Up_001', 'SM_Gum_Bot_Contact', 'SM_Gum_Up_001_Contact')]
assert len(gums) == 2, 'Expected the two current artist gum surfaces'
uvulas = [a for a in all_actors if isinstance(a, u.StaticMeshActor) and a.static_mesh_component.static_mesh
          and a.static_mesh_component.static_mesh.get_name() == 'SM_MiniTong']
existing_uvula = throat.get_editor_property('uvula')
already_integrated = existing_uvula.static_mesh and existing_uvula.static_mesh.get_path_name().startswith(FOLDER + '/SM_InteractiveUvula.')
assert len(uvulas) == 1 or already_integrated and not uvulas, 'Inspect duplicate/new artist uvula'
source_uvula = uvulas[0] if uvulas else None
dirty = [p.get_path_name() for p in u.EditorLoadingAndSavingUtils.get_dirty_content_packages()]
dirty += [p.get_path_name() for p in u.EditorLoadingAndSavingUtils.get_dirty_map_packages()]
report = dict(revision=REVISION, apply=APPLY, world=world.get_path_name(), dirty_before=dirty,
              throat=throat.get_path_name(), gums=[], collision_build=[], uvula_source=source_uvula.get_path_name() if source_uvula else None)
for actor in gums:
    comp = actor.static_mesh_component
    report['gums'].append(dict(actor=actor.get_path_name(), source=comp.static_mesh.get_path_name(),
                              transform=str(comp.get_world_transform()), materials=[comp.get_material(i).get_path_name() if comp.get_material(i) else None for i in range(comp.get_num_materials())]))
if not APPLY:
    (OUT / 'AuthoringPlan.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    u.log('MC_MANUAL_INTAKE_PLAN ' + str(OUT / 'AuthoringPlan.json'))
else:
    assert not dirty, 'Save/review unrelated editor edits before this bounded authoring step'
    # New native fields must be present; never silently author against the old DLL.
    throat.get_editor_property('manual_intake')
    throat.get_editor_property('authored_uvula_rest_transform')
    backup = OUT / 'Backups' / datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    backup.mkdir(parents=True, exist_ok=False)
    source_packages = {'/Game/Maps/L_Mouth'}
    source_packages.update(a.static_mesh_component.static_mesh.get_path_name().split('.')[0] for a in gums)
    if source_uvula:
        source_packages.add(source_uvula.static_mesh_component.static_mesh.get_path_name().split('.')[0])
    report['backups'] = []
    for package in sorted(source_packages):
        suffix = '.umap' if package == '/Game/Maps/L_Mouth' else '.uasset'
        disk = ROOT / 'Content' / (package[len('/Game/'):] + suffix)
        assert disk.is_file(), str(disk)
        target = backup / disk.relative_to(ROOT / 'Content')
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(disk, target)
        report['backups'].append(dict(package=package, path=str(target), sha256=hashlib.sha256(disk.read_bytes()).hexdigest()))
    lib.make_directory(FOLDER)

    def contact_mesh(source, name):
        target = FOLDER + '/' + name
        if lib.does_asset_exist(target):
            mesh = lib.load_asset(target)
            assert lib.get_metadata_tag(mesh, 'MCManualIntakeRevision') == REVISION, 'Unowned destination ' + target
        else:
            mesh = lib.duplicate_asset(source.get_path_name(), target)
            assert mesh, target
        body = mesh.get_editor_property('body_setup')
        assert body, target
        # A single convex hull would close the curved gum arch and the throat.
        # Exact authored triangles provide static contact without changing art.
        body.set_editor_property('collision_trace_flag', u.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
        body.set_editor_property('double_sided_geometry', True)
        # Editing BodySetup flags alone does not invalidate an already cooked
        # triangle mesh in UE5.8. EnableSectionCollision explicitly calls the
        # owning UStaticMesh::PostEditChange -> Build -> InvalidatePhysicsData /
        # CreatePhysicsMeshes, even when that section was already enabled.
        sections = mesh.get_num_sections(0)
        assert sections > 0, target
        for section in range(sections):
            mesh_editor.enable_section_collision(mesh, True, 0, section)
            assert mesh_editor.is_section_collision_enabled(mesh, 0, section), target
        lib.set_metadata_tag(mesh, 'MCManualIntakeRevision', REVISION)
        # On a reviewed retry the actor already points at our derivative. Keep
        # the original art provenance rather than replacing it with a self-link.
        if source.get_path_name() != mesh.get_path_name():
            lib.set_metadata_tag(mesh, 'MCManualIntakeSource', source.get_path_name())
        assert lib.save_loaded_asset(mesh, only_if_is_dirty=False), target
        report['collision_build'].append(dict(mesh=target, sections=sections,
            method='StaticMeshEditorSubsystem.EnableSectionCollision -> StaticMesh.PostEditChange -> Build/Chaos recook',
            trace_flag=str(body.get_editor_property('collision_trace_flag'))))
        return mesh

    with u.ScopedEditorTransaction('Manual throat intake and new interactive uvula'):
        for row, actor in zip(report['gums'], gums):
            comp = actor.static_mesh_component
            source = comp.static_mesh
            name = source.get_name().removesuffix('_Contact') + '_Contact'
            mesh = contact_mesh(source, name)
            materials = [comp.get_material(i) for i in range(comp.get_num_materials())]
            comp.set_static_mesh(mesh)
            for i, mat in enumerate(materials):
                comp.set_material(i, mat)
            comp.set_editor_property('use_default_collision', False)
            comp.set_collision_profile_name('BlockAll')
            comp.set_collision_enabled(u.CollisionEnabled.QUERY_AND_PHYSICS)
            comp.set_collision_response_to_channel(u.CollisionChannel.ECC_CAMERA, u.CollisionResponseType.ECR_IGNORE)
            actor.set_actor_enable_collision(True)
            row['derived'] = mesh.get_path_name()
        if source_uvula:
            comp = source_uvula.static_mesh_component
            rest = u.MathLibrary.make_relative_transform(comp.get_world_transform(), throat.get_actor_transform())
            mesh = contact_mesh(comp.static_mesh, 'SM_InteractiveUvula')
            existing_uvula.set_static_mesh(mesh)
            existing_uvula.set_editor_property('override_materials', [comp.get_material(i) for i in range(comp.get_num_materials())])
            throat.set_editor_property('authored_uvula_rest_transform', rest)
            # Remove the redundant level decoration after the native actor owns its exact art.
            actors.destroy_actor(source_uvula)
        throat.set_editor_property('manual_intake', True)
        throat.set_editor_property('enable_legacy_delivery_zone', False)
        throat.set_editor_property('use_authored_uvula', True)
        throat.set_editor_property('intake_depth', 45.0)
        throat.call_method('RebuildAppearance')
        for actor in actors.get_all_level_actors():
            if isinstance(actor, u.MCFoodDisposal):
                actor.set_editor_property('enable_legacy_delivery_zone', False)
                actor.get_editor_property('label').set_visibility(False)
                actor.get_editor_property('label').set_hidden_in_game(True)
        assert levels.save_current_level(), 'Could not save L_Mouth'
    report['uvula_result'] = dict(mesh=existing_uvula.static_mesh.get_path_name(),
                                  transform=str(existing_uvula.get_world_transform()), collision=str(existing_uvula.get_collision_enabled()))
    report['open_amount'] = throat.open_amount()
    report['protected_source_assets_unchanged'] = []
    for entry in report['backups']:
        # Owned derivatives are intentionally recooked on a retry. Back them up
        # too, but the unchanged-source gate applies only to the artist assets.
        if entry['package'] == '/Game/Maps/L_Mouth' or entry['package'].startswith(FOLDER+'/'):
            continue
        original = ROOT / 'Content' / (entry['package'][len('/Game/'):] + '.uasset')
        unchanged = hashlib.sha256(original.read_bytes()).hexdigest() == entry['sha256']
        report['protected_source_assets_unchanged'].append(dict(package=entry['package'], unchanged=unchanged))
        assert unchanged, 'Source asset changed outside the owned derivative: '+entry['package']
    report['dirty_after'] = [p.get_path_name() for p in u.EditorLoadingAndSavingUtils.get_dirty_content_packages()]
    report['dirty_after'] += [p.get_path_name() for p in u.EditorLoadingAndSavingUtils.get_dirty_map_packages()]
    (OUT / 'AuthoringReport.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    u.log('MC_MANUAL_INTAKE_AUTHORED ' + str(OUT / 'AuthoringReport.json'))
