"""Render a small first-look subset without changing the full QA manifest."""
import os
import runpy
from pathlib import Path

old = os.environ.get('MC_PHASE1_VERIFY_ONLY')
try:
    os.environ['MC_PHASE1_VERIFY_ONLY'] = 'GameplayHUD_Default,PauseMenu_Default,PerkChoice_Risky'
    runpy.run_path(str(Path(__file__).with_name('verify_phase1_ui.py')), run_name='__main__')
finally:
    if old is None:
        os.environ.pop('MC_PHASE1_VERIFY_ONLY', None)
    else:
        os.environ['MC_PHASE1_VERIFY_ONLY'] = old
