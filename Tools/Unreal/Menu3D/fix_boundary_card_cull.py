"""Bounded saved-WBP fix. Run only after root confirms PIE is stopped.

No level/input/PIE/Git operations. The seven-day input/scroll graphs are read
before and after to prove only DriveMouthDay was authored by this tool.
"""
from pathlib import Path
import hashlib
import json
from menu_native_client import call, task_asset, BP, ASSET, BASE

ROOT=Path(__file__).resolve().parents[3]
OUT=Path(__file__).resolve().parent/'Evidence'
w=task_asset('WBP_MainMenu3D')
protected=['UpdateStage','EventGraph','CenterSelectedDay','SelectDay','StepDay',
           'OnMouseWheel','OnPreviewKeyDown','OnMouseMove','UpdatePointerIntent',
           'ResetPointerIntent','OpenConnection','Back']

def graph(name):
    return call(BP,'get_graph',blueprint=w,graph_name=name)

def digest(code):
    return hashlib.sha256(code.encode('utf8')).hexdigest()

before={name:digest(call(BP,'read_graph_dsl',graph=graph(name))) for name in protected}
pc_path=ROOT/'Content/UI/MainMenu3D/BP_Menu3DPlayerController.uasset'
pc_before=hashlib.sha256(pc_path.read_bytes()).hexdigest()
source=OUT/'accordion_DriveMouthDay.dsl'
code=source.read_text(encoding='utf8')
if '(bind effectiveHidden ' not in code:
    signature='(fn DriveMouthDay (Day Open Height Center Hidden Scale)'
    code=code.replace(signature,signature+'''
  (bind isStage (or (== (Variables|Default|GetPageIndex) 1) (== (Variables|Default|GetPageIndex) 5)))
  (bind effectiveHidden (or Hidden (and isStage (or (<= (+ Center (* Height 0.5)) 80.5) (>= (- Center (* Height 0.5)) 819.5)))))''',1)
    code=code.replace(':bNewHidden Hidden)',':bNewHidden effectiveHidden)',1)
    needle='(:then (bind mesh (Class|SkeletalMeshActor|GetSkeletalMeshComponent :self sk))'
    assert needle in code
    code=code.replace(needle,needle+'''
        (if (!= (Class|SceneComponent|GetHiddeninGame :self mesh) effectiveHidden) (Development|SetHiddeninGame :self mesh :NewHidden effectiveHidden :bPropagateToChildren true))''',1)
assert code.count('(bind effectiveHidden ')==1
source.write_text(code,encoding='utf8')
call(BP,'write_graph_dsl',graph=graph('DriveMouthDay'),code=code)
call(BP,'compile_blueprint',blueprint=w,warnings_as_errors=True)
after={name:digest(call(BP,'read_graph_dsl',graph=graph(name))) for name in protected}
assert before==after, 'A protected graph changed; do not save.'
assert hashlib.sha256(pc_path.read_bytes()).hexdigest()==pc_before
call(ASSET,'save_assets',asset_paths=[BASE+'/WBP_MainMenu3D'])
asset_path=ROOT/'Content/UI/MainMenu3D/WBP_MainMenu3D.uasset'
receipt={
    'status':'BOUNDARY_CULL_COMPILED_SAVED_PENDING_ROOT_PIE',
    'asset':BASE+'/WBP_MainMenu3D',
    'only_authored_graph':'DriveMouthDay',
    'viewport_design_y':[80,820],
    'epsilon_design_pixels':0.5,
    'cull':'Page1 OR5: rowBottom<=80.5 OR rowTop>=819.5, OR existing Hidden',
    'attached_components':'Mesh hidden-in-game propagates to attached roots on state change only; existing later ActorHidden threshold gates retained.',
    'avoids_per_frame_render_state_dirty':'Propagation only when root component hidden flag differs.',
    'protected_graph_hashes_unchanged':after,
    'pc_asset_sha256_unchanged':pc_before,
    'compiled_warnings_as_errors':True,
    'saved_asset_sha256':hashlib.sha256(asset_path.read_bytes()).hexdigest(),
    'saved_asset_bytes':asset_path.stat().st_size,
    'PIE_input_level_Git_operations':False,
    'validation':'Actual Blueprint compile and save only; root owns next actual runtime check.'
}
(OUT/'boundary_card_cull_compile_report.json').write_text(json.dumps(receipt,indent=2),encoding='utf8')
print(json.dumps(receipt,indent=2))
