"""Assemble existing lower-row proof without touching Unreal or DCC state."""
import hashlib
import json
from pathlib import Path

root=Path(__file__).resolve().parents[3]
source=root/'ArtSource/UI/MainMenu3D/20261015_refB_v1/Assembly/UnrealHandoff/v07_LowerRowReadability'
review=root/'ArtSource/UI/MainMenu3D/20261015_refB_v1/Assembly/Review/Unreal_FunctionalBlocking_v03_FullRoute'
state=json.loads((review/'LowerRow_v07_Run04/phase_state_validation.json').read_text(encoding='utf8'))
runtime=json.loads((review/'LowerRow_v07_Run05/runtime_restoration.json').read_text(encoding='utf8'))
video=json.loads((review/'LowerRow_v07_Run05/encoded_video_receipt.json').read_text(encoding='utf8'))
assert state['status']=='PASS' and runtime['status']=='NORMAL_INTERACTIVE_RUNTIME_RESTORED'
paths=[source/'MC_DAY03_UnrealMorphSource.blend',source/'SK_DAY03.fbx',
    root/'Content/UI/MainMenu3D/Meshes/SK_DAY03_LowerReadable_v07.uasset',
    root/'Content/UI/MainMenu3D/L_MainMenu3D.umap',root/'Content/UI/MainMenu3D/WBP_MainMenu3D.uasset']
report={'status':'LOWER_ROW_FIX_SAVED_NATIVE_REVIEW_PASS','stage':'Functional gray 3D menu blocking; final reference-quality art remains unapproved',
    'changes':['Symmetric32-crown DAY03 with8 matched pairs per arch and two upper fangs',
        'Rounded seated gums expose lower enamel from Closed through Open',
        'Lower jaw lifted0.8 source units times openness; Closed, upper, XY/UV/rig preserved',
        'Constant uniformXYZscale0.5376540384; original character and animation retained',
        'Cavity/tongue fit: childX0.90, verticalmask75%',
        'DAY03 cavity controls up18designpx, revealOpen3>0.98, Hidden layout while unrevealed'],
    'native_mesh':runtime['mesh'],'saved_map':'/Game/UI/MainMenu3D/L_MainMenu3D',
    'source_independent_qc':str(source/'QA_Independent/independent_qc_review.md'),
    'functional_native_route':{'folder':str(review/'LowerRow_v07_Run04'),'checks':len(state['checks']),'status':state['status']},
    'latest_first_reveal_check':{'folder':str(review/'LowerRow_v07_Run05'),'root_opened':['frame_0023.png','frame_0024.png','frame_0046.png'],
        'result':'Text alignment correct at first reveal; Back clears lower enamel; closing hides controls before overlap',
        'video':video},
    'interactive_runtime':runtime,
    'files':[{'path':str(p),'bytes':p.stat().st_size,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()} for p in paths],
    'limits':['Sampled real screenshots at approximately10fps do not establish every rendered frame or game FPS',
        'Controlled shared-function route; pointer hit delivery isolated only during capture and restored afterward; no new physical-wheel/keyboard claim',
        'Run01 raw mixed-phase FAIL and Run02 off-schedule selection retained as diagnostic evidence',
        'No startup integration, final materials, build/cook/package, multiplayer validation or new Git push performed']}
out=root/'Tools/Unreal/Menu3D/Evidence/final_lower_row_validation_v07.json'
out.write_text(json.dumps(report,indent=2),encoding='utf8')
print(json.dumps({'status':report['status'],'files':len(paths),'route_checks':len(state['checks']),'video':video['video']},indent=2))
