"""Connect all nine fresh Wizard performances against the production skeleton.

Only MCStyleV6 sequences and DA_NutRain are saved. Cast markers keep the new
hand staging free of legacy procedural IK. The shared V4 importer preserves
old art, Tank bindings, gameplay timing and unrelated unsaved packages.
"""
import importlib.util
from pathlib import Path
import unreal as u

ROOT = Path(u.Paths.project_dir()).resolve()
loader = importlib.util.spec_from_file_location(
    'mc_wizard_fresh_v6_import', ROOT / 'Tools/Unreal/integrate_nut_wizard_casts_v4.py')
h = importlib.util.module_from_spec(loader)
loader.loader.exec_module(h)
h.SPEC = dict(h.qa.ROLE_SPECS[1])
h.helpers.SPEC = h.SPEC
h.qa.EXPORT_ROOT = ROOT / 'ArtSource/NutAnimationStyle/ExportsV6'
h.qa.OWNER_TAG = 'MCWizardFreshV6'
h.qa.OWNER_VALUE = 'integrate_nut_wizard_fresh_v6.py:1'
h.DEST = '/Game/Gameplay/CoreLoop/NutWizard/Animations/MCStyleV6'
h.REPORT = ROOT / 'Saved/Checks/WizardFreshV6Integration.json'
h.SOURCE_FILENAME = 'NutWizard_Fresh_v6.blend'
h.AUTHOR_REPORT = ROOT / 'ArtSource/NutAnimationStyle/Wizard/WizardFreshV6Report.json'
h.REVISION_LABEL = 'V6'
h.main()
