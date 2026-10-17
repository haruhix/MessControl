import unreal as u
from pathlib import Path

material = u.load_asset('/Game/Gameplay/UI/Materials/M_UI_LEDGrid')
u.log('LED_AB_CONSTANT_STATS ' + str(u.MaterialEditingLibrary.get_statistics(material)))
file = Path(u.Paths.project_dir()).resolve() / 'Saved/GameplayUI/Previews/LED_AB_constant.png'
ok = u.MCGameplayUIEditorLibrary.render_gameplay_ui_preview(u.MCBossHealthWidget, str(file), u.IntPoint(1920,1080), 'ThreeBosses', 'ru', 1.0)
assert ok
u.log('LED_AB_CONSTANT_RENDER ' + str(file))
