import json
from pathlib import Path
import unreal as u

path = '/Game/Gameplay/UI/Materials/M_UI_LEDGrid'
material = u.load_asset(path)
custom = next(x for x in u.MaterialEditingLibrary.get_material_expressions(material) if x.get_class().get_name() == 'MaterialExpressionCustom')
backup = Path(u.Paths.project_dir()).resolve() / 'Saved/GameplayUI/led_ab_original.json'
assert not backup.exists(), 'Preserve original backup; restore before repeating A/B'
backup.write_text(json.dumps({'material': path, 'node': custom.get_name(), 'code': custom.get_editor_property('code')}, ensure_ascii=False, indent=2), encoding='utf-8')
custom.set_editor_property('code', 'return float4(1,0,0,1);')
u.MaterialEditingLibrary.recompile_material(material)
u.log('LED_AB_CONSTANT_SET_UNSAVED ' + str(backup))
