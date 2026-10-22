"""Read-only chest model/default/material evidence; run in the owning editor.

Does not load a map, start PIE, spawn actors or save packages. Generated evidence
is written to E:, including editor state so the owner can preserve open work.
"""
import json
from pathlib import Path
import unreal as u

OUT = Path(globals().get('MC_CHEST_INSPECTION_OUTPUT',
    'E:/CodexWork/HubChestWardrobe_20261022/ChestInspection')).resolve()
assert OUT.drive.upper() == 'E:'
OUT.mkdir(parents=True, exist_ok=True)

def path(obj):
    return obj.get_path_name() if obj else None

def vec(v):
    return [v.x, v.y, v.z]

def mesh_info(mesh):
    bounds = mesh.get_bounding_box()
    return dict(path=path(mesh), min=vec(bounds.min), max=vec(bounds.max),
        materials=[path(mesh.get_material(i)) for i in range(mesh.get_num_sections(0))])

def chest_info(obj):
    comps = []
    for comp in obj.get_components_by_class(u.StaticMeshComponent):
        comps.append(dict(name=comp.get_name(), mesh=path(comp.static_mesh),
            materials=[path(comp.get_material(i)) for i in range(comp.get_num_materials())],
            relative_location=vec(comp.relative_location), relative_scale=vec(comp.relative_scale3d)))
    return dict(path=path(obj), components=comps,
        model_scale=obj.get_editor_property('model_scale'),
        lid_seat_offset=obj.get_editor_property('lid_seat_offset'))

report = dict(dirty_maps=[path(p) for p in u.EditorLoadingAndSavingUtils.get_dirty_map_packages()],
    dirty_content=[path(p) for p in u.EditorLoadingAndSavingUtils.get_dirty_content_packages()],
    native=chest_info(u.get_default_object(u.MCRewardChest)), meshes=[], materials=[], blueprint=None)
for asset in ['/Game/FromBlender/SM_Chest_Bot', '/Game/FromBlender/SM_Chest_Top',
        '/Game/FromBlender6/SM_Chest_Bot', '/Game/FromBlender6/SM_Chest_Top']:
    mesh = u.load_asset(asset)
    if isinstance(mesh, u.StaticMesh):
        report['meshes'].append(mesh_info(mesh))
bp = u.load_asset('/Game/Gameplay/Roguelike/BP_RewardChest')
if bp:
    report['blueprint'] = chest_info(u.get_default_object(bp.generated_class()))
for asset in ['/Game/Art/Materials/Buster/MI_Chest',
        '/Game/Gameplay/VFX/RewardChest/M_RewardChest_FloorFast',
        '/Game/Gameplay/VFX/RewardChest/M_RewardChest_MagicSparks',
        '/Game/Gameplay/VFX/RewardChest/M_RewardChest_SparksFast']:
    material = u.load_asset(asset)
    if material:
        report['materials'].append(dict(path=path(material),
            scalar_parameters=[str(p) for p in u.MaterialEditingLibrary.get_scalar_parameter_names(material)],
            vector_parameters=[str(p) for p in u.MaterialEditingLibrary.get_vector_parameter_names(material)]))
(OUT / 'Inspection.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
u.log('MC_CHEST_INSPECTION ' + str(OUT / 'Inspection.json'))
