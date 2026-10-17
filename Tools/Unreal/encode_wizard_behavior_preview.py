"""Preview walking and rain; the far-blink follow camera can hit the mouth wall."""
from pathlib import Path
base=Path(__file__).with_name('encode_nut_wizard_preview.py')
code=base.read_text().replace('Artifacts/NutWizardUpdate_20261017','Artifacts/WizardBehaviorFix_20261017')
code=code.replace("frames=data['captures']","frames=[row for row in data['captures'] if row['case'] in ('walk','rain')]")
exec(compile(code,str(base),'exec'))
