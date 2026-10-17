"""Repair interpolation in the eight remaining Tank slots, retaining their poses.

Walk/Melee/Hit stay on the new cartoon revision; Wizard and tuning remain intact.
"""
import importlib.util
from pathlib import Path
import unreal as u

ROOT = Path(u.Paths.project_dir()).resolve()
loader = importlib.util.spec_from_file_location('mc_tank_smooth_helpers', ROOT / 'Tools/Unreal/integrate_nut_tank_cartoon_v3.py')
helpers = importlib.util.module_from_spec(loader)
loader.loader.exec_module(helpers)
helpers.SPEC = dict(helpers.qa.ROLE_SPECS[0], source='TankSmoothV2',
                   clips=tuple(pair for pair in helpers.qa.ROLE_SPECS[0]['clips'] if pair[1] not in ('Walk', 'Melee')))
helpers.DEST = '/Game/Gameplay/CoreLoop/NutTank/Animations/MCSmoothV2'
helpers.REPORT = ROOT / 'Saved/Checks/TankSmoothV2Integration.json'
helpers.MARK_AUTHORED = False
helpers.qa.OWNER_TAG, helpers.qa.OWNER_VALUE = 'MCTankSmoothV2', 'integrate_nut_tank_smooth_v2.py:1'

if __name__ == '__main__':
    helpers.main()
