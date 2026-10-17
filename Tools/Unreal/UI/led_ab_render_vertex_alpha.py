import unreal as u
from pathlib import Path

file = Path(u.Paths.project_dir()).resolve() / 'Saved/GameplayUI/Previews/LED_AB_vertex_alpha.png'
assert u.MCGameplayUIEditorLibrary.render_gameplay_ui_preview(u.MCBossHealthWidget, str(file), u.IntPoint(1920,1080), 'ThreeBosses', 'ru', 1.0)
u.log('LED_AB_VERTEX_ALPHA_RENDER ' + str(file))
