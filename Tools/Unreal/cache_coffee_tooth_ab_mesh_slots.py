"""ROOT-RUN, idle Editor only: read actual tooth LOD0 material-slot maps.

StaticMeshEditorSubsystem.GetLODMaterialSlot refuses PIE. This read-only cache
therefore runs BEFORE the owned tooth A/B PIE. It never guesses section==slot,
changes assets, compiles meshes, changes CPU access, or saves packages.
Saved/CoffeeArchitectureAB/ToothMeshSlots.json is derived, SHA-guarded evidence.
"""
import datetime
import hashlib
import json
from pathlib import Path
import unreal as u

ROOT = Path(u.Paths.project_dir()).resolve()
OUTPUT = ROOT/'Saved/CoffeeArchitectureAB/ToothMeshSlots.json'


def dirty_packages():
    return dict(content=sorted(p.get_path_name() for p in u.EditorLoadingAndSavingUtils.get_dirty_content_packages()),
                maps=sorted(p.get_path_name() for p in u.EditorLoadingAndSavingUtils.get_dirty_map_packages()))


def package_hashes(mesh_path):
    package = mesh_path.split('.', 1)[0]
    if not package.startswith('/Game/'):
        raise RuntimeError('Expected project tooth mesh: '+mesh_path)
    stem = ROOT/'Content'/package.removeprefix('/Game/')
    output = {}
    for suffix in ('.uasset', '.uexp', '.ubulk'):
        filename = stem.with_suffix(suffix)
        if filename.is_file():
            output[str(filename.relative_to(ROOT)).replace('\\', '/')] = hashlib.sha256(filename.read_bytes()).hexdigest()
    if not any(p.endswith('.uasset') for p in output):
        raise RuntimeError('Tooth mesh lacks saved uasset: '+mesh_path)
    return output


def build_cache():
    if u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor() or u.EditorLevelLibrary.get_pie_worlds(True):
        raise RuntimeError('Read section maps in the idle Editor BEFORE PIE')
    before = dirty_packages()
    subsystem = u.get_editor_subsystem(u.StaticMeshEditorSubsystem)
    candidates = set()
    for directory in ('/Game/Gameplay/Arena/V10', '/Game/Gameplay/Arena/Damage'):
        candidates.update(str(p) for p in u.EditorAssetLibrary.list_assets(directory, recursive=True, include_folder=False))
    candidates.add('/Game/Art/Meshes/SM_ToothProp')
    meshes = {}
    for candidate in sorted(candidates):
        mesh = u.load_asset(candidate)
        if not isinstance(mesh, u.StaticMesh):
            continue
        mesh_path = mesh.get_path_name()
        package = mesh_path.split('.', 1)[0]
        if package in before['content']:
            raise RuntimeError('Preserve unsaved tooth mesh edits; cannot cache saved generation: '+mesh_path)
        hashes = package_hashes(mesh_path)
        slots = list(mesh.get_editor_property('static_materials'))
        sections = []
        for section in range(mesh.get_num_sections(0)):
            slot = int(subsystem.get_lod_material_slot(mesh, 0, section))
            if not 0 <= slot < len(slots):
                raise RuntimeError('Invalid actual section map: '+mesh_path+' section '+str(section))
            sections.append(dict(section=section, material_slot=slot, slot_name=str(slots[slot].material_slot_name)))
        if not sections:
            raise RuntimeError('No actual LOD0 sections: '+mesh_path)
        if package_hashes(mesh_path) != hashes:
            raise RuntimeError('Mesh package changed while reading section map: '+mesh_path)
        meshes[mesh_path] = dict(mesh_path=mesh_path, lod=0, sections=sections,
            slots=[dict(index=i, name=str(row.material_slot_name),
                        material=row.material_interface.get_path_name() if row.material_interface else None)
                   for i, row in enumerate(slots)], package_sha256=hashes)
    after = dirty_packages()
    if not meshes or before != after:
        raise RuntimeError('Cache must preserve user dirty packages and contain actual meshes')
    report = dict(schema_version=1, generated_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
        project_root=str(ROOT), source='Idle Editor StaticMeshEditorSubsystem.GetLODMaterialSlot; no section-index guessing',
        dirty_packages_before=before, dirty_packages_after=after, mesh_count=len(meshes), meshes=meshes, passed=True)
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT.write_text(json.dumps(report, indent=2), encoding='utf-8')
    print('COFFEE_TOOTH_SLOT_CACHE_READY', len(meshes), str(OUTPUT))
    return report


if __name__ == '__main__':
    build_cache()
