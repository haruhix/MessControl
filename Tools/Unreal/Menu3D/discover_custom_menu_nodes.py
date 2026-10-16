from pathlib import Path
import json
from menu_native_client import call, task_asset, BP
OUT=Path(__file__).resolve().parent/'Evidence'
graph=call(BP,'get_graph',blueprint=task_asset('WBP_MainMenu3D'),graph_name='EventGraph')
queries=['ShowPage','SelectDay','OpenConnection','UpdateStage','FindStageActors','Variables|Default|GetPageIndex','Input|Key|','EqualExactly','RemovefromParent','SetActorHidden','GetActorLocation']
cat={q:call(BP,'find_node_types',graph=graph,type_id_filter=q,context_pins=[]) for q in queries}
(OUT/'custom_node_catalog.json').write_text(json.dumps(cat,indent=2),encoding='utf8')
print(json.dumps(cat,indent=2),flush=True)
