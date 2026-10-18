"""Offline evidence receipt for the completed non-boss UI polish pass."""
import ast
import hashlib
import json
from datetime import datetime,timezone
from pathlib import Path

root=Path(__file__).resolve().parents[3]
assert root.drive.upper()=='E:'
out=root/'Saved/GameplayUI/Polish'
matrix=json.loads((out/'Previews/report.json').read_text(encoding='utf-8'))
assert matrix['counts']==dict(exported=18,failed=0,unsupported=0,pending=0)
assert all(matrix[key] for key in ('theme_restored','fonts_unchanged','saved_packages_unchanged','original_tables_unchanged','culture_restored'))
for entry in matrix['renders']:
    assert hashlib.sha256(Path(entry['output']).read_bytes()).hexdigest()==entry['sha256'],entry['id']
completion=json.loads((out/'Completion/report.json').read_text(encoding='utf-8'))
assert len(completion)==4 and all(Path(item['path']).is_file() for item in completion)
boss=json.loads((root/'Saved/GameplayUI/Phase1/boss_baseline.json').read_text(encoding='utf-8-sig'))
assert all(hashlib.sha256(Path(item['Path']).read_bytes()).hexdigest().upper()==item['Hash'] for item in boss)
build=(out/'EditorBuild-pass37.log').read_text(encoding='utf-8-sig')
assert 'Result: Succeeded' in build
log=(out/'Editor-pass37.log').read_text(encoding='utf-8-sig')
assert 'Failed to compile Material' not in log and 'LogShaderCompilers: Error' not in log
alpha=json.loads((out/'AlphaFill/pixel_validation.json').read_text())
assert alpha['zero_alpha_empty'] and alpha['max_alpha']=={'low':89,'normal':89}
states=json.loads((out/'PIE/runtime_states.json').read_text())
assert states[-1]['cursor'] is False and not states[-1]['movement_ignored'] and not states[-1]['look_ignored']
assert states[-1]['reserve_count']==8
for name in ('author_ui_polish_font.py','repair_ui_polish_font_fallback.py','verify_ui_polish.py',
             'verify_ui_polish_alpha.py','verify_ui_polish_completion.py','make_ui_polish_review.py'):
    ast.parse((Path(__file__).parent/name).read_text(encoding='utf-8-sig'))
receipt=dict(status='ready_for_user_visual_review',utc=datetime.now(timezone.utc).isoformat(),
             build='EditorBuild-pass37',targeted_pngs=18,additional_unique_completion_pngs=3,
             matrix_hashes_match=True,boss_frozen_files_checked=len(boss),boss_frozen_files_unchanged=True,
             authored_tables_unchanged=True,temporary_copy_restored=True,alpha_fill_pixels=alpha,
             final_input_released=True,review_sheet=str(out/'ReviewSheet.png'),
             solo_evidence=['scroll_end_real_slate_drag','keyboard_choice','mouse_card_header_choice',
                            'pause_settings_keyboard_route','tab_create_release','emotes_T_Three_T','connection_F2_open_close'],
             deferred=['user visual approval','boss Phase2','shipping translations','64 unique card illustrations',
                       'physical gamepad','exhaustive mouse hit regions','full live tutorial flow','cook/package/network'])
(out/'completion_report.json').write_text(json.dumps(receipt,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps({key:receipt[key] for key in ('status','build','targeted_pngs','additional_unique_completion_pngs','boss_frozen_files_checked')},ensure_ascii=True))
