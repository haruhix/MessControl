"""Add stack poses to existing DT_BreakfastMenu rows, preserving their data.

Run inside Unreal after building FMCFoodRow::Stack. Only the table is saved;
meshes, mesh scales and authored collision are untouched.
"""
import copy
import json
import math
import unreal as u

path = '/Game/Data/DT_BreakfastMenu'
table = u.EditorAssetLibrary.load_asset(path)
if not table:
    raise RuntimeError('Required existing breakfast menu is missing: ' + path)

def export_rows():
    result = u.DataTableFunctionLibrary.export_data_table_to_json_string(table)
    if isinstance(result, tuple):
        result = next((item for item in result if isinstance(item, str)), None)
    if not result:
        raise RuntimeError('Could not export existing breakfast rows')
    return json.loads(result)

original = export_rows()
rows = copy.deepcopy(original)
for row in rows:
    stack = row.setdefault('Stack', {})
    for key, default in [('LayerGap', 3.0), ('HorizontalOffset', 2.5), ('YawVariation', 8.0), ('MaxSwayDegrees', 8.0)]:
        stack.setdefault(key, default)
    overrides = stack.setdefault('PoseOverrides', [])
    known = {entry['Mesh'] for entry in overrides}
    # Explicit face normals for carrot discs in this row. Other ingredients
    # automatically lay on their shortest scaled axis, including future meshes.
    for mesh_path in row.get('FragmentMeshes', []):
        if not mesh_path or mesh_path == 'None' or 'SM_CarrotSlice' not in mesh_path or mesh_path in known:
            continue
        mesh = u.EditorAssetLibrary.load_asset(mesh_path)
        if not mesh:
            raise RuntimeError('Saved carrot fragment is missing: ' + mesh_path)
        box = mesh.get_bounding_box()
        extent = box.max - box.min
        scale = row['FragmentScale']
        values = (extent.x * abs(scale['X']), extent.y * abs(scale['Y']), extent.z * abs(scale['Z']))
        axis = 2 if values[2] <= min(values[:2]) + 0.0001 else (0 if values[0] <= values[1] else 1)
        overrides.append(dict(Mesh=mesh.get_path_name(), VerticalAxis=('X', 'Y', 'Z')[axis]))
        known.add(mesh_path)
        u.log('MC_STACK_TABLE_POSE row=%s mesh=%s axis=%s' % (row['Name'], mesh_path, ('X', 'Y', 'Z')[axis]))
    for key, limit in [('LayerGap', 12), ('HorizontalOffset', 8), ('YawVariation', 20), ('MaxSwayDegrees', 12)]:
        if not math.isfinite(stack[key]) or not 0 <= stack[key] <= limit:
            raise RuntimeError('Invalid stack setting %s.%s' % (row['Name'], key))
    if len({entry['Mesh'] for entry in overrides}) != len(overrides):
        raise RuntimeError('Duplicate stack pose mesh in row ' + row['Name'])

if not u.DataTableFunctionLibrary.fill_data_table_from_json_string(table, json.dumps(rows, ensure_ascii=False)):
    raise RuntimeError('Breakfast stack import failed')

def without_stack(items):
    return {row['Name']: {key: value for key, value in row.items() if key != 'Stack'} for row in items}

exported = export_rows()
if without_stack(exported) != without_stack(original):
    raise RuntimeError('Import changed existing menu data; refusing to save')
if not u.EditorAssetLibrary.save_loaded_asset(table, only_if_is_dirty=False):
    raise RuntimeError('Could not save ' + path)
u.log('MC_BREAKFAST_STACK_TABLE_PASS rows=%d old_fields_preserved=1' % len(rows))
