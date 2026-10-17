from pathlib import Path
import unreal as u
root=Path(u.Paths.project_dir()).resolve()
cls=u.load_class(None,'/Game/Gameplay/UI/WBP_BossHealth_v10.WBP_BossHealth_v10_C')
for state in ('OneBoss','TwoBosses','ThreeBosses'):
    output=root/'Saved/GameplayUI/Previews'/('BossPortraits_'+state+'_900.png')
    assert u.MCGameplayUIEditorLibrary.render_gameplay_ui_preview(cls,str(output),u.IntPoint(900,280),state,'ru',1.0)
u.log('MC_UI_BOSS_PORTRAIT_REVIEW_RENDERED')
