"""Connect four larger Wizard performances without changing combat timing."""
import importlib.util
from pathlib import Path
import unreal as u

ROOT = Path(u.Paths.project_dir()).resolve()
loader = importlib.util.spec_from_file_location('mc_wizard_v5_import_helpers', ROOT / 'Tools/Unreal/integrate_nut_wizard_casts_v4.py')
h = importlib.util.module_from_spec(loader)
loader.loader.exec_module(h)
h.SPEC = dict(h.SPEC, clips=tuple(pair for pair in h.SPEC['clips'] if pair[1] != 'Idle'))
h.helpers.SPEC = h.SPEC
h.qa.EXPORT_ROOT = ROOT / 'ArtSource/NutAnimationStyle/ExportsV5'
h.qa.OWNER_TAG, h.qa.OWNER_VALUE = 'MCWizardCastsV5', 'integrate_nut_wizard_casts_v5.py:1'
h.DEST = '/Game/Gameplay/CoreLoop/NutWizard/Animations/MCStyleV5'
h.REPORT = ROOT / 'Saved/Checks/WizardCastsV5Integration.json'
h.SOURCE_FILENAME = 'NutWizard_Casts_v5.blend'
h.AUTHOR_REPORT = ROOT / 'ArtSource/NutAnimationStyle/Wizard/WizardCastsV5Report.json'
h.REVISION_LABEL = 'V5'

if __name__ == '__main__':
    h.main()
