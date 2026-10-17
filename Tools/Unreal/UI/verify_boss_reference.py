"""Render the actual reference WBP with dynamic health and viewport anchors."""
from pathlib import Path
import json
import unreal as u

ROOT = Path(u.Paths.project_dir()).resolve()
OUT = ROOT / 'Saved/GameplayUI/BossReference'
OUT.mkdir(parents=True, exist_ok=True)
assert not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor()
api = u.MCGameplayUIEditorLibrary
report = []
for name, state, culture, width, height in (
    ('WBP_BossHealth_Reference','OneBoss','ru',900,280),
    ('WBP_BossHealth_Reference','TwoBosses','ru',900,280),
    ('WBP_BossHealth_Reference','ThreeBosses','ru',900,280),
    ('WBP_BossHealth_Reference','ZeroHealth','ru',900,280),
    ('WBP_BossHealth_Reference','FullHealth','ru',900,280),
    ('WBP_BossHealth_Reference','BrokenShield','ru',900,280),
    ('WBP_BossHealth_Reference','ShieldedLeader','ru',900,280),
    ('WBP_BossHealth_Reference','LongText','ru',900,280),
    ('WBP_BossHealth_Reference','ThreeBosses','en',900,280),
    ('WBP_GameplayHUD_v10','OneBoss','ru',1920,1080),
    ('WBP_GameplayHUD_v10','TwoBosses','ru',1920,1080),
    ('WBP_GameplayHUD_v10','ThreeBosses','ru',1920,1080),
    ('WBP_GameplayHUD_v10','ThreeBosses','ru',1280,720),
    ('WBP_GameplayHUD_v10','LongText','ru',2560,1080),
):
    cls = u.load_class(None, '/Game/Gameplay/UI/' + name + '.' + name + '_C')
    output = OUT / (name + '_' + state + '_' + culture + '_' + str(width) + '.png')
    ok = api.render_gameplay_ui_preview(cls, str(output), u.IntPoint(width,height), state,culture,1.)
    report.append({'widget':name,'state':state,'culture':culture,'size':[width,height],'path':str(output),'ok':ok})
    (OUT / 'render_report.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    assert ok, output
u.log('MC_BOSS_REFERENCE_RENDERS_PASS ' + str(len(report)))
