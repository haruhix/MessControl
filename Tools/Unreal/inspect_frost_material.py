"""Read the owned tongue-frost material graph before a narrow alpha patch."""
import json
import unreal as u

path = '/Game/Gameplay/IceEvent/M_TongueFrost'
asset = u.load_asset(path)
if not asset:
    candidates = [p for p in u.EditorAssetLibrary.list_assets('/Game', recursive=True)
                  if 'M_TongueFrost.' in p]
    assert len(candidates) == 1, candidates
    path = candidates[0]
    asset = u.load_asset(path)
e = u.MaterialEditingLibrary
nodes = list(e.get_material_expressions(asset))
dither = e.get_material_property_input_node(asset, u.MaterialProperty.MP_OPACITY_MASK)
u.log('MC_FROST_MATERIAL ' + json.dumps(dict(path=path, node_count=len(nodes),
    vertex_colors=[n.get_name() for n in nodes if isinstance(n, u.MaterialExpressionVertexColor)],
    opacity_node=dither.get_class().get_name(),
    opacity_inputs=[str(v) for v in e.get_material_expression_input_names(dither)])))
