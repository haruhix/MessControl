"""Connect the wider tank melee to DA_NutRain; retain all other bindings."""
import importlib.util
from pathlib import Path
import unreal as u

ROOT = Path(u.Paths.project_dir()).resolve()
loader = importlib.util.spec_from_file_location('mc_tank_v5_import_helpers', ROOT / 'Tools/Unreal/integrate_nut_tank_cartoon_v3.py')
h = importlib.util.module_from_spec(loader)
loader.loader.exec_module(h)
h.SPEC = dict(h.qa.ROLE_SPECS[0], source='TankCartoonV5', clips=(('melee', 'Melee'),))
h.DEST = '/Game/Gameplay/CoreLoop/NutTank/Animations/MCStyleV5'
h.REPORT = ROOT / 'Saved/Checks/TankCartoonV5Integration.json'
h.qa.OWNER_TAG, h.qa.OWNER_VALUE = 'MCTankCartoonV5', 'integrate_nut_tank_cartoon_v5.py:1'

if __name__ == '__main__':
    h.main()
