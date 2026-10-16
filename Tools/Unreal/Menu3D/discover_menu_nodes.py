"""Read task graph action APIs; writes CPU documentation only."""
from pathlib import Path
import json
from menu_native_client import call, BP

OUT = Path(__file__).resolve().parent / 'Evidence'
assets = json.loads((OUT / 'created_assets.json').read_text())
queries = ['CreateWidget', 'AddtoViewport', 'SetInputModeUIOnly', 'GetPlayerController', 'SetShowMouseCursor',
           'SetVisibility', 'SetActiveWidgetIndex', 'GetAllActorswithTag', 'SetMorphTarget', 'GetSkeletalMeshComponent',
           'GetComponentbyClass', 'SetScalarParameterValueOnMaterials', 'IsHovered', 'GetScrollOffset', 'SetKeyboardFocus',
           'GetGameUserSettings', 'QuitGame', 'OpenLevel', 'FInterpTo', 'GetWorldDeltaSeconds', 'SetHeightOverride', 'SetText',
           'SetActorLocation', 'SetActorScale3D', 'SetActorHiddenInGame', 'ScrollWidgetIntoView', 'GetOwningPlayer', 'AddChildtoOverlay']
catalog = {}
for query in queries:
    catalog[query] = call(BP, 'find_node_types', graph=assets['WBP_MainMenu3D_graph'], type_id_filter=query, context_pins=[])
(OUT / 'node_catalog.json').write_text(json.dumps(catalog, indent=2), encoding='utf8')
print(json.dumps(catalog, indent=2), flush=True)
