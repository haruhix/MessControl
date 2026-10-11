"""Apply only the player's short combat flash, preserving authored grime/fractures."""
import unreal as u

assert not u.EditorLevelLibrary.get_pie_worlds(False), 'Stop PIE before material authoring.'
path = '/Game/Art/Materials/M_TeethGameplay'
material = u.EditorAssetLibrary.load_asset(path)
assert material, 'Missing player enamel material'
node = next(n for n in u.MaterialEditingLibrary.get_material_expressions(material)
            if n.get_name() == 'MaterialExpressionCustom_0')
code = node.get_editor_property('code')
old = 'return lerp(color,float3(1,.3,.08),saturate(Flash)*.55);'
new = ('float hit=saturate(Flash);\n'
       'float3 hitColor=lerp(float3(1,.30,.08),float3(1,.96,.80),smoothstep(.55,.85,hit));\n'
       'return lerp(color,hitColor,hit*.90);')
assert old in code or new in code, 'Unexpected shader; refusing to replace authored code'
if old in code:
    node.set_editor_property('code', code.replace(old, new))
    u.MaterialEditingLibrary.recompile_material(material)
    assert u.EditorAssetLibrary.save_loaded_asset(material), 'Material save failed'
print('COMBAT_HIT_FLASH_READY '+path)
