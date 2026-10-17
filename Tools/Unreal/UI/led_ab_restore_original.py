import json
from pathlib import Path
import unreal as u

backup = Path(u.Paths.project_dir()).resolve() / 'Saved/GameplayUI/led_ab_original.json'
data = json.loads(backup.read_text(encoding='utf-8'))
material = u.load_asset(data['material'])
custom = next(x for x in u.MaterialEditingLibrary.get_material_expressions(material) if x.get_name() == data['node'])
custom.set_editor_property('code', data['code'])
u.MaterialEditingLibrary.recompile_material(material)
assert custom.get_editor_property('code') == data['code']
u.log('LED_AB_ORIGINAL_RESTORED_UNSAVED ' + str(backup))
