"""Audit the legacy eight-slot HUD without changing physical arena tooth count.

InitialArenaTeeth controls gameplay actors, not HUD layout. Never reduce it to
match the number of authored widgets: that removes teeth from the arena.
"""
import json
from pathlib import Path
import unreal as u
assert not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor()
root=Path(u.Paths.project_dir()).resolve()
asset=u.load_asset('/Game/Data/DA_RunRules')
settings=asset.get_editor_property('settings')
count=settings.get_editor_property('initial_arena_teeth')
report=root/'Saved/GameplayUI/reserve_teeth_config_audit.json'
report.parent.mkdir(parents=True,exist_ok=True)
report.write_text(json.dumps({'asset':asset.get_path_name(),'initial_arena_teeth':count,'hud_icon_slots':8,'changed':False},ensure_ascii=False,indent=2),encoding='utf-8')
u.log('MC_UI_RESERVE_PROFILE_COUNT preserved: '+str(count)+' (HUD slots: 8)')
