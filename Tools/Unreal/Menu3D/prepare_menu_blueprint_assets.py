"""Create only task-owned menu UI/controller assets and inspect graph APIs."""
from pathlib import Path
import json

from menu_native_client import call, ref, BASE, UMG, BP, ASSET

OUT = Path(__file__).resolve().parent / 'Evidence'
OUT.mkdir(exist_ok=True)
if call('EditorToolset.EditorAppToolset', 'IsPIERunning'):
    raise RuntimeError('Preserve running PIE; no task asset authoring')
assets = {}
for name, parent, widget in [('WBP_MainMenu3D', '/Script/UMG.UserWidget', True),
                              ('BP_Menu3DPlayerController', '/Script/Engine.PlayerController', False),
                              ('BP_Menu3DGameMode', '/Script/Engine.GameModeBase', False)]:
    path = BASE + '/' + name
    if call(ASSET, 'exists', path=path):
        raise RuntimeError('Task asset already exists; inspect it before continuing: ' + path)
    if widget:
        asset = call(UMG, 'CreateWidgetBlueprint', folderPath=BASE, assetName=name, parentClass=ref(parent))
    else:
        asset = call(BP, 'create', folder_path=BASE, asset_name=name, asset_type=ref(parent))
    assets[name] = asset
    call(BP, 'compile_blueprint', blueprint=asset, warnings_as_errors=False)
    assets[name+'_graph'] = call(BP, 'get_graph', blueprint=asset, graph_name='EventGraph')
(OUT / 'created_assets.json').write_text(json.dumps(assets, indent=2), encoding='utf8')
docs = call(BP, 'get_graph_dsl_docs')
(OUT / 'graph_dsl_reference.txt').write_text(docs, encoding='utf8')
queries = ['CreateWidget', 'AddtoViewport', 'SetInputModeUIOnly', 'GetPlayerController', 'SetShowMouseCursor',
           'SetVisibility', 'SetActiveWidgetIndex', 'GetAllActorswithTag', 'SetMorphTarget', 'GetSkeletalMeshComponent',
           'GetComponentsbyClass', 'CreateDynamicMaterialInstance', 'SetScalarParameterValue', 'IsHovered',
           'GetScrollOffset', 'SetKeyboardFocus', 'GetGameUserSettings', 'QuitGame', 'OpenLevel', 'GetWorldDeltaSeconds']
catalog = {}
graph = assets['WBP_MainMenu3D_graph']
for query in queries:
    catalog[query] = call(BP, 'find_node_types', graph=graph, type_id_filter=query)
(OUT / 'node_catalog.json').write_text(json.dumps(catalog, indent=2), encoding='utf8')
print(json.dumps({'assets': assets, 'catalog': str(OUT / 'node_catalog.json')}, indent=2), flush=True)
